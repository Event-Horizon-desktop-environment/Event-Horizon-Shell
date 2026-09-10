#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "../app_drawer/overlay/app_drawer_overlay.hpp"
#include "../app_drawer/power/app_drawer_power_modal.hpp"
#include "../app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "configuration/shell_config.hpp"
#include "../app_drawer/list/desktop_list.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"

#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <time.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

static bool s_smenu_mode = false;

constexpr int kPopupW = 858;
constexpr int kPopupH = 680;

constexpr double kMar   = 16.0;
constexpr double kPad   = 12.0;
constexpr double kPadS  =  8.0;
constexpr double kPadXS =  4.0;

constexpr double kLeftFrac  = 0.40;
constexpr double kRightFrac = 0.60;

constexpr double kSearchH   = 48.0;

constexpr double kRowPitch  = 64.0;
constexpr double kGridGap   = 10.0;
constexpr int    kGridCols  = 5;
constexpr double kIconSz    = 46.0;
constexpr double kGridIconSz = 48.0;
constexpr double kGridLabelGap = 4.0;
constexpr double kGridLabelFontPx = 11.0;

constexpr double kPinCols   =  3.0;
constexpr double kPinCellH  = 96.0;
constexpr double kPinIconSz = 58.0;

constexpr double kPowerBtnSz = 36.0;
constexpr double kPowerBtnR  =  8.0;

constexpr double kCatTabH   = 34.0;
constexpr double kCatTabR   =  8.0;
constexpr double kCatTabGap = 6.0;
constexpr double kCatTabFont = 14.0;

constexpr double kPopupR  = 6.0;
constexpr double kFieldR  = 10.0;
constexpr double kRowR    = 12.0;
constexpr double kCellR   =  8.0;
constexpr double kPinIconR =  8.0;

struct Rect { double x, y, w, h; };

Rect right_col(double W, double H) {
     
    if (s_smenu_mode) return {0, 0, W, H};
    double lw = W * kLeftFrac;
    return {lw, 0, W - lw, H};
}

Rect search_rect(double W, double H) {
     
    Rect rc = right_col(W, H);
    return {rc.x + kMar, kMar, rc.w - 2 * kMar, kSearchH};
}

double search_sep_y(double  ) {
    return kMar + kSearchH + kPad;
}

double list_top(double W, double H) {
     
    (void)H;
    if (s_smenu_mode) return search_sep_y(W) + kPad + kCatTabH + kPadS;
    return search_sep_y(W) + 1.0 + kPad;
}

double list_x(double W, double H) { return right_col(W, H).x + kPadS; }
double list_w(double W, double H) { return right_col(W, H).w - 2 * kPadS; }

double pinned_label_y()  { return kMar + 16.0; }

double pinned_sep_y()    { return kMar + 24.0; }

double pinned_grid_top() { return pinned_sep_y() + 1.0 + kPadXS; }

Rect pinned_cell_rect(double leftW, int col, int row) {
     
    const double gridW = leftW - 2*kMar;
    const double cellW = (gridW - (kPinCols - 1)*kPadXS) / kPinCols;
    const double x = kMar + col * (cellW + kPadXS);
    const double y = pinned_grid_top() + row * (kPinCellH + kPadXS);
    return { x, y, cellW, kPinCellH };
}

double power_row_y(double H) {
    return H - kMar - kPowerBtnSz;
}

double power_sep_y(double H) {
    return power_row_y(H) - kPadS - 1.0;
}

double cat_tab_top(double W) { return search_sep_y(W) + kPad; }

Rect power_btn_rect(double  , int idx, double H) {
     
    const double rowY = power_row_y(H);
    const double x    = kMar + idx * (kPowerBtnSz + kPadXS);
    return { x, rowY, kPowerBtnSz, kPowerBtnSz };
}

double smenu_footer_h() {
    return kPowerBtnSz + kPadS + kPadS;
}

Rect smenu_power_btn_rect(int idx, int totalBtns, double W, double H) {
     
    const double rowY = H - kMar - kPowerBtnSz;
    const double totalBtnW = totalBtns * kPowerBtnSz + (totalBtns - 1) * kPadXS;
    const double startX = (W - totalBtnW) / 2.0;
    const double x = startX + idx * (kPowerBtnSz + kPadXS);
    return { x, rowY, kPowerBtnSz, kPowerBtnSz };
}

double list_bottom(double H) {
     
    if (s_smenu_mode) return H - kMar - smenu_footer_h();
    return H - kMar;
}

void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
     
    cairo_new_path(cr);
    const double r  = std::min({rad, rw*0.5, rh*0.5});
    const double x0 = rx,      y0 = ry;
    const double x1 = rx + rw, y1 = ry + rh;
    cairo_arc(cr, x1-r, y0+r, r, -M_PI_2,       0);
    cairo_arc(cr, x1-r, y1-r, r,       0,  M_PI_2);
    cairo_arc(cr, x0+r, y1-r, r,  M_PI_2,  M_PI);
    cairo_arc(cr, x0+r, y0+r, r,  M_PI,  3*M_PI_2);
    cairo_close_path(cr);
}

