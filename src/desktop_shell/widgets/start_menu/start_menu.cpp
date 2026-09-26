#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/tahoe/tahoe_launcher.hpp"
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

// Tahoe unified launcher: single full-width layout, no pinned sidebar.
// (Pinned data stays for context menus; presentation follows Apple's Apps
// browser: search + count pills + icon grid + slim power footer.)
constexpr int kPopupW = 858;
constexpr int kPopupH = 680;

constexpr double kMar   = 20.0;
constexpr double kPad   = 12.0;
constexpr double kPadS  =  8.0;
constexpr double kPadXS =  4.0;

constexpr double kSearchH   = 46.0;

constexpr double kRowPitch  = 64.0;
constexpr double kIconSz    = 46.0;

constexpr double kPowerBtnSz = 32.0;
constexpr double kPowerBtnR  =  8.0;
constexpr int    kPowerBtnCount = 4;
constexpr int    kNightlightBtnIdx = 4;
constexpr int    kFooterBtnCount = 5;

constexpr double kPopupR  = 24.0;
constexpr double kFieldR  = 14.0;
constexpr double kRowR    = 12.0;
constexpr double kCellR   =  16.0;
constexpr double kPinIconR =  8.0;

constexpr double kPillH   = 32.0;

using eh::shell::tahoe::TahoeLayout;

TahoeLayout tahoe_geom(double W, double H) {
  return eh::shell::tahoe::tahoe_layout(W, H);
}

