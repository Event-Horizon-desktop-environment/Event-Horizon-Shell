#define _GNU_SOURCE 1
#pragma GCC diagnostic ignored "-Wunused-function"
#include "desktop_shell/shared/popup/session/session.hpp"

#include "desktop_shell/shared/popup/chrome/chrome.hpp"
#include "desktop_shell/controlcenter/persist/control_center_persist.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/shared/popup/geometry/margins.hpp"
#include "desktop_shell/dock/input/dock_position.hpp"
#include "desktop_shell/shared/popup/paint/finish.hpp"
#include "desktop_shell/shared/popup/buffer/buffer.hpp"
#include "desktop_shell/shared/popup/caret/caret.hpp"
#include "desktop_shell/spotlight/search/spotlight_query.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/shared/toplevel/toplevel_hooks.hpp"

#include "configuration/shell_config.hpp"

#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/launchpad/host/launchpad_host.hpp"

#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "services/tray/dbus/tray_context_menu.hpp"
#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"
#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"
#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"
#include "desktop_shell/widgets/battery/battery_paint.hpp"
#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "services/network/core/network_manager_service.hpp"

#include "wl/surface/layer_surface.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <cairo/cairo.h>

using eh::shell::dock::destroy_popup_buffer;
using eh::shell::dock::dock_popup_destroy_caret_frame;
using eh::shell::dock::kControlCenterPopupW;

using eh::shell::dock::spotlight_popup_total_height;
using eh::shell::dock::kSpotlightPopupW;
using eh::shell::str::trim;

bool dock_popup_pointer_on_any_popup_surface(const DockApp& app) {
  if (!app.pointerSurface) return false;
  if (app.popupOpen && app.pointerSurface == app.popupSurface) return true;
  if (app.launchpad && app.launchpad->is_open() && app.launchpad->owns_surface(app.pointerSurface)) return true;
  return false;
}

int g_dock_appmenu_trace_last_zone = -999;

static void log_dbg(const char* fmt, ...);

static const char* dock_popup_layer_namespace_for_kind(DockApp::PopupKind kind) {
   
  switch (kind) {
    case DockApp::PopupKind::Tray:
      return eh::shell::kTrayMenuNamespace;
    case DockApp::PopupKind::App:
      return eh::shell::kDockContextMenuNamespace;
    case DockApp::PopupKind::AppMenu:
      return eh::shell::kAppDrawerNamespace;
    case DockApp::PopupKind::Spotlight:
      return eh::shell::kSpotlightNamespace;
    case DockApp::PopupKind::ControlCenter:
      return eh::shell::kControlCenterNamespace;
    case DockApp::PopupKind::Trash:
      return eh::shell::kDockContextMenuNamespace;
    case DockApp::PopupKind::Calendar:
      return eh::shell::kCalendarNamespace;
    case DockApp::PopupKind::Weather:
      return eh::shell::kPopupNamespace;
    case DockApp::PopupKind::VolumeMixer:
      return eh::shell::kPopupNamespace;
    case DockApp::PopupKind::Vpn:
    case DockApp::PopupKind::Battery:
      return eh::shell::kPopupNamespace;
    case DockApp::PopupKind::PowerConfirm:
      return eh::shell::kPopupNamespace;
    default:
      return eh::shell::kPopupNamespace;
  }
}

static void dock_popup_layer_surface_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t w,
                                               uint32_t h) {
   
  auto& app = *static_cast<DockApp*>(data);
  log_dbg("[dock-popup] configure ENTER: surface=%p app.popupLayerSurface=%p serial=%u granted=%ux%u kind=%d popupOpen=%d\n",
          (void*)surface, (void*)app.popupLayerSurface, serial, w, h, static_cast<int>(app.popupKind), (int)app.popupOpen);
  if (surface != app.popupLayerSurface) return;
  if (!app.popupOpen || app.popupKind == DockApp::PopupKind::None) {

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    log_dbg("[dock-popup] configure DROPPED (popup closed) serial=%u granted=%ux%u kind=%d\n",
            serial, w, h, static_cast<int>(app.popupKind));
    return;
  }
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  if (app.popupKind == DockApp::PopupKind::ControlCenter && app.ccState.openBenchStartMs != 0 &&
      !app.ccState.openBenchLoggedFirstConfigure && eh_cc_open_bench()) {
    app.ccState.openBenchLoggedFirstConfigure = true;
    const uint64_t ms = eh::shell::monotonic_ms() - app.ccState.openBenchStartMs;
    std::cerr << "[control-center] open: to_layer_surface_configure_ms=" << ms << "\n";
  }
  if (w > 0 && static_cast<int>(w) != app.popupW) app.popupW = static_cast<int>(w);
  if (h > 0 && static_cast<int>(h) != app.popupH) app.popupH = static_cast<int>(h);
  app.popupConfiguredX = 0;
  app.popupConfiguredY = 0;
  app.popupConfiguredW = app.popupW;
  app.popupConfiguredH = app.popupH;
  log_dbg("[dock-popup] configure: serial=%u granted=%ux%u cfgW=%d cfgH=%d kind=%d\n",
          serial, w, h, app.popupConfiguredW, app.popupConfiguredH, static_cast<int>(app.popupKind));
  if (dock_popup_kind_uses_app_drawer_ui(app.popupKind) && eh_app_drawer_debug_level() >= 2) {
    eh::shell::dock::app_drawer::trace_line(2, "dock", "popup_layer_configure app_drawer_ui serial=" + std::to_string(serial) + " w=" + std::to_string(w) +
                                           " h=" + std::to_string(h) + " → popupWxH=" + std::to_string(app.popupW) + "x" +
                                           std::to_string(app.popupH));
  }
  if (dock_popup_kind_uses_app_drawer_ui(app.popupKind)) {
    eh_app_drawer_clamp_scroll(app);
  }
  maybe_log_layout(app, "popup_layer_configure");
  popup_draw_surface(app);
}

