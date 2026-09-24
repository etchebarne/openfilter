#pragma once
#include "DecayHistory.hpp"
#include "Parameters.hpp"
#include "TailGlow.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <openfilter/ui/Analysis.hpp>
#include <openfilter/ui/Draw.hpp>
#include <openfilter/ui/Interaction.hpp>
#include <pugl/pugl.h>
#include <string>
#include <vector>
namespace openfilter::ui {
class X11Raster;
}
namespace openfilter::reverb {
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
    Editor(Read read, Send send, ui::AnalysisTap &tap);
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
    ui::Rect dialBounds(unsigned i) const;
    ui::Rect captionBounds(unsigned i) const;
    ui::Rect valueBounds(unsigned i) const;
    ui::Rect headerBounds(unsigned i) const;
    ui::Rect viewBounds(unsigned i) const;

  private:
    Read read_;
    Send send_;
    ui::AnalysisTap &analysis_;
    PuglWorld *world_ = nullptr;
    PuglView *view_ = nullptr;
    std::unique_ptr<ui::X11Raster> raster_;
    bool realized_ = false, visible_ = false;
    unsigned width_ = 1120, height_ = 720;
    double scale_ = 1, logicalWidth_ = 1120, logicalHeight_ = 720;
    EditorState model_;
    DecayHistory decayHistory_;
    TailGlow tailGlow_;
    int selected_ = -1, nodeDrag_ = -1;
    bool postView_ = false;
    std::chrono::steady_clock::time_point lastAudio_ = std::chrono::steady_clock::now(),
                                          lastTick_ = lastAudio_;
    int drag_ = -1, focused_ = Space, editing_ = -1, menu_ = -1;
    bool help_ = false, selectAll_ = false, inputError_ = false;

    double dragY_ = 0, mouseX_ = -1, mouseY_ = -1;
    std::string input_, originalInput_;
    ui::ClickTracker clicks_;
    Values before_ = defaults();
    std::vector<Values> undo_, redo_;
    std::array<Values, 2> comparisons_{defaults(), defaults()};
    int comparison_ = 0;
    static PuglStatus event(PuglView *view, const PuglEvent *e);
    void invalidate();
    int hit(double x, double y) const;
    bool available(unsigned i) const;
    ui::Rect inspectorBounds() const;
    ui::Rect nodeBounds(unsigned band) const;
    int nodeAt(double x, double y) const;
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
} // namespace openfilter::reverb
