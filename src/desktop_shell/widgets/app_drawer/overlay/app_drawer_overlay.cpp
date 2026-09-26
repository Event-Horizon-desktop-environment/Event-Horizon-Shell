#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/widgets/app_drawer/tahoe/tahoe_launcher.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"


#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <time.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Tahoe unified launcher: single full-width layout, no pinned sidebar.
// (Pinned data stays for context menus; presentation follows Apple's Apps
// browser: search + count pills + icon grid + slim power footer.)
constexpr int kPopupW = 858;
constexpr int kPopupH = 680;

constexpr double kMar = 20.0;
constexpr double kPad = 12.0;
constexpr double kPadS = 8.0;
constexpr double kPadXS = 4.0;

constexpr double kSearchH = 46.0;

constexpr double kRowPitch = 64.0;
constexpr double kIconSz = 46.0;

constexpr double kPowerBtnSz = 32.0;
constexpr double kPowerBtnR = 8.0;
constexpr int kPowerBtnCount = 4;
constexpr int kNightlightBtnIdx = 4;
constexpr int kFooterBtnCount = 5;

constexpr double kFieldR = 14.0;
constexpr double kRowR = 12.0;

using eh::shell::tahoe::TahoeLayout;

TahoeLayout tahoe_geom(double W, double H) {
  return eh::shell::tahoe::tahoe_layout(W, H);
}

struct Rect {
  double x, y, w, h;
};

Rect search_rect(double W, double H) {
  const TahoeLayout l = tahoe_geom(W, H);
  return {l.searchX, l.searchY, l.searchW, l.searchH};
}

double list_top(double W, double H) {
  const TahoeLayout l = tahoe_geom(W, H);
  return l.gridY;
}

double list_bottom(double W, double H) {
  const TahoeLayout l = tahoe_geom(W, H);
  return l.contentBottom;
}

// Slim centered power footer (EH need; Tahoe has none so this stays quiet).
Rect power_btn_rect(double W, int idx, double H) {
  const TahoeLayout l = tahoe_geom(W, H);
  const double rowY = l.footerY + (l.footerH - kPowerBtnSz) * 0.5;
  const double totalBtnW = kFooterBtnCount * kPowerBtnSz + (kFooterBtnCount - 1) * kPadXS;
  const double startX = (W - totalBtnW) * 0.5;
  const double x = startX + idx * (kPowerBtnSz + kPadXS);
  return {x, rowY, kPowerBtnSz, kPowerBtnSz};
}

static const eh::icons::IconEntry* catalog_list_icon(eh::icons::IconCache& icons, const SpotlightHit& hit) {
   
  const std::string stem = eh_app_drawer_desktop_stem_from_path(hit.path);
  if (!stem.empty()) {
    if (const eh::icons::IconEntry* ic = icons.app_icon(stem)) {
      if (ic->surface) return ic;
    }
  }
  if (const eh::icons::IconEntry* ic = icons.tray_icon(hit.iconKey)) {
    if (ic->surface) return ic;
  }
  if (!hit.iconKey.empty() && hit.iconKey != stem) {
    if (const eh::icons::IconEntry* ic = icons.app_icon(hit.iconKey)) {
      if (ic->surface) return ic;
    }
  }
  return nullptr;
}



static void truncate_to_width(cairo_t* cr, const std::string& text, double maxAdv, std::string* out) {
  if (text.empty()) { *out = text; return; }
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, text.c_str(), &ex);
  if (ex.x_advance <= maxAdv) { *out = text; return; }

  size_t lo = 1;
  size_t hi = text.size();
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    while (mid < text.size() && (static_cast<unsigned char>(text[mid]) & 0xc0u) == 0x80u) --mid;
    if (mid == 0) { lo = 0; break; }
    cairo_text_extents(cr, text.substr(0, mid).c_str(), &ex);
    if (ex.x_advance <= maxAdv) lo = mid;
    else hi = mid - 1;
  }
  out->assign(text, 0, lo);
}

struct PowerBtn {
  const char* ligature;
  const char* tooltip;
};
static constexpr PowerBtn kPowerBtns[] = {
    {"lock", "Lock"},
    {"logout", "Logout"},
    {"restart_alt", "Restart"},
    {"power_settings_new", "Shutdown"},
    {"dark_mode", "Nightlight"},
};
}

