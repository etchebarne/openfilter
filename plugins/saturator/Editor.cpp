#include "Editor.hpp"
#include "Engine.hpp"
#include <numbers>
#include <openfilter/ui/Brand.hpp>
#include <openfilter/ui/Meters.hpp>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>
namespace openfilter::saturator {
using namespace ui;
namespace {
const auto accent = hex(0xe6a487), ink = theme::ink, muted = theme::muted, dim = theme::dim;
const auto red = hex(0xea8f86);
const Color bandColors[]{hex(0xe7b76d), hex(0xf09b7e), hex(0xb39be5)};
const char *bandNames[]{"LOW", "MID", "HIGH"};
std::string label(unsigned i, double value) {
    char s[80];
    format(i, value, s, sizeof(s));
    return s;
}
std::string number(double v) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.1f", v);
    return s;
}
double db(double v) {
    return 20 * std::log10(std::max(1e-5, v));
}
Values preset(unsigned n) {
    auto v = defaults();
    if (n == 1) {
        v[band(0, Drive)] = 9;
        v[band(1, Drive)] = 12;
        v[band(2, Drive)] = 4;
        v[band(1, Style)] = 3;
        v[Mix] = 40;
    }
    if (n == 2) {
        for (unsigned b = 0; b < 3; ++b) {
            v[band(b, Drive)] = 18;
            v[band(b, Dynamics)] = 35;
            v[band(b, Style)] = 2;
        }
        v[Mix] = 60;
        v[Compensation] = 65;
    }
    if (n == 3) {
        v[band(0, Drive)] = 3;
        v[band(1, Drive)] = 15;
        v[band(2, Drive)] = 9;
        v[band(1, Style)] = 1;
        v[band(1, Presence)] = 2;
        v[Mix] = 55;
    }
    if (n == 4) {
        for (unsigned b = 0; b < 3; ++b) {
            v[band(b, Drive)] = 24;
            v[band(b, Style)] = 3;
        }
        v[Compensation] = 50;
        v[Mix] = 30;
    }
    return v;
}
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
Editor::Editor(Read read, Send send, ui::AnalysisTap &tap)
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
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter Saturator");
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
    if (spectrum_.update(analysis_.frames.read()))
        lastAudio_ = now;
    else if (std::chrono::duration<double>(now - lastAudio_).count() > .25) {
        spectrum_.decay(elapsed);
        for (unsigned c = 0; c < 4; ++c)
            next.peaks[c] = model_.peaks[c] * std::exp(-elapsed / .4);
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
Rect Editor::graphBounds() const {
    return {24, 116, logicalWidth_ - 132, logicalHeight_ - 442};
}
Rect Editor::panelBounds() const {
    return {42, logicalHeight_ - 323, logicalWidth_ - 162, 206};
}
double Editor::crossover(unsigned i) const {
    const double high = std::min(model_.effective[CrossoverHigh], model_.rate * .45);
    return i == CrossoverHigh ? high : std::min(model_.effective[CrossoverLow], high * .8);
}
double Editor::frequencyX(double hz) const {
    const auto g = graphBounds();
    return g.x + g.w * std::log(hz / 20) / std::log(1000.);
}
double Editor::xFrequency(double x) const {
    const auto g = graphBounds();
    return 20 * std::pow(1000., std::clamp((x - g.x) / g.w, 0., 1.));
}
Rect Editor::nodeBounds(unsigned b) const {
    const auto g = graphBounds();
    const double edges[]{g.x, frequencyX(crossover(CrossoverLow)),
                         frequencyX(crossover(CrossoverHigh)), g.x + g.w};
    const double y = g.y + g.h * (.5 - model_.values[band(b, Level)] / 60.);
    const double w = std::min(58., std::max(14., edges[b + 1] - edges[b] - 12));
    return {(edges[b] + edges[b + 1]) * .5 - w / 2, std::clamp(y - 14, g.y + 64, g.y + g.h - 28), w,
            28};
}
Rect Editor::viewBounds(unsigned b) const {
    const auto p = panelBounds();
    const double width = (p.w - 24) / 3;
    return {p.x + b * (width + 12), logicalHeight_ - 105, width, 48};
}
bool Editor::available(unsigned i) const {
    return i < globals || i == AutoLevel || (i - globals) / stride == selected_;
}
Rect Editor::controlBounds(unsigned i) const {
    const auto p = panelBounds();
    if (i == CrossoverLow || i == CrossoverHigh) {
        const auto g = graphBounds();
        const double left =
            std::clamp(frequencyX(crossover(CrossoverLow)) - 45, g.x + 80, g.x + g.w - 188);
        const double right =
            std::clamp(frequencyX(crossover(CrossoverHigh)) - 45, left + 96, g.x + g.w - 90);
        return {i == CrossoverLow ? left : right, 80, 90, 26};
    }
    if (i < globals || i == AutoLevel) {
        if (i == Bypass)
            return {logicalWidth_ - 124, logicalHeight_ - 38, 106, 28};
        const unsigned col = i == Input          ? 0
                             : i == Mix          ? 1
                             : i == Compensation ? 2
                             : i == AutoLevel    ? 3
                                                 : 4;
        return {24 + col * (logicalWidth_ - 166) / 5., logicalHeight_ - 38,
                (logicalWidth_ - 166) / 5. - 18, 28};
    }
    const unsigned b = (i - globals) / stride, f = (i - globals) % stride;
    if (b != selected_)
        return {};
    if (f == Solo || f == Mute)
        return {p.x + p.w - (f == Solo ? 112 : 56), p.y + 6, 50, 28};
    if (f == Enabled)
        return {p.x, p.y + 6, 46, 28};
    if (f == Style)
        return {p.x + 54, p.y + 6, logicalWidth_ < 1000 ? 138. : 168., 28};
    if (f >= Bass && f <= Presence)
        return {p.x + p.w * (.575 + .067 * (f - Bass)) - 20, p.y + 43, 40, 160};
    const double center = f == BandMix ? .08 : f == Dynamics ? .235 : f == Drive ? .41 : .93;
    const double size = f == Drive ? (logicalWidth_ < 1000 ? 116 : 136) : 78;
    // All captions and readouts share baselines; the drive cap rises above the rail.
    return {p.x + p.w * center - size / 2, p.y + 150 - size, size, size + 53};
}
Rect Editor::menuBounds() const {
    const auto r = menu_ == 100 ? headerBounds(0) : controlBounds(menu_);
    return {r.x, menu_ == 100 ? r.y + r.h + 8 : r.y - (36 * styleCount + 6), 200,
            menu_ == 100 ? 180. : 36. * styleCount};
}
int Editor::hit(double x, double y) const {
    for (unsigned i = 0; i < parameterCount; ++i)
        if (available(i) && controlBounds(i).contains(x, y))
            return i;
    const auto g = graphBounds();
    if (g.contains(x, y)) {
        for (unsigned i : {unsigned(CrossoverLow), unsigned(CrossoverHigh)})
            if (std::abs(x - frequencyX(crossover(i))) < 9)
                return i;
        for (unsigned b = 0; b < 3; ++b)
            if (nodeBounds(b).contains(x, y))
                return band(b, Level);
    }
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
    const bool twice = clicks_.press(target, x, y, buttonId, time);
    if (twice && target >= 0) {
        cancelText();
        menu_ = -1;
        once(target, parameter(target).initial);
        return;
    }
    if (menu_ >= 0) {
        const auto r = menuBounds();
        if (r.contains(x, y)) {
            const unsigned row = unsigned((y - r.y) / 36);
            if (menu_ == 100)
                apply(preset(row));
            else
                once(menu_, row);
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
    for (unsigned b = 0; b < 3; ++b)
        if (viewBounds(b).contains(x, y)) {
            finishGesture();
            selected_ = b;
            focused_ = band(b, Drive);
            invalidate();
            return;
        }
    if (target >= 0) {
        focused_ = target;
        if (target >= int(globals) && target < int(legacyParameterCount))
            selected_ = (target - globals) / stride;
        const auto r = controlBounds(target);
        if (buttonId != 0 || (mods & PUGL_MOD_CTRL)) {
            textEdit(target);
            return;
        }
        if (target >= int(globals) && (target - globals) % stride == Style) {
            menu_ = target;
            invalidate();
            return;
        }
        if (parameter(target).stepped) {
            once(target, 1 - model_.values[target]);
            return;
        }
        const bool graph = graphBounds().contains(x, y) && !r.contains(x, y);
        if (!graph && (target < int(globals) || (r.h > 80 && y >= r.y + r.h - 25))) {
            finishGesture();
            readout_.press(target, x, y);
            return;
        }
        begin(target);
        graphDrag_ = graph;
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (panelBounds().contains(x, y))
        return;
    if (graphBounds().contains(x, y)) {
        finishGesture();
        selected_ = x < frequencyX(crossover(CrossoverLow))    ? 0
                    : x < frequencyX(crossover(CrossoverHigh)) ? 1
                                                               : 2;
        focused_ = band(selected_, Drive);
        invalidate();
        return;
    }
    if (x > logicalWidth_ - 92 && y > 70 && y < logicalHeight_ - 65)
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
        if (graphDrag_ && logarithmic(drag_)) {
            const double delta = (x - dragX_) * ((mods & PUGL_MOD_SHIFT) ? .1 : 1.);
            double hz = xFrequency(frequencyX(model_.values[drag_]) + delta);
            hz = drag_ == CrossoverLow ? std::min(hz, model_.values[CrossoverHigh] * .8)
                                       : std::max(hz, model_.values[CrossoverLow] * 1.25);
            change(drag_, hz);
        } else
            change(drag_, denormalized(drag_, normalized(drag_, model_.values[drag_]) +
                                                  (dragY_ - y) *
                                                      ((mods & PUGL_MOD_SHIFT) ? .0005 : .005)));
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
    // The spectrum continues under the floating controls. Editable band handles
    // remain in the unobstructed upper workspace.
    const double floor = logicalHeight_ - 124;
    const double spectrumHeight = floor - g.y;
    const double edges[]{g.x, frequencyX(crossover(CrossoverLow)),
                         frequencyX(crossover(CrossoverHigh)), g.x + g.w};
    for (unsigned b = 0; b < 3; ++b) {
        const double left = b == 0 ? 0 : edges[b];
        const double right = b == 2 ? logicalWidth_ : edges[b + 1];
        theme::gradient(cr, {left, 60, right - left, floor - 60},
                        bandColors[b].alpha(b == selected_ ? .14 : .025), bandColors[b].alpha(0));
        // Broad, vector-only warm light makes the selected frequency region legible.
        if (b == selected_) {
            cairo_save(cr);
            cairo_rectangle(cr, left, 61, right - left, floor - 61);
            cairo_clip(cr);
            auto *glow = cairo_pattern_create_radial((left + right) / 2, floor * .62, 0,
                                                     (left + right) / 2, floor * .62, 350);
            theme::stop(glow, 0, hex(0xb5432e, .23));
            theme::stop(glow, 1, hex(0xb5432e, 0));
            cairo_set_source(cr, glow);
            cairo_paint(cr);
            cairo_pattern_destroy(glow);
            cairo_restore(cr);
        }
        const double cx = (edges[b] + edges[b + 1]) / 2;
        text(cr, bandNames[b], cx, 143, 10, b == selected_ ? bandColors[b] : dim, true, 1);
        if (!model_.values[band(b, Enabled)] || model_.values[band(b, Solo)] ||
            model_.values[band(b, Mute)])
            text(cr,
                 !model_.values[band(b, Enabled)] ? "OFF"
                 : model_.values[band(b, Mute)]   ? "MUTED"
                                                  : "SOLO",
                 cx, 160, 9, bandColors[b], true, 1);
    }
    for (int d = -72; d <= 0; d += 18) {
        const double y = g.y + spectrumHeight * (-d / 84.);
        line(cr, g.x, y, g.x + g.w, y, dim.alpha(.07));
        text(cr, std::to_string(d), g.x + g.w + 6, y + 3, 9, dim);
    }
    for (double f : {50., 100., 500., 1000., 5000., 10000., 20000.}) {
        const double x = frequencyX(f);
        line(cr, x, g.y, x, floor, dim.alpha(.04));
        text(cr, f >= 1000 ? std::to_string(int(f / 1000)) + "k" : std::to_string(int(f)), x,
             panelBounds().y - 17, 10, dim, false, 1);
    }
    cairo_save(cr);
    cairo_rectangle(cr, g.x, g.y, g.w, spectrumHeight);
    cairo_clip(cr);
    cairo_push_group(cr);
    for (unsigned c = 0; c < 2; ++c) {
        cairo_new_path(cr);
        for (unsigned n = 0; n <= 720; ++n) {
            const double x = g.x + g.w * n / 720.;
            const double y = g.y + spectrumHeight * (-spectrum_.at(c, xFrequency(x)) / 84.);
            if (n == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
        color(cr, c ? hex(0xefc0a3, .85) : hex(0x9da8b0, .38));
        cairo_set_line_width(cr, c ? 1.5 : 1);
        cairo_stroke_preserve(cr);
        cairo_line_to(cr, g.x + g.w, floor);
        cairo_line_to(cr, g.x, floor);
        cairo_close_path(cr);
        auto *fill = cairo_pattern_create_linear(0, g.y, 0, floor);
        theme::stop(fill, 0, (c ? accent : muted).alpha(c ? .16 : .05));
        theme::stop(fill, 1, accent.alpha(0));
        cairo_set_source(cr, fill);
        cairo_fill(cr);
        cairo_pattern_destroy(fill);
    }
    cairo_pop_group_to_source(cr);
    auto *fade = cairo_pattern_create_linear(0, panelBounds().y - 70, 0, panelBounds().y + 140);
    theme::stop(fade, 0, hex(0xffffff));
    theme::stop(fade, .5, hex(0xffffff, .18));
    theme::stop(fade, 1, hex(0xffffff, 0));
    cairo_mask(cr, fade);
    cairo_pattern_destroy(fade);
    for (unsigned b = 0; b < 3; ++b) {
        const auto r = nodeBounds(b);
        line(cr, edges[b], r.y + 14, edges[b + 1], r.y + 14, bandColors[b].alpha(.25));
        theme::raised(cr, r, 7, false, r.contains(mouseX_, mouseY_));
        if (b == selected_)
            line(cr, r.x + 12, r.y + r.h - 3, r.x + r.w - 12, r.y + r.h - 3, bandColors[b], 2);
        centeredText(cr,
                     r.w < 40                          ? "−"
                     : model_.values[band(b, Enabled)] ? number(model_.values[band(b, Level)])
                                                       : "Off",
                     r, 12, model_.values[band(b, Mute)] ? dim : bandColors[b], true);
    }
    cairo_restore(cr);
    for (unsigned i : {unsigned(CrossoverLow), unsigned(CrossoverHigh)}) {
        const auto r = controlBounds(i);
        const double x = frequencyX(crossover(i));
        const double dash[]{3, 5};
        cairo_set_dash(cr, dash, 2, 0);
        line(cr, x, 112, x, panelBounds().y - 33, muted.alpha(.4));
        cairo_set_dash(cr, nullptr, 0, 0);
        circle(cr, x, 116, 3, accent);
        drawValue(cr, r, i, 11);
        if (crossover(i) != model_.values[i])
            text(cr, "Effective " + number(crossover(i)) + " Hz", r.x + r.w / 2, 74, 9, muted,
                 false, 1);
    }
    line(cr, 28, 80, 39, 80, muted.alpha(.6), 1);
    text(cr, "PRE", 45, 83, 9, muted, true);
    line(cr, 79, 80, 90, 80, accent, 1.5);
    text(cr, "POST", 96, 83, 9, accent, true);
}

void Editor::drawMeters(cairo_t *cr) {
    const double x = logicalWidth_ - 67, y = 122, h = logicalHeight_ - 263;
    for (unsigned group = 0; group < 2; ++group) {
        const double xx = x + group * 32;
        text(cr, group ? "OUT" : "IN", xx + 6, 82, 9, muted, true, 1);
        const double peak = std::max(model_.peaks[group * 2], model_.peaks[group * 2 + 1]);
        text(cr, peak < 1e-5 ? "−inf" : number(db(peak)), xx + 6, 103, 9,
             group && model_.clipped ? red : muted, false, 1);
        for (unsigned c = 0; c < 2; ++c)
            meterBar(cr, {xx + c * 8, y, 5, h}, (db(model_.peaks[group * 2 + c]) + 72) / 72.);
    }
    text(cr, model_.clipped ? "CLIP" : "dBFS", x + 20, y + h + 22, 9, model_.clipped ? red : dim,
         false, 1);
}
void Editor::drawValue(cairo_t *cr, Rect r, unsigned i, double size) {
    const bool editing = editing_ == int(i);
    theme::well(cr, r, 4, editing || r.contains(mouseX_, mouseY_) || focused_ == int(i),
                editing && inputError_ ? red : accent);
    cairo_save(cr);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_clip(cr);
    if (editing && selectAll_)
        box(cr, {r.x + 4, r.y + 3, r.w - 8, r.h - 6}, accent.alpha(.17));
    centeredText(cr, editing ? input_ : label(i, model_.values[i]), r, size, editing ? accent : ink,
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
    theme::gradient(cr, {0, 60, width, height - 104}, hex(0x281b1e), hex(0x1b1e21));
    drawGraph(cr);
    drawMeters(cr);
    theme::gradient(cr, {0, 0, width, 60}, hex(0x32363a), hex(0x26292d));
    line(cr, 0, 60, width, 60, hex(0x000000, .35));
    drawBrand(cr, Brand::Saturator, {24, 17, 228, 26});
    const char *commands[]{
        "Starting points v", "", "", "A", "B", comparison_ == 0 ? "A > B" : "B > A", "Help"};
    for (unsigned i = 0; i < 7; ++i)
        drawButton(cr, headerBounds(i), commands[i],
                   (i == 3 && comparison_ == 0) || (i == 4 && comparison_ == 1),
                   i == 1   ? !undo_.empty()
                   : i == 2 ? !redo_.empty()
                            : true);
    for (unsigned i = 1; i <= 2; ++i) {
        const auto r = headerBounds(i);
        cairo_save(cr);
        cairo_translate(cr, r.x + r.w / 2, r.y + r.h / 2);
        if (i == 2)
            cairo_scale(cr, -1, 1);
        const auto col = (i == 1 ? undo_.empty() : redo_.empty()) ? dim : ink;
        color(cr, col);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, -6, -3);
        cairo_curve_to(cr, 8, -6, 10, 6, 0, 6);
        cairo_stroke(cr);
        line(cr, -6, -3, -2, -7, col, 1.5);
        line(cr, -6, -3, -2, 1, col, 1.5);
        cairo_restore(cr);
    }
    const auto p = panelBounds();
    const auto tint = bandColors[selected_];
    const bool enabled = model_.values[band(selected_, Enabled)] != 0;
    // A shallow shared-material rail supports the controls without boxing in the graph.
    const Rect rail{p.x - 10, p.y + 58, p.w + 20, 85};
    theme::raised(cr, rail, 15, true);
    theme::gradient(cr, rail, tint.alpha(.045), tint.alpha(0), 15);
    drawButton(cr, controlBounds(band(selected_, Enabled)), enabled ? "On" : "Off", enabled);
    drawButton(cr, controlBounds(band(selected_, Style)),
               label(band(selected_, Style), model_.values[band(selected_, Style)]) + " v");
    drawButton(cr, controlBounds(band(selected_, Solo)), "Solo",
               model_.values[band(selected_, Solo)]);
    drawButton(cr, controlBounds(band(selected_, Mute)), "Mute",
               model_.values[band(selected_, Mute)]);
    auto dial = [&](unsigned f, const char *caption) {
        const unsigned i = band(selected_, f);
        const auto r = controlBounds(i);
        const double radius = r.w / 2;
        const double x = r.x + r.w / 2, y = r.y + radius;
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, radius / 40., radius / 40.);
        theme::knob(cr, {-50, -40, 100, 80}, normalized(i, model_.values[i]),
                    f == Drive || f == BandMix ? 0 : normalized(i, parameter(i).initial), tint,
                    focused_ == int(i), enabled, true);
        if (f == Drive) {
            const double start = .75 * std::numbers::pi;
            const double end = start + 1.5 * std::numbers::pi;
            color(cr, tint.alpha(enabled ? .42 : .08));
            cairo_set_line_width(cr, 4.5);
            cairo_arc(cr, 0, 0, 37.5, start, end);
            cairo_stroke(cr);
            auto *ring = cairo_pattern_create_linear(-40, -40, 40, 40);
            const Color light{std::min(1., tint.r + .1), std::min(1., tint.g + .1),
                              std::min(1., tint.b + .1)};
            const Color shade{tint.r * .82, tint.g * .82, tint.b * .82};
            theme::stop(ring, 0, enabled ? light : dim);
            theme::stop(ring, 1, enabled ? shade : dim);
            cairo_set_source(cr, ring);
            cairo_arc(cr, 0, 0, 37.5, start,
                      start + (end - start) * normalized(i, model_.values[i]));
            cairo_stroke(cr);
            cairo_pattern_destroy(ring);
        }
        cairo_restore(cr);
        centeredText(cr, caption, {r.x - 8, p.y + 154, r.w + 16, 18}, f == Drive ? 12 : 10,
                     f == Drive ? ink : muted, true);
        const double valueWidth = f == Drive ? 104 : 78;
        drawValue(cr, {x - valueWidth / 2, r.y + r.h - 25, valueWidth, 24}, i,
                  f == Drive ? 15 : 12);
    };
    dial(BandMix, "MIX");
    dial(Dynamics, "DYNAMICS");
    dial(Drive, "DRIVE");
    dial(Level, "LEVEL");
    text(cr, "TONE", p.x + p.w * .675, p.y + 25, 10, muted, true, 1);
    for (unsigned t = 0; t < 4; ++t) {
        const unsigned i = band(selected_, Bass + t);
        const auto r = controlBounds(i);
        const double x = r.x + r.w / 2;
        theme::well(cr, {x - 3, r.y + 3, 6, 91}, 3);
        const double y = r.y + 94 - normalized(i, model_.values[i]) * 91;
        for (int tick = 0; tick < 5; ++tick)
            line(cr, x + 9, r.y + 3 + tick * 22.75, x + 13, r.y + 3 + tick * 22.75, dim.alpha(.35));
        line(cr, x, r.y + 48.5, x, y, tint.alpha(.8), 2);
        const Rect thumb{x - 17, y - 10, 34, 20};
        theme::raised(cr, thumb, 4, false, focused_ == int(i));
        line(cr, x - 9, y, x + 9, y, tint, 1.5);
        if (focused_ == int(i)) {
            rounded(cr, thumb, 4);
            color(cr, tint);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
        centeredText(cr, parameter(i).name, {r.x - 5, p.y + 154, r.w + 10, 18}, 10, muted);
        drawValue(cr, {r.x - 2, r.y + r.h - 25, r.w + 4, 24}, i, 10);
    }
    // Band selectors summarize actual stored settings. The curve is the static
    // waveshaper at the stored drive, not a prediction of the full dynamic processor.
    for (unsigned b = 0; b < 3; ++b) {
        const auto r = viewBounds(b);
        const auto col = bandColors[b];
        theme::well(cr, r, 8, selected_ == b, col);
        if (r.contains(mouseX_, mouseY_))
            theme::gradient(cr, r, col.alpha(.07), col.alpha(.02), 8);
        line(cr, r.x + 12, r.y + 12, r.x + 12, r.y + r.h - 12, col.alpha(selected_ == b ? 1 : .35),
             3);
        const double cx = r.x + 46, cy = r.y + 24;
        line(cr, cx - 19, cy, cx + 19, cy, dim.alpha(.22));
        line(cr, cx, cy - 15, cx, cy + 15, dim.alpha(.22));
        const unsigned style = unsigned(model_.values[band(b, Style)]);
        const double drive = std::pow(10., model_.values[band(b, Drive)] / 20);
        const auto transfer = [&](double x) {
            return style >= 4
                       ? Character(drive).process(style, x, [](double v) { return std::tanh(v); })
                       : shape(style, x * drive);
        };
        const double extent = std::max(std::abs(transfer(-1)), std::abs(transfer(1)));
        cairo_new_path(cr);
        for (unsigned n = 0; n <= 48; ++n) {
            const double v = n / 24. - 1;
            const double x = cx + v * 19, y = cy - 14 * transfer(v) / extent;
            if (n == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
        color(cr, col.alpha(model_.values[band(b, Enabled)] ? .9 : .25));
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        text(cr, bandNames[b], r.x + 80, r.y + 19, 10, selected_ == b ? col : muted, true);
        const std::string state = !model_.values[band(b, Enabled)] ? "Off"
                                  : model_.values[band(b, Mute)]   ? "Muted"
                                  : model_.values[band(b, Solo)]   ? "Solo"
                                                                   : styles[style];
        text(cr, state, r.x + 80, r.y + 35, 11, ink);
        text(cr, number(model_.values[band(b, Drive)]) + " dB", r.x + r.w - 12, r.y + 28, 12, ink,
             true, 2);
    }
    theme::gradient(cr, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(cr, 0, height - 44, width, height - 44, hex(0x000000, .7));
    for (unsigned i : {unsigned(Input), unsigned(Mix), unsigned(Compensation), unsigned(Output)}) {
        const auto r = controlBounds(i);
        const double caption = i == Compensation ? 76 : 42;
        text(cr, i == Compensation ? "Drive comp." : parameter(i).name, r.x, r.y + 18, 10, muted);
        drawValue(cr, {r.x + caption, r.y + 2, r.w - caption, r.h - 4}, i, 11);
    }
    drawButton(cr, controlBounds(AutoLevel),
               model_.values[AutoLevel] ? "Auto level: On" : "Auto level: Off",
               model_.values[AutoLevel]);
    drawButton(cr, controlBounds(Bypass), model_.values[Bypass] ? "Bypassed" : "Bypass",
               model_.values[Bypass]);
    if (menu_ >= 0) {
        const auto r = menuBounds();
        theme::raised(cr, r, 8, true);
        const char *presets[]{"Default", "Gentle warmth", "Drum density", "Vocal presence",
                              "Parallel colour"};
        for (unsigned n = 0; n < (menu_ == 100 ? 5u : styleCount); ++n) {
            const Rect row{r.x + 4, r.y + n * 36 + 2, r.w - 8, 32};
            if (row.contains(mouseX_, mouseY_))
                theme::well(cr, row, 4);
            text(cr, menu_ == 100 ? presets[n] : styles[n], row.x + 16, row.y + 21, 12, ink);
        }
    }
    if (editing_ >= 0 && parameter(editing_).stepped) {
        const auto a = controlBounds(editing_);
        const Rect r{std::clamp(a.x, 10., width - 190), std::max(64., a.y - 76), 180, 68};
        theme::raised(cr, r, 6, true);
        text(cr, parameter(editing_).name, r.x + 12, r.y + 21, 11, muted);
        drawValue(cr, {r.x + 8, r.y + 30, r.w - 16, 30}, editing_, 12);
    }
    if (help_) {
        const Rect r{width / 2 - 285, 110, 570, 286};
        theme::raised(cr, r, 12, true);
        text(cr, "Shape the harmonics · v0.3.1", r.x + 24, r.y + 34, 16, ink, true);
        const char *lines[]{
            "Select LOW, MID or HIGH. Drag the crossover lines horizontally.",
            "Drag a band handle for level. Solo and Mute isolate bands.",
            "Drive comp. defaults to zero. Raise it to apply Auto level matching.",
            "Dynamics: left expands, right compresses. Tone shapes the wet signal.",
            "Double-click resets. Click readouts to type; drag vertically to adjust.",
            "Shift drags finely. Tab focuses; Enter edits; arrows adjust; Esc cancels.",
            "A/B compares settings. Ctrl+Z / Ctrl+Shift+Z undo / redo.",
            "Punch adds odd harmonics; Color adds even harmonics. Both start clean.",
            "Older projects keep their styles. Choose Punch or Color to try them."};
        for (unsigned n = 0; n < 9; ++n)
            text(cr, lines[n], r.x + 24, r.y + 65 + n * 22, 11, muted);
    }
    cairo_restore(cr);
}
} // namespace openfilter::saturator
