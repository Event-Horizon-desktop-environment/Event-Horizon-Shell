#include "wayland/core/connection.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

// Generated protocol headers
#include "xdg-shell-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"
#include "color-management-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "ext-data-control-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

namespace hpv {

namespace {

void xdg_wm_base_ping_handler(void* /*data*/, xdg_wm_base* shell, uint32_t serial) {
    xdg_wm_base_pong(shell, serial);
}

constexpr xdg_wm_base_listener xdg_wm_base_listener_ = {
    .ping = xdg_wm_base_ping_handler,
};

// Color management output image description listeners
constexpr wp_image_description_v1_listener cm_img_desc_listener_ = {
    .failed = WaylandConnection::cm_output_img_desc_failed,
    .ready = WaylandConnection::cm_output_img_desc_ready,
    .ready2 = nullptr,
};

// Image description info listeners
constexpr wp_image_description_info_v1_listener cm_img_desc_info_listener_ = {
    .done = WaylandConnection::cm_output_info_done,
    .icc_file = WaylandConnection::cm_output_info_icc,
    .primaries = [](void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {},
    .primaries_named = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .tf_power = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .tf_named = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .luminances = [](void*, wp_image_description_info_v1*, uint32_t, uint32_t, uint32_t) {},
    .target_primaries = [](void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {},
    .target_luminance = [](void*, wp_image_description_info_v1*, uint32_t, uint32_t) {},
    .target_max_cll = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .target_max_fall = [](void*, wp_image_description_info_v1*, uint32_t) {},
};

// xdg_output_v1 listener for output tracking
void xdg_output_name_handler(void* data, zxdg_output_v1*, const char* name) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    if (name) slot->name = name;
}

void xdg_output_logical_position_handler(void* data, zxdg_output_v1*, int32_t x, int32_t y) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    slot->logical_x = x;
    slot->logical_y = y;
}

void xdg_output_logical_size_handler(void* data, zxdg_output_v1*, int32_t w, int32_t h) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    slot->logical_w = w;
    slot->logical_h = h;
}

void xdg_output_done_handler(void* data, zxdg_output_v1*) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    slot->ready = true;
}

constexpr zxdg_output_v1_listener xdg_output_listener_ = {
    .logical_position = xdg_output_logical_position_handler,
    .logical_size = xdg_output_logical_size_handler,
    .done = xdg_output_done_handler,
    .name = xdg_output_name_handler,
    .description = [](void*, zxdg_output_v1*, const char*) {},
};

// wl_output listener (extended version for tracking)
void output_geometry_handler(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void output_mode_handler(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void output_done_handler(void* data, wl_output*) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    slot->ready = true;
}
void output_scale_handler(void*, wl_output*, int32_t) {}
void output_name_handler(void* data, wl_output*, const char* name) {
    auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
    if (name) slot->name = name;
}

constexpr wl_output_listener output_listener_ = {
    .geometry = output_geometry_handler,
    .mode = output_mode_handler,
    .done = output_done_handler,
    .scale = output_scale_handler,
    .name = output_name_handler,
    .description = [](void*, wl_output*, const char*) {},
};

} // end anonymous namespace

