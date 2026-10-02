// Event Horizon panel core — brand-new lifecycle, draw and reload.
//
// Own Wayland connection, own layers, own tray/MPRIS/poll state. The draw
// path is optimized for a thin strip: one running-snapshot + one MPRIS
// snapshot per frame shared by measure + paint, a per-frame width cache
// inside the painter, input/blur regions limited to the visible bar, and
// Vulkan CPU presentation with damage-buffer commit.

#include "desktop_shell/panel/core/panel.hpp"

#include "desktop_shell/panel/layout/panel_geometry.hpp"
#include "desktop_shell/panel/paint/panel_paint.hpp"

#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "services/tray/dbus/tray_context_menu.hpp"

#include <sdbus-c++/sdbus-c++.h>
#include <mutex>
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"
#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"
#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"
#include "desktop_shell/desktop/widgets/weather_fancy/fancy_weather_card_paint.hpp"
#include "desktop_shell/controlcenter/paint/control_center_popup_paint.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/widgets/battery/battery_paint.hpp"
#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wl/core/protocols.hpp"
#include "services/tray/manager/tray_manager.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/audio/pipewire_service.hpp"
#include "configuration/shell_config.hpp"
#include "wl/surface/layer_surface.hpp"

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace eh::shell::panel {

namespace {

void panel_path_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
  const double r = std::min({radius, w * 0.5, h * 0.5});
  const double p = M_PI;
  cairo_new_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -p * 0.5, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, p * 0.5);
  cairo_arc(cr, x + r, y + h - r, r, p * 0.5, p);
  cairo_arc(cr, x + r, y + r, r, p, p * 1.5);
  cairo_close_path(cr);
}

// Per-corner variant (top-left, top-right, bottom-right, bottom-left).
// Used when the four corner sliders diverge; otherwise the uniform fast path
// above is used.
void panel_path_rounded_rect4(cairo_t* cr, double x, double y, double w, double h, double rtl, double rtr,
                              double rbr, double rbl) {
  rtl = std::clamp(rtl, 0.0, std::min(w, h) * 0.5);
  rtr = std::clamp(rtr, 0.0, std::min(w, h) * 0.5);
  rbr = std::clamp(rbr, 0.0, std::min(w, h) * 0.5);
  rbl = std::clamp(rbl, 0.0, std::min(w, h) * 0.5);
  const double p = M_PI;
  cairo_new_path(cr);
  cairo_move_to(cr, x + rtl, y);
  cairo_line_to(cr, x + w - rtr, y);
  if (rtr > 0.0) cairo_arc(cr, x + w - rtr, y + rtr, rtr, -p * 0.5, 0);
  cairo_line_to(cr, x + w, y + h - rbr);
  if (rbr > 0.0) cairo_arc(cr, x + w - rbr, y + h - rbr, rbr, 0, p * 0.5);
  cairo_line_to(cr, x + rbl, y + h);
  if (rbl > 0.0) cairo_arc(cr, x + rbl, y + h - rbl, rbl, p * 0.5, p);
  cairo_line_to(cr, x, y + rtl);
  if (rtl > 0.0) cairo_arc(cr, x + rtl, y + rtl, rtl, p, p * 1.5);
  cairo_close_path(cr);
}

bool panel_point_in(double px, double py, double x, double y, double w, double h) {
  return px >= x && py >= y && px < (x + w) && py < (y + h);
}

// Index into app.lastHits under the current pointer, or -1.
int panel_hit_slot_index(const PanelApp& app) {
  for (size_t i = 0; i < app.lastHits.size(); ++i) {
    const auto& h = app.lastHits[i];
    if (panel_point_in(app.pointerX, app.pointerY, h.x, h.y, h.w, h.h)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

// Defined below the popup section (needs the popup listener).
void panel_dispatch_slot_click(PanelApp& app, int slotIdx);
void panel_dispatch_dead_zone(PanelApp& app, std::uint32_t button);

namespace {

std::uint64_t panel_now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void panel_layer_configure(void* data, zwlr_layer_surface_v1* layer, std::uint32_t serial, std::uint32_t width,
                           std::uint32_t height) {
  auto* L = static_cast<PanelOutputLayer*>(data);
  if (!L || !L->panel) return;
  PanelApp& app = *L->panel;
  if (layer != L->layer) return;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  L->pendingSerial = 0;
  if (width > 0) L->configuredWidth = static_cast<int>(width);
  if (height > 0) L->configuredHeight = static_cast<int>(height);
  L->configured = L->configuredWidth > 0 && L->configuredHeight > 0;
  L->everConfigured = true;
  panel_draw(app);
}

void panel_layer_closed(void* data, zwlr_layer_surface_v1*) {
  auto* L = static_cast<PanelOutputLayer*>(data);
  if (!L || !L->panel) return;
  L->layer = nullptr;
  L->surface = nullptr;
  L->configured = false;
  L->everConfigured = false;
}

const zwlr_layer_surface_v1_listener kPanelLayerListener = {
    .configure = panel_layer_configure,
    .closed = panel_layer_closed,
};

void panel_pointer_enter(void* data, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t sx,
                         wl_fixed_t sy) {
  auto& app = *static_cast<PanelApp*>(data);
  app.pointerSurface = surface;
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  if (app.settings.autoHide) {
    // The draw pass eases animOffsetPx toward the reveal target for a smooth
    // slide; just flip the target here and let the frame chain do the rest.
    app.reveal = true;
    app.animTargetPx = 0.0;
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  }
}

void panel_pointer_leave(void* data, wl_pointer*, std::uint32_t, wl_surface*) {
  auto& app = *static_cast<PanelApp*>(data);
  app.pointerSurface = nullptr;
  app.hoverSlot = -1;
  app.pressedSlot = -1;
  panel_tooltip_cancel(app);
  if (app.settings.autoHide) {
    app.reveal = false;
    app.animTargetPx = static_cast<double>(app.settings.height);
  }
  panel_schedule_frame(app);
}

void panel_pointer_motion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
  auto& app = *static_cast<PanelApp*>(data);
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  // Clicks inside an open popup belong to the popup surface: leave bar hover
  // alone so the popup doesn't flicker the strip underneath.
  if (app.popupSurface && app.pointerSurface == app.popupSurface) return;
  const int hover = panel_hit_slot_index(app);
  if (hover != app.hoverSlot) {
    app.hoverSlot = hover;
    app.hoverLiftPx = (hover >= 0) ? 5.0 : 0.0;
    if (hover >= 0 && (size_t)hover < app.lastHits.size()) {
      const auto& h = app.lastHits[(size_t)hover];
      debug_log("panel-layout", "hover #%d %s ptr=(%.0f,%.0f) rect=(%.0f,%.0f %.0fx%.0f)", hover,
                h.widgetId.c_str(), app.pointerX, app.pointerY, h.x, h.y, h.w, h.h);
    } else {
      debug_log("panel-layout", "hover none ptr=(%.0f,%.0f)", app.pointerX, app.pointerY);
    }
    app.tooltipHoverSlot = hover;
    app.tooltipHoverStart = std::chrono::steady_clock::now();
    panel_tooltip_cancel(app);
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  }
  if (hover >= 0 && (size_t)hover < app.lastHits.size()) {
    const auto& hit = app.lastHits[(size_t)hover];
    if (hit.slotKind == (int)PanelPaintSlot::Kind::Media && app.mpris) {
      const double iconSize = (double)app.settings.iconSize;
      const int zone = eh::mpris::DockMpris::media_hit_zone(app.pointerX - hit.x, hit.w, iconSize);
      if (zone != app.mediaHoverZone) {
        app.mediaHoverZone = zone;
        app.frameRedrawPending = true;
        panel_schedule_frame(app);
      }
    } else if (app.mediaHoverZone != -2) {
      app.mediaHoverZone = -2;
    }
  } else if (app.mediaHoverZone != -2) {
    app.mediaHoverZone = -2;
  }
}

void panel_pointer_button(void* data, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button,
                          std::uint32_t state) {
  auto& app = *static_cast<PanelApp*>(data);
  (void)button;
  // Clicks inside an open popup are the popup's own (v1: popups are
  // display-only, so just keep them open).
  if (app.popupSurface && app.pointerSurface == app.popupSurface) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && app.popupKind == PanelPopupKind::Tray &&
        !app.popupItems.empty() && app.trayBus && !app.popupService.empty() && !app.popupMenuPath.empty()) {
      double y = 4.0;
      const double py = app.pointerY;
      for (const auto& it : app.popupItems) {
        if (it.id < 0) {
          y += 13.0;
          continue;
        }
        if (py >= y && py < y + 26.0) {
          if (!it.enabled) return;
          try {
            auto menu = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{app.popupService},
                                           sdbus::ObjectPath{app.popupMenuPath});
            menu->callMethod("Event")
                .onInterface("com.canonical.dbusmenu")
                .withArguments(int32_t{it.id}, std::string("clicked"),
                               sdbus::Variant(int32_t{0}), uint32_t{0});
          } catch (const std::exception& e) {
            eh::shell_log::dbus_tray("panel menu event failed: ", e.what());
          }
          panel_popup_close(app);
          return;
        }
        y += 26.0;
      }
    }
    if (state == WL_POINTER_BUTTON_STATE_RELEASED && app.popupKind == PanelPopupKind::Calendar) {
      const double W = 320.0;
      const auto nav = eh::shell::desktop::cal_nav_geom(W, 1.0);
      const double px = app.pointerX;
      const double py = app.pointerY;
      bool onPrev = px >= nav.prevX && px < nav.prevX + nav.btnSz && py >= nav.navY && py < nav.navY + nav.btnSz;
      bool onNext = px >= nav.nextX && px < nav.nextX + nav.btnSz && py >= nav.navY && py < nav.navY + nav.btnSz;
      if (onPrev || onNext) {
        std::tm d = app.calDisplayDate;
        if (d.tm_year == 0 && d.tm_mon == 0) {
          std::time_t t = std::time(nullptr);
          d = *std::localtime(&t);
        }
        int m = d.tm_mon + (onPrev ? -1 : 1);
        int y = d.tm_year;
        if (m < 0) { m = 11; y -= 1; }
        if (m > 11) { m = 0; y += 1; }
        d.tm_mon = m;
        d.tm_year = y;
        app.calDisplayDate = d;
        panel_popup_draw(app);
        return;
      }
    }
    return;
  }
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    app.pressDismissedKind = app.popupKind;
    if (app.popupSurface) panel_popup_close(app);
    app.pressButton = button;
    app.pressedSlot = panel_hit_slot_index(app);
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  } else {
    const int slot = panel_hit_slot_index(app);
    const int pressed = app.pressedSlot;
    app.pressedSlot = -1;
    if (slot >= 0 && slot == pressed) panel_dispatch_slot_click(app, slot);
    else if (slot < 0) panel_dispatch_dead_zone(app, button);
    app.pressDismissedKind = PanelPopupKind::None;
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  }
}

