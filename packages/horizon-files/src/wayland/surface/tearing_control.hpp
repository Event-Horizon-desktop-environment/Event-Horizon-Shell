#pragma once

#include "wayland/core/protocols.hpp"

#include <cstdint>
#include <functional>

struct wl_surface;

namespace eh::wayland {

enum class TearingHint : uint32_t {
  VSync = WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC,
  Async = WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC,
};

class TearingControl {
public:
  TearingControl() = default;
  TearingControl(const TearingControl&) = delete;
  TearingControl& operator=(const TearingControl&) = delete;
  TearingControl(TearingControl&& other) noexcept;
  TearingControl& operator=(TearingControl&& other) noexcept;
  ~TearingControl();

  void bind(wp_tearing_control_manager_v1* manager) { manager_ = manager; }

  bool create(wl_surface* surface);
  void destroy();
  void cleanup();

  void set_hint(TearingHint hint);

  explicit operator bool() const { return tearingCtrl_ != nullptr; }

private:
  wp_tearing_control_manager_v1* manager_ = nullptr;
  wp_tearing_control_v1* tearingCtrl_ = nullptr;
};

} // namespace eh::wayland
