#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "wl/toplevel/workspaces.h"
#include "backends/interfaces/workspace_manager.h"

#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {

struct WorkspaceTextCache {
  std::string cacheKey;
  int fontPx = 0;
  std::vector<int> widths;
  std::vector<int> heights;
  PangoLayout* layout = nullptr;
  ~WorkspaceTextCache() { if (layout) g_object_unref(layout); }
  WorkspaceTextCache() = default;
  WorkspaceTextCache(WorkspaceTextCache&& o) noexcept
      : cacheKey(std::move(o.cacheKey)), fontPx(o.fontPx),
        widths(std::move(o.widths)), heights(std::move(o.heights)),
        layout(o.layout) {
    o.layout = nullptr;
  }
  WorkspaceTextCache& operator=(WorkspaceTextCache&& o) noexcept {
    if (layout) g_object_unref(layout);
    cacheKey = std::move(o.cacheKey);
    fontPx = o.fontPx;
    widths = std::move(o.widths);
    heights = std::move(o.heights);
    layout = o.layout;
    o.layout = nullptr;
    return *this;
  }
  WorkspaceTextCache(const WorkspaceTextCache&) = delete;
  WorkspaceTextCache& operator=(const WorkspaceTextCache&) = delete;
};
static std::unordered_map<std::string, WorkspaceTextCache> g_workspaceTextCache;

constexpr double kPrimR = 0.90, kPrimG = 0.90, kPrimB = 0.90;
constexpr double kSurfR = 0.96, kSurfG = 0.98, kSurfB = 0.99;

WorkspaceDisplayMode parse_display_mode(std::string_view instance_id, const eh::config::ShellConfig& sc) {
   
  const std::string raw = widget_setting(sc, instance_id, "display");
  if (raw == "name") return WorkspaceDisplayMode::Name;
  if (raw == "none") return WorkspaceDisplayMode::None;
  return WorkspaceDisplayMode::Id;
}

bool parse_show_workspace_apps(std::string_view instance_id, const eh::config::ShellConfig& sc) {
   
  const std::string raw = widget_setting(sc, instance_id, "show_apps");
  if (raw.empty()) return true;
  if (raw == "1" || raw == "true" || raw == "True" || raw == "yes") return true;
  if (raw == "0" || raw == "false" || raw == "False" || raw == "no") return false;
  return true;
}

int parse_max_workspace_icons(std::string_view instance_id, const eh::config::ShellConfig& sc) {
   
  const std::string raw = widget_setting(sc, instance_id, "max_workspace_icons");
  if (raw.empty()) return 4;
  char* end = nullptr;
  const long v = std::strtol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return 4;
  return static_cast<int>(std::clamp(v, 1L, 8L));
}

int workspace_max_slots_from_settings(const eh::config::ShellConfig& sc, std::string_view instance_id) {
   
  const std::string raw = widget_setting(sc, instance_id, "max_workspaces");
  if (raw.empty()) return 5;
  char* end = nullptr;
  const long v = std::strtol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return 5;
  return static_cast<int>(std::clamp(v, 0L, 32L));
}

void workspace_strip_apply_max_slots(std::vector<WorkspaceEntry>& out, int max_slots) {
   
  if (max_slots <= 0) return;
  std::sort(out.begin(), out.end(), [](const WorkspaceEntry& a, const WorkspaceEntry& b) {
    return a.id < b.id;
  });
  std::vector<WorkspaceEntry> result;
  result.reserve(static_cast<size_t>(max_slots));
  size_t idx = 0;
  for (int id = 1; id <= max_slots; ++id) {
    if (idx < out.size() && out[idx].id == id) {
      result.push_back(std::move(out[idx]));
      ++idx;
    } else {
      WorkspaceEntry ph{};
      ph.id = id;
      ph.label = std::to_string(id);
      result.push_back(std::move(ph));
    }
  }
  out = std::move(result);
}



