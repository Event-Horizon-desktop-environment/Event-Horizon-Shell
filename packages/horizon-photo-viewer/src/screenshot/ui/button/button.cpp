#include "screenshot/ui/button/button.hpp"
#include "screenshot/app.hpp"

#include "core/screenshot/logging.hpp"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdio>

namespace hpv::sc {

static constexpr double kBorderR = 0.33, kBorderG = 0.33, kBorderB = 0.35;
static constexpr double kBorderActiveR = 0.40, kBorderActiveG = 0.91, kBorderActiveB = 0.98;

static constexpr double kN0R = 0.23, kN0G = 0.23, kN0B = 0.25;
static constexpr double kN1R = 0.16, kN1G = 0.16, kN1B = 0.18;
static constexpr double kN2R = 0.14, kN2G = 0.14, kN2B = 0.16;

static constexpr double kH0R = 0.29, kH0G = 0.29, kH0B = 0.31;
static constexpr double kH1R = 0.21, kH1G = 0.21, kH1B = 0.23;
static constexpr double kH2R = 0.18, kH2G = 0.18, kH2B = 0.20;

static constexpr double kP0R = 0.12, kP0G = 0.12, kP0B = 0.14;
static constexpr double kP1R = 0.16, kP1G = 0.16, kP1B = 0.18;
static constexpr double kP2R = 0.16, kP2G = 0.16, kP2B = 0.18;

static void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r)
{
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  cairo_move_to(cr, x + r, y);
  cairo_line_to(cr, x + w - r, y);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_line_to(cr, x + w, y + h - r);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_line_to(cr, x + r, y + h);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_line_to(cr, x, y + r);
  cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
  cairo_close_path(cr);
}

static void draw_drop_shadow(cairo_t* cr,
                              double x, double y, double w, double h, double r,
                              double offset_y, double blur_radius,
                              double sr, double sg, double sb, double sa)
{
  int steps = std::max(1, static_cast<int>(blur_radius / 2));
  for (int i = steps; i >= 0; --i) {
    double t = static_cast<double>(i) / steps;
    double inset = blur_radius * (1.0 - t);
    cairo_set_source_rgba(cr, sr, sg, sb, sa * t * t);
    rounded_rect(cr,
                 x - inset + offset_y * 0.3,
                 y + offset_y - inset,
                 w + inset * 2,
                 h + inset * 2,
                 r + inset * 0.5);
    cairo_fill(cr);
  }
}

static void paint_vista_body(cairo_t* cr,
                              double x, double y, double w, double h, double r,
                              double r0, double g0, double b0,
                              double r1, double g1, double b1,
                              double r2, double g2, double b2)
{
  cairo_pattern_t* pat = cairo_pattern_create_linear(x, y, x, y + h);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, r0, g0, b0);
  cairo_pattern_add_color_stop_rgb(pat, 0.45, r1, g1, b1);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, r2, g2, b2);
  cairo_set_source(cr, pat);
  rounded_rect(cr, x, y, w, h, r);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
}

static void paint_vista_body_active(cairo_t* cr,
                                     double x, double y, double w, double h, double r,
                                     const AppState& app)
{
  if (app.haveMatugen) {
    double ar = app.chrome.accentR;
    double ag = app.chrome.accentG;
    double ab = app.chrome.accentB;
    paint_vista_body(cr, x, y, w, h, r,
                     ar * 1.15, ag * 1.15, ab * 1.15,
                     ar * 0.95, ag * 0.95, ab * 0.95,
                     ar * 0.80, ag * 0.80, ab * 0.80);
  } else {
    paint_vista_body(cr, x, y, w, h, r,
                     0.133, 0.827, 0.933,
                     0.024, 0.714, 0.831,
                     0.031, 0.569, 0.698);
  }
}

