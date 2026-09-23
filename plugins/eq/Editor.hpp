#pragma once
#include "Parameters.hpp"
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
namespace openfilter::eq {
struct EditorState {
    Values values = defaults(), effective = defaults();
    std::array<double, 4> peaks{};
    bool clipped = false, mono = false;
    double rate = 48000;
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
    Editor(Read read, Send send, ui::AnalysisTap &analysis);
    ~Editor();
    bool attach(uintptr_t parent);
    bool show();
    void hide();
    void tick();
    bool resize(unsigned width, unsigned height);
    bool setScale(double scale);
    void finishGesture();
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    double scale() const { return scale_; }
    uintptr_t nativeWindow() const;
    void paint(cairo_t *cr, double width, double height);
    // Same input paths used by native events and deterministic interaction tests.
    void press(double x, double y, unsigned button, unsigned mods, double time);
    void release(double x, double y);
    void motion(double x, double y, unsigned mods);
    void scroll(double x, double y, double delta, unsigned mods);
    void key(uint32_t key, unsigned mods);
    void input(std::string_view text);
    int selectedBand() const { return selected_; }
    ui::Rect controlBounds(unsigned f) const { return control(f); }
    ui::Rect panelBounds() const { return panel(); }
    ui::Rect graphBounds() const { return graph(); }
    ui::Rect headerBounds(int id) const { return headerButton(id); }

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
    ui::Spectrum spectrum_;
    std::chrono::steady_clock::time_point lastTick_ = std::chrono::steady_clock::now(),
                                          lastAudio_ = lastTick_;
    std::array<bool, bands> present_{};
    int selected_ = -1, hover_ = -1, focused_ = -1, drag_ = -1;
    double mouseX_ = -1, mouseY_ = -1, dragX_ = 0, dragY_ = 0;
    ui::ClickTracker clicks_;
    double panelFrequency_ = 1000;
    bool panelAbove_ = false;
    Values before_ = defaults();
    std::vector<unsigned> gesture_;
    struct History {
        Values values = defaults();
        std::array<bool, bands> present{};
    };
    std::array<bool, bands> beforePresent_{};
    std::vector<History> undo_, redo_;
    std::array<History, 2> comparisons_{};
    int comparison_ = 0;
    double range_ = 12;
    bool pre_ = true, post_ = true, help_ = false;
    int menu_ = -1, menuBand_ = -1;
    ui::Rect menuRect_;
    int editing_ = -1;
    std::string input_;
    bool selectAll_ = false, inputError_ = false;
    std::string hint_;
    static PuglStatus event(PuglView *view, const PuglEvent *event);
    ui::Rect graph() const;
    ui::Rect panel() const;
    ui::Rect control(unsigned field) const;
    ui::Rect headerButton(int id) const;
    ui::Rect footerOutput() const;
    ui::Rect rangeButton() const;
    double xFor(double hz) const;
    double yFor(double db) const;
    double hzFor(double x) const;
    int hitNode(double x, double y) const;
    int hitControl(double x, double y) const;
    bool available(unsigned i) const;
    void selectBand(int band);
    void begin(std::vector<unsigned> indices);
    void change(unsigned index, double value);
    void changeOnce(unsigned index, double value);
    void apply(const Values &values);
    void remember();
    void add(double hz, double gain);
    void remove();
    void undo(bool redo = false);
    void compare(int slot);
    void openMenu(int kind, int band, ui::Rect anchor);
    void textEdit(unsigned index);
    void commitText();
    void cancelText();
    void invalidate();
    Values responseValues_{};
    double responseRate_ = 0, responseWidth_ = 0;
    bool responseMono_ = false;
    std::array<std::vector<double>, bands> bandDb_;
    std::array<std::vector<double>, 2> totalDb_;
    void updateResponse();
    void drawGraph(cairo_t *cr);
    void drawPanel(cairo_t *cr);
    void drawMeters(cairo_t *cr);
    void drawMenu(cairo_t *cr);
    void drawButton(cairo_t *cr, ui::Rect r, std::string_view label, bool active = false,
                    bool enabled = true);
};
} // namespace openfilter::eq