PangoLayout* make_layout(cairo_t* cr, const char* font_desc) {
   
  PangoLayout* l = pango_cairo_create_layout(cr);
  PangoFontDescription* d = pango_font_description_from_string(font_desc);
  pango_layout_set_font_description(l, d);
  pango_font_description_free(d);
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
  return l;
}

void rrect(cairo_t* cr, double x, double y, double w, double h, double rad) {
   
  cairo_new_path(cr);
  cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
  cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3.0 * M_PI_2);
  cairo_close_path(cr);
}

std::string cell_text(const WorkspaceEntry& e, WorkspaceDisplayMode mode) {
   
  switch (mode) {
  case WorkspaceDisplayMode::None:
    return {};
  case WorkspaceDisplayMode::Id:
    return std::to_string(e.id);
  case WorkspaceDisplayMode::Name:
    return e.label.empty() ? std::to_string(e.id) : e.label;
  }
  return {};
}

bool layout_cells(cairo_t* measure_cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double icon_ref_px,
                  double slot_w, double slot_h, const std::vector<WorkspaceEntry>& entries,
                  std::vector<CellGeom>& cells_out, double& out_gap, double& out_hpad, double& out_pill_h) {
   
  cells_out.clear();
  if (entries.empty() || slot_w <= 1.0) return false;

  const double s = icon_ref_px / 30.0;
  out_hpad = std::max(6.0, 8.0 * s);
  out_gap = std::max(4.0, 6.0 * s);
  const WorkspaceDisplayMode mode = parse_display_mode(instance_id, sc);
  const double pill_h = slot_h * slot_pill_style::kPillHeightMul;
  out_pill_h = pill_h;
  const double dot_d = std::max(6.0, std::round(icon_ref_px * 0.20));
  const double pill_min = std::max(22.0, std::round(icon_ref_px * 0.52));
  const int font_px = std::max(9, static_cast<int>(std::lround(10.0 * s)));
  const bool show_apps = parse_show_workspace_apps(instance_id, sc);
  const int max_icons = parse_max_workspace_icons(instance_id, sc);

  cairo_t* scratch_cr = measure_cr;
  if (!scratch_cr) {
    scratch_cr = get_measure_cr();
  }

  std::string wsCacheKey;
  wsCacheKey += show_apps ? 'a' : 'A';
  wsCacheKey += std::to_string(max_icons);
  for (const auto& e : entries) {
    wsCacheKey += cell_text(e, mode);
    wsCacheKey += '\n';
    wsCacheKey += std::to_string(e.app_keys.size());
    wsCacheKey += ',';
  }
  wsCacheKey += std::to_string(font_px);
  const std::string wskid = std::string(instance_id) + ":L";
  auto wsit = g_workspaceTextCache.find(wskid);
  bool wsCacheHit = (wsit != g_workspaceTextCache.end() &&
                     wsit->second.cacheKey == wsCacheKey &&
                     wsit->second.fontPx == font_px);

  bool cacheRefreshed = false;
  std::vector<double> widths;
  widths.reserve(entries.size());
  if (wsCacheHit) {
    for (size_t wi = 0; wi < entries.size() && wi < wsit->second.widths.size(); ++wi) {
      widths.push_back(static_cast<double>(wsit->second.widths[wi]));
    }
  }
  if (widths.empty() && scratch_cr) {
    std::string fd = "Inter SemiBold " + std::to_string(font_px);
    std::vector<int> cwidths;
    cwidths.reserve(entries.size());
    for (const auto& e : entries) {
      if (show_apps && !e.app_keys.empty()) {
        const int n = std::min(max_icons, static_cast<int>(e.app_keys.size()));
        const double icon_cell = pill_h * 0.62;
        const double igap = std::max(2.0, 3.0 * s);
        const double ipad = std::max(4.0, 6.0 * s);
        const double iw = static_cast<double>(n) * icon_cell + static_cast<double>(std::max(0, n - 1)) * igap +
                            2.0 * ipad;
        widths.push_back(std::max(pill_min, iw));
        cwidths.push_back(static_cast<int>(std::max(pill_min, iw)));
        continue;
      }
      const std::string txt = cell_text(e, mode);
      if (mode == WorkspaceDisplayMode::None) {
        const double w = e.active ? pill_min : dot_d;
        widths.push_back(w);
        cwidths.push_back(static_cast<int>(w));
        continue;
      }
      if (txt.empty()) {
        widths.push_back(dot_d);
        cwidths.push_back(static_cast<int>(dot_d));
        continue;
      }
        PangoLayout* pl = make_layout(scratch_cr, fd.c_str());
      pango_layout_set_text(pl, txt.c_str(), -1);
      pango_layout_set_width(pl, -1);
      pango_layout_set_wrap(pl, PANGO_WRAP_NONE);
      pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_NONE);
      int tw = 0;
      int th = 0;
      pango_layout_get_pixel_size(pl, &tw, &th);
      g_object_unref(pl);
      const double w = std::max(pill_min, static_cast<double>(tw) + 12.0 * s);
      widths.push_back(w);
      cwidths.push_back(static_cast<int>(w));
    }
    cacheRefreshed = true;
    WorkspaceTextCache& wsc = g_workspaceTextCache[wskid];
    wsc.cacheKey = wsCacheKey;
    wsc.fontPx = font_px;
    wsc.widths = std::move(cwidths);
    wsc.heights.clear();
  } else if (widths.empty()) {
    for (const auto& e : entries) {
      if (show_apps && !e.app_keys.empty()) {
        const int n = std::min(max_icons, static_cast<int>(e.app_keys.size()));
        const double icon_cell = pill_h * 0.62;
        const double igap = std::max(2.0, 3.0 * s);
        const double ipad = std::max(4.0, 6.0 * s);
        const double iw = static_cast<double>(n) * icon_cell + static_cast<double>(std::max(0, n - 1)) * igap +
                            2.0 * ipad;
        widths.push_back(std::max(pill_min, iw));
        continue;
      }
      if (mode == WorkspaceDisplayMode::None)
        widths.push_back(e.active ? pill_min : dot_d);
      else
        widths.push_back(pill_min);
    }
  }
  double sum = 0.0;
  for (size_t i = 0; i < widths.size(); ++i) {
    sum += widths[i];
    if (i + 1 < widths.size()) sum += out_gap;
  }
  const double inner = std::max(1.0, slot_w - 2.0 * out_hpad);
  double scale = 1.0;
  if (sum > inner + 0.5 && sum > 1.0) scale = inner / sum;

  double x = out_hpad;
  for (size_t i = 0; i < widths.size(); ++i) {
    CellGeom c;
    c.x = x;
    c.w = std::max(4.0, widths[i] * scale);
    cells_out.push_back(c);
    x += c.w;
    if (i + 1 < widths.size()) x += out_gap * scale;
  }
  return cacheRefreshed;
}

