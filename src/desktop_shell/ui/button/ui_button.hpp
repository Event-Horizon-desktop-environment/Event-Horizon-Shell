#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "desktop_shell/ui/theme.hpp"

namespace eh::ui {

enum class ButtonStyle {
  Filled,
  Tonal,
  Outlined,
  Ghost,
};

class Button {
public:
  Button();
  ~Button() = default;

  void setLabel(std::string_view text);
  void setGlyph(std::string_view name);
  void setStyle(ButtonStyle style);
  void setEnabled(bool enabled);
  void setMinSize(float w, float h);
  void setFontSize(float size);
  void setAccentColor(float r, float g, float b);
  void setSurfaceColor(float r, float g, float b);
  void setTextColor(float r, float g, float b);
  void setOutlineColor(float r, float g, float b);

  void setHovered(bool h);
  void setPressed(bool p);

  void setOnClick(std::function<void()> cb);
  void setOnPress(std::function<void()> cb);
  void setOnRelease(std::function<void()> cb);

  [[nodiscard]] std::string_view label() const noexcept { return label_; }
  [[nodiscard]] ButtonStyle style() const noexcept { return style_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] float minWidth() const noexcept { return minW_; }
  [[nodiscard]] float minHeight() const noexcept { return minH_; }
  [[nodiscard]] bool hovered() const noexcept { return hovered_; }
  [[nodiscard]] bool pressed() const noexcept { return pressed_; }

  void setGeometry(float x, float y, float w, float h);
  bool containsPoint(float px, float py) const noexcept;

  bool handlePointerEnter(float px, float py);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerUp(float px, float py);

  void paint(cairo_t* cr) const;

private:
  void resolveColors(float& bg_r, float& bg_g, float& bg_b, float& bg_a,
                     float& fg_r, float& fg_g, float& fg_b, float& fg_a,
                     float& border_r, float& border_g, float& border_b, float& border_a,
                     float& border_w) const;

  std::string label_;
  std::string glyph_;
  ButtonStyle style_ = ButtonStyle::Filled;
  bool enabled_ = true;
  bool hovered_ = false;
  bool pressed_ = false;
  float x_ = 0.0f, y_ = 0.0f, w_ = 0.0f, h_ = 0.0f;
  float minW_ = 64.0f, minH_ = 36.0f;
  float fontSize_ = 14.0f;

  float accentR_ = global_accent_r();
  float accentG_ = global_accent_g();
  float accentB_ = global_accent_b();
  float surfaceR_ = global_surface_r();
  float surfaceG_ = global_surface_g();
  float surfaceB_ = global_surface_b();
  float textR_ = 0.94f, textG_ = 0.94f, textB_ = 0.96f;
  float outlineR_ = global_outline_r();
  float outlineG_ = global_outline_g();
  float outlineB_ = global_outline_b();

  std::function<void()> onClick_;
  std::function<void()> onPress_;
  std::function<void()> onRelease_;
};

}