namespace eh::shell::dock::app_drawer {

static bool s_nightlight_active = false;
static void (*s_nightlight_toggle_fn)() = nullptr;

void set_nightlight_active(bool active) { s_nightlight_active = active; }
bool get_nightlight_active() { return s_nightlight_active; }
void set_nightlight_toggle_fn(void (*fn)()) { s_nightlight_toggle_fn = fn; }

int app_drawer_popup_width() { return kPopupW; }
int app_drawer_popup_height() { return kPopupH; }

void set_nightlight_active(bool);
bool get_nightlight_active();
void set_nightlight_toggle_fn(void (*)());

void app_drawer_update_categories(AppDrawerState& s) {
  // Tahoe buckets (fixed order) with live catalog counts.
  s.categories.clear();
  s.categoryWidths.clear();
  s.categoryCounts.clear();
  const auto& entries = get_cached_entries();
  int counts[eh::shell::tahoe::kTahoeBucketCount] = {};
  for (const auto& e : entries)
    counts[eh::shell::tahoe::tahoe_bucket_for(e.categories)]++;
  for (int b = 0; b < eh::shell::tahoe::kTahoeBucketCount; ++b) {
    s.categories.emplace_back(eh::shell::tahoe::kTahoeBuckets[b].label);
    s.categoryCounts.push_back(counts[b]);
  }
  if (s.selectedCategory >= static_cast<int>(s.categories.size()))
    s.selectedCategory = -1;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surf);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, eh::shell::tahoe::kTahoePillFontPx);
  constexpr double kPillPad = eh::shell::tahoe::kTahoePillPadX;
  cairo_text_extents_t te;
  auto pill_w = [&](const std::string& label, int count) {
    const std::string txt = label + " " + std::to_string(count);
    cairo_text_extents(cr, txt.c_str(), &te);
    return te.x_advance + kPillPad * 2.0;
  };
  // Counts stay parallel to widths: counts[0] = All total.
  s.categoryCounts.push_back(static_cast<int>(entries.size()));
  s.categoryWidths.push_back(pill_w("All", static_cast<int>(entries.size())));
  for (int b = 0; b < eh::shell::tahoe::kTahoeBucketCount; ++b) {
    s.categoryCounts.push_back(counts[b]);
    s.categoryWidths.push_back(pill_w(s.categories[static_cast<size_t>(b)], counts[b]));
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
}

int app_drawer_pick_category_tab(const AppDrawerState& s, double lx, double ly) {
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const TahoeLayout l = tahoe_geom(W, H);
  if (ly < l.pillsY || ly > l.pillsY + l.pillsH) return -1;
  const int nCats = static_cast<int>(s.categories.size());
  if (s.categoryWidths.size() < static_cast<size_t>(nCats + 1)) return -1;
  const auto xs = eh::shell::tahoe::tahoe_pill_xs(s.categoryWidths, l.pillsAvailX, l.pillsAvailW,
                                                  s.selectedCategory + 1);
  for (int i = -1; i < nCats; ++i) {
    const size_t k = static_cast<size_t>(i + 1);
    if (lx >= xs[k] && lx < xs[k] + s.categoryWidths[k]) return i;
  }
  return -1;
}

void app_drawer_clamp_scroll(AppDrawerState& s) {
   
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  const double listH = lb - lt;
  if (listH <= 1.0) {
    if (eh_app_drawer_debug_level() >= 3) {
      trace_line(3, "drawer-overlay", "clamp_scroll listH<=1 → scrollPx=0");
    }
    s.scrollPx = 0;
    return;
  }
  double contentH;
  if (s.viewMode == 0) {
    contentH = eh::shell::tahoe::tahoe_grid_content_h(s.hits.size(), tahoe_geom(W, H));
  } else {
    contentH = static_cast<double>(s.hits.size()) * kRowPitch;
  }
  double maxScroll = contentH - listH;
  if (maxScroll < 0) maxScroll = 0;
  const double before = s.scrollPx;
  s.scrollPx = std::max(0.0, std::min(s.scrollPx, maxScroll));
  s.scrollPxCurrent = std::max(0.0, std::min(s.scrollPxCurrent, maxScroll));
  if (eh_app_drawer_debug_level() >= 3 && before != s.scrollPx) {
    trace_line(3, "drawer-overlay",
                              "clamp_scroll before=" + std::to_string(before) + " after=" + std::to_string(s.scrollPx) +
                                  " maxScroll=" + std::to_string(maxScroll) + " hits=" + std::to_string(s.hits.size()));
  }
}

