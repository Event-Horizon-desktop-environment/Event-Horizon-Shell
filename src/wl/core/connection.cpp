#include "wl/core/connection.hpp"

#include "backends/interfaces/compositor_ipc.h"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "wl/toplevel/workspaces.h"

#include "hyprland-toplevel-mapping-v1-client-protocol.h"
#include "hyprland-toplevel-export-v1-client-protocol.h"

#include "desktop_shell/common/log/debug_log.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using eh::wayland::WaylandConnection;

void conn_out_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                              int32_t) {}

static void conn_log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FILE* f = fopen("/tmp/eh-shot.log", "a");
  if (f) {
    fprintf(f, "[conn] ");
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    fclose(f);
  }
  va_end(ap);
}

void conn_out_mode(void* data, wl_output*, uint32_t, int32_t w, int32_t h, int32_t) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->mode_w = w;
  slot->mode_h = h;
}

void conn_out_done(void* data, wl_output*) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  conn_log("wl_output.done: name=%s mode=(%dx%d) logical=(%dx%d) pos=(%d,%d) xdg=%p ready=%d scale=%d",
           slot->name.c_str(), slot->mode_w, slot->mode_h,
           slot->logical_w, slot->logical_h,
           slot->logical_x, slot->logical_y,
           (void*)slot->xdg, (int)slot->ready, slot->scale);
  if (slot->logical_w > 0 && slot->logical_h > 0) {
    // The xdg path already set the size; only mark ready if we also have a
    // position or there's no xdg output to wait on.
    if (slot->xdg == nullptr || slot->logical_x != 0 || slot->logical_y != 0) {
      slot->ready = true;
      conn_log("  -> ready=true (xdg size+pos ok or no xdg)");
    } else {
      conn_log("  -> deferring ready (xdg bound but position not yet received)");
    }
  } else if (slot->mode_w > 0 && slot->mode_h > 0 && slot->xdg == nullptr) {
    // No xdg manager available, so fall back to the mode-derived size.
    slot->logical_w = slot->mode_w / slot->scale;
    slot->logical_h = slot->mode_h / slot->scale;
    slot->ready = true;
    conn_log("  -> ready=true (mode fallback, no xdg) logical=(%dx%d)", slot->logical_w, slot->logical_h);
  } else {
    conn_log("  -> not ready yet (insufficient data)");
  }
}

void conn_out_scale(void* data, wl_output*, int32_t factor) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  if (factor > 0) slot->scale = factor;
}

void conn_out_name(void* data, wl_output*, const char* name) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  if (name && name[0]) slot->name = name;
}

void conn_out_description(void*, wl_output*, const char*) {}

static const wl_output_listener kConnWlOutputListener = {
    .geometry = conn_out_geometry,
    .mode = conn_out_mode,
    .done = conn_out_done,
    .scale = conn_out_scale,
    .name = conn_out_name,
    .description = conn_out_description,
};

void conn_xdg_logical_position(void* data, zxdg_output_v1*, int32_t x, int32_t y) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  conn_log("xdg.logical_position: name=%s x=%d y=%d (was %d,%d)",
           slot->name.c_str(), x, y, slot->logical_x, slot->logical_y);
  slot->logical_x = x;
  slot->logical_y = y;
}

void conn_xdg_logical_size(void* data, zxdg_output_v1*, int32_t w, int32_t h) {
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  conn_log("xdg.logical_size: name=%s w=%d h=%d (was %d,%d) ready_before=%d",
           slot->name.c_str(), w, h, slot->logical_w, slot->logical_h, (int)slot->ready);
  slot->logical_w = w;
  slot->logical_h = h;
  if (w > 0 && h > 0) {
    slot->ready = true;
    conn_log("  -> ready=true (xdg size)");
  }
}

void conn_xdg_name(void*, zxdg_output_v1*, const char*) {}

void conn_xdg_description(void*, zxdg_output_v1*, const char*) {}

void conn_xdg_done_deprecated(void*, zxdg_output_v1*) {}

static const zxdg_output_v1_listener kConnXdgOutputListener = {
    .logical_position = conn_xdg_logical_position,
    .logical_size = conn_xdg_logical_size,
    .done = conn_xdg_done_deprecated,
    .name = conn_xdg_name,
    .description = conn_xdg_description,
};

}

