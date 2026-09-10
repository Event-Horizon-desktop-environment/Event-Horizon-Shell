#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "desktop_shell/ui/theme.hpp"

namespace eh::ui {

class Dropdown {
public:
  Dropdown();
  ~Dropdown() = default;

  void setOptions(std::vector<std::string> items);
  void setSelectedIndex(std::size_t idx);
  void setPlaceholder(std::string_view text);
  void setEnabled(bool enabled);
  void setGeometry(float x, float y, float w, float h);
  void setMaxVisible(std::size_t rows);
  void setAccentColor(float r, float g, float b);
  void setSurfaceColor(float r, float g, float b);
  void setTextColor(float r, float g, float b);
  void setOutlineColor(float r, float g, float b);

  void setOnSelect(std::function<void(std::size_t, std::string_view)> cb);

  void setTriggerHovered(bool h);
  void setTriggerPressed(bool p);
  void paintTrigger(cairo_t* cr) const;

  [[nodiscard]] std::size_t selectedIndex() const noexcept { return sel_; }
  [[nodiscard]] std::string_view selectedText() const noexcept;
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] float menuHeight() const noexcept;
  [[nodiscard]] const std::vector<std::string>& options() const noexcept { return options_; }

  bool containsPoint(float px, float py) const noexcept;
  bool containsMenuPoint(float px, float py) const noexcept;

  void close();
  void toggle();

  bool handlePointerEnter(float px, float py);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerMove(float px, float py);
  bool handlePointerUp(float px, float py);

  void paint(cairo_t* cr) const;

private:
  static constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

  void selectIndex(std::size_t idx);
  [[nodiscard]] std::size_t optionAt(float px, float py) const noexcept;
  [[nodiscard]] float dropTop() const noexcept;

  std::vector<std::string> options_;
  std::size_t sel_ = kNpos;
  std::size_t hovered_ = kNpos;
  std::string placeholder_;
  bool enabled_ = true;
  bool open_ = false;
  bool trigHovered_ = false;
  bool trigPressed_ = false;
  bool forceTrigHover_ = false;
  bool forceTrigPress_ = false;
  float x_ = 0.0f, y_ = 0.0f, w_ = 0.0f, h_ = 0.0f;
  float optH_ = 32.0f;
  float padding_ = 12.0f;
  float fontSize_ = 14.0f;
  std::size_t maxVisible_ = 6;

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

  std::function<void(std::size_t, std::string_view)> onSelect_;
};

}
