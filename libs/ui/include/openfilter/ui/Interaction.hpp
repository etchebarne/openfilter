#pragma once
#include <cmath>
namespace openfilter::ui {
// Suite-wide: a double primary click on a value resets its descriptor default.
// Text entry is a readout click, secondary click, or Enter on a focused control.
class ClickTracker {
    int target_ = -1;
    double time_ = -1, x_ = 0, y_ = 0;

  public:
    bool press(int target, double x, double y, unsigned button, double time) {
        const bool twice = button == 0 && target >= 0 && target == target_ && time >= time_ &&
                           time - time_ <= .32 && std::hypot(x - x_, y - y_) <= 5;
        target_ = button == 0 && !twice ? target : -1;
        time_ = time;
        x_ = x;
        y_ = y;
        return twice;
    }
    void motion(double x, double y) {
        if (std::hypot(x - x_, y - y_) > 5)
            target_ = -1;
    }
};
// A numeric readout is both an exact-entry click and a relative drag. Do not
// edit values or begin a host gesture for pointer jitter. Editors use the same
// logical-pixel threshold as ClickTracker, independent of host scale.
class ReadoutInteraction {
    int pending_ = -1;
    double x_ = 0, y_ = 0;
    bool dragging_ = false;

  public:
    void reset() noexcept {
        pending_ = -1;
        dragging_ = false;
    }
    void press(unsigned target, double x, double y) noexcept {
        pending_ = int(target);
        x_ = x;
        y_ = y;
        dragging_ = false;
    }
    bool dragging() const noexcept { return dragging_; }
    // Returns true while a possible click is still pending. begin may call
    // the editor's finishGesture(), which resets this helper, so mark the
    // drag only after the callback. Keep the original position for deltas.
    template <class Begin> bool motion(double x, double y, Begin begin) {
        if (pending_ < 0)
            return false;
        if (std::hypot(x - x_, y - y_) <= 5)
            return true;
        const unsigned target = unsigned(pending_);
        const double startX = x_, startY = y_;
        reset();
        begin(target, startX, startY);
        dragging_ = true;
        return false;
    }
    // Only an unmoved press opens entry. Cancellation/drag returns -1.
    int release() noexcept {
        const int target = pending_;
        reset();
        return target;
    }
};
} // namespace openfilter::ui