void utf8_pop_back(std::string& s) {
     
    if (s.empty()) return;
    size_t i = s.size();
    while (i > 0) {
        --i;
        if ((static_cast<unsigned char>(s[i]) & 0xc0u) != 0x80u) { s.resize(i); return; }
    }
    s.clear();
}

void truncate_to_width(cairo_t* cr, const std::string& text, double maxAdv, std::string* out) {
     
    *out = text;
    cairo_text_extents_t ex{};
    for (;;) {
        cairo_text_extents(cr, out->c_str(), &ex);
        if (ex.x_advance <= maxAdv || out->size() < 4) break;
        utf8_pop_back(*out);
    }
}

struct PowerBtn { const char* ligature; const char* tooltip; };
static constexpr PowerBtn kPowerBtns[] = {
    { "lock", "Lock"     },
    { "logout",  "Logout"   },
    { "restart_alt",  "Restart"  },
    { "power_settings_new",  "Shutdown" },
    { "dark_mode",  "Nightlight" },
};
constexpr int kPowerBtnCount = 4;
constexpr int kNightlightBtnIdx = 4;

// Nightlight global state
static bool s_nightlight_active = false;
static void (*s_nightlight_toggle_fn)() = nullptr;

}

void eh_app_drawer_set_nightlight_active(bool active) { s_nightlight_active = active; }
bool eh_app_drawer_get_nightlight_active() { return s_nightlight_active; }
void eh_app_drawer_set_nightlight_toggle_fn(void (*fn)()) { s_nightlight_toggle_fn = fn; }

int eh_app_drawer_popup_width()  { return kPopupW; }
int eh_app_drawer_popup_height() { return kPopupH; }

