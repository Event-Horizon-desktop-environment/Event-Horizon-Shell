#include "core/wayland_connection.hpp"
#include "core/logging.hpp"

#include <wayland-client.h>
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <iostream>

#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "color-management-v1-client-protocol.h"
#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace hs::core {

static void xdg_wm_base_ping(void*, xdg_wm_base* wm, uint32_t serial)
{
  xdg_wm_base_pong(wm, serial);
}

static constexpr xdg_wm_base_listener kXdgWmBaseListener{
  .ping = xdg_wm_base_ping,
};

WaylandConnection::~WaylandConnection() { disconnect(); }

bool WaylandConnection::connect()
{
  display_ = wl_display_connect(nullptr);
  if (!display_) { HS_LOG("connect: wl_display_connect failed"); return false; }
  HS_LOG("connect: connected to display");

  registry_ = wl_display_get_registry(display_);
  if (!registry_) {
    HS_LOG("connect: wl_display_get_registry failed");
    wl_display_disconnect(display_);
    display_ = nullptr;
    return false;
  }

  wl_registry_add_listener(registry_, &kRegistryListener_, this);
  wl_display_roundtrip(display_);
  wl_display_roundtrip(display_);

  HS_LOG("connect: compositor=%p shm=%p seat=%p layer_shell=%p screencopy_mgr=%p ext_cc_mgr=%p",
         (void*)compositor_, (void*)shm_, (void*)seat_, (void*)layerShell_,
         (void*)screencopyMgr_, (void*)extImageCopyCaptureMgr_);
  HS_LOG("connect: xdg_output_mgr=%p ext_output_src=%p ext_toplevel_src=%p color_mgr=%p",
         (void*)xdgOutputMgr_, (void*)extOutputImageCaptureSourceMgr_,
         (void*)extForeignToplevelImageCaptureSourceMgr_, (void*)colorMgr_);
  HS_LOG("connect: ext_data_ctrl=%p wlr_data_ctrl=%p linux_dmabuf=%p ext_toplevel_list=%p wlr_toplevel_mgr=%p",
         (void*)extDataControlMgr_, (void*)wlrDataControlMgr_,
         (void*)linuxDmabuf_, (void*)extForeignToplevelList_, (void*)wlrForeignToplevelMgr_);
  HS_LOG("connect: %zu tracked outputs, xwayland=%d", tracked_outputs_.size(), hasXWayland_);

  return true;
}

void WaylandConnection::disconnect()
{
  HS_LOG("disconnect");
  extForeignToplevels_.shutdown();
  extForeignToplevelList_ = nullptr;
  wlrForeignToplevels_.shutdown();
  wlrForeignToplevelMgr_ = nullptr;

  for (auto& slot : tracked_outputs_) {
    if (slot->xdg) zxdg_output_v1_destroy(slot->xdg);
    slot->xdg = nullptr;
  }
  tracked_outputs_.clear();
  if (linuxDmabuf_) zwp_linux_dmabuf_v1_destroy(linuxDmabuf_);
  if (wlrDataControlMgr_) zwlr_data_control_manager_v1_destroy(wlrDataControlMgr_);
  if (extDataControlMgr_) ext_data_control_manager_v1_destroy(extDataControlMgr_);
  if (colorMgr_) wp_color_manager_v1_destroy(colorMgr_);
  if (extForeignToplevelImageCaptureSourceMgr_) ext_foreign_toplevel_image_capture_source_manager_v1_destroy(extForeignToplevelImageCaptureSourceMgr_);
  if (extOutputImageCaptureSourceMgr_) ext_output_image_capture_source_manager_v1_destroy(extOutputImageCaptureSourceMgr_);
  if (extImageCopyCaptureMgr_) ext_image_copy_capture_manager_v1_destroy(extImageCopyCaptureMgr_);
  if (screencopyMgr_) zwlr_screencopy_manager_v1_destroy(screencopyMgr_);
  if (xdgOutputMgr_) zxdg_output_manager_v1_destroy(xdgOutputMgr_);
  if (layerShell_) zwlr_layer_shell_v1_destroy(layerShell_);
  if (xdgBase_) xdg_wm_base_destroy(xdgBase_);
  if (seat_) {}
  if (shm_) wl_shm_destroy(shm_);
  if (compositor_) wl_compositor_destroy(compositor_);
  if (registry_) wl_registry_destroy(registry_);
  if (display_) wl_display_disconnect(display_);

  display_ = nullptr;
  registry_ = nullptr;
  compositor_ = nullptr;
  shm_ = nullptr;
  seat_ = nullptr;
  xdgBase_ = nullptr;
  layerShell_ = nullptr;
  xdgOutputMgr_ = nullptr;
  screencopyMgr_ = nullptr;
  extImageCopyCaptureMgr_ = nullptr;
  extOutputImageCaptureSourceMgr_ = nullptr;
  extForeignToplevelImageCaptureSourceMgr_ = nullptr;
  colorMgr_ = nullptr;
  extDataControlMgr_ = nullptr;
  wlrDataControlMgr_ = nullptr;
  linuxDmabuf_ = nullptr;
  extForeignToplevelList_ = nullptr;
  wlrForeignToplevelMgr_ = nullptr;
  hasXWayland_ = false;
}

