#pragma once

#include "ux/Live_Wallpaper/app_types.hpp"

#include <cairo/cairo.h>

namespace eh::live_wallpaper {

// Ported gallery helpers for the standalone live wallpaper.
// These were adapted from the main settings wallpaper tab logic but now live
// entirely inside the live wallpaper's codebase and operate on LiveWallpaperConfig +
// the live-specific AppState fields (no ::Settings, no ux/settings/* dependencies).

struct LiveWallpaperVerticalMetrics {
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

struct LiveWallpaperTabLayout {
  static constexpr int kNavW = 80;
  static constexpr int kNavH = 32;

  int galleryTop = 0;
  int gap = 6;
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

// Function declarations are provided by the existing live_*/ensure_live* implementations
// in the rest of the live codebase. The .cpp here serves as the "ported inside" home
// for the types and will host the consolidated implementations in the next pass.
LiveWallpaperVerticalMetrics live_wallpaper_vertical_metrics(double contentX, double contentW, [[maybe_unused]] const AppState& app);
LiveWallpaperTabLayout live_wallpaper_tab_layout(const AppState& app, int cardInsetX, int contentW, int galleryGridTopY);

void live_wallpaper_destroy_thumbs(AppState& app);

} // namespace eh::live_wallpaper