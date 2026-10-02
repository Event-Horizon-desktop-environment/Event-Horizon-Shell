// Event Horizon panel paint — brand-new three-zone strip.
//
// Hosts the existing widget slot paints (clock, weather, media, workspaces,
// battery, bluetooth, world-clock, control-center) inside a thin top/bottom
// bar. Layout, caching and fallback rules are written for the panel and are
// not copied from the dock/taskbar painters.

#include "desktop_shell/panel/paint/panel_paint.hpp"

#include "desktop_shell/panel/core/panel.hpp"
#include "desktop_shell/panel/core/panel_settings.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "services/tray/filter/tray_env_filter.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace eh::shell::panel {

cairo_surface_t* PanelScaledIconCache::getOrScale(cairo_surface_t* src, int tw, int th) {
  if (!src || tw <= 0 || th <= 0) return nullptr;
  if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) return nullptr;
  Key k{src, tw, th};
  auto it = map.find(k);
  if (it != map.end()) {
    ++hits;
    return it->second;
  }
  ++misses;
  const int sw = cairo_image_surface_get_width(src);
  const int sh = cairo_image_surface_get_height(src);
  if (sw <= 0 || sh <= 0) return nullptr;
  if (sw == tw && sh == th) return src;
  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
  if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    if (out) cairo_surface_destroy(out);
    return nullptr;
  }
  cairo_t* cr = cairo_create(out);
  cairo_scale(cr, static_cast<double>(tw) / sw, static_cast<double>(th) / sh);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(out);
    return nullptr;
  }
  if (map.size() >= kCap && !fifo.empty()) {
    auto oit = map.find(fifo.front());
    if (oit != map.end()) {
      if (oit->second) cairo_surface_destroy(oit->second);
      map.erase(oit);
    }
    fifo.erase(fifo.begin());
  }
  map.emplace(k, out);
  fifo.push_back(k);
  return out;
}

namespace {

using Slot = PanelPaintSlot;
using Kind = PanelPaintSlot::Kind;

double panel_ui_scale(const PanelApp& app, const eh::config::ShellConfig& sc) {
  const double g = std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
  return std::clamp(app.settings.scale, 0.5, 2.0) * g;
}

bool panel_widget_blocked(const PanelApp& app, const eh::config::ShellConfig& sc, const std::string& w) {
  (void)sc;
  if (app.settings.widgetsEnabled) return false;
  if (w == "pinned_apps" || w == "running_apps") return false;
  if (eh::config::widget_token_is_system_tray(w)) return false;
  if (w == "settings_button" || w == "trash" || w == "spacer" || w == "separator") return false;
  const std::string impl = eh::config::widget_implementation_type(w);
  return impl == "clock" || impl == "world_clock" || impl == "weather" || impl == "media" ||
         impl == "workspaces" || impl == "control_center" || impl == "notifications" ||
         impl == "volume_mixer" || impl == "battery" || impl == "bluetooth" || impl == "vpn" ||
         impl == "overview";
}

Kind panel_kind_for_token(const std::string& wid) {
  if (eh::config::widget_token_is_system_tray(wid)) return Kind::Tray;
  const std::string impl = eh::config::widget_implementation_type(wid);
  if (impl == "clock") return Kind::Clock;
  if (impl == "weather") return Kind::Weather;
  if (impl == "media") return Kind::Media;
  if (impl == "workspaces") return Kind::Workspaces;
  if (impl == "control_center") return Kind::ControlCenter;
  if (impl == "battery") return Kind::Battery;
  if (impl == "bluetooth") return Kind::Bluetooth;
  if (impl == "world_clock") return Kind::WorldClock;
  if (impl == "volume_mixer") return Kind::VolumeMixer;
  if (impl == "vpn") return Kind::Vpn;
  if (impl == "overview") return Kind::Overview;
  if (impl == "smenu") return Kind::Smenu;
  if (impl == "trash") return Kind::Trash;
  if (impl == "app_drawer") return Kind::AppDrawer;
  if (impl == "app_menu") return Kind::AppMenu;
  if (wid == "settings_button") return Kind::Settings;
  if (wid == "distro_spotlight") return Kind::Spotlight;
  if (wid == "spacer" || wid == "separator") return Kind::Separator;
  return Kind::App;
}

double panel_slot_width_uncached(PanelApp& app, const eh::config::ShellConfig& sc, const Slot& s, double icon,
                                 double boxH, const eh::mpris::PlayerSnapshot& mprisSnap) {
  switch (s.kind) {
    case Kind::Clock:
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, sc, s.key, icon, boxH);
    case Kind::Weather:
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, sc, s.key, icon, boxH);
    case Kind::Media:
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, sc, s.key, icon, boxH, mprisSnap);
    case Kind::Workspaces:
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, sc, s.key, icon, boxH,
                                                                    app.workspaceStrip);
    case Kind::ControlCenter:
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(sc, s.key, icon, boxH);
    case Kind::Battery:
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, sc, s.key, icon, boxH);
    case Kind::Bluetooth:
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, sc, s.key, icon, boxH);
    case Kind::WorldClock:
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, sc, s.key, icon, boxH);
    case Kind::Separator:
      return std::max(8.0, 12.0 * panel_ui_scale(app, sc));
    default:
      return icon;
  }
}

