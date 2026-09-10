#pragma once

#include "desktop_shell/common/bench/debug_profile.hpp"

#include <cairo/cairo.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace eh::wallpaper {

[[nodiscard]] inline bool wallpaper_thumbnail_pipeline_debug_enabled() noexcept {
  return eh::debug_profile::env_bool("EH_WALLPAPER_THUMB_DEBUG") || eh::debug_profile::env_bool("EH_SETTINGS_DEBUG");
}

[[nodiscard]] inline std::mutex& wallpaper_thumbnail_log_mutex() noexcept {
  static std::mutex m;
  return m;
}

[[nodiscard]] bool filename_ends_with_raster_ext(const std::string& name);

[[nodiscard]] std::vector<std::string> scan_image_files(const std::string& dir_path);

[[nodiscard]] std::string normalize_wallpaper_path(const std::string& path);

struct ThumbnailDecoded {
  std::string path;

  int max_px = 96;

  bool from_disk_cache_hit = false;

  bool failed = false;
  std::vector<std::uint8_t> rgba;
  int width = 0;
  int height = 0;
};

[[nodiscard]] ThumbnailDecoded decode_thumbnail_to_rgba(const std::string& path, int max_px);

[[nodiscard]] cairo_surface_t* thumbnail_decoded_to_surface(const ThumbnailDecoded& d);

void write_thumbnail_disk_cache_maybe(const ThumbnailDecoded& decoded);

[[nodiscard]] cairo_surface_t* load_thumbnail(const std::string& path, int max_px);

}
