#pragma once

#include <cairo/cairo.h>

struct App;

static constexpr int kWpThumbGap = 6;
static constexpr int kWpThumbLabelH = 0;

struct WallpaperVerticalMetrics {
  double heroX = 0;
  int heroY = 0;
  double heroW = 0;
  int ctrlY = 0;
  int ctrlW = 0;
  int modeRowY = 0;
  int subTabY = 0;
  int galleryHeaderY = 0;
  int sortPillsY = 0;
  int galleryGridTop = 0;
};

struct WallpaperTabLayout {
  static constexpr int kNavW = 80;
  static constexpr int kNavH = 32;

  int galleryTop = 0;
  int gap = kWpThumbGap;
  int cols = 5;
  int thumb = 0;
  int thumbH = 0;
  int rows = 5;
  int perPage = 25;
  bool showNav = false;
  int navY = 0;
  int galleryBottom = 0;
  int prevX = 0;
  int nextX = 0;
  int navW = 80;
  int navH = 32;
};

void paint_wallpaper_tab(App& app, cairo_t* cr, int contentX, int contentW,
                         double cardX, double cardW, double glassOv,
                         double dockMatA, double paintPointerYOffset);

bool settings_wallpaper_consume_pointer_down(App& app, int contentX, int contentW);

void settings_clamp_wallpaper_scroll_px(App& app);

WallpaperVerticalMetrics wallpaper_vertical_metrics(double contentX, double contentW);

void wallpaper_mode_combo_geom(int contentX, int contentW, int modeRowY,
                              int* outX, int* outY, int* outW, int* outH);

WallpaperTabLayout wallpaper_tab_layout(const App& app, int cardInsetX, int contentW, int galleryGridTopY);

void wallpaper_clamp_page(App& app, int perPage);

void ensure_wallpaper_gallery(App& app);

bool wallpaper_thumb_needs_followup_frame(App& app);

void wallpaper_destroy_thumbs(App& app);

void wallpaper_invalidate_hero(App& app);

void wallpaper_cycle_selection(App& app, int delta);
