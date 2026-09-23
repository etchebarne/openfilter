#pragma once
#include "Draw.hpp"
#include <array>
#include <numbers>

namespace openfilter::ui::theme {
inline const Color background = hex(0x1d2022), surface = hex(0x34383b), border = hex(0x4b5053),
                   ink = hex(0xe9eae7), muted = hex(0xb5babd), dim = hex(0x858d92),
                   accent = hex(0xe6c77a);
inline const std::array<Color, 8> bands{hex(0x8acb92), hex(0xb5a0de), hex(0x79bdce), hex(0xdba276),
                                        hex(0xd58caa), hex(0x7ac4b3), hex(0x95a9e0), hex(0xc5c479)};

inline void stop(cairo_pattern_t *g, double position, Color v) {
    cairo_pattern_add_color_stop_rgba(g, position, v.r, v.g, v.b, v.a);
}
inline void gradient(cairo_t *c, Rect r, Color top, Color bottom, double radius = 0) {
    auto *g = cairo_pattern_create_linear(r.x, r.y, r.x, r.y + r.h);
    stop(g, 0, top);
    stop(g, 1, bottom);
    rounded(c, r, radius);
    cairo_set_source(c, g);
    cairo_fill(c);
    cairo_pattern_destroy(g);
}
// Bounded, vector-only penumbra: no bitmap assets, blur buffers or scale caches.
// All material is lit from the upper left. Highlights never encode selection.
inline void shadow(cairo_t *c, Rect r, double radius, bool floating = false) {
    for (int n = 4; n >= 1; --n) {
        rounded(c, {r.x + (floating ? 1.5 : 2), r.y + (floating ? 5. : 2.5), r.w, r.h}, radius);
        color(c, hex(0x000000, floating ? .046 : .042));
        cairo_set_line_width(c, n * (floating ? 4.2 : 2.1));
        cairo_stroke(c);
        if (!floating) {
            rounded(c, {r.x - 1.5, r.y - 1.5, r.w, r.h}, radius);
            color(c, hex(0xffffff, .015));
            cairo_set_line_width(c, n * 1.6);
            cairo_stroke(c);
        }
    }
}
inline void bevel(cairo_t *c, Rect r, double radius, bool inset = false) {
    auto *g = cairo_pattern_create_linear(r.x, r.y, r.x + r.w * .2, r.y + r.h);
    stop(g, 0, hex(inset ? 0x000000 : 0xffffff, inset ? .48 : .15));
    stop(g, .5, hex(0x000000, .08));
    stop(g, 1, hex(inset ? 0xffffff : 0x000000, inset ? .12 : .38));
    rounded(c, r, radius);
    cairo_set_source(c, g);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    cairo_pattern_destroy(g);
}
inline void raised(cairo_t *c, Rect r, double radius = 6, bool floating = false,
                   bool hover = false) {
    shadow(c, r, radius, floating);
    gradient(c, r, hover ? hex(0x44494d) : hex(0x3b4044), hover ? hex(0x353a3e) : hex(0x2f3336),
             radius);
    bevel(c, r, radius);
}
inline void well(cairo_t *c, Rect r, double radius = 4, bool focused = false, Color tint = accent) {
    gradient(c, r, hex(0x1f2225), hex(0x2b2f32), radius);
    bevel(c, r, radius, true);
    if (focused) {
        rounded(c, r, radius);
        color(c, tint.alpha(.8));
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
    }
}
inline void button(cairo_t *c, Rect r, bool active, bool hover, Color tint = accent) {
    r = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    if (active) {
        well(c, r, 5);
        line(c, r.x + r.w * .35, r.y + r.h - 3, r.x + r.w * .65, r.y + r.h - 3, tint.alpha(.8),
             1.5);
    } else {
        raised(c, r, 5, false, hover);
    }
}
inline void knob(cairo_t *c, Rect r, double normalized, double normalDefault, Color tint,
                 bool focused, bool enabled, bool large = false) {
    const double x = r.x + r.w / 2, y = r.y + 40, radius = large ? 33 : 29;
    const double pi = std::numbers::pi, start = .75 * pi, sweep = 1.5 * pi;
    const double a = start + std::clamp(normalized, 0., 1.) * sweep;
    const double d = start + std::clamp(normalDefault, 0., 1.) * sweep;
    const Color marking = enabled ? tint : dim;
    // A recessed annulus sits under the raised cap, with a fine engraved scale.
    Rect rim{x - radius - 6, y - radius - 6, 2 * radius + 12, 2 * radius + 12};
    well(c, rim, radius + 6);
    for (unsigned n = 0; n <= 12; ++n) {
        const double t = start + n * sweep / 12;
        const double inner = radius + 9, outer = inner + (n % 3 == 0 ? 3 : 1.5);
        line(c, x + std::cos(t) * inner, y + std::sin(t) * inner, x + std::cos(t) * outer,
             y + std::sin(t) * outer, dim.alpha(enabled ? .55 : .22), 1);
    }
    color(c, hex(0x000000, .24));
    cairo_set_line_width(c, 2.5);
    cairo_arc(c, x, y, radius + 4, start, start + sweep);
    cairo_stroke(c);
    color(c, marking.alpha(enabled ? .95 : .4));
    cairo_set_line_width(c, focused ? 2.8 : 2);
    cairo_arc(c, x, y, radius + 4, std::min(a, d), std::max(a, d));
    cairo_stroke(c);
    Rect cap{x - radius, y - radius, 2 * radius, 2 * radius};
    shadow(c, cap, radius);
    gradient(c, cap, hex(0x60666a), hex(0x2b2f32), radius);
    bevel(c, cap, radius);
    // Slightly convex face. The narrow outer lip catches the same light as the panel.
    Rect face{x - radius + 2, y - radius + 2, 2 * radius - 4, 2 * radius - 4};
    gradient(c, face, enabled ? hex(0x4c5256) : hex(0x383c40), hex(0x363b3f), radius - 2);
    auto *light = cairo_pattern_create_radial(x - radius * .36, y - radius * .46, 0, x, y, radius);
    stop(light, 0, hex(0xffffff, .07));
    stop(light, 1, hex(0xffffff, 0));
    cairo_set_source(c, light);
    cairo_arc(c, x, y, radius - 2, 0, 2 * pi);
    cairo_fill(c);
    cairo_pattern_destroy(light);
    line(c, x + std::cos(a) * (radius - 12) + .6, y + std::sin(a) * (radius - 12) + 1,
         x + std::cos(a) * (radius - 4) + .6, y + std::sin(a) * (radius - 4) + 1, hex(0x000000, .5),
         3);
    line(c, x + std::cos(a) * (radius - 12), y + std::sin(a) * (radius - 12),
         x + std::cos(a) * (radius - 4), y + std::sin(a) * (radius - 4), enabled ? ink : dim, 2);
    circle(c, x + std::cos(d) * (radius + 4), y + std::sin(d) * (radius + 4), 1.2, muted);
    if (focused) {
        color(c, tint.alpha(.8));
        cairo_set_line_width(c, 1);
        cairo_arc(c, x, y, radius + 13, start, start + sweep);
        cairo_stroke(c);
    }
}
} // namespace openfilter::ui::theme
