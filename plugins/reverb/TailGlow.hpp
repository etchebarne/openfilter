#pragma once
#include "DecayHistory.hpp"
#include <memory>
#include <openfilter/ui/Draw.hpp>
#include <vector>

namespace openfilter::reverb {
// Reusable UI-only alpha mask. Blur the recorded tail, never controls or EQ.
class TailGlow {
  public:
    void paint(cairo_t *target, ui::Rect graph, const DecayHistory &history) {
        if (graph.w <= 0 || graph.h <= 0)
            return;
        constexpr double resolution = 2.7;
        constexpr int padding = 8;
        const int width = int(std::ceil(graph.w / resolution)) + padding * 2;
        const int height = int(std::ceil(graph.h / resolution)) + padding * 2;
        if (!surface_ || width != width_ || height != height_) {
            surface_.reset(cairo_image_surface_create(CAIRO_FORMAT_A8, width, height));
            width_ = width;
            height_ = height;
            scratch_.resize(size_t(cairo_image_surface_get_stride(surface_.get())) * height);
        }
        if (cairo_surface_status(surface_.get()) != CAIRO_STATUS_SUCCESS)
            return;
        auto *cr = cairo_create(surface_.get());
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_translate(cr, padding, padding);
        cairo_scale(cr, 1 / resolution, 1 / resolution);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        auto contour = [&](const DecayHistory::Curve &curve) {
            cairo_new_path(cr);
            for (unsigned n = 0; n < DecayHistory::points; ++n) {
                const double x = graph.w * n / (DecayHistory::points - 1);
                // Let quiet values fall below the canvas instead of accumulating
                // a false glowing baseline at the display floor.
                const double y = graph.h * (1 - (curve[n] + 96) / 84.);
                if (n == 0)
                    cairo_move_to(cr, x, y);
                else
                    cairo_line_to(cr, x, y);
            }
        };
        const unsigned stride = std::max(1u, (history.count() + 11) / 12);
        for (unsigned n = 0; n < history.count(); n += stride) {
            const auto &slice = history.slice(n);
            const double age = history.age(slice);
            if (age >= DecayHistory::duration ||
                *std::max_element(slice.wet.begin(), slice.wet.end()) < -96)
                continue;
            const double opacity = std::pow(1 - age / DecayHistory::duration, 1.35);
            contour(slice.wet);
            cairo_set_source_rgba(cr, 1, 1, 1, .42 * opacity);
            cairo_set_line_width(cr, 7);
            cairo_stroke_preserve(cr);
            cairo_line_to(cr, graph.w, graph.h + padding * resolution);
            cairo_line_to(cr, 0, graph.h + padding * resolution);
            cairo_close_path(cr);
            auto *fill = cairo_pattern_create_linear(0, 0, 0, graph.h);
            cairo_pattern_add_color_stop_rgba(fill, 0, 1, 1, 1, .065 * opacity);
            cairo_pattern_add_color_stop_rgba(fill, 1, 1, 1, 1, 0);
            cairo_set_source(cr, fill);
            cairo_fill(cr);
            cairo_pattern_destroy(fill);
        }
        if (*std::max_element(history.wet.begin(), history.wet.end()) >= -96) {
            contour(history.wet);
            cairo_set_source_rgba(cr, 1, 1, 1, .65);
            cairo_set_line_width(cr, 6);
            cairo_stroke(cr);
        }
        cairo_destroy(cr);
        cairo_surface_flush(surface_.get());
        auto *data = cairo_image_surface_get_data(surface_.get());
        const int row = cairo_image_surface_get_stride(surface_.get());
        // Three separable binomial passes approximate a Gaussian. Fixed logical
        // resolution keeps the softness and cost stable at every host UI scale.
        constexpr unsigned weights[]{1, 4, 6, 4, 1};
        for (unsigned pass = 0; pass < 3; ++pass) {
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    unsigned sum = 0;
                    for (int k = -2; k <= 2; ++k)
                        if (x + k >= 0 && x + k < width)
                            sum += data[y * row + x + k] * weights[k + 2];
                    scratch_[y * row + x] = (sum + 8) / 16;
                }
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    unsigned sum = 0;
                    for (int k = -2; k <= 2; ++k)
                        if (y + k >= 0 && y + k < height)
                            sum += scratch_[(y + k) * row + x] * weights[k + 2];
                    data[y * row + x] = (sum + 8) / 16;
                }
        }
        cairo_surface_mark_dirty(surface_.get());
        cairo_save(target);
        cairo_translate(target, graph.x - padding * resolution, graph.y - padding * resolution);
        cairo_scale(target, resolution, resolution);
        ui::color(target, ui::hex(0xd8b477, .9));
        auto *mask = cairo_pattern_create_for_surface(surface_.get());
        cairo_pattern_set_filter(mask, CAIRO_FILTER_BILINEAR);
        cairo_mask(target, mask);
        cairo_pattern_destroy(mask);
        cairo_restore(target);
    }

  private:
    std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface_{
        nullptr, cairo_surface_destroy};
    std::vector<unsigned char> scratch_;
    int width_ = 0, height_ = 0;
};
} // namespace openfilter::reverb
