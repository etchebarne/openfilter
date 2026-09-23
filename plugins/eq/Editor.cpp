#include "Editor.hpp"
#include "Response.hpp"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <openfilter/ui/Theme.hpp>
#include <openfilter/ui/X11Raster.hpp>
#include <pugl/stub.h>

namespace openfilter::eq {
using namespace ui;
namespace {
const auto bg = theme::background, surface = theme::surface, border = theme::border,
           ink = theme::ink, muted = theme::muted, dim = theme::dim, accent = theme::accent;
const auto colors = theme::bands;
Color bandColor(unsigned b) {
    return colors[b % colors.size()];
}
std::string number(double value, int precision = 1) {
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + 63, value, std::chars_format::fixed, precision);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : "—";
}
std::string frequency(double hz) {
    return hz >= 1000 ? number(hz / 1000, 2) + " kHz" : number(hz, 1) + " Hz";
}
double db(double value) {
    return 20 * std::log10(std::max(1e-6, value));
}
void chevron(cairo_t *c, double x, double y) {
    line(c, x - 3, y - 2, x, y + 1, muted);
    line(c, x, y + 1, x + 3, y - 2, muted);
}
// Small filter silhouettes are labels, not a second response display.
void filterGlyph(cairo_t *c, double x, double y, int type, Color tint) {
    line(c, x - 22, y + 3, x + 22, y + 3, dim.alpha(.22));
    cairo_new_path(c);
    if (type == 0 || type == 5) {
        const double peak = type == 0 ? -9 : 12;
        cairo_move_to(c, x - 21, y + 3);
        cairo_curve_to(c, x - 7, y + 3, x - 7, y + peak, x, y + peak);
        cairo_curve_to(c, x + 7, y + peak, x + 7, y + 3, x + 21, y + 3);
    } else if (type == 1 || type == 2) {
        const double a = type == 1 ? -5 : 3, b = type == 1 ? 3 : -5;
        cairo_move_to(c, x - 21, y + a);
        cairo_line_to(c, x - 10, y + a);
        cairo_curve_to(c, x, y + a, x, y + b, x + 10, y + b);
        cairo_line_to(c, x + 21, y + b);
    } else {
        const double direction = type == 3 ? 1 : -1;
        cairo_move_to(c, x - 20 * direction, y + 9);
        cairo_curve_to(c, x - 6 * direction, y - 5, x - 10 * direction, y - 5, x + 4 * direction,
                       y - 5);
        cairo_line_to(c, x + 21 * direction, y - 5);
    }
    color(c, tint);
    cairo_set_line_width(c, 1.5);
    cairo_stroke(c);
}
} // namespace

