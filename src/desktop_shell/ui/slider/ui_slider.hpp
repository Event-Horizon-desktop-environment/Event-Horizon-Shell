#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <cairo/cairo.h>

#include "desktop_shell/ui/theme.hpp"

namespace eh::ui {

class Slider {
public:
  Slider();
  ~Slider() = default;

  void setRange(float lo, float hi);
  void setStep(float step);
  void setValue(float val);
  void animateToValue(float val, uint64_t nowMs);
  void setEnabled(bool enabled);
  void setGeometry(float x, float y, float w, float h);
  void setHovered(bool h);
  void setPressed(bool p);
  void setShowValueLabel(bool show);
  void setValueLabel(const char* text);

  void setOnValueChanged(std::function<void(float)> cb);
  void setOnDragStarted(std::function<void()> cb);
  void setOnDragFinished(std::function<void()> cb);

  [[nodiscard]] float value() const noexcept { return value_; }
  [[nodiscard]] float minValue() const noexcept { return lo_; }
  [[nodiscard]] float maxValue() const noexcept { return hi_; }
  [[nodiscard]] float step() const noexcept { return step_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool dragging() const noexcept { return dragging_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] bool isAnimating() const noexcept { return animDurationMs_ > 0; }
  [[nodiscard]] float animatedValue(uint64_t nowMs) const;

  void setAccentColor(float r, float g, float b);
  void setSurfaceColor(float r, float g, float b);
  void setTextColor(float r, float g, float b);
  void setTrackColor(float r, float g, float b);

  bool containsPoint(float px, float py) const noexcept;
  float grabOffset(float px) const noexcept;

  bool handlePointerEnter(float px, float py);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerMove(float px, float py);
  bool handlePointerUp(float px, float py);
  bool handleScroll(float delta);

  void paint(cairo_t* cr) const;
  void paint(cairo_t* cr, uint64_t nowMs) const;

private:
  void applyValue(float raw);
  [[nodiscard]] float snap(float val) const noexcept;
  [[nodiscard]] float normalized() const noexcept;
  [[nodiscard]] float thumbCenter() const noexcept;
  [[nodiscard]] float trackLeft() const noexcept;
  [[nodiscard]] float trackWidth() const noexcept;
  void drawThumbAt(cairo_t* cr, float tc, float ts, float ty, float alpha, bool isHov, bool isDrag) const;
  void paintBody(cairo_t* cr, float paintValue, bool isHov, bool isDrag) const;

  float lo_ = 0.0f;
  float hi_ = 100.0f;
  float step_ = 1.0f;
  float value_ = 50.0f;
  float x_ = 0.0f, y_ = 0.0f, w_ = 0.0f, h_ = 0.0f;
  float thumbSize_ = 16.0f;
  float trackHeight_ = 4.0f;
  bool enabled_ = true;
  bool hovered_ = false;
  bool dragging_ = false;
  bool forceHover_ = false;
  bool forcePressed_ = false;
  bool showValueLabel_ = false;
  std::string valueLabel_;
  float grabOffsetX_ = 0.0f;

  uint64_t animStartMs_ = 0;
  float animFrom_ = 0.0f;
  float animTo_ = 0.0f;
  uint64_t animDurationMs_ = 0;

  float accentR_ = global_accent_r();
  float accentG_ = global_accent_g();
  float accentB_ = global_accent_b();
  float surfaceR_ = global_surface_r();
  float surfaceG_ = global_surface_g();
  float surfaceB_ = global_surface_b();
  float textR_ = 0.94f, textG_ = 0.94f, textB_ = 0.96f;
  float trackR_ = global_outline_r();
  float trackG_ = global_outline_g();
  float trackB_ = global_outline_b();

  std::function<void(float)> onValueChanged_;
  std::function<void()> onDragStarted_;
  std::function<void()> onDragFinished_;
};

}