static void dock_popup_layer_surface_closed(void* data, zwlr_layer_surface_v1*) {
  auto& app = *static_cast<DockApp*>(data);
  app.popupLayerSurface = nullptr;
  app.popupSurface = nullptr;
  app.popupOpen = false;
}

static const zwlr_layer_surface_v1_listener g_dock_popup_layer_listener = {
    .configure = dock_popup_layer_surface_configure,
    .closed = dock_popup_layer_surface_closed,
};

bool dock_popup_create_layer_surface_ex(DockApp& app, int anchorLocalX, wl_output* output, int marginLeft,
                                       int marginBottom) {
   
  (void)anchorLocalX;
  const double us = dock_ui_scale(app.settings);
  if (!app.compositor || !app.layerShell) return false;
  if (!output) return false;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = dock_popup_layer_namespace_for_kind(app.popupKind);
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    cfg.width = 0;
    cfg.height = 0;
    cfg.exclusiveZone = -1;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = 0;
    cfg.marginLeft = 0;
  } else {
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    cfg.width = static_cast<uint32_t>(std::max(1, app.popupW));
    cfg.height = static_cast<uint32_t>(std::max(1, app.popupH));
    cfg.exclusiveZone = -1;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = marginBottom;
    cfg.marginLeft = marginLeft;
    if (app.popupKind == DockApp::PopupKind::AppMenu ||
        app.popupKind == DockApp::PopupKind::ControlCenter ||
        app.popupKind == DockApp::PopupKind::Calendar ||
        app.popupKind == DockApp::PopupKind::Weather) {
      cfg.marginBottom += static_cast<int>(2.0 * us);
      cfg.marginLeft += static_cast<int>(2.0 * us);
    }
  }
  const bool wantKbd = (dock_popup_kind_uses_app_drawer_ui(app.popupKind) || app.popupKind == DockApp::PopupKind::Spotlight ||
                        app.popupKind == DockApp::PopupKind::ControlCenter ||
                        app.popupKind == DockApp::PopupKind::Calendar ||
                        app.popupKind == DockApp::PopupKind::Weather ||
                        app.popupKind == DockApp::PopupKind::VolumeMixer ||
                        app.popupKind == DockApp::PopupKind::Vpn ||
                        app.popupKind == DockApp::PopupKind::Battery ||
                        app.popupKind == DockApp::PopupKind::PowerConfirm);
  cfg.keyboard = wantKbd ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
                         : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, output, cfg, &g_dock_popup_layer_listener, &app,
                                          &surf, &layer)) {
    log_dbg("[dock-popup] create_layer_surface_ex FAIL: create_layer_surface returned false output=%p\n", (void*)output);
    return false;
  }
  app.popupSurface = surf;
  app.popupLayerSurface = layer;
  app.popupConfiguredW = 0;
  app.popupConfiguredH = 0;
  log_dbg("[dock-popup] create_layer_surface_ex OK: surf=%p layer=%p w=%u h=%u marginL=%d marginB=%d ns=%s\n",
          (void*)surf, (void*)layer, cfg.width, cfg.height, marginLeft, marginBottom, cfg.nameSpace);
  wl_surface_commit(app.popupSurface);
  if (app.display) wl_display_roundtrip(app.display);

  return true;
}

static bool dock_popup_create_layer_surface(DockApp& app, int anchorLocalX) {
   
  if (!app.compositor || !app.layerShell) {
    log_dbg("[dock-popup] create_layer_surface FAIL: !compositor || !layerShell\n");
    return false;
  }

  if (app.popupMarginOverride.output) {
    return dock_popup_create_layer_surface_ex(app, anchorLocalX, app.popupMarginOverride.output,
                                               app.popupMarginOverride.marginLeft,
                                               app.popupMarginOverride.marginBottom);
  }

  DockOutputLayer stackLegacy{};
  DockOutputLayer* L = dock_popup_margin_reference_layer(app);
  if (!L && app.layerSurface && app.dockLayerOutput) {
    stackLegacy.surface = app.surface;
    stackLegacy.layer = app.layerSurface;
    stackLegacy.wlOut = app.dockLayerOutput;
    stackLegacy.configuredWidth = app.configuredWidth;
    stackLegacy.configuredHeight = app.configuredHeight;
    L = &stackLegacy;
  }
  if (!L || !L->wlOut) {
    log_dbg("[dock-popup] create_layer_surface FAIL: no reference layer"
            " L=%p layerSurface=%p dockLayerOutput=%p dockLayers=%zu\n",
            (void*)L, (void*)app.layerSurface, (void*)app.dockLayerOutput, app.dockLayers.size());
    return false;
  }

  int marginLeft = 0;
  int marginBottom = 0;
  const bool marginOk = dock_popup_compute_layer_margins(app, L, anchorLocalX, &marginLeft, &marginBottom);
  if (!marginOk) {
    log_dbg("[dock-popup] create_layer_surface FAIL: compute_layer_margins returned false\n");
    return false;
  }

  return dock_popup_create_layer_surface_ex(app, anchorLocalX, L->wlOut, marginLeft, marginBottom);
}