void eh_app_drawer_clamp_scroll(DockApp& app) {
     
    s_smenu_mode = app.appMenuSmenuMode;
    const double W       = static_cast<double>(app.popupW);
    const double H       = static_cast<double>(app.popupH);
    const double lt      = list_top(W, H);
    const double listH   = H - lt - kMar;
    if (listH <= 1.0) {
        if (eh_app_drawer_debug_level() >= 3) {
            eh::shell::dock::app_drawer::trace_line(3, "dock-drawer", "clamp_scroll listH<=1 → scrollPx=0 popup=" + std::to_string(app.popupW) + "x" +
                                                         std::to_string(app.popupH));
        }
        app.appMenuScrollPx = 0;
        return;
    }
    double contentH;
    int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    if (viewMode == 0) {
      const int rows = (static_cast<int>(app.appMenuHits.size()) + kGridCols - 1) / kGridCols;
      const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
      contentH = rows * rowH;
    } else {
      contentH = static_cast<double>(app.appMenuHits.size()) * kRowPitch;
    }
    double maxScroll = contentH - listH;
    if (maxScroll < 0) maxScroll = 0;
    const double before = app.appMenuScrollPx;
    if (app.appMenuScrollPx < 0)         app.appMenuScrollPx = 0;
    if (app.appMenuScrollPx > maxScroll) app.appMenuScrollPx = maxScroll;
    if (app.appMenuScrollPxCurrent < 0)         app.appMenuScrollPxCurrent = 0;
    if (app.appMenuScrollPxCurrent > maxScroll) app.appMenuScrollPxCurrent = maxScroll;
    if (eh_app_drawer_debug_level() >= 3 && before != app.appMenuScrollPx) {
        eh::shell::dock::app_drawer::trace_line(3, "dock-drawer",
                                   "clamp_scroll before=" + std::to_string(before) + " after=" + std::to_string(app.appMenuScrollPx) +
                                       " maxScroll=" + std::to_string(maxScroll) + " hits=" + std::to_string(app.appMenuHits.size()));
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

void eh_app_drawer_update_categories(DockApp& app) {
     
    app.appMenuCategories.clear();
    app.appMenuCategoryWidths.clear();
    const auto& entries = eh::shell::dock::app_drawer::get_cached_entries();
    for (const auto& e : entries) {
        size_t start = 0;
        while (start < e.categories.size()) {
            size_t end = e.categories.find(';', start);
            if (end == std::string::npos) end = e.categories.size();
            const std::string cat = e.categories.substr(start, end - start);
            if (!cat.empty() && eh::shell::dock::app_drawer::is_standard_category(cat) &&
                std::find(app.appMenuCategories.begin(), app.appMenuCategories.end(), cat) == app.appMenuCategories.end())
                app.appMenuCategories.push_back(cat);
            start = end + 1;
        }
    }
    std::sort(app.appMenuCategories.begin(), app.appMenuCategories.end());
    if (app.appMenuSelectedCategory >= static_cast<int>(app.appMenuCategories.size()))
        app.appMenuSelectedCategory = -1;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0);
    constexpr double kPillPad = 2.0;
    cairo_text_extents_t te;
    {
        cairo_text_extents(cr, "All", &te);
        app.appMenuCategoryWidths.push_back(te.x_advance + kPillPad * 2.0);
    }
    for (const auto& cat : app.appMenuCategories) {
        cairo_text_extents(cr, cat.c_str(), &te);
        app.appMenuCategoryWidths.push_back(te.x_advance + kPillPad * 2.0);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

int eh_app_drawer_pick_category_tab(DockApp& app, double lx, double ly) {
     
    constexpr double kPillH = 34.0;
    constexpr double kPillGap = 6.0;
    const double W = static_cast<double>(app.popupW);
    const double ct = cat_tab_top(W);
    if (ly < ct || ly > ct + kPillH) return -1;
    const int nCats = static_cast<int>(app.appMenuCategories.size());
    if (app.appMenuCategoryWidths.size() < static_cast<size_t>(nCats + 1)) return -1;
    double totalW = 0.0;
    for (int i = -1; i < nCats; ++i)
        totalW += app.appMenuCategoryWidths[static_cast<size_t>(i + 1)] + kPillGap;
    totalW -= kPillGap;
    double x = (W - totalW) * 0.5;
    for (int i = -1; i < nCats; ++i) {
        const double tw = app.appMenuCategoryWidths[static_cast<size_t>(i + 1)];
        if (lx >= x && lx < x + tw + kPillGap) return i;
        x += tw + kPillGap;
    }
    return -1;
}

void eh_app_drawer_refresh_hits(DockApp& app) {
   
  s_smenu_mode = app.appMenuSmenuMode;
  const auto t0 = ShellBenchClock::now();
  if (eh_app_drawer_debug_level() >= 1) {
        eh::shell::dock::app_drawer::trace_line(1, "dock-drawer",
                                   "refresh_hits query_len=" + std::to_string(app.appMenuQuery.size()) + " sel_before=" + std::to_string(app.appMenuSel));
    }

    const std::string& q = app.appMenuQuery;
    const bool canFilter = !app.appMenuPrevQuery.empty() &&
        q.size() > app.appMenuPrevQuery.size() &&
        q.compare(0, app.appMenuPrevQuery.size(), app.appMenuPrevQuery) == 0 &&
        !app.appMenuPrevCatIndices.empty();
    if (canFilter) {
        eh_app_drawer_menu_query_filtered(q, app.appMenuPrevCatIndices,
                                          &app.settings.startMenuPinnedApps, &app.appMenuHits);
    } else {
        eh_app_drawer_menu_query(q, &app.settings.startMenuPinnedApps, &app.appMenuHits);
        const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
        std::unordered_map<std::string_view, size_t> pathToIdx;
        pathToIdx.reserve(catalog.size());
        for (size_t i = 0; i < catalog.size(); ++i)
            pathToIdx.emplace(catalog[i].path, i);
        app.appMenuPrevCatIndices.clear();
        app.appMenuPrevCatIndices.reserve(app.appMenuHits.size());
        for (const auto& hit : app.appMenuHits) {
            auto it = pathToIdx.find(hit.path);
            if (it != pathToIdx.end())
                app.appMenuPrevCatIndices.push_back(it->second);
        }
    }
    app.appMenuPrevQuery = q;
    {
        const auto& cat = eh::shell::dock::app_drawer::get_cached_entries();
        app.appMenuPinHits.clear();
        app.appMenuPinHits.reserve(app.settings.drawerPinnedApps.size());
        for (const auto& pinRaw : app.settings.drawerPinnedApps) {
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
            app.appMenuPinHits.push_back(std::move(ph));
        }
    }
    if (app.appMenuSmenuMode && app.appMenuSelectedCategory >= 0 &&
        app.appMenuSelectedCategory < static_cast<int>(app.appMenuCategories.size())) {
        const std::string& cat = app.appMenuCategories[static_cast<size_t>(app.appMenuSelectedCategory)];
        std::vector<SpotlightHit> filtered;
        for (const auto& h : app.appMenuHits) {
            if (cat_has_category(h.categories, cat))
                filtered.push_back(h);
        }
        app.appMenuHits = std::move(filtered);
    }
    eh_app_drawer_clamp_scroll(app);
    if (app.appMenuHits.empty()) {
        app.appMenuSel = -1;
    } else if (app.appMenuSel < 0 || app.appMenuSel >= static_cast<int>(app.appMenuHits.size())) {
        app.appMenuSel = 0;
    }
    if (eh_app_drawer_debug_level() >= 1) {
        eh::shell::dock::app_drawer::trace_line(1, "dock-drawer",
                                   "refresh_hits done hits=" + std::to_string(app.appMenuHits.size()) + " sel=" + std::to_string(app.appMenuSel) +
                                       " scrollPx=" + std::to_string(app.appMenuScrollPx));
    }
    ++app.appMenuListContentGen;
    {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
        std::cerr << "[search-bench] smenu_refresh_hits query=\"" << app.appMenuQuery
                  << "\" hits=" << app.appMenuHits.size()
                  << " " << us << "us" << std::endl;
    }
}

void eh_app_drawer_ensure_sel_visible(DockApp& app) {
     
    s_smenu_mode = app.appMenuSmenuMode;
    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer", "ensure_sel_visible sel=" + std::to_string(app.appMenuSel));
    }
    eh_app_drawer_clamp_scroll(app);
    if (app.appMenuSel < 0) return;
    const double W     = static_cast<double>(app.popupW);
    const double H     = static_cast<double>(app.popupH);
    const double lt    = list_top(W, H);
    const double listH = H - lt - kMar;
    if (listH <= 1.0) return;
    const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    const double rowH = viewMode == 0 ? (kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap) : kRowPitch;
    const double rowTopPx = viewMode == 0 ? (static_cast<double>(app.appMenuSel / kGridCols) * rowH) : (static_cast<double>(app.appMenuSel) * rowH);
    const double relTop   = rowTopPx - app.appMenuScrollPx;
    if (relTop < 0)
        app.appMenuScrollPx = rowTopPx;
    else if (relTop + rowH > listH)
        app.appMenuScrollPx = rowTopPx + rowH - listH;
    eh_app_drawer_clamp_scroll(app);
    app.appMenuScrollPxCurrent = app.appMenuScrollPx;
    app.appMenuScrollAnimStartNs = 0;
    if (eh_app_drawer_debug_level() >= 3) {
        eh::shell::dock::app_drawer::trace_line(3, "dock-drawer", "ensure_sel_visible scrollPx=" + std::to_string(app.appMenuScrollPx));
    }
}

void eh_app_drawer_scroll_pixels(DockApp& app, double deltaPx) {
     
    s_smenu_mode = app.appMenuSmenuMode;
    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer",
                                   "scroll_pixels deltaPx=" + std::to_string(deltaPx) + " scroll_before=" + std::to_string(app.appMenuScrollPx));
    }
    const double oldTarget = app.appMenuScrollPx;
    app.appMenuScrollPx -= deltaPx;
    eh_app_drawer_clamp_scroll(app);
    if (std::abs(app.appMenuScrollPxCurrent - app.appMenuScrollPx) > 0.5) {
        if (app.appMenuScrollPx != oldTarget) {
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            app.appMenuScrollAnimStartNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                                           static_cast<uint64_t>(ts.tv_nsec);
            app.appMenuScrollPxAnimFrom = app.appMenuScrollPxCurrent;
        }
    } else {
        app.appMenuScrollPxCurrent = app.appMenuScrollPx;
        app.appMenuScrollAnimStartNs = 0;
    }
    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer", "scroll_pixels scroll_after=" + std::to_string(app.appMenuScrollPx));
    }
}

