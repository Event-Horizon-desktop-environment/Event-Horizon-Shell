#pragma once

#include <cmath>
#include <cstdint>

#include <cairo/cairo.h>

namespace m3 {

// Colored rounded rectangle.
class Box {
public:
  Box() = default;

  void setColor(float r, float g, float b, float a = 1.0f) { cr_ = r; cg_ = g; cb_ = b; ca_ = a; }
  void setRadius(float rad) { radius_ = rad; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setGlassy(bool g) { glassy_ = g; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  void paint(cairo_t* cr) const {
    if (w_ <= 0 || h_ <= 0 || ca_ <= 0) return;

    cairo_save(cr);
    cairo_translate(cr, x_, y_);

    const float r = std::min(radius_, std::min(w_, h_) * 0.5f);
    if (r > 0) {
      cairo_new_path(cr);
      cairo_arc(cr, r, r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, w_ - r, r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, w_ - r, h_ - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, r, h_ - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
    } else {
      cairo_rectangle(cr, 0, 0, w_, h_);
    }

    cairo_set_source_rgba(cr, cr_, cg_, cb_, ca_);
    cairo_fill(cr);

    if (glassy_) {
      const double op = ca_;
      const double refH = 36.0;
      const double s = std::max(0.6, std::min(1.6, static_cast<double>(h_) / refH));

      auto build_path = [&](double ix, double iy, double iw, double ih, double ir) {
        const float r = std::min(ir, std::min(iw, ih) * 0.5f);
        if (r > 0) {
          cairo_new_path(cr);
          cairo_arc(cr, ix + r, iy + r, r, M_PI, 1.5 * M_PI);
          cairo_arc(cr, ix + iw - r, iy + r, r, 1.5 * M_PI, 2.0 * M_PI);
          cairo_arc(cr, ix + iw - r, iy + ih - r, r, 0.0, 0.5 * M_PI);
          cairo_arc(cr, ix + r, iy + ih - r, r, 0.5 * M_PI, M_PI);
          cairo_close_path(cr);
        } else {
          cairo_rectangle(cr, ix, iy, iw, ih);
        }
      };

      // subtle outer separation (very close to edge)
      {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.12 * op);
        cairo_set_line_width(cr, 1.0);
        build_path(0.5, 0.5, w_ - 1.0, h_ - 1.0, radius_);
        cairo_stroke(cr);
      }

      // inner semi-glassy bright rim (strong top highlight + softer bottom close)
      {
        cairo_pattern_t* rim = cairo_pattern_create_linear(0, 0, 0, h_);
        cairo_pattern_add_color_stop_rgba(rim, 0.00, 1.0, 1.0, 1.0, 0.28 * op);
        cairo_pattern_add_color_stop_rgba(rim, 0.18, 1.0, 1.0, 1.0, 0.14 * op);
        cairo_pattern_add_color_stop_rgba(rim, 0.42, 1.0, 1.0, 1.0, 0.03 * op);
        cairo_pattern_add_color_stop_rgba(rim, 1.00, 1.0, 1.0, 1.0, 0.09 * op);
        cairo_set_source(cr, rim);
        cairo_set_line_width(cr, 1.35);
        double ii = 2.8 * s;
        build_path(ii, ii, w_ - 2 * ii, h_ - 2 * ii, radius_ - ii + 0.5f);
        cairo_stroke(cr);
        cairo_pattern_destroy(rim);
      }

      // extra top-inner highlight band
      {
        cairo_pattern_t* th = cairo_pattern_create_linear(0, 2, 0, h_ * 0.4);
        cairo_pattern_add_color_stop_rgba(th, 0.0, 1.0, 1.0, 1.0, 0.09 * op);
        cairo_pattern_add_color_stop_rgba(th, 1.0, 1.0, 1.0, 1.0, 0.0);
        cairo_set_source(cr, th);
        cairo_set_line_width(cr, 0.7);
        double hi = 3.8 * s;
        build_path(hi, hi, w_ - 2 * hi, h_ - 2 * hi, radius_ - hi + 1.0f);
        cairo_stroke(cr);
        cairo_pattern_destroy(th);
      }

      // bottom-inner highlight band (softer)
      {
        cairo_pattern_t* bh = cairo_pattern_create_linear(0, h_ * 0.55, 0, h_ - 2);
        cairo_pattern_add_color_stop_rgba(bh, 0.0, 1.0, 1.0, 1.0, 0.0);
        cairo_pattern_add_color_stop_rgba(bh, 1.0, 1.0, 1.0, 1.0, 0.07 * op);
        cairo_set_source(cr, bh);
        cairo_set_line_width(cr, 0.7);
        double hi = 3.8 * s;
        build_path(hi, hi, w_ - 2 * hi, h_ - 2 * hi, radius_ - hi + 1.0f);
        cairo_stroke(cr);
        cairo_pattern_destroy(bh);
      }
    }

    cairo_restore(cr);
  }

private:
  float cr_ = 0.0f, cg_ = 0.0f, cb_ = 0.0f, ca_ = 0.0f;
  float radius_ = 0.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  bool glassy_ = false;
};

} // namespace m3