void popup_close(DockApp& app) {
   
  eh::shell::launchpad::Host::close_before_dock_popup(app);
  if (!app.popupOpen) return;
  const DockApp::PopupKind closingKind = app.popupKind;
  dock_popup_destroy_caret_frame(app);
  dock_main_layers_set_keyboard_interactivity(app, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  for (auto& up : app.dockLayers) {
    if (up && up->surface) wl_surface_commit(up->surface);
  }
  if (app.dockLayers.empty() && app.surface) wl_surface_commit(app.surface);

  app.popupOpen = false;
  app.popupKind = DockApp::PopupKind::None;
  app.popupMarginOverride = {};
  app.popupConfiguredW = 0;
  app.popupConfiguredH = 0;
  app.popupLastMarginBottom = -1;
  app.popupLastMarginLeft = -1;
  std::cerr << "[dock-popup] popup_close: reset cfgW/H=0 kind=" << static_cast<int>(closingKind) << "\n";
  app.appMenuGrabSerial = 0;
  if (dock_popup_kind_uses_app_drawer_ui(closingKind)) {
    g_dock_appmenu_trace_last_zone = -999;
    if (eh_app_drawer_debug_level() >= 1) {
      eh::shell::dock::app_drawer::trace_line(1, "dock", "popup_close app_drawer_ui (state cleared)");
    }
  }
  app.spotlightQuery.clear();
  app.spotlightHits.clear();
  app.spotlightSel = -1;
  // Drop the rendered app-menu list cache (a full menu-size ARGB surface,
  // ~2-3MB) so it does not stay resident between popup sessions. It is
  // rebuilt on the next open when needed.
  if (app.appMenuListCache) {
    cairo_surface_destroy(app.appMenuListCache);
    app.appMenuListCache = nullptr;
    app.appMenuListCacheW = 0;
    app.appMenuListCacheH = 0;
    app.appMenuListCacheViewMode = -1;
  }
  app.appMenuQuery.clear();
  app.appMenuHits.clear();
  app.appMenuSel = -1;
  app.appMenuScrollPx = 0;
  app.appMenuSearchFocused = true;
  app.appMenuPowerConfirmOpen = false;
  app.appMenuPowerConfirmIdx = -1;
  app.appMenuHoverRow = -1;
  app.appMenuCategoryHoverIdx = -1;
  app.appMenuRowCtxOpen = false;
  app.appMenuRowCtxAnchorRow = -1;
  app.appMenuRowCtxMenuX = 0;
  app.appMenuRowCtxMenuY = 0;
  app.appMenuRowCtxHoverItem = -1;
  app.appMenuRowCtxPinnedDock = false;
  app.appMenuRowCtxPinnedStart = false;
  app.appMenuRowCtxPinnedDrawer = false;
  app.appMenuRowCtxItemCount = 3;
  app.appMenuDrawerPinHoverIdx = -1;
  app.appMenuPinCtxOpen = false;
  app.appMenuPinCtxAnchorIdx = -1;
  app.appMenuPinCtxMenuX = 0;
  app.appMenuPinCtxMenuY = 0;
  app.appMenuPinCtxHoverItem = -1;
  app.appMenuPinCtxPinnedDock = false;
  app.popupCaretBlinkHalf = static_cast<uint64_t>(-1);
  app.popupHoverItem = -1;

  if (app.popupSurface) {
    wl_surface_attach(app.popupSurface, nullptr, 0, 0);
    wl_surface_damage_buffer(app.popupSurface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(app.popupSurface);
  }

  destroy_popup_buffer(app);
  if (app.popupLayerSurface) {
    zwlr_layer_surface_v1_destroy(app.popupLayerSurface);
    app.popupLayerSurface = nullptr;
  }
  if (app.popupSurface) {
    wl_surface_destroy(app.popupSurface);
    app.popupSurface = nullptr;
  }
  app.popupItems.clear();
  app.popupService.clear();
  app.popupPath.clear();
  app.popupMenuPath.clear();
  app.popupAppKey.clear();
  app.popupAppChosenHandle = nullptr;
  app.popupAppWindows.clear();
  app.popupAppDesktopActions.clear();
  app.popupAppDesktopExec.clear();
  app.ccState.mixerExpanded = false;
  app.ccState.outputDevicesExpanded = false;
  app.ccState.outputDevicesPendingSink.clear();
  app.ccState.outputDevicesIgnoreUntilMs = 0;
  app.ccState.inputDevicesExpanded = false;
  app.ccState.inputDevicesPendingSource.clear();
  app.ccState.inputDevicesIgnoreUntilMs = 0;
  app.ccState.networkExpanded = false;
  app.ccState.wifiPasswordPrompt = false;
  app.ccState.wifiPendingSsid.clear();
  app.ccState.wifiPassword.clear();
  app.ccState.wifiLastError.clear();
  app.ccState.wifiIgnoreUntilMs = 0;
  app.ccState.openBenchStartMs = 0;
  app.ccState.openBenchLoggedFirstConfigure = false;
  app.ccState.openBenchLoggedFirstPaint = false;
  app.weatherInstanceId.clear();
}

static std::vector<DockApp::PopupMenuItem> dbusmenu_fetch_top_level(DockApp& app, const DockApp::TrayItem& ti, const std::string& menuPath) {
   
  std::vector<DockApp::PopupMenuItem> out;
  if (!app.trayBus || menuPath.empty()) return out;
  try {
    auto menu = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti.service}, sdbus::ObjectPath{menuPath});

    using Props = std::map<std::string, sdbus::Variant>;
    using Layout = sdbus::Struct<int32_t, Props, std::vector<sdbus::Variant>>;
    uint32_t revision = 0;
    Layout root{};

    menu->callMethod("GetLayout")
      .onInterface("com.canonical.dbusmenu")
      .withArguments(int32_t{0}, int32_t{1}, std::vector<std::string>{"label", "enabled", "visible", "type"})
      .storeResultsTo(revision, root);

    (void)revision;

    const auto& children = std::get<2>(root);
    out.reserve(children.size());
    for (const auto& vChild : children) {
      Layout child = vChild.get<Layout>();
      const int32_t id = std::get<0>(child);
      const auto& props = std::get<1>(child);

      auto getStr = [&](const char* k) -> std::string {
        auto it = props.find(k);
        if (it == props.end()) return {};
        try {
          return it->second.get<std::string>();
        } catch (...) {
          return {};
        }
      };
      auto getBool = [&](const char* k, bool def) -> bool {
        auto it = props.find(k);
        if (it == props.end()) return def;
        try {
          if (it->second.containsValueOfType<bool>()) return it->second.get<bool>();
          if (it->second.containsValueOfType<int32_t>()) return it->second.get<int32_t>() != 0;
          return def;
        } catch (...) {
          return def;
        }
      };

      const std::string type = getStr("type");
      if (type == "separator") {
        out.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});
        continue;
      }

      const bool visible = getBool("visible", true);
      if (!visible) continue;

      std::string label = getStr("label");

      label.erase(std::remove(label.begin(), label.end(), '_'), label.end());
      const bool enabled = getBool("enabled", true);
      if (label.empty()) label = "(unnamed)";

      out.push_back(DockApp::PopupMenuItem{.label = label, .id = id, .enabled = enabled});
    }
  } catch (const std::exception& e) {
    eh::shell_log::dbus_tray("dbusmenu GetLayout failed: ", e.what());
  }
  return out;
}