void app_drawer_refresh_hits(AppDrawerState& s) {
  if (s.categories.empty()) app_drawer_update_categories(s);
  const auto t0 = ShellBenchClock::now();
  if (eh_app_drawer_debug_level() >= 1) {
    trace_line(1, "drawer-overlay",
                               "refresh_hits query_len=" + std::to_string(s.query.size()) + " sel_before=" + std::to_string(s.sel));
  }

  const std::string& q = s.query;
  const bool canFilter = !s.prevQuery.empty() &&
      q.size() > s.prevQuery.size() &&
      q.compare(0, s.prevQuery.size(), s.prevQuery) == 0 &&
      !s.prevCatIndices.empty();
  if (canFilter) {
    eh_app_drawer_menu_query_filtered(q, s.prevCatIndices, &s.startMenuPinnedApps, &s.hits);
  } else {
    eh_app_drawer_menu_query(q, &s.startMenuPinnedApps, &s.hits);
    const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
    std::unordered_map<std::string_view, size_t> pathToIdx;
    pathToIdx.reserve(catalog.size());
    for (size_t i = 0; i < catalog.size(); ++i)
      pathToIdx.emplace(catalog[i].path, i);
    s.prevCatIndices.clear();
    s.prevCatIndices.reserve(s.hits.size());
    for (const auto& hit : s.hits) {
      auto it = pathToIdx.find(hit.path);
      if (it != pathToIdx.end())
        s.prevCatIndices.push_back(it->second);
    }
  }
  s.prevQuery = q;
  {
    const auto& cat = eh::shell::dock::app_drawer::get_cached_entries();
    s.pinHits.clear();
    s.pinHits.reserve(s.pinnedApps.size());
    for (const auto& pinRaw : s.pinnedApps) {
      SpotlightHit ph;
      if (auto ent = dock_app_drawer_entry_for_pin_in_catalog(pinRaw, cat)) {
        ph.path = ent->path;
        ph.name = ent->name;
        ph.genericName = ent->genericName;
        ph.comment = ent->comment;
        ph.exec = ent->exec;
        ph.iconKey = ent->icon.empty() ? std::string("application-x-executable") : ent->icon;
        ph.categories = ent->categories;
      } else {
        ph.iconKey = pinRaw;
      }
      s.pinHits.push_back(std::move(ph));
    }
  }
  if (s.selectedCategory >= 0 &&
      s.selectedCategory < static_cast<int>(s.categories.size())) {
    const int bucket = s.selectedCategory;
    std::vector<SpotlightHit> filtered;
    for (const auto& h : s.hits) {
      if (eh::shell::tahoe::tahoe_hit_in_bucket(h.categories, bucket))
        filtered.push_back(h);
    }
    s.hits = std::move(filtered);
  }
  ++s.appListContentGen;
  app_drawer_clamp_scroll(s);
  if (s.hits.empty()) s.sel = -1;
  else if (s.sel < 0 || s.sel >= static_cast<int>(s.hits.size())) s.sel = 0;
  if (eh_app_drawer_debug_level() >= 1) {
    trace_line(1, "drawer-overlay",
                               "refresh_hits done hits=" + std::to_string(s.hits.size()) + " sel=" + std::to_string(s.sel) +
                                   " scrollPx=" + std::to_string(s.scrollPx));
  }
  {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
    std::cerr << "[search-bench] overlay_refresh_hits query=\"" << s.query
              << "\" hits=" << s.hits.size()
              << " " << us << "us" << std::endl;
  }
}

void app_drawer_ensure_sel_visible(AppDrawerState& s) {
   
  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay", "ensure_sel_visible sel=" + std::to_string(s.sel));
  }
  app_drawer_clamp_scroll(s);
  if (s.sel < 0) return;
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  const double listH = lb - lt;
  if (listH <= 1.0) return;
  const TahoeLayout tl = tahoe_geom(static_cast<double>(s.popupW), static_cast<double>(s.popupH));
  const double rowH = s.viewMode == 0 ? (tl.cellH + eh::shell::tahoe::kTahoeGridGap) : kRowPitch;
  const int selRow = (s.viewMode == 0) ? (s.sel / eh::shell::tahoe::kTahoeCols) : s.sel;
  const double rowTopPx = static_cast<double>(selRow) * rowH;
  const double relTop = rowTopPx - s.scrollPx;
  if (relTop < 0) s.scrollPx = rowTopPx;
  else if (relTop + rowH > listH) s.scrollPx = rowTopPx + rowH - listH;
  app_drawer_clamp_scroll(s);
  s.scrollPxCurrent = s.scrollPx;
  s.scrollAnimStartNs = 0;
  if (eh_app_drawer_debug_level() >= 3) {
    trace_line(3, "drawer-overlay", "ensure_sel_visible → scrollPx=" + std::to_string(s.scrollPx));
  }
}

void app_drawer_scroll_pixels(AppDrawerState& s, double deltaPx) {
   
  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay",
                               "scroll_pixels deltaPx=" + std::to_string(deltaPx) + " scroll_before=" + std::to_string(s.scrollPx));
  }

  const double oldTarget = s.scrollPx;
  s.scrollPx += deltaPx;
  app_drawer_clamp_scroll(s);
  if (std::abs(s.scrollPxCurrent - s.scrollPx) > 0.5) {
    if (s.scrollPx != oldTarget) {
      timespec ts{};
      clock_gettime(CLOCK_MONOTONIC, &ts);
      s.scrollAnimStartNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                            static_cast<uint64_t>(ts.tv_nsec);
      s.scrollPxAnimFrom = s.scrollPxCurrent;
    }
  } else {
    s.scrollPxCurrent = s.scrollPx;
    s.scrollAnimStartNs = 0;
  }
  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay", "scroll_pixels scroll_after=" + std::to_string(s.scrollPx));
  }
}

