#pragma once

#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/launchpad/search/launchpad_search.hpp"

#include <cairo/cairo.h>
#include <string>
#include <string_view>
#include <vector>

struct wl_surface;
struct wl_compositor;

struct LaunchpadLayout {
  double W = 0;
  double H = 0;
  double searchBarX = 0;
  double searchBarY = 0;
  double searchBarW = 0;
  double searchBarH = 44;
  double organizeBtnX = 0;
  double organizeBtnY = 0;
  double organizeBtnW = 0;
  double organizeBtnH = 0;
  double gridLeft = 0;
  double gridTop = 0;
  double gridW = 0;
  double gridBottom = 0;
  double cellW = 0;
  double cellH = 0;
  double iconSz = 0;
  double labelFontPx = 14.5;
  int gridCols = 15;
  int gridRows = 6;
  int gapPx = 0;
  int itemsPerPage = 90;
  int pageCount = 1;
};

struct LaunchpadPaintModel {
  int popup_w = 0;
  int popup_h = 0;
  int page = 0;
  int sel = -1;
  int hover_idx = -1;
  const std::vector<LaunchpadHit>* hits = nullptr;
  std::string_view search_query{};
  bool power_confirm_open = false;
  int power_confirm_idx = -1;
  double pointer_x = 0;
  double pointer_y = 0;

  const LaunchpadLayout* layout = nullptr;
  const std::vector<LaunchpadFolderDef>* launchpad_folders = nullptr;

  float page_slide_t = -1.f;
  int page_slide_from = 0;
  int page_slide_to = 0;

  // Folder-open overlay state
  bool folder_open = false;
  int folder_open_idx = -1;
  int folder_hover_child = -1;
  int folder_page = 0;
  const std::vector<LaunchpadHit>* folder_children = nullptr;

  // Drag state (for ghost rendering)
  bool drag_active = false;
  int drag_src_idx = -1;

  // Folder child drag state (for ghost rendering)
  bool folder_drag_active = false;
  int folder_drag_child_idx = -1;

  // Organize button hover state
  bool organize_hovered = false;

  // Folder rename editing state
  bool folder_editing = false;
  std::string folder_edit_buffer;
  bool folder_title_hovered = false;

  // Hover bounce animation scale (1.0 = normal)
  float hover_anim_scale = 1.f;
};

void eh_launchpad_clamp_page(LaunchpadPaintModel* m);

void eh_launchpad_ensure_sel_visible(LaunchpadPaintModel* m);

AppDrawerHitZone eh_launchpad_hit_zone(const LaunchpadPaintModel& m, wl_surface* body_surface, wl_surface* pointer_surface,
                                        double local_x, double local_y);

int eh_launchpad_pick_catalog_index(const LaunchpadPaintModel& m, wl_surface* body_surface, wl_surface* pointer_surface,
                                    double local_x, double local_y);

// Pick which folder child icon the pointer is over (-1 if none).
int eh_launchpad_pick_folder_child(const LaunchpadPaintModel& m, double local_x, double local_y);

// Returns true if the pointer is over the folder title area (for rename).
bool eh_launchpad_pick_folder_title(const LaunchpadPaintModel& m, double local_x, double local_y);

// Returns true if the pointer is within the folder card bounds.
bool eh_launchpad_pick_folder_card(const LaunchpadPaintModel& m, double local_x, double local_y);

void eh_launchpad_paint_body(DockApp& icon_host, cairo_t* cr, bool paint_backdrop, bool paint_content,
                             float backdrop_alpha_scale, const LaunchpadPaintModel& m);

void eh_launchpad_set_content_input_region(wl_compositor* compositor, wl_surface* content_surface,
                                           const LaunchpadPaintModel& m);

LaunchpadLayout compute_launchpad_layout(const LaunchpadPaintModel& m, double W, double H);

// Render a single grid page (icons + labels, no overlay highlights) to a cairo surface.
void eh_launchpad_render_page(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m,
                               const LaunchpadLayout& L, int page_index, float alpha_scale,
                               const eh::config::ChromePaintColors& mc);

// Paint only hover/selection overlays (highlight rect + scaled icon) for cells on the current page.
// Assumes the underlying grid icons are already rendered (e.g. from a cached surface).
void eh_launchpad_paint_grid_hover_overlays(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m,
                                             const LaunchpadLayout& L, double bs, double primR, double primG,
                                             double primB, [[maybe_unused]] double outR, [[maybe_unused]] double outG, [[maybe_unused]] double outB);

// Render the folder-open overlay card.
void eh_launchpad_paint_folder_overlay(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m, double bs,
                                       double primR, double primG, double primB,
                                       double outR, double outG, double outB, double us,
                                       const eh::config::ChromePaintColors& mc);
