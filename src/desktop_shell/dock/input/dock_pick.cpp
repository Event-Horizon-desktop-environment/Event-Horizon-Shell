#include "desktop_shell/dock/input/dock_pick.hpp"

#include <cmath>

#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/dock/layout/dock_layout_shared.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/shared/widgets/workspace_strip.hpp"
#include "desktop_shell/dock/paint/dock_strip_geometry.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "configuration/shell_config.hpp"
#include "services/tray/filter/tray_env_filter.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"

#include <algorithm>
#include <mutex>
#include <string>

using eh::shell::dock::dock_ensure_workspace_strip;
using eh::shell::dock::dock_active_canvas_dims;
using eh::shell::dock::dock_pill_geometry;
using eh::shell::dock::strip_layout_x_to_surface;
using eh::shell::dock::strip_surface_x_to_layout;
using eh::shell::dock::widget_strip_h_scale;
using eh::shell::dock::lr_strip_layout;

static bool dock_pick_adjacent_tray(const PickSlot& a, const PickSlot& b) {
  return a.kind == PickSlot::Kind::Tray && b.kind == PickSlot::Kind::Tray;
}

static bool dock_pick_adjacent_pinned_tray_pill(const DockSettings& st, const PickSlot& a, const PickSlot& b) {
  if (!st.dockPinnedAppsTrayPill) return false;
  return a.kind == PickSlot::Kind::App && a.isPinned && b.kind == PickSlot::Kind::App && b.isPinned;
}

static bool dock_pick_adjacent_running_tray_pill(const DockSettings& st, const PickSlot& a, const PickSlot& b) {
  if (!st.dockRunningAppsTrayPill) return false;
  return a.kind == PickSlot::Kind::App && !a.isPinned && b.kind == PickSlot::Kind::App && !b.isPinned;
}

static bool dock_pick_adjacent_pinned_to_running_app(const PickSlot& a, const PickSlot& b) {
  return a.kind == PickSlot::Kind::App && a.isPinned && b.kind == PickSlot::Kind::App && !b.isPinned;
}

double dock_gap_after_pick(const DockSettings& st, const std::vector<PickSlot>& v, size_t idx, double defGap) {
   
  if (idx + 1 >= v.size()) return 0.0;
  if (dock_pick_adjacent_tray(v[idx], v[idx + 1])) return 0.0;
  if (dock_pick_adjacent_pinned_tray_pill(st, v[idx], v[idx + 1])) return 0.0;
  if (dock_pick_adjacent_running_tray_pill(st, v[idx], v[idx + 1])) return 0.0;
  if (dock_pick_adjacent_pinned_to_running_app(v[idx], v[idx + 1])) return std::round(defGap * 1.5);
  return defGap;
}

[[nodiscard]] bool dock_strip_widget_blocked(const DockSettings& st, const std::string& widgetToken) {
   
  if (st.dockWidgetsEnabled) return false;
  if (widgetToken == "pinned_apps" || widgetToken == "running_apps") return false;
  if (eh::config::widget_token_is_system_tray(widgetToken)) return false;
  if (widgetToken == "settings_button" || widgetToken == "distro_spotlight" || widgetToken == "app_menu" ||
      widgetToken == "launchpad" || widgetToken == "trash")
    return false;
  const std::string impl = eh::config::widget_implementation_type(widgetToken);
  return impl == "clock" || impl == "world_clock" || impl == "weather" || impl == "media" || impl == "workspaces" || impl == "control_center" ||
         impl == "notifications" || impl == "volume_mixer" || impl == "app_drawer";
}