void eh_app_drawer_scroll_tick(DockApp& app) {
    if (app.appMenuScrollAnimStartNs == 0) return;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const auto nowNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                       static_cast<uint64_t>(ts.tv_nsec);
    const double elapsedMs = static_cast<double>(nowNs - app.appMenuScrollAnimStartNs) / 1000000.0;
    constexpr double kTau = 40.0;
    const double factor = 1.0 - std::exp(-elapsedMs / kTau);
    const double diff = app.appMenuScrollPx - app.appMenuScrollPxAnimFrom;
    app.appMenuScrollPxCurrent = app.appMenuScrollPxAnimFrom + diff * factor;
    if (std::abs(app.appMenuScrollPxCurrent - app.appMenuScrollPx) <= 0.5) {
        app.appMenuScrollPxCurrent = app.appMenuScrollPx;
        app.appMenuScrollAnimStartNs = 0;
    }
}

AppDrawerHitZone eh_app_drawer_hit_zone(DockApp& app, double lx, double ly) {
     
    s_smenu_mode = app.appMenuSmenuMode;
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);

    if (!s_smenu_mode) {
        const double lw = W * kLeftFrac;

        if (lx < lw) {

            const int maxPins = static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()));
            const int rows    = (maxPins + 2) / 3;
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < 3; ++col) {
                    int idx = row*3 + col;
                    if (idx >= maxPins) break;
                    Rect c = pinned_cell_rect(lw, col, row);
                    if (lx >= c.x && lx < c.x+c.w && ly >= c.y && ly < c.y+c.h)
                        return AppDrawerHitZone::PinnedApp;
                }

            for (int i = 0; i < kPowerBtnCount; ++i) {
                Rect b = power_btn_rect(lw, i, H);
                if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
                    return AppDrawerHitZone::PowerButton;
            }
            {
                Rect b = power_btn_rect(lw, kNightlightBtnIdx, H);
                if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
                    return AppDrawerHitZone::NightlightButton;
            }
            return AppDrawerHitZone::None;
        }
    }

    Rect sr = search_rect(W, H);
    if (lx >= sr.x && lx < sr.x+sr.w && ly >= sr.y && ly < sr.y+sr.h)
        return AppDrawerHitZone::SearchField;

    if (s_smenu_mode) {
        const double ct = cat_tab_top(W);
        if (ly >= ct && ly < ct + kCatTabH)
            return AppDrawerHitZone::CategoryTab;
    }

    const double lt = list_top(W, H);
    const double lb = list_bottom(H);
    if (ly >= lt && ly < lb) return AppDrawerHitZone::AppListRow;

    if (s_smenu_mode) {
        const int totalBtns = kPowerBtnCount + 1;
        for (int i = 0; i < kPowerBtnCount; ++i) {
            Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
            if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
                return AppDrawerHitZone::PowerButton;
        }
        {
            Rect b = smenu_power_btn_rect(kNightlightBtnIdx, totalBtns, W, H);
            if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
                return AppDrawerHitZone::NightlightButton;
        }
    }

    return AppDrawerHitZone::None;
}

