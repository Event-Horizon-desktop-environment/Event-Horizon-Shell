#pragma once

#include "backends/interfaces/compositor_ipc.h"
#include "wl/core/protocols.hpp"
#include "wl/toplevel/ext_foreign_toplevels.hpp"
#include "wl/toplevel/workspaces.h"

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
struct hyprland_toplevel_mapping_manager_v1;
struct hyprland_toplevel_export_manager_v1;
namespace eh::wayland {

struct LogicalOutputInfo {
  std::string name;
  int logical_width = 0;
  int logical_height = 0;
};

struct PickedLogicalOutput {
  wl_output* output = nullptr;
  int logical_width = 0;
  int logical_height = 0;
};

struct LogicalOutputBounds {
  wl_output* output = nullptr;
  std::string name;
  int global_x = 0;
  int global_y = 0;
  int width = 0;
  int height = 0;
};

class WaylandConnection {
public:
  WaylandConnection();
  WaylandConnection(const WaylandConnection&) = delete;
  WaylandConnection& operator=(const WaylandConnection&) = delete;
  WaylandConnection(WaylandConnection&&) = delete;
  WaylandConnection& operator=(WaylandConnection&&) = delete;
  ~WaylandConnection();

  [[nodiscard]] bool connect(bool init_workspaces = true);

  [[nodiscard]] bool attach(wl_display* display, wl_seat* borrowed_seat = nullptr, bool init_workspaces = true);
  void disconnect();

  [[nodiscard]] wl_display* display() const { return display_; }
  [[nodiscard]] wl_registry* registry() const { return registry_; }

  [[nodiscard]] wl_compositor* compositor() const { return compositor_; }
  [[nodiscard]] wl_shm* shm() const { return shm_; }
  [[nodiscard]] wl_seat* seat() const { return seat_; }
  [[nodiscard]] wl_subcompositor* subcompositor() const { return subcompositor_; }

  [[nodiscard]] xdg_wm_base* xdg_base() const { return xdgBase_; }
  [[nodiscard]] zwlr_layer_shell_v1* layer_shell() const { return layerShell_; }
  [[nodiscard]] zxdg_output_manager_v1* xdg_output_manager() const { return xdgOutputMgr_; }
  [[nodiscard]] xdg_activation_v1* xdg_activation() const { return activation_; }
  [[nodiscard]] zwlr_foreign_toplevel_manager_v1* foreign_toplevel_manager() const { return foreignToplevelMgr_; }