void dock_fill_pick_result_slots(DockApp& app, const eh::shell::shared::RunningSnapshot& hitSnap,
                                       const std::vector<std::string>& leftW,
                                       const std::vector<std::string>& centerW,
                                       const std::vector<std::string>& rightW, DockPickResult& r) {
   
  const auto& runningGroups = hitSnap.groups;
  r.settingsChosenSerial = hitSnap.settingsChosenSerial;
  r.settingsActivated = hitSnap.settingsActivated;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    r.traySnap = app.trayItems;
  }
  std::sort(r.traySnap.begin(), r.traySnap.end(), [](const DockApp::TrayItem& a, const DockApp::TrayItem& b) {
    if (a.service != b.service) return a.service < b.service;
    return a.path < b.path;
  });
  r.traySnap.erase(std::remove_if(r.traySnap.begin(), r.traySnap.end(), [](const DockApp::TrayItem& t) {
                     return eh::shell::dock::tray::dock_tray_item_hidden_by_env(t.id, t.title, t.service, t.path);
                   }),
                   r.traySnap.end());

  auto is_pinned = [&](const std::string& k) { return dock_pin_identity_list_contains_toplevel_key(app, k); };

  const eh::config::ShellConfig& scStrip = eh::config::shell_config_snapshot();

  std::vector<PickSlot> pinnedSlots;
  pinnedSlots.reserve(dock_pinned_apps_source_for_layout(app).size());
  std::vector<PickSlot> runningSlots;
  runningSlots.reserve(runningGroups.size());
  std::vector<PickSlot> traySlots;
  traySlots.reserve(r.traySnap.size());
  PickSlot settingsSlot;
  settingsSlot.kind = PickSlot::Kind::Settings;
  settingsSlot.key = eh::shell::kSlotKeySettings;
  settingsSlot.chosenSerial = r.settingsChosenSerial;
  settingsSlot.anyActivated = r.settingsActivated;

  PickSlot spotlightSlot;
  spotlightSlot.kind = PickSlot::Kind::Spotlight;
  spotlightSlot.key = eh::shell::kSlotKeySpotlight;
  PickSlot appMenuSlot;
  appMenuSlot.kind = PickSlot::Kind::AppMenu;
  appMenuSlot.key = eh::shell::kSlotKeyAppMenu;
  PickSlot appDrawerSlot;
  appDrawerSlot.kind = PickSlot::Kind::AppDrawer;
  appDrawerSlot.key = eh::shell::kSlotKeyAppDrawer;

  for (const auto& pRaw : dock_pinned_apps_source_for_layout(app)) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    PickSlot s;
    s.kind = PickSlot::Kind::App;
    s.key = p;
    s.isPinned = true;
    dock_pinned_merge_running_slot(app, runningGroups, p, s.chosenSerial, s.anyActivated, nullptr);
    pinnedSlots.push_back(std::move(s));
  }
  for (const auto& rg : runningGroups) {
    if (is_pinned(rg.pinMatchKey)) continue;
    PickSlot s;
    s.kind = PickSlot::Kind::App;
    s.key = rg.key;
    s.chosenSerial = rg.chosenSerial;
    s.anyActivated = rg.anyActivated;
    s.isPinned = false;
    runningSlots.push_back(std::move(s));
  }
  for (const auto& ti : r.traySnap) {
    PickSlot s;
    s.kind = PickSlot::Kind::Tray;
    s.key = std::string(eh::shell::kSlotKeyTray) + ti.service + ti.path;
    traySlots.push_back(std::move(s));
  }

  auto append_tokens_for_widget = [&](std::vector<PickSlot>& out, const std::string& w) {
    if (w == "pinned_apps") {
      out.insert(out.end(), pinnedSlots.begin(), pinnedSlots.end());
    } else if (w == "running_apps") {
      out.insert(out.end(), runningSlots.begin(), runningSlots.end());
    } else if (eh::config::widget_token_is_system_tray(w)) {
      out.insert(out.end(), traySlots.begin(), traySlots.end());
    } else if (w == "settings_button") {
      out.push_back(settingsSlot);
    } else if (w == "distro_spotlight") {
      out.push_back(spotlightSlot);
    } else if (w == "app_menu") {
      out.push_back(appMenuSlot);
    } else if (eh::config::widget_implementation_type(w) == "app_drawer") {
      out.push_back(appDrawerSlot);
    } else if (eh::config::widget_implementation_type(w) == "smenu") {
      PickSlot sm;
      sm.kind = PickSlot::Kind::Smenu;
      sm.key = eh::shell::kSlotKeySmenu;
      out.push_back(std::move(sm));
    } else if (w == "launchpad" || eh::config::widget_implementation_type(w) == "launchpad") {
      PickSlot lp;
      lp.kind = PickSlot::Kind::Launchpad;
      lp.key = w;
      out.push_back(std::move(lp));
    } else if (eh::config::widget_implementation_type(w) == "clock") {
      PickSlot cs;
      cs.kind = PickSlot::Kind::Clock;
      cs.key = w;
      out.push_back(std::move(cs));
    } else if (eh::config::widget_implementation_type(w) == "weather") {
      PickSlot ws;
      ws.kind = PickSlot::Kind::Weather;
      ws.key = w;
      out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(w) == "media") {
      PickSlot ms;
      ms.kind = PickSlot::Kind::Media;
      ms.key = w;
      out.push_back(std::move(ms));
    } else if (eh::config::widget_implementation_type(w) == "workspaces") {
      PickSlot ws;
      ws.kind = PickSlot::Kind::Workspaces;
      ws.key = w;
      out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(w) == "control_center") {
      PickSlot cc;
      cc.kind = PickSlot::Kind::ControlCenter;
      cc.key = w;
      out.push_back(std::move(cc));
    } else if (w == "trash" || eh::config::widget_implementation_type(w) == "trash") {
      PickSlot ts;
      ts.kind = PickSlot::Kind::Trash;
      ts.key = w;
      out.push_back(std::move(ts));
    } else if (eh::config::widget_implementation_type(w) == "volume_mixer") {
      PickSlot vm;
      vm.kind = PickSlot::Kind::VolumeMixer;
      vm.key = w;
      out.push_back(std::move(vm));
    } else if (eh::config::widget_implementation_type(w) == "vpn") {
      PickSlot vp;
      vp.kind = PickSlot::Kind::Vpn;
      vp.key = w;
      out.push_back(std::move(vp));
    } else if (eh::config::widget_implementation_type(w) == "battery") {
      PickSlot bs;
      bs.kind = PickSlot::Kind::Battery;
      bs.key = w;
      out.push_back(std::move(bs));
    } else if (eh::config::widget_implementation_type(w) == "bluetooth") {
      PickSlot bt;
      bt.kind = PickSlot::Kind::Bluetooth;
      bt.key = w;
      out.push_back(std::move(bt));
    } else if (eh::config::widget_implementation_type(w) == "world_clock") {
      PickSlot wcs;
      wcs.kind = PickSlot::Kind::WorldClock;
      wcs.key = w;
      out.push_back(std::move(wcs));
    }
  };

  auto build_section = [&](const std::vector<std::string>& widgets) {
    std::vector<PickSlot> out;
    out.reserve(widgets.size());
    for (const auto& w : widgets) {
      if (!eh::config::widget_instance_enabled(scStrip, w)) continue;
      if (dock_strip_widget_blocked(app.settings, w)) continue;
      append_tokens_for_widget(out, w);
    }
    return out;
  };

  std::vector<PickSlot> leftSlots = build_section(leftW);
  std::vector<PickSlot> centerSlots = build_section(centerW);
  std::vector<PickSlot> rightSlots = build_section(rightW);

  r.all.clear();
  r.leftSlotCount = leftSlots.size();
  r.centerSlotCount = centerSlots.size();
  r.rightSlotCount = rightSlots.size();

  r.all.reserve(leftSlots.size() + centerSlots.size() + rightSlots.size());
  r.all.insert(r.all.end(), leftSlots.begin(), leftSlots.end());
  r.all.insert(r.all.end(), centerSlots.begin(), centerSlots.end());
  r.all.insert(r.all.end(), rightSlots.begin(), rightSlots.end());
}