static void draw_scaled_icon_with_alpha(cairo_t* cr, cairo_surface_t* surf, double ix, double iy, double cell_sz,
                                        double alpha) {
   
  if (!surf || alpha <= 0.001) return;
  const double sw = cairo_image_surface_get_width(surf);
  const double sh = cairo_image_surface_get_height(surf);
  if (sw <= 0 || sh <= 0) return;
  const double sc = std::min(cell_sz / sw, cell_sz / sh);
  cairo_save(cr);
  cairo_translate(cr, ix + (cell_sz - sw * sc) * 0.5, iy + (cell_sz - sh * sc) * 0.5);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, surf, 0, 0);
  cairo_paint_with_alpha(cr, alpha);
  cairo_restore(cr);
}

static void draw_steam_tile(cairo_t* cr, double ix, double iy, double cell_sz, double alpha) {
   
  const double rr = std::max(2.0, cell_sz * 0.22);
  rrect(cr, ix, iy, cell_sz, cell_sz, rr);
  cairo_set_source_rgba(cr, 0.55, 0.72, 0.94, 0.5 * alpha);
  cairo_fill(cr);
}

bool widget_list_contains_workspaces(const eh::config::ShellConfig&, const std::vector<std::string>& widgets) {
   
  for (const auto& id : widgets)
    if (eh::config::widget_implementation_type(id) == "workspaces") return true;
  return false;
}