void panel_pointer_frame(void*, wl_pointer*) {}
void panel_pointer_axis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
  auto& app = *static_cast<PanelApp*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (!app.settings.scrollChangesVolume) return;
  if (app.hoverSlot >= 0) return;
  const double delta = wl_fixed_to_double(value);
  if (delta == 0.0) return;
  const auto st = eh::shell::dock_slot_hooks::control_center_audio_output_state();
  const double cur = std::clamp((double)st.volume_pct / 100.0, 0.0, 1.0);
  const double next = std::clamp(cur + (delta < 0.0 ? 0.05 : -0.05), 0.0, 1.0);
  eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(next);
}
void panel_pointer_axis_source(void*, wl_pointer*, std::uint32_t) {}
void panel_pointer_axis_stop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void panel_pointer_axis_discrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void panel_pointer_axis_value120(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void panel_pointer_axis_relative_direction(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
#ifdef EH_HAVE_POINTER_WARP
void panel_pointer_warp(void* data, wl_pointer*, wl_fixed_t sx, wl_fixed_t sy) {
  panel_pointer_motion(data, nullptr, 0, sx, sy);
}
#endif

const wl_pointer_listener kPanelPointerListener = {
    .enter = panel_pointer_enter,
    .leave = panel_pointer_leave,
    .motion = panel_pointer_motion,
    .button = panel_pointer_button,
    .axis = panel_pointer_axis,
    .frame = panel_pointer_frame,
    .axis_source = panel_pointer_axis_source,
    .axis_stop = panel_pointer_axis_stop,
    .axis_discrete = panel_pointer_axis_discrete,
    .axis_value120 = panel_pointer_axis_value120,
    .axis_relative_direction = panel_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
    .warp = panel_pointer_warp,
#endif
};

void panel_keyboard_keymap(void* data, wl_keyboard*, std::uint32_t format, int fd, std::uint32_t size) {
  auto& app = *static_cast<PanelApp*>(data);
  (void)format;
  (void)size;
  if (fd >= 0) close(fd);
  (void)app;
}
void panel_keyboard_enter(void*, wl_keyboard*, std::uint32_t, wl_surface*, wl_array*) {}
void panel_keyboard_leave(void*, wl_keyboard*, std::uint32_t, wl_surface*) {}
void panel_keyboard_key(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
void panel_keyboard_modifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t) {}
void panel_keyboard_repeat_info(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

const wl_keyboard_listener kPanelKeyboardListener = {
    .keymap = panel_keyboard_keymap,
    .enter = panel_keyboard_enter,
    .leave = panel_keyboard_leave,
    .key = panel_keyboard_key,
    .modifiers = panel_keyboard_modifiers,
    .repeat_info = panel_keyboard_repeat_info,
};

void panel_frame_done(void* data, wl_callback* cb, std::uint32_t) {
  auto& app = *static_cast<PanelApp*>(data);
  wl_callback_destroy(cb);
  app.frameCallback = nullptr;
  app.frameCallbackRequestedMs = 0;
  app.anim.tick();
  if (app.frameRedrawPending || app.anim.has_active()) panel_draw(app);
}

const wl_callback_listener kPanelFrameListener = {
    .done = panel_frame_done,
};

wl_surface* panel_pick_frame_surface(PanelApp& app) {
  if (app.layers.empty()) return nullptr;
  if (app.pointerSurface) {
    for (const auto& up : app.layers)
      if (up && up->surface == app.pointerSurface) return up->surface;
  }
  return app.layers[0] ? app.layers[0]->surface : nullptr;
}

bool panel_pick_frame_surface_ready(PanelApp& app, wl_surface* surf) {
  if (!surf) return false;
  if (app.layers.empty()) return app.configured;
  for (const auto& up : app.layers)
    if (up && up->surface == surf) return up->configured && up->configuredWidth > 0 && up->configuredHeight > 0;
  return app.configured;
}

void sync_panel_tray_items(PanelApp& app) {
  auto items = eh::tray::TrayManager::instance().copy_items();
  std::lock_guard<std::mutex> lock(app.trayMutex);
  for (auto& it : app.trayItems) {
    if (it.pixSurface) {
      cairo_surface_destroy(it.pixSurface);
      it.pixSurface = nullptr;
    }
    it.proxy.reset();
  }
  app.trayItems = std::move(items);
}

void panel_start_tray_async(PanelApp& app) {
  if (app.trayStartLaunched.exchange(true)) return;
  if (app.trayStartThread.joinable()) app.trayStartThread.join();
  app.trayStartThread = std::thread([]() {
    const bool dockHosts = eh::config::shell_config_snapshot().dock.dockShowDock;
    (void)eh::tray::TrayManager::instance().start_secondary_host(dockHosts ? 8000 : 0);
  });
}

}  // namespace

bool panel_init_on_display(PanelApp& app) {
  app.wl = std::make_unique<eh::wayland::WaylandConnection>();
  if (!app.wl->connect(true)) {
    std::cerr << "[panel] Failed to connect own wl_display\n";
    return false;
  }
  app.display = app.wl->display();
  app.compositor = app.wl->compositor();
  app.shm = app.wl->shm();
  app.seat = app.wl->seat();
  app.layerShell = app.wl->layer_shell();

  eh::shell::dock_slot_hooks::battery_widget_init();
  app.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

  if (app.seat) {
    app.pointer = wl_seat_get_pointer(app.seat);
    if (app.pointer) wl_pointer_add_listener(app.pointer, &kPanelPointerListener, &app);
    app.keyboard = wl_seat_get_keyboard(app.seat);
    if (app.keyboard) wl_keyboard_add_listener(app.keyboard, &kPanelKeyboardListener, &app);
  }

  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    panel_maybe_reload_settings(app, sc);
    app.icons.set_icon_theme(sc.dock.iconTheme);
  }
  app.enabled = app.settings.enabled;
  if (app.display) wl_display_flush(app.display);
  app.wlError = false;
  return true;
}

void panel_init_deferred_startup(PanelApp& app) {
  try {
    app.trayBus = sdbus::createSessionBusConnection();
    if (app.trayBus) app.trayBus->setMethodCallTimeout(eh::tray::kTrayMethodCallTimeout);
  } catch (const std::exception&) {
  }
  panel_start_tray_async(app);
  app.trayEventFd = eh::tray::TrayManager::instance().subscribe();
  if (app.trayEventFd >= 0) sync_panel_tray_items(app);

  eh::net::NetworkManagerService::instance().start();
  eh::audio::PipeWireService::instance().start();
  try {
    app.mpris = std::make_unique<eh::mpris::DockMpris>();
    (void)app.mpris->poll_refresh();
  } catch (...) {
    app.mpris.reset();
  }
  eh::shell::dock_slot_hooks::bluetooth_widget_init();
}

void panel_add_layer(PanelApp& app, wl_output* output) {
  if (!app.compositor || !app.layerShell || !output) return;
  for (const auto& up : app.layers)
    if (up && up->wlOut == output) return;

  auto L = std::make_unique<PanelOutputLayer>();
  L->panel = &app;
  L->wlOut = output;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kPanelNamespace;
  cfg.layer = app.settings.overlayLayer ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, output, cfg, &kPanelLayerListener,
                                         L.get(), &surf, &layer)) {
    std::cerr << "[panel] failed to create layer surface\n";
    return;
  }
  L->surface = surf;
  L->layer = layer;
  if (auto* mgr = app.wl->background_effect_manager())
    L->bgEffect = ext_background_effect_manager_v1_get_background_effect(mgr, surf);
  if (L->surface) {
    if (auto* vp = app.wl->viewporter()) {
      L->surfExt.viewport = wp_viewporter_get_viewport(vp, L->surface);
      wl_surface_set_buffer_scale(L->surface, 1);
    }
    if (auto* fsm = app.wl->fractional_scale_manager())
      L->surfExt.fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, L->surface);
  }
  // Apply anchor/margins/exclusive/size from current settings.
  panel_layer_apply_geometry(app, *L);
  app.layers.push_back(std::move(L));

  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);
  debug_log("panel", "add_layer: n_layers=%zu", app.layers.size());
}