double dock_pick_layout_total_width(const DockApp& app, const std::vector<PickSlot>& all) {
   
  const double icon = static_cast<double>(dock_effective_icon_px(app.settings));
  const double gap = static_cast<double>(dock_effective_gap_px(app.settings));
  const auto& scPick = eh::config::shell_config_snapshot();
  auto pick_slot_w = [&](const PickSlot& s) -> double {
    if (s.kind == PickSlot::Kind::Clock) {
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Weather) {
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Media) {
      const eh::mpris::PlayerSnapshot snap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight), snap);
    }
    if (s.kind == PickSlot::Kind::Workspaces) {
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight),
                                                      app.workspaceStrip);
    }
    if (s.kind == PickSlot::Kind::ControlCenter) {
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Battery) {
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Bluetooth) {
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::WorldClock) {
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    return icon;
  };
  double total = 0.0;
  for (size_t i = 0; i < all.size(); i++) {
    total += pick_slot_w(all[i]);
    if (i + 1 < all.size()) total += dock_gap_after_pick(app.settings, all, i, gap);
  }
  return total;
}

namespace {

struct PickStripGeom {
  double icon = 0;
  double gap = 0;
  double stripInner = 0;
  double midX = 0;
  std::vector<double> w;
  double concatTotal = 0;
  double startX = 0;
  double lw = 0;
  double rw = 0;
  double centerSectionW = 0;
  double secGap = 0;
  bool use_lr = false;
  double xl = 0;
  double xr = 0;
  double total_for_scale = 0;
  double hScale = 1;
};

PickStripGeom compute_pick_strip_geom(const DockApp& app, const DockPickResult& pr, double pill_x, double box_w) {
   
  PickStripGeom g;
  g.icon = static_cast<double>(dock_effective_icon_px(app.settings));
  g.gap = static_cast<double>(dock_effective_gap_px(app.settings));
  g.stripInner = 8.0 * dock_ui_scale(app.settings);
  g.midX = pill_x + box_w * 0.5;
  const auto& scPick = eh::config::shell_config_snapshot();
  auto pick_slot_w = [&](const PickSlot& s) -> double {
    if (s.kind == PickSlot::Kind::Clock) {
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Weather) {
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Media) {
      const eh::mpris::PlayerSnapshot snap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight), snap);
    }
    if (s.kind == PickSlot::Kind::Workspaces) {
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight),
                                                      app.workspaceStrip);
    }
    if (s.kind == PickSlot::Kind::ControlCenter) {
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Battery) {
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Bluetooth) {
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::WorldClock) {
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, scPick, s.key, g.icon, static_cast<double>(app.dockHeight));
    }
    return g.icon;
  };
  g.w.resize(pr.all.size());
  for (size_t i = 0; i < pr.all.size(); ++i) {
    g.w[i] = pick_slot_w(pr.all[i]);
    g.concatTotal += g.w[i];
    if (i + 1 < pr.all.size()) g.concatTotal += dock_gap_after_pick(app.settings, pr.all, i, g.gap);
  }
  g.startX = pill_x + (box_w - g.concatTotal) * 0.5;
  g.total_for_scale = g.concatTotal;
  g.hScale = widget_strip_h_scale(box_w, g.total_for_scale, g.stripInner);

  const size_t lc = pr.leftSlotCount;
  const size_t cc = pr.centerSlotCount;
  const size_t rc = pr.rightSlotCount;
  if (lc + cc + rc != pr.all.size() || pr.all.empty()) {
    return g;
  }
  for (size_t i = 0; i < lc; ++i) {
    g.lw += g.w[i];
    if (i + 1 < lc) g.lw += dock_gap_after_pick(app.settings, pr.all, i, g.gap);
  }
  for (size_t i = 0; i < cc; ++i) {
    const size_t gi = lc + i;
    g.centerSectionW += g.w[gi];
    if (i + 1 < cc) g.centerSectionW += dock_gap_after_pick(app.settings, pr.all, gi, g.gap);
  }
  for (size_t i = 0; i < rc; ++i) {
    const size_t gi = lc + cc + i;
    g.rw += g.w[gi];
    if (i + 1 < rc) g.rw += dock_gap_after_pick(app.settings, pr.all, gi, g.gap);
  }
  g.secGap = std::round(g.gap * 1.5);
  if (cc == 0 && lc > 0 && rc > 0) {
    const auto lr = lr_strip_layout(pill_x, box_w, g.stripInner, g.lw, 0, g.rw, g.secGap);
    if (lr.side_by_side) {
      g.use_lr = true;
      g.xl = lr.x_left;
      g.xr = lr.x_right;
      g.total_for_scale = g.lw + g.secGap + g.rw;
      g.hScale = widget_strip_h_scale(box_w, g.total_for_scale, g.stripInner);
    }
  }
  return g;
}

