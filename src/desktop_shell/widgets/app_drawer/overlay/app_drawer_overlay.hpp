#pragma once

#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eh::shell::dock::app_drawer {

struct AppDrawerChromeColors {
  double dockFillR = 0.12, dockFillG = 0.12, dockFillB = 0.14;
  double drawerDimR = 0.11, drawerDimG = 0.11, drawerDimB = 0.13;
  double accentR = 0.90, accentG = 0.90, accentB = 0.90;
  double outlineR = 0.55, outlineG = 0.60, outlineB = 0.62;
};

struct AppDrawerState {
  int popupW = 850;
  int popupH = 680;

  std::vector<std::string> pinnedApps{};
  std::vector<std::string> startMenuPinnedApps{};

  std::string query{};
  std::string prevQuery{};
  std::vector<size_t> prevCatIndices{};
  std::vector<SpotlightHit> pinHits{};
  std::vector<SpotlightHit> hits{};
  int sel = -1;
  double scrollPx = 0.0;
  double scrollPxCurrent = 0.0;
  double scrollPxAnimFrom = 0.0;
  uint64_t scrollAnimStartNs = 0;
  bool searchFieldFocused = true;

  double pointerX = 0.0;
  double pointerY = 0.0;
  int hoverListRow = -1;
  bool rowCtxOpen = false;
  int rowCtxAnchorRow = -1;
  double rowCtxMenuX = 0;
  double rowCtxMenuY = 0;
  int rowCtxHoverItem = -1;
  bool rowCtxPinnedDock = false;
  bool rowCtxPinnedStart = false;
  bool rowCtxPinnedDrawer = false;
  int rowCtxItemCount = 3;
  int hoverDrawerPinIdx = -1;
  int hoverPowerIdx = -1;
  bool pinCtxOpen = false;
  int pinCtxAnchorIdx = -1;
  double pinCtxMenuX = 0;
  double pinCtxMenuY = 0;
  int pinCtxHoverItem = -1;
  bool pinCtxPinnedDock = false;

  bool smenuMode = false;

  std::vector<std::string> categories{};
  std::vector<double> categoryWidths{};
  int selectedCategory = -1;
  int hoverCategoryIdx = -1;

  int viewMode = 0;

  // Content cache for the scrollable app list
  cairo_surface_t* appListCache = nullptr;
  int appListCacheW = 0;
  int appListCacheH = 0;
  int appListCacheViewMode = -1;
  uint64_t appListContentGen = 0;
  uint64_t appListCacheContentGen = 0;
};

int app_drawer_popup_width();
int app_drawer_popup_height();

void app_drawer_refresh_hits(AppDrawerState& s);
void app_drawer_clamp_scroll(AppDrawerState& s);
void app_drawer_ensure_sel_visible(AppDrawerState& s);
void app_drawer_scroll_pixels(AppDrawerState& s, double deltaPx);
void app_drawer_scroll_tick(AppDrawerState& s);

enum class AppDrawerHitZone : uint8_t {
  None = 0,
  SearchField,
  CategoryTab,
  AppListRow,
  PinnedApp,
  PowerButton,
  NightlightButton,
};

void app_drawer_update_categories(AppDrawerState& s);
int app_drawer_pick_category_tab(const AppDrawerState& s, double localX, double localY);

AppDrawerHitZone app_drawer_hit_zone(const AppDrawerState& s, double localX, double localY);
int app_drawer_pick_row_index(const AppDrawerState& s, double localX, double localY);
int app_drawer_pick_pinned_index(const AppDrawerState& s, double localX, double localY);
int app_drawer_pick_power_index(const AppDrawerState& s, double localX, double localY);

void set_nightlight_active(bool active);
bool get_nightlight_active();
void set_nightlight_toggle_fn(void (*fn)());

void app_drawer_paint(AppDrawerState& s,
                      cairo_t* cr,
                      eh::icons::IconCache& icons,
                      bool paintBackdrop = true,
                      bool paintContent = true,
                      float backdrop_alpha_scale = 1.f,
                      const AppDrawerChromeColors* chrome = nullptr);

constexpr int kAppDrawerRowContextItemMax = 3;
void app_drawer_row_context_menu_layout(double menuAnchorX, double menuAnchorY, double popupW, double popupH,
                                        int itemCount, double* outMenuX, double* outMenuY, double* outMenuW, double* outMenuH);
[[nodiscard]] int app_drawer_row_context_menu_pick(double lx, double ly, double menuX, double menuY, int itemCount);
void app_drawer_row_context_menu_paint(cairo_t* cr, double menuX, double menuY, const char* const* lines, int itemCount,
                                       int hoverItem, const AppDrawerChromeColors& col);
}

namespace eh::appdrawer {
using namespace eh::shell::dock::app_drawer;
}

