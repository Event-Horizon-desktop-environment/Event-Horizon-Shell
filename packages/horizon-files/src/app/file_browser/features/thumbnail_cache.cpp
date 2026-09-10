#include "app/file_browser/features/thumbnail_cache.hpp"

#include <cairo/cairo.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace eh::file_browser {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kCacheVersion = "hf-thumb-v1-png";

static fs::path cache_root_dir() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0])
    return fs::path(xdg) / "horizon-files" / "thumbnails";
  if (const char* home = std::getenv("HOME"); home && home[0])
    return fs::path(home) / ".cache" / "horizon-files" / "thumbnails";
  return fs::path("/tmp") / "horizon-files" / "thumbnails";
}

static uint64_t fnv1a64(const std::string& text) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char ch : text) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

static std::string hex16(uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kDigits[value & 0xF];
    value >>= 4;
  }
  return out;
}

static std::string cache_path_for_source(const std::string& source_path, int max_px) {
  struct stat st{};
  if (stat(source_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return {};
  const std::string key =
      source_path + '\n' +
      std::to_string(static_cast<uintmax_t>(st.st_size)) + '\n' +
      std::to_string(st.st_mtime) + '\n' +
      std::to_string(max_px) + '\n' +
      std::string(kCacheVersion);
  return (cache_root_dir() / (hex16(fnv1a64(key)) + ".png")).string();
}

} // anonymous namespace

cairo_surface_t* load_cached_thumbnail(const std::string& source_path, int max_px) {
  std::string cache_path = cache_path_for_source(source_path, max_px);
  if (cache_path.empty()) return nullptr;
  if (access(cache_path.c_str(), R_OK) != 0) return nullptr;
  cairo_surface_t* s = cairo_image_surface_create_from_png(cache_path.c_str());
  if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return nullptr;
  }
  return s;
}

void save_thumbnail_cache(const std::string& source_path, int max_px, cairo_surface_t* surface) {
  if (!surface) return;
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return;

  std::string cache_path = cache_path_for_source(source_path, max_px);
  if (cache_path.empty()) return;

  fs::path dir = fs::path(cache_path).parent_path();
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return;

  cairo_surface_write_to_png(surface, cache_path.c_str());
}

} // namespace eh::file_browser
