#include "wayland/surface/surface_extensions.hpp"

#include <wayland-client.h>
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"

namespace hpv {

SurfaceExtensions::~SurfaceExtensions() {
    if (tearing_control_) wp_tearing_control_v1_destroy(tearing_control_);
    if (fractional_scale_) wp_fractional_scale_v1_destroy(fractional_scale_);
    if (viewport_) wp_viewport_destroy(viewport_);
}

void SurfaceExtensions::init(WaylandConnection& conn, wl_surface* surface) {
    // Viewporter
    if (conn.viewporter()) {
        viewport_ = wp_viewporter_get_viewport(conn.viewporter(), surface);
    }

    // Fractional scale
    if (conn.fractional_scale_manager()) {
        fractional_scale_ = wp_fractional_scale_manager_v1_get_fractional_scale(
            conn.fractional_scale_manager(), surface);
        wp_fractional_scale_v1_add_listener(fractional_scale_,
                                             &fractional_scale_listener_, this);
    }

    // Tearing control
    if (conn.tearing_control_manager()) {
        tearing_control_ = wp_tearing_control_manager_v1_get_tearing_control(
            conn.tearing_control_manager(), surface);
    }
}

void SurfaceExtensions::set_tearing(bool async) {
    if (!tearing_control_) return;
    wp_tearing_control_v1_set_presentation_hint(tearing_control_,
        async ? WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
              : WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC);
}

void SurfaceExtensions::handle_preferred_scale(void* data,
                                                 wp_fractional_scale_v1*,
                                                 uint32_t scale_120) {
    auto* self = static_cast<SurfaceExtensions*>(data);
    self->preferred_scale_ = scale_120 / 120.0f;
}

}