Editor::Editor(Read read, Send send, AnalysisTap &analysis)
    : read_(std::move(read)), send_(std::move(send)), analysis_(analysis) {
    model_ = read_();
    for (unsigned b = 0; b < bands; ++b) {
        present_[b] = model_.values[index(b, Enabled)] != 0;
        for (unsigned f = 1; f < fields; ++f)
            present_[b] = present_[b] || model_.values[2 + b * fields + f] !=
                                             parameter(2 + b * fields + f).initial;
        if (present_[b] && selected_ < 0)
            selected_ = static_cast<int>(b);
    }
    if (selected_ >= 0)
        selectBand(selected_);
    comparisons_[0] = comparisons_[1] = {model_.values, present_};
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
    puglSetViewString(view_, PUGL_WINDOW_TITLE, "OpenFilter EQ");
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
    if (selected_ >= 0) {
        const double gain =
            model_.values[index(selected_, Type)] < 3 ? model_.values[index(selected_, Gain)] : 0;
        panelAbove_ = yFor(gain) > logicalHeight_ - 292;
    }
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
void Editor::tick() {
    if (!visible_ && world_) {
        puglUpdate(world_, 0);
        return;
    }
    auto next = read_();
    if (next.stateSerial != model_.stateSerial) {
        finishGesture();
        cancelText();
        focused_ = -1;
        undo_.clear();
        redo_.clear();
        present_.fill(false);
        for (unsigned b = 0; b < bands; ++b)
            for (unsigned f = 0; f < fields; ++f)
                present_[b] = present_[b] || next.values[2 + b * fields + f] !=
                                                 parameter(2 + b * fields + f).initial;
        selected_ = -1;
        comparisons_[0] = comparisons_[1] = {next.values, present_};
        comparison_ = 0;
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
    for (unsigned b = 0; b < bands; ++b)
        if (model_.values[index(b, Enabled)] != 0)
            present_[b] = true;
    if (selected_ < 0)
        for (unsigned b = 0; b < bands; ++b)
            if (present_[b]) {
                selected_ = static_cast<int>(b);
                break;
            }
    invalidate();
    if (world_)
        puglUpdate(world_, 0);
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

Rect Editor::graph() const {
    return {0, 60, logicalWidth_ - 120, logicalHeight_ - 104};
}
Rect Editor::panel() const {
    return {std::clamp(xFor(panelFrequency_) - 354., 24., logicalWidth_ - 828.),
            panelAbove_ ? graph().y + 24 : logicalHeight_ - 280, 708, 216};
}
Rect Editor::control(unsigned f) const {
    const auto p = panel();
    if (f == 0)
        return {p.x + 24, p.y + 48, 132, 152};
    if (f == 4)
        return {p.x + 552, p.y + 48, 132, 152};
    if (f == 1)
        return {p.x + 172, p.y + 48, 104, 152};
    if (f == 2)
        return {p.x + 292, p.y + 48, 124, 152};
    return {p.x + 432, p.y + 48, 104, 152};
}
Rect Editor::headerButton(int id) const {
    const double x = logicalWidth_ * .43;
    switch (id) {
    case 0:
        return {x - 82, 16, 28, 28};
    case 1:
        return {x - 50, 16, 28, 28};
    case 2:
        return {x - 6, 16, 28, 28};
    case 3:
        return {x + 24, 16, 28, 28};
    case 4:
        return {x + 60, 16, 54, 28};
    case 5:
        return {logicalWidth_ - 186, 16, 70, 28};
    case 6:
        return {logicalWidth_ - 281, logicalHeight_ - 36, 72, 28};
    default:
        return {};
    }
}
Rect Editor::footerOutput() const {
    return {logicalWidth_ - 199, logicalHeight_ - 36, 180, 28};
}
Rect Editor::rangeButton() const {
    return {logicalWidth_ - 320, 14, 112, 32};
}
void Editor::selectBand(int band) {
    selected_ = band;
    focused_ = -1;
    if (band >= 0) {
        panelFrequency_ = std::exp2(model_.values[index(band, Frequency)]);
        const double gain =
            model_.values[index(band, Type)] < 3 ? model_.values[index(band, Gain)] : 0;
        panelAbove_ = yFor(gain) > logicalHeight_ - 292;
    }
    invalidate();
}
double Editor::xFor(double hz) const {
    const auto g = graph();
    return g.x + g.w * std::log(std::clamp(hz, 10., 30000.) / 10) / std::log(3000.);
}
double Editor::hzFor(double x) const {
    const auto g = graph();
    return 10 * std::pow(3000., std::clamp((x - g.x) / g.w, 0., 1.));
}
double Editor::yFor(double gain) const {
    const auto g = graph();
    return g.y + g.h * (.5 - std::clamp(gain, -range_, range_) / (2 * range_));
}
int Editor::hitNode(double x, double y) const {
    int found = -1;
    double distance = 24;
    for (unsigned b = 0; b < bands; ++b)
        if (present_[b]) {
            const int type = static_cast<int>(model_.values[index(b, Type)]);
            const double nx = xFor(std::exp2(model_.values[index(b, Frequency)])),
                         ny = yFor(type < 3 ? model_.values[index(b, Gain)] : 0);
            const double d = std::hypot(x - nx, y - ny);
            if (d < distance) {
                found = static_cast<int>(b);
                distance = d;
            }
        }
    return found;
}
bool Editor::available(unsigned i) const {
    if (i < 2)
        return true;
    const unsigned b = (i - 2) / fields;
    const auto f = static_cast<Field>((i - 2) % fields);
    const int type = int(model_.values[index(b, Type)]);
    return !(f == Gain && type >= 3) &&
           !(f == Q && isBrickwall(type, int(model_.values[index(b, Slope)])));
}
int Editor::hitControl(double x, double y) const {
    if (footerOutput().contains(x, y))
        return 1;
    if (selected_ < 0)
        return -1;
    for (unsigned f = 1; f <= 3; ++f)
        if (control(f).contains(x, y)) {
            const Field field = f == 1 ? Frequency : f == 2 ? Gain : Q;
            if (!available(index(selected_, field)))
                return -1;
            return static_cast<int>(index(selected_, field));
        }
    return -1;
}
void Editor::begin(std::vector<unsigned> indices) {
    finishGesture();
    before_ = model_.values;
    beforePresent_ = present_;
    gesture_ = std::move(indices);
    for (auto i : gesture_)
        send_(UiKind::Begin, i, 0);
}
void Editor::change(unsigned i, double value) {
    value = parameter(i).constrain(value);
    if (model_.values[i] == value)
        return;
    model_.effective[i] = parameter(i).constrain(model_.effective[i] + value - model_.values[i]);
    model_.values[i] = value;
    send_(UiKind::Value, i, value);
    invalidate();
}
void Editor::remember() {
    if (before_ == model_.values)
        return;
    undo_.push_back({before_, beforePresent_});
    if (undo_.size() > 64)
        undo_.erase(undo_.begin());
    redo_.clear();
}
void Editor::finishGesture() {
    if (gesture_.empty())
        return;
    for (auto i : gesture_)
        send_(UiKind::End, i, 0);
    gesture_.clear();
    remember();
    const bool nodeDrag = drag_ >= 1000;
    drag_ = -1;
    if (nodeDrag && selected_ >= 0)
        selectBand(selected_);
    invalidate();
}
void Editor::changeOnce(unsigned i, double value) {
    begin({i});
    change(i, value);
    finishGesture();
}
void Editor::apply(const Values &v) {
    std::vector<unsigned> changed;
    for (unsigned i = 0; i < parameterCount; ++i)
        if (model_.values[i] != v[i])
            changed.push_back(i);
    begin(changed);
    for (auto i : changed)
        change(i, v[i]);
    finishGesture();
    for (unsigned b = 0; b < bands; ++b)
        if (v[index(b, Enabled)] != 0)
            present_[b] = true;
}
void Editor::add(double hz, double gain) {
    for (unsigned b = 0; b < bands; ++b)
        if (!present_[b]) {
            selected_ = static_cast<int>(b);
            focused_ = -1;
            auto v = model_.values;
            for (unsigned f = 0; f < fields; ++f)
                v[2 + b * fields + f] = parameter(2 + b * fields + f).initial;
            v[index(b, Enabled)] = 1;
            v[index(b, Frequency)] = std::log2(hz);
            v[index(b, Gain)] = gain;
            apply(v);
            selectBand(b);
            return;
        }
    hint_ = "All 24 bands are in use. Remove a band to add another.";
}
void Editor::remove() {
    if (selected_ < 0)
        return;
    const auto b = static_cast<unsigned>(selected_);
    auto v = model_.values;
    for (unsigned f = 0; f < fields; ++f)
        v[2 + b * fields + f] = parameter(2 + b * fields + f).initial;
    apply(v);
    present_[b] = false;
    selected_ = -1;
    focused_ = -1;
    for (unsigned n = 0; n < bands; ++n)
        if (present_[n]) {
            selected_ = static_cast<int>(n);
            break;
        }
}
void Editor::undo(bool redo) {
    finishGesture();
    auto &source = redo ? redo_ : undo_;
    if (source.empty())
        return;
    const auto target = source.back();
    source.pop_back();
    const History current{model_.values, present_};
    const auto savedUndo = undo_, savedRedo = redo_;
    apply(target.values);
    present_ = target.present;
    focused_ = -1;
    if (selected_ < 0 || !present_[selected_]) {
        selected_ = -1;
        for (unsigned b = 0; b < bands; ++b)
            if (present_[b]) {
                selected_ = static_cast<int>(b);
                break;
            }
    }
    undo_ = savedUndo;
    redo_ = savedRedo;
    (redo ? undo_ : redo_).push_back(current);
    invalidate();
}
void Editor::compare(int slot) {
    if (slot == comparison_)
        return;
    comparisons_[comparison_] = {model_.values, present_};
    comparison_ = slot;
    apply(comparisons_[slot].values);
    present_ = comparisons_[slot].present;
    focused_ = -1;
    if (selected_ >= 0 && !present_[selected_])
        selected_ = -1;
}
void Editor::openMenu(int kind, int band, Rect anchor) {
    menu_ = kind;
    menuBand_ = band;
    const int count = kind == 0 ? 6 : kind == 1 ? 5 : int(slopeNames.size());
    menuRect_ = {std::clamp(anchor.x, 16., logicalWidth_ - 206),
                 std::max(74., anchor.y - count * 34 - 10), 190., count * 34. + 10};
    invalidate();
}
void Editor::textEdit(unsigned i) {
    if (!available(i))
        return;
    finishGesture();
    editing_ = static_cast<int>(i);
    focused_ = editing_;
    const int f = i >= 2 ? (i - 2) % fields : -1;
    input_ = number(f == Frequency || f == Q ? std::exp2(model_.values[i]) : model_.values[i],
                    f == Q ? 4 : 2);
    selectAll_ = true;
    inputError_ = false;
    invalidate();
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
    std::string_view input = input_;
    while (!input.empty() && input.front() == ' ')
        input.remove_prefix(1);
    if (!input.empty() && input.front() == '+')
        input.remove_prefix(1);
    double value = 0;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value)) {
        inputError_ = true;
        return;
    }
    std::string_view suffix(result.ptr, input.data() + input.size() - result.ptr);
    while (!suffix.empty() && suffix.front() == ' ')
        suffix.remove_prefix(1);
    while (!suffix.empty() && suffix.back() == ' ')
        suffix.remove_suffix(1);
    const int f = editing_ >= 2 ? (editing_ - 2) % fields : -1;
    if (f == Frequency && (suffix == "k" || suffix == "kHz" || suffix == "khz")) {
        value *= 1000;
        suffix = {};
    }
    if ((f == Frequency && (suffix == "Hz" || suffix == "hz")) ||
        ((f == Gain || editing_ == 1) && (suffix == "dB" || suffix == "db")))
        suffix = {};
    if (!suffix.empty() || ((f == Frequency || f == Q) && value <= 0)) {
        inputError_ = true;
        return;
    }
    if (f == Frequency || f == Q)
        value = std::log2(value);
    const unsigned i = editing_;
    cancelText();
    changeOnce(i, value);
}
void Editor::input(std::string_view textValue) {
    if (editing_ < 0 ||
        std::none_of(textValue.begin(), textValue.end(), [](char c) { return c >= 32 && c < 127; }))
        return;
    if (selectAll_) {
        input_.clear();
        selectAll_ = false;
    }
    for (char c : textValue)
        if (c >= 32 && c < 127 && input_.size() < 32)
            input_ += c;
    inputError_ = false;
    invalidate();
}

void Editor::press(double x, double y, unsigned button, unsigned mods, double time) {
    mouseX_ = x;
    mouseY_ = y;
    if (help_) {
        help_ = false;
        invalidate();
        return;
    }
    hint_.clear();
    const auto p = panel();
    int field = hitControl(x, y);
    int node = selected_ >= 0 && p.contains(x, y) ? -1 : hitNode(x, y);
    if (selected_ >= 0) {
        if (Rect{p.x + 8, p.y + 5, 28, 24}.contains(x, y))
            field = index(selected_, Enabled);
        if (control(0).contains(x, y)) {
            const int type = int(model_.values[index(selected_, Type)]);
            field =
                index(selected_, y > control(0).y + 112 && (type == 3 || type == 4) ? Slope : Type);
        }
        if (control(4).contains(x, y))
            field = index(selected_, Routing);
    }
    if (headerButton(6).contains(x, y))
        field = 0;
    int viewTarget = -1;
    if (rangeButton().contains(x, y))
        viewTarget = 2000;
    if (Rect{logicalWidth_ - 486, logicalHeight_ - 36, 60, 28}.contains(x, y))
        viewTarget = 2001;
    if (Rect{logicalWidth_ - 424, logicalHeight_ - 36, 60, 28}.contains(x, y))
        viewTarget = 2002;
    const bool twice = clicks_.press(field >= 0        ? field
                                     : viewTarget >= 0 ? viewTarget
                                     : node >= 0       ? 1000 + node
                                                       : -1,
                                     x, y, button, time);
    if (twice) {
        cancelText();
        menu_ = -1;
        finishGesture();
        if (viewTarget >= 0) {
            if (viewTarget == 2000)
                range_ = 12;
            if (viewTarget == 2001)
                pre_ = true;
            if (viewTarget == 2002)
                post_ = true;
            invalidate();
            return;
        }
        if (field >= 0)
            changeOnce(field, parameter(field).initial);
        else if (node >= 0) {
            selectBand(node);
            begin({index(node, Frequency), index(node, Gain), index(node, Q)});
            for (const auto f : {Frequency, Gain, Q})
                change(index(node, f), parameter(index(node, f)).initial);
            finishGesture();
            selectBand(node);
        }
        return;
    }
    if (menu_ >= 0) {
        const int item = static_cast<int>((y - menuRect_.y - 5) / 34);
        const int count = menu_ == 0 ? 6 : menu_ == 1 ? 5 : int(slopeNames.size());
        if (menuRect_.contains(x, y) && item >= 0 && item < count)
            changeOnce(index(menuBand_, menu_ == 0 ? Type : menu_ == 1 ? Routing : Slope), item);
        menu_ = -1;
        invalidate();
        return;
    }
    if (editing_ >= 0) {
        commitText();
        if (editing_ >= 0)
            cancelText();
    }
    for (int i = 0; i < 7; ++i)
        if (headerButton(i).contains(x, y) && button == 0) {
            if (i == 0)
                undo();
            if (i == 1)
                undo(true);
            if (i == 2 || i == 3)
                compare(i - 2);
            if (i == 4) {
                comparisons_[1 - comparison_] = {model_.values, present_};
                hint_ = "Copied to comparison slot";
            }
            if (i == 5) {
                apply(defaults());
                present_.fill(false);
                selectBand(-1);
            }
            if (i == 6)
                changeOnce(0, model_.values[0] ? 0 : 1);
            invalidate();
            return;
        }
    if (Rect{16, logicalHeight_ - 36, 86, 28}.contains(x, y)) {
        add(1000, 0);
        return;
    }
    if (Rect{logicalWidth_ - 89, 16, 60, 28}.contains(x, y)) {
        help_ = true;
        invalidate();
        return;
    }
    if (Rect{logicalWidth_ - 486, logicalHeight_ - 36, 60, 28}.contains(x, y)) {
        pre_ = !pre_;
        invalidate();
        return;
    }
    if (Rect{logicalWidth_ - 424, logicalHeight_ - 36, 60, 28}.contains(x, y)) {
        post_ = !post_;
        invalidate();
        return;
    }
    if (rangeButton().contains(x, y)) {
        range_ = range_ == 6 ? 12 : range_ == 12 ? 24 : 6;
        invalidate();
        return;
    }
    if (Rect{logicalWidth_ - 88, 74, 84, logicalHeight_ - 130}.contains(x, y)) {
        send_(UiKind::ClearClip, 0, 0);
        return;
    }
    if (selected_ >= 0 && p.contains(x, y)) {
        if (Rect{p.x + p.w - 32, p.y + 4, 26, 25}.contains(x, y)) {
            selectBand(-1);
            return;
        }
        if (Rect{p.x + p.w - 63, p.y + 4, 26, 25}.contains(x, y)) {
            remove();
            return;
        }
        for (int direction : {-1, 1})
            if (Rect{p.x + (direction < 0 ? 38 : 133), p.y + 4, 24, 25}.contains(x, y)) {
                for (int n = 1; n <= int(bands); ++n) {
                    const int b = (selected_ + direction * n + int(bands)) % int(bands);
                    if (present_[b]) {
                        selectBand(b);
                        return;
                    }
                }
            }
        if (field >= 2) {
            const int f = (field - 2) % fields;
            if (f == Enabled) {
                changeOnce(field, model_.values[field] ? 0 : 1);
                return;
            }
            if (f == Type || f == Slope || f == Routing) {
                openMenu(f == Type      ? 0
                         : f == Routing ? 1
                                        : 2,
                         selected_, f == Routing ? control(4) : control(0));
                return;
            }
        }
    }
    if (field >= 0) {
        focused_ = field;
        const int f = field >= 2 ? (field - 2) % fields : -1;
        const auto r = field == 1 ? footerOutput()
                                  : control(f == Frequency ? 1
                                            : f == Gain    ? 2
                                                           : 3);
        if (button == 1 || (field != 1 && y >= r.y + 120) || (field == 1 && x > r.x + 78) ||
            (mods & PUGL_MOD_CTRL)) {
            textEdit(field);
            return;
        }
        if (button != 0)
            return;
        begin({static_cast<unsigned>(field)});
        drag_ = field;
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (selected_ >= 0 && p.contains(x, y))
        return; // Never create through a floating control.
    if (node >= 0) {
        selectBand(node);
        if (button == 1) {
            openMenu(0, node, {x, y, 0, 0});
            return;
        }
        if (mods & PUGL_MOD_ALT) {
            changeOnce(index(node, Enabled), model_.values[index(node, Enabled)] ? 0 : 1);
            return;
        }
        if (button != 0)
            return;
        std::vector<unsigned> indices{index(node, Frequency)};
        if (model_.values[index(node, Type)] < 3)
            indices.push_back(index(node, Gain));
        begin(indices);
        drag_ = 1000 + node;
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (graph().contains(x, y) && button == 0) {
        const auto g = graph();
        add(hzFor(x), std::clamp((.5 - (y - g.y) / g.h) * 2 * range_, -24., 24.));
        if (selected_ >= 0) {
            begin({index(selected_, Frequency), index(selected_, Gain)});
            drag_ = 1000 + selected_;
            dragX_ = x;
            dragY_ = y;
        }
    }
    invalidate();
}
void Editor::release(double, double) {
    finishGesture();
    invalidate();
}
void Editor::motion(double x, double y, unsigned mods) {
    clicks_.motion(x, y);
    mouseX_ = x;
    mouseY_ = y;
    hover_ = hitNode(x, y);
    if (drag_ >= 1000) {
        const unsigned b = drag_ - 1000;
        const auto g = graph();
        const double precision = mods & PUGL_MOD_SHIFT ? .12 : 1.;
        change(index(b, Frequency), model_.values[index(b, Frequency)] +
                                        (x - dragX_) / g.w * std::log2(3000.) * precision);
        if (model_.values[index(b, Type)] < 3)
            change(index(b, Gain),
                   model_.values[index(b, Gain)] - (y - dragY_) / g.h * 2 * range_ * precision);
        // Follow node edits immediately; knob edits below keep their panel stationary.
        selectBand(static_cast<int>(b));
    } else if (drag_ >= 0) {
        const double precision = mods & PUGL_MOD_SHIFT ? .1 : 1.;
        const int f = drag_ >= 2 ? (drag_ - 2) % fields : -1;
        const double sensitivity = f == Frequency ? .025 : f == Q ? .015 : .15;
        change(drag_, model_.values[drag_] + (dragY_ - y) * sensitivity * precision);
    }
    if (drag_ >= 0) {
        dragX_ = x;
        dragY_ = y;
    }
    invalidate();
}
void Editor::scroll(double x, double y, double delta, unsigned mods) {
    const int node = selected_ >= 0 && panel().contains(x, y) ? -1 : hitNode(x, y);
    int i = hitControl(x, y);
    if (node >= 0) {
        selected_ = node;
        i = index(node, Q);
    }
    if (i < 0 || !available(i))
        return;
    const int f = i >= 2 ? (i - 2) % fields : -1;
    const double step = (f == Frequency ? .06
                         : f == Q       ? .12
                                        : .5) *
                        (mods & PUGL_MOD_SHIFT ? .1 : 1.);
    changeOnce(i, model_.values[i] + delta * step);
}
void Editor::key(uint32_t k, unsigned mods) {
    if (editing_ >= 0) {
        if (k == PUGL_KEY_ENTER || k == PUGL_KEY_PAD_ENTER)
            commitText();
        else if (k == PUGL_KEY_ESCAPE)
            cancelText();
        else if (k == PUGL_KEY_BACKSPACE) {
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
    if (k == PUGL_KEY_ESCAPE) {
        finishGesture();
        menu_ = -1;
        help_ = false;
        selectBand(-1);
        invalidate();
        return;
    }
    if ((mods & PUGL_MOD_CTRL) && (k == 'z' || k == 'Z')) {
        undo(mods & PUGL_MOD_SHIFT);
        return;
    }
    if ((mods & PUGL_MOD_CTRL) && (k == 'y' || k == 'Y')) {
        undo(true);
        return;
    }
    if (k == PUGL_KEY_DELETE || k == PUGL_KEY_BACKSPACE) {
        remove();
        return;
    }
    if (k == 'n' || k == 'N') {
        add(1000, 0);
        return;
    }
    if (k == PUGL_KEY_TAB && selected_ >= 0) {
        const std::array<int, 4> order{int(index(selected_, Frequency)),
                                       int(index(selected_, Gain)), int(index(selected_, Q)), 1};
        auto at = std::find(order.begin(), order.end(), focused_);
        const int offset = at == order.end() ? -1 : static_cast<int>(at - order.begin());
        for (int step = 1; step <= 4; ++step) {
            const int next = order[(offset + ((mods & PUGL_MOD_SHIFT) ? -step : step) + 8) % 4];
            if (available(next)) {
                focused_ = next;
                break;
            }
        }
        invalidate();
        return;
    }
    if (k == PUGL_KEY_ENTER && focused_ >= 0) {
        textEdit(focused_);
        return;
    }
    if (k == ' ' && selected_ >= 0) {
        changeOnce(index(selected_, Enabled), model_.values[index(selected_, Enabled)] ? 0 : 1);
        return;
    }
    if (selected_ < 0)
        return;
    const double fine = mods & PUGL_MOD_SHIFT ? .1 : 1.;
    if (k == PUGL_KEY_LEFT || k == PUGL_KEY_RIGHT)
        changeOnce(index(selected_, Frequency), model_.values[index(selected_, Frequency)] +
                                                    (k == PUGL_KEY_RIGHT ? 1 : -1) * fine / 12);
    if (k == PUGL_KEY_UP || k == PUGL_KEY_DOWN) {
        const unsigned i = focused_ >= 0 ? static_cast<unsigned>(focused_) : index(selected_, Gain);
        if (!available(i))
            return;
        const int f = i >= 2 ? (i - 2) % fields : -1;
        changeOnce(i, model_.values[i] + (k == PUGL_KEY_UP ? 1 : -1) * fine *
                                             (f == Frequency ? .06
                                              : f == Q       ? .12
                                                             : .5));
    }
}

void Editor::drawButton(cairo_t *c, Rect r, std::string_view label, bool active, bool enabled) {
    const bool hover = r.contains(mouseX_, mouseY_);
    theme::button(c, r, active, hover && enabled);
    centeredText(c, label, r, 12, enabled ? (active ? accent : ink) : dim);
}
void Editor::paint(cairo_t *c, double width, double height) {
    logicalWidth_ = width;
    logicalHeight_ = height;
    cairo_save(c);
    cairo_scale(c, scale_, scale_);
    cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
    box(c, {0, 0, width, height}, bg);
    theme::gradient(c, {0, 60, width, height - 104}, hex(0x15181a), hex(0x202426));
    drawGraph(c);
    theme::gradient(c, {0, 0, width, 60}, hex(0x32363a), hex(0x26292d));
    line(c, 0, 60, width, 60, hex(0x000000, .35));
    for (int i = 0; i < 4; ++i)
        line(c, 24 + i * 4, 30 - std::sin(i * 1.8) * 6, 24 + i * 4, 35 + std::sin(i * 1.8) * 6,
             muted, 1.8);
    text(c, "openfilter", 49, 37, 18, ink, true);
    text(c, "EQ", 160, 38, 23, accent);
    for (int i = 0; i < 2; ++i) {
        const auto r = headerButton(i);
        drawButton(c, r, "", false, !(i ? redo_.empty() : undo_.empty()));
        cairo_save(c);
        cairo_translate(c, r.x + r.w / 2, r.y + r.h / 2);
        if (i)
            cairo_scale(c, -1, 1);
        const auto col = (i ? redo_.empty() : undo_.empty()) ? dim : ink;
        color(c, col);
        cairo_set_line_width(c, 1.5);
        cairo_move_to(c, -6, -3);
        cairo_curve_to(c, 8, -6, 10, 6, 0, 6);
        cairo_stroke(c);
        line(c, -6, -3, -2, -7, col, 1.5);
        line(c, -6, -3, -2, 1, col, 1.5);
        cairo_restore(c);
    }
    drawButton(c, headerButton(2), "A", comparison_ == 0);
    drawButton(c, headerButton(3), "B", comparison_ == 1);
    drawButton(c, headerButton(4), comparison_ == 0 ? "A > B" : "B > A");
    const auto range = rangeButton();
    drawButton(c, range, "");
    centeredText(c, "Range", {range.x + 8, range.y, 38, range.h}, 10, muted);
    centeredText(c, number(range_, 0) + " dB", {range.x + 51, range.y, 42, range.h}, 12, accent);
    chevron(c, range.x + range.w - 12, range.y + range.h / 2);
    drawButton(c, headerButton(5), "Reset all");
    drawButton(c, {width - 89, 16, 60, 28}, "Help");
    drawMeters(c);
    drawPanel(c);
    theme::gradient(c, {0, height - 44, width, 44}, hex(0x34393d), hex(0x2a2e31));
    line(c, 0, height - 44, width, height - 44, hex(0x000000, .7));
    line(c, 0, height - 43, width, height - 43, hex(0xffffff, .09));
    drawButton(c, {16, height - 36, 86, 28}, "+ Add band");
    line(c, 112, height - 32, 112, height - 12, border.alpha(.5));
    centeredText(c, "Zero latency", {126, height - 36, 84, 28}, 11, muted);
    if (width >= 1040)
        centeredText(c,
                     std::string(model_.mono ? "Mono" : "Stereo") + " / " +
                         number(model_.rate / 1000, 1) + " kHz",
                     {226, height - 36, 114, 28}, 10, dim);
    centeredText(c, "Analyzer", {width - 549, height - 36, 56, 28}, 11, muted);
    drawButton(c, {width - 486, height - 36, 60, 28}, "Pre", pre_);
    drawButton(c, {width - 424, height - 36, 60, 28}, "Post", post_);
    drawButton(c, headerButton(6), model_.values[0] ? "Bypassed" : "Bypass", model_.values[0] != 0);
    const auto output = footerOutput();
    line(c, output.x - 4, height - 32, output.x - 4, height - 12, border.alpha(.5));
    const Rect outputValue{output.x + 77, output.y + 2, output.w - 77, output.h - 4};
    theme::well(c, outputValue, 4, focused_ == 1 || output.contains(mouseX_, mouseY_));
    centeredText(c, "Output", {output.x + 8, output.y, 58, output.h}, 11, muted);
    centeredText(c,
                 editing_ == 1
                     ? input_
                     : (model_.values[1] >= 0 ? "+" : "") + number(model_.values[1]) + " dB",
                 outputValue, 12, editing_ == 1 ? accent : ink, true);
    if (!hint_.empty()) {
        pill(c, {width / 2 - 135, 56, 270, 27}, surface, border, 5);
        text(c, hint_, width / 2, 74, 11, ink, false, 1);
    }
    if (model_.values[0]) {
        pill(c, {width / 2 - 65, 88, 130, 26}, hex(0x392f21), accent.alpha(.3));
        text(c, "EQ BYPASSED", width / 2, 106, 11, accent, true, 1);
    }
    drawMenu(c);
    if (help_) {
        Rect r{width - 426, 68, 400, 280};
        theme::raised(c, r, 8, true);
        text(c, "Editing your sound", r.x + 20, r.y + 30, 15, ink, true);
        const char *rows[]{"Click empty graph                 Create a band",
                           "Drag node / scroll                  Frequency + gain / Q",
                           "Double-click a control           Reset to its default",
                           "Double-click a node              Reset frequency, gain + Q",
                           "Click a value / right-click      Type an exact value",
                           "Shift + drag                          Fine adjustment",
                           "Alt + click node                    Enable / disable band",
                           "Delete / Ctrl + Z                    Remove / undo",
                           "Tab, then Enter                     Focus / edit value",
                           "Escape                                  Close band controls"};
        for (unsigned n = 0; n < 10; ++n)
            text(c, rows[n], r.x + 20, r.y + 57 + n * 21, 11, muted);
    }
    cairo_restore(c);
}

void Editor::updateResponse() {
    const double width = graph().w;
    if (responseValues_ == model_.effective && responseRate_ == model_.rate &&
        responseWidth_ == width && responseMono_ == model_.mono)
        return;
    responseValues_ = model_.effective;
    responseRate_ = model_.rate;
    responseWidth_ = width;
    responseMono_ = model_.mono;
    const unsigned count = static_cast<unsigned>(width) / 2 + 1;
    for (auto &curve : bandDb_)
        curve.resize(count);
    for (auto &curve : totalDb_)
        curve.resize(count);
    for (unsigned n = 0; n < count; ++n) {
        const double hz = hzFor(graph().x + n * 2);
        for (unsigned b = 0; b < bands; ++b)
            bandDb_[b][n] = hz < model_.rate * .499
                                ? db(std::abs(bandTransfer(model_.effective, b, hz, model_.rate)))
                                : 0;
        const auto r = totalTransfer(model_.effective, std::min(hz, model_.rate * .499),
                                     model_.rate, model_.mono);
        totalDb_[0][n] = 10 * std::log10(std::max(1e-12, std::norm(r.ll) + std::norm(r.lr)));
        totalDb_[1][n] = 10 * std::log10(std::max(1e-12, std::norm(r.rl) + std::norm(r.rr)));
    }
}
void Editor::drawGraph(cairo_t *c) {
    updateResponse();
    const auto g = graph();
    const auto responseY = [&](double level) { return g.y + g.h * (.5 - level / (2 * range_)); };
    cairo_save(c);
    cairo_rectangle(c, g.x, g.y, g.w, g.h);
    cairo_clip(c);
    for (double f : {10., 20., 30., 50., 100., 200., 300., 500., 1000., 2000., 3000., 5000., 10000.,
                     20000., 30000.})
        line(c, xFor(f), g.y, xFor(f), g.y + g.h, hex(0x778289, .15));
    for (int n = -2; n <= 2; ++n)
        line(c, g.x, yFor(n * range_ / 3), g.x + g.w, yFor(n * range_ / 3),
             n == 0 ? hex(0xb0b8bd, .3) : hex(0x778289, .19));
    for (unsigned channel = 0; channel < 2; ++channel)
        if (channel == 0 ? pre_ : post_) {
            cairo_new_path(c);
            cairo_move_to(c, g.x, g.y + g.h);
            for (int px = 0; px <= static_cast<int>(g.w); px += 2) {
                const double hz = hzFor(g.x + px);
                const double level = hz < model_.rate * .5 ? spectrum_.at(channel, hz) : -120;
                const double y = g.y + g.h * (1 - std::clamp((level + 96) / 96, 0., 1.));
                cairo_line_to(c, g.x + px, y);
            }
            cairo_line_to(c, g.x + g.w, g.y + g.h);
            cairo_close_path(c);
            color(c, channel == 0 ? hex(0xb4bdc1, .075) : hex(0xc1c9cd, .11));
            cairo_fill_preserve(c);
            color(c, channel == 0 ? hex(0xa6b2b9, .23) : hex(0xc9d1d5, .4));
            cairo_set_line_width(c, 1);
            cairo_stroke(c);
        }
    for (unsigned fillBand = 0; fillBand < bands; ++fillBand)
        if (present_[fillBand] && model_.values[index(fillBand, Enabled)] != 0) {
            const auto col = bandColor(fillBand);
            const int type = int(model_.values[index(fillBand, Type)]);
            const bool cut = type == 3 || type == 4;
            const double baseline = cut ? g.y + g.h : yFor(0);
            cairo_new_path(c);
            cairo_move_to(c, g.x, baseline);
            for (int px = 0; px <= static_cast<int>(g.w); px += 2) {
                const double level = bandDb_[fillBand][px / 2];
                cairo_line_to(c, g.x + px, responseY(level));
            }
            cairo_line_to(c, g.x + g.w, baseline);
            cairo_close_path(c);
            color(c,
                  col.alpha(selected_ == int(fillBand) ? (cut ? .20 : .25) : (cut ? .075 : .12)));
            cairo_fill(c);
        }
    for (unsigned b = 0; b < bands; ++b)
        if (present_[b]) {
            const bool enabled = model_.values[index(b, Enabled)] != 0;
            const auto col = bandColor(b);
            cairo_new_path(c);
            bool first = true;
            for (int px = 0; px <= static_cast<int>(g.w); px += 2) {
                const double hz = hzFor(g.x + px);
                if (hz >= model_.rate * .499)
                    break;
                const double y = responseY(bandDb_[b][px / 2]);
                if (first) {
                    cairo_move_to(c, g.x + px, y);
                    first = false;
                } else
                    cairo_line_to(c, g.x + px, y);
            }
            color(c, col.alpha(enabled ? (selected_ == int(b) ? .85 : .5) : .2));
            cairo_set_line_width(c, selected_ == int(b) ? 1.7 : 1.1);
            cairo_stroke(c);
        }
    for (unsigned channel = 0; channel < (model_.mono ? 1u : 2u); ++channel) {
        cairo_new_path(c);
        bool first = true;
        for (int px = 0; px <= static_cast<int>(g.w); px += 2) {
            const double hz = hzFor(g.x + px);
            if (hz >= model_.rate * .499)
                break;
            const double y = responseY(totalDb_[channel][px / 2]);
            if (first) {
                cairo_move_to(c, g.x + px, y);
                first = false;
            } else
                cairo_line_to(c, g.x + px, y);
        }
        color(c, channel == 0 ? accent : hex(0xece2ac, .42));
        cairo_set_line_width(c, channel == 0 ? 2.2 : 1.2);
        cairo_stroke(c);
    }
    cairo_restore(c);
    for (double f : {10., 20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.})
        text(c, f >= 1000 ? number(f / 1000, 0) + "k" : number(f, 0), std::max(12., xFor(f)),
             g.y + g.h - 10, 10, dim, false, 1);
    for (int n = -3; n <= 3; ++n)
        text(c, (n > 0 ? "+" : "") + number(n * range_ / 3, 0), g.x + g.w + 20,
             std::clamp(yFor(n * range_ / 3) + 4, g.y + 14, g.y + g.h - 10), 10,
             n == 0 ? muted : accent.alpha(.65), false, 2);
    for (unsigned b = 0; b < bands; ++b)
        if (present_[b]) {
            const bool enabled = model_.values[index(b, Enabled)] != 0;
            const auto col = bandColor(b);
            const int type = static_cast<int>(model_.values[index(b, Type)]);
            const double x = xFor(std::exp2(model_.values[index(b, Frequency)])),
                         y = yFor(type < 3 ? model_.values[index(b, Gain)] : 0);
            if (selected_ == int(b)) {
                circle(c, x, y, 17, col.alpha(.1));
                circle(c, x, y, 12, col.alpha(.18));
            }
            circle(c, x, y, selected_ == int(b) ? 7.5 : 5.5, enabled ? col : surface);
            color(c, col);
            cairo_set_line_width(c, 1.6);
            cairo_arc(c, x, y, selected_ == int(b) ? 7.5 : 5.5, 0, 2 * std::numbers::pi);
            cairo_stroke(c);
            if (selected_ == int(b) || hover_ == int(b))
                text(c, std::to_string(b + 1), x, y + 3, 9, enabled ? bg : col, true, 1);
            if (hover_ == int(b) || drag_ == 1000 + int(b)) {
                const std::string caption =
                    frequency(std::exp2(model_.values[index(b, Frequency)])) + "   " +
                    (type < 3 ? (model_.values[index(b, Gain)] >= 0 ? "+" : "") +
                                    number(model_.values[index(b, Gain)]) + " dB"
                     : isBrickwall(type, int(model_.values[index(b, Slope)]))
                         ? "Brickwall"
                         : "Q " + number(std::exp2(model_.values[index(b, Q)]), 2));
                const double tx = std::clamp(x - 86., g.x, g.x + g.w - 172);
                pill(c, {tx, y - 49, 172, 29}, surface, bandColor(b).alpha(.35), 5);
                text(c, caption, tx + 86, y - 30, 11, ink, false, 1);
            }
        }
    if (std::none_of(present_.begin(), present_.end(), [](bool p) { return p; })) {
        text(c, "Click the graph to create a band", g.x + g.w / 2, g.y + g.h * .3, 18, muted, false,
             1);
        text(c, "Drag to shape the curve. Double-click controls to reset.", g.x + g.w / 2,
             g.y + g.h * .3 + 25, 12, dim, false, 1);
    }
}
void Editor::drawPanel(cairo_t *c) {
    if (selected_ < 0)
        return;
    const auto p = panel();
    const auto col = bandColor(selected_);
    theme::raised(c, p, 14, true);
    line(c, p.x + 12, p.y + 32, p.x + p.w - 12, p.y + 32, hex(0x000000, .2));
    line(c, p.x + 12, p.y + 33, p.x + p.w - 12, p.y + 33, hex(0xffffff, .035));
    for (double x : {p.x + 164, p.x + 544}) {
        line(c, x, p.y + 54, x, p.y + 192, hex(0x000000, .18));
        line(c, x + 1, p.y + 54, x + 1, p.y + 192, hex(0xffffff, .04));
    }
    theme::well(c, {p.x + 12, p.y + 6, 20, 22}, 6);

    const bool enabled = model_.values[index(selected_, Enabled)] != 0;
    const auto power = enabled ? col : hex(0xea8f86);
    color(c, power);
    cairo_set_line_width(c, 1.5);
    cairo_arc(c, p.x + 22, p.y + 17, 6, -.25 * std::numbers::pi, 1.25 * std::numbers::pi);
    cairo_stroke(c);
    line(c, p.x + 22, p.y + 9, p.x + 22, p.y + 16, power, 1.5);
    drawButton(c, {p.x + 38, p.y + 4, 24, 25}, "");
    line(c, p.x + 52, p.y + 13, p.x + 48, p.y + 16.5, muted, 1.5);
    line(c, p.x + 48, p.y + 16.5, p.x + 52, p.y + 20, muted, 1.5);
    text(c, "Band " + std::to_string(selected_ + 1), p.x + 97, p.y + 21, 11, col, true, 1);
    drawButton(c, {p.x + 133, p.y + 4, 24, 25}, "");
    line(c, p.x + 143, p.y + 13, p.x + 147, p.y + 16.5, muted, 1.5);
    line(c, p.x + 147, p.y + 16.5, p.x + 143, p.y + 20, muted, 1.5);
    for (const Rect r :
         {Rect{p.x + p.w - 63, p.y + 4, 26, 25}, Rect{p.x + p.w - 32, p.y + 4, 26, 25}})
        if (r.contains(mouseX_, mouseY_))
            theme::well(c, r, 5);
    // Trash and close are distinct actions; closing retains every band.
    const double tx = p.x + p.w - 50, ty = p.y + 16;
    line(c, tx - 5, ty - 4, tx + 5, ty - 4, muted, 1.3);
    line(c, tx - 2, ty - 7, tx + 2, ty - 7, muted, 1.3);
    line(c, tx - 4, ty - 2, tx - 3, ty + 5, muted, 1.3);
    line(c, tx + 4, ty - 2, tx + 3, ty + 5, muted, 1.3);
    line(c, tx - 3, ty + 5, tx + 3, ty + 5, muted, 1.3);
    const double xx = p.x + p.w - 19;
    line(c, xx - 3, ty - 3, xx + 3, ty + 3, muted, 1.5);
    line(c, xx - 3, ty + 3, xx + 3, ty - 3, muted, 1.5);
    const int type = int(model_.values[index(selected_, Type)]);
    for (unsigned f = 0; f < 5; ++f) {
        const auto r = control(f);
        if (f == 0 || f == 4) {
            const bool hover = r.contains(mouseX_, mouseY_);
            centeredText(c, f == 0 ? "Filter" : "Channel", {r.x, r.y - 4, r.w, 20}, 11, muted);
            if (f == 0)
                filterGlyph(c, r.x + r.w / 2, r.y + 29, type, col.alpha(.8));
            else {
                const double cx = r.x + r.w / 2;
                line(c, cx - 14, r.y + 29, cx + 14, r.y + 29, dim.alpha(.65), 1.5);
                for (double x : {cx - 14, cx + 14}) {
                    circle(c, x, r.y + 29, 3.5, surface);
                    color(c, muted);
                    cairo_arc(c, x, r.y + 29, 3.5, 0, 2 * std::numbers::pi);
                    cairo_set_line_width(c, 1.2);
                    cairo_stroke(c);
                }
            }
            const Rect selector{r.x + 4, r.y + 48, r.w - 8, 32};
            theme::raised(c, selector, 5, false, hover);
            centeredText(c,
                         f == 0 ? shapeNames[type]
                                : routeNames[int(model_.values[index(selected_, Routing)])],
                         {selector.x + 5, selector.y, selector.w - 22, selector.h}, 12, ink);
            chevron(c, selector.x + selector.w - 13, selector.y + selector.h / 2);
            if (f == 0 && (type == 3 || type == 4)) {
                const Rect slope{r.x + 4, r.y + 120, r.w - 8, 28};
                theme::well(c, slope, 4);
                centeredText(c, slopeNames[int(model_.values[index(selected_, Slope)])],
                             {slope.x + 4, slope.y, slope.w - 22, slope.h}, 11, muted);
                chevron(c, slope.x + slope.w - 13, slope.y + slope.h / 2);
            }
            continue;
        }
        const unsigned i = index(selected_, f == 1 ? Frequency : f == 2 ? Gain : Q);
        const auto descriptor = parameter(i);
        const bool disabled = !available(i), editing = editing_ == int(i);
        theme::knob(c, {r.x, r.y + 24, r.w, 90},
                    (model_.values[i] - descriptor.min) / (descriptor.max - descriptor.min),
                    (descriptor.initial - descriptor.min) / (descriptor.max - descriptor.min), col,
                    focused_ == int(i), !disabled, f == 2);
        centeredText(c,
                     f == 1   ? "Frequency"
                     : f == 2 ? "Gain"
                              : "Q",
                     {r.x, r.y - 4, r.w, 20}, 11, disabled ? dim : muted);
        const Rect valueBox{r.x + 2, r.y + 120, r.w - 4, 28};
        theme::well(c, valueBox, 4, editing || valueBox.contains(mouseX_, mouseY_),
                    inputError_ && editing ? hex(0xea8f86) : col);
        if (editing && selectAll_)
            box(c, {valueBox.x + 4, valueBox.y + 3, valueBox.w - 8, valueBox.h - 6},
                col.alpha(.17));
        const std::string value =
            disabled  ? "—"
            : editing ? input_
            : f == 1  ? frequency(std::exp2(model_.values[i]))
            : f == 2  ? (model_.values[i] >= 0 ? "+" : "") + number(model_.values[i]) + " dB"
                      : number(std::exp2(model_.values[i]), 2);
        cairo_save(c);
        cairo_rectangle(c, valueBox.x, valueBox.y, valueBox.w, valueBox.h);
        cairo_clip(c);
        centeredText(c, value, valueBox, 13, disabled ? dim : editing ? col : ink, true);
        cairo_restore(c);
    }
}
void Editor::drawMeters(cairo_t *c) {
    const double left = logicalWidth_ - 78, top = 128, bottom = logicalHeight_ - 68;
    line(c, left - 14, 88, left - 14, bottom, border.alpha(.3));
    for (unsigned group = 0; group < 2; ++group) {
        const double x = left + group * 30;
        text(c, group ? "OUT" : "IN", x + 6.5, 99, 9, muted, true, 1);
        const double peak = std::max(model_.peaks[2 * group], model_.peaks[2 * group + 1]);
        text(c, peak < 1e-5 ? "-inf" : number(db(peak), 1), x + 6.5, 115, 9,
             group == 1 && model_.clipped ? hex(0xef8c76) : muted, false, 1);
        for (unsigned channel = 0; channel < 2; ++channel) {
            const double bx = x + channel * 8;
            theme::well(c, {bx - .5, top - 1, 6, bottom - top + 2}, 2);
            const double level =
                std::clamp((db(model_.peaks[2 * group + channel]) + 60) / 60, 0., 1.);
            const double levelY = bottom - level * (bottom - top);
            auto *gradient = cairo_pattern_create_linear(0, bottom, 0, top);
            theme::stop(gradient, 0, hex(0x6ba783));
            theme::stop(gradient, .75, hex(0xc1c479));
            theme::stop(gradient, 1, accent);
            cairo_set_source(c, gradient);
            cairo_rectangle(c, bx, levelY, 5, bottom - levelY);
            cairo_fill(c);
            cairo_pattern_destroy(gradient);
            // Fine segmentation improves level reading without changing peak ballistics.
            for (double y = bottom - 5; y > levelY; y -= 6)
                line(c, bx, y, bx + 5, y, bg.alpha(.7), 1);
        }
    }
    for (int level = 0; level >= -60; level -= 12) {
        const double y = bottom - (level + 60) / 60. * (bottom - top);
        text(c, std::to_string(level), logicalWidth_ - 6, y + 3, 8, dim, false, 2);
    }
    text(c, model_.clipped ? "CLIP" : "dBFS", left + 21, bottom + 17, 9,
         model_.clipped ? hex(0xef8c76) : dim, true, 1);
}
void Editor::drawMenu(cairo_t *c) {
    if (menu_ < 0)
        return;
    theme::raised(c, menuRect_, 8, true);
    const int count = menu_ == 0 ? 6 : menu_ == 1 ? 5 : int(slopeNames.size());
    const int selected = static_cast<int>(model_.values[index(menuBand_, menu_ == 0   ? Type
                                                                         : menu_ == 1 ? Routing
                                                                                      : Slope)]);
    for (int i = 0; i < count; ++i) {
        Rect row{menuRect_.x + 5, menuRect_.y + 5 + i * 34, menuRect_.w - 10, 32};
        if (row.contains(mouseX_, mouseY_))
            theme::well(c, row, 4);
        const auto *label = menu_ == 0 ? shapeNames[i] : menu_ == 1 ? routeNames[i] : slopeNames[i];
        if (selected == i)
            circle(c, row.x + 12, row.y + 16, 3, bandColor(menuBand_));
        text(c, label, row.x + 26, row.y + 21, 12, selected == i ? ink : muted);
    }
}
} // namespace openfilter::eq
