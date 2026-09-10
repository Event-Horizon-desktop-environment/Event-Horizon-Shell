#include "wayland/core/connection.hpp"

#include <cstring>
#include <cstdio>

#include <wayland-client.h>

namespace eh::wayland {

WaylandConnection::WaylandConnection() = default;

WaylandConnection::~WaylandConnection() {
  disconnect();
}

bool WaylandConnection::connect(bool) {
  display_ = wl_display_connect(nullptr);
  if (!display_) return false;

  owns_display_ = true;
  registry_ = wl_display_get_registry(display_);
  if (!registry_) {
    wl_display_disconnect(display_);
    display_ = nullptr;
    return false;
  }

  wl_registry_add_listener(registry_, &kRegistryListener_, this);
  wl_display_roundtrip(display_);
  wl_display_roundtrip(display_);

  return compositor_ && shm_ && seat_;
}

void WaylandConnection::disconnect() {
  if (extDataControlMgr_) ext_data_control_manager_v1_destroy(extDataControlMgr_);
  extDataControlMgr_ = nullptr;
  if (wlrDataControlMgr_) zwlr_data_control_manager_v1_destroy(wlrDataControlMgr_);
  wlrDataControlMgr_ = nullptr;
  if (dataDeviceMgr_) wl_data_device_manager_destroy(dataDeviceMgr_);
  dataDeviceMgr_ = nullptr;
  if (tearingControlMgr_) wp_tearing_control_manager_v1_destroy(tearingControlMgr_);
  tearingControlMgr_ = nullptr;
  if (fractionalScaleMgr_) wp_fractional_scale_manager_v1_destroy(fractionalScaleMgr_);
  fractionalScaleMgr_ = nullptr;
  if (viewporter_) wp_viewporter_destroy(viewporter_);
  viewporter_ = nullptr;
  if (xdgBase_) xdg_wm_base_destroy(xdgBase_);
  xdgBase_ = nullptr;
  if (subcompositor_) wl_subcompositor_destroy(subcompositor_);
  subcompositor_ = nullptr;
  if (shm_) wl_shm_destroy(shm_);
  shm_ = nullptr;
  if (seat_) wl_seat_destroy(seat_);
  seat_ = nullptr;
  if (compositor_) wl_compositor_destroy(compositor_);
  compositor_ = nullptr;
  if (registry_) wl_registry_destroy(registry_);
  registry_ = nullptr;
  if (display_ && owns_display_) wl_display_disconnect(display_);
  display_ = nullptr;
  owns_display_ = false;
}

void WaylandConnection::registry_global(void* data, wl_registry* registry, uint32_t name,
                                         const char* iface, uint32_t version) {
  auto* self = static_cast<WaylandConnection*>(data);

  if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
    self->compositor_ = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(iface, wl_subcompositor_interface.name) == 0) {
    self->subcompositor_ = static_cast<wl_subcompositor*>(
        wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
  } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
    self->shm_ = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
    self->seat_ = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
  } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
    self->xdgBase_ = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 2u)));
  } else if (std::strcmp(iface, wl_data_device_manager_interface.name) == 0) {
    self->dataDeviceMgr_ = static_cast<wl_data_device_manager*>(
        wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
  } else if (std::strcmp(iface, ext_data_control_manager_v1_interface.name) == 0) {
    self->extDataControlMgr_ = static_cast<ext_data_control_manager_v1*>(
        wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
  } else if (std::strcmp(iface, zwlr_data_control_manager_v1_interface.name) == 0) {
    self->wlrDataControlMgr_ = static_cast<zwlr_data_control_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, 1));
  } else if (std::strcmp(iface, wp_viewporter_interface.name) == 0) {
    self->viewporter_ = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
  } else if (std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
    self->fractionalScaleMgr_ = static_cast<wp_fractional_scale_manager_v1*>(
        wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
  } else if (std::strcmp(iface, wp_tearing_control_manager_v1_interface.name) == 0) {
    self->tearingControlMgr_ = static_cast<wp_tearing_control_manager_v1*>(
        wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1));
  }
}

void WaylandConnection::registry_global_remove(void*, wl_registry*, uint32_t) {}

}
