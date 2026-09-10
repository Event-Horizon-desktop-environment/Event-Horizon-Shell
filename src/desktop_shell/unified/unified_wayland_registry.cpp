#include "desktop_shell/unified/unified_wayland_registry.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/common/bench/startup_trace.hpp"
#include "wl/core/protocols.hpp"

#include <algorithm>
#include <memory>
#include <iostream>
#include <string>
#include <string_view>

#include "desktop_shell/shared/system/input/shell_input.hpp"

static DockOutputSlot* dock_ensure_output_slot(DockApp& app, wl_output* out, uint32_t name) {
    
  for (auto& u : app.outputSlots) {
    if (u && u->output == out) return u.get();
  }
  auto up = std::make_unique<DockOutputSlot>();
  up->output = out;
  up->globalName = name;
  DockOutputSlot* p = up.get();
  app.outputSlots.push_back(std::move(up));
  return p;
}

static void dock_xdg_logical_position(void* data, zxdg_output_v1*, int32_t x, int32_t y) {
   
  static_cast<DockOutputSlot*>(data)->logical_x = x;
  static_cast<DockOutputSlot*>(data)->logical_y = y;
}

static void dock_xdg_logical_size(void* data, zxdg_output_v1*, int32_t w, int32_t h) {
   
  auto* s = static_cast<DockOutputSlot*>(data);
  s->logical_w = w;
  s->logical_h = h;
  if (w > 0 && h > 0) s->ready = true;
}

static void dock_zxdg_output_name(void*, zxdg_output_v1*, const char*) {}

static void dock_zxdg_output_description(void*, zxdg_output_v1*, const char*) {}

static void dock_xdg_output_done_deprecated(void*, zxdg_output_v1*) {}

static const zxdg_output_v1_listener dock_zxdg_output_listener = {
    .logical_position = dock_xdg_logical_position,
    .logical_size = dock_xdg_logical_size,
    .done = dock_xdg_output_done_deprecated,
    .name = dock_zxdg_output_name,
    .description = dock_zxdg_output_description,
};

static void dock_bind_xdg_for_slot(DockApp& app, DockOutputSlot* slot) {
    
  if (!slot || !app.xdgOutputManager || slot->xdg || !slot->output) return;
  slot->xdg = zxdg_output_manager_v1_get_xdg_output(app.xdgOutputManager, slot->output);
  zxdg_output_v1_add_listener(slot->xdg, &dock_zxdg_output_listener, slot);
}

void dock_bind_xdg_all_slots(DockApp& app) {
   
  if (!app.xdgOutputManager) return;
  for (auto& u : app.outputSlots) {
    if (u) dock_bind_xdg_for_slot(app, u.get());
  }
}

static void dock_output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                                 int32_t) {}

static void dock_output_mode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height,
                             int32_t refresh_mHz) {
   
  auto& a = *static_cast<DockApp*>(data);
  if (!(flags & WL_OUTPUT_MODE_CURRENT) || width <= 0 || height <= 0) return;
  for (auto& u : a.outputSlots) {
    if (!u || u->output != output) continue;
    u->current_mode_refresh_mHz = refresh_mHz;
    a.primaryOutputWidthPx = width;
    a.primaryOutputHeightPx = height;
    return;
  }
}

static void dock_output_done(void* data, wl_output* output) {
   
  auto& app = *static_cast<DockApp*>(data);
  for (auto& u : app.outputSlots) {
    if (u && u->output == output) {
      if (u->logical_w > 0 && u->logical_h > 0) u->ready = true;
      break;
    }
  }
}

static void dock_output_scale(void*, wl_output*, int32_t) {}

static void dock_output_name(void* data, wl_output* output, const char* name) {
   
  auto& app = *static_cast<DockApp*>(data);
  if (!name || !name[0]) return;
  for (auto& u : app.outputSlots) {
    if (u && u->output == output) {
      u->output_name.assign(name);
      break;
    }
  }
}

static void dock_output_description(void*, wl_output*, const char*) {}

static const wl_output_listener dock_output_listener = {
    .geometry = dock_output_geometry,
    .mode = dock_output_mode,
    .done = dock_output_done,
    .scale = dock_output_scale,
    .name = dock_output_name,
    .description = dock_output_description,
};

static void dock_fractional_scale_preferred(void* data, wp_fractional_scale_v1*  , uint32_t scale) {
   
  auto& L = *static_cast<DockOutputLayer*>(data);
  const std::int32_t next = static_cast<std::int32_t>(scale);
  if (L.surfExt.preferredScale120 == next) return;
  L.surfExt.preferredScale120 = next;
  if (L.dock) L.dock->deferDockRedraw = true;
}

static const wp_fractional_scale_v1_listener g_dock_fractional_scale_listener = {
    .preferred_scale = dock_fractional_scale_preferred,
};

void dock_attach_output_layer_fractional_scale(DockApp& app, DockOutputLayer& L) {
   
  if (!L.surface) return;
  if (app.viewporter && app.fractionalScaleMgr && !L.surfExt.viewport) {
    L.surfExt.viewport = wp_viewporter_get_viewport(app.viewporter, L.surface);
    wl_surface_set_buffer_scale(L.surface, 1);
  }
  if (!app.fractionalScaleMgr || L.surfExt.fractionalScale) return;
  L.surfExt.fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(app.fractionalScaleMgr, L.surface);
  if (!L.surfExt.fractionalScale) return;
  wp_fractional_scale_v1_add_listener(L.surfExt.fractionalScale, &g_dock_fractional_scale_listener, &L);
}

