#pragma once

#include "m3/core/primitives/box.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>

namespace eh::widgets::slot_pill_style {

inline constexpr double kPillHeightMul = 1.0;
inline double g_opacityScale = 1.0;
inline double g_pillR = 1.0;
inline double g_pillG = 1.0;
inline double g_pillB = 1.0;
inline double g_hoverAccentR = 1.0;
inline double g_hoverAccentG = 1.0;
inline double g_hoverAccentB = 1.0;
inline double g_mediaBtnR = 0.22;
inline double g_mediaBtnG = 0.24;
inline double g_mediaBtnB = 0.26;
inline double g_mediaGlyphR = 1.0;
inline double g_mediaGlyphG = 1.0;
inline double g_mediaGlyphB = 1.0;
inline double g_mediaOnAccentR = 0.10;
inline double g_mediaOnAccentG = 0.10;
inline double g_mediaOnAccentB = 0.10;

inline double corner_radius(double pill_h, double slot_w) {
  return std::max(2.0, std::min(pill_h, slot_w) * 0.22);
}

inline void set_fill_for_state(cairo_t* cr, bool hovered, bool pressed) {
  (void)hovered;
  (void)pressed;
  cairo_set_source_rgba(cr, g_pillR, g_pillG, g_pillB, g_opacityScale);
}

inline void stroke_pill_after_fill_preserve(cairo_t* cr, bool hovered, bool pressed) {
  (void)hovered;
  (void)pressed;
  cairo_set_source_rgba(cr, g_pillR, g_pillG, g_pillB, g_opacityScale);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

// Flat frosted treatment (no glossy wash): milky lift + bottom-biased edge light.
// Call this after you have filled the base shape.
// opacity controls the strength of the highlights (typically the fill alpha).
inline void paint_glass_layers(cairo_t* cr, double x, double y, double w, double h, double opacity = 1.0, double outer_radius = -1.0) {
  double rad = (outer_radius > 0 ? outer_radius : corner_radius(h, w));
  const double op = g_opacityScale * opacity;

  // Milky lift — faint white wash so the fill reads frosted, not dark.
  {
    cairo_save(cr);
    eh::shell::shared::path_rounded_rect(cr, x, y, w, h, rad);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07 * op);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  // Edge light — inner 1px stroke, soft on top, bright along the bottom.
  {
    cairo_pattern_t* rim = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(rim, 0.0, 1.0, 1.0, 1.0, 0.12 * op);
    cairo_pattern_add_color_stop_rgba(rim, 0.5, 1.0, 1.0, 1.0, 0.05 * op);
    cairo_pattern_add_color_stop_rgba(rim, 1.0, 1.0, 1.0, 1.0, 0.42 * op);
    cairo_set_source(cr, rim);
    cairo_set_line_width(cr, 1.0);
    eh::shell::shared::path_rounded_rect(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0,
                                         std::max(0.0, rad - 1.0));
    cairo_stroke(cr);
    cairo_pattern_destroy(rim);
  }

  // Faint outer glow so the edge lifts off the wallpaper.
  {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08 * op);
    cairo_set_line_width(cr, 1.0);
    eh::shell::shared::path_rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, rad);
    cairo_stroke(cr);
  }
}

// M3-based pill background (fill only, no stroke) + semi-glassy inner design
// (replicates the inner glassy rim + top/bottom highlight bands from the file browser location bar).
//
// This is the single place for dock widget "pills". All widgets in src/desktop_shell/widgets/
// that render as dock slots (battery, bluetooth, clock, media, weather, workspaces, etc.)
// go through this, so they all get the glass effect automatically.
inline void paint_pill(cairo_t* cr, double x, double y, double w, double h) {
  m3::Box box;
  box.setColor(static_cast<float>(g_pillR), static_cast<float>(g_pillG),
               static_cast<float>(g_pillB), static_cast<float>(g_opacityScale));
  box.setRadius(static_cast<float>(corner_radius(h, w)));
  box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h));
  box.paint(cr);

  paint_glass_layers(cr, x, y, w, h, 1.0, corner_radius(h, w));  // pass native rad for exact conformance (no pill-default override)
}

// Create a rounded-rect path matching the pill shape (for clipping).
inline void pill_clip_path(cairo_t* cr, double x, double y, double w, double h) {
  const double rad = corner_radius(h, w);
  cairo_new_path(cr);
  cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
  cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

}
