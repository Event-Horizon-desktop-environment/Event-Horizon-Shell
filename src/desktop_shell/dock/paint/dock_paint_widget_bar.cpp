#include "desktop_shell/dock/paint/dock_paint_widget_bar.hpp"

#include "desktop_shell/dock/layout/dock_layout_shared.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/fs/trash_state.hpp"
#include "desktop_shell/shared/widgets/workspace_strip.hpp"
#include "desktop_shell/dock/paint/dock_strip_geometry.hpp"
#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/widgets/shared/shared_slot_paint.hpp"
#include "desktop_shell/launchpad/host/launchpad_host.hpp"
#include "services/tray/filter/tray_env_filter.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using eh::shell::dock::lr_strip_layout;
using eh::shell::dock::widget_strip_h_scale;

namespace {
constexpr double kDockRunIndicatorGapPx = 0.0;
constexpr double kDockPressIconScale = 0.92;
constexpr double kDockPressLiftPx = 2.0;

// Request at least kHiResIconPx so the cached raster isn't capped below
// what a scaled/HiDPI dock actually needs on screen — see icon_cache.hpp.
const eh::icons::IconEntry* paint_cached_app_icon(DockApp& app, const std::string& key) {
  return app.icons.app_icon(key, eh::icons::kHiResIconPx);
}
const eh::icons::IconEntry* paint_cached_tray_icon(DockApp& app, const std::string& name) { return app.icons.tray_icon(name); }
}