double pick_slot_left_layout(const DockApp& app, const DockPickResult& pr, const PickStripGeom& g, int idx) {
   
  if (idx < 0 || static_cast<size_t>(idx) >= pr.all.size()) return 0.0;
  const size_t u = static_cast<size_t>(idx);
  const size_t lc = pr.leftSlotCount;
  const size_t cc = pr.centerSlotCount;
  if (g.use_lr && cc == 0) {
    if (u < lc) {
      double c = g.xl;
      for (size_t i = 0; i < u; ++i) {
        c += g.w[i];
        if (i + 1 < lc) c += dock_gap_after_pick(app.settings, pr.all, i, g.gap);
      }
      return c;
    }
    const size_t r0 = lc;
    double c = g.xl + g.lw + g.secGap;
    for (size_t i = r0; i < u; ++i) {
      c += g.w[i];
      if (i + 1 < pr.all.size()) c += dock_gap_after_pick(app.settings, pr.all, i, g.gap);
    }
    return c;
  }
  double c = g.startX;
  for (size_t i = 0; i < u; ++i) {
    c += g.w[i];
    if (i + 1 < pr.all.size()) c += dock_gap_after_pick(app.settings, pr.all, i, g.gap);
  }
  return c;
}

}

DockPickResult dock_pick_at(DockApp& app, double px, double py) {
   
  DockPickResult r;
  if (!app.configured) return r;

  eh::shell::dock::dock_ensure_workspace_strip(app);
  const auto hitSnap = eh::shell::shared::build_running_snapshot(app.toplevels, app.appFirstSeenSerial, app.settings.dockGroupApps);
  dock_fill_pick_result_slots(app, hitSnap, app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets,
                               r);

  double x{};
  double y{};
  double boxW{};
  double boxH{};
  dock_pill_geometry(app, x, y, boxW, boxH);
  const PickStripGeom g = compute_pick_strip_geom(app, r, x, boxW);
  const double iconY = y + (boxH - g.icon) / 2.0;
  const double pxL = strip_surface_x_to_layout(px, g.midX, g.hScale);
  const double rowPad = 8.0 * dock_ui_scale(app.settings);
  if (py < iconY - rowPad || py > iconY + g.icon + rowPad) return r;
  if (r.all.empty()) return r;

  const double stripLeftL = g.use_lr ? g.xl : g.startX;
  const double stripW = g.use_lr ? (g.lw + g.secGap + g.rw) : g.concatTotal;
  if (pxL < stripLeftL || pxL > stripLeftL + stripW) return r;

  for (size_t i = 0; i < r.all.size(); i++) {
    const double leftL = pick_slot_left_layout(app, r, g, static_cast<int>(i));
    const double sw = g.w[i];
    if (pxL >= leftL && pxL < leftL + sw) {
      r.idx = static_cast<int>(i);
      return r;
    }
  }
  return r;
}

