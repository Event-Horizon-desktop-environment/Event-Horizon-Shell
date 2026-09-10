#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
#include <cstring>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include "helpers/timing.h"

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== Cairo Render Benchmarks ===\n\n");

    bench("rect_fill_1080p", 200, []() {
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 0.15, 0.25, 0.45);
        cairo_rectangle(cr, 0, 0, 1920, 1080);
        cairo_fill(cr);
        cairo_surface_flush(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    });

    bench("100_small_rects", 200, []() {
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(surface);
        for (int i = 0; i < 100; ++i) {
            cairo_set_source_rgb(cr, 0.2, 0.4, 0.8);
            cairo_rectangle(cr, i * 10, i * 5, 80, 30);
            cairo_fill(cr);
        }
        cairo_surface_flush(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    });

    bench("text_render_50_strings", 100, []() {
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(surface);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14.0);
        for (int i = 0; i < 50; ++i) {
            cairo_move_to(cr, 10, 20 + i * 20);
            cairo_show_text(cr, "Event Horizon Shell — Performance Test String");
        }
        cairo_surface_flush(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    });

    bench("rounded_rect_20", 200, []() {
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(surface);
        double radius = 12.0;
        for (int i = 0; i < 20; ++i) {
            double x = 50 + i * 90, y = 50, w = 80, h = 600;
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI / 2, 0);
            cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
            cairo_arc(cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI);
            cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, 0.3, 0.6, 0.9, 0.5);
            cairo_fill(cr);
        }
        cairo_surface_flush(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    });

    bench("image_blit_256", 200, []() {
        cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 256, 256);
        cairo_surface_t* dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(dst);
        cairo_set_source_rgba(cr, 0.3, 0.6, 0.9, 0.8);
        cairo_rectangle(cr, 0, 0, 256, 256);
        cairo_fill(cr);
        cairo_surface_flush(src);
        cairo_set_source_surface(cr, src, 100, 100);
        cairo_paint(cr);
        cairo_surface_flush(dst);
        cairo_destroy(cr);
        cairo_surface_destroy(src);
        cairo_surface_destroy(dst);
    });

    printf("\nDone.\n");
    return 0;
}