void panel_destroy_layers(PanelApp& app) {
  for (auto& up : app.layers) {
    if (!up) continue;
    up->shmBuf.destroy();
    up->shmBuf2.destroy();
    if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
    if (up->surface) wl_surface_destroy(up->surface);
    up->layer = nullptr;
    up->surface = nullptr;
    up->configured = false;
  }
  app.layers.clear();
  app.lastHits.clear();
}

void panel_schedule_frame(PanelApp& app) {
  if (app.frameCallback) {
    if (app.frameCallbackRequestedMs != 0 && panel_now_ms() - app.frameCallbackRequestedMs > 100)
      app.frameCallback = nullptr;
    else
      return;
  }
  wl_surface* surf = panel_pick_frame_surface(app);
  if (!surf || !panel_pick_frame_surface_ready(app, surf)) return;
  app.frameCallback = wl_surface_frame(surf);
  app.frameCallbackRequestedMs = panel_now_ms();
  wl_callback_add_listener(app.frameCallback, &kPanelFrameListener, &app);
  wl_surface_commit(surf);
  if (app.display) wl_display_flush(app.display);
}

void panel_draw(PanelApp& app) {
  app.frameRedrawPending = false;
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const PanelSettings& ts = app.settings;
  if (!app.enabled) return;

  const double bgAlpha = static_cast<double>(ts.opacity) / 100.0;
  const bool isFloating = ts.widthMode == 0;
  const bool isFill = ts.widthMode == 2;
  const int margin = isFloating ? ts.floatingAmount : 0;

  eh::shell::shared::RunningSnapshot runningSnap;
  if (app.toplevels && app.toplevels->size() > 0)
    runningSnap = eh::shell::shared::build_running_snapshot(*app.toplevels, app.appFirstSeenSerial, true);
  const eh::mpris::PlayerSnapshot mprisSnap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};

  PanelSectionWidths fillSections{};
  if (isFill)
    fillSections = panel_measure_sections(app, ts.leftWidgets, ts.centerWidgets, ts.rightWidgets, runningSnap,
                                          mprisSnap);

  // Taskbar parity: smooth auto-hide slide. The pointer handlers only flip
  // the target; each frame eases the offset toward it and chains another
  // frame until settled, giving a vsync-locked slide without extra timers.
  const double hideTarget =
      (ts.autoHide && !app.reveal) ? static_cast<double>(isFloating ? ts.height + 4 : ts.height) : 0.0;
  app.animTargetPx = hideTarget;
  bool slideMoving = false;
  {
    const double diff = hideTarget - app.animOffsetPx;
    if (std::fabs(diff) > 0.5) {
      app.animOffsetPx += diff * 0.35;
      slideMoving = true;
    } else {
      app.animOffsetPx = hideTarget;
    }
  }

  for (auto& up : app.layers) {
    if (!up || !up->surface || !up->layer || !up->configured) continue;
    if (up->configuredWidth <= 0 || up->configuredHeight <= 0) continue;

    const int logW = up->configuredWidth;
    const int logH = up->configuredHeight;
    const int barH = isFloating ? ts.height + 4 : ts.height;
    const double bufScale = up->surfExt.preferred_scale();
    const int bufW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logW) * bufScale)));
    const int bufH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logH) * bufScale)));

    if (!app.panelVk || !app.panelVk->valid()) {
      if (app.vkFailed) continue;
      if (!app.panelVk) app.panelVk = std::make_shared<eh::wayland::VulkanDisplayContext>();
      if (!app.panelVk->init(app.display)) {
        app.vkFailed = true;
        continue;
      }
    }
    if (!up->vkLayer || !up->vkLayer->valid()) {
      if (!up->vkLayer) up->vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
      if (!up->vkLayer->create(*app.panelVk, app.display, up->surface, bufW, bufH)) continue;
    } else {
      up->vkLayer->resize(bufW, bufH);
    }
    if (!up->glRaster.ensure(bufW, bufH)) continue;

    cairo_t* cr = up->glRaster.cairo();
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (bufScale != 1.0) cairo_scale(cr, bufScale, bufScale);

    const double barW = [&]() -> double {
      if (isFloating) return static_cast<double>(logW) - static_cast<double>(margin) * 2.0;
      if (isFill) {
        const double ui = std::clamp(ts.scale, 0.5, 2.0) * std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
        const double pad = panel_strip_pad_px(ui);
        const double secGap = panel_section_gap_px(ui);
        const double half = pad + std::max(fillSections.left, fillSections.right) + secGap;
        const double cw2 = fillSections.center + 2.0 * half;
        return std::clamp(cw2, 80.0, static_cast<double>(logW));
      }
      return static_cast<double>(logW);
    }();
    const double barX = isFloating ? static_cast<double>(margin)
                        : isFill   ? (static_cast<double>(logW) - barW) / 2.0
                                   : 0.0;
    double barY = 0.0;

    const double autoHideClip = ts.autoHide ? app.animOffsetPx : 0.0;
    double barHVisible = static_cast<double>(barH) - autoHideClip;
    if (barHVisible < 1.0) barHVisible = 0.0;
    if (!ts.positionTop) barY += autoHideClip;

    if (barW > 0 && barHVisible > 0) {
      cairo_save(cr);
      cairo_rectangle(cr, barX, barY, barW, barHVisible);
      cairo_clip(cr);
      const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
      const bool uniformCorners =
          ts.cornerTL == ts.cornerTR && ts.cornerTL == ts.cornerBL && ts.cornerBR == ts.cornerTL;
      if (uniformCorners) {
        eh::shell::shared::paint_glass_card(cr, barX, barY, barW, barHVisible, static_cast<double>(ts.radius),
                                            mc, bgAlpha);
      } else {
        // Diverged corners: clip the bespoke shape, then lay the glass card
        // (sized with the largest radius) inside it so the interior shading
        // matches the uniform path exactly.
        cairo_save(cr);
        panel_path_rounded_rect4(cr, barX, barY, barW, barHVisible, static_cast<double>(ts.cornerTL),
                                 static_cast<double>(ts.cornerTR), static_cast<double>(ts.cornerBR),
                                 static_cast<double>(ts.cornerBL));
        cairo_clip(cr);
        const double maxR =
            static_cast<double>(std::max({ts.cornerTL, ts.cornerTR, ts.cornerBL, ts.cornerBR}));
        eh::shell::shared::paint_glass_card(cr, barX, barY, barW, barHVisible, maxR, mc, bgAlpha);
        cairo_restore(cr);
      }
      if (ts.border && ts.borderSize > 0) {
        const double bw = static_cast<double>(ts.borderSize);
        const double inset = bw * 0.5;
        if (uniformCorners) {
          const double rr = std::max(0.0, static_cast<double>(ts.radius) - inset);
          panel_path_rounded_rect(cr, barX + inset, barY + inset, std::max(1.0, barW - bw),
                                  std::max(1.0, barHVisible - bw), rr);
        } else {
          panel_path_rounded_rect4(cr, barX + inset, barY + inset, std::max(1.0, barW - bw),
                                   std::max(1.0, barHVisible - bw), static_cast<double>(ts.cornerTL) - inset,
                                   static_cast<double>(ts.cornerTR) - inset, static_cast<double>(ts.cornerBR) - inset,
                                   static_cast<double>(ts.cornerBL) - inset);
        }
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
        cairo_set_line_width(cr, bw);
        cairo_stroke(cr);
      }
      if (!ts.leftWidgets.empty() || !ts.centerWidgets.empty() || !ts.rightWidgets.empty()) {
        std::vector<PanelWidgetHit> hits;
        panel_paint_widget_bar(app, cr, barX, barY, barW, barHVisible, ts.leftWidgets, ts.centerWidgets,
                               ts.rightWidgets, &hits, app.hoverSlot, app.pressedSlot, true, runningSnap,
                               mprisSnap);
        app.lastHits = std::move(hits);
        app.lastBarX = barX;
        app.lastBarY = barY;
        app.lastBarW = barW;
        app.lastBarH = barHVisible;
      }
      cairo_restore(cr);
    }

    cairo_restore(cr);
    cairo_surface_flush(up->glRaster.cairo_surface());
    if ((up->regionBarW != barW || up->regionBarH != barHVisible) && barW > 0 && barHVisible > 0) {
      up->regionBarW = barW;
      up->regionBarH = barHVisible;
      if (up->bgEffect) {
        wl_region* rgn = wl_compositor_create_region(app.compositor);
        wl_region_add(rgn, static_cast<int32_t>(barX), static_cast<int32_t>(barY), static_cast<int32_t>(barW),
                      static_cast<int32_t>(barHVisible));
        ext_background_effect_surface_v1_set_blur_region(up->bgEffect, rgn);
        wl_region_destroy(rgn);
      }
      wl_region* inputRgn = wl_compositor_create_region(app.compositor);
      wl_region_add(inputRgn, static_cast<int32_t>(barX), static_cast<int32_t>(barY),
                    static_cast<int32_t>(barW), static_cast<int32_t>(barHVisible));
      wl_surface_set_input_region(up->surface, inputRgn);
      wl_region_destroy(inputRgn);
    }
    if (up->surfExt.viewport && up->surface) {
      wl_surface_set_buffer_scale(up->surface, 1);
      wp_viewport_set_destination(up->surfExt.viewport, logW, logH);
    }
    bool transient = false;
    if (!up->vkLayer->present_cpu_bgra(*app.panelVk, up->glRaster.data(), bufW, bufH, up->glRaster.stride(),
                                       &transient)) {
      if (transient)
        up->vkLayer.reset();
      else
        app.vkFailed = true;
      continue;
    }
    wl_surface_damage_buffer(up->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(up->surface);
  }
  if (slideMoving) {
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  }
}

