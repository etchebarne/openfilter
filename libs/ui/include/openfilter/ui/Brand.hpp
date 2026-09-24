#pragma once
#include "BrandPaths.hpp"

namespace openfilter::ui {
enum class Brand { Eq, Compressor, Limiter };

// Outlined artwork is embedded in the binary and painted only by the UI thread.
// Fit without distortion, left aligned and vertically centered in the header.
inline void drawBrand(cairo_t *c, Brand brand, Rect destination) {
    if (destination.w <= 0 || destination.h <= 0)
        return;
    const auto bounds = brand == Brand::Eq           ? brand_detail::eqBounds
                        : brand == Brand::Compressor ? brand_detail::compressorBounds
                                                     : brand_detail::limiterBounds;
    const double scale = std::min(destination.w / bounds.w, destination.h / bounds.h);
    cairo_save(c);
    cairo_translate(c, destination.x, destination.y + (destination.h - bounds.h * scale) / 2);
    cairo_scale(c, scale, scale);
    cairo_translate(c, -bounds.x, -bounds.y);
    if (brand == Brand::Eq)
        brand_detail::eq(c);
    else if (brand == Brand::Compressor)
        brand_detail::compressor(c);
    else
        brand_detail::limiter(c);
    cairo_restore(c);
}
} // namespace openfilter::ui