void panel_append_widget(PanelApp& app, const eh::config::ShellConfig& sc, std::vector<Slot>& out,
                         const std::string& wid, const PanelRunningSnapshot& runningSnap,
                         const std::vector<PanelTrayItem>& traySnap) {
  if (panel_widget_blocked(app, sc, wid)) return;
  if (wid == "pinned_apps" || wid == "running_apps") {
    for (const auto& g : runningSnap.groups) {
      Slot s;
      s.kind = Kind::App;
      s.key = g.key;
      s.chosenSerial = g.chosenSerial;
      s.anyActivated = g.anyActivated;
      out.push_back(std::move(s));
    }
    return;
  }
  if (eh::config::widget_token_is_system_tray(wid)) {
    for (const auto& t : traySnap) {
      Slot s;
      s.kind = Kind::Tray;
      s.key = t.service + '\x1f' + t.path;
      out.push_back(std::move(s));
    }
    return;
  }
  Slot s;
  s.kind = panel_kind_for_token(wid);
  if (s.kind == Kind::AppMenu || s.kind == Kind::Smenu || s.kind == Kind::Spotlight ||
      s.kind == Kind::AppDrawer)
    return;
  s.key = wid;
  out.push_back(std::move(s));
}

const PanelTrayItem* panel_find_tray(const std::vector<PanelTrayItem>& snap, const std::string& key) {
  for (const auto& t : snap)
    if (t.service + '\x1f' + t.path == key) return &t;
  return nullptr;
}

}  // namespace

PanelSectionWidths panel_measure_sections(PanelApp& app, const std::vector<std::string>& leftW,
                                          const std::vector<std::string>& centerW,
                                          const std::vector<std::string>& rightW,
                                          const PanelRunningSnapshot& runningSnap,
                                          const eh::mpris::PlayerSnapshot& mprisSnap) {
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double ui = panel_ui_scale(app, sc);
  const double barH = static_cast<double>(app.settings.height);
  const double iconRaw = static_cast<double>(app.settings.iconSize) * ui;
  const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, barH - 4.0));
  const double gap = static_cast<double>(app.settings.iconSpacing) * ui;

  std::vector<PanelTrayItem> traySnap;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySnap = app.trayItems;
  }

  auto build = [&](const std::vector<std::string>& ws) {
    std::vector<Slot> out;
    for (const auto& w : ws) panel_append_widget(app, sc, out, w, runningSnap, traySnap);
    return out;
  };
  auto width_of = [&](const std::vector<Slot>& slots) {
    double tw = 0.0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      const Slot& s = slots[i];
      double w = panel_slot_width_uncached(app, sc, s, icon, barH, mprisSnap);
      // Tray icons pack with no gap (single pill); app icons keep the gap.
      tw += w;
      if (i + 1 < slots.size()) {
        const Slot& n = slots[i + 1];
        if (s.kind == Kind::Tray && n.kind == Kind::Tray)
          tw += 0.0;
        else
          tw += gap;
      }
    }
    return tw;
  };

  PanelSectionWidths o;
  o.left = width_of(build(leftW));
  o.center = width_of(build(centerW));
  o.right = width_of(build(rightW));
  const double secGap = panel_section_gap_px(ui);
  o.total = o.left + o.center + o.right;
  if (o.left > 0.0 && (o.center > 0.0 || o.right > 0.0)) o.total += secGap;
  if (o.center > 0.0 && o.right > 0.0) o.total += secGap;
  return o;
}