static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
   
  auto& app = *static_cast<DockApp*>(data);
  const std::string_view iface(interface);
  // ESSENTIAL: needed immediately for surface creation and rendering
  if (iface == wl_compositor_interface.name) {
    EH_ST_TRACE(std::cerr << "dock registry: bind compositor name=" << name << " v=" << version);
    app.compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min<uint32_t>(version, 4)));
    return;
  }
  if (iface == wl_shm_interface.name) {
    EH_ST_TRACE(std::cerr << "dock registry: bind wl_shm name=" << name);
    app.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    return;
  }
  if (iface == zwlr_layer_shell_v1_interface.name) {
    EH_ST_TRACE(std::cerr << "dock registry: bind zwlr_layer_shell_v1 name=" << name);
    app.layerShell = static_cast<zwlr_layer_shell_v1*>(
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min<uint32_t>(version, 4)));
    return;
  }
  if (iface == wl_output_interface.name) {
    EH_ST_TRACE(std::cerr << "dock registry: bind wl_output name=" << name);
    auto* out = static_cast<wl_output*>(
        wl_registry_bind(registry, name, &wl_output_interface, std::min<uint32_t>(version, 4)));
    dock_ensure_output_slot(app, out, name);
    wl_output_add_listener(out, &dock_output_listener, &app);
    return;
  }
  // NON-ESSENTIAL: deferred to after roundtrip for faster startup
  app.deferredBinds.push_back({interface, name, version});
}

static void registry_global_remove(void* data, wl_registry* /*registry*/, uint32_t name) {
  auto& app = *static_cast<DockApp*>(data);
  for (auto it = app.outputSlots.begin(); it != app.outputSlots.end(); ++it) {
    auto* slot = it->get();
    if (!slot || slot->globalName != name) continue;
    if (slot->xdg) {
      zxdg_output_v1_destroy(slot->xdg);
      slot->xdg = nullptr;
    }
    if (slot->output) {
      wl_output_destroy(slot->output);
      slot->output = nullptr;
    }
    app.outputSlots.erase(it);
    break;
  }
  app.pendingDockOutputRebind = true;
}

void dock_bind_deferred_globals(DockApp& app) {
   
  for (const auto& d : app.deferredBinds) {
    const std::string_view iface(d.iface);
    if (iface == wl_seat_interface.name) {
      app.seat = static_cast<wl_seat*>(wl_registry_bind(app.registry, d.name, &wl_seat_interface, 5));
      if (app.seat) wl_seat_add_listener(app.seat, &g_shell_seat_listener, &app);
    } else if (iface == zwlr_foreign_toplevel_manager_v1_interface.name) {
      app.toplevelManager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<uint32_t>(d.version, 3)));
    } else if (iface == ext_foreign_toplevel_list_v1_interface.name) {
      // ext-foreign-toplevel-list-v1 mirrors zwlr-foreign-toplevel-manager.
      // Bind it only where the compositor lacks the standard manager.
      if (!is_hyprland()) continue;
      app.extToplevelList = static_cast<ext_foreign_toplevel_list_v1*>(
          wl_registry_bind(app.registry, d.name, &ext_foreign_toplevel_list_v1_interface, std::min<uint32_t>(d.version, 1)));
      app.extToplevels.bind(app.extToplevelList, app.display);
    } else if (iface == xdg_wm_base_interface.name) {
      app.xdgBase = static_cast<xdg_wm_base*>(wl_registry_bind(app.registry, d.name, &xdg_wm_base_interface, std::min<uint32_t>(d.version, 2)));
      if (app.xdgBase) {
        static const xdg_wm_base_listener xdgListener = {
            .ping = [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
        };
        xdg_wm_base_add_listener(app.xdgBase, &xdgListener, &app);
      }
    } else if (iface == zxdg_output_manager_v1_interface.name) {
      app.xdgOutputManager = static_cast<zxdg_output_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &zxdg_output_manager_v1_interface, std::min<uint32_t>(d.version, 3)));
    } else if (iface == wp_fractional_scale_manager_v1_interface.name) {
      app.fractionalScaleMgr = static_cast<wp_fractional_scale_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &wp_fractional_scale_manager_v1_interface, std::min<uint32_t>(d.version, 1)));
    } else if (iface == wp_viewporter_interface.name) {
      app.viewporter = static_cast<wp_viewporter*>(
          wl_registry_bind(app.registry, d.name, &wp_viewporter_interface, std::min<uint32_t>(d.version, 1)));
    } else if (iface == zwlr_gamma_control_manager_v1_interface.name) {
      app.gammaControlMgr = static_cast<zwlr_gamma_control_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &zwlr_gamma_control_manager_v1_interface, 1));
    } else if (iface == ext_idle_notifier_v1_interface.name) {
      app.idleNotifier = static_cast<ext_idle_notifier_v1*>(
          wl_registry_bind(app.registry, d.name, &ext_idle_notifier_v1_interface, 1));
    } else if (iface == ext_session_lock_manager_v1_interface.name) {
      app.sessionLockMgr = static_cast<ext_session_lock_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &ext_session_lock_manager_v1_interface, 1));
    } else if (iface == ext_background_effect_manager_v1_interface.name) {
      app.bgEffectMgr = static_cast<ext_background_effect_manager_v1*>(
          wl_registry_bind(app.registry, d.name, &ext_background_effect_manager_v1_interface, 1));
    } else if (iface == zwp_pointer_constraints_v1_interface.name) {
      app.pointerConstraints = static_cast<zwp_pointer_constraints_v1*>(
          wl_registry_bind(app.registry, d.name, &zwp_pointer_constraints_v1_interface, 1));
    }
  }
  app.deferredBinds.clear();
}

const wl_registry_listener g_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};
