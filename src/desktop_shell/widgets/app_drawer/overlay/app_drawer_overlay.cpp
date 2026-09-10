#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"

#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <time.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

static bool s_smenu_mode = false;

constexpr int kPopupW = 858;
constexpr int kPopupH = 680;

constexpr double kMar = 16.0;
constexpr double kPad = 12.0;
constexpr double kPadS = 8.0;
constexpr double kPadXS = 4.0;

constexpr double kLeftFrac = 0.40;

constexpr double kSearchH = 48.0;

constexpr double kRowPitch = 64.0;
constexpr double kGridGap = 10.0;
constexpr int kGridCols = 5;
constexpr double kIconSz = 46.0;
constexpr double kGridIconSz = 48.0;
constexpr double kGridLabelGap = 4.0;
constexpr double kGridLabelFontPx = 11.0;

constexpr double kPinCols = 3.0;
constexpr double kPinCellH = 96.0;
constexpr double kPinIconSz = 58.0;

constexpr double kCatTabH   = 34.0;
constexpr double kCatTabR   =  8.0;
constexpr double kCatTabGap = 6.0;
constexpr double kCatTabFont = 14.0;

constexpr double kPowerBtnSz = 36.0;
constexpr double kPowerBtnR = 8.0;

constexpr double kPopupR = 14.0;
constexpr double kFieldR = 10.0;
constexpr double kRowR = 12.0;
constexpr double kCellR = 8.0;
constexpr double kPinIconR = 8.0;

struct Rect {
  double x, y, w, h;
};

Rect right_col(double W, double H) {
  if (s_smenu_mode) return {0, 0, W, H};
  const double lw = W * kLeftFrac;
  return {lw, 0, W - lw, H};
}

Rect search_rect(double W, double H) {
  const Rect rc = right_col(W, H);
  return {rc.x + kMar, kMar, rc.w - 2 * kMar, kSearchH};
}

double search_sep_y(double  ) { return kMar + kSearchH + kPad; }
double cat_tab_top(double W) { return search_sep_y(W) + kPad; }
double list_top(double W, double H) {
  (void)H;
  if (s_smenu_mode) return search_sep_y(W) + kPad + kCatTabH + kPadS;
  return search_sep_y(W) + 1.0 + kPad;
}
double list_x(double W, double H) { return right_col(W, H).x + kPadS; }
double list_w(double W, double H) { return right_col(W, H).w - 2 * kPadS; }

double pinned_label_y() { return kMar + 16.0; }
double pinned_sep_y() { return kMar + 24.0; }
double pinned_grid_top() { return pinned_sep_y() + 1.0 + kPadXS; }

Rect pinned_cell_rect(double leftW, int col, int row) {
  const double gridW = leftW - 2 * kMar;
  const double cellW = (gridW - (kPinCols - 1) * kPadXS) / kPinCols;
  const double x = kMar + col * (cellW + kPadXS);
  const double y = pinned_grid_top() + row * (kPinCellH + kPadXS);
  return {x, y, cellW, kPinCellH};
}

double power_row_y(double H) { return H - kMar - kPowerBtnSz; }
double power_sep_y(double H) { return power_row_y(H) - kPadS - 1.0; }

Rect power_btn_rect(double  , int idx, double H) {
  const double rowY = power_row_y(H);
  const double x = kMar + idx * (kPowerBtnSz + kPadXS);
  return {x, rowY, kPowerBtnSz, kPowerBtnSz};
}