void WaylandConnection::registry_global(void* data, wl_registry* registry, uint32_t name,
                                         const char* iface, uint32_t version) {
    auto& self = *static_cast<WaylandConnection*>(data);
    std::string_view ifc(iface ? iface : "");

    if (ifc == wl_compositor_interface.name) {
        self.compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             std::min(version, 4u)));
    } else if (ifc == wl_shm_interface.name) {
        self.shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (ifc == wl_seat_interface.name) {
        if (!self.seat_) {
            self.seat_ = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface,
                                 std::min(version, 5u)));
        }
    } else if (ifc == xdg_wm_base_interface.name) {
        self.xdg_base_ = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface,
                             std::min(version, 5u)));
        xdg_wm_base_add_listener(self.xdg_base_, &xdg_wm_base_listener_, nullptr);
    } else if (ifc == wp_viewporter_interface.name) {
        self.viewporter_ = static_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (ifc == wp_fractional_scale_manager_v1_interface.name) {
        self.fractional_scale_mgr_ = static_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(registry, name,
                &wp_fractional_scale_manager_v1_interface, 1));
    } else if (ifc == wp_tearing_control_manager_v1_interface.name) {
        self.tearing_control_mgr_ = static_cast<wp_tearing_control_manager_v1*>(
            wl_registry_bind(registry, name,
                &wp_tearing_control_manager_v1_interface, 1));
    } else if (ifc == wp_color_manager_v1_interface.name) {
        self.color_mgr_ = static_cast<wp_color_manager_v1*>(
            wl_registry_bind(registry, name,
                &wp_color_manager_v1_interface,
                std::min(version, 2u)));
    } else if (ifc == zwp_idle_inhibit_manager_v1_interface.name) {
        self.idle_inhibit_mgr_ = static_cast<zwp_idle_inhibit_manager_v1*>(
            wl_registry_bind(registry, name,
                &zwp_idle_inhibit_manager_v1_interface, 1));
    } else if (ifc == zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) {
        self.keyboard_shortcuts_inhibit_mgr_ =
            static_cast<zwp_keyboard_shortcuts_inhibit_manager_v1*>(
                wl_registry_bind(registry, name,
                    &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1));
    } else if (ifc == zwp_linux_dmabuf_v1_interface.name) {
        self.linux_dmabuf_ = static_cast<zwp_linux_dmabuf_v1*>(
            wl_registry_bind(registry, name,
                &zwp_linux_dmabuf_v1_interface,
                std::min(version, 4u)));
    } else if (ifc == wp_single_pixel_buffer_manager_v1_interface.name) {
        self.single_pixel_buffer_mgr_ =
            static_cast<wp_single_pixel_buffer_manager_v1*>(
                wl_registry_bind(registry, name,
                    &wp_single_pixel_buffer_manager_v1_interface, 1));
    } else if (ifc == zxdg_output_manager_v1_interface.name) {
        self.xdg_output_mgr_ = static_cast<zxdg_output_manager_v1*>(
            wl_registry_bind(registry, name,
                &zxdg_output_manager_v1_interface,
                std::min(version, 3u)));
    } else if (ifc == wl_data_device_manager_interface.name) {
        self.data_device_mgr_ = static_cast<wl_data_device_manager*>(
            wl_registry_bind(registry, name,
                &wl_data_device_manager_interface,
                std::min(version, 3u)));
    // --- Screenshot protocol bindings ---
    } else if (ifc == zwlr_layer_shell_v1_interface.name) {
        self.layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name,
                &zwlr_layer_shell_v1_interface,
                std::min(version, 4u)));
    } else if (ifc == zwlr_screencopy_manager_v1_interface.name) {
        self.screencopy_mgr_ = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(registry, name,
                &zwlr_screencopy_manager_v1_interface,
                std::min(version, 3u)));
    } else if (ifc == ext_image_copy_capture_manager_v1_interface.name) {
        self.ext_image_copy_capture_mgr_ = static_cast<ext_image_copy_capture_manager_v1*>(
            wl_registry_bind(registry, name,
                &ext_image_copy_capture_manager_v1_interface, 1));
    } else if (ifc == ext_output_image_capture_source_manager_v1_interface.name) {
        self.ext_output_image_capture_source_mgr_ = static_cast<ext_output_image_capture_source_manager_v1*>(
            wl_registry_bind(registry, name,
                &ext_output_image_capture_source_manager_v1_interface, 1));
    } else if (ifc == ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) {
        self.ext_foreign_toplevel_image_capture_source_mgr_ =
            static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(
                wl_registry_bind(registry, name,
                    &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    } else if (ifc == ext_data_control_manager_v1_interface.name) {
        self.ext_data_control_mgr_ = static_cast<ext_data_control_manager_v1*>(
            wl_registry_bind(registry, name,
                &ext_data_control_manager_v1_interface, 1));
    } else if (ifc == zwlr_data_control_manager_v1_interface.name) {
        self.wlr_data_control_mgr_ = static_cast<zwlr_data_control_manager_v1*>(
            wl_registry_bind(registry, name,
                &zwlr_data_control_manager_v1_interface,
                std::min(version, 2u)));
    } else if (ifc == ext_foreign_toplevel_list_v1_interface.name) {
        self.ext_foreign_toplevel_list_ = static_cast<ext_foreign_toplevel_list_v1*>(
            wl_registry_bind(registry, name,
                &ext_foreign_toplevel_list_v1_interface, 1));
        if (self.ext_foreign_toplevel_list_) {
            self.extForeignToplevels_.bind(self.ext_foreign_toplevel_list_, self.display_);
        }
    } else if (ifc == zwlr_foreign_toplevel_manager_v1_interface.name) {
        self.wlr_foreign_toplevel_mgr_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(registry, name,
                &zwlr_foreign_toplevel_manager_v1_interface,
                std::min(version, 3u)));
        if (self.wlr_foreign_toplevel_mgr_) {
            self.wlrForeignToplevels_.bind(self.wlr_foreign_toplevel_mgr_, self.display_);
        }
    } else if (ifc == wl_output_interface.name) {
        auto slot = std::make_unique<WaylandConnection::OutputSlot>();
        slot->output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface,
                             std::min(version, 4u)));
        wl_output_add_listener(slot->output, &output_listener_, slot.get());
        if (!self.output_) self.output_ = slot->output;
        self.tracked_outputs_.push_back(std::move(slot));
    }
}

