#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include <cairo/cairo.h>

#include "configuration/shell_config.hpp"

namespace eh::shell::dock::control_center::paint_utils {

constexpr double kSurfR = 0.93, kSurfG = 0.93, kSurfB = 0.93;
constexpr double kGlassFillR = 0.11, kGlassFillG = 0.11, kGlassFillB = 0.13;
constexpr double kGlassFillA = 1.00;
constexpr double kGlassStrokeA = 0.08;

inline void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI_2);
  cairo_close_path(cr);
}

inline double cc_text_width(cairo_t* cr, const std::string& s) {
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, s.c_str(), &ex);
  return ex.x_advance;
}

inline void cc_paint_glass_card_mc(cairo_t* cr, double x, double y, double w, double h, double r,
                                    double inner_glass_scale, const eh::config::ChromePaintColors& mc) {
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  rrect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.55 * s);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.13 * s);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

inline void cc_draw_status_pill(cairo_t* cr, double x, double y, const std::string& label, bool active,
                                 double inner_glass_scale, const eh::config::ChromePaintColors& mc) {
  if (label.empty()) return;
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  cairo_save(cr);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11.0);
  const double tw = cc_text_width(cr, label);
  const double padX = 10.0;
  const double w = std::max(44.0, tw + padX * 2.0);
  const double h = 20.0;
  const double r = h * 0.5;
  rrect(cr, x, y, w, h, r);
  if (active)
    cairo_set_source_rgba(cr, 0.298, 0.686, 0.314, 0.15 * s);
  else
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.10 * s);
  cairo_fill_preserve(cr);
  if (active)
    cairo_set_source_rgba(cr, 0.298, 0.686, 0.314, 0.30 * s);
  else
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.18 * s);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  if (active)
    cairo_set_source_rgba(cr, 0.30, 0.85, 0.38, 0.96);
  else
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.70);
  cairo_move_to(cr, x + (w - tw) * 0.5, y + 14.0);
  cairo_show_text(cr, label.c_str());
  cairo_restore(cr);
}

inline void truncate_to_width(cairo_t* cr, const std::string& text, double maxAdvance, std::string* out) {
  *out = text;
  for (;;) {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, out->c_str(), &ex);
    if (ex.x_advance <= maxAdvance || out->size() < 4) break;
    out->pop_back();
    while (!out->empty() && (static_cast<unsigned char>(out->back()) & 0xC0) == 0x80)
      out->pop_back();
  }
}

inline bool parse_bool(const std::string& v, bool def) {
  if (v.empty()) return def;
  if (v == "1" || v == "true" || v == "True" || v == "yes" || v == "Yes") return true;
  if (v == "0" || v == "false" || v == "False" || v == "no" || v == "No") return false;
  return def;
}

inline double parse_double(const std::string& v, double def) {
  if (v.empty()) return def;
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  if (!end || end == v.c_str()) return def;
  return d;
}

inline std::string lower_copy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace eh::shell::dock::control_center::paint_utils
