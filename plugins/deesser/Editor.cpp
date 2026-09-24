#include "Editor.hpp"
#include "Engine.hpp"
#include <numbers>
#include <openfilter/ui/Brand.hpp>
#include <openfilter/ui/Meters.hpp>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>

namespace openfilter::deesser {
using namespace ui;
namespace {
const auto accent = hex(0xb9d879), ink = theme::ink, muted = theme::muted, dim = theme::dim;
const auto teal = hex(0x7ac4b3), red = hex(0xe48f85);
std::string label(unsigned i, double value) {
    char s[80];
    format(i, value, s, sizeof(s));
    return (value >= 0 && (i == Input || i == Output) ? "+" : "") + std::string(s);
}
std::string number(double v) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.1f", v);
    return s;
}
double db(double v) {
    return 20 * std::log10(std::max(1e-4, v));
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
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter De-Esser");
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
    if (!spectrum_.update(analysis_.spectrum.frames.read()))
        spectrum_.decay(elapsed);
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
    return {28, 104, logicalWidth_ - 214, logicalHeight_ - 424};
}
bool Editor::available(unsigned) const {
    return true;
}
Rect Editor::controlBounds(unsigned i) const {
    const double w = logicalWidth_ - 160, bottom = workspaceBottom();
    if (i == Threshold || i == Range)
        return {w * (i == Threshold ? .125 : .335) - 65, bottom - 256, 130, 174};
    if (i == Low || i == High)
        return {32 + (i == High ? w * .40 - 104 : 0), bottom - 36, 104, 26};
    if (i == Detection || i == Processing || i == Sidechain)
        return {w * .48, bottom - (i == Detection ? 215 : i == Processing ? 149 : 91), w * .30, 32};
    if (i == Audition)
        return {w * .48, bottom - 43, w * .30, 28};
    if (i == StereoLink || i == Lookahead)
        return {w * .82, bottom - (i == StereoLink ? 230 : 122), w * .16, 102};
    if (i == Attack || i == Release)
        return {logicalWidth_ - 580 + (i == Release ? 120 : 0), logicalHeight_ - 36, 112, 28};
    if (i == Input)
        return {logicalWidth_ - 340, logicalHeight_ - 36, 94, 28};
    if (i == Output)
        return {logicalWidth_ - 234, logicalHeight_ - 36, 94, 28};
    if (i == Bypass)
        return {logicalWidth_ - 128, logicalHeight_ - 36, 108, 28};
    return {};
}
Rect Editor::bandBounds() const {
    return {32, workspaceBottom() - 76, (logicalWidth_ - 160) * .40, 36};
}
Rect Editor::menuBounds() const {
    const auto r = menu_ == 100 ? headerBounds(0) : controlBounds(menu_);
    return {r.x, menu_ == 100 ? r.y + r.h + 8 : r.y - 80, menu_ == 100 ? 200. : r.w,
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
    bandDrag_ = false;
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
    mouseX_ = x;
    mouseY_ = y;
    const int target = hit(x, y);
    const auto band = bandBounds();
    const int handle =
        band.contains(x, y)
            ? (std::abs(x -
                        (band.x + band.w * std::log(model_.values[Low] / 1000) / std::log(22.))) <
                       std::abs(x - (band.x +
                                     band.w * std::log(model_.values[High] / 1000) / std::log(22.)))
                   ? int(Low)
                   : int(High))
            : -1;
    const int clickTarget = menu_ >= 0 || help_ ? -1 : handle >= 0 ? handle : target;
    const bool twice = clicks_.press(clickTarget, x, y, buttonId, time);
    if (twice && clickTarget >= 0) {
        cancelText();
        menu_ = -1;
        once(clickTarget, parameter(clickTarget).initial);
        return;
    }
    if (menu_ >= 0) {
        const auto r = menuBounds();
        if (r.contains(x, y)) {
            const unsigned row = static_cast<unsigned>((y - r.y) / (menu_ == 100 ? 36 : 36));
            if (menu_ == 100) {
                auto v = defaults();
                if (row == 1) {
                    v[Low] = 4500;
                    v[High] = 11000;
                    v[Range] = 4;
                    v[Threshold] = -32;
                }
                if (row == 2) {
                    v[Low] = 7000;
                    v[High] = 16000;
                    v[Range] = 7;
                    v[Threshold] = -28;
                }
                if (row == 3) {
                    v[Detection] = 1;
                    v[Range] = 3;
                    v[Attack] = 2;
                    v[Release] = 120;
                }
                if (row == 4) {
                    v[Processing] = 0;
                    v[Range] = 4;
                    v[Low] = 5500;
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
    if (handle >= 0) {
        focused_ = handle;
        begin(handle);
        bandDrag_ = true;
        motion(x, y, mods);
        return;
    }
    if (target >= 0) {
        focused_ = target;
        const auto r = controlBounds(target);
        if (buttonId != 0 || (mods & PUGL_MOD_CTRL)) {
            textEdit(target);
            return;
        }
        if (target == Detection || target == Processing || target == Sidechain) {
            once(target, x < r.x + r.w / 2 ? 0 : 1);
            return;
        }
        if (parameter(target).stepped) {
            once(target, 1 - model_.values[target]);
            return;
        }
        if ((r.h == 174 && y > r.y + 145) || (r.h == 102 && y > r.y + 77) || r.h == 26 ||
            r.h == 28) {
            textEdit(target);
            return;
        }
        begin(target);
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (x > logicalWidth_ - 160 && y > 70)
        send_(UiKind::ClearClip, 0, 0);
}
void Editor::release(double, double) {
    finishGesture();
    invalidate();
}
void Editor::motion(double x, double y, unsigned mods) {
    mouseX_ = x;
    mouseY_ = y;
    clicks_.motion(x, y);
    if (drag_ >= 0) {
        if (bandDrag_) {
            const auto b = bandBounds();
            change(drag_, 1000 * std::pow(22., std::clamp((x - b.x) / b.w, 0., 1.)));
        } else {
            const double delta = dragY_ - y;
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
    const double right = logicalWidth_ - 158, center = g.y + g.h * .5;
    // Display-only square-root scaling exposes quiet syllables without claiming
    // that waveform height is a dBFS axis. Zero and polarity stay meaningful.
    auto height = [&](double amplitude) {
        return g.h * .47 * std::sqrt(std::clamp(std::abs(amplitude), 0., 1.));
    };
    auto frame = [&](unsigned n) -> const MeterFrame & {
        return history_[(historyPosition_ + n) % history_.size()];
    };
    auto envelope = [&](bool detected) {
        cairo_new_path(cr);
        for (unsigned n = 0; n < history_.size(); ++n) {
            const auto &f = frame(n);
            const double amplitude =
                detected ? std::min(f.positive, f.detectedPositive) : f.positive;
            const double x = right * n / (history_.size() - 1), y = center - height(amplitude);
            if (n == 0)
                cairo_move_to(cr, x, y);
            else
                cairo_line_to(cr, x, y);
        }
        for (unsigned n = history_.size(); n-- > 0;) {
            const auto &f = frame(n);
            const double amplitude =
                detected ? std::max(f.negative, f.detectedNegative) : f.negative;
            cairo_line_to(cr, right * n / (history_.size() - 1), center + height(amplitude));
        }
        cairo_close_path(cr);
    };
    cairo_save(cr);
    cairo_rectangle(cr, 0, g.y - 10, right, g.h + 20);
    cairo_clip(cr);
    for (int n = -3; n <= 3; ++n)
        if (n != 0)
            line(cr, 0, center + n * g.h / 6., right, center + n * g.h / 6., dim.alpha(.09));
    envelope(false);
    color(cr, muted.alpha(.105));
    cairo_fill_preserve(cr);
    color(cr, muted.alpha(.27));
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    envelope(true);
    auto *fill = cairo_pattern_create_linear(0, center - g.h * .4, 0, center + g.h * .4);
    theme::stop(fill, 0, accent.alpha(.62));
    theme::stop(fill, .5, accent.alpha(.24));
    theme::stop(fill, 1, accent.alpha(.62));
    cairo_set_source(cr, fill);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(fill);
    color(cr, accent.alpha(.78));
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);
    // The center line is the waveform origin, not a threshold or gain trace.
    line(cr, 0, center, right, center, accent.alpha(.58), .8);
    auto *fade = cairo_pattern_create_linear(0, 0, right, 0);
    theme::stop(fade, 0, hex(0x171b1d, .68));
    theme::stop(fade, .05, hex(0x171b1d, 0));
    theme::stop(fade, .95, hex(0x171b1d, 0));
    theme::stop(fade, 1, hex(0x171b1d, .35));
    cairo_rectangle(cr, 0, g.y - 10, right, g.h + 20);
    cairo_set_source(cr, fade);
    cairo_fill(cr);
    cairo_pattern_destroy(fade);
    cairo_restore(cr);
    text(cr, "INPUT", 28, 83, 9, dim, true);
    text(cr, "SIBILANCE", 82, 83, 9, accent.alpha(.8), true);
    text(cr, "6 s", right - 28, 83, 9, dim);
}
void Editor::drawMeters(cairo_t *cr) {
    const double top = 128, bottom = workspaceBottom() - 24;
    line(cr, logicalWidth_ - 142, 88, logicalWidth_ - 142, bottom, theme::border.alpha(.3));
    for (unsigned group = 0; group < 3; ++group) {
        const bool gr = group == 1;
        const double x = logicalWidth_ - (group == 0   ? 118
                                          : group == 1 ? 80
                                                       : 40),
                     center = x + (gr ? 2.5 : 6.5);
        text(cr, group == 0 ? "IN" : gr ? "GR" : "OUT", center, 99, 9, muted, true, 1);
        const unsigned start = group == 0 ? 0 : 2;
        const double peak =
            gr ? model_.reduction : std::max(model_.peaks[start], model_.peaks[start + 1]);
        text(cr,
             gr            ? number(peak)
             : peak < 1e-5 ? "-inf"
                           : number(db(peak)),
             center, 115, 9, group == 2 && model_.clipped ? hex(0xef8c76) : muted, false, 1);
        if (gr)
            meterBar(cr, {x, top, 5, bottom - top}, peak / 24., true);
        else
            for (unsigned channel = 0; channel < 2; ++channel)
                meterBar(cr, {x + channel * 8, top, 5, bottom - top},
                         (db(model_.peaks[start + channel]) + 60) / 60.);
        text(cr,
             gr                             ? "dB"
             : group == 2 && model_.clipped ? "CLIP"
                                            : "dBFS",
             center, bottom + 17, 9, group == 2 && model_.clipped ? hex(0xef8c76) : dim, true, 1);
    }
    for (int n = 0; n <= 24; n += 6) {
        const double y = top + n / 24. * (bottom - top);
        text(cr, std::to_string(n), logicalWidth_ - 59, y + 3, 8, dim, false, 2);
    }
    for (int n = 0; n <= 60; n += 12) {
        const double y = top + n / 60. * (bottom - top);
        text(cr, std::to_string(-n), logicalWidth_ - 6, y + 3, 8, dim, false, 2);
    }
}
void Editor::drawValue(cairo_t *cr, Rect r, unsigned i, double size) {
    const bool editing = editing_ == int(i);
    const auto tint = i == Output ? teal : accent;
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
    drawBrand(cr, Brand::Deesser, {24, 17, 228, 26});
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
    for (unsigned i : {Threshold, Range, StereoLink, Lookahead}) {
        const auto r = controlBounds(i);
        const bool large = i == Threshold || i == Range;
        cairo_save(cr);
        cairo_translate(cr, r.x + r.w / 2, r.y + (large ? 62 : 30));
        const double scale = large ? 1.55 : .72;
        cairo_scale(cr, scale, scale);
        theme::knob(cr, {-50, -40, 100, 80}, normalized(i, model_.values[i]),
                    normalized(i, parameter(i).initial), accent, focused_ == int(i), true, large);
        cairo_restore(cr);
        centeredText(cr, parameter(i).name, {r.x, r.y + (large ? 126 : 59), r.w, 17}, 11, muted);
        drawValue(cr, {r.x + 8, r.y + (large ? 148 : 79), r.w - 16, 22}, i, large ? 14 : 12);
    }
    const auto band = bandBounds();
    theme::well(cr, band, 5);
    auto edge = [&](unsigned i) {
        return band.x + band.w * std::log(model_.values[i] / 1000) / std::log(22.);
    };
    const double lo = std::min(edge(Low), edge(High)), hi = std::max(edge(Low), edge(High));
    box(cr, {lo, band.y + 3, hi - lo, band.h - 6}, accent.alpha(.07));
    cairo_save(cr);
    cairo_rectangle(cr, band.x + 3, band.y + 3, band.w - 6, band.h - 6);
    cairo_clip(cr);
    for (double hz : {2000., 4000., 8000., 16000.}) {
        const double x = band.x + band.w * std::log(hz / 1000) / std::log(22.);
        line(cr, x, band.y + 3, x, band.y + band.h - 3, dim.alpha(.2));
    }
    const unsigned columns = static_cast<unsigned>(band.w);
    for (unsigned n = 0; n < columns; ++n) {
        const double x = band.x + n;
        const double lowHz = 1000 * std::pow(22., n / band.w);
        const double highHz = 1000 * std::pow(22., (n + 1) / band.w);
        const double level = spectrum_.level(lowHz, highHz);
        const double amount = std::clamp((level + 90) / 84., 0., 1.);
        const double height = amount * (band.h - 6);
        const bool selected = x >= lo && x <= hi;
        const auto tint = selected ? accent : muted;
        box(cr, {x, band.y + band.h - 3 - height, 1.2, height}, tint.alpha(selected ? .68 : .27));
    }
    cairo_restore(cr);
    for (unsigned i : {Low, High}) {
        const double x = edge(i);
        line(cr, x, band.y + 3, x, band.y + band.h - 3, accent, 2);
        circle(cr, x, band.y + band.h / 2, 4, ink);
        drawValue(cr, controlBounds(i), i, 12);
    }
    centeredText(cr, "DETECTION BAND", {band.x, workspaceBottom() - 36, band.w, 26}, 9, dim);
    for (unsigned i : {Detection, Processing, Sidechain}) {
        const auto r = controlBounds(i);
        centeredText(cr,
                     i == Detection    ? "DETECTION"
                     : i == Processing ? "PROCESSING"
                                       : "SIDE CHAIN",
                     {r.x, r.y - 23, r.w, 17}, 10, dim);
        const double half = r.w / 2;
        for (unsigned n = 0; n < 2; ++n)
            drawButton(cr, {r.x + n * half, r.y, half - 2, r.h}, choice(i, n),
                       model_.values[i] == n);
    }
    drawButton(cr, controlBounds(Audition),
               model_.values[Audition] != 0 ? "Audition · ON" : "Audition",
               model_.values[Audition] != 0);
    drawMeters(cr);
    theme::gradient(cr, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(cr, 0, height - 44, width, height - 44, hex(0x000000, .7));
    line(cr, 0, height - 43, width, height - 43, hex(0xffffff, .09));
    centeredText(cr, "15 ms latency", {20, height - 36, 90, 28}, 11, muted);
    if (width > 1200)
        centeredText(cr,
                     std::string(model_.mono ? "Mono" : "Stereo") + " / " +
                         number(model_.rate / 1000) + " kHz",
                     {126, height - 36, 120, 28}, 10, dim);
    for (unsigned i : {Attack, Release, Input, Output}) {
        const auto r = controlBounds(i);
        text(cr, parameter(i).name, r.x, r.y + 18, 11, muted);
        const Rect field{r.x + (i == Attack || i == Release ? 47 : 40), r.y + 2,
                         r.w - (i == Attack || i == Release ? 47 : 40), r.h - 4};
        drawValue(cr, field, i, 12);
    }
    drawButton(cr, controlBounds(Bypass), model_.values[Bypass] != 0 ? "Bypassed" : "Bypass",
               model_.values[Bypass] != 0);
    if (menu_ >= 0) {
        const auto r = menuBounds();
        theme::raised(cr, r, 8, true);
        const char *presets[]{"Default", "Gentle vocal", "Bright vocal", "Cymbal control",
                              "Wide vocal"};
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
        text(cr, "De-Esser controls", r.x + 24, r.y + 34, 15, ink, true);
        const char *lines[]{"Drag a knob vertically. Shift makes fine adjustments.",
                            "Double-click any value to reset its descriptor default.",
                            "Click a value or right-click a control for exact entry.",
                            "Tab focuses controls; Enter edits; arrows adjust; Esc cancels.",
                            "Use Threshold and the detection-band handles to tune sibilance.",
                            "A/B compares settings. Copy stores the active slot into the other.",
                            "Ctrl+Z / Ctrl+Shift+Z undo / redo. Presets are starting points.",
                            "GR is attenuation; meters are sample peaks, not true peaks.",
                            "External SC needs host routing. Split Band is a minimum-phase shelf."};
        for (unsigned n = 0; n < 9; ++n)
            text(cr, lines[n], r.x + 24, r.y + 63 + n * 21, 11, muted);
    }
    cairo_restore(cr);
}
} // namespace openfilter::deesser
