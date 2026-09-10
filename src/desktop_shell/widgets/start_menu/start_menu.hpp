#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include <cairo/cairo.h>
#include <string>

int eh_app_drawer_popup_width();
int eh_app_drawer_popup_height();

void eh_app_drawer_refresh_hits(DockApp& app);
void eh_app_drawer_clamp_scroll(DockApp& app);
void eh_app_drawer_ensure_sel_visible(DockApp& app);

enum class AppDrawerHitZone : uint8_t {
    None = 0,
    SearchField,
    CategoryTab,
    AppListRow,
    PinnedApp,
    PowerButton,
    NightlightButton,
    OrganizeButton,
};

void eh_app_drawer_update_categories(DockApp& app);

int eh_app_drawer_pick_category_tab(DockApp& app, double localX, double localY);

void eh_app_drawer_set_nightlight_active(bool active);
bool eh_app_drawer_get_nightlight_active();
void eh_app_drawer_set_nightlight_toggle_fn(void (*fn)());

AppDrawerHitZone eh_app_drawer_hit_zone(DockApp& app, double localX, double localY);

int eh_app_drawer_pick_row_index(DockApp& app, double localX, double localY);

int eh_app_drawer_pick_pinned_index(DockApp& app, double localX, double localY);

int eh_app_drawer_pick_power_index(DockApp& app, double localX, double localY);

void eh_app_drawer_scroll_pixels(DockApp& app, double deltaPx);
void eh_app_drawer_scroll_tick(DockApp& app);

void eh_app_drawer_paint(DockApp& app, cairo_t* cr, bool paintBackdrop = true, bool paintContent = true,
                         float backdrop_alpha_scale = 1.f);

const eh::icons::IconEntry* eh_app_drawer_resolve_catalog_icon(DockApp& app, const std::string& desktop_path,
                                                                const std::string& icon_key);
