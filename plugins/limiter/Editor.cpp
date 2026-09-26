#include "Editor.hpp"
#include "Engine.hpp"
#include <numbers>
#include <openfilter/ui/Brand.hpp>
#include <openfilter/ui/Meters.hpp>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>

namespace openfilter::limiter {
using namespace ui;
namespace {
const auto accent = theme::accent, ink = theme::ink, muted = theme::muted, dim = theme::dim;
const auto teal = hex(0x7ac4b3), red = hex(0xe48f85);
std::string label(unsigned i, double value) {
    char s[80];
    format(i, value, s, sizeof(s));
    return (value >= 0 && i == Gain ? "+" : "") + std::string(s);
}
std::string number(double v) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.1f", v);
    return s;
}
double db(double v) {
    return 20 * std::log10(std::max(1e-4, v));
}
constexpr unsigned mainControls[]{Lookahead, Attack, Release, StereoLink, ReleaseLink};
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
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter Limiter");
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
    return logicalHeight_ - 44;
}
Rect Editor::graphBounds() const {
    return {0, 94, logicalWidth_ - 132, workspaceBottom() - 118};
}
Rect Editor::viewBounds(unsigned) const {
    return {advancedVisible_ ? 724. : 0., workspaceBottom() - 160, 28, 142};
}
bool Editor::available(unsigned i) const {
    if (i >= parameterCount)
        return false;
    if (model_.values[Style] == double(Legacy) &&
        (i == Lookahead || i == Attack || i == ReleaseLink))
        return false;
    return advancedVisible_ || (i != Release && i != StereoLink && i != Lookahead && i != Attack &&
                                i != ReleaseLink && i != Style);
}
Rect Editor::controlBounds(unsigned i) const {
    const double bottom = workspaceBottom();
    if (i == Gain)
        return {24, 112, 66, bottom - 308};
    if (i == Lookahead)
        return {158, bottom - 152, 110, 132};
    if (i == Attack)
        return {278, bottom - 152, 110, 132};
    if (i == Release)
        return {398, bottom - 152, 110, 132};
    if (i == StereoLink)
        return {542, bottom - 130, 82, 110};
    if (i == ReleaseLink)
        return {634, bottom - 130, 82, 110};
    if (i == Style)
        return {18, bottom - 101, 122, 30};
    if (i == TruePeak)
        return {20, logicalHeight_ - 36, 112, 28};
    if (i == AutoRelease)
        return {144, logicalHeight_ - 36, 112, 28};
    if (i == UnityGain)
        return {logicalWidth_ - 486, logicalHeight_ - 36, 112, 28};
    if (i == Ceiling)
        return {logicalWidth_ - 356, logicalHeight_ - 36, 192, 28};
    if (i == Bypass)
        return {logicalWidth_ - 142, logicalHeight_ - 36, 118, 28};
    return {};
}
Rect Editor::gainReadoutBounds() const {
    const auto r = controlBounds(Gain);
    const double travel = r.h - 50;
    const double y = r.y + 16 + (1 - normalized(Gain, model_.values[Gain])) * travel;
    return {r.x + 3, y - 13, r.w - 6, 26};
}
Rect Editor::menuBounds() const {
    if (menu_ == Style) {
        const auto r = controlBounds(Style);
        return {r.x, r.y - 152, 180, 144};
    }
    const auto r = headerBounds(0);
    return {r.x, r.y + r.h + 8, 218, 144};
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
    const bool viewTarget = viewBounds(0).contains(x, y);
    const bool twice =
        clicks_.press(viewTarget ? int(parameterCount) : target, x, y, buttonId, time);
    if (help_) {
        help_ = false;
        invalidate();
        return;
    }
    if (twice && target == Style) {
        menu_ = -1;
        once(Style, parameter(Style).initial);
        return;
    }
    if (menu_ >= 0) {
        const auto r = menuBounds();
        if (r.contains(x, y)) {
            const unsigned row = static_cast<unsigned>((y - r.y) / 36);
            if (menu_ == Style) {
                once(Style, row);
                menu_ = -1;
                invalidate();
                return;
            }
            auto v = defaults();
            if (row == 1) {
                v[Gain] = 3;
                v[Release] = 350;
                v[AutoRelease] = 1;
            }
            if (row == 2) {
                v[Gain] = 6;
                v[Style] = Punch;
                v[Lookahead] = 1;
                v[Attack] = 250;
                v[Release] = 120;
            }
            if (row == 3) {
                v[Gain] = 4;
                v[Release] = 500;
                v[Ceiling] = -2;
            }
            apply(v);
        }
        menu_ = -1;
        invalidate();
        return;
    }
    if (twice && target >= 0) {
        cancelText();
        once(target, parameter(target).initial);
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
                help_ = true;
            invalidate();
            return;
        }
    if (viewTarget) {
        finishGesture();
        cancelText();
        advancedVisible_ = twice ? true : !advancedVisible_;
        if (!available(focused_))
            focused_ = Gain;
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
        if (target == Style) {
            finishGesture();
            menu_ = Style;
            invalidate();
            return;
        }
        if (parameter(target).stepped) {
            once(target, 1 - model_.values[target]);
            return;
        }
        if ((target == Gain && gainReadoutBounds().contains(x, y)) || target == Ceiling ||
            (target != Gain &&
             y > r.y + ((target == StereoLink || target == ReleaseLink) ? 88 : 106))) {
            finishGesture();
            readout_.press(target, x, y);
            return;
        }
        begin(target);
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (x > graphBounds().w && y > 60)
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
        const double delta = dragY_ - y;
        const double sensitivity = drag_ == Gain ? 1 / (controlBounds(Gain).h - 50) : .005;
        change(drag_,
               denormalized(drag_, normalized(drag_, model_.values[drag_]) +
                                       delta * sensitivity * ((mods & PUGL_MOD_SHIFT) ? .1 : 1)));
        dragX_ = x;
        dragY_ = y;
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
    auto y = [&](double level) { return g.y - level / 36. * g.h; };
    cairo_save(cr);
    cairo_rectangle(cr, g.x, g.y, g.w, workspaceBottom() - g.y);
    cairo_clip(cr);
    // A continuous canvas runs under the fader and the shallow control strip.
    for (int d = -36; d <= 0; d += 3) {
        const double yy = y(d);
        line(cr, 0, yy, g.w, yy, dim.alpha(d % 6 == 0 ? .20 : .10));
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        cairo_new_path(cr);
        for (unsigned n = 0; n < history_.size(); ++n) {
            const auto &f = history_[(historyPosition_ + n) % history_.size()];
            const double yy = kind == 2 ? y(-std::min(36., f.reduction))
                                        : y(std::max(-36., db(kind == 0 ? f.input : f.output)));
            const double xx = g.w * n / (history_.size() - 1);
            if (n == 0)
                cairo_move_to(cr, xx, yy);
            else
                cairo_line_to(cr, xx, yy);
        }
        const Color tint = kind == 0 ? hex(0x96a8b8) : kind == 1 ? teal : red;
        color(cr, tint.alpha(kind == 2 ? .95 : .7));
        cairo_set_line_width(cr, kind == 2 ? 1.5 : .9);
        cairo_stroke_preserve(cr);
        cairo_line_to(cr, g.w, kind == 2 ? g.y : workspaceBottom());
        cairo_line_to(cr, 0, kind == 2 ? g.y : workspaceBottom());
        cairo_close_path(cr);
        auto *fill = cairo_pattern_create_linear(0, g.y, 0, workspaceBottom());
        theme::stop(fill, 0, tint.alpha(kind == 2 ? .20 : .13));
        theme::stop(fill, 1, tint.alpha(kind == 2 ? .02 : .25));
        cairo_set_source(cr, fill);
        cairo_fill(cr);
        cairo_pattern_destroy(fill);
    }
    // Read real attenuation peaks, with one label per two-second region.
    for (unsigned section = 0; section < 3; ++section) {
        unsigned peakAt = 0;
        double peak = .75;
        for (unsigned n = section * 200 + 55; n < (section + 1) * 200 - 30; ++n) {
            const double value = history_[(historyPosition_ + n) % history_.size()].reduction;
            if (value > peak) {
                peak = value;
                peakAt = n;
            }
        }
        if (!peakAt)
            continue;
        const double x = std::clamp(g.w * peakAt / (history_.size() - 1), 132., g.w - 85.);
        const double yy = std::min(y(-peak) + 14, workspaceBottom() - 200);
        theme::gradient(cr, {x - 32, yy, 64, 24}, accent, hex(0xb89a55), 5);
        centeredText(cr, "-" + number(peak) + " dB", {x - 32, yy, 64, 24}, 11, hex(0x242622));
    }
    cairo_restore(cr);
    for (int d = -33; d <= 0; d += 3) {
        const bool covered =
            advancedVisible_ && g.w - 49 < 752 && y(d) + 4 >= workspaceBottom() - 160;
        if (!covered)
            text(cr, std::to_string(d) + " dB", g.w - 9, y(d) + 4, 10, muted, false, 2);
    }
    text(cr, "6 s", g.w - 10, 78, 9, dim, false, 2);
}
void Editor::drawMeters(cairo_t *cr) {
    const auto g = graphBounds();
    const double left = g.w + 7, top = g.y, bottom = workspaceBottom() - 24;
    const double peak = std::max(model_.peaks[2], model_.peaks[3]);
    centeredText(cr, "OUT", {left, 62, 44, 13}, 8, dim);
    centeredText(cr, peak < 1e-5 ? "-inf" : number(db(peak)), {left, 76, 44, 16}, 12,
                 model_.clipped ? red : teal);
    centeredText(cr, "GR", {left + 50, 62, 27, 13}, 8, dim);
    centeredText(cr, number(model_.reduction), {left + 45, 76, 36, 16}, 12, red);
    for (unsigned c = 0; c < 2; ++c)
        meterBar(cr, {left + c * 23., top, 20, bottom - top}, (db(model_.peaks[2 + c]) + 36) / 36.);
    meterBar(cr, {left + 51, top, 22, bottom - top}, model_.reduction / 36., true);
    for (int n = 0; n <= 36; n += 3) {
        const double yy = top + n / 36. * (bottom - top);
        text(cr, std::to_string(n), left + 88, yy + 3, 9, dim);
    }
    if (model_.clipped)
        centeredText(cr, "CLIP", {left, bottom + 5, 44, 16}, 9, red);
}
void Editor::drawValue(cairo_t *cr, Rect r, unsigned i, double size) {
    const bool editing = editing_ == int(i);
    const auto tint = i == Ceiling ? teal : accent;
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
    drawBrand(cr, Brand::Limiter, {24, 17, 228, 26});
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
    const auto gainRect = controlBounds(Gain);
    // The readout is the fader handle. Drag it; click without moving for entry.
    theme::gradient(cr, gainRect, hex(0x181d20, .55), hex(0x22282b, .28), 10);
    line(cr, gainRect.x, gainRect.y + 10, gainRect.x, gainRect.y + gainRect.h - 10, ink.alpha(.16));
    const auto handle = gainReadoutBounds();
    if (editing_ == Gain)
        drawValue(cr, handle, Gain, 12);
    else {
        theme::raised(cr, handle, 5);
        centeredText(cr, "+" + number(model_.values[Gain]), handle, 13,
                     focused_ == Gain ? accent : ink);
    }
    centeredText(cr, "GAIN", {gainRect.x, gainRect.y + gainRect.h + 8, gainRect.w, 18}, 10, muted);
    if (advancedVisible_) {
        const double top = workspaceBottom() - 160;
        theme::raised(cr, {-10, top, 762, 142}, 10, true);
        for (double x : {150., 526., 724.})
            line(cr, x, top + 12, x, top + 130, theme::border.alpha(.6));
        centeredText(cr, "STYLE", {18, top + 9, 122, 22}, 11, muted);
        drawButton(cr, controlBounds(Style), label(Style, model_.values[Style]) + " v", false);
        centeredText(cr, "CHANNEL LINKING", {535, top + 9, 181, 22}, 11, muted);
        for (unsigned i : mainControls) {
            const auto r = controlBounds(i);
            const bool linking = i == StereoLink || i == ReleaseLink;
            const bool enabled = available(i);
            const char *caption =
                i == Lookahead ? "LOOKAHEAD"
                : i == Attack  ? "ATTACK"
                : i == Release ? "RELEASE"
                : i == StereoLink
                    ? (model_.values[Style] == double(Legacy) ? "STEREO" : "TRANSIENTS")
                    : "RELEASE";
            centeredText(cr, caption, {r.x - 2, r.y + 1, r.w + 4, 22}, linking ? 9 : 11,
                         enabled ? muted : dim);
            const double factor = linking ? .65 : 1.;
            cairo_save(cr);
            cairo_translate(cr, r.x + r.w / 2, r.y + (linking ? 55 : 61));
            cairo_scale(cr, factor, factor);
            theme::knob(cr, {-50, -40, 100, 80}, normalized(i, model_.values[i]),
                        normalized(i, parameter(i).initial), linking ? teal : accent,
                        focused_ == int(i) && enabled, enabled);
            cairo_restore(cr);
            const Rect value{r.x + 5, r.y + (linking ? 88 : 106), r.w - 10, 22};
            if (enabled)
                drawValue(cr, value, i, 11);
            else {
                theme::well(cr, value, 4);
                centeredText(cr, i == Lookahead ? "5.0 ms" : "—", value, 11, dim);
            }
        }
    }
    const auto advanced = viewBounds(0);
    if (!advancedVisible_)
        theme::raised(cr, advanced, 5);
    cairo_save(cr);
    cairo_translate(cr, advanced.x + advanced.w / 2, advanced.y + advanced.h / 2);
    cairo_rotate(cr, std::numbers::pi / 2);
    centeredText(cr, advancedVisible_ ? "ADVANCED  <" : "ADVANCED  >",
                 {-advanced.h / 2, -advanced.w / 2, advanced.h, advanced.w}, 9,
                 advanced.contains(mouseX_, mouseY_) ? ink : muted);
    cairo_restore(cr);
    drawMeters(cr);
    theme::gradient(cr, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(cr, 0, height - 44, width, height - 44, hex(0x000000, .7));
    line(cr, 0, height - 43, width, height - 43, hex(0xffffff, .09));
    drawButton(cr, controlBounds(TruePeak), "True peak", model_.values[TruePeak] != 0);
    drawButton(cr, controlBounds(AutoRelease), "Auto release", model_.values[AutoRelease] != 0);
    if (width > 1000)
        centeredText(cr,
                     std::string(model_.mono ? "Mono" : "Stereo") + " / " +
                         number(model_.rate / 1000) + " kHz / " +
                         (model_.values[Style] == double(Legacy) ? "Legacy"
                          : model_.rate <= 192000                ? "4x"
                          : model_.rate <= 384000                ? "2x"
                                                                 : "1x"),
                     {270, height - 36, 190, 28}, 10, dim);
    drawButton(cr, controlBounds(UnityGain), "Unity gain", model_.values[UnityGain] != 0);
    const auto ceiling = controlBounds(Ceiling);
    text(cr, "Ceiling", ceiling.x, ceiling.y + 18, 11, muted);
    drawValue(cr, {ceiling.x + 50, ceiling.y + 2, ceiling.w - 50, ceiling.h - 4}, Ceiling, 12);
    drawButton(cr, controlBounds(Bypass), model_.values[Bypass] != 0 ? "Bypassed" : "Bypass",
               model_.values[Bypass] != 0);
    if (menu_ >= 0) {
        const auto r = menuBounds();
        theme::raised(cr, r, 8, true);
        const char *presets[]{"Default", "Gentle lift", "Transient control", "Slow recovery"};
        const unsigned rows = 4;
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
        const Rect r{std::clamp(anchor.x, 12., width - 182), anchor.y - 76, 170, 68};
        theme::raised(cr, r, 6, true);
        text(cr, parameter(editing_).name, r.x + 12, r.y + 21, 11, muted);
        theme::well(cr, {r.x + 8, r.y + 30, r.w - 16, 30}, 4, true, inputError_ ? red : accent);
        centeredText(cr, input_, {r.x + 8, r.y + 30, r.w - 16, 30}, 12, ink);
    }
    if (help_) {
        const Rect r{width * .5 - 280, 110, 560, 270};
        theme::raised(cr, r, 12, true);
        text(cr, "Limiter controls", r.x + 24, r.y + 34, 15, ink, true);
        const char *lines[]{
            "Drag knobs or readouts vertically. Shift adjusts finely.",
            "Double-click any value to reset its descriptor default.",
            "Click a value or right-click a control for exact entry.",
            "Tab focuses controls; Enter edits; arrows adjust; Esc cancels.",
            "Gain drives limiting. Ceiling trims the final limited output.",
            "A/B compares settings. Copy stores the active slot into the other.",
            "Ctrl+Z / Ctrl+Shift+Z undo / redo. Presets are starting points.",
            "True peak enables inter-sample protection and true-peak output meters.",
            "Attack shapes sustained reduction; Lookahead smooths transient capture."};
        for (unsigned n = 0; n < 9; ++n)
            text(cr, lines[n], r.x + 24, r.y + 63 + n * 21, 11, muted);
    }
    cairo_restore(cr);
}
} // namespace openfilter::limiter