void dock_paint_widget_bar(DockApp& app, cairo_t* cr, double x, double y, double boxW, double boxH,
                            const std::vector<std::string>& leftW, const std::vector<std::string>& centerW,
                            const std::vector<std::string>& rightW, bool isPanel, int panelLayoutMode,
                            bool* out_media_marquee_wants_frame, size_t paint_layer_idx,
                            std::vector<DockWidgetHit>* out_hits,
                            int taskbarHoverSlot, int taskbarPressedSlot, double taskbarHoverLiftPx,
                            bool* out_media_progress_tick) {
   
  (void)panelLayoutMode;
  eh::widgets::slot_pill_style::g_opacityScale = static_cast<double>(std::clamp(app.settings.slotPillOpacity, 0, 100)) / 100.0;
  const bool ptr_on_dock = dock_pointer_on_any_dock_layer(app);
  const bool input_this_layer =
      ptr_on_dock && (app.dockLayers.size() <= 1 || paint_layer_idx == app.pointerDockLayerIdx);
  const eh::config::ShellAppearance apDockBar = eh::config::shell_config_snapshot().appearance;
  const eh::config::ChromePaintColors mcDockBar = eh::config::derived_chrome_colors(apDockBar);
  {
    const double baseR = mcDockBar.dockFillR * 0.6 + mcDockBar.accentR * 0.15;
    const double baseG = mcDockBar.dockFillG * 0.6 + mcDockBar.accentG * 0.15;
    const double baseB = mcDockBar.dockFillB * 0.6 + mcDockBar.accentB * 0.15;
    eh::widgets::slot_pill_style::g_pillR = baseR;
    eh::widgets::slot_pill_style::g_pillG = baseG;
    eh::widgets::slot_pill_style::g_pillB = baseB;
  }
  eh::widgets::slot_pill_style::g_hoverAccentR = mcDockBar.accentR;
  eh::widgets::slot_pill_style::g_hoverAccentG = mcDockBar.accentG;
  eh::widgets::slot_pill_style::g_hoverAccentB = mcDockBar.accentB;
  const bool dockBarMatugen = apDockBar.anyPaletteActive();
  auto rounded_rect = [&](double rx, double ry, double rw, double rh, double r) {
    const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  struct Slot {
    using Kind = eh::widgets::shared_slot_paint::SlotKind;
    Kind kind = Kind::App;
    std::string key;
    std::string iconId;
    std::uint64_t chosenSerial = 0;
    bool anyActivated = false;
    bool isPinned = false;
  };

  const auto runningSnap = eh::shell::shared::build_running_snapshot(app.toplevels, app.appFirstSeenSerial, app.settings.dockGroupApps);
  const auto& runningGroups = runningSnap.groups;
  const std::uint64_t settingsChosenSerial = runningSnap.settingsChosenSerial;
  bool settingsActivated = runningSnap.settingsActivated;

  std::vector<DockApp::TrayItem> traySnap;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySnap = app.trayItems;
  }
  std::sort(traySnap.begin(), traySnap.end(), [](const DockApp::TrayItem& a, const DockApp::TrayItem& b) {
    if (a.service != b.service) return a.service < b.service;
    return a.path < b.path;
  });
  traySnap.erase(std::remove_if(traySnap.begin(), traySnap.end(), [](const DockApp::TrayItem& t) {
                     return eh::shell::dock::tray::dock_tray_item_hidden_by_env(t.id, t.title, t.service, t.path);
                   }),
                 traySnap.end());

  auto is_pinned = [&](const std::string& k) { return dock_pin_identity_list_contains_toplevel_key(app, k); };

  const eh::config::ShellConfig& scBarPaint = eh::config::shell_config_snapshot();

  std::vector<Slot> pinnedSlots;
  std::vector<Slot> runningSlots;
  std::vector<Slot> traySlots;
  pinnedSlots.reserve(app.settings.pinnedApps.size());
  runningSlots.reserve(runningGroups.size());
  traySlots.reserve(traySnap.size());
  Slot settingsSlot;
  settingsSlot.kind = Slot::Kind::Settings;
  settingsSlot.key = eh::shell::kSlotKeySettings;
  settingsSlot.iconId = "";
  settingsSlot.chosenSerial = settingsChosenSerial;
  settingsSlot.anyActivated = settingsActivated;

  Slot spotlightSlot;
  spotlightSlot.kind = Slot::Kind::Spotlight;
  spotlightSlot.key = eh::shell::kSlotKeySpotlight;
  spotlightSlot.iconId = "";
  spotlightSlot.chosenSerial = 0;
  spotlightSlot.anyActivated = false;

  Slot appMenuSlot;
  appMenuSlot.kind = Slot::Kind::AppMenu;
  appMenuSlot.key = eh::shell::kSlotKeyAppMenu;
  appMenuSlot.iconId = "";
  appMenuSlot.chosenSerial = 0;
  appMenuSlot.anyActivated = false;

  Slot appDrawerSlot;
  appDrawerSlot.kind = Slot::Kind::AppDrawer;
  appDrawerSlot.key = eh::shell::kSlotKeyAppDrawer;
  appDrawerSlot.iconId = "";
  appDrawerSlot.chosenSerial = 0;
  appDrawerSlot.anyActivated = false;

  for (const auto& pRaw : dock_pinned_apps_source_for_layout(app)) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    Slot s;
    s.kind = Slot::Kind::App;
    s.key = p;
    s.iconId = pRaw;
    s.isPinned = true;
    dock_pinned_merge_running_slot(app, runningGroups, p, s.chosenSerial, s.anyActivated, &s.iconId);
    pinnedSlots.push_back(std::move(s));
  }

  for (const auto& rg : runningGroups) {
    if (is_pinned(rg.pinMatchKey)) continue;
    Slot s;
    s.kind = Slot::Kind::App;
    s.key = rg.key;
    s.iconId = rg.iconId;
    s.chosenSerial = rg.chosenSerial;
    s.anyActivated = rg.anyActivated;
    s.isPinned = false;
    runningSlots.push_back(std::move(s));
  }

  for (const auto& ti : traySnap) {
    Slot s;
    s.kind = Slot::Kind::Tray;
    s.key.clear();
    s.key.reserve(8 + ti.service.size() + ti.path.size());
    s.key += eh::shell::kSlotKeyTray;
    s.key += ti.service;
    s.key += ti.path;
    traySlots.push_back(std::move(s));
  }

  auto append_widget = [&](std::vector<Slot>& out, const std::string& wid) {
    if (!eh::config::widget_instance_enabled(scBarPaint, wid)) return;
    if (dock_strip_widget_blocked(app.settings, wid)) return;
    if (wid == "pinned_apps") {
      for (const auto& s : pinnedSlots) out.push_back(s);
    } else if (wid == "running_apps") {
      for (const auto& s : runningSlots) out.push_back(s);
    } else if (eh::config::widget_token_is_system_tray(wid)) {
      for (const auto& s : traySlots) out.push_back(s);
    } else if (wid == "settings_button") {
      out.push_back(settingsSlot);
    } else if (wid == "distro_spotlight") {
      out.push_back(spotlightSlot);
    } else if (wid == "app_menu") {
      out.push_back(appMenuSlot);
    } else if (eh::config::widget_implementation_type(wid) == "app_drawer") {
      out.push_back(appDrawerSlot);
    } else if (eh::config::widget_implementation_type(wid) == "smenu") {
      Slot sm;
      sm.kind = Slot::Kind::Smenu;
      sm.key = wid;
      out.push_back(std::move(sm));
    } else if (wid == "launchpad" || eh::config::widget_implementation_type(wid) == "launchpad") {
      Slot lp;
      lp.kind = Slot::Kind::Launchpad;
      lp.key = wid;
      out.push_back(std::move(lp));
    } else if (eh::config::widget_implementation_type(wid) == "clock") {
      Slot cs;
      cs.kind = Slot::Kind::Clock;
      cs.key = wid;
      out.push_back(std::move(cs));
    } else if (eh::config::widget_implementation_type(wid) == "weather") {
      Slot ws;
      ws.kind = Slot::Kind::Weather;
      ws.key = wid;
      out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(wid) == "media") {
      Slot ms;
      ms.kind = Slot::Kind::Media;
      ms.key = wid;
      out.push_back(std::move(ms));
    } else if (eh::config::widget_implementation_type(wid) == "workspaces") {
      Slot ws;
      ws.kind = Slot::Kind::Workspaces;
      ws.key = wid;
      out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(wid) == "control_center") {
      Slot cc;
      cc.kind = Slot::Kind::ControlCenter;
      cc.key = wid;
      out.push_back(std::move(cc));
    } else if (wid == "trash" || eh::config::widget_implementation_type(wid) == "trash") {
      Slot ts;
      ts.kind = Slot::Kind::Trash;
      ts.key = wid;
      out.push_back(std::move(ts));
    } else if (eh::config::widget_implementation_type(wid) == "volume_mixer") {
      Slot vm;
      vm.kind = Slot::Kind::VolumeMixer;
      vm.key = wid;
      out.push_back(std::move(vm));
    } else if (eh::config::widget_implementation_type(wid) == "vpn") {
      Slot vpn;
      vpn.kind = Slot::Kind::Vpn;
      vpn.key = wid;
      out.push_back(std::move(vpn));
    } else if (eh::config::widget_implementation_type(wid) == "battery") {
      Slot bat;
      bat.kind = Slot::Kind::Battery;
      bat.key = wid;
      out.push_back(std::move(bat));
    } else if (eh::config::widget_implementation_type(wid) == "bluetooth") {
      Slot bt;
      bt.kind = Slot::Kind::Bluetooth;
      bt.key = wid;
      out.push_back(std::move(bt));
    } else if (eh::config::widget_implementation_type(wid) == "world_clock") {
      Slot wcs;
      wcs.kind = Slot::Kind::WorldClock;
      wcs.key = wid;
      out.push_back(std::move(wcs));
    }
  };

  auto build_section = [&](const std::vector<std::string>& widgets) {
    std::vector<Slot> out;
    out.reserve(widgets.size());
    for (const auto& w : widgets) append_widget(out, w);
    return out;
  };

  std::vector<Slot> left = build_section(leftW);
  std::vector<Slot> center = build_section(centerW);
  std::vector<Slot> right = build_section(rightW);

  std::vector<Slot> all;
  all.reserve(left.size() + center.size() + right.size());
  all.insert(all.end(), left.begin(), left.end());
  all.insert(all.end(), center.begin(), center.end());
  all.insert(all.end(), right.begin(), right.end());

  const double icon = static_cast<double>(dock_effective_icon_px(app.settings));
  const double gap = static_cast<double>(dock_effective_gap_px(app.settings));
  const auto& scPaint = eh::config::shell_config_snapshot();

  auto paint_slot_w = [&](const Slot& s) -> double {
    if (s.kind == Slot::Kind::Clock) {
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, scPaint, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Weather) {
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, scPaint, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Media) {
      const eh::mpris::PlayerSnapshot snap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, scPaint, s.key, icon, boxH, snap);
    }
    if (s.kind == Slot::Kind::Workspaces) {
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, scPaint, s.key, icon, boxH, app.workspaceStrip);
    }
    if (s.kind == Slot::Kind::ControlCenter) {
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(scPaint, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Battery) {
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, scPaint, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Bluetooth) {
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, scPaint, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::WorldClock) {
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, scPaint, s.key, icon, boxH);
    }
    return icon;
  };
  auto gap_after_paint = [&](size_t idx) -> double {
    if (idx + 1 >= all.size()) return 0.0;
    if (all[idx].kind == Slot::Kind::Tray && all[idx + 1].kind == Slot::Kind::Tray) return 0.0;
    if (app.settings.dockPinnedAppsTrayPill && all[idx].kind == Slot::Kind::App && all[idx].isPinned &&
        all[idx + 1].kind == Slot::Kind::App && all[idx + 1].isPinned)
      return 0.0;
    if (all[idx].kind == Slot::Kind::App && all[idx].isPinned && all[idx + 1].kind == Slot::Kind::App &&
        !all[idx + 1].isPinned)
      return std::round(gap * 1.5);
    return gap;
  };

  auto gap_after_slots = [&](const std::vector<Slot>& slots, size_t idx) -> double {
    if (idx + 1 >= slots.size()) return 0.0;
    if (slots[idx].kind == Slot::Kind::Tray && slots[idx + 1].kind == Slot::Kind::Tray) return 0.0;
    if (app.settings.dockPinnedAppsTrayPill && slots[idx].kind == Slot::Kind::App && slots[idx].isPinned &&
        slots[idx + 1].kind == Slot::Kind::App && slots[idx + 1].isPinned)
      return 0.0;
    if (slots[idx].kind == Slot::Kind::App && slots[idx].isPinned && slots[idx + 1].kind == Slot::Kind::App &&
        !slots[idx + 1].isPinned)
      return std::round(gap * 1.5);
    return gap;
  };
  auto section_width = [&](const std::vector<Slot>& slots) -> double {
    double tw = 0.0;
    for (size_t i = 0; i < slots.size(); ++i) {
      tw += paint_slot_w(slots[i]);
      if (i + 1 < slots.size()) tw += gap_after_slots(slots, i);
    }
    return tw;
  };

  const double stripInner = 12.0 * dock_ui_scale(app.settings);
  const double secGapPaint = std::round(gap * 1.5);
  const double lwPaint = section_width(left);
  const double cwPaint = section_width(center);
  const double rwPaint = section_width(right);
  const auto lrPaint = lr_strip_layout(x, boxW, stripInner, lwPaint, cwPaint, rwPaint, secGapPaint);
  const bool use_lr_paint = !isPanel && center.empty() && lrPaint.side_by_side;

  double totalW = 0.0;
  if (use_lr_paint) {
    totalW = lwPaint + secGapPaint + rwPaint;
  } else {
    for (size_t i = 0; i < all.size(); i++) {
      totalW += paint_slot_w(all[i]);
      if (i + 1 < all.size()) totalW += gap_after_paint(i);
    }
  }

  double panelLeftX = x;
  double panelCenterX = x + (boxW - cwPaint) / 2.0;
  double panelRightX = x + boxW - rwPaint;
  bool use_panel_paint = false;
  if (isPanel) {
    use_panel_paint = true;
    const double pad = secGapPaint;
    if (!left.empty() && !right.empty() && panelLeftX + lwPaint + pad > panelRightX) use_panel_paint = false;
    if (use_panel_paint && !left.empty() && !center.empty() && panelLeftX + lwPaint + pad > panelCenterX) use_panel_paint = false;
    if (use_panel_paint && !center.empty() && !right.empty() && panelCenterX + cwPaint + pad > panelRightX) use_panel_paint = false;
    if (use_panel_paint && left.empty() && center.empty() && !right.empty() && rwPaint > boxW) use_panel_paint = false;
    if (use_panel_paint && !left.empty() && center.empty() && right.empty() && lwPaint > boxW) use_panel_paint = false;
    if (use_panel_paint && left.empty() && !center.empty() && right.empty() && cwPaint > boxW) use_panel_paint = false;
  }

  const double iconY = y + (boxH - icon) / 2.0;
  const double startX = use_panel_paint ? 0.0 : (isPanel ? x : x + (boxW - totalW) / 2.0);

  if (eh_dock_settings_debug() && !app.pinDragging) {
    struct SlotRect {
      std::string kind;
      std::string key;
      double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    };
    auto slot_to_rect = [&](const Slot& s, double sx) -> SlotRect {
      SlotRect r;
      r.key = s.key;
      r.x = sx;
      r.y = iconY;
      r.w = paint_slot_w(s);
      r.h = icon;
      switch (s.kind) {
        case Slot::Kind::App: r.kind = "app"; break;
        case Slot::Kind::Tray: r.kind = "tray"; break;
        case Slot::Kind::Settings: r.kind = "settings"; break;
        case Slot::Kind::Spotlight: r.kind = "spotlight"; break;
        case Slot::Kind::AppMenu: r.kind = "app_menu"; break;
        case Slot::Kind::Smenu: r.kind = "smenu"; break;
        case Slot::Kind::Launchpad: r.kind = "launchpad"; break;
        case Slot::Kind::AppDrawer: r.kind = "app_drawer"; break;
        case Slot::Kind::Clock: r.kind = "clock"; break;
        case Slot::Kind::Weather: r.kind = "weather"; break;
        case Slot::Kind::Media: r.kind = "media"; break;
        case Slot::Kind::ControlCenter: r.kind = "control_center"; break;
        case Slot::Kind::Workspaces: r.kind = "workspaces"; break;
        case Slot::Kind::Trash: r.kind = "trash"; break;
        case Slot::Kind::VolumeMixer: r.kind = "volume_mixer"; break;
        case Slot::Kind::Vpn: r.kind = "vpn"; break;
        case Slot::Kind::Battery: r.kind = "battery"; break;
        case Slot::Kind::Bluetooth: r.kind = "bluetooth"; break;
        case Slot::Kind::WorldClock: r.kind = "world_clock"; break;
        case Slot::Kind::Separator: r.kind = "separator"; break;
      }
      return r;
    };
    auto build_rects = [&](const std::vector<Slot>& slots, double x0, std::vector<SlotRect>& out) {
      double cx = x0;
      for (const auto& s : slots) {
        out.push_back(slot_to_rect(s, cx));
        cx += out.back().w + gap;
      }
    };

    std::vector<SlotRect> rects;
    if (use_panel_paint) {
      rects.reserve(left.size() + center.size() + right.size());
      build_rects(left, panelLeftX, rects);
      build_rects(center, panelCenterX, rects);
      build_rects(right, panelRightX, rects);
    } else {
      rects.reserve(all.size());
      double dbgX = startX;
      for (size_t i = 0; i < all.size(); i++) {
        const auto& s = all[i];
        rects.push_back(slot_to_rect(s, dbgX));
        dbgX += rects.back().w + gap_after_paint(i);
      }
    }

    auto intersects = [](const SlotRect& a, const SlotRect& b) -> bool {
      const double ax1 = a.x, ax2 = a.x + a.w;
      const double bx1 = b.x, bx2 = b.x + b.w;
      const double ay1 = a.y, ay2 = a.y + a.h;
      const double by1 = b.y, by2 = b.y + b.h;
      const bool xOver = (ax1 < bx2) && (bx1 < ax2);
      const bool yOver = (ay1 < by2) && (by1 < ay2);
      return xOver && yOver;
    };

    bool anyOverlap = false;
    for (size_t i = 0; i < rects.size(); i++) {
      for (size_t j = i + 1; j < rects.size(); j++) {
        if (intersects(rects[i], rects[j])) {
          anyOverlap = true;
          std::cerr << "[layout] OVERLAP: i=" << i << " (" << rects[i].kind << " key='" << rects[i].key << "'"
                    << " rect=(" << rects[i].x << "," << rects[i].y << " " << rects[i].w << "x" << rects[i].h << "))"
                    << " j=" << j << " (" << rects[j].kind << " key='" << rects[j].key << "'"
                    << " rect=(" << rects[j].x << "," << rects[j].y << " " << rects[j].w << "x" << rects[j].h << "))\n";
        }
      }
    }

    std::string snap;
    snap.reserve(128 + rects.size() * 32);
    snap += "dock=(" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(boxW) + "x" + std::to_string(boxH) + ") ";
    if (use_panel_paint)
      snap += "panelLeftX=" + std::to_string(panelLeftX) + " panelCenterX=" + std::to_string(panelCenterX) + " panelRightX=" + std::to_string(panelRightX) + " ";
    else
      snap += "startX=" + std::to_string(startX) + " ";
    snap += "totalW=" + std::to_string(totalW) + " n=" + std::to_string(rects.size()) + " ";
    for (size_t i = 0; i < rects.size(); i++) {
      const auto& r = rects[i];
      snap += "[" + std::to_string(i) + ":" + r.kind + ":" + r.key + "@";
      snap += std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h) + "]";
    }

    std::string& layoutSnap = app.lastWidgetLayoutSnapshot;
    if (anyOverlap || snap != layoutSnap) {
      layoutSnap = snap;
      std::cout << "[layout] widgets " << snap << "\n";
    }
  }

  bool media_marquee_accum = false;
  bool media_progress_tick_accum = false;

  auto render_section = [&](const std::vector<Slot>& slots, double x0, int slot_index_base) {
    bool haveFloatingPin = false;
    std::string floatingPinLookup{};
    bool floatingPinRunning = false;
    bool floatingPinActivated = false;

    auto gap_after_local = [&](size_t idx) -> double {
      if (idx + 1 >= slots.size()) return 0.0;
      if (slots[idx].kind == Slot::Kind::Tray && slots[idx + 1].kind == Slot::Kind::Tray) return 0.0;
      if (app.settings.dockPinnedAppsTrayPill && slots[idx].kind == Slot::Kind::App && slots[idx].isPinned &&
          slots[idx + 1].kind == Slot::Kind::App && slots[idx + 1].isPinned)
        return 0.0;
      if (slots[idx].kind == Slot::Kind::App && slots[idx].isPinned && slots[idx + 1].kind == Slot::Kind::App &&
          !slots[idx + 1].isPinned)
        return std::round(gap * 1.5);
      return gap;
    };

    auto effective_hover_match = [&](int global_idx) -> bool {
      if (isPanel) return taskbarHoverSlot >= 0 && global_idx == taskbarHoverSlot;
      return input_this_layer && global_idx == app.dockHoverSlot;
    };
    auto effective_press_match = [&](int global_idx) -> bool {
      if (isPanel) return taskbarPressedSlot >= 0 && global_idx == taskbarPressedSlot;
      return input_this_layer && global_idx == app.dockPressedSlot;
    };
    auto effective_hover_lift = [&](int global_idx) -> double {
      if (isPanel) {
        if (global_idx == taskbarHoverSlot) return taskbarHoverLiftPx;
        return 0.0;
      }
      if (input_this_layer && global_idx == app.dockHoverSlot) return app.dockHoverLiftPx;
      return 0.0;
    };

    std::vector<double> pinnedXs;
    pinnedXs.reserve(64);
    {
      double tx = x0;
      for (size_t i = 0; i < slots.size(); i++) {
        const auto& s = slots[i];
        const double slotW = paint_slot_w(s);
        const double ix = tx;
        tx += slotW + gap_after_local(i);
        if (s.kind == Slot::Kind::App && s.isPinned) {
          if (!app.pinDragKey.empty() && s.key == app.pinDragKey) continue;
          pinnedXs.push_back(ix);
        }
      }
    }

    double curX = x0;
    for (size_t i = 0; i < slots.size(); i++) {
      const auto& s = slots[i];
      const double slotW = paint_slot_w(s);
      const double ix = curX;
      if (out_hits) {
        out_hits->push_back({s.key, ix, iconY, slotW, icon, s.chosenSerial, s.isPinned, static_cast<int>(s.kind)});
      }
      curX += slotW + gap_after_local(i);

      const bool isDraggedPin =
          (app.pinDragging || app.pinDragCandidate) && !app.pinDragKey.empty() && (s.key == app.pinDragKey);
      const int globalIdx = static_cast<int>(i) + slot_index_base;
      const bool slotPressed = s.kind != Slot::Kind::Clock && s.kind != Slot::Kind::WorldClock &&
          s.kind != Slot::Kind::Weather &&
          s.kind != Slot::Kind::Media &&           s.kind != Slot::Kind::Workspaces && s.kind != Slot::Kind::ControlCenter &&
          s.kind != Slot::Kind::VolumeMixer && s.kind != Slot::Kind::Vpn && s.kind != Slot::Kind::Battery &&
          s.kind != Slot::Kind::Bluetooth &&          s.kind != Slot::Kind::Settings &&
          s.kind != Slot::Kind::Spotlight && s.kind != Slot::Kind::AppDrawer &&
          s.kind != Slot::Kind::Smenu &&
          s.kind != Slot::Kind::Launchpad && s.kind != Slot::Kind::Trash &&
          s.kind != Slot::Kind::AppMenu && s.kind != Slot::Kind::Tray &&
          effective_press_match(globalIdx);
      double liftY = (app.pinDragging && isDraggedPin) ? -4.0 : 0.0;
      if (s.kind != Slot::Kind::Clock && s.kind != Slot::Kind::WorldClock &&
          s.kind != Slot::Kind::Weather && s.kind != Slot::Kind::Media && s.kind != Slot::Kind::Workspaces &&
          s.kind != Slot::Kind::VolumeMixer && s.kind != Slot::Kind::Vpn && s.kind != Slot::Kind::Battery &&
          s.kind != Slot::Kind::Bluetooth &&          s.kind != Slot::Kind::ControlCenter &&
          s.kind != Slot::Kind::Settings && s.kind != Slot::Kind::Spotlight &&
          s.kind != Slot::Kind::AppDrawer && s.kind != Slot::Kind::Smenu &&
          s.kind != Slot::Kind::Launchpad &&
          s.kind != Slot::Kind::Trash && s.kind != Slot::Kind::AppMenu &&
          s.kind != Slot::Kind::Tray)
        liftY -= effective_hover_lift(globalIdx);
      if (slotPressed && !isDraggedPin) liftY -= kDockPressLiftPx;
      liftY += dock_launch_bounce_extra_lift_y(app, s.kind == Slot::Kind::App, s.key);
      const double pressS = slotPressed ? kDockPressIconScale : 1.0;
      const bool usePress = (pressS < 0.999);

      if (!isPanel && isDraggedPin) {
        rounded_rect(ix - 2.0, iconY + liftY - 2.0, icon + 4.0, icon + 4.0, 14.0);
        cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, app.pinDragging ? 0.12 : 0.08);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, app.pinDragging ? 0.55 : 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        if (app.pinDragging) {
          haveFloatingPin = true;
          floatingPinLookup = !s.iconId.empty() ? s.iconId : s.key;
          floatingPinRunning = dock_toplevel_by_serial(app, s.chosenSerial) != nullptr;
          floatingPinActivated = s.anyActivated;
          rounded_rect(ix, iconY, icon, icon, 12.0);
          cairo_set_source_rgba(cr, 1, 1, 1, 0.04);
          cairo_fill(cr);
          continue;
        }
      }

      if (slotPressed && !isDraggedPin) {
        rounded_rect(ix - 2.0, iconY + liftY - 2.0, icon + 4.0, icon + 4.0, 14.0);
        cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.58);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      }

      if (usePress) {
        const double pcx = ix + icon * 0.5;
        const double pcy = iconY + liftY + icon * 0.5;
        cairo_save(cr);
        cairo_translate(cr, pcx, pcy);
        cairo_scale(cr, pressS, pressS);
        cairo_translate(cr, -pcx, -pcy);
      }

      auto hover_overlay = [&] {
        const double r = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
        rounded_rect(ix, iconY + liftY, slotW, icon, r);
        cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR, eh::widgets::slot_pill_style::g_hoverAccentG, eh::widgets::slot_pill_style::g_hoverAccentB, 0.18);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR, eh::widgets::slot_pill_style::g_hoverAccentG, eh::widgets::slot_pill_style::g_hoverAccentB, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      };
      bool drew = false;
      const bool cellHovered = effective_hover_match(static_cast<int>(i) + slot_index_base);
      if (s.kind == Slot::Kind::Settings) {
        eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        if (cellHovered) hover_overlay();
        if (!app.settingsLogo) app.settingsLogo = eh::shell::asset::load_brand_logo_surface();
        if (app.settingsLogo) {
          const int lw = cairo_image_surface_get_width(app.settingsLogo);
          const int lh = cairo_image_surface_get_height(app.settingsLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc = avail / std::max(1, std::max(lw, lh));
          const double dw = lw * sc;
          const double dh = lh * sc;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.settingsLogo, dx, dy, sc, dockBarMatugen, mcDockBar.accentR,
                                                mcDockBar.accentG, mcDockBar.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Spotlight || s.kind == Slot::Kind::AppDrawer) {
        if (cellHovered) hover_overlay();
        if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
        if (app.distroSpotlightLogo) {
          const int lw = cairo_image_surface_get_width(app.distroSpotlightLogo);
          const int lh = cairo_image_surface_get_height(app.distroSpotlightLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc = avail / std::max(1, std::max(lw, lh));
          const double dw = lw * sc;
          const double dh = lh * sc;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.distroSpotlightLogo, dx, dy, sc, dockBarMatugen,
                                                 mcDockBar.accentR, mcDockBar.accentG, mcDockBar.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Launchpad) {
        if (app.settings.dockPinnedAppsTrayPill && i + 1 < slots.size() && slots[i + 1].kind == Slot::Kind::App && slots[i + 1].isPinned) {
          size_t re = i + 1;
          while (re + 1 < slots.size() && slots[re + 1].kind == Slot::Kind::App && slots[re + 1].isPinned)
            re++;
          double pw = static_cast<double>(re - i + 1) * icon;
          for (size_t j = i; j < re; ++j)
            pw += gap_after_local(j);
          if (re + 1 < slots.size() && slots[re + 1].kind == Slot::Kind::Trash) {
            pw += gap_after_local(re) + icon;
          }
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY, pw, icon);
        }
        if (cellHovered) hover_overlay();
        if (app.launchpad) {
          app.launchpad->paint_dock_button(cr, ix, iconY + liftY, icon, false);
          drew = true;
        } else if (!drew) {
          const double gx = ix + icon * 0.5;
          const double gy = iconY + liftY + icon * 0.5;
          const double gr = dockBarMatugen ? mcDockBar.accentR : 0.88;
          const double gg = dockBarMatugen ? mcDockBar.accentG : 0.93;
          const double gb = dockBarMatugen ? mcDockBar.accentB : 0.94;
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "view_quilt", gr, gg, gb, 1.0);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Trash) {
        if (cellHovered) hover_overlay();
        app.trashFull = eh::shell::fs::trash_has_files();
        cairo_surface_t* trashLogo = app.trashFull ? app.trashFullLogo : app.trashEmptyLogo;
        if (!trashLogo) {
          if (app.trashFull) {
            if (!app.trashFullLogo) app.trashFullLogo = eh::shell::asset::load_trash_full_surface();
            trashLogo = app.trashFullLogo;
          } else {
            if (!app.trashEmptyLogo) app.trashEmptyLogo = eh::shell::asset::load_trash_empty_surface();
            trashLogo = app.trashEmptyLogo;
          }
        }
        if (trashLogo) {
          const int lw = cairo_image_surface_get_width(trashLogo);
          const int lh = cairo_image_surface_get_height(trashLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc = avail / std::max(1, std::max(lw, lh));
          const double dw = lw * sc;
          const double dh = lh * sc;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, trashLogo, dx, dy, sc, false, 0, 0, 0);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::AppMenu) {
        if (cellHovered) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        const double pitch = icon / 6.8;
        const double rDot = icon * 0.06;
        if (dockBarMatugen)
          cairo_set_source_rgba(cr, mcDockBar.accentR, mcDockBar.accentG, mcDockBar.accentB, 1.0);
        else
          cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        for (int ry = -1; ry <= 1; ++ry) {
          for (int sx = -1; sx <= 1; ++sx) {
            cairo_arc(cr, gx + static_cast<double>(sx) * pitch, gy + static_cast<double>(ry) * pitch, rDot, 0,
                      2 * M_PI);
            cairo_fill(cr);
          }
        }
        drew = true;
      } else if (s.kind == Slot::Kind::Smenu) {
        if (cellHovered) hover_overlay();
        if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
        if (app.distroSpotlightLogo) {
          const int lw = cairo_image_surface_get_width(app.distroSpotlightLogo);
          const int lh = cairo_image_surface_get_height(app.distroSpotlightLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc = avail / std::max(1, std::max(lw, lh));
          const double dw = lw * sc;
          const double dh = lh * sc;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.distroSpotlightLogo, dx, dy, sc, dockBarMatugen,
                                                 mcDockBar.accentR, mcDockBar.accentG, mcDockBar.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Tray) {

        const DockApp::TrayItem* ti = nullptr;
        if (s.key.rfind(eh::shell::kSlotKeyTray, 0) == 0) {
          const std::string want = s.key.substr(std::string(eh::shell::kSlotKeyTray).size());
          for (const auto& cand : traySnap) {
            if ((cand.service + cand.path) == want) {
              ti = &cand;
              break;
            }
          }
        }
        const bool firstTrayInRun = (i == 0 || slots[i - 1].kind != Slot::Kind::Tray);
        if (firstTrayInRun) {
          size_t runEnd = i;
          while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Tray) runEnd++;
          const int nRun = static_cast<int>(runEnd - i + 1);
          const double runW = static_cast<double>(nRun) * icon;
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY, runW, icon);
        }
        const bool cellHovered = effective_hover_match(static_cast<int>(i) + slot_index_base);
        if (cellHovered) hover_overlay();
        if (ti) {
          const double maxSide = std::min(icon * 0.6, icon - 8.0);
          const double avail = std::max(4.0, maxSide);
          if (!ti->iconName.empty()) {
            if (const auto* ic = paint_cached_tray_icon(app, ti->iconName)) {
              if (ic->surface) {
                const double sx = avail / std::max(1, ic->width);
                const double sy = avail / std::max(1, ic->height);
                const double sc = std::min(sx, sy);
                const double dw = ic->width * sc;
                const double dh = ic->height * sc;
                const double dx = ix + (icon - dw) / 2.0;
                const double dy = iconY + liftY + (icon - dh) / 2.0;
                cairo_save(cr);
                cairo_translate(cr, dx, dy);
                cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, ic->surface, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
                drew = true;
              }
            }
          } else if (ti->pixSurface) {
            const int lw = cairo_image_surface_get_width(ti->pixSurface);
            const int lh = cairo_image_surface_get_height(ti->pixSurface);
            const double sx = avail / std::max(1, lw);
            const double sy = avail / std::max(1, lh);
            const double sc = std::min(sx, sy);
            const double dw = lw * sc;
            const double dh = lh * sc;
            const double dx = ix + (icon - dw) / 2.0;
            const double dy = iconY + liftY + (icon - dh) / 2.0;
            cairo_save(cr);
            cairo_translate(cr, dx, dy);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, ti->pixSurface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            drew = true;
          } else {
            const std::string mk = ti->service + ti->path;
            if (!app.trayMissingLogged.contains(mk)) {
              app.trayMissingLogged[mk] = true;
              std::cout << "[tray-icon] miss: service='" << ti->service << "' path='" << ti->path
                        << "' id='" << ti->id << "' title='" << ti->title
                        << "' reason=no_IconName_and_no_IconPixmap\n";
            }
          }
        }
      } else if (s.kind == Slot::Kind::WorldClock) {
        eh::shell::dock_slot_hooks::paint_world_clock_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (cellHovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Clock) {
        eh::shell::dock_slot_hooks::paint_clock_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (cellHovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Weather) {
        eh::shell::dock_slot_hooks::paint_weather_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (cellHovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Media) {
        const eh::mpris::PlayerSnapshot snap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};
        const bool mh = effective_hover_match(static_cast<int>(i) + slot_index_base);
        const bool mp = effective_press_match(static_cast<int>(i) + slot_index_base);
        const int mediaHoverBtn = mh ? eh::mpris::DockMpris::media_hit_zone(app.pointerX - ix, slotW, icon) : -1;
        bool media_progress = false;
        if (eh::shell::dock_slot_hooks::paint_media_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, snap,
                                                          mh, mp, mediaHoverBtn, app.pointerX, app.pointerY,
                                                          &media_progress)) {
          media_marquee_accum = true;
        }
        if (media_progress) media_progress_tick_accum = true;
        drew = true;
      } else if (s.kind == Slot::Kind::Workspaces) {
        const bool wh = effective_hover_match(static_cast<int>(i) + slot_index_base);
        const bool wp = effective_press_match(static_cast<int>(i) + slot_index_base);
        if (eh::shell::dock_slot_hooks::paint_workspaces_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, app.workspaceStrip,
                                                wh, wp, &app.icons, &app.wsStripAnim)) {
          app.pendingRedraw = true;
        }
        if (wh) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::ControlCenter) {
        const bool ch = effective_hover_match(static_cast<int>(i) + slot_index_base);
        const bool cp = effective_press_match(static_cast<int>(i) + slot_index_base);
        (void)eh::shell::dock_slot_hooks::paint_control_center_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, ch, cp);
        if (ch) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::VolumeMixer) {
        {
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        }
        const bool ch = effective_hover_match(static_cast<int>(i) + slot_index_base);
        if (ch) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        if (dockBarMatugen)
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "volume_up", mcDockBar.accentR, mcDockBar.accentG, mcDockBar.accentB, 1.0);
        else
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "volume_up", 1.0, 1.0, 1.0, 1.0);
        drew = true;
      } else if (s.kind == Slot::Kind::Vpn) {
        {
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        }
        const bool ch = effective_hover_match(static_cast<int>(i) + slot_index_base);
        if (ch) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        if (dockBarMatugen)
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "vpn_key", mcDockBar.accentR, mcDockBar.accentG, mcDockBar.accentB, 1.0);
        else
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "vpn_key", 1.0, 1.0, 1.0, 1.0);
        drew = true;
      } else if (s.kind == Slot::Kind::Battery) {
        eh::shell::dock_slot_hooks::paint_battery_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        drew = true;
      } else if (s.kind == Slot::Kind::Bluetooth) {
        eh::shell::dock_slot_hooks::paint_bluetooth_slot(cr, scPaint, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (cellHovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::App) {
        if (app.settings.dockPinnedAppsTrayPill && s.isPinned) {
          const bool prevIsLaunchpad = i > 0 && slots[i - 1].kind == Slot::Kind::Launchpad;
          const bool firstPinnedInRun =
              (i == 0 || slots[i - 1].kind != Slot::Kind::App || !slots[i - 1].isPinned);
          if (firstPinnedInRun && !prevIsLaunchpad) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && slots[runEnd + 1].isPinned)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = static_cast<double>(runEnd - i + 1) * icon;
            if (appendRightTrash) {
              const double g = gap_after_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        if (app.settings.dockRunningAppsTrayPill && !s.isPinned) {
          const bool firstRunningInRun =
              (i == 0 || slots[i - 1].kind != Slot::Kind::App || slots[i - 1].isPinned);
          if (firstRunningInRun) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && !slots[runEnd + 1].isPinned)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = static_cast<double>(runEnd - i + 1) * icon;
            if (appendRightTrash) {
              const double g = gap_after_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        const std::string lookup = !s.iconId.empty() ? s.iconId : s.key;
        const eh::icons::IconEntry* ic = paint_cached_app_icon(app, lookup);
        if (!ic || !ic->surface) {
          ic = paint_cached_tray_icon(app, lookup);
        }
        if (ic && ic->surface) {
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sx = avail / std::max(1, ic->width);
          const double sy = avail / std::max(1, ic->height);
          const double sc = std::min(sx, sy);
          const double dw = ic->width * sc;
          const double dh = ic->height * sc;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          cairo_save(cr);
          cairo_translate(cr, dx, dy);
          cairo_scale(cr, sc, sc);
          cairo_set_source_surface(cr, ic->surface, 0, 0);
          cairo_paint(cr);
          cairo_restore(cr);
          drew = true;
        }
      }

      if (!drew) {
        rounded_rect(ix, iconY + liftY, slotW, icon, 12.0);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        cairo_fill(cr);
      }

      if (usePress) cairo_restore(cr);

      const double iconBottom = iconY + liftY + icon;
      const double dotR = 3.0;
      const double dotCy = iconBottom + kDockRunIndicatorGapPx + dotR;
      if (s.kind == Slot::Kind::App && dock_toplevel_by_serial(app, s.chosenSerial)) {
        cairo_arc(cr, ix + icon / 2.0, dotCy, dotR, 0, 2 * M_PI);
        if (s.anyActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_fill(cr);
      } else if (s.kind == Slot::Kind::Settings && dock_toplevel_by_serial(app, s.chosenSerial)) {
        cairo_arc(cr, ix + icon / 2.0, dotCy, dotR, 0, 2 * M_PI);
        if (s.anyActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_fill(cr);
      }
    }

    const bool pinFloatThisLayer =
        app.dockLayers.size() <= 1 || paint_layer_idx == app.pointerDockLayerIdx;
    if (!isPanel && app.pinDragging && haveFloatingPin && !floatingPinLookup.empty() && pinFloatThisLayer) {

      const double fx = std::max(x + 8.0, std::min(x + boxW - icon - 8.0, app.pointerX - icon / 2.0));
      const double fy = iconY - 6.0;
      rounded_rect(fx - 2.0, fy - 2.0, icon + 4.0, icon + 4.0, 14.0);
      cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.65);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
      if (const auto* ic = paint_cached_app_icon(app, floatingPinLookup)) {
        if (ic->surface) {
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sx = avail / std::max(1, ic->width);
          const double sy = avail / std::max(1, ic->height);
          const double sc = std::min(sx, sy);
          const double dw = ic->width * sc;
          const double dh = ic->height * sc;
          const double dx = fx + (icon - dw) / 2.0;
          const double dy = fy + (icon - dh) / 2.0;
          cairo_save(cr);
          cairo_translate(cr, dx, dy);
          cairo_scale(cr, sc, sc);
          cairo_set_source_surface(cr, ic->surface, 0, 0);
          cairo_paint(cr);
          cairo_restore(cr);
        }
      }
      if (floatingPinRunning) {
        const double fBottom = fy + icon;
        const double fDotR = 3.0;
        const double fDotCy = fBottom + kDockRunIndicatorGapPx + fDotR;
        cairo_arc(cr, fx + icon / 2.0, fDotCy, fDotR, 0, 2 * M_PI);
        if (floatingPinActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_fill(cr);
      }
    }
  };

  if (use_panel_paint) {
    render_section(left, panelLeftX, 0);
    render_section(center, panelCenterX, static_cast<int>(left.size()));
    render_section(right, panelRightX, static_cast<int>(left.size() + center.size()));
  } else {
    const double midX = x + boxW * 0.5;
    const double hScale = widget_strip_h_scale(boxW, totalW, stripInner);
    if (hScale < 1.0 - 1e-9) {
      cairo_save(cr);
      cairo_translate(cr, midX, 0.0);
      cairo_scale(cr, hScale, 1.0);
      cairo_translate(cr, -midX, 0.0);
      if (use_lr_paint) {
        render_section(left, lrPaint.x_left, 0);
        render_section(right, lrPaint.x_right, static_cast<int>(left.size()));
      } else {
        render_section(all, startX, 0);
      }
      cairo_restore(cr);
      if (out_hits) {
        for (auto& hh : *out_hits) {
          hh.x = midX + (hh.x - midX) * hScale;
          hh.w = hh.w * hScale;
          hh.h = hh.h * hScale;
        }
      }
    } else {
      if (use_lr_paint) {
        render_section(left, lrPaint.x_left, 0);
        render_section(right, lrPaint.x_right, static_cast<int>(left.size()));
      } else {
        render_section(all, startX, 0);
      }
    }
  }

  if (out_media_marquee_wants_frame) *out_media_marquee_wants_frame = media_marquee_accum;
  if (out_media_progress_tick) *out_media_progress_tick = media_progress_tick_accum;
}