void popup_open_for_tray(DockApp& app, const DockApp::TrayItem& ti, int anchorX, int anchorY, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Tray;

  const std::string menuPath = ti.proxy ? eh::shell::dock::tray_menu::dock_get_menu_object_path(*ti.proxy) : std::string{};
  if (menuPath.empty()) {
    std::cout << "[tray] menu: item has no DBusMenu 'Menu' property\n";
    return;
  }

  auto items = dbusmenu_fetch_top_level(app, ti, menuPath);
  if (items.empty()) {
    return;
  }

  {
    const double us = dock_ui_scale(app.settings);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    double maxw = 0;
    for (const auto& it : items) {
      if (it.id < 0) continue;
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, it.label.c_str(), &ex);
      maxw = std::max(maxw, std::max(ex.x_advance, ex.x_bearing + ex.width));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    const int w = std::clamp(static_cast<int>(16.0 * us + maxw + 16.0 * us), static_cast<int>(160.0 * us), static_cast<int>(600.0 * us));
    int itemCount = 0, sepCount = 0;
    for (const auto& it : items) {
      if (it.id < 0) ++sepCount;
      else ++itemCount;
    }
    const int h = static_cast<int>(4.0 * us + 26.0 * us * itemCount + 13.0 * us * sepCount + 4.0 * us);
    app.popupW = w;
    app.popupH = h;
  }

  app.popupItems = std::move(items);
  app.popupOpen = true;
  app.popupService = ti.service;
  app.popupPath = ti.path;
  app.popupMenuPath = menuPath;
  app.popupAnchorX = anchorX;
  app.popupAnchorY = anchorY;

  if (!app.configured) {
    if (dock_foreign_toplevel_debug_enabled()) {
      std::cerr << "[dock] popup suppressed: layer-surface not configured yet\n";
    }
    return;
  }

  (void)serial;
  (void)anchorY;
  if (!app.compositor || !app.layerShell) {
    popup_close(app);
    return;
  }
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_spotlight(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Spotlight;
  app.popupW = kSpotlightPopupW();
  app.popupH = spotlight_popup_total_height();
  app.popupAnchorX = anchorX;
  app.popupAnchorY = 0;
  app.popupItems.clear();
  app.popupOpen = true;
  app.popupCaretBlinkHalf = static_cast<uint64_t>(-1);

  if (!app.configured) {
    if (dock_foreign_toplevel_debug_enabled()) {
      std::cerr << "[dock] spotlight popup suppressed: layer-surface not configured yet\n";
    }
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }

  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }

  if (zwlr_layer_surface_v1* pl = dock_popup_parent_layer_surface(app)) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(pl, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  }
  if (wl_surface* ps = dock_popup_parent_wl_surface(app)) wl_surface_commit(ps);

  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }

  eh::shell::dock::dock_spotlight_refresh_results(app);

  wl_display_flush(app.display);
}

