#pragma once

#include "wl/core/protocols.hpp"

namespace eh::wayland {

class ColorManager {
public:
  ColorManager() = default;
  ColorManager(const ColorManager&) = delete;
  ColorManager& operator=(const ColorManager&) = delete;
  ColorManager(ColorManager&& other) noexcept;
  ColorManager& operator=(ColorManager&& other) noexcept;
  ~ColorManager();

  void bind(wp_color_manager_v1* manager) { manager_ = manager; }

  wp_color_manager_v1* get() const { return manager_; }

  explicit operator bool() const { return manager_ != nullptr; }

private:
  wp_color_manager_v1* manager_ = nullptr;
};

} // namespace eh::wayland