  // Hand off foreign_toplevel_manager ownership so disconnect() won't destroy it.
  [[nodiscard]] zwlr_foreign_toplevel_manager_v1* release_foreign_toplevel_manager() {
    auto* p = foreignToplevelMgr_;
    foreignToplevelMgr_ = nullptr;
    return p;
  }
  [[nodiscard]] ext_session_lock_manager_v1* session_lock_manager() const { return sessionLockMgr_; }
  [[nodiscard]] ext_idle_notifier_v1* idle_notifier() const { return idleNotifier_; }
  [[nodiscard]] zwp_idle_inhibit_manager_v1* idle_inhibit_manager() const { return idleInhibitMgr_; }
  [[nodiscard]] ext_background_effect_manager_v1* background_effect_manager() const { return backgroundEffectMgr_; }
  [[nodiscard]] wp_fractional_scale_manager_v1* fractional_scale_manager() const { return fractionalScaleMgr_; }
  [[nodiscard]] wp_viewporter* viewporter() const { return viewporter_; }
  [[nodiscard]] zwp_virtual_keyboard_manager_v1* virtual_keyboard_manager() const { return virtualKeyboardMgr_; }
  [[nodiscard]] ext_workspace_manager_v1* workspace_manager() const { return workspaceMgr_; }
  [[nodiscard]] zdwl_ipc_manager_v2* dwl_ipc_manager() const { return dwlIpcMgr_; }
  [[nodiscard]] WaylandWorkspaces& wayland_workspaces() { return *waylandWorkspaces_; }
  [[nodiscard]] CompositorRuntimeRegistry& runtime_registry() { return runtimeRegistry_; }
  [[nodiscard]] ext_data_control_manager_v1* ext_data_control_manager() const { return extDataControlMgr_; }
  [[nodiscard]] zwlr_data_control_manager_v1* wlr_data_control_manager() const { return wlrDataControlMgr_; }
  [[nodiscard]] zwlr_screencopy_manager_v1* screencopy_manager() const { return screencopyMgr_; }
  [[nodiscard]] hyprland_toplevel_export_manager_v1* hyprland_toplevel_export_manager() const {
    return hyprlandToplevelExportMgr_;
  }
  [[nodiscard]] zwlr_gamma_control_manager_v1* gamma_control_manager() const { return gammaControlMgr_; }
  [[nodiscard]] wp_tearing_control_manager_v1* tearing_control_manager() const { return tearingControlMgr_; }
  [[nodiscard]] wl_data_device_manager* data_device_manager() const { return dataDeviceMgr_; }
  void set_hyprland_toplevel_mapping_manager_callback(
      std::function<void(struct hyprland_toplevel_mapping_manager_v1*)> cb) {
    hyprlandToplevelMappingMgrCb_ = std::move(cb);
  }
  [[nodiscard]] wp_single_pixel_buffer_manager_v1* single_pixel_buffer_manager() const { return singlePixelBufferMgr_; }
  [[nodiscard]] xdg_wm_dialog_v1* xdg_dialog_manager() const { return xdgDialogMgr_; }
  [[nodiscard]] zwp_pointer_constraints_v1* pointer_constraints() const { return pointerConstraints_; }
  [[nodiscard]] zwp_relative_pointer_manager_v1* relative_pointer_manager() const { return relativePointerMgr_; }
  [[nodiscard]] zwp_keyboard_shortcuts_inhibit_manager_v1* keyboard_shortcuts_inhibit_manager() const { return keyboardShortcutsInhibitMgr_; }
  [[nodiscard]] zwp_linux_dmabuf_v1* linux_dmabuf() const { return linuxDmabuf_; }
  [[nodiscard]] ext_image_copy_capture_manager_v1* ext_image_copy_capture_manager() const { return extImageCopyCaptureMgr_; }
  [[nodiscard]] ext_output_image_capture_source_manager_v1* ext_output_image_capture_source_manager() const { return extOutputImageCaptureSourceMgr_; }
  [[nodiscard]] ext_foreign_toplevel_image_capture_source_manager_v1* ext_foreign_toplevel_image_capture_source_manager() const { return extForeignToplevelImageCaptureSourceMgr_; }
  [[nodiscard]] wp_color_manager_v1* color_manager() const { return colorMgr_; }
  [[nodiscard]] bool has_core_shell_globals() const { return compositor_ && shm_; }
  [[nodiscard]] bool has_xdg_shell() const { return xdgBase_ != nullptr; }
  [[nodiscard]] bool has_layer_shell() const { return layerShell_ != nullptr; }
  [[nodiscard]] bool has_ext_foreign_toplevel_list() const { return extForeignToplevelList_ != nullptr; }
  [[nodiscard]] ext_foreign_toplevel_list_v1* ext_foreign_toplevel_list() const { return extForeignToplevelList_; }
  [[nodiscard]] ExtForeignToplevels& ext_foreign_toplevels() { return extForeignToplevels_; }

  [[nodiscard]] const std::vector<LogicalOutputInfo>& logical_outputs() const { return logical_outputs_; }
  [[nodiscard]] PickedLogicalOutput pick_largest_logical_output() const;

  [[nodiscard]] std::vector<LogicalOutputBounds> logical_output_bounds() const;

  [[nodiscard]] wl_output* output_by_name(std::string_view name) const;

  // Every bound wl_output global on this connection (not just "ready" ones).
  [[nodiscard]] std::vector<wl_output*> outputs() const;

  void refresh_logical_outputs();

