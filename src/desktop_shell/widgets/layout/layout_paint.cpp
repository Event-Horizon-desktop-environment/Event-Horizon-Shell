#include "desktop_shell/widgets/layout/layout_paint.hpp"

#include "backends/hyprland/hyprland_backends.h"
#include "backends/mango/mango_backends.h"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

namespace eh::widgets {
namespace {

// Union of every layout id both backends understand. `hypr`/`mango`
// mark which backend offers the entry (wiki: mangowm/mango "layouts").
struct LayoutDef {
  const char* id;
  const char* glyph;
  const char* title;
  const char* desc;
  bool hypr;
  bool mango;
};

constexpr LayoutDef kLayouts[] = {
    // Hyprland set.
    {"dwindle", "account_tree", "Dwindle", "Auto-tiling splits", true, true},
    {"master", "view_sidebar", "Master", "Master + stack", true, false},
    {"scrolling", "view_column", "Scrolling", "Column strip", true, false},
    {"monocle", "fullscreen", "Monocle", "Single maximized", true, true},
    // MangoWM set (wiki "Supported Layouts").
    {"tile", "view_sidebar", "Tile", "Master-stack tiling", false, true},
    {"scroller", "view_column", "Scroller", "Scrollable strip", false, true},
    {"grid", "grid_view", "Grid", "Even grid", false, true},
    {"deck", "view_agenda", "Deck", "Master + card stack", false, true},
    {"center_tile", "vertical_split", "Center Tile", "Centered master", false, true},
    {"vertical_tile", "splitscreen", "Vertical Tile", "Vertical master-stack", false, true},
    {"right_tile", "horizontal_split", "Right Tile", "Master on the right", false, true},
    {"vertical_scroller", "view_stream", "V-Scroller", "Vertical strip", false, true},
    {"vertical_grid", "view_module", "V-Grid", "Vertical grid", false, true},
    {"vertical_deck", "layers", "V-Deck", "Vertical card stack", false, true},
    {"fair", "dashboard", "Fair", "Even split", false, true},
    {"vertical_fair", "space_dashboard", "V-Fair", "Even vertical split", false, true},
};

// Presentation order for the Mango popup: most common first, default on top.
// (Wiki lists 14 supported layouts; the union table above is grouped by
// backend, so Mango gets its own curated order here.)
constexpr const char* kMangoOrder[] = {
    "tile", "scroller", "monocle", "grid", "deck", "dwindle", "center_tile", "vertical_tile",
    "right_tile", "vertical_scroller", "vertical_grid", "vertical_deck", "fair", "vertical_fair",
};
constexpr int kMangoCount = static_cast<int>(sizeof(kMangoOrder) / sizeof(kMangoOrder[0]));

std::string to_lower_trim(std::string_view v) {
  size_t b = 0;
  while (b < v.size() && static_cast<unsigned char>(v[b]) <= ' ') ++b;
  size_t e = v.size();
  while (e > b && static_cast<unsigned char>(v[e - 1]) <= ' ') --e;
  std::string out;
  out.reserve(e - b);
  for (size_t i = b; i < e; ++i)
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(v[i]))));
  return out;
}

const LayoutDef* find_def(std::string_view name) {
  const std::string n = to_lower_trim(name);
  for (const auto& d : kLayouts)
    if (n == d.id) return &d;
  if (n == "monicle") {  // common misspelling of monocle
    for (const auto& d : kLayouts)
      if (std::string_view(d.id) == "monocle") return &d;
  }
  return nullptr;
}

std::mutex g_mu;
std::string g_cachedHypr = "dwindle";
std::string g_cachedMango = "tile";
std::chrono::steady_clock::time_point g_lastHyprPoll{};
std::chrono::steady_clock::time_point g_lastMangoPoll{};

// Debug override: EH_LAYOUT_DEBUG=<any known id>
std::string debug_override() {
  const char* e = std::getenv("EH_LAYOUT_DEBUG");
  if (!e || !e[0]) return {};
  if (const LayoutDef* d = find_def(e)) return {d->id};
  return {};
}

// --- Hyprland ---

bool hypr_response_has_layout(const std::string& resp, std::string* out) {
  const std::string low = to_lower_trim(resp);
  for (const auto& d : kLayouts) {
    if (!d.hypr) continue;
    if (low.find(d.id) != std::string::npos) {
      if (out) *out = d.id;
      return true;
    }
  }
  return false;
}

std::string query_hypr_live() {
  if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE") == nullptr) return {};
  // JSON first (matches j/status, j/clients, ... convention), then plain.
  if (auto r = hyprland_ipc_request("j/getoption general:layout")) {
    std::string found;
    if (hypr_response_has_layout(*r, &found)) return found;
  }
  if (auto r = hyprland_ipc_request("getoption general:layout")) {
    std::string found;
    if (hypr_response_has_layout(*r, &found)) return found;
  }
  return {};
}

