#pragma once
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo.h>
#include <cstdint>
#include <cstdlib>

namespace openfilter::ui {
// Owns only resources on the editor's private Pugl display. Cairo renders into
// CPU memory; XPutImage presents it without Cairo's per-display rendering caches.
class X11Raster {
    Display *display_;
    Window window_;
    GC gc_ = nullptr;
    XWindowAttributes attributes_{};
    XImage *image_ = nullptr;
    cairo_surface_t *surface_ = nullptr;
    bool borrowed_ = false;
    void clear() {
        if (image_) {
            if (borrowed_)
                image_->data = nullptr;
            XDestroyImage(image_);
            image_ = nullptr;
        }
        if (surface_) {
            cairo_surface_destroy(surface_);
            surface_ = nullptr;
        }
    }
    static unsigned long component(unsigned value, unsigned long mask) {
        if (!mask)
            return 0;
        unsigned shift = 0;
        while (!(mask & 1)) {
            ++shift;
            mask >>= 1;
        }
        return ((value * mask + 127) / 255) << shift;
    }

  public:
    X11Raster(Display *display, Window window) : display_(display), window_(window) {
        XGetWindowAttributes(display_, window_, &attributes_);
        gc_ = XCreateGC(display_, window_, 0, nullptr);
    }
    ~X11Raster() {
        clear();
        if (gc_)
            XFreeGC(display_, gc_);
    }
    X11Raster(const X11Raster &) = delete;
    X11Raster &operator=(const X11Raster &) = delete;
    cairo_surface_t *surface(unsigned width, unsigned height) {
        if (surface_ && image_->width == int(width) && image_->height == int(height))
            return surface_;
        clear();
        surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(surface_) != CAIRO_STATUS_SUCCESS) {
            clear();
            return nullptr;
        }
        image_ = XCreateImage(display_, attributes_.visual, attributes_.depth, ZPixmap, 0, nullptr,
                              width, height, 32, 0);
        if (!image_) {
            clear();
            return nullptr;
        }
        borrowed_ = image_->bits_per_pixel == 32 && image_->red_mask == 0xff0000 &&
                    image_->green_mask == 0xff00 && image_->blue_mask == 0xff &&
                    image_->bytes_per_line == cairo_image_surface_get_stride(surface_);
        image_->data = borrowed_ ? reinterpret_cast<char *>(cairo_image_surface_get_data(surface_))
                                 : static_cast<char *>(std::calloc(image_->bytes_per_line, height));
        if (!image_->data) {
            clear();
            return nullptr;
        }
        return surface_;
    }
    void present() {
        cairo_surface_flush(surface_);
        if (!borrowed_) {
            const auto *data = cairo_image_surface_get_data(surface_);
            const int stride = cairo_image_surface_get_stride(surface_);
            for (int y = 0; y < image_->height; ++y) {
                const auto *pixels = reinterpret_cast<const uint32_t *>(data + y * stride);
                for (int x = 0; x < image_->width; ++x) {
                    const auto v = pixels[x];
                    XPutPixel(image_, x, y,
                              component((v >> 16) & 255, image_->red_mask) |
                                  component((v >> 8) & 255, image_->green_mask) |
                                  component(v & 255, image_->blue_mask));
                }
            }
        }
        XPutImage(display_, window_, gc_, image_, 0, 0, 0, 0, image_->width, image_->height);
        XFlush(display_);
    }
};
} // namespace openfilter::ui