  void sync_logical_outputs_from_cache();

  struct OutputSlot {
    WaylandConnection* owner = nullptr;
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
  static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version);
  static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);
  static constexpr wl_registry_listener kRegistryListener_ = {
      .global = registry_global,
      .global_remove = registry_global_remove,
  };

  wl_display* display_ = nullptr;
  bool owns_display_ = false;
  wl_seat* borrowed_seat_ = nullptr;
  wl_registry* registry_ = nullptr;

  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  wl_seat* seat_ = nullptr;
  wl_subcompositor* subcompositor_ = nullptr;

  xdg_wm_base* xdgBase_ = nullptr;
  zwlr_layer_shell_v1* layerShell_ = nullptr;
  zxdg_output_manager_v1* xdgOutputMgr_ = nullptr;
  xdg_activation_v1* activation_ = nullptr;
  zwlr_foreign_toplevel_manager_v1* foreignToplevelMgr_ = nullptr;
  ext_foreign_toplevel_list_v1* extForeignToplevelList_ = nullptr;
  ExtForeignToplevels extForeignToplevels_{};
  ext_session_lock_manager_v1* sessionLockMgr_ = nullptr;
  ext_idle_notifier_v1* idleNotifier_ = nullptr;
  zwp_idle_inhibit_manager_v1* idleInhibitMgr_ = nullptr;
  ext_background_effect_manager_v1* backgroundEffectMgr_ = nullptr;
  wp_fractional_scale_manager_v1* fractionalScaleMgr_ = nullptr;
  wp_viewporter* viewporter_ = nullptr;
  zwp_virtual_keyboard_manager_v1* virtualKeyboardMgr_ = nullptr;
  ext_workspace_manager_v1* workspaceMgr_ = nullptr;
  zdwl_ipc_manager_v2* dwlIpcMgr_ = nullptr;
  ext_data_control_manager_v1* extDataControlMgr_ = nullptr;
  zwlr_data_control_manager_v1* wlrDataControlMgr_ = nullptr;
  zwlr_screencopy_manager_v1* screencopyMgr_ = nullptr;
  hyprland_toplevel_export_manager_v1* hyprlandToplevelExportMgr_ = nullptr;
  zwlr_gamma_control_manager_v1* gammaControlMgr_ = nullptr;
  wp_tearing_control_manager_v1* tearingControlMgr_ = nullptr;
  wl_data_device_manager* dataDeviceMgr_ = nullptr;
  wp_single_pixel_buffer_manager_v1* singlePixelBufferMgr_ = nullptr;
  xdg_wm_dialog_v1* xdgDialogMgr_ = nullptr;
  zwp_pointer_constraints_v1* pointerConstraints_ = nullptr;
  zwp_relative_pointer_manager_v1* relativePointerMgr_ = nullptr;
  zwp_keyboard_shortcuts_inhibit_manager_v1* keyboardShortcutsInhibitMgr_ = nullptr;
  zwp_linux_dmabuf_v1* linuxDmabuf_ = nullptr;
  ext_image_copy_capture_manager_v1* extImageCopyCaptureMgr_ = nullptr;
  ext_output_image_capture_source_manager_v1* extOutputImageCaptureSourceMgr_ = nullptr;
  ext_foreign_toplevel_image_capture_source_manager_v1* extForeignToplevelImageCaptureSourceMgr_ = nullptr;
  wp_color_manager_v1* colorMgr_ = nullptr;
  CompositorRuntimeRegistry runtimeRegistry_;
  std::unique_ptr<WaylandWorkspaces> waylandWorkspaces_;

  std::vector<std::unique_ptr<OutputSlot>> tracked_outputs_;
  std::vector<LogicalOutputInfo> logical_outputs_;
  std::function<void(struct hyprland_toplevel_mapping_manager_v1*)> hyprlandToplevelMappingMgrCb_;

  void clear_tracked_outputs_();
  void rebuild_logical_snapshot_();
  void bind_xdg_for_tracked_();
};

}
