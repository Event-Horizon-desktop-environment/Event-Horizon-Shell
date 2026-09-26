#pragma once
// Shared cairo primitives for Horizon Disks.
// All widgets are immediate-mode; geometry comes from layout.hpp so that
// draw and hit-testing share a single source of truth.

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eh::disks {
struct AppState;
}

namespace eh::disks::widgets {

struct RGB {
  double r = 0, g = 0, b = 0;
};
struct RGBA {
  double r = 0, g = 0, b = 0, a = 1;
};

void fill_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                  RGBA c);
void stroke_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                    RGB c, double lw = 1.0);
// 1px hairline (crisp on integer layouts).
void hairline_h(cairo_t* cr, double x0, double x1, double y, RGB c);
void hairline_v(cairo_t* cr, double x, double y0, double y1, RGB c);

// Text helpers. All use sans-serif; weight: 0=normal, 1=bold.
void text(cairo_t* cr, std::string_view s, double x, double baseline_y, int px,
          RGB c, int weight = 0);
double text_w(cairo_t* cr, std::string_view s, int px, int weight = 0);
// Ellipsize to max_w, returns drawn string.
std::string ellipsize(cairo_t* cr, std::string_view s, int px, int weight,
                      double max_w);
void text_ellipsis(cairo_t* cr, std::string_view s, double x, double baseline_y,
                   int px, RGB c, int weight, double max_w);

// Right-aligned text.
void text_right(cairo_t* cr, std::string_view s, double right_x, double baseline_y,
                int px, RGB c, int weight = 0);

// Buttons.
bool pill_button(cairo_t* cr, const AppState& app, double x, double y, double w,
                 double h, std::string_view label, bool hover, bool pressed,
                 bool primary, bool destructive, bool disabled);
bool icon_button(cairo_t* cr, const AppState& app, double x, double y, double s,
                 std::string_view glyph, bool hover, bool active,
                 bool disabled, bool danger = false);
// Small ghost action button with label (e.g. Mount / Unmount).
bool action_button(cairo_t* cr, const AppState& app, double x, double y, double w,
                   double h, std::string_view label, bool hover, bool tone_green,
                   bool tone_red);

// Toggles / checks / radios.
void checkbox(cairo_t* cr, const AppState& app, double x, double y, double s,
              bool on, bool hover);
void radio_dot(cairo_t* cr, const AppState& app, double cx, double cy, double r,
               bool on);
void toggle(cairo_t* cr, const AppState& app, double x, double y, double w,
            double h, bool on, bool hover);

// Pills / badges.
void fs_pill(cairo_t* cr, const AppState& app, double x, double y,
             std::string_view fs, double max_w, double* out_w = nullptr);
void badge(cairo_t* cr, const AppState& app, double x, double y,
           std::string_view label, RGB bg, RGB fg, double* out_w = nullptr);

// Usage bar (mounted filesystems) + space-allocation fill.
void usage_bar(cairo_t* cr, double x, double y, double w, double h, double frac);
// Diagonal hatch for free space (Disks 51 free-space pattern).
void hatch_rect(cairo_t* cr, double x, double y, double w, double h, RGBA base,
                RGB line);
// Indeterminate / determinate spinner (job tracking, Disks 51).
void spinner(cairo_t* cr, double cx, double cy, double r, RGB c, double phase);
void progress_bar(cairo_t* cr, double x, double y, double w, double h,
                  double frac, RGBA track, RGB fill);

// Card + section helpers.
void card(cairo_t* cr, const AppState& app, double x, double y, double w, double h);
void section_label(cairo_t* cr, const AppState& app, double x, double baseline_y,
                   std::string_view s);

// Filesystem color (GParted palette, muted for dark UI).
RGB fs_color(std::string_view fstype, std::string_view usage);

}  // namespace eh::disks::widgets
