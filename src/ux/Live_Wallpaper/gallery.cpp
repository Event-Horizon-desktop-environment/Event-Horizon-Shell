#include "ux/Live_Wallpaper/gallery.hpp"
#include "ux/Live_Wallpaper/app.hpp"  // for some helpers like wallpaper_file_basename if needed

#include <algorithm>
#include <cmath>
#include <sys/stat.h>

#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
#include "wallpaper/wallpaper_log.hpp"

namespace eh::live_wallpaper {

static std::string wallpaper_folder_key(const std::string& folder) {
  // Simple key for mtime tracking (can be the normalized path)
  return eh::wallpaper::normalize_wallpaper_path(folder);
}

LiveWallpaperVerticalMetrics live_wallpaper_vertical_metrics(double contentX, double contentW, [[maybe_unused]] const AppState& app) {
  LiveWallpaperVerticalMetrics m{};
  m.subTabY = 96; // approximate, matches previous usage in live code
  m.heroX = contentX + 8.0;
  m.heroY = m.subTabY + 52;
  m.heroW = (contentW - 16.0) * 8.0 / 12.0;
  m.heroW = std::max(m.heroW, 400.0);
  m.ctrlW = static_cast<int>(contentW - 16.0 - m.heroW - 12.0);
  m.ctrlY = m.heroY;
  m.modeRowY = m.ctrlY + 16;
  m.galleryHeaderY = m.heroY + 220 + 12; // kHeroH approx
  m.sortPillsY = m.galleryHeaderY + 20;
  m.galleryGridTop = m.sortPillsY + 28 + 36;
  return m;
}

// Declared here but implemented in app.cpp (the gallery layout is shared with
// the integrated settings tab, so keeping a single definition avoids handling
// the duplicate during the separation).
LiveWallpaperTabLayout live_wallpaper_tab_layout(const AppState& app, int cardInsetX, int contentW, int galleryGridTopY);

void live_wallpaper_clamp_page(AppState& app, int perPage) {
  if (perPage <= 0) return;
  const int maxPage = std::max(0, static_cast<int>((app.liveWallpaperGalleryPaths.size() + perPage - 1) / perPage) - 1);
  if (app.liveWallpaperGalleryPage > maxPage) app.liveWallpaperGalleryPage = maxPage;
  if (app.liveWallpaperGalleryPage < 0) app.liveWallpaperGalleryPage = 0;
}

void live_ensure_wallpaper_gallery(AppState& app) {
  if (app.config.folder.empty()) {
    if (!app.liveWallpaperGalleryPaths.empty()) {
      eh::wallpaper::WallpaperThumbnailService::instance().release_all();
      live_wallpaper_destroy_thumbs(app);
      app.liveWallpaperGalleryPaths.clear();
      app.liveWallpaperGalleryMtimeCache.clear();
      app.liveWallpaperGalleryPage = 0;
    }
    app.liveWallpaperGalleryFolderSynced.clear();
    app.liveWallpaperGalleryFolderMtime = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    // valid flag lives in the main settings App for the integrated tab; standalone uses synced/mtime
    return;
  }

  const std::string key = wallpaper_folder_key(app.config.folder);
  const auto now = std::chrono::steady_clock::now();
  constexpr auto kStatDebounce = std::chrono::milliseconds(500);
  bool needsRescan = false;

  if (now - app.liveWallpaperGalleryLastStatCheck >= kStatDebounce) {
    app.liveWallpaperGalleryLastStatCheck = now;
    struct stat st {};
    const time_t curMtime = (::stat(key.c_str(), &st) == 0) ? st.st_mtime : 0;
    if (app.liveWallpaperGalleryFolderSynced != key || app.liveWallpaperGalleryFolderMtime != curMtime) {
      needsRescan = true;
      app.liveWallpaperGalleryFolderSynced = key;
      app.liveWallpaperGalleryFolderMtime = curMtime;
    }
  }

  if (needsRescan || app.liveWallpaperGalleryPaths.empty()) {
    eh::wallpaper::WallpaperThumbnailService::instance().release_all();
    live_wallpaper_destroy_thumbs(app);
    app.liveWallpaperGalleryPaths = eh::wallpaper::scan_image_files(key.empty() ? app.config.folder : key);
    WP_LOG("live rescan: found %zu files", app.liveWallpaperGalleryPaths.size());
    app.liveWallpaperGalleryMtimeCache.clear();
    app.liveWallpaperGalleryPage = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    app.liveWallpaperGalleryPrecached = false;
  }

  if (app.liveWallpaperGallerySortMode != app.liveWallpaperGallerySortModeApplied) {
    // Record the applied sort mode so the next scan skips re-sorting.
    app.liveWallpaperGallerySortModeApplied = app.liveWallpaperGallerySortMode;
  }

  if (!app.liveWallpaperGalleryPrecached && !app.liveWallpaperGalleryPaths.empty()) {
    app.liveWallpaperGalleryPrecached = true;
    for (const auto& fp : app.liveWallpaperGalleryPaths) {
      eh::wallpaper::WallpaperThumbnailService::instance().request(fp, 512);
    }
  }
}

bool live_wallpaper_thumb_needs_followup_frame(AppState& app) {
  if (app.liveWallpaperUiSubTab != 0) return false;
  live_ensure_wallpaper_gallery(app);
  if (app.liveWallpaperGalleryPaths.empty()) return false;

  const int contentX = 16; // approximate for standalone
  const int contentW = app.width - 32;
  const int cardInsetXi = contentX + 8;
  LiveWallpaperTabLayout wl = live_wallpaper_tab_layout(app, cardInsetXi, contentW, 0);
  live_wallpaper_clamp_page(app, wl.perPage);

  const size_t gallOff = static_cast<size_t>(std::max(0, app.liveWallpaperGalleryPage)) * static_cast<size_t>(wl.perPage);
  const size_t nTotal = app.liveWallpaperGalleryPaths.size();
  const size_t nSlots = (nTotal <= gallOff) ? 0 : std::min(static_cast<size_t>(wl.perPage), nTotal - gallOff);

  for (size_t ii = 0; ii < nSlots; ++ii) {
    const std::string& fp = app.liveWallpaperGalleryPaths[gallOff + ii];
    if (app.liveWallpaperThumbs.find(fp) == app.liveWallpaperThumbs.end()) return true;
  }
  return eh::wallpaper::WallpaperThumbnailService::instance().has_pending_completed();
}

void live_wallpaper_destroy_thumbs(AppState& app) {
  for (auto& [_, s] : app.liveWallpaperThumbs) {
    if (s) cairo_surface_destroy(s);
  }
  app.liveWallpaperThumbs.clear();
  for (auto& [_, s] : app.liveWallpaperBlurredThumbs) {
    if (s) cairo_surface_destroy(s);
  }
  app.liveWallpaperBlurredThumbs.clear();
  app.liveWallpaperThumbLru.clear();
}

// Forward declarations of helpers whose bodies live in app.cpp; declared here
// so the gallery code can call them without duplicating the definitions.
void live_wallpaper_invalidate_hero(AppState& app);
void live_wallpaper_ensure_hero_surface(AppState& app);
void live_wallpaper_cycle_selection(AppState& app, int delta);

} // namespace eh::live_wallpaper