void panel_handle_tray(PanelApp& app) {
  std::uint64_t v = 0;
  while (read(app.trayEventFd, &v, sizeof(v)) > 0) {
  }
  sync_panel_tray_items(app);
  panel_draw(app);
}

void panel_maybe_reload_settings(PanelApp& app, const eh::config::ShellConfig& sc) {
  PanelSettings next;
  next.enabled = sc.panel.enabled;
  next.widthMode = std::clamp(sc.panel.widthMode, 0, 2);
  next.height = std::clamp(sc.panel.height, 20, 120);
  next.radius = std::clamp(sc.panel.radius, 0, 50);
  next.opacity = std::clamp(sc.panel.opacity, 0, 100);
  next.iconSize = std::clamp(sc.panel.iconSize, 8, 96);
  next.iconSpacing = std::clamp(sc.panel.iconSpacing, 0, 50);
  next.floatingAmount = std::clamp(sc.panel.floatingAmount, 0, 50);
  next.edgeGap = std::clamp(sc.panel.edgeGap, 0, 25);
  next.exclusiveZoneGap = std::clamp(sc.panel.exclusiveZoneGap, 0, 100);
  next.scale = std::clamp(sc.panel.scale, 0.5, 2.0);
  next.iconTheme = sc.panel.iconTheme;
  next.outputName = sc.panel.outputName;
  next.leftWidgets = sc.panel.leftWidgets;
  next.centerWidgets = sc.panel.centerWidgets;
  next.rightWidgets = sc.panel.rightWidgets;
  next.positionTop = sc.panel.positionTop;
  next.autoHide = sc.panel.autoHide;
  next.tooltipsEnabled = sc.panel.tooltipsEnabled;
  next.widgetsEnabled = sc.panel.widgetsEnabled;
  next.border = sc.panel.border;
  next.borderSize = std::clamp(sc.panel.borderSize, 1, 12);
  next.reserveSpace = sc.panel.reserveSpace;
  next.overlayLayer = sc.panel.overlayLayer;
  next.hoverHighlight = sc.panel.hoverHighlight;
  next.smartAutoHide = sc.panel.smartAutoHide;
  next.revealOnWorkspaceSwitch = sc.panel.revealOnWorkspaceSwitch;
  next.scrollChangesVolume = sc.panel.scrollChangesVolume;
  next.deadZoneLeft = sc.panel.deadZoneLeft;
  next.deadZoneMiddle = sc.panel.deadZoneMiddle;
  next.deadZoneRight = sc.panel.deadZoneRight;
  next.capsuleEnabled = sc.panel.capsuleEnabled;
  next.capsuleOpacity = std::clamp(sc.panel.capsuleOpacity, 0, 100);
  next.cornerTL = std::clamp(sc.panel.cornerTL, 0, 50);
  next.cornerTR = std::clamp(sc.panel.cornerTR, 0, 50);
  next.cornerBL = std::clamp(sc.panel.cornerBL, 0, 50);
  next.cornerBR = std::clamp(sc.panel.cornerBR, 0, 50);
  next.clickThroughWidgets = sc.panel.clickThroughWidgets;
  next.uiGlobalScale = std::clamp(sc.dock.shellUiScale, 0.5, 2.0);

  const bool layerChanged = next.overlayLayer != app.settings.overlayLayer;
  const bool geomChanged =
      next.widthMode != app.settings.widthMode || next.height != app.settings.height ||
      next.floatingAmount != app.settings.floatingAmount || next.edgeGap != app.settings.edgeGap ||
      next.positionTop != app.settings.positionTop || next.autoHide != app.settings.autoHide ||
      next.smartAutoHide != app.settings.smartAutoHide || next.reserveSpace != app.settings.reserveSpace ||
      next.exclusiveZoneGap != app.settings.exclusiveZoneGap || next.outputName != app.settings.outputName ||
      next.enabled != app.settings.enabled;
  const bool wasEnabled = app.settings.enabled;

  app.settings = std::move(next);
  // Normalize height so layer size, exclusive zone, paint and popup
  // clearance all agree (same rule as the painter's effective height).
  app.settings.height = panel_effective_height_px(app.settings, app.settings.uiGlobalScale);
  app.enabled = app.settings.enabled;
  app.icons.set_icon_theme(sc.dock.iconTheme.empty() ? app.settings.iconTheme : sc.dock.iconTheme);
  app.slotsValid = false;

  if (geomChanged) {
    for (auto& up : app.layers)
      if (up) panel_layer_apply_geometry(app, *up);
    // The layer-shell level (TOP vs OVERLAY) is fixed at surface creation, so
    // a layer flip or output move destroys the layers and lets the standalone
    // loop recreate them via pendingOutputRebind.
    if (layerChanged || wasEnabled != app.enabled || app.settings.outputName != sc.panel.outputName) {
      panel_destroy_layers(app);
      app.pendingOutputRebind = true;
    }
  }
  panel_apply_autohide_state(app, app.settings.autoHide);
}