void app_drawer_scroll_tick(AppDrawerState& s) {
  if (s.scrollAnimStartNs == 0) return;
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const auto nowNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                     static_cast<uint64_t>(ts.tv_nsec);
  const double elapsedMs = static_cast<double>(nowNs - s.scrollAnimStartNs) / 1000000.0;
  constexpr double kTau = 40.0;
  const double factor = 1.0 - std::exp(-elapsedMs / kTau);
  const double diff = s.scrollPx - s.scrollPxAnimFrom;
  s.scrollPxCurrent = s.scrollPxAnimFrom + diff * factor;
  if (std::abs(s.scrollPxCurrent - s.scrollPx) <= 0.5) {
    s.scrollPxCurrent = s.scrollPx;
    s.scrollAnimStartNs = 0;
  }
}

AppDrawerHitZone app_drawer_hit_zone(const AppDrawerState& s, double lx, double ly) {
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const TahoeLayout l = tahoe_geom(W, H);

  Rect sr = search_rect(W, H);
  if (lx >= sr.x && lx < sr.x + sr.w && ly >= sr.y && ly < sr.y + sr.h) return AppDrawerHitZone::SearchField;

  if (ly >= l.pillsY && ly < l.pillsY + l.pillsH && lx >= l.pillsAvailX &&
      lx < l.pillsAvailX + l.pillsAvailW)
    return AppDrawerHitZone::CategoryTab;

  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  if (ly >= lt && ly < lb && lx >= l.gridX && lx < l.gridX + l.gridW) return AppDrawerHitZone::AppListRow;

  for (int i = 0; i < kPowerBtnCount; ++i) {
    Rect b = power_btn_rect(W, i, H);
    if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::PowerButton;
  }
  {
    Rect b = power_btn_rect(W, kNightlightBtnIdx, H);
    if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::NightlightButton;
  }

  return AppDrawerHitZone::None;
}

int app_drawer_pick_row_index(const AppDrawerState& s, double lx, double ly) {
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const TahoeLayout l = tahoe_geom(W, H);
  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  if (ly < lt || ly > lb) return -1;
  if (s.viewMode == 0) {
    return eh::shell::tahoe::tahoe_pick_grid(lx, ly, s.hits.size(), l, s.scrollPxCurrent);
  }
  const double rel = ly - lt + s.scrollPxCurrent;
  const int idx = static_cast<int>(std::floor(rel / kRowPitch));
  if (idx < 0 || idx >= static_cast<int>(s.hits.size())) return -1;
  return idx;
}

int app_drawer_pick_pinned_index(const AppDrawerState& s, double lx, double ly) {
  (void)s; (void)lx; (void)ly;
  return -1;
}

int app_drawer_pick_power_index(const AppDrawerState& s, double lx, double ly) {
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  for (int i = 0; i < kPowerBtnCount; ++i) {
    Rect b = power_btn_rect(W, i, H);
    if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return i;
  }
  return -1;
}

namespace {
constexpr double kRowCtxItemH = 28.0;
constexpr double kRowCtxPad = 8.0;
constexpr double kRowCtxMinW = 232.0;
}

void app_drawer_row_context_menu_layout(double menuAnchorX, double menuAnchorY, double popupW, double popupH,
                                        int itemCount, double* outMenuX, double* outMenuY, double* outMenuW,
                                        double* outMenuH) {
  const int n = std::clamp(itemCount, 1, kAppDrawerRowContextItemMax);
  const double mw = kRowCtxMinW;
  const double mh = kRowCtxPad * 2.0 + static_cast<double>(n) * kRowCtxItemH;
  const double mx = std::clamp(menuAnchorX, eh::shell::tahoe::kTahoeMar,
                               std::max(eh::shell::tahoe::kTahoeMar, popupW - mw - eh::shell::tahoe::kTahoeMar));
  const double my = std::clamp(menuAnchorY, eh::shell::tahoe::kTahoeMar,
                               std::max(eh::shell::tahoe::kTahoeMar, popupH - mh - eh::shell::tahoe::kTahoeMar));
  *outMenuX = mx;
  *outMenuY = my;
  *outMenuW = mw;
  *outMenuH = mh;
}

int app_drawer_row_context_menu_pick(double lx, double ly, double menuX, double menuY, int itemCount) {
  const int n = std::clamp(itemCount, 1, kAppDrawerRowContextItemMax);
  for (int i = 0; i < n; ++i) {
    const double y0 = menuY + kRowCtxPad + static_cast<double>(i) * kRowCtxItemH;
    if (lx >= menuX && lx < menuX + kRowCtxMinW && ly >= y0 && ly < y0 + kRowCtxItemH) return i;
  }
  return -1;
}