std::string read_hypr_persisted() {
  try {
    auto cfg = eh::settings_hyprland::read_config();
    if (const LayoutDef* d = find_def(cfg.general.layout); d && d->hypr) return {d->id};
  } catch (...) {
  }
  return {"dwindle"};
}

// --- MangoWM ---
// Layouts are per-tag runtime state; the compositor is the source of truth.
// The wiki IPC page documents `dispatch setlayout,<name>` for switching
// (cf. `bind=SUPER,t,setlayout,tile`). For reading, probe the one-shot
// `get` endpoints and pick up any "*layout*" string field matching a known
// Mango id — if the compositor does not report one, keep the last applied
// value (default "tile").

bool mango_json_scan_layout(const nlohmann::json& j, std::string* out) {
  if (j.is_string()) {
    if (const LayoutDef* d = find_def(j.get<std::string>()); d && d->mango) {
      if (out) *out = d->id;
      return true;
    }
    return false;
  }
  if (j.is_array()) {
    for (const auto& item : j)
      if (mango_json_scan_layout(item, out)) return true;
    return false;
  }
  if (j.is_object()) {
    // Prefer fields whose key mentions layout (avoids matching a window
    // titled e.g. "tile"), then fall back to a full recursive scan.
    for (const auto& [k, v] : j.items()) {
      const std::string kl = to_lower_trim(k);
      if (kl.find("layout") == std::string::npos) continue;
      if (v.is_string()) {
        if (const LayoutDef* d = find_def(v.get<std::string>()); d && d->mango) {
          if (out) *out = d->id;
          return true;
        }
      } else if (mango_json_scan_layout(v, out)) {
        return true;
      }
    }
    for (const auto& [k, v] : j.items()) {
      const std::string kl = to_lower_trim(k);
      if (kl.find("layout") != std::string::npos) continue;
      if (mango_json_scan_layout(v, out)) return true;
    }
  }
  return false;
}

std::string query_mango_live() {
  if (std::getenv("MANGO_INSTANCE_SIGNATURE") == nullptr) return {};
  // Fresh runtime per query: ipcQuery() mutates the endpoint cache, so a
  // shared instance would need external locking across paint threads.
  wspace::mango::MangoSession rt;
  if (!rt.available()) return {};
  // NOTE: structured bindings over json::items() need C++17 compatible
  // iteration; kept explicit for -Werror builds.
  if (auto r = rt.ipcQuery("get all-monitors")) {
    std::string found;
    if (mango_json_scan_layout(*r, &found)) return found;
  }
  if (auto r = rt.ipcQuery("get all-tags")) {
    std::string found;
    if (mango_json_scan_layout(*r, &found)) return found;
  }
  return {};
}

}  // namespace

LayoutBackend layout_backend() {
  return is_mango() ? LayoutBackend::Mango : LayoutBackend::Hyprland;
}

int layout_count() {
  if (layout_backend() == LayoutBackend::Mango) return kMangoCount;
  int n = 0;
  for (const auto& d : kLayouts)
    if (d.hypr) ++n;
  return n;
}

const char* layout_id_at(int idx) {
  if (layout_backend() == LayoutBackend::Mango) {
    if (idx < 0 || idx >= kMangoCount) return "tile";
    return kMangoOrder[idx];
  }
  int i = 0;
  for (const auto& d : kLayouts) {
    if (!d.hypr) continue;
    if (i == idx) return d.id;
    ++i;
  }
  return "dwindle";
}

int layout_index_for(std::string_view name) {
  std::string n = to_lower_trim(name);
  if (n == "monicle") n = "monocle";  // common misspelling
  if (layout_backend() == LayoutBackend::Mango) {
    for (int i = 0; i < kMangoCount; ++i)
      if (n == kMangoOrder[i]) return i;
    return -1;
  }
  int i = 0;
  for (const auto& d : kLayouts) {
    if (!d.hypr) continue;
    if (n == d.id) return i;
    ++i;
  }
  return -1;
}

const char* layout_glyph_for(std::string_view name) {
  if (const LayoutDef* d = find_def(name)) return d->glyph;
  return layout_backend() == LayoutBackend::Mango ? "view_sidebar" : "grid_view";
}

const char* layout_title_for(std::string_view name) {
  if (const LayoutDef* d = find_def(name)) return d->title;
  return layout_backend() == LayoutBackend::Mango ? "Tile" : "Dwindle";
}

const char* layout_desc_for(std::string_view name) {
  if (const LayoutDef* d = find_def(name)) return d->desc;
  return "";
}

std::string layout_normalize(std::string_view name) {
  if (const LayoutDef* d = find_def(name)) return {d->id};
  return layout_backend() == LayoutBackend::Mango ? std::string("tile") : std::string("dwindle");
}