int eh_app_drawer_pick_row_index(DockApp& app, double lx, double ly) {
     
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    const double lt = list_top(W, H);
    if (ly < lt || ly > H - kMar) return -1;
    const double rel = ly - lt + app.appMenuScrollPxCurrent;
    const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    if (viewMode == 0) {
        const double lx_ = list_x(W, H);
        const double lw_ = list_w(W, H);
        const double cellW = (lw_ - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
        const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
        const int row = static_cast<int>(std::floor(rel / rowH));
        const double colX = lx - lx_;
        const int col = static_cast<int>(std::floor((colX - kPadS) / (cellW + kGridGap)));
        const int idx = row * kGridCols + std::clamp(col, 0, kGridCols - 1);
        if (idx < 0 || idx >= static_cast<int>(app.appMenuHits.size())) return -1;
        return idx;
    }
    const int    idx = static_cast<int>(std::floor(rel / kRowPitch));
    if (idx < 0 || idx >= static_cast<int>(app.appMenuHits.size())) return -1;
    return idx;
}

int eh_app_drawer_pick_pinned_index(DockApp& app, double lx, double ly) {
     
    if (app.appMenuSmenuMode) return -1;
    const double W       = static_cast<double>(app.popupW);
    const double lw      = W * kLeftFrac;
    const int    maxPins = static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()));
    const int    rows    = (maxPins + 2) / 3;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < 3; ++col) {
            int idx = row*3 + col;
            if (idx >= maxPins) break;
            Rect c = pinned_cell_rect(lw, col, row);
            if (lx >= c.x && lx < c.x+c.w && ly >= c.y && ly < c.y+c.h)
                return idx;
        }
    return -1;
}

int eh_app_drawer_pick_power_index(DockApp& app, double lx, double ly) {
     
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    if (app.appMenuSmenuMode) {
        s_smenu_mode = true;
        const int totalBtns = kPowerBtnCount + 1;
        for (int i = 0; i < kPowerBtnCount; ++i) {
            Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
            if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
                return i;
        }
        return -1;
    }
    const double lw = W * kLeftFrac;
    for (int i = 0; i < kPowerBtnCount; ++i) {
        Rect b = power_btn_rect(lw, i, H);
        if (lx >= b.x && lx < b.x+b.w && ly >= b.y && ly < b.y+b.h)
            return i;
    }
    return -1;
}

