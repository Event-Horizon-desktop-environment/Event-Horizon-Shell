#include "desktop_shell/ui/toggle/ui_toggle.hpp"
#include <algorithm>
#include <cmath>

namespace eh::ui {

void Toggle::setOn(bool on, uint32_t now_ms) {
   
  if (on == on_) return;
  on_ = on;
  if (now_ms == 0) {
    animT_ = on_ ? 1.f : 0.f;
    animating_ = false;
    return;
  }
  animStartMs_ = now_ms;
  animating_ = true;
}

void Toggle::paint(cairo_t* cr, uint32_t now_ms) {
   
  if (animating_) {
    const float elapsed = static_cast<float>(now_ms - animStartMs_);
    const float rawT = std::min(elapsed / kAnimMs, 1.f);
    animT_ = easeOutCubic(rawT);
    if (rawT >= 1.f) animating_ = false;
  }

  const float t = animT_;

  const float pad = 2;
  const float thumbD = h_ - pad * 2;
  const float radius = h_ * radiusFactor_;

  auto roundRect = [](cairo_t* c, float rx, float ry, float rw, float rh, float r) {
    if (r <= 0.f) {
      cairo_rectangle(c, static_cast<double>(rx), static_cast<double>(ry),
                      static_cast<double>(rw), static_cast<double>(rh));
      return;
    }
    cairo_move_to(c, rx + r, ry);
    cairo_line_to(c, rx + rw - r, ry);
    cairo_arc(c, rx + rw - r, ry + r, r, -static_cast<double>(M_PI_2), 0);
    cairo_line_to(c, rx + rw, ry + rh - r);
    cairo_arc(c, rx + rw - r, ry + rh - r, r, 0, static_cast<double>(M_PI_2));
    cairo_line_to(c, rx + r, ry + rh);
    cairo_arc(c, rx + r, ry + rh - r, r, static_cast<double>(M_PI_2), static_cast<double>(M_PI));
    cairo_line_to(c, rx, ry + r);
    cairo_arc(c, rx + r, ry + r, r, static_cast<double>(M_PI), -static_cast<double>(M_PI_2));
    cairo_close_path(c);
  };

  cairo_save(cr);

  // Interpolated track color: surface (off) → accent (on)
  float tr, tg, tb;
  lerpColor(t, surfaceR_, surfaceG_, surfaceB_, accentR_, accentG_, accentB_, tr, tg, tb);
  float trackAlpha = 0.8f + 0.2f * t;
  cairo_set_source_rgba(cr, static_cast<double>(tr), static_cast<double>(tg),
                        static_cast<double>(tb), static_cast<double>(trackAlpha));
  roundRect(cr, x_, y_, w_, h_, radius);
  cairo_fill(cr);

  // Hover glow overlay
  if (hovered_ && !on_) {
    cairo_set_source_rgba(cr, static_cast<double>(accentR_), static_cast<double>(accentG_),
                          static_cast<double>(accentB_), 0.15);
    roundRect(cr, x_, y_, w_, h_, radius);
    cairo_fill(cr);
  }

  // Thumb position: interpolated
  const float thumbMin = x_ + pad;
  const float thumbMax = x_ + w_ - thumbD - pad;
  const float thumbX = thumbMin + (thumbMax - thumbMin) * t;
  const float thumbY = y_ + pad;

  // Thumb shadow
  cairo_set_source_rgba(cr, 0, 0, 0, 0.15);
  cairo_arc(cr, static_cast<double>(thumbX + thumbD / 2 + 1),
            static_cast<double>(thumbY + thumbD / 2 + 1),
            static_cast<double>(thumbD / 2), 0, 2 * M_PI);
  cairo_fill(cr);

  // Thumb
  cairo_set_source_rgba(cr, static_cast<double>(textR_), static_cast<double>(textG_),
                        static_cast<double>(textB_), 1.0);
  cairo_arc(cr, static_cast<double>(thumbX + thumbD / 2),
            static_cast<double>(thumbY + thumbD / 2),
            static_cast<double>(thumbD / 2), 0, 2 * M_PI);
  cairo_fill(cr);

  cairo_restore(cr);
}

}