static void xdg_output_name(void* data, struct zxdg_output_v1*, const char* name)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->name = name ? name : "";
}

static void xdg_output_logical_position(void* data, struct zxdg_output_v1*, int32_t x, int32_t y)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->logical_x = x;
  slot->logical_y = y;
}

static void xdg_output_logical_size(void* data, struct zxdg_output_v1*, int32_t w, int32_t h)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->logical_w = w;
  slot->logical_h = h;
}

static void xdg_output_done(void* data, struct zxdg_output_v1*)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->ready = true;
}

static constexpr zxdg_output_v1_listener kXdgOutputListener = {
  .logical_position = xdg_output_logical_position,
  .logical_size = xdg_output_logical_size,
  .done = xdg_output_done,
  .name = xdg_output_name,
  .description = [](void*, zxdg_output_v1*, const char*) {},
};

void WaylandConnection::bind_xdg_for_tracked_()
{
  HS_LOG("bind_xdg_for_tracked_: xdgOutputMgr=%p tracked=%zu", (void*)xdgOutputMgr_, tracked_outputs_.size());
  if (!xdgOutputMgr_) return;
  for (auto& slot : tracked_outputs_) {
    if (slot->output && !slot->xdg) {
      slot->xdg = zxdg_output_manager_v1_get_xdg_output(xdgOutputMgr_, slot->output);
      if (slot->xdg) {
        zxdg_output_v1_add_listener(slot->xdg, &kXdgOutputListener, slot.get());
      }
    }
  }
}

void WaylandConnection::refresh_logical_outputs()
{
  HS_LOG("refresh_logical_outputs: %zu tracked outputs, xdgMgr=%p",
         tracked_outputs_.size(), (void*)xdgOutputMgr_);
  bind_xdg_for_tracked_();
  if (display_) {
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);
  }
  for (const auto& slot : tracked_outputs_) {
    HS_LOG("  output slot: name='%s' logical=(%d,%d %dx%d) ready=%d",
           slot->name.c_str(), slot->logical_x, slot->logical_y,
           slot->logical_w, slot->logical_h, slot->ready);
  }
}

std::vector<LogicalOutputBounds> WaylandConnection::logical_output_bounds() const
{
  std::vector<LogicalOutputBounds> result;
  for (const auto& slot : tracked_outputs_) {
    if (!slot->ready || !slot->output) {
      HS_LOG("logical_output_bounds: skipping slot (ready=%d output=%p)", slot->ready, (void*)slot->output);
      continue;
    }
    LogicalOutputBounds b;
    b.output = slot->output;
    b.name = slot->name;
    b.global_x = slot->logical_x;
    b.global_y = slot->logical_y;
    b.width = slot->logical_w;
    b.height = slot->logical_h;
    HS_LOG("logical_output_bounds: name='%s' @(%d,%d) %dx%d", slot->name.c_str(), b.global_x, b.global_y, b.width, b.height);
    result.push_back(std::move(b));
  }
  HS_LOG("logical_output_bounds: %zu results returned", result.size());
  return result;
}

PickedLogicalOutput WaylandConnection::pick_largest_logical_output() const
{
  HS_LOG("pick_largest_logical_output: %zu tracked outputs", tracked_outputs_.size());
  PickedLogicalOutput best{};
  int bestArea = 0;
  for (const auto& slot : tracked_outputs_) {
    if (!slot->ready || !slot->output) continue;
    int area = slot->logical_w * slot->logical_h;
    if (area > bestArea) {
      best.output = slot->output;
      best.logical_width = slot->logical_w;
      best.logical_height = slot->logical_h;
      bestArea = area;
    }
  }
  return best;
}

