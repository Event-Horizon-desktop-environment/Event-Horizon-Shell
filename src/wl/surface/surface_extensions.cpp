#include "wl/surface/surface_extensions.hpp"

#include <algorithm>

namespace eh::wayland {
namespace {

void fractional_scale_preferred_scale(void* data, wp_fractional_scale_v1*  , std::uint32_t scale) {
  auto* ext = static_cast<SurfaceExtensions*>(data);

  ext->preferredScale120 = static_cast<std::int32_t>(scale);
}

constexpr wp_fractional_scale_v1_listener kFractionalListener = {
    .preferred_scale = fractional_scale_preferred_scale,
};

}

void attach_surface_extensions(WaylandConnection& wl, wl_surface* surface, SurfaceExtensions& ext) {
   
  if (!surface) return;

  if (!ext.viewport && wl.viewporter()) {
    ext.viewport = wp_viewporter_get_viewport(wl.viewporter(), surface);
  }

  if (!ext.fractionalScale && wl.fractional_scale_manager()) {
    ext.fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(wl.fractional_scale_manager(), surface);
    if (ext.fractionalScale) {
      wp_fractional_scale_v1_add_listener(ext.fractionalScale, &kFractionalListener, &ext);
    }
  }

  if (!ext.tearingControl && wl.tearing_control_manager()) {
    ext.tearingControl = wp_tearing_control_manager_v1_get_tearing_control(
        wl.tearing_control_manager(), surface);
    if (ext.tearingControl) {
      wp_tearing_control_v1_set_presentation_hint(
          ext.tearingControl, WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);
    }
  }
}

void SurfaceExtensions::ensure_tearing_control(WaylandConnection& wl, wl_surface* surface) {
   
  if (!tearingControl && wl.tearing_control_manager()) {
    tearingControl = wp_tearing_control_manager_v1_get_tearing_control(
        wl.tearing_control_manager(), surface);
    if (tearingControl) {
      wp_tearing_control_v1_set_presentation_hint(
          tearingControl, WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);
    }
  }
}

}
