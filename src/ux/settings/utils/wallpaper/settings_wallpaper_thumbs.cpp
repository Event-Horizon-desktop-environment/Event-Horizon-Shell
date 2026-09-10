#include <algorithm>
#include <cairo/cairo.h>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <string>


#include "ux/settings/utils/wallpaper/settings_wallpaper_thumbs.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"

#include "wallpaper/wallpaper_log.hpp"

static bool eh_wallpaper_thumb_debug() {
  WP_SCOPE();
  return eh::wallpaper::wallpaper_thumbnail_pipeline_debug_enabled();
}

static std::string wallpaper_basename_for_log(const std::string& path) {
  WP_SCOPE();
  const auto s = path.rfind('/');
  return (s == std::string::npos) ? path : path.substr(s + 1);
}

static constexpr std::size_t kWallpaperThumbMapMax = 32;

static void wallpaper_thumb_lru_erase_path(App& app, const std::string& path) {
  WP_SCOPE();
  auto lit = std::find(app.wallpaperThumbLru.begin(), app.wallpaperThumbLru.end(), path);
  if (lit != app.wallpaperThumbLru.end()) app.wallpaperThumbLru.erase(lit);
}

static void wallpaper_thumb_store(App& app, const std::string& path, cairo_surface_t* surf) {
  WP_SCOPE();
    
  wallpaper_thumb_lru_erase_path(app, path);
  if (const auto it = app.wallpaperThumbs.find(path); it != app.wallpaperThumbs.end()) {
    if (it->second) cairo_surface_destroy(it->second);
    app.wallpaperThumbs.erase(it);
  }
  while (app.wallpaperThumbs.size() >= kWallpaperThumbMapMax && !app.wallpaperThumbLru.empty()) {
    const std::string victim = std::move(app.wallpaperThumbLru.front());
    app.wallpaperThumbLru.pop_front();
    if (const auto vit = app.wallpaperThumbs.find(victim); vit != app.wallpaperThumbs.end()) {
      if (vit->second) cairo_surface_destroy(vit->second);
      app.wallpaperThumbs.erase(vit);
    }
  }
  app.wallpaperThumbs.emplace(path, surf);
  app.wallpaperThumbLru.push_back(path);
}

int wallpaper_merge_async_thumbnails(App& app) {
  WP_SCOPE();
  WP_LOG("start: thumbs=%zu pending=%d", app.wallpaperThumbs.size(), app.wallpaperGalleryPrecached);
  int merged = 0;
  for (auto decoded : eh::wallpaper::WallpaperThumbnailService::instance().take_completed()) {
    if (eh_wallpaper_thumb_debug()) {
      std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] merge: incoming file=\"" << wallpaper_basename_for_log(decoded.path)
                << "\" decode_failed=" << (decoded.failed ? 1 : 0)
                << " disk_cache_hit=" << (decoded.from_disk_cache_hit ? 1 : 0) << " rgba_bytes=" << decoded.rgba.size()
                << " dim=" << decoded.width << "x" << decoded.height << " max_px=" << decoded.max_px << "\n";
    }

    if (!app.wallpaperHeroRequest.empty() && decoded.path == app.wallpaperHeroRequest) {
      cairo_surface_t* surf = eh::wallpaper::thumbnail_decoded_to_surface(decoded);
      if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        if (app.wallpaperHeroSurf) cairo_surface_destroy(app.wallpaperHeroSurf);
        app.wallpaperHeroSurf = surf;
        app.wallpaperHeroPath = decoded.path;
      } else {
        cairo_surface_destroy(surf);
      }
      app.wallpaperHeroRequest.clear();
      ++merged;
      continue;
    }

    cairo_surface_t* surf = eh::wallpaper::thumbnail_decoded_to_surface(decoded);
    if (!surf) {
      if (eh_wallpaper_thumb_debug()) {
        std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] merge: surface build returned null path_len=" << decoded.path.size() << "\n";
      }
      continue;
    }
    if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
      cairo_surface_flush(surf);
    }
    if (eh_wallpaper_thumb_debug()) {
      const cairo_status_t st = cairo_surface_status(surf);
      const int iw = cairo_image_surface_get_width(surf);
      const int ih = cairo_image_surface_get_height(surf);
      std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] merge: cairo_surface status="
                << (st == CAIRO_STATUS_SUCCESS ? "OK" : cairo_status_to_string(st)) << " image_wh=" << iw << "x" << ih
                << " file=\"" << wallpaper_basename_for_log(decoded.path) << "\"\n";
    }
    ++merged;
    wallpaper_thumb_store(app, decoded.path, surf);
  }
  WP_LOG("end: merged=%d thumbs=%zu", merged, app.wallpaperThumbs.size());
  if (eh_wallpaper_thumb_debug() && merged > 0) {
    std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] merged " << merged << " surface(s); thumb map size=" << app.wallpaperThumbs.size()
              << "\n";
  }
  return merged;
}
