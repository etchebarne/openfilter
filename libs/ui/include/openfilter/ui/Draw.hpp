#pragma once
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <string>
#include <string_view>

namespace openfilter::ui {
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};
struct Color {
    double r, g, b, a = 1;
    Color alpha(double opacity) const { return {r, g, b, opacity}; }
};
inline Color hex(unsigned v, double a = 1) {
    return {((v >> 16) & 255) / 255., ((v >> 8) & 255) / 255., (v & 255) / 255., a};
}
inline void color(cairo_t *c, Color v) {
    cairo_set_source_rgba(c, v.r, v.g, v.b, v.a);
}
inline void box(cairo_t *c, Rect r, Color v) {
    color(c, v);
    cairo_rectangle(c, r.x, r.y, r.w, r.h);
    cairo_fill(c);
}
inline void line(cairo_t *c, double x1, double y1, double x2, double y2, Color v,
                 double width = 1) {
    color(c, v);
    cairo_set_line_width(c, width);
    cairo_move_to(c, x1, y1);
    cairo_line_to(c, x2, y2);
    cairo_stroke(c);
}
inline void rounded(cairo_t *c, Rect r, double radius) {
    const double pi = std::acos(-1.);
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    cairo_new_sub_path(c);
    cairo_arc(c, r.x + r.w - radius, r.y + radius, radius, -pi / 2, 0);
    cairo_arc(c, r.x + r.w - radius, r.y + r.h - radius, radius, 0, pi / 2);
    cairo_arc(c, r.x + radius, r.y + r.h - radius, radius, pi / 2, pi);
    cairo_arc(c, r.x + radius, r.y + radius, radius, pi, pi * 1.5);
    cairo_close_path(c);
}
inline void pill(cairo_t *c, Rect r, Color fill, Color border, double radius = 6) {
    rounded(c, r, radius);
    color(c, fill);
    cairo_fill_preserve(c);
    color(c, border);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
}
inline void text(cairo_t *c, std::string_view value, double x, double y, double size, Color ink,
                 bool bold = false, int align = 0) {
    cairo_select_font_face(c, "Noto Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(c, size);
    std::string s(value);
    cairo_text_extents_t ext{};
    cairo_text_extents(c, s.c_str(), &ext);
    if (align == 1)
        x -= ext.x_advance / 2;
    else if (align == 2)
        x -= ext.x_advance;
    color(c, ink);
    cairo_move_to(c, x, y);
    cairo_show_text(c, s.c_str());
}
// Center visible glyph bounds, not an assumed baseline offset.
inline void centeredText(cairo_t *c, std::string_view value, Rect r, double size, Color ink,
                         bool bold = false) {
    cairo_select_font_face(c, "Noto Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(c, size);
    const std::string label(value);
    cairo_text_extents_t ext{};
    cairo_text_extents(c, label.c_str(), &ext);
    color(c, ink);
    cairo_move_to(c, r.x + (r.w - ext.width) / 2 - ext.x_bearing,
                  r.y + (r.h - ext.height) / 2 - ext.y_bearing);
    cairo_show_text(c, label.c_str());
}
inline void circle(cairo_t *c, double x, double y, double r, Color fill) {
    color(c, fill);
    cairo_arc(c, x, y, r, 0, 2 * std::acos(-1.));
    cairo_fill(c);
}
} // namespace openfilter::ui