void app_drawer_row_context_menu_paint(cairo_t* cr, double menuX, double menuY, const char* const* lines, int itemCount,
                                       int hoverItem, const AppDrawerChromeColors& col) {
  const int n = std::clamp(itemCount, 1, kAppDrawerRowContextItemMax);
  const double mw = kRowCtxMinW;
  const double mh = kRowCtxPad * 2.0 + static_cast<double>(n) * kRowCtxItemH;
  eh::shell::tahoe::tahoe_rr(cr, menuX, menuY, mw, mh, 10.0);
  cairo_set_source_rgba(cr, col.dockFillR, col.dockFillG, col.dockFillB, 0.98);
  cairo_fill(cr);

  eh::shell::tahoe::tahoe_rr(cr, menuX, menuY, mw, mh, 10.0);
  cairo_set_source_rgba(cr, col.outlineR, col.outlineG, col.outlineB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  for (int i = 0; i < n; ++i) {
    const double y0 = menuY + kRowCtxPad + static_cast<double>(i) * kRowCtxItemH;
    if (i == hoverItem) {
      eh::shell::tahoe::tahoe_rr(cr, menuX + 3.0, y0 + 2.0, mw - 6.0, kRowCtxItemH - 4.0, 6.0);
      cairo_set_source_rgba(cr, col.accentR, col.accentG, col.accentB, 0.25);
      cairo_fill(cr);
    }
    cairo_set_source_rgba(cr, 0.92, 0.95, 0.97, 0.96);
    cairo_move_to(cr, menuX + kRowCtxPad + 4.0, y0 + kRowCtxItemH * 0.72);
    cairo_show_text(cr, (lines && lines[i]) ? lines[i] : "");
  }
}

void app_drawer_paint(AppDrawerState& s,
                      cairo_t* cr,
                      eh::icons::IconCache& icons,
                      bool paintBackdrop,
                      bool paintContent,
                      float backdrop_alpha_scale,
                      const AppDrawerChromeColors* chrome) {
  app_drawer_scroll_tick(s);
  AppDrawerChromeColors def{};
  const AppDrawerChromeColors& col = chrome ? *chrome : def;
  const double primR = col.accentR, primG = col.accentG, primB = col.accentB;

  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay",
                               "paint begin W=" + std::to_string(s.popupW) + " H=" + std::to_string(s.popupH) +
                                   " backdrop=" + std::string(paintBackdrop ? "1" : "0") + " content=" +
                                   std::string(paintContent ? "1" : "0") + " hits=" + std::to_string(s.hits.size()) +
                                   " sel=" + std::to_string(s.sel));
  }
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const TahoeLayout l = tahoe_geom(W, H);
  const double bs = std::clamp(static_cast<double>(backdrop_alpha_scale), 0.0, 1.0);

  namespace th = eh::shell::tahoe;

  if (paintBackdrop)
    th::tahoe_paint_card(cr, W, H, col.dockFillR, col.dockFillG, col.dockFillB, bs);

  if (!paintContent)
    return;

  cairo_save(cr);
  th::tahoe_rr(cr, 0, 0, W, H, th::kTahoeRadius);
  cairo_clip(cr);

  // ---- search field (Tahoe header) ----
  {
    Rect sr = search_rect(W, H);
    th::tahoe_rr(cr, sr.x, sr.y, sr.w, sr.h, kFieldR);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, s.query.empty() ? 0.12 : 0.30);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    const double midY = sr.y + sr.h * 0.5;
    eh::shell::draw_material_glyph(cr, sr.x + 30.0, midY, 20.0, "search",
                                   s.query.empty() ? col.outlineR : primR,
                                   s.query.empty() ? col.outlineG : primG,
                                   s.query.empty() ? col.outlineB : primB, 1.0);

    const double textX = sr.x + 52.0;
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 15.0);
    std::string shown;
    if (s.query.empty()) {
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.38);
      cairo_move_to(cr, textX, midY + 5.5);
      cairo_show_text(cr, "Search for an app\u2026");
    } else {
      truncate_to_width(cr, s.query, sr.x + sr.w - textX - 130.0, &shown);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
      cairo_move_to(cr, textX, midY + 5.5);
      cairo_show_text(cr, shown.c_str());
    }
    {
      const uint64_t caretMono = eh::shell::monotonic_ms();
      const bool caretOn = eh::shell::text_caret_blink_on(caretMono, s.searchFieldFocused);
      double caretX = textX;
      if (!s.query.empty()) {
        cairo_text_extents_t qEx{};
        cairo_text_extents(cr, shown.c_str(), &qEx);
        caretX = textX + qEx.x_advance + 1.5;
      }
      if (caretOn) {
        cairo_move_to(cr, caretX, midY - 10.0);
        cairo_line_to(cr, caretX, midY + 10.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 1.0);
      }
    }
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
    cairo_text_extents_t he{};
    cairo_text_extents(cr, "Esc to close", &he);
    cairo_move_to(cr, sr.x + sr.w - 14.0 - he.x_advance, midY + 4.5);
    cairo_show_text(cr, "Esc to close");
  }

  // ---- divider under search ----
  {
    const double sepY = l.searchY + l.searchH + 12.0;
    cairo_move_to(cr, kMar, sepY);
    cairo_line_to(cr, W - kMar, sepY);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }

  // ---- category pills with counts ----
  if (!s.categories.empty() &&
      s.categoryWidths.size() >= s.categories.size() + 1) {
    const int nCats = static_cast<int>(s.categories.size());
    const auto xs = th::tahoe_pill_xs(s.categoryWidths, l.pillsAvailX, l.pillsAvailW,
                                      s.selectedCategory + 1);
    cairo_save(cr);
    cairo_rectangle(cr, l.pillsAvailX, l.pillsY - 2.0, l.pillsAvailW, l.pillsH + 4.0);
    cairo_clip(cr);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, eh::shell::tahoe::kTahoePillFontPx);
    for (int i = -1; i < nCats; ++i) {
      const size_t k = static_cast<size_t>(i + 1);
      const double px = xs[k];
      const double pw = s.categoryWidths[k];
      if (px + pw < l.pillsAvailX || px > l.pillsAvailX + l.pillsAvailW) continue;
      const bool selected = (i == s.selectedCategory);
      const bool hovered = (i == s.hoverCategoryIdx);
      th::tahoe_rr(cr, px, l.pillsY, pw, l.pillsH, l.pillsH * 0.5);
      if (selected) {
        cairo_set_source_rgba(cr, primR, primG, primB, 0.95);
      } else if (hovered) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
      } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
      }
      cairo_fill_preserve(cr);
      if (!selected) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, hovered ? 0.22 : 0.12);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      } else {
        cairo_new_path(cr);
      }
      const char* label = (i < 0) ? "All" : s.categories[static_cast<size_t>(i)].c_str();
      const int count = (k < s.categoryCounts.size()) ? s.categoryCounts[k] : 0;
      cairo_text_extents_t le{}, ce{};
      cairo_text_extents(cr, label, &le);
      char cbuf[16];
      std::snprintf(cbuf, sizeof(cbuf), "%d", count);
      cairo_text_extents(cr, cbuf, &ce);
      const double totalAdv = le.x_advance + 6.0 + ce.x_advance;
      double tx = px + (pw - totalAdv) * 0.5;
      const double baseY = l.pillsY + l.pillsH * 0.5 + 4.5;
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, selected ? 0.98 : 0.82);
      cairo_move_to(cr, tx, baseY);
      cairo_show_text(cr, label);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, selected ? 0.75 : 0.45);
      cairo_move_to(cr, tx + le.x_advance + 6.0, baseY);
      cairo_show_text(cr, cbuf);
    }
    cairo_restore(cr);
  }

  // ---- footer divider + slim power strip ----
  {
    cairo_move_to(cr, kMar, l.footerY - 2.0);
    cairo_line_to(cr, W - kMar, l.footerY - 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    for (int i = 0; i < kPowerBtnCount; ++i) {
      Rect b = power_btn_rect(W, i, H);
      if (s.hoverPowerIdx == i) {
        th::tahoe_rr(cr, b.x, b.y, b.w, b.h, kPowerBtnR);
        cairo_set_source_rgba(cr, primR, primG, primB, 0.20);
        cairo_fill(cr);
      }
      eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0,
                                     kPowerBtns[i].ligature, 1.0, 1.0, 1.0, 0.62);
    }
    {
      const double sepX = power_btn_rect(W, kPowerBtnCount - 1, H).x + kPowerBtnSz + kPadXS * 1.5;
      const double rowY = power_btn_rect(W, 0, H).y;
      cairo_move_to(cr, sepX, rowY + 4.0);
      cairo_line_to(cr, sepX, rowY + kPowerBtnSz - 4.0);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }
    {
      Rect b = power_btn_rect(W, kNightlightBtnIdx, H);
      if (s.hoverPowerIdx == kNightlightBtnIdx) {
        th::tahoe_rr(cr, b.x, b.y, b.w, b.h, kPowerBtnR);
        cairo_set_source_rgba(cr, primR, primG, primB, 0.20);
        cairo_fill(cr);
      }
      const char* glyph = s_nightlight_active ? "dark_mode" : "light_mode";
      eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0, glyph,
                                     1.0, 1.0, 1.0, 0.62);
    }
  }

  // ---- app grid / list ----
  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  const double listH = lb - lt;

  cairo_save(cr);
  cairo_rectangle(cr, l.gridX, lt, l.gridW, std::max(0.0, listH));
  cairo_clip(cr);

  if (!s.hits.empty()) {
    const size_t n = s.hits.size();
    const double totalContentH = (s.viewMode == 0)
        ? th::tahoe_grid_content_h(n, l)
        : static_cast<double>(n) * kRowPitch;
    const int cw = static_cast<int>(l.gridW);
    const int ch = static_cast<int>(std::ceil(totalContentH));

    const bool cacheOk = s.appListCache &&
        s.appListCacheW == cw &&
        s.appListCacheH == ch &&
        s.appListCacheViewMode == s.viewMode &&
        s.appListCacheContentGen == s.appListContentGen;

    if (!cacheOk) {
      if (s.appListCache) {
        cairo_surface_destroy(s.appListCache);
        s.appListCache = nullptr;
      }
      if (cw > 0 && ch > 0) {
        s.appListCache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
        cairo_t* cc = cairo_create(s.appListCache);
        cairo_select_font_face(cc, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

        if (s.viewMode == 0) {
          cairo_set_font_size(cc, th::kTahoeLabelFontPx);
          for (size_t i = 0; i < n; ++i) {
            double gx = 0, gy = 0;
            th::tahoe_cell_xy(static_cast<int>(i), l, 0.0, gx, gy);
            gx -= l.gridX;
            gy -= l.gridY;
            const SpotlightHit& hit = s.hits[i];
            const double iconX = gx + (l.cellW - l.iconSz) * 0.5;
            const double iconY = gy + 4.0;

            bool iconDrawn = false;
            if (const eh::icons::IconEntry* ic = catalog_list_icon(icons, hit)) {
              if (ic->surface) {
                cairo_save(cc);
                cairo_translate(cc, iconX, iconY);
                const double iw = static_cast<double>(ic->width);
                const double ih = static_cast<double>(ic->height);
                const double sc = l.iconSz / std::max(1.0, std::max(iw, ih));
                cairo_scale(cc, sc, sc);
                cairo_set_source_surface(cc, ic->surface, 0, 0);
                cairo_paint(cc);
                cairo_restore(cc);
                iconDrawn = true;
              }
            }
            if (!iconDrawn) {
              th::tahoe_rr(cc, iconX, iconY, l.iconSz, l.iconSz, 14.0);
              cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.10);
              cairo_fill(cc);
              cairo_set_font_size(cc, 24.0);
              cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.8);
              std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
              cairo_text_extents_t ie{};
              cairo_text_extents(cc, init.c_str(), &ie);
              cairo_move_to(cc, iconX + (l.iconSz - ie.x_advance) * 0.5,
                            iconY + l.iconSz * 0.5 + 8.0);
              cairo_show_text(cc, init.c_str());
              cairo_set_font_size(cc, th::kTahoeLabelFontPx);
            }

            std::string line1, line2;
            const int nLines = th::tahoe_two_lines(hit.name, line1, line2);
            cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.88);
            const double labelTop = iconY + l.iconSz + 7.0;
            const double maxLabelW = l.cellW - 8.0;
            auto center_show = [&](const std::string& txt, double baselineY) {
              std::string trunc;
              truncate_to_width(cc, txt, maxLabelW, &trunc);
              cairo_text_extents_t ex{};
              cairo_text_extents(cc, trunc.c_str(), &ex);
              cairo_move_to(cc, gx + (l.cellW - ex.x_advance) * 0.5, baselineY);
              cairo_show_text(cc, trunc.c_str());
            };
            if (nLines == 1) {
              center_show(line1, labelTop + 16.0);
            } else {
              center_show(line1, labelTop + 10.0);
              center_show(line2, labelTop + 23.0);
            }
          }
        } else {
          for (size_t i = 0; i < n; ++i) {
            const double rowY = static_cast<double>(i) * kRowPitch;
            const SpotlightHit& hit = s.hits[i];

            const double iconX = kPad;
            const double iconY = rowY + (kRowPitch - kIconSz) * 0.5;

            bool iconDrawn = false;
            if (const eh::icons::IconEntry* ic = catalog_list_icon(icons, hit)) {
              if (ic->surface) {
                cairo_save(cc);
                cairo_translate(cc, iconX, iconY);
                const double iw = static_cast<double>(ic->width);
                const double ih = static_cast<double>(ic->height);
                const double sc = kIconSz / std::max(1.0, std::max(iw, ih));
                cairo_scale(cc, sc, sc);
                cairo_set_source_surface(cc, ic->surface, 0, 0);
                cairo_paint(cc);
                cairo_restore(cc);
                iconDrawn = true;
              }
            }
            if (!iconDrawn) {
              th::tahoe_rr(cc, iconX, iconY, kIconSz, kIconSz, 12.0);
              cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.10);
              cairo_fill(cc);
              cairo_set_font_size(cc, 17.0);
              cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.8);
              std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
              cairo_move_to(cc, iconX + kIconSz * 0.30, iconY + kIconSz * 0.66);
              cairo_show_text(cc, init.c_str());
            }

            const double textLeft = iconX + kIconSz + kPad;
            const double textMaxW = static_cast<double>(cw) - textLeft - kPadS;

            cairo_select_font_face(cc, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cc, 13.5);
            cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.95);
            std::string nameTrunc;
            truncate_to_width(cc, hit.name, textMaxW, &nameTrunc);
            const double nameY =
                (hit.comment.empty() && hit.genericName.empty()) ? (rowY + kRowPitch * 0.5 + 5.0) : (rowY + 24.0);
            cairo_move_to(cc, textLeft, nameY);
            cairo_show_text(cc, nameTrunc.c_str());

            const std::string& sub = !hit.comment.empty() ? hit.comment : hit.genericName;
            if (!sub.empty()) {
              cairo_select_font_face(cc, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
              cairo_set_font_size(cc, 11.0);
              cairo_set_source_rgba(cc, 1.0, 1.0, 1.0, 0.55);
              std::string subTrunc;
              truncate_to_width(cc, sub, textMaxW, &subTrunc);
              cairo_move_to(cc, textLeft, rowY + 44.0);
              cairo_show_text(cc, subTrunc.c_str());
            }
          }
        }

        cairo_destroy(cc);
      }
      s.appListCacheW = cw;
      s.appListCacheH = ch;
      s.appListCacheViewMode = s.viewMode;
      s.appListCacheContentGen = s.appListContentGen;
    }

    if (s.appListCache) {
      const double dy = lt - s.scrollPxCurrent;
      cairo_set_source_surface(cr, s.appListCache, l.gridX, dy);
      cairo_paint(cr);

      const int litRow = (s.hoverListRow >= 0) ? s.hoverListRow : s.sel;
      if (litRow >= 0 && static_cast<size_t>(litRow) < n) {
        if (s.viewMode == 0) {
          double gx = 0, gy = 0;
          th::tahoe_cell_xy(litRow, l, s.scrollPxCurrent, gx, gy);
          gy += lt - l.gridY;
          const bool sel = (litRow == s.sel);
          th::tahoe_rr(cr, gx, gy, l.cellW, l.cellH, 16.0);
          if (sel) {
            cairo_set_source_rgba(cr, primR, primG, primB, 0.28);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, primR, primG, primB, 0.55);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
          } else {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
            cairo_fill(cr);
          }
        } else {
          const double rowY = lt + static_cast<double>(litRow) * kRowPitch - s.scrollPxCurrent;
          th::tahoe_rr(cr, l.gridX + kPadXS, rowY + 2.0, l.gridW - 2 * kPadXS, kRowPitch - 4.0, kRowR);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.18);
          cairo_fill_preserve(cr);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.35);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }
    }
  } else {
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
    const char* msg = s.query.empty() ? "No applications" : "No matching applications";
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, msg, &ex);
    cairo_move_to(cr, l.gridX + (l.gridW - ex.x_advance) * 0.5, lt + 64.0);
    cairo_show_text(cr, msg);
  }

  cairo_restore(cr);
  cairo_restore(cr);

  if (false && s.pinCtxOpen && s.pinCtxAnchorIdx >= 0 &&
      s.pinCtxAnchorIdx < static_cast<int>(std::min<size_t>(15, s.pinnedApps.size()))) {
    const char* lp0 = s.pinCtxPinnedDock ? "Unpin from dock" : "Pin to dock";
    const char* lp1 = "Remove from drawer";
    const char* linesP[2] = {lp0, lp1};
    double mxp = 0, myp = 0, mwp = 0, mhp = 0;
    app_drawer_row_context_menu_layout(s.pinCtxMenuX, s.pinCtxMenuY, W, H, 2, &mxp, &myp, &mwp, &mhp);
    app_drawer_row_context_menu_paint(cr, mxp, myp, linesP, 2, s.pinCtxHoverItem, col);
  }
  if (s.rowCtxOpen && s.rowCtxAnchorRow >= 0 && s.rowCtxAnchorRow < static_cast<int>(s.hits.size())) {
    const char* l0 = s.rowCtxPinnedDock ? "Unpin from dock" : "Pin to dock";
    const char* l1 = s.rowCtxPinnedStart ? "Unpin from Start" : "Pin to Start";
    const char* l2 = s.rowCtxPinnedDrawer ? "Unpin from drawer" : "Pin to drawer";
    const char* linesR[3] = {l0, l1, l2};
    const int n = std::clamp(s.rowCtxItemCount, 2, kAppDrawerRowContextItemMax);
    double mx = 0, my = 0, mw = 0, mh = 0;
    app_drawer_row_context_menu_layout(s.rowCtxMenuX, s.rowCtxMenuY, W, H, n, &mx, &my, &mw, &mh);
    app_drawer_row_context_menu_paint(cr, mx, my, linesR, n, s.rowCtxHoverItem, col);
  }

  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay", "paint end");
  }
}

}
