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
} // namespace openfilter::ui
