#pragma once
#include "Theme.hpp"

namespace openfilter::ui {
// The suite's original EQ meter material: narrow recessed lane, continuous level
// fill, six-pixel segmentation. This painter does not own meter ballistics.
// Level meters grow upwards; gain reduction grows downwards in its own dB scale.
inline void meterBar(cairo_t *c, Rect lane, double amount, bool reduction = false) {
    amount = std::clamp(amount, 0., 1.);
    const double top = lane.y, bottom = lane.y + lane.h;
    theme::well(c, {lane.x - .5, top - 1, lane.w + 1, lane.h + 2}, 2);
    const double edge = reduction ? top + amount * lane.h : bottom - amount * lane.h;
    auto *gradient =
        cairo_pattern_create_linear(0, reduction ? top : bottom, 0, reduction ? bottom : top);
    theme::stop(gradient, 0, reduction ? hex(0xd8ae76) : hex(0x6ba783));
    theme::stop(gradient, .75, reduction ? hex(0xdf9980) : hex(0xc1c479));
    theme::stop(gradient, 1, reduction ? hex(0xef8c76) : theme::accent);
    cairo_set_source(c, gradient);
    cairo_rectangle(c, lane.x, reduction ? top : edge, lane.w, amount * lane.h);
    cairo_fill(c);
    cairo_pattern_destroy(gradient);
    if (reduction) {
        for (double y = top + 5; y < edge; y += 6)
            line(c, lane.x, y, lane.x + lane.w, y, theme::background.alpha(.7), 1);
    } else {
        for (double y = bottom - 5; y > edge; y -= 6)
            line(c, lane.x, y, lane.x + lane.w, y, theme::background.alpha(.7), 1);
    }
}
} // namespace openfilter::ui