void popup_open_control_center(DockApp& app, int anchorX, uint32_t serial) {
  
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.ccState.openBenchStartMs = eh::shell::monotonic_ms();
  app.ccState.openBenchLoggedFirstConfigure = false;
  app.ccState.openBenchLoggedFirstPaint = false;
  {
    const auto [savedSink, savedSource] = eh::shell::control_center::cc_load_audio_prefs();
    if (!savedSink.empty()) {
      eh::shell::dock_slot_hooks::control_center_set_default_sink(savedSink);
      app.ccState.outputDevicesPendingSink = savedSink;
      app.ccState.outputDevicesIgnoreUntilMs = eh::shell::monotonic_ms() + 900;
    }
    if (!savedSource.empty()) {
      eh::shell::dock_slot_hooks::control_center_set_default_source(savedSource);
      app.ccState.inputDevicesPendingSource = savedSource;
      app.ccState.inputDevicesIgnoreUntilMs = eh::shell::monotonic_ms() + 900;
    }
  }
  app.popupKind = DockApp::PopupKind::ControlCenter;
  app.popupW = kControlCenterPopupW();
  app.popupH = static_cast<int>(std::ceil(control_center_popup_height()));
  app.popupAnchorX = anchorX;
  app.popupAnchorY = 0;
  app.popupItems.clear();
  app.popupOpen = true;

  if (!app.configured) {
    app.ccState.openBenchStartMs = 0;
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  if (!app.compositor || !app.layerShell) {
    app.ccState.openBenchStartMs = 0;
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }

  if (zwlr_layer_surface_v1* pl = dock_popup_parent_layer_surface(app)) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(pl, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  }
  if (wl_surface* ps = dock_popup_parent_wl_surface(app)) wl_surface_commit(ps);

  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    app.ccState.openBenchStartMs = 0;
    popup_close(app);
    return;
  }

  wl_display_flush(app.display);
  if (eh_cc_open_bench() && app.ccState.openBenchStartMs != 0) {
    const uint64_t ms = eh::shell::monotonic_ms() - app.ccState.openBenchStartMs;
    std::cerr << "[control-center] open: setup_through_roundtrip_ms=" << ms << "\n";
  }
}

void dock_destroy_app_menu_host_surfaces(DockApp& app) {
   
  app.appMenuHostBuf.destroy();
  if (app.appMenuHostLayerSurface) zwlr_layer_surface_v1_destroy(app.appMenuHostLayerSurface);
  if (app.appMenuHostSurface) wl_surface_destroy(app.appMenuHostSurface);
  app.appMenuHostLayerSurface = nullptr;
  app.appMenuHostSurface = nullptr;
  app.appMenuHostConfigured = false;
  app.appMenuHostWlOutput = nullptr;
}

static void app_drawer_ui_spawn_popup(DockApp& app) {
   
  if (!app.popupOpen || !dock_popup_kind_uses_app_drawer_ui(app.popupKind)) return;
  if (!app.compositor || !app.layerShell) return;
  if (app.popupSurface) return;

  (void)app.appMenuGrabSerial;
  if (eh_app_drawer_debug_level() >= 1) {
    eh::shell::dock::app_drawer::trace_line(1, "dock", "app_drawer_ui_spawn_popup create_layer anchorX=" + std::to_string(app.popupAnchorX));
  }

  eh::shell::dock::app_drawer::invalidate_desktop_entries_cache();
  eh_app_drawer_refresh_hits(app);
  if (!dock_popup_create_layer_surface(app, app.popupAnchorX)) {
    if (eh_app_drawer_debug_level() >= 1) {
      eh::shell::dock::app_drawer::trace_line(1, "dock", "app_drawer_ui_spawn_popup dock_popup_create_layer_surface FAILED → popup_close");
    }
    popup_close(app);
    return;
  }

  if (eh_app_drawer_debug_level() >= 1) {
    eh::shell::dock::app_drawer::trace_line(1, "dock", "app_drawer_ui_spawn_popup ok surface+layer hits=" + std::to_string(app.appMenuHits.size()));
  }
  wl_display_flush(app.display);
}