void panel_cleanup(PanelApp& app) {
  panel_popup_close(app);
  if (app.tooltipSurface) {
    if (app.tooltipLayer) zwlr_layer_surface_v1_destroy(app.tooltipLayer);
    wl_surface_destroy(app.tooltipSurface);
    app.tooltipSurface = nullptr;
    app.tooltipLayer = nullptr;
  }
  app.tooltipShm.destroy();
  for (auto& up : app.layers) {
    if (!up) continue;
    up->shmBuf.destroy();
    up->shmBuf2.destroy();
    if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
    if (up->surface) wl_surface_destroy(up->surface);
    up->layer = nullptr;
    up->surface = nullptr;
  }
  app.layers.clear();
  if (app.trayStartThread.joinable()) {
    // TrayManager shutdown joins internally; detach guard below.
    app.trayStartThread.detach();
  }
  if (app.pointer) {
    wl_pointer_destroy(app.pointer);
    app.pointer = nullptr;
  }
  if (app.keyboard) {
    wl_keyboard_destroy(app.keyboard);
    app.keyboard = nullptr;
  }
  if (app.xkbState) {
    xkb_state_unref(app.xkbState);
    app.xkbState = nullptr;
  }
  if (app.xkbKeymap) {
    xkb_keymap_unref(app.xkbKeymap);
    app.xkbKeymap = nullptr;
  }
  if (app.xkbCtx) {
    xkb_context_unref(app.xkbCtx);
    app.xkbCtx = nullptr;
  }
  app.scaledIcons.clear();
  app.wl.reset();
  app.display = nullptr;
}

void panel_apply_autohide_state(PanelApp& app, bool autoHide) {
  (void)autoHide;
  if (!app.settings.autoHide) {
    app.animOffsetPx = 0.0;
    app.animTargetPx = 0.0;
    app.reveal = true;
  } else {
    app.reveal = false;
    app.animTargetPx = static_cast<double>(app.settings.height);
    app.animOffsetPx = app.animTargetPx;
  }
}

void panel_update_context_hide(PanelApp& app) {
  if (!app.settings.smartAutoHide && !app.settings.revealOnWorkspaceSwitch) return;
  bool anyOccupied = false;
  int activeId = -1;
  for (const auto& e : app.workspaceStrip) {
    if (e.occupied) anyOccupied = true;
    if (e.active) activeId = e.id;
  }
  const std::uint64_t now = panel_now_ms();
  if (activeId >= 0 && activeId != app.contextHideLastActiveWs) {
    app.contextHideLastActiveWs = activeId;
    if (app.settings.revealOnWorkspaceSwitch && (app.settings.autoHide || app.settings.smartAutoHide))
      app.contextHideRevealUntilMs = now + 2000;
  }
  bool want = app.reveal;
  if (app.settings.smartAutoHide) want = !anyOccupied;
  if (now < app.contextHideRevealUntilMs) want = true;
  if (!app.settings.autoHide && !app.settings.smartAutoHide) want = true;
  if (want != app.reveal) {
    app.reveal = want;
    app.animTargetPx = want ? 0.0 : static_cast<double>(app.settings.height);
    app.frameRedrawPending = true;
    panel_schedule_frame(app);
  }
}

namespace {
PanelOverviewToggleFn g_panel_overview_toggle_fn;
}  // namespace

void panel_set_overview_toggle_fn(PanelOverviewToggleFn fn) {
  g_panel_overview_toggle_fn = std::move(fn);
}

void panel_request_overview_toggle() {
  if (g_panel_overview_toggle_fn) g_panel_overview_toggle_fn();
}

void panel_tooltip_destroy(PanelApp& app) {
  app.tooltipShownSlot = -1;
  app.tooltipText.clear();
  if (app.tooltipSurface) {
    app.tooltipShm.destroy();
    if (app.tooltipLayer) zwlr_layer_surface_v1_destroy(app.tooltipLayer);
    wl_surface_destroy(app.tooltipSurface);
    app.tooltipSurface = nullptr;
    app.tooltipLayer = nullptr;
    app.tooltipCfgW = 0;
    app.tooltipCfgH = 0;
    app.tooltipConfigured = false;
    if (app.display) (void)wl_display_roundtrip(app.display);
  }
}

void panel_tooltip_cancel(PanelApp& app) {
  app.tooltipHoverSlot = -1;
  panel_tooltip_destroy(app);
}

namespace {

constexpr int kPanelTooltipDelayMs = 500;
constexpr int kPanelTooltipPadH = 10;
constexpr int kPanelTooltipPadV = 6;
constexpr int kPanelTooltipFontPx = 12;

std::string panel_tooltip_text_for(const PanelApp& app, int slotIdx) {
  if (slotIdx < 0 || static_cast<size_t>(slotIdx) >= app.lastHits.size()) return {};
  const PanelWidgetHit& hit = app.lastHits[static_cast<size_t>(slotIdx)];
  const auto kind = static_cast<PanelPaintSlot::Kind>(hit.slotKind);
  if (kind == PanelPaintSlot::Kind::App) {
    if (auto desktop = find_desktop_file_for_appid(hit.widgetId)) {
      if (auto info = read_desktop_entry_info(*desktop)) {
        if (!info->name.empty()) return info->name;
      }
    }
    return hit.widgetId;
  }
  switch (kind) {
    case PanelPaintSlot::Kind::Clock:
      return "Clock";
    case PanelPaintSlot::Kind::WorldClock:
      return "World Clock";
    case PanelPaintSlot::Kind::Weather:
      return "Weather";
    case PanelPaintSlot::Kind::Media:
      return "Media";
    case PanelPaintSlot::Kind::Workspaces:
      return "Workspaces";
    case PanelPaintSlot::Kind::Overview:
      return "Overview";
    case PanelPaintSlot::Kind::ControlCenter:
      return "Control Center";
    case PanelPaintSlot::Kind::Battery:
      return "Battery";
    case PanelPaintSlot::Kind::Bluetooth:
      return "Bluetooth";
    case PanelPaintSlot::Kind::VolumeMixer:
      return "Volume";
    case PanelPaintSlot::Kind::Vpn:
      return "VPN";
    case PanelPaintSlot::Kind::Tray:
      return "Tray";
    case PanelPaintSlot::Kind::Settings:
      return "Settings";
    case PanelPaintSlot::Kind::Trash:
      return "Trash";
    case PanelPaintSlot::Kind::Smenu:
      return "Start Menu";
    case PanelPaintSlot::Kind::AppMenu:
      return "Applications";
    case PanelPaintSlot::Kind::AppDrawer:
      return "Applications";
    case PanelPaintSlot::Kind::Spotlight:
      return "Spotlight";
    case PanelPaintSlot::Kind::Separator:
      return {};
    case PanelPaintSlot::Kind::App:
      return {};
  }
  return hit.widgetId;
}

void panel_tooltip_layer_configure(void* data, zwlr_layer_surface_v1* layer, std::uint32_t serial,
                                   std::uint32_t w, std::uint32_t h) {
  auto* app = static_cast<PanelApp*>(data);
  if (!app || layer != app->tooltipLayer) return;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (w > 0) app->tooltipCfgW = static_cast<int>(w);
  if (h > 0) app->tooltipCfgH = static_cast<int>(h);
  app->tooltipConfigured = app->tooltipCfgW > 0 && app->tooltipCfgH > 0;
}

void panel_tooltip_layer_closed(void* data, zwlr_layer_surface_v1*) {
  auto* app = static_cast<PanelApp*>(data);
  if (!app) return;
  app->tooltipLayer = nullptr;
  app->tooltipSurface = nullptr;
  app->tooltipConfigured = false;
}

const zwlr_layer_surface_v1_listener kPanelTooltipListener = {
    .configure = panel_tooltip_layer_configure,
    .closed = panel_tooltip_layer_closed,
};

}  // namespace

