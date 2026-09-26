#include "Editor.hpp"
#include "Engine.hpp"
#include <numbers>
#include <openfilter/ui/Brand.hpp>
#include <openfilter/ui/Meters.hpp>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>

namespace openfilter::gate {
using namespace ui;
namespace {
const auto accent = hex(0xd8cf79), ink = theme::ink, muted = theme::muted, dim = theme::dim;
const auto teal = hex(0x7ac4b3), red = hex(0xe48f85);
std::string label(unsigned i, double value) {
    if ((i == SidechainHP && value == 0) || (i == SidechainLP && value == 20000))
        return "Off";
    char s[80];
    format(i, value, s, sizeof(s));
    return (value >= 0 && (i == Input || i == Output || i == WetGain) ? "+" : "") + std::string(s);
}
std::string number(double v) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.1f", v);
    return s;
}
double db(double v) {
    return 20 * std::log10(std::max(1e-4, v));
}
constexpr unsigned mainControls[]{Threshold, Ratio, Range, Attack, Release, Hold, Knee, Lookahead};
} // namespace
void Editor::drawButton(cairo_t *cr, Rect r, std::string_view s, bool active, bool enabled) {
    theme::button(cr, r, active, enabled && r.contains(mouseX_, mouseY_));
    const bool dropdown = s.ends_with(" v");
    if (dropdown)
        s.remove_suffix(2);
    auto labelBounds = r;
    if (dropdown)
        labelBounds.w -= 12;
    centeredText(cr, s, labelBounds, 12, enabled ? (active ? accent : ink) : dim);
    if (dropdown) {
        const double x = r.x + r.w - 13, y = r.y + r.h / 2;
        line(cr, x - 3, y - 1.5, x, y + 1.5, muted, 1.2);
        line(cr, x, y + 1.5, x + 3, y - 1.5, muted, 1.2);
    }
}
Editor::Editor(Read read, Send send, MeterTap &tap)
    : read_(std::move(read)), send_(std::move(send)), analysis_(tap) {
    model_ = read_();
    comparisons_[0] = comparisons_[1] = model_.values;
}
Editor::~Editor() {
    finishGesture();
    analysis_.enabled.store(false, std::memory_order_relaxed);
    raster_.reset();
    if (view_)
        puglFreeView(view_);
    if (world_)
        puglFreeWorld(world_);
}
bool Editor::attach(uintptr_t parent) {
    if (world_ || !parent)
        return false;
    world_ = puglNewWorld(PUGL_MODULE, 0);
    if (!world_)
        return false;
    view_ = puglNewView(world_);
    if (!view_)
        return false;
    puglSetHandle(view_, this);
    puglSetEventFunc(view_, event);
    puglSetBackend(view_, puglStubBackend());
    puglSetViewHint(view_, PUGL_RESIZABLE, PUGL_TRUE);
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter Gate");
    puglSetSizeHint(view_, PUGL_DEFAULT_SIZE, width_, height_);
    puglSetSizeHint(view_, PUGL_MIN_SIZE, static_cast<unsigned>(900 * scale_),
                    static_cast<unsigned>(600 * scale_));
    puglSetParent(view_, parent);
    realized_ = puglRealize(view_) == PUGL_SUCCESS;
    return realized_;
}
bool Editor::show() {
    if (!realized_)
        return false;
    visible_ = puglShow(view_, PUGL_SHOW_PASSIVE) == PUGL_SUCCESS;
    analysis_.enabled.store(visible_, std::memory_order_relaxed);
    invalidate();
    return visible_;
}
void Editor::hide() {
    finishGesture();
    cancelText();
    visible_ = false;
    analysis_.enabled.store(false, std::memory_order_relaxed);
    if (view_)
        puglHide(view_);
}
bool Editor::resize(unsigned w, unsigned h) {
    if (w < 900 * scale_ || h < 600 * scale_ || w > 2400 * scale_ || h > 1400 * scale_)
        return false;
    width_ = w;
    height_ = h;
    logicalWidth_ = w / scale_;
    logicalHeight_ = h / scale_;
    if (view_)
        puglSetSizeHint(view_, realized_ ? PUGL_CURRENT_SIZE : PUGL_DEFAULT_SIZE, w, h);
    invalidate();
    return true;
}
bool Editor::setScale(double s) {
    if (!std::isfinite(s) || s < .75 || s > 3)
        return false;
    const double ratio = s / scale_;
    scale_ = s;
    if (view_)
        puglSetSizeHint(view_, PUGL_MIN_SIZE, static_cast<unsigned>(900 * s),
                        static_cast<unsigned>(600 * s));
    return resize(static_cast<unsigned>(std::round(width_ * ratio)),
                  static_cast<unsigned>(std::round(height_ * ratio)));
}
uintptr_t Editor::nativeWindow() const {
    return view_ ? puglGetNativeView(view_) : 0;
}
void Editor::invalidate() {
    if (view_ && realized_)
        puglObscureView(view_);
}
PuglStatus Editor::event(PuglView *view, const PuglEvent *e) {
    auto &self = *static_cast<Editor *>(puglGetHandle(view));
    try {
        switch (e->type) {
        case PUGL_EXPOSE: {
            if (!self.raster_)
                self.raster_ = std::make_unique<ui::X11Raster>(
                    static_cast<Display *>(puglGetNativeWorld(self.world_)), self.nativeWindow());
            auto *surface = self.raster_->surface(self.width_, self.height_);
            if (!surface)
                return PUGL_NO_MEMORY;
            auto *cr = cairo_create(surface);
            self.paint(cr, self.width_ / self.scale_, self.height_ / self.scale_);
            cairo_destroy(cr);
            self.raster_->present();
            break;
        }
        case PUGL_CONFIGURE:
            self.width_ = e->configure.width;
            self.height_ = e->configure.height;
            self.logicalWidth_ = self.width_ / self.scale_;
            self.logicalHeight_ = self.height_ / self.scale_;
            break;
        case PUGL_BUTTON_PRESS:
            puglGrabFocus(view);
            self.press(e->button.x / self.scale_, e->button.y / self.scale_, e->button.button,
                       e->button.state, e->button.time);
            break;
        case PUGL_BUTTON_RELEASE:
            self.release(e->button.x / self.scale_, e->button.y / self.scale_);
            break;
        case PUGL_MOTION:
            self.motion(e->motion.x / self.scale_, e->motion.y / self.scale_, e->motion.state);
            break;
        case PUGL_SCROLL:
            self.scroll(e->scroll.x / self.scale_, e->scroll.y / self.scale_, e->scroll.dy,
                        e->scroll.state);
            break;
        case PUGL_KEY_PRESS:
            self.key(e->key.key, e->key.state);
            break;
        case PUGL_TEXT:
            self.input(e->text.string);
            break;
        case PUGL_FOCUS_OUT:
            self.finishGesture();
            self.commitText();
            break;
        default:
            break;
        }
    } catch (...) {
        return PUGL_FAILURE;
    }
    return PUGL_SUCCESS;
}
void Editor::tick() {
    if (!visible_ && world_) {
        puglUpdate(world_, 0);
        return;
    }
    auto next = read_();
    if (next.stateSerial != model_.stateSerial) {
        finishGesture();
        cancelText();
        undo_.clear();
        redo_.clear();
        comparisons_[0] = comparisons_[1] = next.values;
        comparison_ = 0;
        menu_ = -1;
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::min(.25, std::chrono::duration<double>(now - lastTick_).count());
    lastTick_ = now;
    MeterFrame f;
    bool fresh = false;
    for (unsigned n = 0; n < 1024 && analysis_.frames.pop(f); ++n) {
        history_[historyPosition_] = f;
        historyPosition_ = (historyPosition_ + 1) % history_.size();
        fresh = true;
    }
    if (fresh)
        lastAudio_ = now;
    else if (std::chrono::duration<double>(now - lastAudio_).count() > .25) {
        for (unsigned c = 0; c < 4; ++c)
            next.peaks[c] = model_.peaks[c] * std::exp(-elapsed / .4);
        next.reduction = model_.reduction * std::exp(-elapsed / .4);
    }
    model_ = next;
    invalidate();
    if (world_)
        puglUpdate(world_, 0);
}
Rect Editor::headerBounds(unsigned i) const {
    if (i == 0)
        return {logicalWidth_ - 584, 16, 132, 28};
    return {logicalWidth_ - 440 + (i - 1) * 60, 16, 54, 28};
}
double Editor::workspaceBottom() const {
    return logicalHeight_ - 44 - (sidechainVisible_ ? 200 : 0);
}
Rect Editor::graphBounds() const {
    return {logicalWidth_ * .265, 110, logicalWidth_ * .355, workspaceBottom() - 170};
}
Rect Editor::viewBounds(unsigned i) const {
    const auto g = graphBounds();
    if (i == 0)
        return {g.x, 74, 86, 26};
    return {g.x + g.w / 2 - 62, workspaceBottom() - 31, 124, 26};
}
bool Editor::available(unsigned i) const {
    for (auto main : mainControls)
        if (i == main)
            return true;
    return sidechainVisible_ || i == Bypass || i == Output || i == Detector;
}
Rect Editor::controlBounds(unsigned i) const {
    const double w = logicalWidth_, bottom = workspaceBottom();
    const double h = bottom - 70;
    const double factor = std::min(1., h / 400.);
    auto dial = [&](double x, double y, double size) -> Rect {
        const double width = size * factor;
        return {w * x - width / 2, y, width, width + 34};
    };
    if (i == Threshold)
        return dial(.13, 85, 152);
    if (i == Ratio)
        return dial(.067, bottom - 130 * factor - 15, 88);
    if (i == Range)
        return dial(.202, bottom - 130 * factor - 15, 88);
    if (i == Attack)
        return dial(.791, 122, 100);
    if (i == Release)
        return dial(.791, bottom - 145 * factor - 10, 100);
    if (i == Hold)
        return dial(.932, 82, 76);
    if (i == Knee)
        return dial(.932, 82 + h * .32, 76);
    if (i == Lookahead)
        return dial(.932, bottom - 121 * factor - 9, 76);
    if (i == Detector)
        return {w * .791 - 53, 77, 106, 28};
    if (i == Input || i == Mix || i == WetGain || i == DryGain) {
        const double x = i == Input ? .072 : i == Mix ? .229 : i == WetGain ? .79 : .932;
        return {w * x - 45, bottom + 33, 90, 122};
    }
    if (i == Hysteresis)
        return {18, bottom + 164, w * .15, 28};
    if (i == StereoLink)
        return {w * .175, bottom + 164, w * .15, 28};
    if (i == WetPan)
        return {w * .715, bottom + 164, w * .135, 28};
    if (i == DryPan)
        return {w * .865, bottom + 164, w * .12 - 8, 28};
    if (i == Sidechain)
        return {w * .465, bottom + 17, w * .19, 28};
    if (i == SidechainHP)
        return {w * .345, bottom + 66, w * .145, 54};
    if (i == SidechainLP)
        return {w * .515, bottom + 66, w * .145, 54};
    if (i == Audition)
        return {w * .445, bottom + 140, 116, 28};
    if (i == Output)
        return {w - 296, logicalHeight_ - 36, 132, 28};
    if (i == Bypass)
        return {w - 142, logicalHeight_ - 36, 118, 28};
    return {};
}
Rect Editor::menuBounds() const {
    const auto r = menu_ == 100 ? headerBounds(0) : controlBounds(menu_);
    return {r.x, menu_ == 100 ? r.y + r.h + 8 : r.y + r.h + 6, menu_ == 100 ? 200. : r.w,
            menu_ == 100 ? 180. : 72.};
}
int Editor::hit(double x, double y) const {
    for (unsigned i = 0; i < parameterCount; ++i)
        if (available(i) && controlBounds(i).contains(x, y))
            return i;
    return -1;
}
void Editor::begin(unsigned i) {
    finishGesture();
    before_ = model_.values;
    drag_ = i;
    send_(UiKind::Begin, i, 0);
}
void Editor::change(unsigned i, double value) {
    value = parameter(i).constrain(value);
    model_.values[i] = model_.effective[i] = value;
    send_(UiKind::Value, i, value);
    invalidate();
}
void Editor::finishGesture() {
    readout_.reset();
    if (drag_ < 0)
        return;
    send_(UiKind::End, drag_, 0);
    if (before_ != model_.values) {
        if (undo_.size() == 64)
            undo_.erase(undo_.begin());
        undo_.push_back(before_);
        redo_.clear();
    }
    drag_ = -1;
    graphDrag_ = false;
}
void Editor::once(unsigned i, double v) {
    begin(i);
    change(i, v);
    finishGesture();
}
void Editor::apply(const Values &v) {
    finishGesture();
    const auto old = model_.values;
    for (unsigned i = 0; i < parameterCount; ++i)
        if (v[i] != old[i]) {
            send_(UiKind::Begin, i, 0);
            change(i, v[i]);
            send_(UiKind::End, i, 0);
        }
    if (old != v) {
        if (undo_.size() == 64)
            undo_.erase(undo_.begin());
        undo_.push_back(old);
        redo_.clear();
    }
}
void Editor::undo(bool redo) {
    finishGesture();
    auto &from = redo ? redo_ : undo_;
    auto &to = redo ? undo_ : redo_;
    if (from.empty())
        return;
    const auto v = from.back();
    from.pop_back();
    to.push_back(model_.values);
    for (unsigned i = 0; i < parameterCount; ++i)
        if (v[i] != model_.values[i]) {
            send_(UiKind::Begin, i, 0);
            change(i, v[i]);
            send_(UiKind::End, i, 0);
        }
}
void Editor::textEdit(unsigned i) {
    finishGesture();
    editing_ = i;
    input_ = label(i, model_.values[i]);
    selectAll_ = true;
    inputError_ = false;
}
void Editor::cancelText() {
    editing_ = -1;
    input_.clear();
    inputError_ = false;
    invalidate();
}
void Editor::commitText() {
    if (editing_ < 0)
        return;
    double v;
    if (!parse(editing_, input_, v)) {
        inputError_ = true;
        invalidate();
        return;
    }
    once(editing_, v);
    cancelText();
}
void Editor::press(double x, double y, unsigned buttonId, unsigned mods, double time) {
    readout_.reset();
    mouseX_ = x;
    mouseY_ = y;
    const int target = hit(x, y);
    int viewTarget = -1;
    for (unsigned i = 0; i < 2; ++i)
        if (viewBounds(i).contains(x, y))
            viewTarget = i;
    const auto graph = graphBounds();
    const bool kneeTarget = target < 0 && kneeVisible_ && x >= graph.x && x < graph.x + graph.w &&
                            y >= graph.y && y < graph.y + graph.h;
    const bool twice = clicks_.press(viewTarget >= 0 ? int(parameterCount) + viewTarget
                                     : kneeTarget    ? int(Threshold)
                                                     : target,
                                     x, y, buttonId, time);
    if (twice && kneeTarget && viewTarget < 0) {
        cancelText();
        menu_ = -1;
        once(Threshold, parameter(Threshold).initial);
        return;
    }
    if (twice && target >= 0 && viewTarget < 0) {
        cancelText();
        menu_ = -1;
        once(target, parameter(target).initial);
        return;
    }
    if (menu_ >= 0) {
        const auto r = menuBounds();
        if (r.contains(x, y)) {
            const unsigned row = static_cast<unsigned>((y - r.y) / (menu_ == 100 ? 36 : 36));
            if (menu_ == 100) {
                auto v = defaults();
                if (row == 1) {
                    v[Detector] = 1;
                    v[Ratio] = 2;
                    v[Attack] = 5;
                    v[Release] = 250;
                    v[Knee] = 12;
                    v[Threshold] = -42;
                    v[Range] = 18;
                    v[Hold] = 60;
                    v[SidechainHP] = 80;
                }
                if (row == 2) {
                    v[Threshold] = -28;
                    v[Ratio] = 20;
                    v[Attack] = .2;
                    v[Release] = 90;
                    v[Hold] = 35;
                    v[Lookahead] = 2;
                    v[SidechainHP] = 120;
                    v[SidechainLP] = 8000;
                }
                if (row == 3) {
                    v[Threshold] = -38;
                    v[Ratio] = 4;
                    v[Attack] = 3;
                    v[Release] = 220;
                    v[Range] = 24;
                    v[Hysteresis] = 6;
                    v[SidechainHP] = 60;
                    v[Hold] = 80;
                }
                if (row == 4) {
                    v[Threshold] = -30;
                    v[Ratio] = 100;
                    v[Attack] = .5;
                    v[Release] = 120;
                    v[Sidechain] = 1;
                    v[Lookahead] = 5;
                }
                apply(v);
            } else
                once(menu_, std::min(row, 1u));
        }
        menu_ = -1;
        invalidate();
        return;
    }
    if (editing_ >= 0) {
        commitText();
        if (editing_ >= 0)
            return;
    }
    for (unsigned i = 0; i < 7; ++i)
        if (headerBounds(i).contains(x, y)) {
            finishGesture();
            if (i == 0)
                menu_ = 100;
            if (i == 1)
                undo(false);
            if (i == 2)
                undo(true);
            if (i == 3 || i == 4) {
                const int slot = i - 3;
                if (slot != comparison_) {
                    comparisons_[comparison_] = model_.values;
                    apply(comparisons_[slot]);
                    comparison_ = slot;
                }
            }
            if (i == 5)
                comparisons_[1 - comparison_] = model_.values;
            if (i == 6)
                help_ = !help_;
            invalidate();
            return;
        }
    if (help_) {
        help_ = false;
        invalidate();
        return;
    }
    if (viewTarget >= 0) {
        finishGesture();
        cancelText();
        if (viewTarget == 0)
            kneeVisible_ = twice ? true : !kneeVisible_;
        else {
            sidechainVisible_ = twice ? true : !sidechainVisible_;
            if (!available(focused_))
                focused_ = Threshold;
        }
        invalidate();
        return;
    }
    if (target >= 0) {
        focused_ = target;
        const auto r = controlBounds(target);
        if (buttonId != 0 || (mods & PUGL_MOD_CTRL)) {
            textEdit(target);
            return;
        }
        if (target == Detector || target == Sidechain) {
            menu_ = target;
            invalidate();
            return;
        }
        if (parameter(target).stepped) {
            once(target, 1 - model_.values[target]);
            return;
        }
        if ((!parameter(target).stepped && r.h >= 80 && y >= r.y + r.h - 25) ||
            (r.h == 54 && y < r.y + 25 && x > r.x + r.w * .38) || r.h == 28) {
            finishGesture();
            readout_.press(target, x, y);
            return;
        }
        begin(target);
        dragX_ = x;
        dragY_ = y;
        return;
    }
    const auto g = graphBounds();
    // The overlaid knee is interactive; controls intercept events before the canvas.
    if (kneeVisible_ && x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h) {
        focused_ = Threshold;
        begin(Threshold);
        graphDrag_ = true;
        motion(x, y, mods);
        return;
    }
    if (x > logicalWidth_ * .655 && x < logicalWidth_ * .72 && y > 70 && y < workspaceBottom())
        send_(UiKind::ClearClip, 0, 0);
}
void Editor::release(double, double) {
    const int edit = readout_.release();
    finishGesture();
    if (edit >= 0)
        textEdit(edit);
    invalidate();
}
void Editor::motion(double x, double y, unsigned mods) {
    mouseX_ = x;
    mouseY_ = y;
    clicks_.motion(x, y);
    if (readout_.motion(x, y, [&](unsigned i, double startX, double startY) {
            begin(i);
            dragX_ = startX;
            dragY_ = startY;
        })) {
        invalidate();
        return;
    }
    if (drag_ >= 0) {
        if (graphDrag_) {
            const auto g = graphBounds();
            change(Threshold, -80 + 80 * (x - g.x) / g.w);
        } else {
            const auto r = controlBounds(drag_);
            const double delta = !readout_.dragging() && r.h == 54 ? x - dragX_ : dragY_ - y;
            change(drag_,
                   denormalized(drag_, normalized(drag_, model_.values[drag_]) +
                                           delta * ((mods & PUGL_MOD_SHIFT) ? .0005 : .005)));
            dragX_ = x;
            dragY_ = y;
        }
    }
    invalidate();
}
void Editor::scroll(double x, double y, double delta, unsigned mods) {
    const int i = hit(x, y);
    if (i < 0 || menu_ >= 0 || editing_ >= 0)
        return;
    const auto p = parameter(i);
    once(i, p.stepped ? model_.values[i] + (delta > 0 ? 1 : -1)
                      : denormalized(i, normalized(i, model_.values[i]) +
                                            delta * ((mods & PUGL_MOD_SHIFT) ? .001 : .01)));
}
void Editor::key(uint32_t k, unsigned mods) {
    if (k == PUGL_KEY_ESCAPE) {
        cancelText();
        menu_ = -1;
        help_ = false;
        finishGesture();
        return;
    }
    if (editing_ >= 0) {
        if (k == PUGL_KEY_ENTER || k == '\r')
            commitText();
        else if (k == PUGL_KEY_BACKSPACE || k == 8) {
            if (selectAll_)
                input_.clear();
            else if (!input_.empty())
                input_.pop_back();
            selectAll_ = false;
            inputError_ = false;
        } else if ((mods & PUGL_MOD_CTRL) && (k == 'a' || k == 'A'))
            selectAll_ = true;
        invalidate();
        return;
    }
    if ((mods & PUGL_MOD_CTRL) && (k == 'z' || k == 'Z')) {
        undo(mods & PUGL_MOD_SHIFT);
        return;
    }
    if (k == PUGL_KEY_TAB || k == '\t') {
        do {
            focused_ =
                (focused_ + ((mods & PUGL_MOD_SHIFT) ? parameterCount - 1 : 1)) % parameterCount;
        } while (!available(focused_));
        invalidate();
        return;
    }
    if (k == PUGL_KEY_ENTER || k == '\r') {
        textEdit(focused_);
        return;
    }
    if (k == PUGL_KEY_UP || k == PUGL_KEY_RIGHT || k == PUGL_KEY_DOWN || k == PUGL_KEY_LEFT) {
        const auto r = controlBounds(focused_);
        scroll(r.x + 1, r.y + 1, (k == PUGL_KEY_UP || k == PUGL_KEY_RIGHT) ? 1 : -1, mods);
    }
}
void Editor::input(std::string_view s) {
    if (editing_ < 0)
        return;
    if (selectAll_) {
        input_.clear();
        selectAll_ = false;
    }
    for (char c : s)
        if (c >= 32 && c < 127 && input_.size() < 48)
            input_.push_back(c);
    inputError_ = false;
    invalidate();
}
void Editor::drawGraph(cairo_t *cr) {
    const auto g = graphBounds();
    const auto inputTint = hex(0x89939d), outputTint = hex(0xa7b9c9);
    auto y = [&](double level) { return g.y + g.h * (1 - (level + 80) / 80.); };
    // Quiet horizontal guides leave the filled signal silhouettes and the
    // transfer curve as the main visual hierarchy. GR has its own meter.
    for (int d = -80; d <= 0; d += 10) {
        const double yy = y(d);
        line(cr, g.x, yy, g.x + g.w, yy, dim.alpha(d % 20 == 0 ? .13 : .06));
        text(cr, std::to_string(d), g.x + g.w + 6, yy + 3, 9, muted);
        if (kneeVisible_) {
            const double xx = g.x + g.w * (d + 80) / 80.;
            text(cr, std::to_string(d), xx, g.y + g.h + 16, 9, dim, false, 1);
        }
    }
    if (!kneeVisible_) {
        text(cr, "−6 s", g.x, g.y + g.h + 16, 9, dim);
        text(cr, "Now", g.x + g.w, g.y + g.h + 16, 9, dim, false, 2);
    }
    cairo_save(cr);
    cairo_rectangle(cr, g.x, g.y, g.w, g.h);
    cairo_clip(cr);
    // Both layers come directly from latency-aligned audio peaks. No display
    // smoothing changes transient height; fading is purely a fill treatment.
    for (unsigned kind = 0; kind < 2; ++kind) {
        cairo_new_path(cr);
        cairo_move_to(cr, g.x, g.y + g.h);
        for (unsigned n = 0; n < history_.size(); ++n) {
            const auto &f = history_[(historyPosition_ + n) % history_.size()];
            const double level = kind == 0 ? f.input : f.output;
            cairo_line_to(cr, g.x + g.w * n / (history_.size() - 1),
                          y(std::clamp(db(level), -80., 0.)));
        }
        cairo_line_to(cr, g.x + g.w, g.y + g.h);
        cairo_close_path(cr);
        const auto tint = kind == 0 ? inputTint : outputTint;
        auto *fill = cairo_pattern_create_linear(g.x, g.y + g.h, g.x + g.w * .65, g.y);
        theme::stop(fill, 0, tint.alpha(kind == 0 ? .025 : .035));
        theme::stop(fill, .45, tint.alpha(kind == 0 ? .12 : .22));
        theme::stop(fill, 1, tint.alpha(kind == 0 ? .28 : .62));
        cairo_set_source(cr, fill);
        cairo_fill(cr);
        cairo_pattern_destroy(fill);
    }
    if (kneeVisible_) {
        const auto &p = model_.effective;
        const double threshold = model_.values[Threshold];
        const double xx = g.x + g.w * (threshold + 80) / 80.;
        const double dash[]{4, 6};
        cairo_set_dash(cr, dash, 2, 0);
        line(cr, g.x, y(threshold), g.x + g.w, y(threshold), muted.alpha(.48));
        line(cr, xx, g.y, xx, g.y + g.h, muted.alpha(.48));
        cairo_set_dash(cr, nullptr, 0, 0);
        cairo_new_path(cr);
        for (unsigned n = 0; n <= 320; ++n) {
            const double input = -80 + n * .25;
            const double output =
                input - reduction(input, p[Threshold], p[Ratio], p[Knee], p[Range]);
            const double px = g.x + g.w * n / 320.;
            if (n == 0)
                cairo_move_to(cr, px, y(output));
            else
                cairo_line_to(cr, px, y(output));
        }
        color(cr, hex(0x101416, .45));
        cairo_set_line_width(cr, 5);
        cairo_stroke_preserve(cr);
        color(cr, ink);
        cairo_set_line_width(cr, 2.8);
        cairo_stroke(cr);
        circle(cr, xx,
               y(threshold - reduction(threshold, p[Threshold], p[Ratio], p[Knee], p[Range])), 3.5,
               accent);
    }
    cairo_restore(cr);
    box(cr, {g.x + 99, 85, 6, 6}, inputTint.alpha(.45));
    text(cr, "In", g.x + 111, 91, 10, muted);
    box(cr, {g.x + 140, 85, 6, 6}, outputTint.alpha(.8));
    text(cr, "Out", g.x + 152, 91, 10, outputTint);
    text(cr, "6 s history", g.x + g.w, 91, 9, dim, false, 2);
}
void Editor::drawMeters(cairo_t *cr) {
    const auto g = graphBounds();
    const double x = logicalWidth_ * .669, y = g.y;
    text(cr, "GR", x + 3, 83, 9, red, true, 1);
    text(cr, number(model_.reduction), x + 3, 99, 9, muted, false, 1);
    meterBar(cr, {x, y, 6, g.h}, model_.reduction / 100., true);
    const double out = std::max(model_.peaks[2], model_.peaks[3]);
    text(cr, "OUT", x + 34, 83, 9, muted, true, 1);
    text(cr, out < 1e-4 ? "-inf" : number(db(out)), x + 34, 99, 9, model_.clipped ? red : muted,
         false, 1);
    for (unsigned c = 0; c < 2; ++c)
        meterBar(cr, {x + 25 + c * 10, y, 6, g.h}, (db(model_.peaks[c + 2]) + 80) / 80.);
    text(cr, "dB", x + 3, y + g.h + 16, 9, dim, false, 1);
    text(cr, model_.clipped ? "CLIP" : "dBFS", x + 34, y + g.h + 16, 9, model_.clipped ? red : dim,
         false, 1);
    for (unsigned n = 0; n <= 100; n += 20)
        text(cr, std::to_string(n), x - 4, y + g.h * n / 100. + 3, 8, dim, false, 2);
}
void Editor::drawValue(cairo_t *cr, Rect r, unsigned i, double size) {
    const bool editing = editing_ == int(i);
    const auto tint = i == WetGain || i == Mix ? teal : accent;
    theme::well(cr, r, 4, editing || r.contains(mouseX_, mouseY_) || focused_ == int(i),
                editing && inputError_ ? hex(0xea8f86) : tint);
    cairo_save(cr);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_clip(cr);
    if (editing && selectAll_)
        box(cr, {r.x + 4, r.y + 3, r.w - 8, r.h - 6}, tint.alpha(.17));
    centeredText(cr, editing ? input_ : label(i, model_.values[i]), r, size, editing ? tint : ink,
                 true);
    cairo_restore(cr);
}
void Editor::paint(cairo_t *cr, double width, double height) {
    logicalWidth_ = width;
    logicalHeight_ = height;
    cairo_save(cr);
    cairo_scale(cr, scale_, scale_);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    box(cr, {0, 0, width, height}, theme::background);
    theme::gradient(cr, {0, 60, width, height - 104}, hex(0x15181a), hex(0x202426));
    theme::gradient(cr, {0, 0, width, 60}, hex(0x32363a), hex(0x26292d));
    line(cr, 0, 60, width, 60, hex(0x000000, .35));
    drawBrand(cr, Brand::Gate, {24, 17, 228, 26});
    const char *commands[]{
        "Starting points v", "", "", "A", "B", comparison_ == 0 ? "A > B" : "B > A", "Help"};
    for (unsigned i = 0; i < 7; ++i) {
        const auto r = headerBounds(i);
        const bool enabled = i == 1 ? !undo_.empty() : i == 2 ? !redo_.empty() : true;
        drawButton(cr, r, commands[i], (i == 3 && comparison_ == 0) || (i == 4 && comparison_ == 1),
                   enabled);
        if (i == 1 || i == 2) {
            cairo_save(cr);
            cairo_translate(cr, r.x + r.w / 2, r.y + r.h / 2);
            if (i == 2)
                cairo_scale(cr, -1, 1);
            const auto col = enabled ? ink : dim;
            color(cr, col);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, -6, -3);
            cairo_curve_to(cr, 8, -6, 10, 6, 0, 6);
            cairo_stroke(cr);
            line(cr, -6, -3, -2, -7, col, 1.5);
            line(cr, -6, -3, -2, 1, col, 1.5);
            cairo_restore(cr);
        }
    }
    drawGraph(cr);
    drawMeters(cr);
    drawButton(cr, viewBounds(0), "Transfer", kneeVisible_);
    auto dial = [&](unsigned i) {
        const auto r = controlBounds(i);
        const double radius = (r.h - 42) / 2;
        cairo_save(cr);
        cairo_translate(cr, r.x + r.w / 2, r.y + radius);
        const double factor = radius / 40.;
        cairo_scale(cr, factor, factor);
        theme::knob(cr, {-50, -40, 100, 80}, normalized(i, model_.values[i]),
                    normalized(i, parameter(i).initial),
                    i == WetGain || i == DryGain ? teal : accent, focused_ == int(i), true, true);
        cairo_restore(cr);
        centeredText(cr, parameter(i).name, {r.x - 8, r.y + r.h - 43, r.w + 16, 17},
                     i == Threshold ? 12 : 11, muted);
        drawValue(cr, {r.x + 1, r.y + r.h - 24, r.w - 2, 23}, i, i == Threshold ? 14 : 11);
    };
    for (unsigned i : mainControls)
        dial(i);
    drawButton(cr, controlBounds(Detector), label(Detector, model_.values[Detector]) + " v");
    const std::string expert = model_.values[Audition] != 0    ? "Audition"
                               : model_.values[Sidechain] != 0 ? "External SC"
                                                               : "Expert";
    drawButton(cr, viewBounds(1), expert + (sidechainVisible_ ? "  −" : "  +"),
               sidechainVisible_ || model_.values[Audition] != 0);
    if (sidechainVisible_) {
        const double y = workspaceBottom();
        theme::gradient(cr, {0, y, width, 200}, hex(0x292d30), theme::background);
        line(cr, 0, y, width, y, theme::border.alpha(.6));
        for (unsigned i : {Input, Mix, WetGain, DryGain})
            dial(i);
        centeredText(cr, "SIDE CHAIN", {width * .345, y + 17, width * .115, 28}, 11, muted, true);
        drawButton(cr, controlBounds(Sidechain), label(Sidechain, model_.values[Sidechain]) + " v");
        for (unsigned i : {SidechainHP, SidechainLP}) {
            const auto r = controlBounds(i);
            text(cr, i == SidechainHP ? "HP" : "LP", r.x, r.y + 16, 10, muted);
            drawValue(cr, {r.x + r.w * .27, r.y, r.w * .73, 24}, i, 11);
            const double yy = r.y + 39;
            theme::well(cr, {r.x, yy - 3, r.w, 6}, 3);
            const double xx = r.x + r.w * normalized(i, model_.values[i]);
            line(cr, r.x, yy, xx, yy, accent.alpha(.6), 2);
            circle(cr, xx, yy, 4, accent);
        }
        drawButton(cr, controlBounds(Audition), "Audition", model_.values[Audition] != 0);
        for (unsigned i : {Hysteresis, StereoLink, WetPan, DryPan}) {
            const auto r = controlBounds(i);
            const double caption = i == Hysteresis ? 57 : i == StereoLink ? 33 : 26;
            text(cr,
                 i == Hysteresis   ? "Hysteresis"
                 : i == StereoLink ? "Link"
                                   : "Pan",
                 r.x, r.y + 18, 10, muted);
            drawValue(cr, {r.x + caption, r.y + 1, r.w - caption, 25}, i, 10);
        }
    }
    theme::gradient(cr, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(cr, 0, height - 44, width, height - 44, hex(0x000000, .7));
    line(cr, 0, height - 43, width, height - 43, hex(0xffffff, .09));
    centeredText(cr, "10 ms latency", {20, height - 36, 90, 28}, 11, muted);
    if (width > 1000)
        centeredText(cr,
                     std::string(model_.mono ? "Mono" : "Stereo") + " / " +
                         number(model_.rate / 1000) + " kHz",
                     {126, height - 36, 120, 28}, 10, dim);
    for (unsigned i : {Output}) {
        const auto r = controlBounds(i);
        text(cr, parameter(i).name, r.x, r.y + 18, 11, muted);
        const Rect field{r.x + 46, r.y + 2, r.w - 46, r.h - 4};
        drawValue(cr, field, i, 12);
    }
    drawButton(cr, controlBounds(Bypass), model_.values[Bypass] != 0 ? "Bypassed" : "Bypass",
               model_.values[Bypass] != 0);
    if (menu_ >= 0) {
        const auto r = menuBounds();
        theme::raised(cr, r, 8, true);
        const char *presets[]{"Default", "Gentle expansion", "Tight drums", "Vocal cleanup",
                              "External trigger"};
        const unsigned rows = menu_ == 100 ? 5 : 2;
        for (unsigned n = 0; n < rows; ++n) {
            const Rect row{r.x + 4, r.y + n * 36 + 2, r.w - 8, 32};
            if (row.contains(mouseX_, mouseY_))
                theme::well(cr, row, 4);
            const bool selected = menu_ != 100 && model_.values[menu_] == n;
            if (selected)
                circle(cr, row.x + 12, row.y + 16, 3, accent);
            text(cr, menu_ == 100 ? presets[n] : label(menu_, n), row.x + 26, row.y + 21, 12,
                 selected ? ink : muted);
        }
    }
    if (editing_ >= 0 && parameter(editing_).stepped) {
        const auto anchor = controlBounds(editing_);
        const Rect r{anchor.x, anchor.y - 76, 170, 68};
        theme::raised(cr, r, 6, true);
        text(cr, parameter(editing_).name, r.x + 12, r.y + 21, 11, muted);
        theme::well(cr, {r.x + 8, r.y + 30, r.w - 16, 30}, 4, true, inputError_ ? red : accent);
        centeredText(cr, input_, {r.x + 8, r.y + 30, r.w - 16, 30}, 12, ink);
    }
    if (help_) {
        const Rect r{width * .5 - 280, 110, 560, 270};
        theme::raised(cr, r, 12, true);
        text(cr, "Gate controls", r.x + 24, r.y + 34, 15, ink, true);
        const char *lines[]{
            "Drag knobs or readouts vertically. Shift makes fine adjustments.",
            "Double-click any value to reset its descriptor default.",
            "Click a value or right-click a control for exact entry.",
            "Tab focuses controls; Enter edits; arrows adjust; Esc cancels.",
            "Drag the transfer curve to set threshold. Expert opens sidechain controls.",
            "A/B compares settings. Copy stores the active slot into the other.",
            "Ctrl+Z / Ctrl+Shift+Z undo / redo. Presets are starting points.",
            "GR is attenuation; meters are sample peaks, not true peaks.",
            "External SC needs host routing. Lookahead retains fixed 10 ms latency."};
        for (unsigned n = 0; n < 9; ++n)
            text(cr, lines[n], r.x + 24, r.y + 63 + n * 21, 11, muted);
    }
    cairo_restore(cr);
}
} // namespace openfilter::gate
