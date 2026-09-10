#pragma once

#include "wl/core/protocols.hpp"
#include "wl/core/connection.hpp"

#include <cstdint>

namespace eh::wayland {

struct SurfaceExtensions {
  wp_viewport* viewport = nullptr;
  wp_fractional_scale_v1* fractionalScale = nullptr;
  wp_tearing_control_v1* tearingControl = nullptr;
  std::int32_t preferredScale120 = 120;

  void destroy_tearing_control() {
    if (tearingControl) {
      wp_tearing_control_v1_destroy(tearingControl);
      tearingControl = nullptr;
    }
  }

  void ensure_tearing_control(WaylandConnection& wl, wl_surface* surface);

  void destroy() {
    destroy_tearing_control();
    if (viewport) {
      wp_viewport_destroy(viewport);
      viewport = nullptr;
    }
    if (fractionalScale) {
      wp_fractional_scale_v1_destroy(fractionalScale);
      fractionalScale = nullptr;
    }
    preferredScale120 = 120;
  }

  [[nodiscard]] double preferred_scale() const {
    return static_cast<double>(preferredScale120) / 120.0;
  }
};

void attach_surface_extensions(WaylandConnection& wl, wl_surface* surface, SurfaceExtensions& ext);

}