double dock_strip_slot_center_x(const DockApp& app, const DockPickResult& pr, int idx) {
   
  if (idx < 0 || static_cast<size_t>(idx) >= pr.all.size()) {
    int cw{}, ch{};
    dock_active_canvas_dims(app, cw, ch);
    return static_cast<double>(cw > 0 ? cw : app.configuredWidth) * 0.5;
  }
  double px{}, py{}, boxW{}, boxH{};
  dock_pill_geometry(app, px, py, boxW, boxH);
  const PickStripGeom g = compute_pick_strip_geom(app, pr, px, boxW);
  const double leftL = pick_slot_left_layout(app, pr, g, idx);
  const double cxLayout = leftL + g.w[static_cast<size_t>(idx)] * 0.5;
  return strip_layout_x_to_surface(cxLayout, g.midX, g.hScale);
}

std::pair<double, double> dock_strip_slot_xw(const DockApp& app, const DockPickResult& pr, int idx) {
   
  if (idx < 0 || static_cast<size_t>(idx) >= pr.all.size()) return {0.0, 0.0};
  double px{}, py{}, boxW{}, boxH{};
  dock_pill_geometry(app, px, py, boxW, boxH);
  const PickStripGeom g = compute_pick_strip_geom(app, pr, px, boxW);
  const double leftL = pick_slot_left_layout(app, pr, g, idx);
  const double surfL = strip_layout_x_to_surface(leftL, g.midX, g.hScale);
  return {surfL, g.w[static_cast<size_t>(idx)] * g.hScale};
}

int dock_pick_workspace_index(const DockApp& app, const DockPickResult& pr, int idx, double pointer_x) {
   
  if (idx < 0 || static_cast<size_t>(idx) >= pr.all.size()) return -1;
  double px{}, py{}, boxW{}, boxH{};
  dock_pill_geometry(app, px, py, boxW, boxH);
  const PickStripGeom g = compute_pick_strip_geom(app, pr, px, boxW);
  const double slotL = pick_slot_left_layout(app, pr, g, idx);
  const double slotW = g.w[static_cast<size_t>(idx)];
  const double pxL = strip_surface_x_to_layout(pointer_x, g.midX, g.hScale);
  const double localX = pxL - slotL;
  const auto& scAct = eh::config::shell_config_snapshot();
  return eh::shell::dock_slot_hooks::workspaces_pick_index(
      localX, slotW, scAct, pr.all[static_cast<size_t>(idx)].key,
      static_cast<double>(dock_effective_icon_px(app.settings)),
      app.workspaceStrip);
}