void draw_vista_button(cairo_t* cr, const AppState& app,
                       double x, double y, double w, double h,
                       const char* label, const char* sublabel,
                       bool active, bool hovered, bool pressed)
{
  SC_LOG("draw_vista_button: enter label=%s active=%d hovered=%d pressed=%d", label ? label : "(null)", active, hovered, pressed);
  double r = 8.0;

  if (active && !pressed) {
    rounded_rect(cr, x - 3, y - 3, w + 6, h + 6, r + 2);
    if (app.haveMatugen) {
      cairo_set_source_rgba(cr, app.chrome.accentR, app.chrome.accentG, app.chrome.accentB, 0.25);
    } else {
      cairo_set_source_rgba(cr, kBorderActiveR, kBorderActiveG, kBorderActiveB, 0.25);
    }
    cairo_set_line_width(cr, 3);
    cairo_stroke(cr);

    draw_drop_shadow(cr, x, y, w, h, r, 6, 6,
                     0, 0, 0, 0.30);

    paint_vista_body_active(cr, x, y, w, h, r, app);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.20);
    rounded_rect(cr, x, y + h - 4, w, 4, r);
    cairo_clip(cr);
    rounded_rect(cr, x, y, w, h, r);
    cairo_fill(cr);
    cairo_reset_clip(cr);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    rounded_rect(cr, x, y, w, h, r);
    cairo_clip(cr);
    cairo_rectangle(cr, x, y, w, 1);
    cairo_fill(cr);
    cairo_reset_clip(cr);

    if (app.haveMatugen) {
      cairo_set_source_rgba(cr, app.chrome.accentR * 1.3, app.chrome.accentG * 1.3,
                            app.chrome.accentB * 1.3, 0.8);
    } else {
      cairo_set_source_rgba(cr, kBorderActiveR, kBorderActiveG, kBorderActiveB, 0.8);
    }
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
    cairo_stroke(cr);
  } else if (pressed) {
    draw_drop_shadow(cr, x, y, w, h, r, 2, 4,
                     0, 0, 0, 0.25);

    paint_vista_body(cr, x, y, w, h, r, kP0R, kP0G, kP0B, kP1R, kP1G, kP1B, kP2R, kP2G, kP2B);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, kBorderR * 0.7, kBorderG * 0.7, kBorderB * 0.7, 0.8);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r - 0.5);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  } else if (hovered) {
    draw_drop_shadow(cr, x, y, w, h, r, 6, 8,
                     0, 0, 0, 0.35);

    paint_vista_body(cr, x, y, w, h, r, kH0R, kH0G, kH0B, kH1R, kH1G, kH1B, kH2R, kH2G, kH2B);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.30);
    rounded_rect(cr, x, y, w, h, r);
    cairo_clip(cr);
    cairo_rectangle(cr, x, y, w, 1);
    cairo_fill(cr);
    cairo_reset_clip(cr);

    cairo_set_source_rgba(cr, kBorderR, kBorderG, kBorderB, 0.8);
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
    cairo_stroke(cr);
  } else {
    draw_drop_shadow(cr, x, y, w, h, r, 4, 6,
                     0, 0, 0, 0.30);

    paint_vista_body(cr, x, y, w, h, r, kN0R, kN0G, kN0B, kN1R, kN1G, kN1B, kN2R, kN2G, kN2B);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
    rounded_rect(cr, x, y, w, h, r);
    cairo_clip(cr);
    cairo_rectangle(cr, x, y, w, 1);
    cairo_fill(cr);
    cairo_reset_clip(cr);

    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
    rounded_rect(cr, x, y + h - 3, w, 3, r);
    cairo_clip(cr);
    rounded_rect(cr, x, y, w, h, r);
    cairo_fill(cr);
    cairo_reset_clip(cr);

    cairo_set_source_rgba(cr, kBorderR, kBorderG, kBorderB, 0.7);
    cairo_set_line_width(cr, 1);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
    cairo_stroke(cr);
  }

  {
    double font_size = 14.0;
    double label_y = y + h / 2 - (sublabel ? 2.0 : 0.0);
    if (active) {
      cairo_set_source_rgba(cr, 0.067, 0.067, 0.067, 1.0);
    } else {
      cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0);
    }
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, font_size);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    cairo_move_to(cr, x + (w - te.width) / 2, label_y + te.height / 2);
    cairo_show_text(cr, label);

    if (sublabel) {
      cairo_set_source_rgba(cr, 0.65, 0.65, 0.68, active ? 0.7 : 0.6);
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10.0);
      cairo_text_extents(cr, sublabel, &te);
      cairo_move_to(cr, x + (w - te.width) / 2, y + h / 2 + 12 + te.height / 2);
      cairo_show_text(cr, sublabel);
    }
  }
}

}