std::string layout_current() {
  if (std::string ov = debug_override(); !ov.empty()) return ov;
  std::lock_guard<std::mutex> lock(g_mu);
  const auto now = std::chrono::steady_clock::now();
  if (layout_backend() == LayoutBackend::Mango) {
    if (now - g_lastMangoPoll < std::chrono::seconds(1) && !g_cachedMango.empty()) return g_cachedMango;
    g_lastMangoPoll = now;
    if (std::string live = query_mango_live(); !live.empty()) g_cachedMango = live;
    return g_cachedMango;
  }
  if (now - g_lastHyprPoll < std::chrono::seconds(1) && !g_cachedHypr.empty()) return g_cachedHypr;
  g_lastHyprPoll = now;
  if (std::string live = query_hypr_live(); !live.empty()) {
    g_cachedHypr = live;
    return g_cachedHypr;
  }
  // No compositor (or query failed): fall back to persisted config so the
  // slot still reflects what Hyprland will use on next reload.
  g_cachedHypr = read_hypr_persisted();
  return g_cachedHypr;
}

bool layout_apply(const std::string& name) {
  const LayoutDef* d = find_def(name);
  if (!d) return false;
  const bool mango = layout_backend() == LayoutBackend::Mango;
  if (mango ? !d->mango : !d->hypr) return false;
  const std::string norm(d->id);
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (mango) {
      g_cachedMango = norm;
      g_lastMangoPoll = std::chrono::steady_clock::now();
    } else {
      g_cachedHypr = norm;
      g_lastHyprPoll = std::chrono::steady_clock::now();
    }
  }
  if (mango) {
    // Wiki "Switching Layouts": `bind=SUPER,t,setlayout,tile`.
    if (std::getenv("MANGO_INSTANCE_SIGNATURE") == nullptr) return false;
    wspace::mango::MangoSession rt;
    return rt.available() && rt.runCommand("setlayout," + norm);
  }
  bool liveOk = false;
  if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE") != nullptr) {
    if (auto resp = hyprland_ipc_request("keyword general:layout " + norm)) {
      std::string tail = *resp;
      while (!tail.empty() && static_cast<unsigned char>(tail.back()) <= ' ') tail.pop_back();
      liveOk = (tail == "ok");
    }
  }
  // Persist so the choice survives reloads / restarts.
  bool persistOk = false;
  try {
    auto cfg = eh::settings_hyprland::read_config();
    cfg.general.layout = norm;
    persistOk = eh::settings_hyprland::write_general(cfg.general);
  } catch (...) {
    persistOk = false;
  }
  if (!liveOk && persistOk) {
    // Keyword path unavailable (e.g. older Hyprland): full reload applies it.
    try {
      liveOk = eh::settings_hyprland::apply_config();
    } catch (...) {
    }
  }
  return liveOk || persistOk;
}

bool widget_list_contains_layout(const eh::config::ShellConfig& sc,
                                 const std::vector<std::string>& widgets) {
  (void)sc;
  for (const auto& w : widgets)
    if (eh::config::widget_implementation_type(w) == "layout") return true;
  return false;
}

double dock_layout_slot_width(cairo_t* /*measure_cr*/, const eh::config::ShellConfig& sc,
                              std::string_view /*instance_id*/, double icon_ref_px,
                              double /*bar_height*/) {
  const double us = std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
  // Icon-only pill, same footprint as bluetooth/vpn slots.
  return icon_ref_px + 4.0 * us;
}

void paint_layout_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view /*instance_id*/,
                       double x, double y, double slot_w, double slot_h, double icon_ref_px,
                       bool hovered, bool pressed) {
  const std::string cur = layout_current();
  const char* glyph = layout_glyph_for(cur);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const bool matugen = sc.appearance.anyPaletteActive();

  eh::widgets::slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);
  if (hovered || pressed) {
    const double r = eh::widgets::slot_pill_style::corner_radius(slot_h, slot_w);
    cairo_new_sub_path(cr);
    const double rad = r;
    cairo_arc(cr, x + slot_w - rad, y + rad, rad, -M_PI_2, 0);
    cairo_arc(cr, x + slot_w - rad, y + slot_h - rad, rad, 0, M_PI_2);
    cairo_arc(cr, x + rad, y + slot_h - rad, rad, M_PI_2, M_PI);
    cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    if (matugen)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, pressed ? 0.28 : 0.18);
    else
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, pressed ? 0.14 : 0.08);
    cairo_fill_preserve(cr);
    if (matugen)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
    else
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.22);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  const double px = icon_ref_px * 0.52;
  const double cx = x + slot_w * 0.5;
  const double cy = y + slot_h * 0.5;
  if (matugen)
    eh::shell::draw_material_glyph(cr, cx, cy, px, glyph, mc.accentR, mc.accentG, mc.accentB, 1.0);
  else
    eh::shell::draw_material_glyph(cr, cx, cy, px, glyph, 1.0, 1.0, 1.0, 1.0);
}

}  // namespace eh::widgets