void WaylandConnection::registry_global_remove(void* /*data*/, wl_registry* /*registry*/,
                                                uint32_t /*name*/) {}

bool WaylandConnection::connect() {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::cerr << "wl_display_connect failed\n";
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    if (!registry_) {
        std::cerr << "wl_display_get_registry failed\n";
        return false;
    }

    wl_registry_add_listener(registry_, &registry_listener_, this);
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !xdg_base_) {
        std::cerr << "Missing required Wayland globals\n";
        return false;
    }

    // Set output_ from first tracked output
    if (!tracked_outputs_.empty()) {
        output_ = tracked_outputs_[0]->output;
    }

    // Bind xdg_output for each tracked output
    bind_xdg_for_tracked_();

    // Fetch display ICC profile from color management protocol
    fetch_display_icc_profile();

    return true;
}

void WaylandConnection::fetch_display_icc_profile() {
    if (!color_mgr_ || !output_) return;
    if (cm_output_) wp_color_management_output_v1_destroy(cm_output_);
    cm_output_ = wp_color_manager_v1_get_output(color_mgr_, output_);
    if (!cm_output_) return;

    if (cm_img_desc_) wp_image_description_v1_destroy(cm_img_desc_);
    cm_img_desc_ = wp_color_management_output_v1_get_image_description(cm_output_);
    if (!cm_img_desc_) return;

    wp_image_description_v1_add_listener(cm_img_desc_, &cm_img_desc_listener_, this);

    // Dispatch events until ICC profile is ready or failed
    cm_icc_ready_ = false;
    display_icc_profile_.clear();
    wl_display_roundtrip(display_);
    while (!cm_icc_ready_) {
        if (wl_display_dispatch(display_) < 0) break;
    }
}

void WaylandConnection::cm_output_img_desc_ready(void* data, wp_image_description_v1* desc,
                                                    uint32_t /*identity*/) {
    auto* self = static_cast<WaylandConnection*>(data);
    wp_image_description_info_v1* info = wp_image_description_v1_get_information(desc);
    wp_image_description_info_v1_add_listener(info, &cm_img_desc_info_listener_, self);
    wp_image_description_v1_destroy(desc);
    // Don't mark ready yet — wait for icc_file + done
}

void WaylandConnection::cm_output_img_desc_failed(void* data, wp_image_description_v1* desc,
                                                     uint32_t /*cause*/, const char* /*msg*/) {
    auto* self = static_cast<WaylandConnection*>(data);
    wp_image_description_v1_destroy(desc);
    self->cm_icc_ready_ = true;
}

void WaylandConnection::cm_output_info_icc(void* data, wp_image_description_info_v1* /*info*/,
                                              int32_t icc_fd, uint32_t icc_size) {
    auto* self = static_cast<WaylandConnection*>(data);
    if (icc_fd < 0 || icc_size == 0) return;

    void* map = mmap(nullptr, icc_size, PROT_READ, MAP_PRIVATE, icc_fd, 0);
    close(icc_fd);
    if (map == MAP_FAILED) return;

    self->display_icc_profile_.assign(static_cast<const uint8_t*>(map),
                                       static_cast<const uint8_t*>(map) + icc_size);
    munmap(map, icc_size);
}

void WaylandConnection::cm_output_info_done(void* data, wp_image_description_info_v1* info) {
    auto* self = static_cast<WaylandConnection*>(data);
    wp_image_description_info_v1_destroy(info);
    self->cm_icc_ready_ = true;
}

void WaylandConnection::bind_xdg_for_tracked_() {
    if (!xdg_output_mgr_) return;
    for (auto& slot : tracked_outputs_) {
        if (slot->output && !slot->xdg) {
            slot->xdg = zxdg_output_manager_v1_get_xdg_output(xdg_output_mgr_, slot->output);
            if (slot->xdg) {
                zxdg_output_v1_add_listener(slot->xdg, &xdg_output_listener_, slot.get());
            }
        }
    }
}

void WaylandConnection::refresh_logical_outputs() {
    bind_xdg_for_tracked_();
    if (display_) {
        wl_display_roundtrip(display_);
        wl_display_roundtrip(display_);
    }
}