void WsStripAnimState::updateTargets(const std::vector<CellGeom>& newCells, uint64_t nowMs) {
   
  if (toCells.empty()) {
    toCells = newCells;
    return;
  }
  fromCells.resize(newCells.size());
  for (size_t i = 0; i < newCells.size(); ++i) {
    if (active && i < toCells.size()) {
      fromCells[i] = {getX(i, nowMs), getW(i, nowMs)};
    } else if (i < toCells.size()) {
      fromCells[i] = toCells[i];
    } else {
      fromCells[i] = newCells[i];
    }
  }
  toCells = newCells;
  startMs = nowMs;
  active = true;
}

double WsStripAnimState::getX(size_t i, uint64_t nowMs) const {
  if (!active || i >= fromCells.size() || i >= toCells.size()) return (i < toCells.size()) ? toCells[i].x : 0.0;
  const float t = progress(nowMs);
  const float e = 1.0f - std::pow(1.0f - t, 3.0f);
  return fromCells[i].x + (toCells[i].x - fromCells[i].x) * static_cast<double>(e);
}

double WsStripAnimState::getW(size_t i, uint64_t nowMs) const {
  if (!active || i >= fromCells.size() || i >= toCells.size()) return (i < toCells.size()) ? toCells[i].w : 0.0;
  const float t = progress(nowMs);
  const float e = 1.0f - std::pow(1.0f - t, 3.0f);
  return fromCells[i].w + (toCells[i].w - fromCells[i].w) * static_cast<double>(e);
}

float WsStripAnimState::progress(uint64_t nowMs) const {
   
  const uint64_t elapsed = nowMs - startMs;
  if (elapsed >= durationMs) return 1.0f;
  return static_cast<float>(elapsed) / static_cast<float>(durationMs);
}

void workspace_strip_poll(std::vector<WorkspaceEntry>& out, timespec& last_poll_mono, CompositorKind /*kind*/) {
   
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (last_poll_mono.tv_sec != 0 || last_poll_mono.tv_nsec != 0) {
    const int64_t dt_ms =
        (now.tv_sec - last_poll_mono.tv_sec) * 1000 + (now.tv_nsec - last_poll_mono.tv_nsec) / 1000000;
    if (dt_ms >= 0 && dt_ms < 280 && !out.empty()) return;
  }
  last_poll_mono = now;

  if (auto* ww = global_wayland_workspaces(); ww != nullptr) {
    const auto workspaces = ww->allRegions();
    const auto appMap = ww->appsByDesk(nullptr);
    out.clear();
    out.reserve(workspaces.size());
    for (const auto& ws : workspaces) {
      WorkspaceEntry e;
      e.id = static_cast<int>(ws.index);
      e.label = ws.name.empty() ? ws.id : ws.name;
      e.active = ws.active;
      e.occupied = ws.occupied;
      e.urgent = ws.urgent;
      auto it = appMap.find(ws.id);
      if (it != appMap.end()) {
        e.app_keys = it->second;
      }
      out.push_back(std::move(e));
    }
  }
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const std::string wid = eh::config::workspaces_widget_instance_id(sc);
  const int ms = workspace_max_slots_from_settings(sc, wid);
  if (ms > 0) workspace_strip_apply_max_slots(out, ms);
}