void popup_open_app_menu(DockApp& app, int anchorX, uint32_t serial) {
   
  if (eh_app_drawer_debug_level() >= 1) {
    eh::shell::dock::app_drawer::trace_line(1, "dock", "popup_open_app_menu anchorX=" + std::to_string(anchorX) + " serial=" + std::to_string(serial));
  }
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::AppMenu;
  app.popupW = eh_app_drawer_popup_width();
  app.popupH = eh_app_drawer_popup_height();
  app.popupAnchorX = anchorX;
  app.popupAnchorY = 0;
  app.popupItems.clear();
  app.appMenuGrabSerial = serial;
  app.popupOpen = true;
  app.appMenuSearchFocused = true;
  app.popupCaretBlinkHalf = static_cast<uint64_t>(-1);

  if (!app.configured) {
    if (dock_foreign_toplevel_debug_enabled()) {
      std::cerr << "[dock] app menu suppressed: layer-surface not configured yet\n";
    }
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    app.appMenuGrabSerial = 0;
    return;
  }

  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    app.appMenuGrabSerial = 0;
    return;
  }

  if (zwlr_layer_surface_v1* pl = dock_popup_parent_layer_surface(app)) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(pl, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  }
  if (wl_surface* ps = dock_popup_parent_wl_surface(app)) wl_surface_commit(ps);

  (void)serial;
  app_drawer_ui_spawn_popup(app);
}

static void log_dbg(const char*, ...) {}

void popup_open_power_confirm(DockApp& app, int powerIdx) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::PowerConfirm;
  app.popupW = 1;
  app.popupH = 1;
  app.popupAnchorX = 0;
  app.popupAnchorY = 0;
  app.popupItems.clear();
  app.appMenuPowerConfirmOpen = true;
  app.appMenuPowerConfirmIdx = powerIdx;
  app.appMenuPowerConfirmStartMs = eh::shell::monotonic_ms();
  app.popupOpen = true;

  if (!app.configured) {
    log_dbg("[dock-popup] power_confirm FAIL: !app.configured\n");
    app.popupOpen = false;
    app.appMenuPowerConfirmOpen = false;
    app.appMenuPowerConfirmIdx = -1;
    return;
  }

  if (!app.compositor || !app.layerShell) {
    log_dbg("[dock-popup] power_confirm FAIL: compositor=%p layerShell=%p\n", (void*)app.compositor, (void*)app.layerShell);
    app.popupOpen = false;
    app.appMenuPowerConfirmOpen = false;
    app.appMenuPowerConfirmIdx = -1;
    return;
  }

  if (zwlr_layer_surface_v1* pl = dock_popup_parent_layer_surface(app)) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(pl, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  }
  if (wl_surface* ps = dock_popup_parent_wl_surface(app)) wl_surface_commit(ps);

  if (!dock_popup_create_layer_surface(app, 0)) {
    log_dbg("[dock-popup] power_confirm FAIL: dock_popup_create_layer_surface returned false\n"
            "  dockLayers=%zu pointerDockLayerIdx=%d dockLayerOutput=%p layerSurface=%p configured=%d\n",
            app.dockLayers.size(), app.pointerDockLayerIdx, (void*)app.dockLayerOutput,
            (void*)app.layerSurface, (int)app.configured);
    app.popupOpen = false;
    app.appMenuPowerConfirmOpen = false;
    app.appMenuPowerConfirmIdx = -1;
    return;
  }
  app.pointerSurface = app.popupSurface;
  wl_display_flush(app.display);
}

static std::string desktop_exec_strip_field_codes(std::string exec) {
   
  std::string out;
  out.reserve(exec.size());
  for (size_t i = 0; i < exec.size(); i++) {
    if (exec[i] == '%' && i + 1 < exec.size()) {
      const char c = exec[i + 1];

      if (c == '%') out.push_back('%');

      i++;
      continue;
    }
    out.push_back(exec[i]);
  }
  return trim(out);
}

void launch_exec_command(const std::string& execLine) {
   
  const std::string cmd = desktop_exec_strip_field_codes(execLine);
  if (cmd.empty()) return;

  const pid_t intermediate = ::fork();
  if (intermediate < 0) return;
  if (intermediate > 0) {
    ::waitpid(intermediate, nullptr, 0);
    return;
  }

  if (::setsid() < 0) ::_exit(1);

  const pid_t worker = ::fork();
  if (worker < 0) ::_exit(1);
  if (worker > 0) ::_exit(0);

  const int devnull = ::open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    ::dup2(devnull, STDIN_FILENO);
    ::dup2(devnull, STDOUT_FILENO);
    ::dup2(devnull, STDERR_FILENO);
    ::close(devnull);
  }

  std::vector<char> arg_lc(cmd.begin(), cmd.end());
  arg_lc.push_back('\0');

  // Launch through the per-user systemd manager whenever possible so the app's
  // process materializes in the user-writable (delegated) cgroup subtree —
  // user@1000.service/app.slice. VramBoostManager needs that to protect the
  // app's VRAM via dmem.min without running the shell as root. `--scope` keeps
  // the caller's full environment (games rely on it). As root the DE bypasses
  // this (root can write any cgroup already).
  if (::geteuid() != 0) {
    const std::string unit = "eh-app-" + std::to_string(static_cast<long>(::getpid()));
    std::vector<std::string> sdr = {"systemd-run", "--user", "--scope", "--collect",
                                    "--quiet", "--slice=app.slice", "--unit=" + unit,
                                    "--", "/bin/sh", "-lc", cmd};
    std::vector<char*> argvS;
    argvS.reserve(sdr.size() + 1);
    for (auto& a : sdr) argvS.push_back(a.data());
    argvS.push_back(nullptr);
    ::execvp("systemd-run", argvS.data());
  }

  char argv0[] = "sh";
  char argv1[] = "-lc";
  char* argv[] = {argv0, argv1, arg_lc.data(), nullptr};

  ::execvp("/bin/sh", argv);
  ::_exit(127);
}