static void output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
static void output_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}

static void output_done(void* data, wl_output*)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  slot->ready = true;
}
static void output_scale(void*, wl_output*, int32_t) {}
static void output_name(void* data, wl_output*, const char* name)
{
  auto* slot = static_cast<WaylandConnection::OutputSlot*>(data);
  if (name) slot->name = name;
}

static constexpr wl_output_listener kOutputListener = {
  .geometry = output_geometry,
  .mode = output_mode,
  .done = output_done,
  .scale = output_scale,
  .name = output_name,
  .description = [](void*, wl_output*, const char*) {},
};

void WaylandConnection::registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version)
{
  auto& self = *static_cast<WaylandConnection*>(data);
  HS_LOG("registry_global: name=%u iface=%s version=%u", name, iface, version);

  if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
    self.compositor_ = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
    self.shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1u)));
  } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
    self.seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
  } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
    self.xdgBase_ = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 7u)));
    xdg_wm_base_add_listener(self.xdgBase_, &kXdgWmBaseListener, nullptr);
  } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
    self.layerShell_ = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
  } else if (std::strcmp(iface, zxdg_output_manager_v1_interface.name) == 0) {
    self.xdgOutputMgr_ = static_cast<zxdg_output_manager_v1*>(wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, std::min(version, 3u)));
  } else if (std::strcmp(iface, zwlr_screencopy_manager_v1_interface.name) == 0) {
    self.screencopyMgr_ = static_cast<zwlr_screencopy_manager_v1*>(wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, std::min(version, 3u)));
  } else if (std::strcmp(iface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
    self.extImageCopyCaptureMgr_ = static_cast<ext_image_copy_capture_manager_v1*>(wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, std::min(version, 1u)));
  } else if (std::strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
    self.extOutputImageCaptureSourceMgr_ = static_cast<ext_output_image_capture_source_manager_v1*>(wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, std::min(version, 1u)));
  } else if (std::strcmp(iface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
    self.extForeignToplevelImageCaptureSourceMgr_ = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, std::min(version, 1u)));
  } else if (std::strcmp(iface, wp_color_manager_v1_interface.name) == 0) {
    self.colorMgr_ = static_cast<wp_color_manager_v1*>(wl_registry_bind(registry, name, &wp_color_manager_v1_interface, std::min(version, 3u)));
  } else if (std::strcmp(iface, ext_data_control_manager_v1_interface.name) == 0) {
    self.extDataControlMgr_ = static_cast<ext_data_control_manager_v1*>(wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, std::min(version, 1u)));
  } else if (std::strcmp(iface, zwlr_data_control_manager_v1_interface.name) == 0) {
    self.wlrDataControlMgr_ = static_cast<zwlr_data_control_manager_v1*>(wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, std::min(version, 2u)));
  } else if (std::strcmp(iface, zwp_linux_dmabuf_v1_interface.name) == 0) {
    self.linuxDmabuf_ = static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, std::min(version, 6u)));
  } else if (std::strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
    self.wlrForeignToplevelMgr_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3u)));
    if (self.wlrForeignToplevelMgr_) {
      self.wlrForeignToplevels_.bind(self.wlrForeignToplevelMgr_, self.display_);
    }
  } else if (std::strcmp(iface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
    self.extForeignToplevelList_ = static_cast<ext_foreign_toplevel_list_v1*>(wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, std::min(version, 1u)));
    if (self.extForeignToplevelList_) {
      self.extForeignToplevels_.bind(self.extForeignToplevelList_, self.display_);
    }
  } else if (std::strcmp(iface, "xwayland_shell_v1") == 0) {
    self.hasXWayland_ = true;
  } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
    auto slot = std::make_unique<OutputSlot>();
    slot->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
    wl_output_add_listener(slot->output, &kOutputListener, slot.get());
    self.tracked_outputs_.push_back(std::move(slot));
  }
}

void WaylandConnection::registry_global_remove(void* data, wl_registry* registry, uint32_t name)
{
  HS_LOG("registry_global_remove: name=%u", name);
  (void)data;
  (void)registry;
}

}