void panel_paint_widget_bar(PanelApp& app, cairo_t* cr, double x, double y, double boxW, double boxH,
                            const std::vector<std::string>& leftW, const std::vector<std::string>& centerW,
                            const std::vector<std::string>& rightW, std::vector<PanelWidgetHit>* out_hits,
                            int hoverSlot, int pressedSlot, bool usePanel,
                            const PanelRunningSnapshot& runningSnap,
                            const eh::mpris::PlayerSnapshot& mprisSnap) {
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
  // Widget opacity follows the launcher "Widget opacity" card
  // (appearance.overlayOpacityWidgetCard), layered over the panel's own
  // capsule switch + opacity. A disabled capsule layer fades the pills out
  // entirely via a zero scale.
  {
    const double cardA = std::clamp(static_cast<double>(sc.appearance.overlayOpacityWidgetCard), 0.0, 1.0);
    const double capA =
        app.settings.capsuleEnabled ? std::clamp(static_cast<double>(app.settings.capsuleOpacity), 0.0, 100.0) / 100.0
                                    : 0.0;
    eh::widgets::slot_pill_style::g_opacityScale = cardA * capA;
  }
  {
    const double baseR = mc.dockFillR * 0.6 + mc.accentR * 0.15;
    const double baseG = mc.dockFillG * 0.6 + mc.accentG * 0.15;
    const double baseB = mc.dockFillB * 0.6 + mc.accentB * 0.15;
    eh::widgets::slot_pill_style::g_pillR = baseR;
    eh::widgets::slot_pill_style::g_pillG = baseG;
    eh::widgets::slot_pill_style::g_pillB = baseB;
  }
  eh::widgets::slot_pill_style::g_hoverAccentR = mc.accentR;
  eh::widgets::slot_pill_style::g_hoverAccentG = mc.accentG;
  eh::widgets::slot_pill_style::g_hoverAccentB = mc.accentB;
  const bool matugen = sc.appearance.anyPaletteActive();

  eh::shell::dock_slot_hooks::workspace_strip_poll(app.workspaceStrip, app.workspaceStripLastPoll,
                                                   app.compositorKind);
  panel_update_context_hide(app);

  const double ui = panel_ui_scale(app, sc);
  const double iconRaw = static_cast<double>(app.settings.iconSize) * ui;
  const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, boxH - 4.0));
  const double gap = static_cast<double>(app.settings.iconSpacing) * ui;

  std::vector<PanelTrayItem> traySnap;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySnap = app.trayItems;
  }
  std::sort(traySnap.begin(), traySnap.end(), [](const PanelTrayItem& a, const PanelTrayItem& b) {
    if (a.service != b.service) return a.service < b.service;
    return a.path < b.path;
  });
  traySnap.erase(std::remove_if(traySnap.begin(), traySnap.end(),
                                [](const PanelTrayItem& t) {
                                  return eh::tray::tray_item_hidden_by_env(t.id, t.title, t.service, t.path);
                                }),
                 traySnap.end());

  auto build = [&](const std::vector<std::string>& ws) {
    std::vector<Slot> out;
    for (const auto& w : ws) panel_append_widget(app, sc, out, w, runningSnap, traySnap);
    return out;
  };
  std::vector<Slot> left = build(leftW);
  std::vector<Slot> center = build(centerW);
  std::vector<Slot> right = build(rightW);
  std::vector<Slot> all;
  all.reserve(left.size() + center.size() + right.size());
  all.insert(all.end(), left.begin(), left.end());
  all.insert(all.end(), center.begin(), center.end());
  all.insert(all.end(), right.begin(), right.end());

  // Width cache for this frame: rich widgets measure text, so cache by kind+key.
  struct WKey {
    Kind kind = Kind::App;
    std::string key;
    bool operator==(const WKey& o) const { return kind == o.kind && key == o.key; }
  };
  struct WHash {
    std::size_t operator()(const WKey& k) const noexcept {
      std::size_t h = std::hash<int>{}(static_cast<int>(k.kind));
      h ^= std::hash<std::string>{}(k.key) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };
  std::unordered_map<WKey, double, WHash> wcache;
  auto slot_w = [&](const Slot& s) -> double {
    WKey k{s.kind, s.key};
    auto it = wcache.find(k);
    if (it != wcache.end()) return it->second;
    const double w = panel_slot_width_uncached(app, sc, s, icon, boxH, mprisSnap);
    wcache.emplace(std::move(k), w);
    return w;
  };
  auto gap_between = [&](const Slot& a, const Slot& b) -> double {
    if (a.kind == Kind::Tray && b.kind == Kind::Tray) return 0.0;
    return gap;
  };

  auto section_w = [&](const std::vector<Slot>& slots) {
    double tw = 0.0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      tw += slot_w(slots[i]);
      if (i + 1 < slots.size()) tw += gap_between(slots[i], slots[i + 1]);
    }
    return tw;
  };

  const double lw = section_w(left);
  const double cw = section_w(center);
  const double rw = section_w(right);

  const double stripPad = panel_strip_pad_px(ui);
  const double secGap = panel_section_gap_px(ui);

  double panelLeftX = x + stripPad;
  double panelCenterX = x + (boxW - cw) / 2.0;
  double panelRightX = x + boxW - stripPad - rw;
  {
    const double pad = secGap;
    const double innerL = x + stripPad;
    const double innerR = x + boxW - stripPad;
    if (!left.empty() && !right.empty() && panelLeftX + lw + pad > panelRightX) usePanel = false;
    if (usePanel && !left.empty() && !center.empty() && panelLeftX + lw + pad > panelCenterX)
      usePanel = false;
    if (usePanel && !center.empty() && !right.empty() && panelCenterX + cw + pad > panelRightX)
      usePanel = false;
    if (usePanel && !left.empty() && panelLeftX + lw > innerR + 1e-9) usePanel = false;
    if (usePanel && !center.empty() && (panelCenterX < innerL - 1e-9 || panelCenterX + cw > innerR + 1e-9))
      usePanel = false;
    if (usePanel && !right.empty() && (panelRightX < innerL - 1e-9 || panelRightX + rw > innerR + 1e-9))
      usePanel = false;
  }

  const double iconY = y + (boxH - icon) / 2.0;

  auto rounded_rect = [&](double rx, double ry, double rw0, double rh, double r) {
    const double x0 = rx, y0 = ry, x1 = rx + rw0, y1 = ry + rh;
    r = std::clamp(r, 0.0, std::min(rw0, rh) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  auto hover_overlay = [&](double ix, double y0, double slotW) {
    if (!app.settings.hoverHighlight) return;
    const double r = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
    rounded_rect(ix, y0, slotW, icon, r);
    cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR,
                          eh::widgets::slot_pill_style::g_hoverAccentG,
                          eh::widgets::slot_pill_style::g_hoverAccentB, 0.18);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR,
                          eh::widgets::slot_pill_style::g_hoverAccentG,
                          eh::widgets::slot_pill_style::g_hoverAccentB, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  };

  // Render one anchored section; returns next global slot index.
  int globalBase = 0;
  auto render_section = [&](const std::vector<Slot>& slots, double x0, int base) {
    double curX = x0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      const Slot& s = slots[i];
      const double slotW = slot_w(s);
      const double ix = curX;
      if (out_hits) {
        PanelWidgetHit h;
        h.widgetId = s.key;
        h.x = ix;
        h.y = iconY;
        h.w = slotW;
        h.h = icon;
        h.chosenSerial = s.chosenSerial;
        h.slotKind = static_cast<int>(s.kind);
        out_hits->push_back(std::move(h));
      }
      curX += slotW + (i + 1 < slots.size() ? gap_between(slots[i], slots[i + 1]) : 0.0);

      const int gIdx = base + static_cast<int>(i);
      const bool hovered = (hoverSlot >= 0 && gIdx == hoverSlot);
      const bool pressed = (pressedSlot >= 0 && gIdx == pressedSlot);
      // Taskbar parity: hover lift + press dip apply to app icon slots
      // only; wide widget slots paint their own internal feedback.
      double liftY = 0.0;
      if (s.kind == Kind::App) {
        if (hovered) liftY -= app.hoverLiftPx;
        if (pressed) liftY -= 2.0;
      }
      const double slotY = iconY + liftY;
      if (pressed && s.kind == Kind::App) {
        rounded_rect(ix - 2.0, slotY - 2.0, slotW + 4.0, icon + 4.0, 14.0);
        cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.58);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      }

      if (s.kind == Kind::Separator) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, ix + slotW / 2.0, slotY + 4.0);
        cairo_line_to(cr, ix + slotW / 2.0, slotY + icon - 4.0);
        cairo_stroke(cr);
        continue;
      }

      bool drew = false;
      if (s.kind == Kind::Clock) {
        eh::shell::dock_slot_hooks::paint_clock_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon, hovered, pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::Weather) {
        eh::shell::dock_slot_hooks::paint_weather_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon, hovered,
                                                       pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::Media) {
        const int zone = hovered ? eh::mpris::DockMpris::media_hit_zone(app.pointerX - ix, slotW, icon) : -1;
        (void)eh::shell::dock_slot_hooks::paint_media_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon,
                                                           mprisSnap, hovered, pressed, zone, app.pointerX,
                                                           app.pointerY);
        drew = true;
      } else if (s.kind == Kind::Workspaces) {
        (void)eh::shell::dock_slot_hooks::paint_workspaces_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon,
                                                                app.workspaceStrip, hovered, pressed, &app.icons,
                                                                &app.wsStripAnim);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::ControlCenter) {
        (void)eh::shell::dock_slot_hooks::paint_control_center_slot(cr, sc, s.key, ix, slotY, slotW, icon,
                                                                    icon, hovered, pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::Battery) {
        eh::shell::dock_slot_hooks::paint_battery_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon, hovered,
                                                       pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::Bluetooth) {
        eh::shell::dock_slot_hooks::paint_bluetooth_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon, hovered,
                                                         pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::WorldClock) {
        eh::shell::dock_slot_hooks::paint_world_clock_slot(cr, sc, s.key, ix, slotY, slotW, icon, icon, hovered,
                                                           pressed);
        if (hovered) hover_overlay(ix, slotY, slotW);
        drew = true;
      } else if (s.kind == Kind::Tray) {
        const PanelTrayItem* ti = panel_find_tray(traySnap, s.key);
        const bool firstTray = (i == 0 || slots[i - 1].kind != Kind::Tray);
        if (firstTray) {
          std::size_t runEnd = i;
          while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Kind::Tray) ++runEnd;
          const double runW = static_cast<double>(runEnd - i + 1) * icon;
          eh::widgets::slot_pill_style::paint_pill(cr, ix, slotY, runW, icon);
        }
        if (hovered) hover_overlay(ix, slotY, slotW);
        if (ti) {
          const double avail = std::max(4.0, std::min(icon * 0.6, icon - 6.0));
          if (!ti->iconName.empty()) {
            if (const auto* ic = app.icons.tray_icon(ti->iconName)) {
              if (ic->surface) {
                const double sc_i = std::min(avail / std::max(1, ic->width), avail / std::max(1, ic->height));
                const double dw = ic->width * sc_i;
                const double dh = ic->height * sc_i;
                const double dx = ix + (icon - dw) / 2.0;
                const double dy = slotY + (icon - dh) / 2.0;
                cairo_surface_t* scaled = app.scaledIcons.getOrScale(
                    ic->surface, std::max(1, static_cast<int>(std::ceil(dw))),
                    std::max(1, static_cast<int>(std::ceil(dh))));
                cairo_set_source_surface(cr, scaled ? scaled : ic->surface, dx, dy);
                cairo_paint(cr);
                drew = true;
              }
            }
          } else if (ti->pixSurface) {
            const int lw_img = cairo_image_surface_get_width(ti->pixSurface);
            const int lh_img = cairo_image_surface_get_height(ti->pixSurface);
            const double sc_i = std::min(avail / std::max(1, lw_img), avail / std::max(1, lh_img));
            const double dw = lw_img * sc_i;
            const double dh = lh_img * sc_i;
            const double dx = ix + (icon - dw) / 2.0;
            const double dy = slotY + (icon - dh) / 2.0;
            cairo_surface_t* scaled = app.scaledIcons.getOrScale(
                ti->pixSurface, std::max(1, static_cast<int>(std::ceil(dw))),
                std::max(1, static_cast<int>(std::ceil(dh))));
            cairo_set_source_surface(cr, scaled ? scaled : ti->pixSurface, dx, dy);
            cairo_paint(cr);
            drew = true;
          }
        }
      } else if (s.kind == Kind::App) {
        // Running app icon (from running_apps / pinned_apps expansion).
        eh::widgets::slot_pill_style::paint_pill(cr, ix, slotY, slotW, icon);
        if (hovered) hover_overlay(ix, slotY, slotW);
        if (const auto* ic = app.icons.app_icon(s.key)) {
          if (ic->surface) {
            const double avail = std::max(4.0, icon - 6.0);
            const double sc_i = std::min(avail / std::max(1, ic->width), avail / std::max(1, ic->height));
            const double dw = ic->width * sc_i;
            const double dh = ic->height * sc_i;
            cairo_surface_t* scaled = app.scaledIcons.getOrScale(
                ic->surface, std::max(1, static_cast<int>(std::ceil(dw))),
                std::max(1, static_cast<int>(std::ceil(dh))));
            cairo_set_source_surface(cr, scaled ? scaled : ic->surface, ix + (icon - dw) / 2.0,
                                     slotY + (icon - dh) / 2.0);
            cairo_paint(cr);
            drew = true;
          }
        }
        if (!drew) {
          eh::shell::draw_material_glyph(cr, ix + icon / 2.0, slotY + icon / 2.0, icon * 0.5, "apps", 1, 1, 1,
                                         0.9);
        }
        if (s.chosenSerial != 0) {
          const double dotR = 2.5;
          const double dotCy = slotY + icon + 5.0;
          cairo_new_path(cr);
          cairo_arc(cr, ix + slotW / 2.0, dotCy, dotR, 0, 2 * M_PI);
          if (s.anyActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
          else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
          cairo_fill(cr);
        }
      } else {
        // Generic indicator slot: pill + glyph. Covers settings_button,
        // smenu/app_menu, volume_mixer, vpn, notifications, trash, overview, etc.
        eh::widgets::slot_pill_style::paint_pill(cr, ix, slotY, slotW, icon);
        if (hovered) hover_overlay(ix, slotY, slotW);
        const char* glyph = "circle";
        if (s.kind == Kind::Settings)
          glyph = "settings";
        else if (s.kind == Kind::Overview)
          glyph = "dashboard";
        else if (s.kind == Kind::Smenu)
          glyph = "apps";
        else if (s.kind == Kind::AppMenu)
          glyph = "grid_view";
        else if (s.kind == Kind::VolumeMixer)
          glyph = "volume_up";
        else if (s.kind == Kind::Vpn)
          glyph = "vpn_key";
        else if (s.kind == Kind::Trash)
          glyph = "delete";
        else if (s.kind == Kind::Spotlight)
          glyph = "search";
        else if (s.kind == Kind::AppDrawer)
          glyph = "apps";
        if (matugen)
          eh::shell::draw_material_glyph(cr, ix + icon / 2.0, slotY + icon / 2.0, icon * 0.5, glyph, mc.accentR,
                                         mc.accentG, mc.accentB, 1.0);
        else
          eh::shell::draw_material_glyph(cr, ix + icon / 2.0, slotY + icon / 2.0, icon * 0.5, glyph, 1, 1, 1,
                                         1.0);
      }
      (void)drew;
    }
    (void)globalBase;
  };

  if (usePanel) {
    render_section(left, panelLeftX, 0);
    render_section(center, panelCenterX, static_cast<int>(left.size()));
    render_section(right, panelRightX, static_cast<int>(left.size() + center.size()));
    static uint64_t lastDumpMs = 0;
    const uint64_t nowMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    if (nowMs - lastDumpMs > 10000) {
      lastDumpMs = nowMs;
      std::string hs;
      if (out_hits) {
        for (const auto& hh : *out_hits) {
          char b[96];
          std::snprintf(b, sizeof(b), "[%s x=%.0f w=%.0f]", hh.widgetId.c_str(), hh.x, hh.w);
          hs += b;
        }
      }
      debug_log("panel-layout", "panel boxW=%.0f L=%zu(%.0f@%.0f) C=%zu(%.0f@%.0f) R=%zu(%.0f@%.0f) hits=%s",
                boxW, left.size(), lw, panelLeftX, center.size(), cw, panelCenterX, right.size(), rw,
                panelRightX, hs.c_str());
    }
    return;
  }

  // Fallback: single centered strip with horizontal compression.
  double totalW = 0.0;
  for (std::size_t i = 0; i < all.size(); ++i) {
    totalW += slot_w(all[i]);
    if (i + 1 < all.size()) totalW += gap_between(all[i], all[i + 1]);
  }
  const double avail = std::max(1.0, boxW - stripPad * 2.0);
  const double hscale = totalW > avail ? avail / totalW : 1.0;
  double cx = x + (boxW - totalW * hscale) / 2.0;
  cairo_save(cr);
  cairo_translate(cr, cx, 0);
  cairo_scale(cr, hscale, 1.0);
  cairo_translate(cr, -cx, 0);
  render_section(all, cx, 0);
  cairo_restore(cr);
  if (hscale < 1.0 - 1e-9 && out_hits) {
    for (auto& hh : *out_hits) {
      hh.x = cx + (hh.x - cx) * hscale;
      hh.w = hh.w * hscale;
    }
  }
  debug_log("panel-layout", "fallback boxW=%.0f totalW=%.0f hscale=%.3f hits=%zu", boxW, totalW,
            hscale, out_hits ? out_hits->size() : 0);
}

}  // namespace eh::shell::panel
