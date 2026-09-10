#pragma once

#include <cairo.h>
#include <cstdint>

namespace eh::ui {

class Toggle {
public:
  Toggle() = default;

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setOn(bool on, uint32_t now_ms = 0);
  void setRadiusFactor(float f) { radiusFactor_ = f; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHovered(bool h) { hovered_ = h; }

  void paint(cairo_t* cr, uint32_t now_ms = 0);

  [[nodiscard]] bool animating() const noexcept { return animating_; }

private:
  static constexpr float kAnimMs = 50.f;

  static float easeOutCubic(float t) {
    const float f = t - 1.f;
    return f * f * f + 1.f;
  }

  static void lerpColor(float t, float aR, float aG, float aB, float bR, float bG, float bB,
                        float& outR, float& outG, float& outB) {
    outR = aR + (bR - aR) * t;
    outG = aG + (bG - aG) * t;
    outB = aB + (bB - aB) * t;
  }

  float x_ = 0, y_ = 0, w_ = 52, h_ = 26;
  bool on_ = false;
  bool hovered_ = false;
  float radiusFactor_ = 0.5f;
  float accentR_ = 0.43f, accentG_ = 0.59f, accentB_ = 0.88f;
  float surfaceR_ = 0.15f, surfaceG_ = 0.15f, surfaceB_ = 0.17f;
  float textR_ = 0.94f, textG_ = 0.94f, textB_ = 0.96f;
  float outlineR_ = 0.35f, outlineG_ = 0.35f, outlineB_ = 0.37f;

  float animT_ = 0.f;  // 0=off visual, 1=on visual
  uint32_t animStartMs_ = 0;
  bool animating_ = false;
};

}