std::vector<LogicalOutputBounds> WaylandConnection::logical_output_bounds() const {
    std::vector<LogicalOutputBounds> result;
    for (const auto& slot : tracked_outputs_) {
        if (!slot->ready || !slot->output) continue;
        LogicalOutputBounds b;
        b.output = slot->output;
        b.name = slot->name;
        b.global_x = slot->logical_x;
        b.global_y = slot->logical_y;
        b.width = slot->logical_w;
        b.height = slot->logical_h;
        result.push_back(std::move(b));
    }
    return result;
}

void WaylandConnection::disconnect() {
    // Clean up persistent toplevel tracking (shutdown destroys the protocol objects)
    extForeignToplevels_.shutdown();
    wlrForeignToplevels_.shutdown();
    ext_foreign_toplevel_list_ = nullptr;
    wlr_foreign_toplevel_mgr_ = nullptr;

    // Clean up tracked outputs
    for (auto& slot : tracked_outputs_) {
        if (slot->xdg) zxdg_output_v1_destroy(slot->xdg);
    }
    tracked_outputs_.clear();
    if (wlr_data_control_mgr_) zwlr_data_control_manager_v1_destroy(wlr_data_control_mgr_);
    if (ext_data_control_mgr_) ext_data_control_manager_v1_destroy(ext_data_control_mgr_);
    if (ext_foreign_toplevel_image_capture_source_mgr_) ext_foreign_toplevel_image_capture_source_manager_v1_destroy(ext_foreign_toplevel_image_capture_source_mgr_);
    if (ext_output_image_capture_source_mgr_) ext_output_image_capture_source_manager_v1_destroy(ext_output_image_capture_source_mgr_);
    if (ext_image_copy_capture_mgr_) ext_image_copy_capture_manager_v1_destroy(ext_image_copy_capture_mgr_);
    if (screencopy_mgr_) zwlr_screencopy_manager_v1_destroy(screencopy_mgr_);
    if (layer_shell_) zwlr_layer_shell_v1_destroy(layer_shell_);
    if (xdg_output_mgr_) zxdg_output_manager_v1_destroy(xdg_output_mgr_);
    if (single_pixel_buffer_mgr_) wp_single_pixel_buffer_manager_v1_destroy(single_pixel_buffer_mgr_);
    if (linux_dmabuf_) zwp_linux_dmabuf_v1_destroy(linux_dmabuf_);
    if (keyboard_shortcuts_inhibit_mgr_) zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(keyboard_shortcuts_inhibit_mgr_);
    if (idle_inhibit_mgr_) zwp_idle_inhibit_manager_v1_destroy(idle_inhibit_mgr_);
    if (cm_img_desc_) wp_image_description_v1_destroy(cm_img_desc_);
    if (cm_output_) wp_color_management_output_v1_destroy(cm_output_);
    if (color_mgr_) wp_color_manager_v1_destroy(color_mgr_);
    if (tearing_control_mgr_) wp_tearing_control_manager_v1_destroy(tearing_control_mgr_);
    if (fractional_scale_mgr_) wp_fractional_scale_manager_v1_destroy(fractional_scale_mgr_);
    if (viewporter_) wp_viewporter_destroy(viewporter_);
    if (data_device_mgr_) wl_data_device_manager_destroy(data_device_mgr_);
    if (xdg_base_) xdg_wm_base_destroy(xdg_base_);
    if (seat_) wl_seat_destroy(seat_);
    if (shm_) wl_shm_destroy(shm_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }

    // Null all remaining pointers to make disconnect() idempotent
    wlr_foreign_toplevel_mgr_ = nullptr;
    ext_foreign_toplevel_list_ = nullptr;
    wlr_data_control_mgr_ = nullptr;
    ext_data_control_mgr_ = nullptr;
    ext_foreign_toplevel_image_capture_source_mgr_ = nullptr;
    ext_output_image_capture_source_mgr_ = nullptr;
    ext_image_copy_capture_mgr_ = nullptr;
    screencopy_mgr_ = nullptr;
    layer_shell_ = nullptr;
    xdg_output_mgr_ = nullptr;
    single_pixel_buffer_mgr_ = nullptr;
    linux_dmabuf_ = nullptr;
    keyboard_shortcuts_inhibit_mgr_ = nullptr;
    idle_inhibit_mgr_ = nullptr;
    cm_img_desc_ = nullptr;
    cm_output_ = nullptr;
    color_mgr_ = nullptr;
    tearing_control_mgr_ = nullptr;
    fractional_scale_mgr_ = nullptr;
    viewporter_ = nullptr;
    data_device_mgr_ = nullptr;
    xdg_base_ = nullptr;
    seat_ = nullptr;
    output_ = nullptr;
    shm_ = nullptr;
    compositor_ = nullptr;
    registry_ = nullptr;
}

WaylandConnection::~WaylandConnection() {
    disconnect();
}

}
