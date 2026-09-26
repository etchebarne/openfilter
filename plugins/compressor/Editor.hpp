#pragma once
#include "Meter.hpp"
#include "Parameters.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <openfilter/ui/Draw.hpp>
#include <openfilter/ui/Interaction.hpp>
#include <pugl/pugl.h>
#include <string>
#include <vector>
namespace openfilter::ui {
class X11Raster;
}
namespace openfilter::compressor {
struct EditorState {
    Values values = defaults(), effective = defaults();
    std::array<double, 4> peaks{};
    bool clipped = false, mono = false;
    double rate = 48000, reduction = 0;
    uint64_t stateSerial = 0;
};
enum class UiKind { Begin, Value, End, ClearClip };
struct UiMessage {
    UiKind kind = UiKind::Value;
    unsigned index = 0;
    double value = 0;
    uint64_t serial = 0, stateSerial = 0;
};
class Editor {
  public:
    using Read = std::function<EditorState()>;
    using Send = std::function<void(UiKind, unsigned, double)>;
    Editor(Read read, Send send, MeterTap &tap);
    ~Editor();
    bool attach(uintptr_t parent);
    bool show();
    void hide();
    void tick();
    bool resize(unsigned w, unsigned h);
    bool setScale(double scale);
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    double scale() const { return scale_; }
    uintptr_t nativeWindow() const;
    void paint(cairo_t *cr, double width, double height);
    void press(double x, double y, unsigned button, unsigned mods, double time);
    void release(double x, double y);
    void motion(double x, double y, unsigned mods);
    void scroll(double x, double y, double delta, unsigned mods);
    void key(uint32_t key, unsigned mods);
    void input(std::string_view text);
    void finishGesture();
    ui::Rect controlBounds(unsigned i) const;
    ui::Rect graphBounds() const;
    ui::Rect headerBounds(unsigned i) const;
    ui::Rect viewBounds(unsigned i) const;

  private:
    Read read_;
    Send send_;
    MeterTap &analysis_;
    PuglWorld *world_ = nullptr;
    PuglView *view_ = nullptr;
    std::unique_ptr<ui::X11Raster> raster_;
    bool realized_ = false, visible_ = false;
    unsigned width_ = 1120, height_ = 720;
    double scale_ = 1, logicalWidth_ = 1120, logicalHeight_ = 720;
    EditorState model_;
    std::array<MeterFrame, 600> history_{};
    unsigned historyPosition_ = 0;
    std::chrono::steady_clock::time_point lastAudio_ = std::chrono::steady_clock::now(),
                                          lastTick_ = lastAudio_;
    int drag_ = -1, focused_ = Threshold, editing_ = -1, menu_ = -1;
    bool graphDrag_ = false, help_ = false, selectAll_ = false, inputError_ = false;
    bool kneeVisible_ = true, sidechainVisible_ = false;
    double dragX_ = 0, dragY_ = 0, mouseX_ = -1, mouseY_ = -1;
    std::string input_;
    ui::ClickTracker clicks_;
    ui::ReadoutInteraction readout_;
    Values before_ = defaults();
    std::vector<Values> undo_, redo_;
    std::array<Values, 2> comparisons_{defaults(), defaults()};
    int comparison_ = 0;
    static PuglStatus event(PuglView *view, const PuglEvent *e);
    void invalidate();
    int hit(double x, double y) const;
    bool available(unsigned i) const;
    double workspaceBottom() const;
    void begin(unsigned i);
    void change(unsigned i, double value);
    void once(unsigned i, double value);
    void apply(const Values &values);
    void undo(bool redo);
    void textEdit(unsigned i);
    void commitText();
    void cancelText();
    ui::Rect menuBounds() const;
    void drawGraph(cairo_t *cr);
    void drawMeters(cairo_t *cr);
    void drawButton(cairo_t *cr, ui::Rect r, std::string_view label, bool active = false,
                    bool enabled = true);
    void drawValue(cairo_t *cr, ui::Rect r, unsigned i, double size = 13);
};
} // namespace openfilter::compressor