void eh_app_drawer_paint(DockApp& app, cairo_t* cr, bool paintBackdrop, bool paintContent,
                         float backdrop_alpha_scale) {
     
    eh_app_drawer_scroll_tick(app);
    s_smenu_mode = app.appMenuSmenuMode;

    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer",
                                   "paint begin W=" + std::to_string(app.popupW) + " H=" + std::to_string(app.popupH) +
                                       " backdrop=" + std::string(paintBackdrop ? "1" : "0") + " content=" +
                                       std::string(paintContent ? "1" : "0") + " hits=" + std::to_string(app.appMenuHits.size()) +
                                       " sel=" + std::to_string(app.appMenuSel) + " smenu=" + std::string(s_smenu_mode ? "1" : "0"));
    }
    const eh::config::ChromePaintColors mc =
        eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
    const double surfR = mc.dockFillR, surfG = mc.dockFillG, surfB = mc.dockFillB;
    const double dimR = mc.drawerDimR, dimG = mc.drawerDimG, dimB = mc.drawerDimB;
    const double primR = mc.accentR, primG = mc.accentG, primB = mc.accentB;
    const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
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
            cairo_set_source_rgba(cr, dimR, dimG, dimB, 0.40 * bs);
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
    cairo_move_to(cr, kMar,      sepY1);
    cairo_line_to(cr, lw - kMar, sepY1);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    const int maxPins = static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()));
    if (maxPins > 0) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        for (int i = 0; i < maxPins; ++i) {
            const int col = i % 3;
            const int row = i / 3;
            Rect cell = pinned_cell_rect(lw, col, row);
            const bool pinLit = (i == app.appMenuDrawerPinHoverIdx);
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

            const std::string& rawId = app.settings.drawerPinnedApps[static_cast<size_t>(i)];
            const SpotlightHit& pinHit = app.appMenuPinHits[static_cast<size_t>(i)];
            const bool hasPin = !pinHit.path.empty();
            const std::string desktopPath = hasPin ? pinHit.path : std::string{};
            const std::string iconKey = hasPin ? pinHit.iconKey : rawId;
            std::string displayName;
            if (hasPin && !pinHit.name.empty())
                displayName = pinHit.name;
            else {
                displayName = rawId;
                if (displayName.size() > 8 && displayName.substr(displayName.size() - 8) == ".desktop")
                    displayName = displayName.substr(0, displayName.size() - 8);
                if (const auto sl = displayName.rfind('/'); sl != std::string::npos)
                    displayName = displayName.substr(sl + 1);
            }

            const double ix = cell.x + (cell.w - kPinIconSz) * 0.5;
            const double iy = cell.y + 8.0;

            bool iconDrawn = false;
            if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(app, desktopPath, iconKey)) {
                if (ic->surface) {
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
    cairo_move_to(cr, kMar,      sepY2);
    cairo_line_to(cr, lw - kMar, sepY2);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 15.0);
    for (int i = 0; i < kPowerBtnCount; ++i) {
        Rect b = power_btn_rect(lw, i, H);

        if (app.appMenuPowerHoverIdx == i) {
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
        const double hx = app.pointerX, hy = app.pointerY;
        const bool hovering = (hx >= b.x && hx < b.x + b.w && hy >= b.y && hy < b.y + b.h);

        if (hovering) {
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
        const double sepY2 = smenu_power_btn_rect(0, totalBtns, W, H).y - kPadS - 1.0;
        cairo_move_to(cr, kMar, sepY2);
        cairo_line_to(cr, W - kMar, sepY2);
        cairo_set_source_rgba(cr, outR, outG, outB, 0.18);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15.0);
        for (int i = 0; i < kPowerBtnCount; ++i) {
            Rect b = smenu_power_btn_rect(i, totalBtns, W, H);
            if (app.appMenuPowerHoverIdx == i) {
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
            const double rowY = smenu_power_btn_rect(0, totalBtns, W, H).y;
            cairo_move_to(cr, sepX, rowY + 2.0);
            cairo_line_to(cr, sepX, rowY + kPowerBtnSz - 2.0);
            cairo_set_source_rgba(cr, outR, outG, outB, 0.25);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        // Nightlight button
        {
            Rect b = smenu_power_btn_rect(kNightlightBtnIdx, totalBtns, W, H);
            const double hx = app.pointerX, hy = app.pointerY;
            const bool hovering = (hx >= b.x && hx < b.x + b.w && hy >= b.y && hy < b.y + b.h);
            if (hovering) {
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
      sb.setColor(static_cast<float>(dimR), static_cast<float>(dimG),
                  static_cast<float>(dimB), 0.55f);
      sb.setRadius(static_cast<float>(kFieldR));
      sb.setGeometry(static_cast<float>(sr.x), static_cast<float>(sr.y),
                     static_cast<float>(sr.w), static_cast<float>(sr.h));
      sb.setGlassy(true);
      sb.paint(cr);
    }

    if (!app.appMenuQuery.empty())
        cairo_set_source_rgba(cr, primR, primG, primB, 0.65);
    else
        cairo_set_source_rgba(cr, outR, outG, outB, 0.28);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 15.0);
    const double siconX = sr.x + kPad;
    const double siconY = sr.y + sr.h * 0.5 + 5.5;
    if (!app.appMenuQuery.empty())
      eh::shell::draw_material_glyph(cr, siconX + 8.0, sr.y + sr.h * 0.5, 20.0, "search", primR, primG, primB, 1.0);
    else
      eh::shell::draw_material_glyph(cr, siconX + 8.0, sr.y + sr.h * 0.5, 20.0, "search", outR, outG, outB, 1.0);

    const double textX = siconX + 22.0;
    cairo_save(cr);
    cairo_rectangle(cr, sr.x+2, sr.y+2, sr.w-4, sr.h-4);
    cairo_clip(cr);
    cairo_set_font_size(cr, 14.5);
    std::string shown;
    if (app.appMenuQuery.empty()) {
        cairo_set_source_rgba(cr, outR, outG, outB, 0.65);
        cairo_move_to(cr, textX, siconY);
        cairo_show_text(cr, "Type here to search");
    } else {
        truncate_to_width(cr, app.appMenuQuery, sr.x + sr.w - textX - kMar, &shown);
        cairo_set_source_rgba(cr, 0.92, 0.95, 0.96, 0.97);
        cairo_move_to(cr, textX, siconY);
        cairo_show_text(cr, shown.c_str());
    }
    {
        const uint64_t caretMono = eh::shell::monotonic_ms();
        const bool caretOn = eh::shell::text_caret_blink_on(caretMono, app.appMenuSearchFocused);
        double caretX = textX;
        if (!app.appMenuQuery.empty()) {
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
    cairo_move_to(cr, rcx + kMar,      ssy);
    cairo_line_to(cr, rcx + right_col(W,H).w - kMar, ssy);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    if (s_smenu_mode && !app.appMenuCategories.empty()) {
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
        for (int i = -1; i < static_cast<int>(app.appMenuCategories.size()); ++i)
            totalW += app.appMenuCategoryWidths[static_cast<size_t>(i + 1)] + kPillGap;
        totalW -= kPillGap;
        double cx = (W - totalW) * 0.5;
        for (int i = -1; i < static_cast<int>(app.appMenuCategories.size()); ++i) {
            const bool selected = (i == app.appMenuSelectedCategory);
            const bool hovered = (i == app.appMenuCategoryHoverIdx);
            const char* label = (i < 0) ? "All" : app.appMenuCategories[static_cast<size_t>(i)].c_str();
            const double tw = app.appMenuCategoryWidths[static_cast<size_t>(i + 1)];
            {
              m3::Box pill;
              if (selected) {
                pill.setColor(static_cast<float>(primR), static_cast<float>(primG),
                              static_cast<float>(primB), 0.25f);
              } else if (hovered) {
                pill.setColor(static_cast<float>(primR), static_cast<float>(primG),
                              static_cast<float>(primB), 0.10f);
              } else {
                pill.setColor(static_cast<float>(dimR), static_cast<float>(dimG),
                              static_cast<float>(dimB), 0.40f);
              }
              pill.setRadius(static_cast<float>(kPillR));
              pill.setGeometry(static_cast<float>(cx), static_cast<float>(ct),
                               static_cast<float>(tw), static_cast<float>(kPillH));
              pill.setGlassy(true);
              pill.paint(cr);
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

    const double lt    = list_top(W, H);
    const double lb    = list_bottom(H);
    const double listH = lb - lt;
    const double lx_   = list_x(W, H);
    const double lw_   = list_w(W, H);

    cairo_save(cr);
    cairo_rectangle(cr, lx_, lt, lw_, std::max(0.0, listH));
    cairo_clip(cr);

    if (!app.appMenuHits.empty()) {
        const size_t n = app.appMenuHits.size();
        const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
        const double totalContentH = (viewMode == 0)
            ? (static_cast<double>((static_cast<int>(n) + kGridCols - 1) / kGridCols) *
               (kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap))
            : static_cast<double>(n) * kRowPitch;
        const int cw = static_cast<int>(lw_);
        const int ch = static_cast<int>(totalContentH);

        bool cacheOk = app.appMenuListCache &&
            app.appMenuListCacheW == cw &&
            app.appMenuListCacheH == ch &&
            app.appMenuListCacheViewMode == viewMode &&
            app.appMenuListCacheContentGen == app.appMenuListContentGen;

        if (!cacheOk) {
            if (app.appMenuListCache) {
                cairo_surface_destroy(app.appMenuListCache);
                app.appMenuListCache = nullptr;
            }
            if (cw > 0 && ch > 0) {
                app.appMenuListCache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
                cairo_t* cc = cairo_create(app.appMenuListCache);
                cairo_select_font_face(cc, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

                if (viewMode == 0) {
                    const double cellW = (static_cast<double>(cw) - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
                    const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
                    for (size_t i = 0; i < n; ++i) {
                        const int col = static_cast<int>(i) % kGridCols;
                        const int row = static_cast<int>(i) / kGridCols;
                        const double gx = kPadS + col * (cellW + kGridGap);
                        const double gy = static_cast<double>(row) * rowH;
                        const SpotlightHit& hit = app.appMenuHits[i];
                        const double iconX = gx + (cellW - kGridIconSz) * 0.5;
                        const double iconY = gy + 4.0;

                        bool iconDrawn = false;
                        if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(app, hit.path, hit.iconKey)) {
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
                        const SpotlightHit& hit = app.appMenuHits[i];

                        const double rowInnerH = kRowPitch - 4.0;
                        rr(cc, kPadXS, rowY + 2.0, static_cast<double>(cw) - 2 * kPadXS, rowInnerH, kRowR);
                        cairo_set_source_rgba(cc, 0, 0, 0, 0);
                        cairo_fill(cc);

                        const double iconX = kPad + kPadXS;
                        const double iconY = rowY + (kRowPitch - kIconSz) * 0.5;

                        bool iconDrawn = false;
                        if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(app, hit.path, hit.iconKey)) {
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
                        const double nameY = hit.comment.empty() && hit.genericName.empty()
                                             ? rowY + kRowPitch * 0.5 + 5.0
                                             : rowY + 22.0;
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
            app.appMenuListCacheW = cw;
            app.appMenuListCacheH = ch;
            app.appMenuListCacheViewMode = viewMode;
            app.appMenuListCacheContentGen = app.appMenuListContentGen;
        }

        if (app.appMenuListCache) {
            const double dy = lt - app.appMenuScrollPxCurrent;
            cairo_set_source_surface(cr, app.appMenuListCache, lx_, dy);
            cairo_paint(cr);

            const int litRow = (app.appMenuHoverRow >= 0) ? app.appMenuHoverRow : app.appMenuSel;
            if (litRow >= 0 && static_cast<size_t>(litRow) < n) {
                if (viewMode == 0) {
                    const double cellW = (lw_ - kPadS - (kGridCols - 1) * kGridGap) / kGridCols;
                    const double rowH = kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap;
                    const int col = litRow % kGridCols;
                    const int row = litRow / kGridCols;
                    const double gx = lx_ + kPadS + col * (cellW + kGridGap);
                    const double gy = lt + static_cast<double>(row) * rowH - app.appMenuScrollPxCurrent;
                    const double iconX = gx + (cellW - kGridIconSz) * 0.5;
                    const double iconY = gy + 4.0;
                    rr(cr, iconX - 2, iconY - 2, kGridIconSz + 4, kGridIconSz + 4, (kGridIconSz + 4) * 0.5);
                    cairo_set_source_rgba(cr, primR, primG, primB, 0.15);
                    cairo_fill(cr);
                } else {
                    const double rowY = lt + static_cast<double>(litRow) * kRowPitch - app.appMenuScrollPxCurrent;
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
    } else if (!app.appMenuQuery.empty()) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13.0);
        cairo_set_source_rgba(cr, outR, outG, outB, 0.70);
        cairo_move_to(cr, lx_ + kMar, lt + 48.0);
        cairo_show_text(cr, "No matching applications");
    }

    cairo_restore(cr);
    cairo_restore(cr);

    if (!s_smenu_mode && app.appMenuPowerConfirmOpen && app.appMenuPowerConfirmIdx >= 1 && app.appMenuPowerConfirmIdx <= 3) {
        eh::shell::dock::app_drawer::paint_power_confirm_modal(cr, W, H, app.appMenuPowerConfirmIdx, app.pointerX, app.pointerY, mc);
    }
    eh::shell::dock::app_drawer::AppDrawerChromeColors adc{};
    adc.dockFillR = mc.dockFillR;
    adc.dockFillG = mc.dockFillG;
    adc.dockFillB = mc.dockFillB;
    adc.drawerDimR = mc.drawerDimR;
    adc.drawerDimG = mc.drawerDimG;
    adc.drawerDimB = mc.drawerDimB;
    adc.accentR = mc.accentR;
    adc.accentG = mc.accentG;
    adc.accentB = mc.accentB;
    adc.outlineR = mc.outlineR;
    adc.outlineG = mc.outlineG;
    adc.outlineB = mc.outlineB;
    if (!s_smenu_mode && app.appMenuPinCtxOpen && app.appMenuPinCtxAnchorIdx >= 0 &&
        app.appMenuPinCtxAnchorIdx <
            static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()))) {
        const char* lp0 = app.appMenuPinCtxPinnedDock ? "Unpin from dock" : "Pin to dock";
        const char* lp1 = "Remove from drawer";
        const char* linesP[2] = {lp0, lp1};
        double mxp = 0, myp = 0, mwp = 0, mhp = 0;
        eh::shell::dock::app_drawer::app_drawer_row_context_menu_layout(app.appMenuPinCtxMenuX, app.appMenuPinCtxMenuY, W, H, 2, &mxp, &myp,
                                                          &mwp, &mhp);
        eh::shell::dock::app_drawer::app_drawer_row_context_menu_paint(cr, mxp, myp, linesP, 2, app.appMenuPinCtxHoverItem, adc);
    }
    if (app.appMenuRowCtxOpen && app.appMenuRowCtxAnchorRow >= 0 &&
        app.appMenuRowCtxAnchorRow < static_cast<int>(app.appMenuHits.size())) {
        const char* l0 = app.appMenuRowCtxPinnedDock ? "Unpin from dock" : "Pin to dock";
        const char* l1 = app.appMenuRowCtxPinnedStart ? "Unpin from Start" : "Pin to Start";
        const char* l2 = app.appMenuRowCtxPinnedDrawer ? "Unpin from drawer" : "Pin to drawer";
        const char* linesR[3] = {l0, l1, l2};
        const int n = std::clamp(app.appMenuRowCtxItemCount, 2, eh::shell::dock::app_drawer::kAppDrawerRowContextItemMax);
        double mx = 0, my = 0, mw = 0, mh = 0;
        eh::shell::dock::app_drawer::app_drawer_row_context_menu_layout(app.appMenuRowCtxMenuX, app.appMenuRowCtxMenuY, W, H, n, &mx, &my,
                                                          &mw, &mh);
        eh::shell::dock::app_drawer::app_drawer_row_context_menu_paint(cr, mx, my, linesR, n, app.appMenuRowCtxHoverItem, adc);
    }
    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer", "paint end");
    }
}