double dock_workspaces_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                                  double icon_ref_px, double bar_height, const std::vector<WorkspaceEntry>& entries) {
   
  if (entries.empty()) return std::min(bar_height * 3.5, 120.0);

  const double s = icon_ref_px / 30.0;
  const double hPad = std::max(6.0, 8.0 * s);
  const double gap = std::max(4.0, 6.0 * s);
  const WorkspaceDisplayMode mode = parse_display_mode(instance_id, sc);
  const double dot_d = std::max(6.0, std::round(icon_ref_px * 0.20));
  const double pill_min = std::max(22.0, std::round(icon_ref_px * 0.52));
  const int font_px = std::max(9, static_cast<int>(std::lround(10.0 * s)));
  const double pill_h_meas = icon_ref_px * slot_pill_style::kPillHeightMul;
  const bool show_apps = parse_show_workspace_apps(instance_id, sc);
  const int max_icons = parse_max_workspace_icons(instance_id, sc);

  cairo_t* cr = measure_cr;
  if (!cr) {
    cr = get_measure_cr();
  }

  std::string fd = "Inter SemiBold " + std::to_string(font_px);
  double sum = 0.0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    double w = pill_min;
    if (show_apps && !e.app_keys.empty()) {
      const int n = std::min(max_icons, static_cast<int>(e.app_keys.size()));
      const double icon_cell = pill_h_meas * 0.62;
      const double igap = std::max(2.0, 3.0 * s);
      const double ipad = std::max(4.0, 6.0 * s);
      w = std::max(pill_min, static_cast<double>(n) * icon_cell + static_cast<double>(std::max(0, n - 1)) * igap +
                               2.0 * ipad);
    } else if (mode == WorkspaceDisplayMode::None) {
      w = e.active ? pill_min : dot_d;
    } else {
      const std::string txt = cell_text(e, mode);
      if (!txt.empty()) {
        PangoLayout* pl = make_layout(cr, fd.c_str());
        pango_layout_set_text(pl, txt.c_str(), -1);
        pango_layout_set_width(pl, -1);
        pango_layout_set_wrap(pl, PANGO_WRAP_NONE);
        pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_NONE);
        int tw = 0;
        int th = 0;
        pango_layout_get_pixel_size(pl, &tw, &th);
        g_object_unref(pl);
        w = std::max(pill_min, static_cast<double>(tw) + 12.0 * s);
      } else
        w = dot_d;
    }
    sum += w;
    if (i + 1 < entries.size()) sum += gap;
  }

  const double natural = 2.0 * hPad + sum;
  const double cap = std::min(bar_height * 22.0, 920.0);
  return std::clamp(natural, 60.0, cap);
}

