#pragma once

#include "desktop_shell/Overview/overview_types.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


namespace eh::shell::overview {

void compute_overview_layout(OverviewLayout& layout, double w, double h, int nWorkspaces,
                             double uiScale, OverviewAxis axis = OverviewAxis::Vertical,
                             double cardScale = 0.5, double cardGap = 24,
                             double closeBtnSz = 34, double searchW = 420);

[[nodiscard]] OverviewCardRect overview_card_rect(const OverviewLayout& layout, int index,
                                                  double scrollPos);

[[nodiscard]] double overview_selected_index(const OverviewLayout& layout, double scrollPos,
                                             int nWorkspaces);

void paint_search_bar(cairo_t* cr, const OverviewColors& colors, const OverviewLayout& layout,
                      float hoverLift, float progress, const std::string& query = {});

// Bottom pill button: "Show all apps" (workspace view) or "Back"
// (launchpad view). Painted last so it stays on top; clicked via
// pick_apps_button_at in either mode.
void paint_apps_button(cairo_t* cr, const OverviewColors& colors, const OverviewLayout& layout,
                       float hoverLift, float progress);

[[nodiscard]] bool pick_apps_button_at(const OverviewLayout& layout, double mx, double my);

void paint_workspace_cards(cairo_t* cr, ShellCtx& ctx, const OverviewColors& colors,
                           const OverviewLayout& layout,
                           const std::vector<OverviewWorkspace>& workspaces,
                           double scrollPos, int selectedIndex,
                           int hoveredWs, int hoveredWinFlat, bool closeHovered,
                           const std::vector<float>& wsHoverLifts,
                           const std::vector<float>& winHoverLifts,
                           int draggedWs, int draggedWinFlat,
                           double ghostX, double ghostY,
                           int dropTargetWs,
                           float progress,
                           bool invalidateCards = false,
                           const WorkspaceCapture* wallpaper = nullptr);

int pick_workspace_at(const OverviewLayout& layout, double mx, double my,
                      const std::vector<OverviewWorkspace>& workspaces, double scrollPos);

int pick_window_at(const OverviewLayout& layout, const std::vector<OverviewWorkspace>& workspaces,
                   double mx, double my, int wsIndex, double scrollPos, int* outFlat = nullptr);

[[nodiscard]] OverviewCardRect
overview_tile_rect_for_flat(const OverviewLayout& layout,
                            const std::vector<OverviewWorkspace>& workspaces, int flatIdx,
                            double scrollPos, bool* outOk = nullptr);

bool pick_close_at(const OverviewLayout& layout, const std::vector<OverviewWorkspace>& workspaces,
                   double mx, double my, int wsIndex, double scrollPos, int* outFlat = nullptr);

void compute_app_grid_layout(AppGridLayout& grid, double w, double h, double searchY,
                             double uiScale, int nApps, double bottomReservePx = 0.0);
// Folder modal grid: fixed 4 columns centered box, up to 4 visible rows.
void compute_modal_grid(AppGridLayout& modal, double w, double h, double uiScale, int nApps);
void paint_app_grid(cairo_t* cr, ShellCtx& ctx, const OverviewColors& colors,
                    const OverviewLayout& layout,
                    const std::vector<GridItem>& items,
                    const std::vector<SpotlightHit>& apps,
                    const std::vector<AppFolder>& folders,
                    int hoveredIdx, float hoverLift, float progress,
                    double scrollPos = 0.0);
// Card rect of the folder modal box (title sits above it). Shared by paint
// and input so backdrop-vs-modal hit tests can never disagree.
[[nodiscard]] OverviewCardRect modal_card_rect(const OverviewLayout& layout);
void paint_folder_modal(cairo_t* cr, ShellCtx& ctx, const OverviewColors& colors,
                        const OverviewLayout& layout,
                        const std::vector<GridItem>& modalItems,
                        const std::vector<SpotlightHit>& apps,
                        const std::vector<AppFolder>& folders,
                        int hoveredIdx, float hoverLift, float progress,
                        double scrollPos, const std::string& title);
// Page dots under the main grid card; hidden when everything fits one page.
void paint_page_dots(cairo_t* cr, const OverviewColors& colors, const OverviewLayout& layout,
                     double scrollPos, float progress);
int pick_app_at(const AppGridLayout& grid, double mx, double my, int nApps,
                double scrollPos = 0.0);

void compute_quick_select_layout(QuickSelectLayout& qs, OverviewAxis axis, double w, double h,
                                 double cardW, double cardH, double uiScale, int nWorkspaces);

void paint_quick_select_strip(cairo_t* cr, ShellCtx& ctx, const OverviewColors& colors,
                              const QuickSelectLayout& qs, const OverviewLayout& layout,
                              const std::vector<OverviewWorkspace>& workspaces,
                              int selectedIndex, int hoveredWs, int hoveredQs,
                              float progress);

int pick_quick_select_at(const QuickSelectLayout& qs, const OverviewLayout& layout,
                         const std::vector<OverviewWorkspace>& workspaces,
                         double mx, double my);

// True when (mx,my) is over the strip's add-workspace slot.
bool pick_quick_select_add_at(const QuickSelectLayout& qs, double mx, double my);

void get_cache_stats(int& cardHits, int& cardMisses, int& winHits, int& winMisses,
                     int& stripHits, int& stripMisses);

void prune_stale_card_disk_cache(const std::vector<OverviewWorkspace>& workspaces,
                                 const OverviewColors& colors);

} // namespace eh::shell::overview
