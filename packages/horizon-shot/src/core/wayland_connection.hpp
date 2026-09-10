#pragma once

#include "core/shm_buffer.hpp"
#include "core/wayland_seat.hpp"
#include "core/clipboard.hpp"
#include "core/foreign_toplevels.hpp"
#include "core/wlr_foreign_toplevels.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <memory>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct wl_output;
struct xdg_wm_base;
struct zwlr_layer_shell_v1;
struct zxdg_output_manager_v1;
struct zwlr_screencopy_manager_v1;
struct ext_image_copy_capture_manager_v1;
struct ext_output_image_capture_source_manager_v1;
struct ext_foreign_toplevel_image_capture_source_manager_v1;
struct wp_color_manager_v1;
struct ext_data_control_manager_v1;
struct zwlr_data_control_manager_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwp_linux_dmabuf_v1;
struct ext_foreign_toplevel_list_v1;
struct zxdg_output_v1;

namespace hs::core {

struct LogicalOutputBounds {
  wl_output* output = nullptr;
  std::string name;
  int global_x = 0;
  int global_y = 0;
  int width = 0;
  int height = 0;
};

struct PickedLogicalOutput {
  wl_output* output = nullptr;
  int logical_width = 0;
  int logical_height = 0;
};

class WaylandConnection {
public:
  WaylandConnection() = default;
  ~WaylandConnection();

  WaylandConnection(const WaylandConnection&) = delete;
  WaylandConnection& operator=(const WaylandConnection&) = delete;

  bool connect();
  void disconnect();

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  wl_shm* shm() const { return shm_; }
  wl_seat* seat() const { return seat_; }
  xdg_wm_base* xdg_base() const { return xdgBase_; }
  zwlr_layer_shell_v1* layer_shell() const { return layerShell_; }
  zxdg_output_manager_v1* xdg_output_manager() const { return xdgOutputMgr_; }
  zwlr_screencopy_manager_v1* screencopy_manager() const { return screencopyMgr_; }
  ext_image_copy_capture_manager_v1* ext_image_copy_capture_manager() const { return extImageCopyCaptureMgr_; }
  ext_output_image_capture_source_manager_v1* ext_output_image_capture_source_manager() const { return extOutputImageCaptureSourceMgr_; }
  ext_foreign_toplevel_image_capture_source_manager_v1* ext_foreign_toplevel_image_capture_source_manager() const { return extForeignToplevelImageCaptureSourceMgr_; }
  wp_color_manager_v1* color_manager() const { return colorMgr_; }
  ext_data_control_manager_v1* ext_data_control_manager() const { return extDataControlMgr_; }
  zwlr_data_control_manager_v1* wlr_data_control_manager() const { return wlrDataControlMgr_; }
  zwp_linux_dmabuf_v1* linux_dmabuf() const { return linuxDmabuf_; }
  ext_foreign_toplevel_list_v1* ext_foreign_toplevel_list() const { return extForeignToplevelList_; }
  bool has_ext_foreign_toplevel_list() const { return extForeignToplevelList_ != nullptr; }
  bool has_wlr_foreign_toplevel_manager() const { return wlrForeignToplevelMgr_ != nullptr; }
  bool has_any_toplevel_list() const { return has_ext_foreign_toplevel_list() || has_wlr_foreign_toplevel_manager(); }
  bool has_xwayland() const { return hasXWayland_; }
  ExtForeignToplevels& ext_foreign_toplevels() { return extForeignToplevels_; }
  WlrForeignToplevels& wlr_foreign_toplevels() { return wlrForeignToplevels_; }

  void refresh_logical_outputs();
  std::vector<LogicalOutputBounds> logical_output_bounds() const;
  PickedLogicalOutput pick_largest_logical_output() const;

private:
  static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version);
  static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);
  static constexpr wl_registry_listener kRegistryListener_ = {
      .global = registry_global,
      .global_remove = registry_global_remove,
  };

  wl_display* display_ = nullptr;
  wl_registry* registry_ = nullptr;

  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  wl_seat* seat_ = nullptr;

  xdg_wm_base* xdgBase_ = nullptr;
  zwlr_layer_shell_v1* layerShell_ = nullptr;
  zxdg_output_manager_v1* xdgOutputMgr_ = nullptr;
  zwlr_screencopy_manager_v1* screencopyMgr_ = nullptr;
  ext_image_copy_capture_manager_v1* extImageCopyCaptureMgr_ = nullptr;
  ext_output_image_capture_source_manager_v1* extOutputImageCaptureSourceMgr_ = nullptr;
  ext_foreign_toplevel_image_capture_source_manager_v1* extForeignToplevelImageCaptureSourceMgr_ = nullptr;
  wp_color_manager_v1* colorMgr_ = nullptr;
  ext_data_control_manager_v1* extDataControlMgr_ = nullptr;
  zwlr_data_control_manager_v1* wlrDataControlMgr_ = nullptr;
  zwp_linux_dmabuf_v1* linuxDmabuf_ = nullptr;
  ext_foreign_toplevel_list_v1* extForeignToplevelList_ = nullptr;
  ExtForeignToplevels extForeignToplevels_{};
  zwlr_foreign_toplevel_manager_v1* wlrForeignToplevelMgr_ = nullptr;
  WlrForeignToplevels wlrForeignToplevels_{};
  bool hasXWayland_ = false;

public:
  struct OutputSlot {
    wl_output* output = nullptr;
    zxdg_output_v1* xdg = nullptr;
    std::string name;
    int logical_x = 0;
    int logical_y = 0;
    int logical_w = 0;
    int logical_h = 0;
    int mode_w = 0;
    int mode_h = 0;
    int scale = 1;
    bool ready = false;
  };

private:
  std::vector<std::unique_ptr<OutputSlot>> tracked_outputs_;
  void bind_xdg_for_tracked_();
};

}
