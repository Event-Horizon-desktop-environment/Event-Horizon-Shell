#pragma once

#include <cairo/cairo.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"
#include "wl/input/clipboard.hpp"
#include "wl/surface/surface_extensions.hpp"

// NOTE: Fully standalone now. No ux/settings/* or monolithic ::Settings.
// LiveWallpaperConfig holds exactly what this app needs (wallpaper selection + live UI prefs).
// Gallery/hero/thumb logic is ported inside this codebase (see gallery helpers + app.cpp).

struct wl_callback;

namespace eh::live_wallpaper {

// Local config for the standalone live wallpaper app (fully separated).
// Ported the wallpaper selection fields + live UI prefs out of the main monolithic ::Settings.
struct LiveWallpaperConfig {
  bool enabled = false;
  int mode = 0;
  std::string image{};
  std::string folder{};
  std::string playerCmd{};
  int folderPickerMode = 0;

  // Live-only UI + gallery prefs
  int uiOpacityPct = 75;
  int galleryScalePct = 100;
  int gallerySortMode = 2;
  int galleryPage = 0;
  int galleryThumbRadiusPx = 8;
  int galleryColumns = 5;
};

struct AppState {
  AppState();
  ~AppState();

  eh::wayland::WaylandConnection wl{};
  eh::wayland::WaylandSeat seat{};
  eh::wayland::ClipboardService clipboard{};

  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  eh::wayland::SurfaceExtensions surfaceExt{};

  int width = 960;
  int height = 680;
  bool running = true;
  bool pendingRedraw = false;

  double pointerX = 0;
  double pointerY = 0;
  bool pointerLeftDown = false;

  wl_shm* shm = nullptr;
  std::array<eh::wayland::ShmBuffer, 2> buf{};

  eh::config::ChromePaintColors drawChrome{};
  bool drawChromeMatugen = false;

  LiveWallpaperConfig config{};

  // Live wallpaper gallery state.
  int liveWallpaperUiSubTab = 0;
  bool liveWallpaperModeDropdownOpen = false;
  int liveWallpaperModeDropdownHoverRow = -1;

  std::vector<std::string> liveWallpaperGalleryPaths{};
  std::unordered_map<std::string, time_t> liveWallpaperGalleryMtimeCache{};
  int liveWallpaperGallerySortMode = 2;
  int liveWallpaperGallerySortModeApplied = -1;
  int liveWallpaperGalleryPage = 0;
  bool liveWallpaperGalleryPrecached = false;
  int liveWallpaperGalleryColumns = 5;
  int liveWallpaperGalleryRows = 5;
  int liveWallpaperGalleryScalePct = 100;
  int liveWallpaperGalleryThumbRadiusPx = 8;
  int liveWallpaperUiOpacityPct = 75;

  std::string liveWallpaperGalleryFolderSynced{};
  time_t liveWallpaperGalleryFolderMtime = 0;
  std::chrono::steady_clock::time_point liveWallpaperGalleryLastStatCheck{};

  int liveWallpaperScrollPx = 0;

  cairo_surface_t* liveWallpaperHeroSurf = nullptr;
  std::string liveWallpaperHeroPath{};
  std::string liveWallpaperHeroRequest{};

  std::unordered_map<std::string, cairo_surface_t*> liveWallpaperThumbs{};
  std::unordered_map<std::string, cairo_surface_t*> liveWallpaperBlurredThumbs{};
  std::list<std::string> liveWallpaperThumbLru{};
  std::list<std::string> liveWallpaperPendingVideoThumbs{};



  int liveWallpaperHoveredSlot = -1;
  std::string liveWallpaperHoveredPath{};
  float liveWallpaperHoverScale = 1.0f;
  eh::shell::AnimationManager liveWallpaperHoverAnim{};

  // Video hover preview (play in place).
  std::vector<cairo_surface_t*> liveWallpaperHoverVideoFrames{};
  int liveWallpaperHoverVideoFrameIndex = 0;
  std::string liveWallpaperHoverVideoPath{};
  std::chrono::steady_clock::time_point liveWallpaperHoverVideoLastFrameTime{};
  std::chrono::milliseconds liveWallpaperHoverVideoFrameDuration{33};

  bool wallpaperFolderPickerOpen = false;

  // 18+ adult marking.
  std::unordered_map<std::string, bool> liveWallpaperAdultFlags{};
  bool liveWallpaperContextMenuOpen = false;
  int liveWallpaperContextMenuX = 0;
  int liveWallpaperContextMenuY = 0;
  std::string liveWallpaperContextMenuPath{};

  // Frame callback.
  wl_callback* surfaceFrameCb = nullptr;

  // Paint tracking.
  int last_paint_w = -1;
  int last_paint_h = -1;
};

} // namespace eh::live_wallpaper
