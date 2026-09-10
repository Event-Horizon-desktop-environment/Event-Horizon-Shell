#pragma once

#include "wayland/core/connection.hpp"

// Generated protocol headers for the types we use
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"

struct wl_surface;

namespace hpv {

class SurfaceExtensions {
public:
    SurfaceExtensions() = default;
    ~SurfaceExtensions();

    void init(WaylandConnection& conn, wl_surface* surface);

    wp_viewport* viewport() const { return viewport_; }

    void set_tearing(bool async);

private:
    static void handle_preferred_scale(void* data, wp_fractional_scale_v1*,
                                       uint32_t scale_120);

    static constexpr wp_fractional_scale_v1_listener fractional_scale_listener_ = {
        .preferred_scale = handle_preferred_scale,
    };

    wp_viewport* viewport_ = nullptr;
    wp_fractional_scale_v1* fractional_scale_ = nullptr;
    wp_tearing_control_v1* tearing_control_ = nullptr;
    float preferred_scale_ = 1.0f;
};

}