void popup_open_calendar(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Calendar;
  app.popupW = eh::shell::dock::popup::calendar::kCalendarPopupW;
  app.popupH = eh::shell::dock::popup::calendar::kCalendarPopupH;
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;

  std::time_t t = std::time(nullptr);
  std::tm local{};
  localtime_r(&t, &local);
  app.calSelectedDate = local;
  app.calDisplayDate = local;
  app.calDisplayDate.tm_mday = 1;
  app.calDisplayDate.tm_hour = 0;
  app.calDisplayDate.tm_min = 0;
  app.calDisplayDate.tm_sec = 0;
  app.calDisplayDate.tm_isdst = -1;
  std::mktime(&app.calDisplayDate);

  if (!app.configured) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_weather(DockApp& app, int anchorX, const std::string& instanceId, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Weather;
  app.popupW = eh::shell::dock::popup::weather::kWeatherPopupW;
  app.popupH = eh::shell::dock::popup::weather::kWeatherPopupH;
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;
  app.weatherInstanceId = instanceId;

  if (!app.configured) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_volume_mixer(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  const double us = dock_ui_scale(app.settings);
  app.popupKind = DockApp::PopupKind::VolumeMixer;
  app.popupW = eh::shell::dock::popup::volume_mixer::kVolumeMixerPopupW;
  app.popupH = static_cast<int>(440.0 * us);
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;

  if (!app.configured) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_media_player(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::MediaPlayer;
  app.popupW = eh::shell::dock::popup::media_player::kMediaPlayerPopupW;
  app.popupH = eh::shell::dock::popup::media_player::kMediaPlayerPopupH;
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;

  if (!app.configured) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  if (!app.compositor || !app.layerShell) {
    app.popupOpen = false;
    app.popupKind = DockApp::PopupKind::None;
    return;
  }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_vpn(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Vpn;
  app.popupW = eh::shell::dock::popup::vpn::kVpnPopupW;
  {
    auto& nm = eh::net::NetworkManagerService::instance();
    const auto& st = nm.state();
    app.popupH = eh::shell::dock::popup::vpn::vpn_popup_height(static_cast<int>(st.vpnConnections.size()));
  }
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;
  if (!app.configured) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  if (!app.compositor || !app.layerShell) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) { popup_close(app); return; }
  wl_display_flush(app.display);
}

void popup_open_battery(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Battery;
  app.popupW = eh::widgets::kBatteryPopupW;
  app.popupH = eh::widgets::battery_popup_height();
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;
  if (!app.configured) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  if (!app.compositor || !app.layerShell) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) { popup_close(app); return; }
  wl_display_flush(app.display);
}

void popup_open_bluetooth(DockApp& app, int anchorX, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Bluetooth;
  app.popupW = eh::widgets::kBluetoothPopupW;
  app.popupH = eh::widgets::bluetooth_popup_height();
  app.popupAnchorX = anchorX;
  app.popupItems.clear();
  app.popupOpen = true;
  if (!app.configured) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  if (!app.compositor || !app.layerShell) { app.popupOpen = false; app.popupKind = DockApp::PopupKind::None; return; }
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) { popup_close(app); return; }
  wl_display_flush(app.display);
}

void popup_open_for_trash(DockApp& app, int anchorX, int anchorY, uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::Trash;
  app.popupAnchorX = anchorX;
  app.popupAnchorY = anchorY;
  {
    const double us = dock_ui_scale(app.settings);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    double maxw = 0;
    for (const auto& it : app.popupItems) {
      if (it.id < 0) continue;
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, it.label.c_str(), &ex);
      maxw = std::max(maxw, std::max(ex.x_advance, ex.x_bearing + ex.width));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    app.popupW = std::clamp(static_cast<int>(16.0 * us + maxw + 16.0 * us), static_cast<int>(160.0 * us), static_cast<int>(600.0 * us));
    int itemCount = 0, sepCount = 0;
    for (const auto& it : app.popupItems) {
      if (it.id < 0) ++sepCount;
      else ++itemCount;
    }
    app.popupH = static_cast<int>(4.0 * us + 26.0 * us * itemCount + 13.0 * us * sepCount + 4.0 * us);
  }
  app.popupOpen = true;
  if (!app.compositor || !app.layerShell) return;
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}

void popup_open_for_app(DockApp& app,
                               const std::string& appKey,
                               zwlr_foreign_toplevel_handle_v1* chosenHandle,
                               int anchorX,
                               int anchorY,
                               uint32_t serial) {
   
  popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  app.popupKind = DockApp::PopupKind::App;
  app.popupAppKey = appKey;
  app.popupAppChosenHandle = chosenHandle;

  std::string lookupId = appKey;
  if (chosenHandle) {
    for (const auto& tl : app.toplevels) {
      if (tl.handle == chosenHandle) {
        lookupId = tl.appId;
        break;
      }
    }
  }
  if (!lookupId.empty() && lookupId != eh::shell::kSlotKeySettings) {
    if (auto desktop = find_desktop_file_for_appid(lookupId)) {
      if (auto info = read_desktop_entry_info(*desktop)) {
        app.popupAppDesktopExec = info->exec;
        app.popupAppDesktopActions = info->actions;
      }
    }
  }

  std::vector<DockApp::PopupMenuItem> items;
  items.reserve(16);

  for (size_t i = 0; i < app.popupAppDesktopActions.size(); i++) {
    const auto& a = app.popupAppDesktopActions[i];
    const std::string name = a.name.empty() ? a.id : a.name;
    items.push_back(DockApp::PopupMenuItem{
      .label = name,
      .id = static_cast<int32_t>(1000 + i),
      .enabled = !a.exec.empty(),
    });
  }
  if (!items.empty()) {
    items.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});
  }

  const bool pinnedNow = dock_pin_identity_list_contains_toplevel_key(app, app.popupAppKey);
  items.push_back(DockApp::PopupMenuItem{
    .label = pinnedNow ? "Unpin" : "Pin",
    .id = static_cast<int32_t>(AppPopupAction::PinToggle),
    .enabled = (app.popupAppKey != "unknown" && app.popupAppKey != eh::shell::kSettingsAppId && app.popupAppKey != eh::shell::kSlotKeySettings),
  });
  items.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});

  if (chosenHandle) {
    items.push_back(DockApp::PopupMenuItem{.label = "Minimize window", .id = static_cast<int32_t>(AppPopupAction::MinimizeOne), .enabled = true});
    items.push_back(DockApp::PopupMenuItem{.label = "Toggle maximize", .id = static_cast<int32_t>(AppPopupAction::ToggleMaximize), .enabled = true});
    items.push_back(DockApp::PopupMenuItem{.label = "Toggle fullscreen", .id = static_cast<int32_t>(AppPopupAction::ToggleFullscreen), .enabled = true});
    items.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});
  }

  for (size_t i = 0; i < app.popupAppWindows.size(); i++) {
    const auto& w = app.popupAppWindows[i];
    const std::string label = w.second.empty() ? "(untitled)" : w.second;
    items.push_back(DockApp::PopupMenuItem{.label = label, .id = static_cast<int32_t>(2000 + i), .enabled = (w.first != nullptr)});
  }
  if (!app.popupAppWindows.empty()) {
    items.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});
  }

  if (!app.popupAppDesktopExec.empty()) {
    items.push_back(DockApp::PopupMenuItem{.label = "New Window", .id = 3000, .enabled = true});
    items.push_back(DockApp::PopupMenuItem{.label = "", .id = -1, .enabled = false});
  }

  items.push_back(DockApp::PopupMenuItem{.label = "Close window", .id = static_cast<int32_t>(AppPopupAction::CloseOne), .enabled = (chosenHandle != nullptr)});
  items.push_back(DockApp::PopupMenuItem{.label = "Close all windows", .id = static_cast<int32_t>(AppPopupAction::CloseAll), .enabled = true});

  {
    const double us = dock_ui_scale(app.settings);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    double maxw = 0;
    for (const auto& it : items) {
      if (it.id < 0) continue;
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, it.label.c_str(), &ex);
      maxw = std::max(maxw, std::max(ex.x_advance, ex.x_bearing + ex.width));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    const int w = std::clamp(static_cast<int>(16.0 * us + maxw + 16.0 * us), static_cast<int>(160.0 * us), static_cast<int>(600.0 * us));
    int itemCount = 0, sepCount = 0;
    for (const auto& it : items) {
      if (it.id < 0) ++sepCount;
      else ++itemCount;
    }
    const int h = static_cast<int>(4.0 * us + 26.0 * us * itemCount + 13.0 * us * sepCount + 4.0 * us);
    app.popupW = w;
    app.popupH = h;
  }

  app.popupItems = std::move(items);
  app.popupOpen = true;
  app.popupAnchorX = anchorX;
  app.popupAnchorY = anchorY;

  if (!app.compositor || !app.layerShell) return;
  (void)serial;
  if (!dock_popup_create_layer_surface(app, anchorX)) {
    popup_close(app);
    return;
  }
  wl_display_flush(app.display);
}
