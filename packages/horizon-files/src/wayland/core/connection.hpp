#pragma once

#include "wayland/core/protocols.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct wl_registry;
struct wl_display;
struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct wl_subcompositor;

namespace eh::wayland {

class WaylandConnection {
public:
  WaylandConnection();
  WaylandConnection(const WaylandConnection&) = delete;
  WaylandConnection& operator=(const WaylandConnection&) = delete;
  WaylandConnection(WaylandConnection&&) = delete;
  WaylandConnection& operator=(WaylandConnection&&) = delete;
  ~WaylandConnection();

  [[nodiscard]] bool connect(bool init_workspaces = true);

  [[nodiscard]] wl_display* display() const { return display_; }
  [[nodiscard]] wl_registry* registry() const { return registry_; }
  [[nodiscard]] wl_compositor* compositor() const { return compositor_; }
  [[nodiscard]] wl_shm* shm() const { return shm_; }
  [[nodiscard]] wl_seat* seat() const { return seat_; }
  [[nodiscard]] wl_subcompositor* subcompositor() const { return subcompositor_; }
  [[nodiscard]] xdg_wm_base* xdg_base() const { return xdgBase_; }
  [[nodiscard]] wl_data_device_manager* data_device_manager() const { return dataDeviceMgr_; }
  [[nodiscard]] ext_data_control_manager_v1* ext_data_control_manager() const { return extDataControlMgr_; }
  [[nodiscard]] zwlr_data_control_manager_v1* wlr_data_control_manager() const { return wlrDataControlMgr_; }
  [[nodiscard]] wp_viewporter* viewporter() const { return viewporter_; }
  [[nodiscard]] wp_fractional_scale_manager_v1* fractional_scale_manager() const { return fractionalScaleMgr_; }
  [[nodiscard]] wp_tearing_control_manager_v1* tearing_control_manager() const { return tearingControlMgr_; }

  void disconnect();

private:
  static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version);
  static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);
  static constexpr wl_registry_listener kRegistryListener_ = {
      .global = registry_global,
      .global_remove = registry_global_remove,
  };

  wl_display* display_ = nullptr;
  bool owns_display_ = false;
  wl_registry* registry_ = nullptr;

  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  wl_seat* seat_ = nullptr;
  wl_subcompositor* subcompositor_ = nullptr;

  xdg_wm_base* xdgBase_ = nullptr;
  wp_viewporter* viewporter_ = nullptr;
  wp_fractional_scale_manager_v1* fractionalScaleMgr_ = nullptr;
  wp_tearing_control_manager_v1* tearingControlMgr_ = nullptr;
  wl_data_device_manager* dataDeviceMgr_ = nullptr;
  ext_data_control_manager_v1* extDataControlMgr_ = nullptr;
  zwlr_data_control_manager_v1* wlrDataControlMgr_ = nullptr;
};

}