// Start menu footer layout.
double smenu_footer_h() {
  return kPowerBtnSz + kPadS + kPadS;
}
double smenu_power_row_y(double H) {
  return H - kMar - kPowerBtnSz;
}
Rect smenu_power_btn_rect(int idx, int totalBtns, double W, double H) {
  const double rowY = smenu_power_row_y(H);
  const double totalBtnW = totalBtns * kPowerBtnSz + (totalBtns - 1) * kPadXS;
  const double startX = (W - totalBtnW) / 2.0;
  const double x = startX + idx * (kPowerBtnSz + kPadXS);
  return {x, rowY, kPowerBtnSz, kPowerBtnSz};
}
double list_bottom(double  , double H) {
  if (s_smenu_mode) return H - kMar - smenu_footer_h();
  return H - kMar;
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

static std::string pinned_tile_fallback_label(const std::string& rawId) {
   
  std::string lbl = rawId;
  if (lbl.size() > 8 && lbl.substr(lbl.size() - 8) == ".desktop") lbl = lbl.substr(0, lbl.size() - 8);
  if (const auto sl = lbl.rfind('/'); sl != std::string::npos) lbl = lbl.substr(sl + 1);
  return lbl;
}

void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
  cairo_new_path(cr);
  const double r = std::min({rad, rw * 0.5, rh * 0.5});
  const double x0 = rx, y0 = ry;
  const double x1 = rx + rw, y1 = ry + rh;
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
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
constexpr int kPowerBtnCount = 4;
constexpr int kNightlightBtnIdx = 4;

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
     
    s.categories.clear();
    s.categoryWidths.clear();
    const auto& entries = get_cached_entries();
    for (const auto& e : entries) {
        size_t start = 0;
        while (start < e.categories.size()) {
            size_t end = e.categories.find(';', start);
            if (end == std::string::npos) end = e.categories.size();
            const std::string cat = e.categories.substr(start, end - start);
            if (!cat.empty() && is_standard_category(cat) &&
                std::find(s.categories.begin(), s.categories.end(), cat) == s.categories.end())
                s.categories.push_back(cat);
            start = end + 1;
        }
    }
    std::sort(s.categories.begin(), s.categories.end());
    if (s.selectedCategory >= static_cast<int>(s.categories.size()))
        s.selectedCategory = -1;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0);
    constexpr double kPillPad = 2.0;
    cairo_text_extents_t te;
    {
        cairo_text_extents(cr, "All", &te);
        s.categoryWidths.push_back(te.x_advance + kPillPad * 2.0);
    }
    for (const auto& cat : s.categories) {
        cairo_text_extents(cr, cat.c_str(), &te);
        s.categoryWidths.push_back(te.x_advance + kPillPad * 2.0);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

int app_drawer_pick_category_tab(const AppDrawerState& s, double lx, double ly) {
     
    constexpr double kPillH = 34.0;
    constexpr double kPillGap = 6.0;
    const double W = static_cast<double>(s.popupW);
    const double ct = cat_tab_top(W);
    if (ly < ct || ly > ct + kPillH) return -1;
    const int nCats = static_cast<int>(s.categories.size());
    if (s.categoryWidths.size() < static_cast<size_t>(nCats + 1)) return -1;
    double totalW = 0.0;
    for (int i = -1; i < nCats; ++i)
        totalW += s.categoryWidths[static_cast<size_t>(i + 1)] + kPillGap;
    totalW -= kPillGap;
    double x = (W - totalW) * 0.5;
    for (int i = -1; i < nCats; ++i) {
        const double tw = s.categoryWidths[static_cast<size_t>(i + 1)];
        if (lx >= x && lx < x + tw + kPillGap) return i;
        x += tw + kPillGap;
    }
    return -1;
}

void app_drawer_clamp_scroll(AppDrawerState& s) {
   
  s_smenu_mode = s.smenuMode;
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
    const int rows = (static_cast<int>(s.hits.size()) + kGridCols - 1) / kGridCols;
    const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
    contentH = rows * rowH;
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

static bool cat_has_category(const std::string& categories, const std::string& cat) {
     
    if (cat.empty()) return true;
    size_t start = 0;
    while (start < categories.size()) {
        size_t end = categories.find(';', start);
        if (end == std::string::npos) end = categories.size();
        if (categories.substr(start, end - start) == cat) return true;
        start = end + 1;
    }
    return false;
}

void app_drawer_refresh_hits(AppDrawerState& s) {
   
  s_smenu_mode = s.smenuMode;
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
  if (s.smenuMode && s.selectedCategory >= 0 &&
      s.selectedCategory < static_cast<int>(s.categories.size())) {
    const std::string& cat = s.categories[static_cast<size_t>(s.selectedCategory)];
    std::vector<SpotlightHit> filtered;
    for (const auto& h : s.hits) {
      if (cat_has_category(h.categories, cat))
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
   
  s_smenu_mode = s.smenuMode;
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
  const double rowH = s.viewMode == 0 ? (kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap) : kRowPitch;
  const int selRow = (s.viewMode == 0) ? (s.sel / kGridCols) : s.sel;
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
   
  s_smenu_mode = s.smenuMode;
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
   
  s_smenu_mode = s.smenuMode;
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);

  if (!s_smenu_mode) {
    const double lw = W * kLeftFrac;

    if (lx < lw) {
      const int maxPins = static_cast<int>(std::min<size_t>(15, s.pinnedApps.size()));
      const int rows = (maxPins + 2) / 3;
      for (int row = 0; row < rows; ++row)
        for (int col = 0; col < 3; ++col) {
          int idx = row * 3 + col;
          if (idx >= maxPins) break;
          Rect c = pinned_cell_rect(lw, col, row);
          if (lx >= c.x && lx < c.x + c.w && ly >= c.y && ly < c.y + c.h) return AppDrawerHitZone::PinnedApp;
        }
      for (int i = 0; i < kPowerBtnCount; ++i) {
        Rect b = power_btn_rect(lw, i, H);
        if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::PowerButton;
      }
      {
        Rect b = power_btn_rect(lw, kNightlightBtnIdx, H);
        if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::NightlightButton;
      }
      return AppDrawerHitZone::None;
    }
  }

  Rect sr = search_rect(W, H);
  if (lx >= sr.x && lx < sr.x + sr.w && ly >= sr.y && ly < sr.y + sr.h) return AppDrawerHitZone::SearchField;

  if (s_smenu_mode) {
    const double ct = cat_tab_top(W);
    if (ly >= ct && ly < ct + kCatTabH) return AppDrawerHitZone::CategoryTab;
  }

  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  if (ly >= lt && ly < lb) return AppDrawerHitZone::AppListRow;

  // Start menu footer power buttons
  if (s_smenu_mode) {
    const int totalBtns = kPowerBtnCount + 1;
    for (int i = 0; i < kPowerBtnCount; ++i) {
      Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
      if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::PowerButton;
    }
    {
      Rect b = smenu_power_btn_rect(kNightlightBtnIdx, totalBtns, W, H);
      if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return AppDrawerHitZone::NightlightButton;
    }
  }

  return AppDrawerHitZone::None;
}

int app_drawer_pick_row_index(const AppDrawerState& s, double lx, double ly) {
   
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const double lt = list_top(W, H);
  if (ly < lt || ly > H - kMar) return -1;
  const double rel = ly - lt + s.scrollPxCurrent;
  if (s.viewMode == 0) {
    const double lx_ = list_x(W, H);
    const double lw_ = list_w(W, H);
    const double cellW = (lw_ - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
    const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
    const int row = static_cast<int>(std::floor(rel / rowH));
    const double colX = lx - lx_;
    const int col = static_cast<int>(std::floor((colX - kPadS) / (cellW + kGridGap)));
    const int idx = row * kGridCols + std::clamp(col, 0, kGridCols - 1);
    if (idx < 0 || idx >= static_cast<int>(s.hits.size())) return -1;
    return idx;
  }
  const int idx = static_cast<int>(std::floor(rel / kRowPitch));
  if (idx < 0 || idx >= static_cast<int>(s.hits.size())) return -1;
  return idx;
}

int app_drawer_pick_pinned_index(const AppDrawerState& s, double lx, double ly) {
   
  if (s.smenuMode) return -1;
  const double W = static_cast<double>(s.popupW);
  const double lw = W * kLeftFrac;
  const int maxPins = static_cast<int>(std::min<size_t>(15, s.pinnedApps.size()));
  const int rows = (maxPins + 2) / 3;
  for (int row = 0; row < rows; ++row)
    for (int col = 0; col < 3; ++col) {
      int idx = row * 3 + col;
      if (idx >= maxPins) break;
      Rect c = pinned_cell_rect(lw, col, row);
      if (lx >= c.x && lx < c.x + c.w && ly >= c.y && ly < c.y + c.h) return idx;
    }
  return -1;
}

int app_drawer_pick_power_index(const AppDrawerState& s, double lx, double ly) {
   
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  if (s.smenuMode) {
    const int totalBtns = kPowerBtnCount + 1;
    for (int i = 0; i < kPowerBtnCount; ++i) {
      Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
      if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return i;
    }
    return -1;
  }
  const double lw = W * kLeftFrac;
  for (int i = 0; i < kPowerBtnCount; ++i) {
    Rect b = power_btn_rect(lw, i, H);
    if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h) return i;
  }
  return -1;
}

static constexpr double kRowCtxItemH = 28.0;
static constexpr double kRowCtxPad = 8.0;
static constexpr double kRowCtxMinW = 232.0;

void app_drawer_row_context_menu_layout(double menuAnchorX, double menuAnchorY, double popupW, double popupH,
                                        int itemCount, double* outMenuX, double* outMenuY, double* outMenuW,
                                        double* outMenuH) {
   
  const int n = std::clamp(itemCount, 1, kAppDrawerRowContextItemMax);
  const double mw = kRowCtxMinW;
  const double mh = kRowCtxPad * 2.0 + static_cast<double>(n) * kRowCtxItemH;
  const double mx = std::clamp(menuAnchorX, kMar, std::max(kMar, popupW - mw - kMar));
  const double my = std::clamp(menuAnchorY, kMar, std::max(kMar, popupH - mh - kMar));
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
  {
    m3::Box box;
    box.setColor(static_cast<float>(col.dockFillR), static_cast<float>(col.dockFillG),
                 static_cast<float>(col.dockFillB), 0.98f);
    box.setRadius(10.0f);
    box.setGeometry(menuX, menuY, mw, mh);
    box.setGlassy(true);
    box.paint(cr);
  }

  rr(cr, menuX, menuY, mw, mh, 10.0);
  cairo_set_source_rgba(cr, col.outlineR, col.outlineG, col.outlineB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  for (int i = 0; i < n; ++i) {
    const double y0 = menuY + kRowCtxPad + static_cast<double>(i) * kRowCtxItemH;
    if (i == hoverItem) {
      m3::Box box;
      box.setColor(static_cast<float>(col.accentR), static_cast<float>(col.accentG),
                   static_cast<float>(col.accentB), 0.16f);
      box.setRadius(6.0f);
      box.setGeometry(menuX + 3.0, y0 + 2.0, mw - 6.0, kRowCtxItemH - 4.0);
      box.setGlassy(true);
      box.paint(cr);
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
  const double surfR = col.dockFillR, surfG = col.dockFillG, surfB = col.dockFillB;
  const double surfVR = col.drawerDimR, surfVG = col.drawerDimG, surfVB = col.drawerDimB;
  const double primR = col.accentR, primG = col.accentG, primB = col.accentB;
  const double outR = col.outlineR, outG = col.outlineG, outB = col.outlineB;

  s_smenu_mode = s.smenuMode;

  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "drawer-overlay",
                               "paint begin W=" + std::to_string(s.popupW) + " H=" + std::to_string(s.popupH) +
                                   " backdrop=" + std::string(paintBackdrop ? "1" : "0") + " content=" +
                                   std::string(paintContent ? "1" : "0") + " hits=" + std::to_string(s.hits.size()) +
                                   " sel=" + std::to_string(s.sel) + " smenu=" + std::string(s_smenu_mode ? "1" : "0"));
  }
  const double W = static_cast<double>(s.popupW);
  const double H = static_cast<double>(s.popupH);
  const double lw = W * kLeftFrac;
  const double bs = std::clamp(static_cast<double>(backdrop_alpha_scale), 0.0, 1.0);

  if (paintBackdrop) {
    {
      m3::Box box;
      box.setColor(static_cast<float>(surfR * 0.35), static_cast<float>(surfG * 0.35),
                   static_cast<float>(surfB * 0.35), static_cast<float>(0.78 * bs));
      box.setRadius(static_cast<float>(kPopupR));
      box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
      box.setGlassy(true);
      box.paint(cr);
    }
    rr(cr, 0.5, 0.5, W - 1.0, H - 1.0, kPopupR);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    if (!s_smenu_mode) {
      cairo_save(cr);
      rr(cr, 0, 0, W, H, kPopupR);
      cairo_clip(cr);
      cairo_rectangle(cr, 0, 0, lw, H);
      cairo_set_source_rgba(cr, surfVR, surfVG, surfVB, 0.40 * bs);
      cairo_fill(cr);
      cairo_restore(cr);

      cairo_move_to(cr, lw, kMar);
      cairo_line_to(cr, lw, H - kMar);
      cairo_set_source_rgba(cr, outR, outG, outB, 0.12 * bs);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }
  }

  if (!paintContent) {
    return;
  }

  cairo_save(cr);
  rr(cr, 0, 0, W, H, kPopupR);
  cairo_clip(cr);

  if (!s_smenu_mode) {

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
  cairo_move_to(cr, kMar, pinned_label_y());
  cairo_show_text(cr, "Pinned");

  const double sepY1 = pinned_sep_y();
  cairo_move_to(cr, kMar, sepY1);
  cairo_line_to(cr, lw - kMar, sepY1);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const int maxPins = static_cast<int>(std::min<size_t>(15, s.pinnedApps.size()));
  if (maxPins > 0) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    for (int i = 0; i < maxPins; ++i) {
      const int col = i % 3;
      const int row = i / 3;
      Rect cell = pinned_cell_rect(lw, col, row);
      const bool pinLit = (i == s.hoverDrawerPinIdx);
      {
        m3::Box b;
        if (pinLit) {
          b.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), 0.18f);
        } else {
          b.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), 0.06f);
        }
        b.setRadius(static_cast<float>(kCellR));
        b.setGeometry(static_cast<float>(cell.x), static_cast<float>(cell.y),
                      static_cast<float>(cell.w), static_cast<float>(cell.h));
        b.setGlassy(true);
        b.paint(cr);
        if (!pinLit) {
          rr(cr, cell.x, cell.y, cell.w, cell.h, kCellR);
          cairo_set_source_rgba(cr, outR, outG, outB, 0.10);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }

      const std::string& rawId = s.pinnedApps[static_cast<size_t>(i)];
      const SpotlightHit& pinHit = s.pinHits[static_cast<size_t>(i)];
      const bool hasPin = !pinHit.path.empty();
      const std::string displayName = hasPin && !pinHit.name.empty() ? pinHit.name : pinned_tile_fallback_label(rawId);
      const double ix = cell.x + (cell.w - kPinIconSz) * 0.5;
      const double iy = cell.y + 8.0;

      bool iconDrawn = false;
      const eh::icons::IconEntry* ic = nullptr;
      if (hasPin) {
        ic = catalog_list_icon(icons, pinHit);
      } else {
        ic = icons.app_icon(rawId);
      }
      if (ic && ic->surface) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        const double iw = static_cast<double>(ic->width);
        const double ih = static_cast<double>(ic->height);
        const double sc = kPinIconSz / std::max(1.0, std::max(iw, ih));
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, ic->surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        iconDrawn = true;
      }
      if (!iconDrawn) {
        rr(cr, ix, iy, kPinIconSz, kPinIconSz, kPinIconR);
        cairo_set_source_rgba(cr, 0.12, 0.16, 0.18, 1.0);
        cairo_fill(cr);
        cairo_set_font_size(cr, 18.0);
        cairo_set_source_rgba(cr, primR, primG, primB, 0.95);
        std::string init = displayName.empty() ? "?" : displayName.substr(0, 1);
        cairo_move_to(cr, ix + kPinIconSz * 0.33, iy + kPinIconSz * 0.66);
        cairo_show_text(cr, init.c_str());
      }

      cairo_set_font_size(cr, 11.0);
      cairo_set_source_rgba(cr, 0.88, 0.92, 0.95, 0.90);
      const double maxLabelW = cell.w - 4.0;
      std::string lblTrunc;
      truncate_to_width(cr, displayName, maxLabelW, &lblTrunc);
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, lblTrunc.c_str(), &ex);
      const double lx_off = cell.x + (cell.w - ex.x_advance) * 0.5;
      const double ly_off = cell.y + kPinCellH - 6.0;
      cairo_move_to(cr, lx_off, ly_off);
      cairo_show_text(cr, lblTrunc.c_str());
    }
  } else {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.55);
    cairo_move_to(cr, kMar + 4.0, pinned_grid_top() + 28.0);
    cairo_show_text(cr, "No pinned apps");
  }

  const double sepY2 = power_sep_y(H);
  cairo_move_to(cr, kMar, sepY2);
  cairo_line_to(cr, lw - kMar, sepY2);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  for (int i = 0; i < kPowerBtnCount; ++i) {
    Rect b = power_btn_rect(lw, i, H);
    if (s.hoverPowerIdx == i) {
      m3::Box btn;
      btn.setColor(static_cast<float>(primR), static_cast<float>(primG),
                   static_cast<float>(primB), 0.18f);
      btn.setRadius(static_cast<float>(kPowerBtnR));
      btn.setGeometry(static_cast<float>(b.x), static_cast<float>(b.y),
                      static_cast<float>(b.w), static_cast<float>(b.h));
      btn.setGlassy(true);
      btn.paint(cr);
    }
    eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0, kPowerBtns[i].ligature, 0.88, 0.92, 0.95, 1.0);
  }

  // Separator before nightlight button
  {
    const double sepX = power_btn_rect(lw, kPowerBtnCount - 1, H).x + kPowerBtnSz + kPadXS * 1.5;
    const double rowY = power_row_y(H);
    cairo_move_to(cr, sepX, rowY + 2.0);
    cairo_line_to(cr, sepX, rowY + kPowerBtnSz - 2.0);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }

  // Nightlight button
  {
    Rect b = power_btn_rect(lw, kNightlightBtnIdx, H);
    if (s.hoverPowerIdx == kNightlightBtnIdx) {
      m3::Box btn;
      btn.setColor(static_cast<float>(primR), static_cast<float>(primG),
                   static_cast<float>(primB), 0.18f);
      btn.setRadius(static_cast<float>(kPowerBtnR));
      btn.setGeometry(static_cast<float>(b.x), static_cast<float>(b.y),
                      static_cast<float>(b.w), static_cast<float>(b.h));
      btn.setGlassy(true);
      btn.paint(cr);
    }
    const char* glyph = s_nightlight_active ? "dark_mode" : "light_mode";
    const double glyphR = s_nightlight_active ? 0.60 : 0.88;
    const double glyphG = s_nightlight_active ? 0.70 : 0.92;
    const double glyphB = s_nightlight_active ? 0.95 : 0.95;
    eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0, glyph, glyphR, glyphG, glyphB, 1.0);
  }

  }  // !s_smenu_mode

  if (s_smenu_mode) {
    const int totalBtns = kPowerBtnCount + 1;
    const double sepY2 = smenu_power_row_y(H) - kPadS - 1.0;
    cairo_move_to(cr, kMar, sepY2);
    cairo_line_to(cr, W - kMar, sepY2);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    for (int i = 0; i < kPowerBtnCount; ++i) {
      Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
      if (s.hoverPowerIdx == i) {
        m3::Box btn;
        btn.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), 0.18f);
        btn.setRadius(static_cast<float>(kPowerBtnR));
        btn.setGeometry(static_cast<float>(b.x), static_cast<float>(b.y),
                        static_cast<float>(b.w), static_cast<float>(b.h));
        btn.setGlassy(true);
        btn.paint(cr);
      }
      eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0, kPowerBtns[i].ligature, 0.88, 0.92, 0.95, 1.0);
    }

    // Separator before nightlight button
    {
      const double sepX = smenu_power_btn_rect(kPowerBtnCount - 1, totalBtns, W, H).x + kPowerBtnSz + kPadXS * 1.5;
      const double rowY = smenu_power_row_y(H);
      cairo_move_to(cr, sepX, rowY + 2.0);
      cairo_line_to(cr, sepX, rowY + kPowerBtnSz - 2.0);
      cairo_set_source_rgba(cr, outR, outG, outB, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Nightlight button
    {
      Rect b = smenu_power_btn_rect(kNightlightBtnIdx, totalBtns, W, H);
      if (s.hoverPowerIdx == kNightlightBtnIdx) {
        m3::Box btn;
        btn.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), 0.18f);
        btn.setRadius(static_cast<float>(kPowerBtnR));
        btn.setGeometry(static_cast<float>(b.x), static_cast<float>(b.y),
                        static_cast<float>(b.w), static_cast<float>(b.h));
        btn.setGlassy(true);
        btn.paint(cr);
      }
      const char* glyph = s_nightlight_active ? "dark_mode" : "light_mode";
      const double glyphR = s_nightlight_active ? 0.60 : 0.88;
      const double glyphG = s_nightlight_active ? 0.70 : 0.92;
      const double glyphB = s_nightlight_active ? 0.95 : 0.95;
      eh::shell::draw_material_glyph(cr, b.x + b.w * 0.5, b.y + b.h * 0.5, 22.0, glyph, glyphR, glyphG, glyphB, 1.0);
    }
  }

  Rect sr = search_rect(W, H);
  {
    m3::Box sb;
    sb.setColor(static_cast<float>(surfVR), static_cast<float>(surfVG),
                static_cast<float>(surfVB), 0.55f);
    sb.setRadius(static_cast<float>(kFieldR));
    sb.setGeometry(static_cast<float>(sr.x), static_cast<float>(sr.y),
                   static_cast<float>(sr.w), static_cast<float>(sr.h));
    sb.setGlassy(true);
    sb.paint(cr);
  }
  cairo_set_source_rgba(cr, s.query.empty() ? outR : primR, s.query.empty() ? outG : primG, s.query.empty() ? outB : primB,
                        s.query.empty() ? 0.28 : 0.65);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);

  const double siconX = sr.x + kPad;
  const double siconY = sr.y + sr.h * 0.5 + 5.5;
  if (s.query.empty())
    eh::shell::draw_material_glyph(cr, siconX + 8.0, sr.y + sr.h * 0.5, 20.0, "search", outR, outG, outB, 1.0);
  else
    eh::shell::draw_material_glyph(cr, siconX + 8.0, sr.y + sr.h * 0.5, 20.0, "search", primR, primG, primB, 1.0);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 15.0);
  const double textX = siconX + 22.0;
  cairo_save(cr);
  cairo_rectangle(cr, sr.x + 2, sr.y + 2, sr.w - 4, sr.h - 4);
  cairo_clip(cr);
  cairo_set_font_size(cr, 14.5);
  std::string shown;
  if (s.query.empty()) {
    cairo_set_source_rgba(cr, outR, outG, outB, 0.65);
    cairo_move_to(cr, textX, siconY);
    cairo_show_text(cr, "Type here to search");
  } else {
    truncate_to_width(cr, s.query, sr.x + sr.w - textX - kMar, &shown);
    cairo_set_source_rgba(cr, 0.92, 0.95, 0.96, 0.97);
    cairo_move_to(cr, textX, siconY);
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
      cairo_move_to(cr, caretX, siconY - 11.0);
      cairo_line_to(cr, caretX, siconY + 4.0);
      cairo_set_source_rgba(cr, 0.92, 0.95, 0.96, 0.95);
      cairo_set_line_width(cr, 1.5);
      cairo_stroke(cr);
      cairo_set_line_width(cr, 1.0);
    }
  }
  cairo_restore(cr);

  const double ssy = search_sep_y(W);
  const double rcx = right_col(W, H).x;
  cairo_move_to(cr, rcx + kMar, ssy);
  cairo_line_to(cr, rcx + right_col(W, H).w - kMar, ssy);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  if (s_smenu_mode && !s.categories.empty()) {
    constexpr double kPillH = 34.0;
    constexpr double kPillR = 8.0;
    constexpr double kPillPad = 2.0;
    constexpr double kPillFont = 14.0;
    constexpr double kPillGap = 6.0;
    const double ct = cat_tab_top(W);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kPillFont);
    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    const double baseY = ct + kPillH * 0.5 + (fe.ascent - fe.descent) * 0.5;
    double totalW = 0.0;
    for (int i = -1; i < static_cast<int>(s.categories.size()); ++i)
      totalW += s.categoryWidths[static_cast<size_t>(i + 1)] + kPillGap;
    totalW -= kPillGap;
    double cx = (W - totalW) * 0.5;
    for (int i = -1; i < static_cast<int>(s.categories.size()); ++i) {
      const bool selected = (i == s.selectedCategory);
      const bool hovered = (i == s.hoverCategoryIdx);
      const char* label = (i < 0) ? "All" : s.categories[static_cast<size_t>(i)].c_str();
      const double tw = s.categoryWidths[static_cast<size_t>(i + 1)];
      {
        m3::Box pill;
        if (selected) {
          pill.setColor(static_cast<float>(primR), static_cast<float>(primG),
                        static_cast<float>(primB), 0.25f);
        } else if (hovered) {
          pill.setColor(static_cast<float>(primR), static_cast<float>(primG),
                        static_cast<float>(primB), 0.10f);
        } else {
          pill.setColor(static_cast<float>(surfVR), static_cast<float>(surfVG),
                        static_cast<float>(surfVB), 0.40f);
        }
        pill.setRadius(static_cast<float>(kPillR));
        pill.setGeometry(static_cast<float>(cx), static_cast<float>(ct),
                         static_cast<float>(tw), static_cast<float>(kPillH));
        pill.setGlassy(true);
        pill.paint(cr);
        // keep subtle border for non-selected
        if (!selected) {
          rr(cr, cx, ct, tw, kPillH, kPillR);
          cairo_set_source_rgba(cr, outR, outG, outB, hovered ? 0.30 : 0.18);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        } else {
          rr(cr, cx, ct, tw, kPillH, kPillR);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.50);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }
      cairo_set_source_rgba(cr, primR, primG, primB, selected ? 0.95 : 0.75);
      cairo_move_to(cr, cx + kPillPad, baseY);
      cairo_show_text(cr, label);
      cx += tw + kPillGap;
    }
  }

  const double lt = list_top(W, H);
  const double lb = list_bottom(W, H);
  const double listH = lb - lt;
  const double lx_ = list_x(W, H);
  const double lw_ = list_w(W, H);

  cairo_save(cr);
  cairo_rectangle(cr, lx_, lt, lw_, std::max(0.0, listH));
  cairo_clip(cr);

  if (!s.hits.empty()) {
    const size_t n = s.hits.size();
    const double totalContentH = (s.viewMode == 0)
      ? (static_cast<double>((static_cast<int>(n) + kGridCols - 1) / kGridCols) *
         (kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap))
      : static_cast<double>(n) * kRowPitch;
    const int cw = static_cast<int>(lw_);
    const int ch = static_cast<int>(totalContentH);

    bool cacheOk = s.appListCache &&
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
        cairo_select_font_face(cc, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

        if (s.viewMode == 0) {
          const double cellW = (static_cast<double>(cw) - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
          const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
          for (size_t i = 0; i < n; ++i) {
            const int col = static_cast<int>(i) % kGridCols;
            const int row = static_cast<int>(i) / kGridCols;
            const double gx = kPadS + col * (cellW + kGridGap);
            const double gy = static_cast<double>(row) * rowH;
            const SpotlightHit& hit = s.hits[i];
            const double iconX = gx + (cellW - kGridIconSz) * 0.5;
            const double iconY = gy + 4.0;

            bool iconDrawn = false;
            if (const eh::icons::IconEntry* ic = catalog_list_icon(icons, hit)) {
              if (ic->surface) {
                cairo_save(cc);
                cairo_translate(cc, iconX, iconY);
                const double iw = static_cast<double>(ic->width);
                const double ih = static_cast<double>(ic->height);
                const double sc = kGridIconSz / std::max(1.0, std::max(iw, ih));
                cairo_scale(cc, sc, sc);
                cairo_set_source_surface(cc, ic->surface, 0, 0);
                cairo_paint(cc);
                cairo_restore(cc);
                iconDrawn = true;
              }
            }
            if (!iconDrawn) {
              rr(cc, iconX, iconY, kGridIconSz, kGridIconSz, kCellR);
              cairo_set_source_rgba(cc, 0.12, 0.16, 0.18, 1.0);
              cairo_fill(cc);
              cairo_set_font_size(cc, 22.0);
              cairo_set_source_rgba(cc, primR, primG, primB, 0.95);
              std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
              cairo_move_to(cc, iconX + kGridIconSz * 0.28, iconY + kGridIconSz * 0.68);
              cairo_show_text(cc, init.c_str());
            }

            cairo_select_font_face(cc, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cc, kGridLabelFontPx);
            cairo_set_source_rgba(cc, primR, primG, primB, 0.85);
            const double labelMax = cellW - 4.0;
            std::string lblTrunc;
            cairo_text_extents_t ex{};
            truncate_to_width(cc, hit.name, labelMax, &lblTrunc);
            cairo_text_extents(cc, lblTrunc.c_str(), &ex);
            const double lxCache = gx + (cellW - ex.x_advance) * 0.5;
            cairo_move_to(cc, lxCache, iconY + kGridIconSz + kGridLabelGap + kGridLabelFontPx);
            cairo_show_text(cc, lblTrunc.c_str());
          }
        } else {
          for (size_t i = 0; i < n; ++i) {
            const double rowY = static_cast<double>(i) * kRowPitch;
            const SpotlightHit& hit = s.hits[i];

            const double rowInnerH = kRowPitch - 4.0;
            rr(cc, kPadXS, rowY + 2.0, static_cast<double>(cw) - 2 * kPadXS, rowInnerH, kRowR);
            cairo_set_source_rgba(cc, 0, 0, 0, 0);
            cairo_fill(cc);

            const double iconX = kPad + kPadXS;
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
              rr(cc, iconX, iconY, kIconSz, kIconSz, kPinIconR);
              cairo_set_source_rgba(cc, 0.12, 0.16, 0.18, 1.0);
              cairo_fill(cc);
              cairo_set_font_size(cc, 17.0);
              cairo_set_source_rgba(cc, primR, primG, primB, 0.95);
              std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
              cairo_move_to(cc, iconX + kIconSz * 0.30, iconY + kIconSz * 0.66);
              cairo_show_text(cc, init.c_str());
            }

            const double textLeft = iconX + kIconSz + kPad;
            const double textMaxW = static_cast<double>(cw) - textLeft - kPadS;

            cairo_select_font_face(cc, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cc, 13.5);
            cairo_set_source_rgba(cc, 0.93, 0.96, 0.98, 0.97);
            std::string nameTrunc;
            truncate_to_width(cc, hit.name, textMaxW, &nameTrunc);
            const double nameY =
                (hit.comment.empty() && hit.genericName.empty()) ? (rowY + kRowPitch * 0.5 + 5.0) : (rowY + 22.0);
            cairo_move_to(cc, textLeft, nameY);
            cairo_show_text(cc, nameTrunc.c_str());

            const std::string& sub = !hit.comment.empty() ? hit.comment : hit.genericName;
            if (!sub.empty()) {
              cairo_select_font_face(cc, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
              cairo_set_font_size(cc, 11.0);
              cairo_set_source_rgba(cc, outR, outG, outB, 0.75);
              std::string subTrunc;
              truncate_to_width(cc, sub, textMaxW, &subTrunc);
              cairo_move_to(cc, textLeft, rowY + 42.0);
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
      cairo_set_source_surface(cr, s.appListCache, lx_, dy);
      cairo_paint(cr);

      const int litRow = (s.hoverListRow >= 0) ? s.hoverListRow : s.sel;
      if (litRow >= 0 && static_cast<size_t>(litRow) < n) {
        if (s.viewMode == 0) {
          const double cellW = (lw_ - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
          const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
          const int col = litRow % kGridCols;
          const int row = litRow / kGridCols;
          const double gx = lx_ + kPadS + col * (cellW + kGridGap);
          const double gy = lt + static_cast<double>(row) * rowH - s.scrollPxCurrent;
          const double iconX = gx + (cellW - kGridIconSz) * 0.5;
          const double iconY = gy + 4.0;
          rr(cr, iconX - 2, iconY - 2, kGridIconSz + 4, kGridIconSz + 4, (kGridIconSz + 4) * 0.5);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.15);
          cairo_fill(cr);
        } else {
          const double rowY = lt + static_cast<double>(litRow) * kRowPitch - s.scrollPxCurrent;
          const double rowInnerH = kRowPitch - 4.0;
          rr(cr, lx_ + kPadXS, rowY + 2.0, lw_ - 2 * kPadXS, rowInnerH, kRowR);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.18);
          cairo_fill_preserve(cr);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.35);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }
    }
  } else if (!s.query.empty()) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.70);
    cairo_move_to(cr, lx_ + kMar, lt + 48.0);
    cairo_show_text(cr, "No matching applications");
  }

  cairo_restore(cr);
  cairo_restore(cr);

  if (!s_smenu_mode && s.pinCtxOpen && s.pinCtxAnchorIdx >= 0 &&
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