namespace eh::wayland {

WaylandConnection::WaylandConnection()
    : waylandWorkspaces_(std::make_unique<WaylandWorkspaces>(runtimeRegistry_)) {
  MANGOWM_DEBUG("WaylandConnection ctor this=%p", (void*)this);
}

WaylandConnection::~WaylandConnection() {
  MANGOWM_DEBUG("WaylandConnection dtor this=%p", (void*)this);
  disconnect();
}

void WaylandConnection::clear_tracked_outputs_() {
  for (auto& t : tracked_outputs_) {
    if (!t) continue;
    if (t->xdg) {
      zxdg_output_v1_destroy(t->xdg);
      t->xdg = nullptr;
    }
    if (t->output) {
      wl_output_destroy(t->output);
      t->output = nullptr;
    }
  }
  tracked_outputs_.clear();
  tracked_outputs_.shrink_to_fit();
  logical_outputs_.clear();
  logical_outputs_.shrink_to_fit();
}

void WaylandConnection::bind_xdg_for_tracked_() {
  if (!xdgOutputMgr_) return;
  for (auto& t : tracked_outputs_) {
    if (!t || t->xdg || !t->output) continue;
    t->xdg = zxdg_output_manager_v1_get_xdg_output(xdgOutputMgr_, t->output);
    if (t->xdg) zxdg_output_v1_add_listener(t->xdg, &kConnXdgOutputListener, t.get());
  }
}

void WaylandConnection::rebuild_logical_snapshot_() {
  logical_outputs_.clear();
  std::vector<const OutputSlot*> tmp;
  tmp.reserve(tracked_outputs_.size());
  for (const auto& t : tracked_outputs_) {
    if (!t || !t->ready || t->logical_w <= 0 || t->logical_h <= 0 || t->name.empty()) continue;
    tmp.push_back(t.get());
  }
  std::sort(tmp.begin(), tmp.end(), [](const OutputSlot* a, const OutputSlot* b) {
    if (a->logical_x != b->logical_x) return a->logical_x < b->logical_x;
    if (a->logical_y != b->logical_y) return a->logical_y < b->logical_y;
    return a->name < b->name;
  });
  for (const OutputSlot* t : tmp) {
    logical_outputs_.emplace_back(t->name, t->logical_w, t->logical_h);
  }
}

void WaylandConnection::refresh_logical_outputs() {
  conn_log("refresh_logical_outputs: enter xdgOutputMgr=%p", (void*)xdgOutputMgr_);
  if (!display_) { conn_log("refresh_logical_outputs: no display"); return; }
  (void)wl_display_roundtrip(display_);
  (void)wl_display_roundtrip(display_);
  if (xdgOutputMgr_) {
    conn_log("refresh_logical_outputs: third roundtrip for xdg");
    (void)wl_display_roundtrip(display_);
  }
  rebuild_logical_snapshot_();
  conn_log("refresh_logical_outputs: %zu logical outputs after rebuild", logical_outputs_.size());
}

void WaylandConnection::sync_logical_outputs_from_cache() { rebuild_logical_snapshot_(); }

wl_output* WaylandConnection::output_by_name(std::string_view name) const {
  for (const auto& t : tracked_outputs_) {
    if (!t || !t->output || !t->ready || t->name.empty()) continue;
    if (name == t->name) return t->output;
  }
  return nullptr;
}

std::vector<LogicalOutputBounds> WaylandConnection::logical_output_bounds() const {
  std::vector<LogicalOutputBounds> out;
  out.reserve(tracked_outputs_.size());
  for (const auto& t : tracked_outputs_) {
    if (!t || !t->output || !t->ready || t->logical_w <= 0 || t->logical_h <= 0) continue;
    LogicalOutputBounds b{};
    b.output = t->output;
    b.name = t->name;
    b.global_x = t->logical_x;
    b.global_y = t->logical_y;
    b.width = t->logical_w;
    b.height = t->logical_h;
    out.push_back(std::move(b));
  }
  return out;
}

std::vector<wl_output*> WaylandConnection::outputs() const {
  std::vector<wl_output*> out;
  out.reserve(tracked_outputs_.size());
  for (const auto& t : tracked_outputs_)
    if (t && t->output) out.push_back(t->output);
  return out;
}

PickedLogicalOutput WaylandConnection::pick_largest_logical_output() const {
  PickedLogicalOutput r{};
  int best_area = 0;
  for (const auto& t : tracked_outputs_) {
    if (!t || !t->ready || !t->output) continue;
    if (t->logical_w <= 0 || t->logical_h <= 0) continue;
    const int area = t->logical_w * t->logical_h;
    if (area > best_area) {
      best_area = area;
      r.output = t->output;
      r.logical_width = t->logical_w;
      r.logical_height = t->logical_h;
    }
  }
  return r;
}

bool WaylandConnection::connect(bool init_workspaces) {
   
  MANGOWM_INFO("WaylandConnection::connect this=%p init_ws=%d", (void*)this, (int)init_workspaces);
  if (display_) {
    MANGOWM_DEBUG("already connected");
    return true;
  }

  display_ = wl_display_connect(nullptr);
  if (!display_) {
    MANGOWM_ERROR("wl_display_connect failed");
    return false;
  }
  owns_display_ = true;
  MANGOWM_DEBUG("display=%p owns_display=1", (void*)display_);

  registry_ = wl_display_get_registry(display_);
  if (!registry_) {
    MANGOWM_ERROR("wl_display_get_registry failed");
    return false;
  }

  conn_log("connect: registry listener added, starting roundtrips");
  wl_registry_add_listener(registry_, &kRegistryListener_, this);
  wl_display_roundtrip(display_);
  conn_log("connect: roundtrip #1 done, xdgOutputMgr=%p", (void*)xdgOutputMgr_);
  wl_display_roundtrip(display_);
  conn_log("connect: roundtrip #2 done");
  if (xdgOutputMgr_) {
    conn_log("connect: roundtrip #3 (xdg flush)");
    wl_display_roundtrip(display_);
    conn_log("connect: roundtrip #3 done");
  }
  rebuild_logical_snapshot_();
  if (init_workspaces) {
    waylandWorkspaces_->initialize();
    set_global_wayland_workspaces(waylandWorkspaces_.get());
  }
  MANGOWM_INFO("connect complete: compositor=%d shm=%d seat=%d xdg=%d layer=%d",
    (int)(compositor_ != nullptr), (int)(shm_ != nullptr), (int)(seat_ != nullptr),
    (int)has_xdg_shell(), (int)(layerShell_ != nullptr));
  return true;
}

bool WaylandConnection::attach(wl_display* display, wl_seat* borrowed_seat, bool init_workspaces) {
   
  MANGOWM_INFO("WaylandConnection::attach this=%p display=%p seat=%p init_ws=%d",
    (void*)this, (void*)display, (void*)borrowed_seat, (int)init_workspaces);
  debug_log("settings", "WaylandConnection::attach: enter display=%p seat=%p init_ws=%d",
            (void*)display, (void*)borrowed_seat, (int)init_workspaces);
  if (!display) {
    MANGOWM_ERROR("attach: null display");
    debug_log("settings", "WaylandConnection::attach FAIL: null display");
    return false;
  }
  if (display_) {
    bool ok = display_ == display;
    MANGOWM_INFO("attach: already attached (display_=%p match=%d)", (void*)display_, (int)ok);
    debug_log("settings", "WaylandConnection::attach: already attached (display_=%p match=%d)",
              (void*)display_, (int)ok);
    return ok;
  }

  display_ = display;
  owns_display_ = false;
  borrowed_seat_ = borrowed_seat;
  if (borrowed_seat_) seat_ = borrowed_seat;

  registry_ = wl_display_get_registry(display_);
  if (!registry_) {
    MANGOWM_ERROR("attach: no registry");
    debug_log("settings", "WaylandConnection::attach: FAIL: no registry");
    return false;
  }
  MANGOWM_DEBUG("attach: registry=%p roundtrip #1", (void*)registry_);
  debug_log("settings", "WaylandConnection::attach: registry=%p, roundtrip #1", (void*)registry_);

  conn_log("attach: registry listener added, starting roundtrips");
  wl_registry_add_listener(registry_, &kRegistryListener_, this);
  wl_display_roundtrip(display_);
  MANGOWM_DEBUG("roundtrip #1 done");
  debug_log("settings", "WaylandConnection::attach: roundtrip #1 done");
  conn_log("attach: roundtrip #1 done, xdgOutputMgr=%p", (void*)xdgOutputMgr_);
  wl_display_roundtrip(display_);
  MANGOWM_DEBUG("roundtrip #2 done");
  debug_log("settings", "WaylandConnection::attach: roundtrip #2 done");
  conn_log("attach: roundtrip #2 done");
  if (xdgOutputMgr_) {
    conn_log("attach: roundtrip #3 (xdg flush)");
    wl_display_roundtrip(display_);
    MANGOWM_DEBUG("roundtrip #3 done (xdg)");
    debug_log("settings", "WaylandConnection::attach: roundtrip #3 (xdg flush)");
    conn_log("attach: roundtrip #3 done");
  }
  rebuild_logical_snapshot_();
  if (init_workspaces) {
    waylandWorkspaces_->initialize();
    set_global_wayland_workspaces(waylandWorkspaces_.get());
  }
  bool result = compositor_ && shm_ && has_xdg_shell();
  MANGOWM_INFO("attach result=%d compositor=%d shm=%d xdg=%d",
    (int)result, (int)(compositor_ != nullptr), (int)(shm_ != nullptr), (int)has_xdg_shell());
  debug_log("settings", "WaylandConnection::attach: result=%d compositor=%d shm=%d xdg=%d",
            (int)result, (int)(compositor_ != nullptr), (int)(shm_ != nullptr), (int)has_xdg_shell());
  return result;
}

void WaylandConnection::disconnect() {
  // Touching any Wayland proxy while the display is in a fatal error state
  // segfaults, so skip all Wayland cleanup — the OS closes the connection.
  if (display_ && wl_display_get_error(display_)) {
    display_ = nullptr;
    return;
  }

  set_global_wayland_workspaces(nullptr);
  waylandWorkspaces_->teardown();
  // The backend takes ownership and frees workspaceMgr_ + dwlIpcMgr_.
  workspaceMgr_ = nullptr;
  dwlIpcMgr_ = nullptr;
  clear_tracked_outputs_();
  if (screencopyMgr_) {
    zwlr_screencopy_manager_v1_destroy(screencopyMgr_);
    screencopyMgr_ = nullptr;
  }
  if (gammaControlMgr_) {
    zwlr_gamma_control_manager_v1_destroy(gammaControlMgr_);
    gammaControlMgr_ = nullptr;
  }
  if (tearingControlMgr_) {
    wp_tearing_control_manager_v1_destroy(tearingControlMgr_);
    tearingControlMgr_ = nullptr;
  }
  if (dataDeviceMgr_) {
    wl_data_device_manager_destroy(dataDeviceMgr_);
    dataDeviceMgr_ = nullptr;
  }
  if (wlrDataControlMgr_) {
    zwlr_data_control_manager_v1_destroy(wlrDataControlMgr_);
    wlrDataControlMgr_ = nullptr;
  }
  if (extDataControlMgr_) {
    ext_data_control_manager_v1_destroy(extDataControlMgr_);
    extDataControlMgr_ = nullptr;
  }
  if (workspaceMgr_) {
    ext_workspace_manager_v1_destroy(workspaceMgr_);
    workspaceMgr_ = nullptr;
  }
  if (virtualKeyboardMgr_) {
    zwp_virtual_keyboard_manager_v1_destroy(virtualKeyboardMgr_);
    virtualKeyboardMgr_ = nullptr;
  }
  if (viewporter_) {
    wp_viewporter_destroy(viewporter_);
    viewporter_ = nullptr;
  }
  if (fractionalScaleMgr_) {
    wp_fractional_scale_manager_v1_destroy(fractionalScaleMgr_);
    fractionalScaleMgr_ = nullptr;
  }
  if (backgroundEffectMgr_) {
    ext_background_effect_manager_v1_destroy(backgroundEffectMgr_);
    backgroundEffectMgr_ = nullptr;
  }
  if (idleInhibitMgr_) {
    zwp_idle_inhibit_manager_v1_destroy(idleInhibitMgr_);
    idleInhibitMgr_ = nullptr;
  }
  if (idleNotifier_) {
    ext_idle_notifier_v1_destroy(idleNotifier_);
    idleNotifier_ = nullptr;
  }
  if (sessionLockMgr_) {
    ext_session_lock_manager_v1_destroy(sessionLockMgr_);
    sessionLockMgr_ = nullptr;
  }
  if (colorMgr_) {
    wp_color_manager_v1_destroy(colorMgr_);
    colorMgr_ = nullptr;
  }
  if (extForeignToplevelImageCaptureSourceMgr_) {
    ext_foreign_toplevel_image_capture_source_manager_v1_destroy(extForeignToplevelImageCaptureSourceMgr_);
    extForeignToplevelImageCaptureSourceMgr_ = nullptr;
  }
  if (extOutputImageCaptureSourceMgr_) {
    ext_output_image_capture_source_manager_v1_destroy(extOutputImageCaptureSourceMgr_);
    extOutputImageCaptureSourceMgr_ = nullptr;
  }
  if (extImageCopyCaptureMgr_) {
    ext_image_copy_capture_manager_v1_destroy(extImageCopyCaptureMgr_);
    extImageCopyCaptureMgr_ = nullptr;
  }
  if (hyprlandToplevelExportMgr_) {
    hyprland_toplevel_export_manager_v1_destroy(hyprlandToplevelExportMgr_);
    hyprlandToplevelExportMgr_ = nullptr;
  }
  if (linuxDmabuf_) {
    zwp_linux_dmabuf_v1_destroy(linuxDmabuf_);
    linuxDmabuf_ = nullptr;
  }
  if (keyboardShortcutsInhibitMgr_) {
    zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(keyboardShortcutsInhibitMgr_);
    keyboardShortcutsInhibitMgr_ = nullptr;
  }
  if (relativePointerMgr_) {
    zwp_relative_pointer_manager_v1_destroy(relativePointerMgr_);
    relativePointerMgr_ = nullptr;
  }
  if (pointerConstraints_) {
    zwp_pointer_constraints_v1_destroy(pointerConstraints_);
    pointerConstraints_ = nullptr;
  }
  if (xdgDialogMgr_) {
    xdg_wm_dialog_v1_destroy(xdgDialogMgr_);
    xdgDialogMgr_ = nullptr;
  }
  if (singlePixelBufferMgr_) {
    wp_single_pixel_buffer_manager_v1_destroy(singlePixelBufferMgr_);
    singlePixelBufferMgr_ = nullptr;
  }
  extForeignToplevels_.shutdown();
  extForeignToplevelList_ = nullptr;
  if (foreignToplevelMgr_) {
    zwlr_foreign_toplevel_manager_v1_destroy(foreignToplevelMgr_);
    foreignToplevelMgr_ = nullptr;
  }
  if (activation_) {
    xdg_activation_v1_destroy(activation_);
    activation_ = nullptr;
  }
  if (xdgOutputMgr_) {
    zxdg_output_manager_v1_destroy(xdgOutputMgr_);
    xdgOutputMgr_ = nullptr;
  }
  if (layerShell_) {
    zwlr_layer_shell_v1_destroy(layerShell_);
    layerShell_ = nullptr;
  }
  if (subcompositor_) {
    wl_subcompositor_destroy(subcompositor_);
    subcompositor_ = nullptr;
  }
  if (xdgBase_) {
    xdg_wm_base_destroy(xdgBase_);
    xdgBase_ = nullptr;
  }
  if (seat_) {
    if (!borrowed_seat_) wl_seat_destroy(seat_);
    seat_ = nullptr;
  }
  borrowed_seat_ = nullptr;
  if (shm_) {
    wl_shm_destroy(shm_);
    shm_ = nullptr;
  }
  if (compositor_) {
    wl_compositor_destroy(compositor_);
    compositor_ = nullptr;
  }
  if (registry_) {
    wl_registry_destroy(registry_);
    registry_ = nullptr;
  }
  if (owns_display_ && display_) {
    wl_display_disconnect(display_);
  }
  display_ = nullptr;
  owns_display_ = false;
}

void WaylandConnection::registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface,
                                       uint32_t version) {
  auto& self = *static_cast<WaylandConnection*>(data);
  const std::string_view ifc(iface ? iface : "");

  if (ifc == wl_compositor_interface.name) {
    self.compositor_ = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min<uint32_t>(version, 4)));
    return;
  }
  if (ifc == wl_subcompositor_interface.name) {
    self.subcompositor_ = static_cast<wl_subcompositor*>(
        wl_registry_bind(registry, name, &wl_subcompositor_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == wl_shm_interface.name) {
    self.shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    return;
  }
  if (ifc == wl_seat_interface.name) {
    if (!self.seat_) {
      self.seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min<uint32_t>(version, 5)));
    }
    return;
  }
  if (ifc == xdg_wm_base_interface.name) {
    self.xdgBase_ = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min<uint32_t>(version, 5)));
    return;
  }
  if (ifc == zwlr_layer_shell_v1_interface.name) {
    self.layerShell_ = static_cast<zwlr_layer_shell_v1*>(
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min<uint32_t>(version, 4)));
    return;
  }
  if (ifc == zxdg_output_manager_v1_interface.name) {
    self.xdgOutputMgr_ = static_cast<zxdg_output_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, std::min<uint32_t>(version, 3)));
    self.bind_xdg_for_tracked_();
    return;
  }
  if (ifc == wl_output_interface.name) {
    auto slot = std::make_unique<WaylandConnection::OutputSlot>();
    slot->owner = &self;
    slot->output = static_cast<wl_output*>(
        wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(version, 4)));
    wl_output_add_listener(slot->output, &kConnWlOutputListener, slot.get());
    wl_output* out = slot->output;
    self.tracked_outputs_.push_back(std::move(slot));
    self.bind_xdg_for_tracked_();
    if (self.dwlIpcMgr_) self.waylandWorkspaces_->outputAttached(out);
    return;
  }
  if (ifc == xdg_activation_v1_interface.name) {
    self.activation_ = static_cast<xdg_activation_v1*>(
        wl_registry_bind(registry, name, &xdg_activation_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwlr_foreign_toplevel_manager_v1_interface.name) {
    self.foreignToplevelMgr_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<uint32_t>(version, 3)));
    return;
  }
  if (ifc == ext_foreign_toplevel_list_v1_interface.name) {
    // ext-foreign-toplevel-list-v1 mirrors zwlr-foreign-toplevel-manager, so
    // bind it only where the compositor lacks the standard manager.
    if (!is_hyprland()) return;
    self.extForeignToplevelList_ = static_cast<ext_foreign_toplevel_list_v1*>(
        wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, std::min<uint32_t>(version, 1)));
    self.extForeignToplevels_.bind(self.extForeignToplevelList_, self.display_);
    return;
  }
  if (ifc == ext_session_lock_manager_v1_interface.name) {
    self.sessionLockMgr_ = static_cast<ext_session_lock_manager_v1*>(
        wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == ext_idle_notifier_v1_interface.name) {
    self.idleNotifier_ = static_cast<ext_idle_notifier_v1*>(
        wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwp_idle_inhibit_manager_v1_interface.name) {
    self.idleInhibitMgr_ = static_cast<zwp_idle_inhibit_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == ext_background_effect_manager_v1_interface.name) {
    self.backgroundEffectMgr_ = static_cast<ext_background_effect_manager_v1*>(
        wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == hyprland_toplevel_mapping_manager_v1_interface.name) {
    const auto bindVersion = std::min<uint32_t>(version, 1);
    auto* manager = static_cast<hyprland_toplevel_mapping_manager_v1*>(
        wl_registry_bind(registry, name, &hyprland_toplevel_mapping_manager_v1_interface, bindVersion));
    if (self.hyprlandToplevelMappingMgrCb_) {
      self.hyprlandToplevelMappingMgrCb_(manager);
    } else {
      hyprland_toplevel_mapping_manager_v1_destroy(manager);
    }
    return;
  }
  if (ifc == hyprland_toplevel_export_manager_v1_interface.name) {
    self.hyprlandToplevelExportMgr_ = static_cast<hyprland_toplevel_export_manager_v1*>(
        wl_registry_bind(registry, name, &hyprland_toplevel_export_manager_v1_interface, std::min<uint32_t>(version, 2)));
    return;
  }
  if (ifc == wp_fractional_scale_manager_v1_interface.name) {
    self.fractionalScaleMgr_ = static_cast<wp_fractional_scale_manager_v1*>(
        wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == wp_viewporter_interface.name) {
    self.viewporter_ = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwp_virtual_keyboard_manager_v1_interface.name) {
    self.virtualKeyboardMgr_ = static_cast<zwp_virtual_keyboard_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == ext_workspace_manager_v1_interface.name) {
    self.workspaceMgr_ = static_cast<ext_workspace_manager_v1*>(
        wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, std::min<uint32_t>(version, 1)));
    if (self.waylandWorkspaces_) {
      self.waylandWorkspaces_->bindExtProtocol(self.workspaceMgr_);
    }
    return;
  }
  if (ifc == zdwl_ipc_manager_v2_interface.name) {
    self.dwlIpcMgr_ = static_cast<zdwl_ipc_manager_v2*>(
        wl_registry_bind(registry, name, &zdwl_ipc_manager_v2_interface, std::min<uint32_t>(version, 2)));
    // Keep dwlIpcMgr_ so outputAttached can be tracked later.
    return;
  }
  if (ifc == ext_data_control_manager_v1_interface.name) {
    self.extDataControlMgr_ = static_cast<ext_data_control_manager_v1*>(
        wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwlr_data_control_manager_v1_interface.name) {
    self.wlrDataControlMgr_ = static_cast<zwlr_data_control_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, std::min<uint32_t>(version, 2)));
    return;
  }
  if (ifc == zwlr_gamma_control_manager_v1_interface.name) {
    self.gammaControlMgr_ = static_cast<zwlr_gamma_control_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_gamma_control_manager_v1_interface, 1));
    return;
  }

  if (ifc == zwlr_screencopy_manager_v1_interface.name) {

    self.screencopyMgr_ = static_cast<zwlr_screencopy_manager_v1*>(wl_registry_bind(
        registry, name, &zwlr_screencopy_manager_v1_interface, std::min<uint32_t>(version, 3)));
    return;
  }
  if (ifc == wp_tearing_control_manager_v1_interface.name) {
    self.tearingControlMgr_ = static_cast<wp_tearing_control_manager_v1*>(
        wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1));
    return;
  }
  if (ifc == wl_data_device_manager_interface.name) {
    self.dataDeviceMgr_ = static_cast<wl_data_device_manager*>(
        wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min<uint32_t>(version, 3)));
    return;
  }
  if (ifc == wp_single_pixel_buffer_manager_v1_interface.name) {
    self.singlePixelBufferMgr_ = static_cast<wp_single_pixel_buffer_manager_v1*>(
        wl_registry_bind(registry, name, &wp_single_pixel_buffer_manager_v1_interface, 1));
    return;
  }
  if (ifc == xdg_wm_dialog_v1_interface.name) {
    self.xdgDialogMgr_ = static_cast<xdg_wm_dialog_v1*>(
        wl_registry_bind(registry, name, &xdg_wm_dialog_v1_interface, 1));
    return;
  }
  if (ifc == zwp_pointer_constraints_v1_interface.name) {
    self.pointerConstraints_ = static_cast<zwp_pointer_constraints_v1*>(
        wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwp_relative_pointer_manager_v1_interface.name) {
    self.relativePointerMgr_ = static_cast<zwp_relative_pointer_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, std::min<uint32_t>(version, 1)));
    return;
  }
  if (ifc == zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) {
    self.keyboardShortcutsInhibitMgr_ = static_cast<zwp_keyboard_shortcuts_inhibit_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1));
    return;
  }
  if (ifc == zwp_linux_dmabuf_v1_interface.name) {
    self.linuxDmabuf_ = static_cast<zwp_linux_dmabuf_v1*>(
        wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, std::min<uint32_t>(version, 4)));
    return;
  }
  if (ifc == ext_output_image_capture_source_manager_v1_interface.name) {
    self.extOutputImageCaptureSourceMgr_ = static_cast<ext_output_image_capture_source_manager_v1*>(
        wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1));
    return;
  }
  if (ifc == ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) {
    self.extForeignToplevelImageCaptureSourceMgr_ = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(
        wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    return;
  }
  if (ifc == ext_image_copy_capture_manager_v1_interface.name) {
    self.extImageCopyCaptureMgr_ = static_cast<ext_image_copy_capture_manager_v1*>(
        wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    return;
  }
  if (ifc == wp_color_manager_v1_interface.name) {
    self.colorMgr_ = static_cast<wp_color_manager_v1*>(
        wl_registry_bind(registry, name, &wp_color_manager_v1_interface, std::min<uint32_t>(version, 2)));
    return;
  }
}

void WaylandConnection::registry_global_remove(void* data, wl_registry*  , uint32_t  ) {
  auto& self = *static_cast<WaylandConnection*>(data);
  (void)self;
}

}