struct Rect { double x, y, w, h; };

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
     
    const double W       = static_cast<double>(app.popupW);
    const double H       = static_cast<double>(app.popupH);
    const double lt      = list_top(W, H);
    const double listH   = list_bottom(W, H) - lt;
    if (listH <= 1.0) {
        if (eh_app_drawer_debug_level() >= 3) {
            eh::shell::dock::app_drawer::trace_line(3, "dock-drawer", "clamp_scroll listH<=1 → scrollPx=0 popup=" + std::to_string(app.popupW) + "x" +
                                                         std::to_string(app.popupH));
        }
        app.appMenuScrollPx = 0;
        return;
    }
    double contentH;
    const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    if (viewMode == 0) {
      contentH = eh::shell::tahoe::tahoe_grid_content_h(app.appMenuHits.size(), tahoe_geom(W, H));
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

void eh_app_drawer_update_categories(DockApp& app) {
    // Tahoe buckets (fixed order) with live catalog counts, e.g. "Main 22".
    app.appMenuCategories.clear();
    app.appMenuCategoryWidths.clear();
    app.appMenuCategoryCounts.clear();
    const auto& entries = eh::shell::dock::app_drawer::get_cached_entries();
    int counts[eh::shell::tahoe::kTahoeBucketCount] = {};
    for (const auto& e : entries)
        counts[eh::shell::tahoe::tahoe_bucket_for(e.categories)]++;
    for (int b = 0; b < eh::shell::tahoe::kTahoeBucketCount; ++b) {
        app.appMenuCategories.emplace_back(eh::shell::tahoe::kTahoeBuckets[b].label);
        app.appMenuCategoryCounts.push_back(counts[b]);
    }
    if (app.appMenuSelectedCategory >= static_cast<int>(app.appMenuCategories.size()))
        app.appMenuSelectedCategory = -1;
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
    app.appMenuCategoryCounts.push_back(static_cast<int>(entries.size()));
    app.appMenuCategoryWidths.push_back(pill_w("All", static_cast<int>(entries.size())));
    for (int b = 0; b < eh::shell::tahoe::kTahoeBucketCount; ++b) {
        app.appMenuCategoryCounts.push_back(counts[b]);
        app.appMenuCategoryWidths.push_back(
            pill_w(app.appMenuCategories[static_cast<size_t>(b)], counts[b]));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

int eh_app_drawer_pick_category_tab(DockApp& app, double lx, double ly) {
    const double W = static_cast<double>(app.popupW);
    const double H = static_cast<double>(app.popupH);
    const TahoeLayout l = tahoe_geom(W, H);
    if (ly < l.pillsY || ly > l.pillsY + l.pillsH) return -1;
    const int nCats = static_cast<int>(app.appMenuCategories.size());
    if (app.appMenuCategoryWidths.size() < static_cast<size_t>(nCats + 1)) return -1;
    const auto xs = eh::shell::tahoe::tahoe_pill_xs(app.appMenuCategoryWidths, l.pillsAvailX, l.pillsAvailW,
                                                    app.appMenuSelectedCategory + 1);
    for (int i = -1; i < nCats; ++i) {
        const size_t k = static_cast<size_t>(i + 1);
        if (lx >= xs[k] && lx < xs[k] + app.appMenuCategoryWidths[k]) return i;
    }
    return -1;
}

void eh_app_drawer_refresh_hits(DockApp& app) {
   
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
    if (app.appMenuCategories.empty()) eh_app_drawer_update_categories(app);
    if (app.appMenuSelectedCategory >= 0 &&
        app.appMenuSelectedCategory < static_cast<int>(app.appMenuCategories.size())) {
        const int bucket = app.appMenuSelectedCategory;
        std::vector<SpotlightHit> filtered;
        for (const auto& h : app.appMenuHits) {
            if (eh::shell::tahoe::tahoe_hit_in_bucket(h.categories, bucket))
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
     
    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer", "ensure_sel_visible sel=" + std::to_string(app.appMenuSel));
    }
    eh_app_drawer_clamp_scroll(app);
    if (app.appMenuSel < 0) return;
    const double W     = static_cast<double>(app.popupW);
    const double H     = static_cast<double>(app.popupH);
    const double lt    = list_top(W, H);
    const double listH = list_bottom(W, H) - lt;
    if (listH <= 1.0) return;
    const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    const TahoeLayout tl = tahoe_geom(W, H);
    const int cols = eh::shell::tahoe::kTahoeCols;
    const double rowH = viewMode == 0 ? (tl.cellH + eh::shell::tahoe::kTahoeGridGap) : kRowPitch;
    const double rowTopPx = viewMode == 0 ? (static_cast<double>(app.appMenuSel / cols) * rowH) : (static_cast<double>(app.appMenuSel) * rowH);
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
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    const TahoeLayout l = tahoe_geom(W, H);

    Rect sr = search_rect(W, H);
    if (lx >= sr.x && lx < sr.x + sr.w && ly >= sr.y && ly < sr.y + sr.h)
        return AppDrawerHitZone::SearchField;

    if (ly >= l.pillsY && ly < l.pillsY + l.pillsH && lx >= l.pillsAvailX &&
        lx < l.pillsAvailX + l.pillsAvailW)
        return AppDrawerHitZone::CategoryTab;

    const double lt = list_top(W, H);
    const double lb = list_bottom(W, H);
    if (ly >= lt && ly < lb && lx >= l.gridX && lx < l.gridX + l.gridW)
        return AppDrawerHitZone::AppListRow;

    for (int i = 0; i < kPowerBtnCount; ++i) {
        Rect b = power_btn_rect(W, i, H);
        if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h)
            return AppDrawerHitZone::PowerButton;
    }
    {
        Rect b = power_btn_rect(W, kNightlightBtnIdx, H);
        if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h)
            return AppDrawerHitZone::NightlightButton;
    }

    return AppDrawerHitZone::None;
}

int eh_app_drawer_pick_row_index(DockApp& app, double lx, double ly) {
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    const TahoeLayout l = tahoe_geom(W, H);
    const double lt = list_top(W, H);
    const double lb = list_bottom(W, H);
    if (ly < lt || ly > lb) return -1;
    const int viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    if (viewMode == 0) {
        return eh::shell::tahoe::tahoe_pick_grid(lx, ly, app.appMenuHits.size(), l,
                                                 app.appMenuScrollPxCurrent);
    }
    const double rel = ly - lt + app.appMenuScrollPxCurrent;
    const int idx = static_cast<int>(std::floor(rel / kRowPitch));
    if (idx < 0 || idx >= static_cast<int>(app.appMenuHits.size())) return -1;
    return idx;
}

int eh_app_drawer_pick_pinned_index(DockApp& app, double lx, double ly) {
    (void)app; (void)lx; (void)ly;
    return -1;
}

int eh_app_drawer_pick_power_index(DockApp& app, double lx, double ly) {
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    for (int i = 0; i < kPowerBtnCount; ++i) {
        Rect b = power_btn_rect(W, i, H);
        if (lx >= b.x && lx < b.x + b.w && ly >= b.y && ly < b.y + b.h)
            return i;
    }
    return -1;
}

void eh_app_drawer_paint(DockApp& app, cairo_t* cr, bool paintBackdrop, bool paintContent,
                         float backdrop_alpha_scale) {
    eh_app_drawer_scroll_tick(app);

    if (eh_app_drawer_debug_level() >= 2) {
        eh::shell::dock::app_drawer::trace_line(2, "dock-drawer",
                                   "paint begin W=" + std::to_string(app.popupW) + " H=" + std::to_string(app.popupH) +
                                       " backdrop=" + std::string(paintBackdrop ? "1" : "0") + " content=" +
                                       std::string(paintContent ? "1" : "0") + " hits=" + std::to_string(app.appMenuHits.size()) +
                                       " sel=" + std::to_string(app.appMenuSel));
    }
    const auto& scSnap = eh::config::shell_config_snapshot();
    const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(scSnap.appearance);
    const double primR = mc.accentR, primG = mc.accentG, primB = mc.accentB;
    const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;
    const double W  = static_cast<double>(app.popupW);
    const double H  = static_cast<double>(app.popupH);
    const TahoeLayout l = tahoe_geom(W, H);
    const double bs = std::clamp(static_cast<double>(backdrop_alpha_scale), 0.0, 1.0);
    const double cardA = scSnap.appearance.overlayOpacityWidgetCard * bs;
    const int viewMode = scSnap.appearance.launchpadViewMode;

    namespace th = eh::shell::tahoe;

    if (paintBackdrop)
        th::tahoe_paint_card(cr, W, H, mc.dockFillR, mc.dockFillG, mc.dockFillB, cardA);

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
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, app.appMenuQuery.empty() ? 0.12 : 0.30);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        const double midY = sr.y + sr.h * 0.5;
        eh::shell::draw_material_glyph(cr, sr.x + 30.0, midY, 20.0, "search",
                                       app.appMenuQuery.empty() ? outR : primR,
                                       app.appMenuQuery.empty() ? outG : primG,
                                       app.appMenuQuery.empty() ? outB : primB, 1.0);

        const double textX = sr.x + 52.0;
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15.0);
        std::string shown;
        if (app.appMenuQuery.empty()) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.38);
            cairo_move_to(cr, textX, midY + 5.5);
            cairo_show_text(cr, "Search for an app\u2026");
        } else {
            truncate_to_width(cr, app.appMenuQuery, sr.x + sr.w - textX - 130.0, &shown);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
            cairo_move_to(cr, textX, midY + 5.5);
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
    if (!app.appMenuCategories.empty() &&
        app.appMenuCategoryWidths.size() >= app.appMenuCategories.size() + 1) {
        const int nCats = static_cast<int>(app.appMenuCategories.size());
        const auto xs = th::tahoe_pill_xs(app.appMenuCategoryWidths, l.pillsAvailX, l.pillsAvailW,
                                          app.appMenuSelectedCategory + 1);
        cairo_save(cr);
        cairo_rectangle(cr, l.pillsAvailX, l.pillsY - 2.0, l.pillsAvailW, l.pillsH + 4.0);
        cairo_clip(cr);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, eh::shell::tahoe::kTahoePillFontPx);
        for (int i = -1; i < nCats; ++i) {
            const size_t k = static_cast<size_t>(i + 1);
            const double px = xs[k];
            const double pw = app.appMenuCategoryWidths[k];
            if (px + pw < l.pillsAvailX || px > l.pillsAvailX + l.pillsAvailW) continue;
            const bool selected = (i == app.appMenuSelectedCategory);
            const bool hovered = (i == app.appMenuCategoryHoverIdx);
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
            const char* label = (i < 0) ? "All" : app.appMenuCategories[static_cast<size_t>(i)].c_str();
            const int count = (k < app.appMenuCategoryCounts.size()) ? app.appMenuCategoryCounts[k] : 0;
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
            if (app.appMenuPowerHoverIdx == i) {
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
            if (app.appMenuPowerHoverIdx == kNightlightBtnIdx) {
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

    if (!app.appMenuHits.empty()) {
        const size_t n = app.appMenuHits.size();
        const double totalContentH = (viewMode == 0)
            ? th::tahoe_grid_content_h(n, l)
            : static_cast<double>(n) * kRowPitch;
        const int cw = static_cast<int>(l.gridW);
        const int ch = static_cast<int>(std::ceil(totalContentH));

        const bool cacheOk = app.appMenuListCache &&
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
                cairo_select_font_face(cc, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

                if (viewMode == 0) {
                    cairo_set_font_size(cc, th::kTahoeLabelFontPx);
                    for (size_t i = 0; i < n; ++i) {
                        double gx = 0, gy = 0;
                        th::tahoe_cell_xy(static_cast<int>(i), l, 0.0, gx, gy);
                        gx -= l.gridX;
                        gy -= l.gridY;
                        const SpotlightHit& hit = app.appMenuHits[i];
                        const double iconX = gx + (l.cellW - l.iconSz) * 0.5;
                        const double iconY = gy + 4.0;

                        bool iconDrawn = false;
                        if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(app, hit.path, hit.iconKey)) {
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
                        const SpotlightHit& hit = app.appMenuHits[i];

                        const double iconX = kPad;
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
                        const double nameY = hit.comment.empty() && hit.genericName.empty()
                                             ? rowY + kRowPitch * 0.5 + 5.0
                                             : rowY + 24.0;
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
            app.appMenuListCacheW = cw;
            app.appMenuListCacheH = ch;
            app.appMenuListCacheViewMode = viewMode;
            app.appMenuListCacheContentGen = app.appMenuListContentGen;
        }

        if (app.appMenuListCache) {
            const double dy = lt - app.appMenuScrollPxCurrent;
            cairo_set_source_surface(cr, app.appMenuListCache, l.gridX, dy);
            cairo_paint(cr);

            const int litRow = (app.appMenuHoverRow >= 0) ? app.appMenuHoverRow : app.appMenuSel;
            if (litRow >= 0 && static_cast<size_t>(litRow) < n) {
                if (viewMode == 0) {
                    double gx = 0, gy = 0;
                    th::tahoe_cell_xy(litRow, l, app.appMenuScrollPxCurrent, gx, gy);
                    gy += lt - l.gridY;
                    const bool sel = (litRow == app.appMenuSel);
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
                    const double rowY = lt + static_cast<double>(litRow) * kRowPitch - app.appMenuScrollPxCurrent;
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
        const char* msg = app.appMenuQuery.empty() ? "No applications" : "No matching applications";
        cairo_text_extents_t ex{};
        cairo_text_extents(cr, msg, &ex);
        cairo_move_to(cr, l.gridX + (l.gridW - ex.x_advance) * 0.5, lt + 64.0);
        cairo_show_text(cr, msg);
    }

    cairo_restore(cr);
    cairo_restore(cr);

    if (app.appMenuPowerConfirmOpen && app.appMenuPowerConfirmIdx >= 1 && app.appMenuPowerConfirmIdx <= 3) {
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
    if (false && app.appMenuPinCtxOpen && app.appMenuPinCtxAnchorIdx >= 0 &&
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