void panel_tooltip_tick(PanelApp& app) {
  if (!app.settings.tooltipsEnabled) {
    panel_tooltip_cancel(app);
    return;
  }
  const int hover = app.hoverSlot;
  if (hover < 0 || hover != app.tooltipHoverSlot) {
    if (hover != app.tooltipHoverSlot) panel_tooltip_destroy(app);
    return;
  }
  if (app.tooltipShownSlot == hover) return;
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                            app.tooltipHoverStart)
                          .count();
  if (waited < kPanelTooltipDelayMs) return;
  const std::string text = panel_tooltip_text_for(app, hover);
  if (text.empty() || !app.compositor || !app.layerShell) return;

  // Measure with toy text (matches the draw pass below).
  cairo_surface_t* measure =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
  double tw = 0, th = 0;
  {
    cairo_t* mcr = cairo_create(measure);
    cairo_select_font_face(mcr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(mcr, kPanelTooltipFontPx);
    cairo_text_extents_t te{};
    cairo_text_extents(mcr, text.c_str(), &te);
    tw = te.width;
    th = te.height;
    cairo_destroy(mcr);
  }
  cairo_surface_destroy(measure);
  const int boxW = static_cast<int>(std::ceil(tw)) + kPanelTooltipPadH * 2;
  const int boxH = static_cast<int>(std::ceil(th)) + kPanelTooltipPadV * 2;

  // Direction-aware anchor: below a top bar, above a bottom bar — mirroring
  // the popup placement so tips never cover sibling widgets.
  const PanelWidgetHit& hit = app.lastHits[static_cast<size_t>(hover)];
  const int anchorX = static_cast<int>(hit.x + hit.w * 0.5);
  const auto pos = panel_compute_popup_position(app, anchorX, boxW, boxH);
  if (!pos.wl_out) return;

  panel_tooltip_destroy(app);
  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kPanelPopupNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.width = static_cast<std::uint32_t>(boxW);
  cfg.height = static_cast<std::uint32_t>(boxH);
  cfg.exclusiveZone = -1;
  cfg.marginLeft = pos.margin_side;
  if (app.settings.positionTop) {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    cfg.marginTop = pos.margin_edge;
  } else {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    cfg.marginBottom = pos.margin_edge;
  }
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, static_cast<wl_output*>(pos.wl_out),
                                         cfg, &kPanelTooltipListener, &app, &surf, &layer))
    return;
  app.tooltipSurface = surf;
  app.tooltipLayer = layer;
  app.tooltipText = text;
  app.tooltipShownSlot = hover;
  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);
  if (!app.tooltipConfigured) return;

  if (!app.tooltipShm.ensure(app.shm, "eh_panel_tooltip", boxW, boxH)) {
    panel_tooltip_destroy(app);
    return;
  }
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
  const double cardA = std::clamp(static_cast<double>(sc.appearance.overlayOpacityWidgetCard), 0.0, 1.0);
  cairo_t* cr = app.tooltipShm.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  eh::shell::shared::paint_glass_card(cr, 0, 0, boxW, boxH, 8.0, mc, 0.96 * cardA);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kPanelTooltipFontPx);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
  cairo_text_extents_t te{};
  cairo_text_extents(cr, text.c_str(), &te);
  cairo_move_to(cr, (boxW - te.width) / 2.0 - te.x_bearing,
                (boxH - te.height) / 2.0 - te.y_bearing);
  cairo_show_text(cr, text.c_str());
  cairo_restore(cr);
  cairo_surface_flush(app.tooltipShm.cairo_surface());
  wl_surface_attach(surf, app.tooltipShm.wl(), 0, 0);
  wl_surface_damage_buffer(surf, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surf);
  if (app.display) wl_display_flush(app.display);
}

// ---- Popups (v1: calendar + control-center real, others glass stub) ----

void panel_popup_close(PanelApp& app) {
  app.popupHoverItem = -1;
  app.popupKind = PanelPopupKind::None;
  if (app.popupSurface) {
    if (app.popupFrameCb) {
      wl_callback_destroy(app.popupFrameCb);
      app.popupFrameCb = nullptr;
    }
    app.popupBuf.destroy();
    if (app.popupLayer) zwlr_layer_surface_v1_destroy(app.popupLayer);
    wl_surface_destroy(app.popupSurface);
    app.popupSurface = nullptr;
    app.popupLayer = nullptr;
    app.popupConfiguredW = 0;
    app.popupConfiguredH = 0;
    app.popupConfigured = false;
    app.popupPendingSerial = 0;
    if (app.display) (void)wl_display_roundtrip(app.display);
  }
}

namespace {
void panel_popup_configure(void* data, zwlr_layer_surface_v1* layer, std::uint32_t serial, std::uint32_t w,
                           std::uint32_t h) {
  auto* app = static_cast<PanelApp*>(data);
  if (!app || layer != app->popupLayer) return;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (w > 0) app->popupConfiguredW = static_cast<int>(w);
  if (h > 0) app->popupConfiguredH = static_cast<int>(h);
  app->popupConfigured = app->popupConfiguredW > 0 && app->popupConfiguredH > 0;
  panel_popup_draw(*app);
}
void panel_popup_closed(void* data, zwlr_layer_surface_v1*) {
  auto* app = static_cast<PanelApp*>(data);
  if (!app) return;
  app->popupLayer = nullptr;
  app->popupSurface = nullptr;
  app->popupConfigured = false;
}
const zwlr_layer_surface_v1_listener kPanelPopupListener = {
    .configure = panel_popup_configure,
    .closed = panel_popup_closed,
};
}  // namespace

// Create the popup layer surface anchored to the bar (top or bottom),
// horizontally positioned by the shared popup-position helper so popups
// never run off the output edge and always clear the bar.
bool panel_popup_open(PanelApp& app, PanelPopupKind kind, int anchorX, int popupW, int popupH) {
  panel_popup_close(app);
  if (!app.compositor || !app.layerShell) {
    debug_log("panel-layout", "popup open FAIL kind=%d anchor=%d size=%dx%d (no compositor/shell)", (int)kind,
              anchorX, popupW, popupH);
    return false;
  }
  const auto pos = panel_compute_popup_position(app, anchorX, popupW, popupH);
  if (!pos.wl_out) {
    debug_log("panel-layout", "popup open FAIL kind=%d anchor=%d size=%dx%d (no output)", (int)kind, anchorX,
              popupW, popupH);
    return false;
  }

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kPanelPopupNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.width = static_cast<std::uint32_t>(std::max(1, popupW));
  cfg.height = static_cast<std::uint32_t>(std::max(1, popupH));
  cfg.exclusiveZone = -1;
  cfg.marginLeft = pos.margin_side;
  if (app.settings.positionTop) {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    cfg.marginTop = pos.margin_edge;
  } else {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    cfg.marginBottom = pos.margin_edge;
  }
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, static_cast<wl_output*>(pos.wl_out),
                                         cfg, &kPanelPopupListener, &app, &surf, &layer))
    return false;
  app.popupSurface = surf;
  app.popupLayer = layer;
  app.popupKind = kind;
  app.popupW = popupW;
  app.popupH = popupH;
  app.popupAnchorX = anchorX;
  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);
  debug_log("panel-layout", "popup open OK kind=%d anchor=%d size=%dx%d", (int)kind, anchorX, popupW,
            popupH);
  return true;
}