bool paint_workspaces_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x,
                           double y, double slot_w, double slot_h, double icon_ref_px,
                           const std::vector<WorkspaceEntry>& entries, bool hovered, bool pressed,
                           eh::icons::IconCache* icon_cache, WsStripAnimState* anim) {
   
  if (slot_w <= 1.0) return false;
  (void)hovered;
  const double pill_h = slot_h * slot_pill_style::kPillHeightMul;
  const double pill_y = y + (slot_h - pill_h) * 0.5;

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, pill_y, slot_w, pill_h);

  slot_pill_style::pill_clip_path(cr, x, pill_y, slot_w, pill_h);
  cairo_clip(cr);

  if (entries.empty()) {
    PangoLayout* pl = make_layout(cr, "Sans SemiBold 10");
    pango_layout_set_text(pl, "\342\200\224", -1);
    int tw = 0;
    int th = 0;
    pango_layout_get_pixel_size(pl, &tw, &th);
    cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.45);
    cairo_move_to(cr, x + (slot_w - tw) * 0.5, pill_y + (pill_h - th) * 0.5);
    pango_cairo_show_layout(cr, pl);
    g_object_unref(pl);
    cairo_restore(cr);
    return false;
  }

  std::vector<CellGeom> cells;
  double gap = 0;
  double hpad = 0;
  double ph = 0;
  bool needsRedraw = false;

  if (layout_cells(cr, sc, instance_id, icon_ref_px, slot_w, slot_h, entries, cells, gap, hpad, ph))
    needsRedraw = true;
  (void)ph;

  const auto nowMs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  if (anim) {
    if (anim->toCells.size() != cells.size()) {
      anim->updateTargets(cells, nowMs);
    } else {
      for (size_t i = 0; i < cells.size(); ++i) {
        if (std::abs(anim->toCells[i].x - cells[i].x) > 0.5 ||
            std::abs(anim->toCells[i].w - cells[i].w) > 0.5) {
          anim->updateTargets(cells, nowMs);
          break;
        }
      }
    }
    if (anim->active) {
      const float t = anim->progress(nowMs);
      if (t >= 1.0f) {
        anim->active = false;
      } else {
        for (size_t i = 0; i < cells.size(); ++i) {
          cells[i].x = anim->getX(i, nowMs);
          cells[i].w = anim->getW(i, nowMs);
        }
        needsRedraw = true;
      }
    }
  }

  const WorkspaceDisplayMode mode = parse_display_mode(instance_id, sc);
  const double s = icon_ref_px / 30.0;
  const int font_px = std::max(9, static_cast<int>(std::lround(10.0 * s)));
  std::string fd = "Inter SemiBold " + std::to_string(font_px);
  const bool show_apps = parse_show_workspace_apps(instance_id, sc);
  const int max_icons = parse_max_workspace_icons(instance_id, sc);

  std::string paintWSCacheKey;
  for (const auto& e : entries) {
    paintWSCacheKey += cell_text(e, mode);
    paintWSCacheKey += '\n';
  }
  paintWSCacheKey += std::to_string(font_px);
  const std::string paintWskid = std::string(instance_id) + ":P";

  auto pit = g_workspaceTextCache.find(paintWskid);
  bool paintCacheHit = (pit != g_workspaceTextCache.end() &&
                        pit->second.cacheKey == paintWSCacheKey &&
                        pit->second.fontPx == font_px);
  WorkspaceTextCache* pentry = paintCacheHit ? &pit->second : &g_workspaceTextCache[paintWskid];
  WorkspaceTextCache& prec = *pentry;
  if (!paintCacheHit) {
    prec.cacheKey = paintWSCacheKey;
    prec.fontPx = font_px;
    prec.widths.clear();
    prec.heights.clear();
  }
  if (!prec.layout || prec.fontPx != font_px) {
    if (prec.layout) g_object_unref(prec.layout);
    prec.layout = make_layout(cr, fd.c_str());
  } else {
    pango_cairo_update_layout(cr, prec.layout);
  }

  const bool matugenChrome = sc.appearance.anyPaletteActive();
  const eh::config::ChromePaintColors chrome = eh::config::derived_chrome_colors(sc.appearance);
  const double activeFillR = matugenChrome ? chrome.accentR : kPrimR;
  const double activeFillG = matugenChrome ? chrome.accentG : kPrimG;
  const double activeFillB = matugenChrome ? chrome.accentB : kPrimB;
  const double activeTxR = matugenChrome ? kSurfR : 0.04;
  const double activeTxG = matugenChrome ? kSurfG : 0.10;
  const double activeTxB = matugenChrome ? kSurfB : 0.09;

  std::vector<int> paintWidths;
  std::vector<int> paintHeights;
  bool paintCacheMiss = false;

  for (size_t i = 0; i < entries.size() && i < cells.size(); ++i) {
    const auto& e = entries[i];
    const CellGeom& c = cells[i];
    const double cx = x + c.x;
    const double cw = c.w;
    const double rr = std::min(cw * 0.5, pill_h * 0.5) - 1.0;

    if (e.active) {
      rrect(cr, cx, pill_y + 2.0, cw, pill_h - 4.0, std::max(2.0, rr * 0.5));
      cairo_set_source_rgba(cr, activeFillR, activeFillG, activeFillB, pressed ? 0.85 : 0.75);
      cairo_fill(cr);
    } else if (e.occupied) {
      rrect(cr, cx, pill_y + 2.0, cw, pill_h - 4.0, std::max(2.0, rr * 0.5));
      cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.55);
      cairo_fill(cr);
    } else {
      rrect(cr, cx, pill_y + 2.0, cw, pill_h - 4.0, std::max(2.0, rr * 0.5));
      cairo_set_source_rgba(cr, 0.22, 0.24, 0.26, 0.45);
      cairo_fill(cr);
    }

    const std::string txt = cell_text(e, mode);
    const double icon_alpha = e.active ? 1.0 : 0.62;

    if (show_apps && !e.app_keys.empty() && icon_cache) {
      const int n = std::min(max_icons, static_cast<int>(e.app_keys.size()));
      const double icon_cell = pill_h * 0.62;
      const double igap = std::max(2.0, 3.0 * s);
      const double row_w =
          static_cast<double>(n) * icon_cell + static_cast<double>(std::max(0, n - 1)) * igap;
      const double ix0 = cx + (cw - row_w) * 0.5;
      const double iy0 = pill_y + (pill_h - icon_cell) * 0.5;
      for (int ki = 0; ki < n; ++ki) {
        const std::string& ak = e.app_keys[static_cast<size_t>(ki)];
        const double ixx = ix0 + static_cast<double>(ki) * (icon_cell + igap);
        if (ak.find("steam_app") != std::string::npos) {
          draw_steam_tile(cr, ixx, iy0, icon_cell, icon_alpha);
        } else if (const eh::icons::IconEntry* ent = icon_cache->app_icon(ak)) {
          draw_scaled_icon_with_alpha(cr, ent->surface, ixx, iy0, icon_cell, icon_alpha);
        } else {
          const double rr = std::max(2.0, icon_cell * 0.22);
          rrect(cr, ixx, iy0, icon_cell, icon_cell, rr);
          cairo_set_source_rgba(cr, 0.55, 0.60, 0.65, 0.25 * icon_alpha);
          cairo_fill(cr);
        }
      }
    } else if (!txt.empty()) {
      PangoLayout* pl = prec.layout;
      pango_layout_set_text(pl, txt.c_str(), -1);
      pango_layout_set_width(pl, -1);
      pango_layout_set_wrap(pl, PANGO_WRAP_NONE);
      pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_NONE);
      pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);
      int tw = 0;
      int th = 0;
      if (paintCacheHit && i < prec.widths.size()) {
        tw = prec.widths[i];
        th = prec.heights[i];
      } else {
        pango_layout_get_pixel_size(pl, &tw, &th);
        paintCacheMiss = true;
      }
      if (paintCacheMiss) {
        paintWidths.push_back(tw);
        paintHeights.push_back(th);
      }
      cairo_set_source_rgba(cr, e.active ? activeTxR : kSurfR, e.active ? activeTxG : kSurfG,
                            e.active ? activeTxB : kSurfB, e.active ? 1.0 : 0.88);
      cairo_move_to(cr, cx + (cw - static_cast<double>(tw)) * 0.5,
                    pill_y + (pill_h - static_cast<double>(th)) * 0.5);
      pango_cairo_show_layout(cr, pl);
    }
  }

  if (paintCacheMiss) {
    prec.widths = paintWidths;
    prec.heights = paintHeights;
  }
  cairo_restore(cr);
  return needsRedraw;
}

int workspaces_pick_index(double local_x, double slot_w, const eh::config::ShellConfig& sc, std::string_view instance_id,
                          double icon_ref_px, const std::vector<WorkspaceEntry>& entries) {
   
  if (entries.empty() || slot_w <= 1.0) return -1;
  std::vector<CellGeom> cells;
  double gap = 0;
  double hpad = 0;
  double ph = 0;
  (void)layout_cells(nullptr, sc, instance_id, icon_ref_px, slot_w, icon_ref_px, entries, cells, gap, hpad, ph);
  if (cells.empty()) return -1;
  int best = 0;
  double bestDist = std::abs(local_x - (cells[0].x + cells[0].w * 0.5));
  for (size_t i = 1; i < cells.size(); ++i) {
    const double dist = std::abs(local_x - (cells[i].x + cells[i].w * 0.5));
    if (dist < bestDist) {
      bestDist = dist;
      best = static_cast<int>(i);
    }
  }
  return best;
}

void workspace_activate_entry(const WorkspaceEntry& e, CompositorKind /*kind*/) {
    
  if (e.id <= 0) return;
  if (auto* ww = global_wayland_workspaces(); ww != nullptr) {
    ww->jumpTo(std::to_string(e.id));
  }
}

} // namespace eh::widgets
