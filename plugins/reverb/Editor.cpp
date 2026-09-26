#include "Editor.hpp"
#include "Engine.hpp"
#include "Response.hpp"
#include <numbers>
#include <openfilter/ui/Brand.hpp>
#include <openfilter/ui/Meters.hpp>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>

namespace openfilter::reverb {
using namespace ui;
namespace {
const auto accent = theme::accent, ink = theme::ink, muted = theme::muted, dim = theme::dim;
const auto teal = hex(0x7ac4b3), red = hex(0xe48f85);
std::string label(unsigned i, double value) {
    char s[80];
    format(i, value, s, sizeof(s));
    return (i == DecayRate                              ? "Decay "
            : i == PredelayOffset                       ? "Offset "
            : value >= 0 && (i == Input || i == Output) ? "+"
                                                        : "") +
           std::string(s);
}
std::string number(double v) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.1f", v);
    return s;
}
double db(double v) {
    return 20 * std::log10(std::max(1e-4, v));
}
constexpr unsigned mainControls[]{Predelay,   Character, Thickness, Distance, Space,
                                  Brightness, Width,     Ducking,   Mix};
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
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter Reverb");
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
    decayHistory_.clear();
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
        decayHistory_.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::min(.25, std::chrono::duration<double>(now - lastTick_).count());
    lastTick_ = now;
    const bool fresh = decayHistory_.update(analysis_.frames.read());
    if (fresh)
        lastAudio_ = now;
    else if (std::chrono::duration<double>(now - lastAudio_).count() > .25) {
        for (unsigned c = 0; c < 4; ++c)
            next.peaks[c] = model_.peaks[c] * std::exp(-elapsed / .4);
        decayHistory_.idle(elapsed);
    }
    model_ = next;
    if (!available(focused_))
        focused_ = Space;
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
    return {54, 308, logicalWidth_ - 170, logicalHeight_ - 392};
}
Rect Editor::viewBounds(unsigned i) const {
    return i == 0 ? Rect{18, 272, 126, 26} : Rect{logicalWidth_ - 252, 272, 124, 26};
}
Rect Editor::inspectorBounds() const {
    return {logicalWidth_ / 2 - 247, logicalHeight_ - 164, 494, 88};
}
bool Editor::available(unsigned i) const {
    if (i == PredelayOffset && model_.values[PredelaySync] == 0)
        return false;
    if (i >= globals && (i - globals) % fields == Amount && postView_ && selected_ >= 0 &&
        model_.values[bandIndex(true, selected_, Type)] >= 3)
        return false;
    if (i >= globals && (i - globals) % fields == Q && !postView_ && selected_ >= 0 &&
        model_.values[bandIndex(false, selected_, Type)] != 0)
        return false;
    return i < globals || (selected_ >= 0 && i >= bandIndex(postView_, selected_, 0) &&
                           i < bandIndex(postView_, selected_, 0) + fields);
}
Rect Editor::controlBounds(unsigned i) const {
    const double span = logicalWidth_ - 84;
    constexpr double positions[]{0, .11, .215, .325, .5, .675, .785, .89, 1};
    for (unsigned n = 0; n < 9; ++n)
        if (i == mainControls[n]) {
            const double center = 42 + positions[n] * span;
            const double w = i == Space ? 152 : (i == Distance || i == Brightness) ? 96 : 80;
            return {center - w / 2, i == Space ? 104. : 126., w, i == Space ? 164. : 136.};
        }
    if (i == DecayRate)
        return {logicalWidth_ / 2 - 62, 74, 124, 24};
    if (i == Style)
        return {124, 78, std::min(206., logicalWidth_ * .19), 24};
    if (i == PredelaySync)
        return {16, 78, 96, 24};
    if (i == PredelayOffset)
        return {16, 108, 96, 22};
    if (i == AutoGate)
        return {logicalWidth_ - 274, 78, 74, 24};
    if (i == GateHold)
        return {logicalWidth_ - 192, 78, 76, 24};
    if (i == Freeze)
        return {logicalWidth_ - 80, 78, 68, 24};
    if (i == MixLock)
        return {logicalWidth_ - 80, 108, 68, 22};
    if (i == Input)
        return {logicalWidth_ - 446, logicalHeight_ - 36, 132, 28};
    if (i == Output)
        return {logicalWidth_ - 296, logicalHeight_ - 36, 132, 28};
    if (i == Bypass)
        return {logicalWidth_ - 142, logicalHeight_ - 36, 118, 28};
    if (i >= globals && selected_ >= 0) {
        const auto p = inspectorBounds();
        const unsigned f = (i - globals) % fields;
        constexpr double x[]{12, 104, 208, 304, 376};
        constexpr double w[]{78, 94, 86, 62, 106};
        return {p.x + x[f], p.y + 38, w[f], 30};
    }
    return {};
}
Rect Editor::dialBounds(unsigned i) const {
    const auto r = controlBounds(i);
    // Full painted extent, including tick marks, shadow and keyboard focus ring.
    const double radius = i == Space ? 68 : i == Distance || i == Brightness ? 41 : 34;
    return {r.x + r.w / 2 - radius, 174 - radius, radius * 2, radius * 2};
}
Rect Editor::captionBounds(unsigned i) const {
    const auto r = controlBounds(i);
    return {r.x, i == Space ? 250. : 217., r.w, 18};
}
Rect Editor::valueBounds(unsigned i) const {
    const auto r = controlBounds(i);
    if (i == Space)
        return {r.x + r.w / 2 - 46, 160, 92, 28};
    return {r.x + 3, 238, r.w - 6, 24};
}
Rect Editor::nodeBounds(unsigned b) const {
    const auto g = graphBounds();
    const auto i = bandIndex(postView_, b, 0);
    const double x = g.x + g.w * std::log(model_.values[i + Frequency] / 20) / std::log(1000.);
    const double n =
        postView_ ? (model_.values[i + Type] >= 3 ? .5 : (model_.values[i + Amount] + 24) / 48)
                  : std::log2(model_.values[i + Amount] / 25) / 4;
    return {x - 9, g.y + g.h * (1 - n) - 9, 18, 18};
}
int Editor::nodeAt(double x, double y) const {
    for (unsigned b = 0; b < bands; ++b)
        if (model_.values[bandIndex(postView_, b, Enabled)] && nodeBounds(b).contains(x, y))
            return b;
    return -1;
}
Rect Editor::menuBounds() const {
    const auto r = menu_ == 1000 ? headerBounds(0) : controlBounds(menu_);
    const unsigned rows = menu_ == 1000 ? 5 : unsigned(parameter(menu_).max) + 1;
    return {std::min(r.x, logicalWidth_ - 216),
            std::min(r.y + r.h + 8, logicalHeight_ - 48 - rows * 32), 208, double(rows * 32)};
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
    if (nodeDrag_ >= 0 && unsigned(drag_) != bandIndex(postView_, nodeDrag_, Frequency))
        send_(UiKind::End, bandIndex(postView_, nodeDrag_, Frequency), 0);
    nodeDrag_ = -1;
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
    char buffer[80];
    format(i, model_.values[i], buffer, sizeof(buffer));
    input_ = originalInput_ = buffer;
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
    if (input_ == originalInput_) {
        cancelText();
        return;
    }
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
    const int target = hit(x, y), node = nodeAt(x, y);
    const bool twice = clicks_.press(target >= 0 ? target
                                     : node >= 0 ? int(parameterCount) + node
                                                 : -1,
                                     x, y, buttonId, time);
    if (twice && target >= 0) {
        cancelText();
        menu_ = -1;
        once(target, parameter(target).initial);
        return;
    }
    if (menu_ >= 0) {
        const auto r = menuBounds();
        if (r.contains(x, y)) {
            const unsigned row = unsigned((y - r.y) / 32);
            if (menu_ == 1000) {
                auto v = defaults();
                if (row == 1) {
                    v[Space] = .65;
                    v[Predelay] = 8;
                    v[Distance] = 15;
                    v[Mix] = 20;
                }
                if (row == 2) {
                    v[Space] = 3.2;
                    v[Style] = 1;
                    v[Character] = 45;
                    v[Brightness] = 45;
                }
                if (row == 3) {
                    v[Space] = 1.8;
                    v[Style] = 2;
                    v[Thickness] = 90;
                    v[Predelay] = 35;
                }
                if (row == 4) {
                    v[Space] = 8;
                    v[Character] = 80;
                    v[Distance] = 80;
                    v[Mix] = 50;
                }
                v[MixLock] = model_.values[MixLock];
                if (v[MixLock])
                    v[Mix] = model_.values[Mix];
                apply(v);
            } else
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
                menu_ = 1000;
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
    for (unsigned i = 0; i < 2; ++i)
        if (viewBounds(i).contains(x, y)) {
            finishGesture();
            postView_ = i == 1;
            selected_ = -1;
            focused_ = Space;
            invalidate();
            return;
        }
    if (target >= 0) {
        focused_ = target;
        if (buttonId != 0 || (mods & PUGL_MOD_CTRL)) {
            textEdit(target);
            return;
        }
        if (parameter(target).stepped) {
            if (parameter(target).max > 1)
                menu_ = target;
            else
                once(target, 1 - model_.values[target]);
            invalidate();
            return;
        }
        const auto r = controlBounds(target);
        if (r.h <= 30 || (unsigned(target) < globals && valueBounds(target).contains(x, y))) {
            finishGesture();
            readout_.press(target, x, y);
            return;
        }
        begin(target);
        dragY_ = y;
        return;
    }
    if (selected_ >= 0 && inspectorBounds().contains(x, y))
        return;
    if (graphBounds().contains(x, y)) {
        if (buttonId != 0) {
            if (node >= 0) {
                selected_ = node;
                textEdit(bandIndex(postView_, node, Amount));
            }
            return;
        }
        if (twice && node >= 0) {
            auto v = model_.values;
            for (unsigned f : {unsigned(Frequency), unsigned(Amount), unsigned(Q)})
                v[bandIndex(postView_, node, f)] = parameter(bandIndex(postView_, node, f)).initial;
            apply(v);
            return;
        }
        int b = node;
        if (b < 0)
            for (unsigned n = 0; n < bands; ++n)
                if (!model_.values[bandIndex(postView_, n, Enabled)]) {
                    b = n;
                    break;
                }
        if (b < 0)
            return;
        selected_ = b;
        focused_ = bandIndex(postView_, b, Amount);
        if (!available(focused_))
            focused_ = bandIndex(postView_, b, Frequency);
        const auto old = model_.values;
        if (node < 0)
            once(bandIndex(postView_, b, Enabled), 1);
        begin(focused_);
        nodeDrag_ = b;
        if (node < 0) {
            if (!undo_.empty())
                undo_.pop_back();
            before_ = old;
        }
        if (unsigned(focused_) != bandIndex(postView_, b, Frequency))
            send_(UiKind::Begin, bandIndex(postView_, b, Frequency), 0);
        motion(x, y, mods);
        return;
    }
    if (x > logicalWidth_ - 90 && y > 300)
        send_(UiKind::ClearClip, 0, 0);
    selected_ = -1;
    invalidate();
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
            dragY_ = startY;
            (void)startX;
        })) {
        invalidate();
        return;
    }
    if (drag_ >= 0) {
        if (nodeDrag_ >= 0) {
            const auto g = graphBounds();
            const auto i = bandIndex(postView_, nodeDrag_, 0);
            change(i + Frequency, 20 * std::pow(1000., std::clamp((x - g.x) / g.w, 0., 1.)));
            const double n = std::clamp(1 - (y - g.y) / g.h, 0., 1.);
            if (available(i + Amount))
                change(i + Amount, postView_ ? -24 + 48 * n : 25 * std::pow(16., n));
        } else {
            change(drag_, denormalized(drag_, normalized(drag_, model_.values[drag_]) +
                                                  (dragY_ - y) *
                                                      ((mods & PUGL_MOD_SHIFT) ? .0005 : .005)));
            dragY_ = y;
        }
    }
    invalidate();
}
void Editor::scroll(double x, double y, double delta, unsigned mods) {
    if (menu_ >= 0 || editing_ >= 0)
        return;
    int i = hit(x, y);
    const int node = nodeAt(x, y);
    if (i < 0 && node >= 0)
        i = bandIndex(postView_, node, Q);
    if (i < 0)
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
        selected_ = -1;
        focused_ = Space;
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
    if ((k == PUGL_KEY_DELETE || k == 127) && selected_ >= 0) {
        once(bandIndex(postView_, selected_, Enabled), 0);
        selected_ = -1;
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
void Editor::drawValue(cairo_t *cr, Rect r, unsigned i, double size) {
    const bool editing = editing_ == int(i);
    const auto tint = i == Mix ? teal : accent;
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
void Editor::drawGraph(cairo_t *cr) {
    const auto g = graphBounds();
    const auto blue = hex(0x75c6df);
    auto xx = [&](double hz) { return g.x + g.w * std::log(hz / 20) / std::log(1000.); };
    for (double hz : {20., 30., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) {
        const double x = xx(hz);
        line(cr, x, g.y - 10, x, g.y + g.h + 12, dim.alpha(.12));
        const std::string s =
            hz >= 1000 ? std::to_string(int(hz / 1000)) + "k" : std::to_string(int(hz));
        text(cr, s, x, g.y + g.h + 30, 10, muted, false, 1);
    }
    for (unsigned n = 0; n <= 4; ++n) {
        const double y = g.y + g.h * n / 4;
        line(cr, 0, y, g.x + g.w, y, dim.alpha(n == 2 ? .28 : .12));
        text(cr, std::to_string(400 >> n) + "%", g.x - 10, y + 4, 10, blue.alpha(.8), false, 2);
        text(cr, std::to_string(24 - int(n) * 12), g.x + g.w + 10, y + 4, 10, accent.alpha(.8));
    }
    cairo_save(cr);
    cairo_rectangle(cr, g.x, g.y, g.w, g.h);
    cairo_clip(cr);
    // Persistent ribbons are previous measured wet spectra. Their opacity follows
    // age; their contour follows captured audio, never the current parameter curve.
    const double bottom = g.y + g.h;
    auto trace = [&](const DecayHistory::Curve &curve, bool smooth = false) {
        cairo_new_path(cr);
        const double dx = g.w / (DecayHistory::points - 1);
        auto yy = [&](unsigned n) {
            return g.y + g.h * (1 - std::clamp((curve[n] + 96) / 84., 0., 1.));
        };
        cairo_move_to(cr, g.x, yy(0));
        for (unsigned n = 1; n < DecayHistory::points; ++n) {
            const double x = g.x + dx * (n - 1), previous = yy(n - 1), next = yy(n);
            // Horizontal cubic tangents keep a soft contour without overshoot.
            if (smooth)
                cairo_curve_to(cr, x + dx * .4, previous, x + dx * .6, next, x + dx, next);
            else
                cairo_line_to(cr, x + dx, next);
        }
    };
    tailGlow_.paint(cr, g, decayHistory_);
    trace(decayHistory_.output, true);
    color(cr, hex(0xdde3e4, .08));
    cairo_set_line_width(cr, 4);
    cairo_stroke_preserve(cr);
    color(cr, hex(0xe1e6e5, .78));
    cairo_set_line_width(cr, 1.35);
    cairo_stroke_preserve(cr);
    cairo_line_to(cr, g.x + g.w, bottom);
    cairo_line_to(cr, g.x, bottom);
    cairo_close_path(cr);
    auto *outputFill = cairo_pattern_create_linear(0, g.y, 0, bottom);
    theme::stop(outputFill, 0, hex(0xe1e6e5, .075));
    theme::stop(outputFill, 1, hex(0xe1e6e5, 0));
    cairo_set_source(cr, outputFill);
    cairo_fill(cr);
    cairo_pattern_destroy(outputFill);
    for (unsigned layer = 0; layer < 2; ++layer) {
        cairo_new_path(cr);
        for (unsigned n = 0; n <= 500; ++n) {
            const double f = 20 * std::pow(1000., n / 500.);
            if (f > model_.rate * .475)
                break;
            const double value =
                layer ? (postResponse(model_.effective, f, model_.rate) + 24) / 48
                      : (std::log2(decayResponse(model_.effective, f, model_.rate) * 4) / 4);
            const double y = g.y + g.h * (1 - value);
            if (!n)
                cairo_move_to(cr, g.x, y);
            else
                cairo_line_to(cr, g.x + g.w * n / 500., y);
        }
        color(cr, (layer ? accent : blue).alpha(postView_ == bool(layer) ? 1 : .5));
        cairo_set_line_width(cr, postView_ == bool(layer) ? 2.3 : 1.6);
        cairo_stroke_preserve(cr);
        cairo_line_to(cr, g.x + g.w, bottom);
        cairo_line_to(cr, g.x, bottom);
        cairo_close_path(cr);
        color(cr, (layer ? accent : blue).alpha(.025));
        cairo_fill(cr);
    }
    cairo_restore(cr);
    for (unsigned b = 0; b < bands; ++b)
        if (model_.values[bandIndex(postView_, b, Enabled)]) {
            const auto r = nodeBounds(b);
            const auto tint = postView_ ? accent : blue;
            if (selected_ == int(b))
                circle(cr, r.x + 9, r.y + 9, 13, tint.alpha(.12));
            circle(cr, r.x + 9, r.y + 9, 6.5, theme::background);
            circle(cr, r.x + 9, r.y + 9, 5, tint);
            text(cr, std::to_string(b + 1), r.x + 9, r.y - 7, 9, tint, true, 1);
        }
    bool any = false;
    for (unsigned b = 0; b < bands; ++b)
        any = any || model_.values[bandIndex(postView_, b, Enabled)];
    if (selected_ < 0 && !any)
        centeredText(cr, "Click to add a band  /  Drag to shape  /  Scroll for Q",
                     {g.x, g.y + g.h - 35, g.w, 20}, 11, dim);
}
void Editor::drawMeters(cairo_t *cr) {
    const double x = logicalWidth_ - 50, top = 326, bottom = logicalHeight_ - 84;
    centeredText(cr, "OUT", {x - 10, 276, 42, 20}, 9, muted, true);
    const double peak = std::max(model_.peaks[2], model_.peaks[3]);
    centeredText(cr, peak < 1e-5 ? "-inf" : number(db(peak)), {x - 16, 298, 50, 20}, 10,
                 model_.clipped ? red : ink);
    for (unsigned ch = 0; ch < 2; ++ch)
        meterBar(cr, {x + ch * 10, top, 6, bottom - top}, (db(model_.peaks[2 + ch]) + 90) / 90);
    for (int n = 0; n <= 90; n += 18)
        text(cr, std::to_string(-n), x - 7, top + n / 90. * (bottom - top) + 3, 8, dim, false, 2);
    text(cr, model_.clipped ? "CLIP" : "dBFS", x + 6, bottom + 25, 9, model_.clipped ? red : dim,
         true, 1);
}
void Editor::paint(cairo_t *cr, double width, double height) {
    logicalWidth_ = width;
    logicalHeight_ = height;
    cairo_save(cr);
    cairo_scale(cr, scale_, scale_);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    box(cr, {0, 0, width, height}, theme::background);
    theme::gradient(cr, {0, 60, width, height - 104}, hex(0x141719), hex(0x1b2023));
    theme::gradient(cr, {0, 0, width, 60}, hex(0x32363a), hex(0x272b2e));
    drawBrand(cr, Brand::Reverb, {24, 19, 255, 25});
    const char *commands[]{"Starting points v",
                           "Undo",
                           "Redo",
                           "A",
                           "B",
                           comparison_ == 0 ? "A > B" : "B > A",
                           "Help"};
    for (unsigned i = 0; i < 7; ++i)
        drawButton(cr, headerBounds(i), commands[i],
                   (i == 3 && comparison_ == 0) || (i == 4 && comparison_ == 1),
                   i == 1   ? !undo_.empty()
                   : i == 2 ? !redo_.empty()
                            : true);
    theme::gradient(cr, {0, 60, width, 208}, hex(0x292e32), hex(0x252b2f));
    line(cr, 0, 267, width, 267, hex(0xffffff, .035));
    for (unsigned i : mainControls) {
        const auto r = controlBounds(i), dial = dialBounds(i);
        const bool big = i == Space;
        const double factor = big ? 1.46 : i == Distance || i == Brightness ? .96 : .8;
        cairo_save(cr);
        cairo_translate(cr, r.x + r.w / 2, dial.y + dial.h / 2);
        cairo_scale(cr, factor, factor);
        theme::knob(cr, {-50, -40, 100, 80}, normalized(i, model_.values[i]),
                    normalized(i, parameter(i).initial), i == Mix ? teal : accent,
                    focused_ == int(i), true, big);
        cairo_restore(cr);
        centeredText(cr, parameter(i).name, captionBounds(i), big ? 12 : 11, muted, true);
        if (big) {
            const auto v = valueBounds(i);
            if (editing_ == int(i))
                drawValue(cr, v, i, 17);
            else {
                const double seconds = model_.values[Space] * model_.values[DecayRate] * .01;
                centeredText(cr, number(seconds) + " s", v, 23, ink, true);
                if (v.contains(mouseX_, mouseY_))
                    line(cr, v.x + 14, v.y + v.h - 1, v.x + v.w - 14, v.y + v.h - 1,
                         accent.alpha(.65));
            }
        } else
            drawValue(cr, valueBounds(i), i, 12);
    }
    for (unsigned i : {unsigned(Style), unsigned(PredelaySync), unsigned(AutoGate),
                       unsigned(Freeze), unsigned(MixLock)}) {
        std::string caption = i == Style          ? label(i, model_.values[i]) + " v"
                              : i == PredelaySync ? "Sync: " + label(i, model_.values[i]) + " v"
                                                  : parameter(i).name;
        drawButton(cr, controlBounds(i), caption, parameter(i).max == 1 && model_.values[i]);
    }
    for (unsigned i : {unsigned(DecayRate), unsigned(GateHold), unsigned(PredelayOffset)})
        if (available(i))
            drawValue(cr, controlBounds(i), i, 11);
    drawGraph(cr);
    drawMeters(cr);
    drawButton(cr, viewBounds(0), "Decay Rate EQ", !postView_);
    drawButton(cr, viewBounds(1), "Post EQ", postView_);
    centeredText(cr, postView_ ? "Shape the wet signal" : "Shape the tail across frequency",
                 {width / 2 - 150, 272, 300, 24}, 11, dim);
    if (selected_ >= 0) {
        const auto p = inspectorBounds();
        theme::raised(cr, p, 10, true);
        text(cr,
             std::string(postView_ ? "POST EQ  /  " : "DECAY EQ  /  ") +
                 std::to_string(selected_ + 1),
             p.x + 14, p.y + 22, 10, postView_ ? accent : hex(0x75c6df), true);
        text(cr, "Delete removes  /  Esc closes", p.x + p.w - 14, p.y + 22, 9, dim, false, 2);
        const auto i = bandIndex(postView_, selected_, 0);
        for (unsigned f = 0; f < fields; ++f) {
            const auto r = controlBounds(i + f);
            if (f == Enabled || f == Type)
                drawButton(cr, r,
                           f == Enabled ? "Enabled" : label(i + f, model_.values[i + f]) + " v",
                           f == Enabled && model_.values[i + f]);
            else if (available(i + f))
                drawValue(cr, r, i + f, 11);
            else {
                theme::well(cr, r, 4);
                centeredText(cr, "—", r, 11, dim);
            }
            if (f == Frequency || f == Amount || f == Q)
                centeredText(cr, parameter(i + f).name, {r.x, p.y + 70, r.w, 14}, 9, dim);
        }
    }
    theme::gradient(cr, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(cr, 0, height - 44, width, height - 44, hex(0x000000, .7));
    centeredText(cr, "Reverb + Out", {16, height - 36, 100, 28}, 11, muted);
    centeredText(cr, "Tail history  2.2 s", {126, height - 36, 132, 28}, 10, dim);
    centeredText(cr, model_.revision == 1 ? "Legacy engine" : "Refined engine",
                 {270, height - 36, 100, 28}, 10, dim);
    for (unsigned i : {unsigned(Input), unsigned(Output)}) {
        const auto r = controlBounds(i);
        text(cr, parameter(i).name, r.x, r.y + 18, 11, muted);
        drawValue(cr, {r.x + 46, r.y + 2, r.w - 46, r.h - 4}, i, 12);
    }
    drawButton(cr, controlBounds(Bypass), model_.values[Bypass] ? "Bypassed" : "Bypass",
               model_.values[Bypass]);
    if (menu_ >= 0) {
        const auto r = menuBounds();
        theme::raised(cr, r, 8, true);
        const char *presets[]{"Default space", "Close room", "Vintage hall", "Vocal plate",
                              "Long atmosphere"};
        const unsigned rows = menu_ == 1000 ? 5 : unsigned(parameter(menu_).max) + 1;
        for (unsigned n = 0; n < rows; ++n) {
            const Rect row{r.x + 4, r.y + n * 32 + 2, r.w - 8, 28};
            if (row.contains(mouseX_, mouseY_))
                theme::well(cr, row, 4);
            text(cr, menu_ == 1000 ? presets[n] : choice(menu_, n), row.x + 14, row.y + 19, 12,
                 ink);
        }
    }
    if (editing_ >= 0 && parameter(editing_).stepped) {
        const auto r = controlBounds(editing_);
        theme::raised(cr, {r.x, r.y + 36, 180, 40}, 6, true);
        centeredText(cr, input_, {r.x + 5, r.y + 40, 170, 30}, 12, inputError_ ? red : ink);
    }
    if (help_) {
        const Rect r{width / 2 - 280, 130, 560, 286};
        theme::raised(cr, r, 12, true);
        text(cr, "Reverb controls", r.x + 24, r.y + 34, 16, ink, true);
        const char *lines[]{
            "Drag knobs or readouts; Shift is fine. Click readouts to type.",
            "Double-click restores the descriptor default. Tab / Enter / arrows edit.",
            "Choose Decay Rate EQ or Post EQ, then click the canvas to add a band.",
            "Drag nodes for frequency and amount; scroll for Q. Delete removes.",
            "Decay shapes feedback loss; Post EQ shapes the wet output.",
            "A/B compares settings. Ctrl+Z / Ctrl+Shift+Z undo / redo.",
            "Lock Mix preserves mix when choosing a starting preset.",
            "Sync follows host tempo; Offset appears below Sync when enabled.",
            "Freeze sustains the tank. Gate hold is in ms. Meters are sample peaks.",
            "Gold ribbons are captured wet spectra; white is the current output."};
        for (unsigned n = 0; n < 10; ++n)
            text(cr, lines[n], r.x + 24, r.y + 62 + n * 21, 11, muted);
    }
    cairo_restore(cr);
}
} // namespace openfilter::reverb