namespace {

// Toggle-close: the press already dismissed this kind; a release on the
// owning slot must not reopen it straight away.
bool panel_toggle_closed(PanelApp& app, PanelPopupKind kind) { return app.pressDismissedKind == kind; }

void panel_open_popup_for_slot(PanelApp& app, const PanelWidgetHit& hit, PanelPaintSlot::Kind kind) {
  const int anchorX = static_cast<int>(hit.x + hit.w * 0.5);
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  switch (kind) {
    case PanelPaintSlot::Kind::Clock:
    case PanelPaintSlot::Kind::WorldClock: {
      if (panel_toggle_closed(app, PanelPopupKind::Calendar)) return;
      std::time_t nowT = std::time(nullptr);
      app.calDisplayDate = *std::localtime(&nowT);
      app.calDisplayDate.tm_mday = 1;
      app.calDisplayDate.tm_hour = 0; app.calDisplayDate.tm_min = 0; app.calDisplayDate.tm_sec = 0;
      app.calDisplayDate.tm_isdst = -1;
      std::mktime(&app.calDisplayDate);
      constexpr int w = eh::shell::dock::popup::calendar::kCalendarPopupW;
      constexpr int h = eh::shell::dock::popup::calendar::kCalendarPopupH;
      if (panel_popup_open(app, PanelPopupKind::Calendar, anchorX, w, h)) panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::ControlCenter: {
      if (panel_toggle_closed(app, PanelPopupKind::ControlCenter)) return;
      app.ccWidgetId = hit.widgetId;
      const int w = ::control_center_popup_width_for(sc, hit.widgetId);
      // Measure the pear layout so growth sections (device lists, mixer
      // rows) fit; fall back to the legacy fixed height otherwise.
      int h = 620;
      {
        namespace ccl = eh::shell::dock::control_center;
        if (ccl::pear_layout_enabled(sc, hit.widgetId)) {
          const auto cfg = ccl::pear_center_config(sc, hit.widgetId);
          const double ui = std::clamp(app.settings.scale, 0.5, 2.0) *
                            std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
          h = static_cast<int>(
              std::ceil(ccl::cc_compute_pear_layout(static_cast<double>(w), app.ccState, cfg, ui).totalH));
          h = std::max(200, h);
          // Clamp to the output so growth sections (device lists, mixer
          // rows) can't push content off-screen, where it would read as
          // "missing". Mirrors the taskbar clamp.
          if (auto* ref = panel_ref_layer(app); ref && ref->wlOut && app.wl) {
            for (const auto& b : app.wl->logical_output_bounds()) {
              if (b.output != ref->wlOut) continue;
              if (b.height > 0)
                h = std::min(h, b.height - panel_popup_clearance_px(app.settings) - 12);
              break;
            }
          }
        }
      }
      if (panel_popup_open(app, PanelPopupKind::ControlCenter, anchorX, w, h)) panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::Weather: {
      if (panel_toggle_closed(app, PanelPopupKind::Weather)) return;
      constexpr int w = eh::shell::dock::popup::weather::kWeatherPopupW;
      const int h = eh::shell::desktop::measure_fancy_weather_height(
          eh::config::shell_config_snapshot(), hit.widgetId);
      if (panel_popup_open(app, PanelPopupKind::Weather, anchorX, w, h)) panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::VolumeMixer: {
      if (panel_toggle_closed(app, PanelPopupKind::VolumeMixer)) return;
      if (panel_popup_open(app, PanelPopupKind::VolumeMixer, anchorX, 300, 440)) panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::Battery: {
      if (panel_toggle_closed(app, PanelPopupKind::Battery)) return;
      if (panel_popup_open(app, PanelPopupKind::Battery, anchorX, eh::widgets::kBatteryPopupW,
                           eh::widgets::battery_popup_height()))
        panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::Bluetooth: {
      if (panel_toggle_closed(app, PanelPopupKind::Bluetooth)) return;
      if (panel_popup_open(app, PanelPopupKind::Bluetooth, anchorX, eh::widgets::kBluetoothPopupW,
                           eh::widgets::bluetooth_popup_height()))
        panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::Vpn: {
      if (panel_toggle_closed(app, PanelPopupKind::Vpn)) return;
      const int n = static_cast<int>(eh::net::NetworkManagerService::instance().state().vpnConnections.size());
      if (panel_popup_open(app, PanelPopupKind::Vpn, anchorX, eh::shell::dock::popup::vpn::kVpnPopupW,
                           eh::shell::dock::popup::vpn::vpn_popup_height(n)))
        panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::Tray: {
      const PanelTrayItem* ti = nullptr;
      {
        std::lock_guard<std::mutex> lock(app.trayMutex);
        for (const auto& t : app.trayItems) {
          if (t.service + '\x1f' + t.path == hit.widgetId) {
            ti = &t;
            break;
          }
        }
      }
      if (app.pressButton == 0x110 && ti && app.trayBus && !ti->service.empty() && !ti->path.empty()) {
        try {
          auto proxy = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti->service},
                                          sdbus::ObjectPath{ti->path});
          proxy->callMethod("Activate")
              .onInterface("org.kde.StatusNotifierItem")
              .withTimeout(eh::shell::dock::tray_menu::kTrayMenuCallTimeout)
              .withArguments(static_cast<int32_t>(app.pointerX),
                             static_cast<int32_t>(app.pointerY));
        } catch (const std::exception& e) {
          eh::shell_log::dbus_tray("panel activate failed: ", e.what());
        }
        return;
      }
      if (app.pressButton == 0x111 && ti && app.trayBus && !ti->service.empty() && !ti->path.empty()) {
        namespace tray_menu = eh::shell::dock::tray_menu;
        const std::string menuPath = tray_menu::tray_menu_path_interactive(*app.trayBus, ti->service, ti->path);
        if (!menuPath.empty()) {
          try {
            auto menu = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti->service},
                                           sdbus::ObjectPath{menuPath});
            tray_menu::tray_menu_about_to_show(*menu);
            using Props = std::map<std::string, sdbus::Variant>;
            using Layout = sdbus::Struct<int32_t, Props, std::vector<sdbus::Variant>>;
            uint32_t revision = 0;
            Layout root{};
            menu->callMethod("GetLayout")
                .onInterface("com.canonical.dbusmenu")
                .withTimeout(tray_menu::kTrayMenuCallTimeout)
                .withArguments(int32_t{0}, int32_t{1},
                               std::vector<std::string>{"label", "enabled", "visible", "type"})
                .storeResultsTo(revision, root);
            (void)revision;
            app.popupItems.clear();
            for (const auto& vChild : std::get<2>(root)) {
              Layout child = vChild.get<Layout>();
              const int32_t id = std::get<0>(child);
              const auto& props = std::get<1>(child);
              auto getStr = [&](const char* k) -> std::string {
                auto it = props.find(k);
                if (it == props.end()) return {};
                try { return it->second.template get<std::string>(); } catch (const sdbus::Error&) { return {}; }
              };
              auto getBool = [&](const char* k, bool def) -> bool {
                auto it = props.find(k);
                if (it == props.end()) return def;
                try {
                  if (it->second.containsValueOfType<bool>()) return it->second.template get<bool>();
                  if (it->second.containsValueOfType<int32_t>()) return it->second.template get<int32_t>() != 0;
                  return def;
                } catch (const sdbus::Error&) { return def; }
              };
              if (getStr("type") == "separator") {
                app.popupItems.push_back({.id = -1, .label = "", .enabled = false});
                continue;
              }
              if (!getBool("visible", true)) continue;
              PanelPopupItem pi;
              pi.id = id;
              pi.label = getStr("label");
              pi.enabled = getBool("enabled", true);
              app.popupItems.push_back(std::move(pi));
            }
            if (!app.popupItems.empty()) {
              app.popupService = ti->service;
              app.popupMenuPath = menuPath;
              app.popupWidgetId = hit.widgetId;
              int rows = 0;
              for (const auto& pi : app.popupItems) rows += (pi.id < 0) ? 13 : 26;
              const int ph = rows + 8;
              if (panel_popup_open(app, PanelPopupKind::Tray, anchorX, 280, ph)) panel_popup_draw(app);
              return;
            }
          } catch (const std::exception& e) {
            eh::shell_log::dbus_tray("panel menu layout failed: ", e.what());
          }
        }
      }
      if (panel_toggle_closed(app, PanelPopupKind::Tray)) return;
      app.popupWidgetId = hit.widgetId;
      if (panel_popup_open(app, PanelPopupKind::Tray, anchorX, 280, 360)) panel_popup_draw(app);
      return;
    }
    case PanelPaintSlot::Kind::App:
    case PanelPaintSlot::Kind::Settings:
    case PanelPaintSlot::Kind::Media:
    case PanelPaintSlot::Kind::Workspaces:
    case PanelPaintSlot::Kind::Overview:
    case PanelPaintSlot::Kind::Trash:
    case PanelPaintSlot::Kind::Smenu:
    case PanelPaintSlot::Kind::AppMenu:
    case PanelPaintSlot::Kind::AppDrawer:
    case PanelPaintSlot::Kind::Spotlight:
    case PanelPaintSlot::Kind::Separator:
      return;
  }
}

}  // namespace

void panel_dispatch_slot_click(PanelApp& app, int slotIdx) {
  if (slotIdx < 0 || static_cast<size_t>(slotIdx) >= app.lastHits.size()) {
    debug_log("panel-layout", "click miss idx=%d button=0x%x", slotIdx, app.pressButton);
    return;
  }
  const PanelWidgetHit hit = app.lastHits[static_cast<size_t>(slotIdx)];
  const auto kind = static_cast<PanelPaintSlot::Kind>(hit.slotKind);
  debug_log("panel-layout", "click #%d %s kind=%d button=0x%x", slotIdx, hit.widgetId.c_str(), (int)kind,
            app.pressButton);

  if (kind == PanelPaintSlot::Kind::Overview) {
    panel_request_overview_toggle();
    if (app.display) wl_display_flush(app.display);
    return;
  }

  if (kind == PanelPaintSlot::Kind::Workspaces) {
    // Middle-click opens the overview (GNOME parity); left-click switches.
    if (app.pressButton == 0x112) {
      panel_request_overview_toggle();
      if (app.display) wl_display_flush(app.display);
      return;
    }
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    const double ui = std::clamp(app.settings.scale, 0.5, 2.0) * std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
    const double icon = std::clamp(static_cast<double>(app.settings.iconSize) * ui, 8.0,
                                   std::max(8.0, app.lastBarH - 4.0));
    const int idx = eh::widgets::workspaces_pick_index(app.pointerX - hit.x, hit.w, sc, hit.widgetId, icon,
                                                       app.workspaceStrip);
    if (idx >= 0 && static_cast<size_t>(idx) < app.workspaceStrip.size())
      eh::widgets::workspace_activate_entry(app.workspaceStrip[static_cast<size_t>(idx)], app.compositorKind);
    return;
  }

  if (kind == PanelPaintSlot::Kind::Media) {
    if (!app.mpris) return;
    const double iconSize = (double)app.settings.iconSize;
    const PanelWidgetHit* hitp = nullptr;
    for (const auto& h : app.lastHits) {
      if (h.slotKind == (int)PanelPaintSlot::Kind::Media) {
        hitp = &h;
        break;
      }
    }
    int zone = -1;
    if (hitp) zone = eh::mpris::DockMpris::media_hit_zone(app.pointerX - hitp->x, hitp->w, iconSize);
    try {
      if (zone == 0) app.mpris->previous();
      else if (zone == 2) app.mpris->next();
      else if (zone == 1) app.mpris->play_pause();
      else {
        if (app.popupSurface && app.popupKind == PanelPopupKind::MediaPlayer) panel_popup_close(app);
        else {
          const int anchorX = hitp ? (int)(hitp->x + hitp->w * 0.5) : (int)app.pointerX;
          if (panel_popup_open(app, PanelPopupKind::MediaPlayer, anchorX,
                               eh::widgets::popup::media_player::kMediaPlayerPopupW,
                               eh::widgets::popup::media_player::kMediaPlayerPopupH)) panel_popup_draw(app);
        }
      }
    } catch (...) {
    }
    return;
  }

  if (kind == PanelPaintSlot::Kind::App) {
    if (app.toplevels && hit.chosenSerial != 0) {
      for (const auto& tl : *app.toplevels) {
        if (tl.serial == hit.chosenSerial && tl.handle && !tl.closed) {
          zwlr_foreign_toplevel_handle_v1_activate(tl.handle, app.seat);
          if (app.display) wl_display_flush(app.display);
          break;
        }
      }
    }
    return;
  }

  if (kind == PanelPaintSlot::Kind::Settings) {
    eh::settings::request_launch_settings();
    return;
  }

  if (kind == PanelPaintSlot::Kind::Trash) {
    launch_exec_command("xdg-open trash:///");
    if (app.display) wl_display_flush(app.display);
    return;
  }

  panel_open_popup_for_slot(app, hit, kind);
  if (app.display) wl_display_flush(app.display);
}

void panel_dispatch_dead_zone(PanelApp& app, std::uint32_t button) {
  std::string action;
  if (button == 0x110) action = app.settings.deadZoneLeft;
  else if (button == 0x112) action = app.settings.deadZoneMiddle;
  else if (button == 0x111) action = app.settings.deadZoneRight;
  else return;
  if (action == "none" || action.empty()) return;
  const int anchorX = static_cast<int>(app.pointerX);
  PanelPopupKind kind = PanelPopupKind::None;
  int w = 0, h = 0;
  if (action == "control-center") {
    kind = PanelPopupKind::ControlCenter;
    w = 360;
    h = 620;
  } else if (action == "calendar") {
    kind = PanelPopupKind::Calendar;
    w = eh::shell::dock::popup::calendar::kCalendarPopupW;
    h = eh::shell::dock::popup::calendar::kCalendarPopupH;
  } else if (action == "tray") {
    kind = PanelPopupKind::Tray;
    w = 280;
    h = 200;
  } else {
    return;
  }
  if (panel_toggle_closed(app, kind)) return;
  if (panel_popup_open(app, kind, anchorX, w, h)) panel_popup_draw(app);
  if (app.display) wl_display_flush(app.display);
}

void panel_popup_draw(PanelApp& app) {
  if (!app.popupSurface || !app.popupConfigured) return;
  const int pw = app.popupConfiguredW > 0 ? app.popupConfiguredW : app.popupW;
  const int ph = app.popupConfiguredH > 0 ? app.popupConfiguredH : app.popupH;
  if (pw <= 0 || ph <= 0) return;
  if (!app.popupBuf.ensure(app.shm, eh::shell::kPanelPopupNamespace, pw, ph)) return;
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  cairo_t* cr = app.popupBuf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
  // Popups are widget cards: honor the launcher "Widget opacity" card alpha.
  const double cardA = std::clamp(static_cast<double>(sc.appearance.overlayOpacityWidgetCard), 0.0, 1.0);
  eh::shell::shared::paint_glass_card(cr, 0, 0, pw, ph, 16.0, mc, 0.96 * cardA);
  if (app.popupKind == PanelPopupKind::Calendar) {
    eh::shell::dock::popup::calendar::dock_calendar_popup_paint(app, cr, sc);
  } else if (app.popupKind == PanelPopupKind::ControlCenter) {
    control_center_popup_paint(app.ccState, cr, pw, ph, sc, app.mpris.get(), app.ccWidgetId, app.icons, {});
  } else if (app.popupKind == PanelPopupKind::Battery) {
    eh::widgets::dock_battery_popup_paint(app.pointerX, app.pointerY, cr, sc);
  } else if (app.popupKind == PanelPopupKind::Bluetooth) {
    eh::widgets::dock_bluetooth_popup_paint(app.pointerX, app.pointerY, cr, sc);
  } else if (app.popupKind == PanelPopupKind::MediaPlayer) {
    eh::widgets::popup::media_player::media_player_popup_paint(app, cr, sc);
  } else if (app.popupKind == PanelPopupKind::Weather) {
    eh::widgets::popup::weather::weather_popup_paint(app, cr, sc);
  } else if (app.popupKind == PanelPopupKind::VolumeMixer) {
    eh::widgets::popup::volume_mixer::volume_mixer_popup_paint(app, cr, sc);
  } else if (app.popupKind == PanelPopupKind::Vpn) {
    eh::widgets::popup::vpn::vpn_popup_paint(app, cr, sc);
  } else if (app.popupKind == PanelPopupKind::Tray && !app.popupItems.empty()) {
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    double yy = 4.0;
    for (const auto& it : app.popupItems) {
      if (it.id < 0) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, 10.0, yy + 13.0);
        cairo_line_to(cr, (double)pw - 10.0, yy + 13.0);
        cairo_stroke(cr);
        yy += 13.0;
        continue;
      }
      cairo_set_source_rgba(cr, 1, 1, 1, it.enabled ? 0.87 : 0.35);
      cairo_move_to(cr, 12.0, yy + 17.0);
      cairo_show_text(cr, it.label.c_str());
      yy += 26.0;
    }
  } else {
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.9);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14.0);
    cairo_move_to(cr, 16, 28);
    const char* title = "Panel";
    switch (app.popupKind) {
      case PanelPopupKind::Weather:
        title = "Weather";
        break;
      case PanelPopupKind::VolumeMixer:
        title = "Volume";
        break;
      case PanelPopupKind::Battery:
        title = "Battery";
        break;
      case PanelPopupKind::Bluetooth:
        title = "Bluetooth";
        break;
      case PanelPopupKind::Vpn:
        title = "VPN";
        break;
      case PanelPopupKind::MediaPlayer:
        title = "Media";
        break;
      case PanelPopupKind::Tray:
        title = "Tray";
        break;
      default:
        break;
    }
    cairo_show_text(cr, title);
  }
  cairo_restore(cr);
  cairo_surface_flush(app.popupBuf.cairo_surface());
  wl_surface_attach(app.popupSurface, app.popupBuf.wl(), 0, 0);
  wl_surface_damage_buffer(app.popupSurface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(app.popupSurface);
  if (app.display) wl_display_flush(app.display);
}

}  // namespace eh::shell::panel
