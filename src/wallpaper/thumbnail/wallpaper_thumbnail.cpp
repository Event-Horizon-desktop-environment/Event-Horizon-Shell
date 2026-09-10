#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"

#include "wallpaper/raster/wallpaper_raster_decode.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include <webp/encode.h>

#ifdef EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef EH_HAVE_LIBJPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#include "wallpaper/wallpaper_log.hpp"

namespace eh::wallpaper {

constexpr std::string_view kCacheVersion = "eh-wallpaper-thumb-v3-webp";

constexpr std::string_view kLegacyCacheVersion = "eh-wallpaper-thumb-v2";
constexpr float kDiskThumbWebpQuality = 82.0f;

constexpr std::int64_t kDefaultMaxThumbSourcePixels = 40LL * 1024 * 1024;
[[nodiscard]] static std::int64_t thumb_max_source_pixels() noexcept {
  WP_SCOPE();
    
  static std::int64_t cached = -1;
  if (cached >= 0) return cached;
  const char* e = std::getenv("EH_WALLPAPER_THUMB_MAX_PIXELS");
  if (e == nullptr || e[0] == '\0') {
    cached = kDefaultMaxThumbSourcePixels;
    return cached;
  }
  char* end = nullptr;
  errno = 0;
  const long long v = std::strtoll(e, &end, 10);
  if (errno != 0 || end == e) {
    cached = kDefaultMaxThumbSourcePixels;
    return cached;
  }
  long long clamped = v;
  constexpr long long kMinPx = 4LL * 1024 * 1024;
  constexpr long long kMaxPx = 120LL * 1024 * 1024;
  if (clamped < kMinPx) clamped = kMinPx;
  if (clamped > kMaxPx) clamped = kMaxPx;
  cached = static_cast<std::int64_t>(clamped);
  return cached;
}



static bool ends_ci(const std::string& path, std::string_view suf) {
  WP_SCOPE();
    
  if (path.size() < suf.size()) return false;
  for (size_t i = 0; i < suf.size(); ++i) {
    const char a = path[path.size() - suf.size() + i];
    const char b = suf[i];
    if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) return false;
  }
  return true;
}

static std::string basename_only(const std::string& path) {
  WP_SCOPE();
    
  const size_t s = path.rfind('/');
  return (s == std::string::npos) ? path : path.substr(s + 1);
}

bool filename_ends_with_raster_ext(const std::string& name) {
  WP_SCOPE();
    
  return ends_ci(name, ".png") || ends_ci(name, ".jpg") || ends_ci(name, ".jpeg") || ends_ci(name, ".webp") ||
         ends_ci(name, ".bmp") || ends_ci(name, ".gif") || ends_ci(name, ".svg");
}


[[nodiscard]] static bool thumb_source_pixels_over_cap(int w, int h) noexcept {
  WP_SCOPE();
  if (w <= 0 || h <= 0) return true;
  const auto p = static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h);
  return p > thumb_max_source_pixels();
}

static bool read_whole_file_bytes(const std::string& path, std::vector<std::uint8_t>* out) {
  WP_SCOPE();
    
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] read: fopen failed \"" << basename_only(path) << "\" errno=" << errno << " ("
                << std::strerror(errno) << ")\n";
    }
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  constexpr long kMaxBytes = 256L << 20;
  if (sz <= 0 || sz > kMaxBytes) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] read: bad size sz=" << sz << " file=\"" << basename_only(path) << "\"\n";
    }
    fclose(f);
    return false;
  }
  out->resize(static_cast<size_t>(sz));
  if (fread(out->data(), 1, out->size(), f) != out->size()) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] read: fread short file=\"" << basename_only(path) << "\"\n";
    }
    fclose(f);
    return false;
  }
  fclose(f);
  return true;
}

static std::filesystem::path cache_root_dir() {
  WP_SCOPE();
    
  namespace fs = std::filesystem;
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return fs::path(xdg) / "event-horizon" / "wallpaper-thumbnails";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return fs::path(home) / ".cache" / "event-horizon" / "wallpaper-thumbnails";
  }
  return fs::path("/tmp") / "event-horizon" / "wallpaper-thumbnails";
}

static std::uint64_t fnv1a64(std::string_view text) {
  WP_SCOPE();
    
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char ch : text) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

static std::string hex16(std::uint64_t value) {
  WP_SCOPE();
    
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kDigits[value & 0xF];
    value >>= 4;
  }
  return out;
}

static bool rgba_disk_cache_mostly_transparent(const unsigned char* d, int w, int h) {
  WP_SCOPE();
    
  if (!d || w <= 0 || h <= 0) return true;
  const int xs[] = {0, w - 1, 0, w - 1, w / 2};
  const int ys[] = {0, 0, h - 1, h - 1, h / 2};
  int low_alpha = 0;
  for (int i = 0; i < 5; ++i) {
    const int x = std::clamp(xs[i], 0, w - 1);
    const int y = std::clamp(ys[i], 0, h - 1);
    const unsigned char a = d[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u + 3];
    if (a < 24) ++low_alpha;
  }
  return low_alpha >= 4;
}

static std::optional<std::filesystem::path> cache_webp_path_for_source(const std::string& source_path, int max_px) {
  WP_SCOPE();
    
  namespace fs = std::filesystem;
  struct stat st {};
  if (stat(source_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return std::nullopt;
  const auto size = static_cast<std::uintmax_t>(st.st_size);
  const auto mtime = st.st_mtime;
  const std::string key =
      source_path + '\n' + std::to_string(size) + '\n' + std::to_string(mtime) + '\n' + std::to_string(max_px) +
      '\n' + std::string(kCacheVersion);
  return cache_root_dir() / (hex16(fnv1a64(key)) + ".webp");
}

static std::optional<std::filesystem::path> cache_legacy_png_path_for_source(const std::string& source_path,
                                                                             int max_px) {
  WP_SCOPE();
    
  namespace fs = std::filesystem;
  struct stat st {};
  if (stat(source_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return std::nullopt;
  const auto size = static_cast<std::uintmax_t>(st.st_size);
  const auto mtime = st.st_mtime;
  const std::string key =
      source_path + '\n' + std::to_string(size) + '\n' + std::to_string(mtime) + '\n' + std::to_string(max_px) +
      '\n' + std::string(kLegacyCacheVersion);
  return cache_root_dir() / (hex16(fnv1a64(key)) + ".png");
}

static cairo_surface_t* placeholder_thumb_msg(int max_px, const std::string& line1, const std::string& line2) {
  WP_SCOPE();
    
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, max_px, max_px);
  if (!s) return nullptr;
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgba(cr, 0.1, 0.12, 0.13, 0.95);
  cairo_paint(cr);
  cairo_set_source_rgba(cr, 0.6, 0.65, 0.67, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, max_px <= 72 ? 9.5 : 10.5);
  cairo_move_to(cr, 6.0, static_cast<double>(max_px) / 2.0 - (line2.empty() ? 0 : 8.0));
  cairo_show_text(cr, line1.c_str());
  if (!line2.empty()) {
    cairo_move_to(cr, 6.0, static_cast<double>(max_px) / 2.0 + 12.0);
    cairo_show_text(cr, line2.c_str());
  }
  cairo_destroy(cr);
  cairo_surface_mark_dirty_rectangle(s, 0, 0, max_px, max_px);
  cairo_surface_flush(s);
  return s;
}

static cairo_surface_t* failed_thumbnail_surface(const std::string& path, int max_px) {
  WP_SCOPE();
    
  std::string b = basename_only(path);
  if (static_cast<int>(b.size()) > 20) b = b.substr(0, 17) + "…";
  if (ends_ci(path, ".png")) return placeholder_thumb_msg(max_px, "PNG", b);
  if (ends_ci(path, ".webp")) return placeholder_thumb_msg(max_px, "WebP", b);
  if (ends_ci(path, ".jpg") || ends_ci(path, ".jpeg")) return placeholder_thumb_msg(max_px, "JPEG", b);
  if (ends_ci(path, ".bmp")) return placeholder_thumb_msg(max_px, "BMP", b);
  if (ends_ci(path, ".gif")) return placeholder_thumb_msg(max_px, "GIF", b);
  return placeholder_thumb_msg(max_px, "?", b);
}

static cairo_surface_t* surface_from_linear_rgba(const uint8_t* rgba, int w, int h) {
  WP_SCOPE();
    
  if (w <= 0 || h <= 0 || !rgba) return nullptr;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    return nullptr;
  }
  const int stride = cairo_image_surface_get_stride(surf);
  unsigned char* dst = cairo_image_surface_get_data(surf);
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<std::uint32_t*>(dst + y * stride);
    const uint8_t* src = rgba + static_cast<size_t>(y * w * 4);
    for (int x = 0; x < w; ++x) {
      const uint32_t r = src[0];
      const uint32_t g = src[1];
      const uint32_t b = src[2];
      const uint32_t a = src[3];
      const uint32_t pr = (r * a + 127) / 255;
      const uint32_t pg = (g * a + 127) / 255;
      const uint32_t pb = (b * a + 127) / 255;
      row[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
      src += 4;
    }
  }

  cairo_surface_mark_dirty_rectangle(surf, 0, 0, w, h);
  cairo_surface_flush(surf);
  return surf;
}

static void scale_rgba_thumb_nn_halve(std::vector<uint8_t>& rgba, int& w, int& h, int max_px) {
  WP_SCOPE();
    
  while (w > 0 && h > 0 && std::max(w, h) > max_px) {
    const int nw = std::max(1, (w + 1) / 2);
    const int nh = std::max(1, (h + 1) / 2);
    std::vector<uint8_t> out(static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4u);
    for (int y = 0; y < nh; ++y) {
      const int sy = std::min(y * 2, h - 1);
      for (int x = 0; x < nw; ++x) {
        const int sx = std::min(x * 2, w - 1);
        const size_t si = (static_cast<size_t>(sy) * static_cast<size_t>(w) + static_cast<size_t>(sx)) * 4u;
        const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(nw) + static_cast<size_t>(x)) * 4u;
        std::memcpy(&out[di], &rgba[si], 4);
      }
    }
    rgba.swap(out);
    w = nw;
    h = nh;
  }
}

static void scale_rgba_thumb(std::vector<uint8_t>& rgba, int& w, int& h, int max_px) {
  WP_SCOPE();
    
  if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) return;
  if (max_px < 24) max_px = 24;
  const int max_dim = std::max(w, h);
  if (max_dim <= max_px) return;
  const double sc = max_px / static_cast<double>(max_dim);
  const int nw = std::max(1, static_cast<int>(std::lround(w * sc)));
  const int nh = std::max(1, static_cast<int>(std::lround(h * sc)));
  std::vector<uint8_t> out(static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4u);
  if (stbir_resize_uint8_linear(rgba.data(), w, h, w * 4, out.data(), nw, nh, nw * 4, STBIR_RGBA) == nullptr) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] scale: stbir failed " << w << "x" << h << " -> " << nw << "x" << nh
                << " bytes=" << rgba.size() << " using halve fallback\n";
    }
    scale_rgba_thumb_nn_halve(rgba, w, h, max_px);
    return;
  }
  rgba.swap(out);
  w = nw;
  h = nh;
}

#ifdef EH_HAVE_LIBJPEG
namespace {

struct MyJpegErrorMgr {
  struct jpeg_error_mgr pub;
  jmp_buf jmp;
};

void jpeg_error_throw(j_common_ptr cinfo) {
  WP_SCOPE();
    
  MyJpegErrorMgr* err = reinterpret_cast<MyJpegErrorMgr*>(cinfo->err);
  longjmp(err->jmp, 1);
}

bool decompress_jpeg_fallback_rgba(const std::string& path, std::vector<uint8_t>& out_rgbax, int& w, int& h) {
  WP_SCOPE();
    
  FILE* in = fopen(path.c_str(), "rb");
  if (!in) return false;

  jpeg_decompress_struct cinfo{};
  MyJpegErrorMgr jerr{};
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpeg_error_throw;

  bool decomp_created = false;
  if (setjmp(jerr.jmp)) {
    if (decomp_created) jpeg_destroy_decompress(&cinfo);
    fclose(in);
    return false;
  }

  jpeg_create_decompress(&cinfo);
  decomp_created = true;

  jpeg_stdio_src(&cinfo, in);
  jpeg_read_header(&cinfo, TRUE);

  cinfo.out_color_space = JCS_RGB;

  jpeg_start_decompress(&cinfo);

  w = static_cast<int>(cinfo.output_width);
  h = static_cast<int>(cinfo.output_height);
  if (w <= 0 || h <= 0) {
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(in);
    return false;
  }

  const JDIMENSION rowStride = static_cast<JDIMENSION>(w * 3);
  std::vector<uint8_t> rgb(static_cast<size_t>(w * h * 3));
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW ptr = reinterpret_cast<JSAMPROW>(rgb.data() + static_cast<size_t>(cinfo.output_scanline) * rowStride);
    jpeg_read_scanlines(&cinfo, &ptr, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(in);

  out_rgbax.resize(static_cast<size_t>(w * h * 4));
  for (int i = 0; i < w * h; ++i) {
    out_rgbax[static_cast<size_t>(i * 4) + 0] = rgb[static_cast<size_t>(i * 3) + 0];
    out_rgbax[static_cast<size_t>(i * 4) + 1] = rgb[static_cast<size_t>(i * 3) + 1];
    out_rgbax[static_cast<size_t>(i * 4) + 2] = rgb[static_cast<size_t>(i * 3) + 2];
    out_rgbax[static_cast<size_t>(i * 4) + 3] = 255;
  }
  return true;
}

}
#endif

ThumbnailDecoded decode_thumbnail_to_rgba(const std::string& path, int max_px) {
  WP_SCOPE();
  WP_LOG("path=%s", path.c_str());
  auto _wpts = std::chrono::steady_clock::now();
  ThumbnailDecoded out{};
  out.path = path;
  out.max_px = std::max(24, max_px);

  if (auto cache = cache_webp_path_for_source(path, out.max_px)) {
    std::vector<std::uint8_t> cached_bytes;
    if (read_whole_file_bytes(cache->string(), &cached_bytes) && !cached_bytes.empty()) {
      std::string cache_err;
      if (auto raster = decode_raster_from_bytes(cached_bytes.data(), cached_bytes.size(), &cache_err)) {
        const int w = raster->width;
        const int h = raster->height;
        if (w > 0 && h > 0 && !raster->pixels.empty() &&
            raster->pixels.size() >= static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) {
           
          if (thumb_source_pixels_over_cap(w, h)) {
            if (wallpaper_thumbnail_pipeline_debug_enabled()) {
              std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
              std::cerr << "[wallpaper-thumb] decode: dropped oversize disk cache src=\"" << basename_only(path) << "\" "
                        << w << "x" << h << "\n";
            }
            std::error_code ec;
            std::filesystem::remove(*cache, ec);
          } else if (rgba_disk_cache_mostly_transparent(raster->pixels.data(), w, h)) {
            if (wallpaper_thumbnail_pipeline_debug_enabled()) {
              std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
              std::cerr << "[wallpaper-thumb] decode: dropped transparent/empty disk cache src=\"" << basename_only(path)
                        << "\" cache_webp " << w << "x" << h << "\n";
            }
            std::error_code ec;
            std::filesystem::remove(*cache, ec);
          } else {
            out.width = w;
            out.height = h;
            out.rgba = std::move(raster->pixels);
            out.from_disk_cache_hit = true;
            if (wallpaper_thumbnail_pipeline_debug_enabled()) {
              std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
              std::cerr << "[wallpaper-thumb] decode: disk cache hit src=\"" << basename_only(path) << "\" cache_webp "
                        << w << "x" << h << "\n";
            }
            return out;
          }
        }
      }
    }
    if (std::filesystem::exists(*cache)) {
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: dropped corrupt disk cache for src=\"" << basename_only(path) << "\"\n";
      }
      std::error_code ec;
      std::filesystem::remove(*cache, ec);
    }
  }

  if (auto legacy = cache_legacy_png_path_for_source(path, out.max_px)) {
    int lw = 0, lh = 0, lcomp = 0;
    if (stbi_info(legacy->string().c_str(), &lw, &lh, &lcomp) && thumb_source_pixels_over_cap(lw, lh)) {
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: skip oversize legacy PNG cache src=\"" << basename_only(path) << "\" "
                  << lw << "x" << lh << "\n";
      }
    } else {
      lw = 0;
      lh = 0;
      lcomp = 0;
      unsigned char* ld =
          stbi_load(legacy->string().c_str(), &lw, &lh, &lcomp, 4);
    if (ld && lw > 0 && lh > 0 && !thumb_source_pixels_over_cap(lw, lh)) {
      if (rgba_disk_cache_mostly_transparent(ld, lw, lh)) {
        stbi_image_free(ld);
        ld = nullptr;
        if (wallpaper_thumbnail_pipeline_debug_enabled()) {
          std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
          std::cerr << "[wallpaper-thumb] decode: dropped transparent legacy PNG cache src=\"" << basename_only(path)
                    << "\" " << lw << "x" << lh << "\n";
        }
        std::error_code ec;
        std::filesystem::remove(*legacy, ec);
      } else {
        out.width = lw;
        out.height = lh;
        out.rgba.assign(ld, ld + static_cast<size_t>(lw) * static_cast<size_t>(lh) * 4u);
        stbi_image_free(ld);
        out.from_disk_cache_hit = false;
        write_thumbnail_disk_cache_maybe(out);
        out.from_disk_cache_hit = true;
        if (wallpaper_thumbnail_pipeline_debug_enabled()) {
          std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
          std::cerr << "[wallpaper-thumb] decode: legacy PNG cache migrated to WebP src=\"" << basename_only(path)
                    << "\" " << lw << "x" << lh << "\n";
        }
        return out;
      }
    }
    if (ld) stbi_image_free(ld);
    if (std::filesystem::exists(*legacy)) {
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: dropped corrupt legacy PNG cache src=\"" << basename_only(path) << "\"\n";
      }
      std::error_code ec;
      std::filesystem::remove(*legacy, ec);
    }
    }
  }

  {
    int iw = 0, ih = 0, ic = 0;
    if (stbi_info(path.c_str(), &iw, &ih, &ic)) {
      if (thumb_source_pixels_over_cap(iw, ih)) {
        if (wallpaper_thumbnail_pipeline_debug_enabled()) {
          std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
          std::cerr << "[wallpaper-thumb] decode: rejected oversize source (stbi_info) file=\"" << basename_only(path)
                    << "\" " << iw << "x" << ih << "\n";
        }
        out.failed = true;
        return out;
      }
    }
  }

  // Fast JPEG decode using libjpeg IDCT scaling, avoiding a full-res decode.
#ifdef EH_HAVE_LIBJPEG
  if (ends_ci(path, ".jpg") || ends_ci(path, ".jpeg")) {
    FILE* in = fopen(path.c_str(), "rb");
    if (in) {
      jpeg_decompress_struct cinfo{};
      MyJpegErrorMgr jerr{};
      cinfo.err = jpeg_std_error(&jerr.pub);
      jerr.pub.error_exit = jpeg_error_throw;
      bool decomp_created = false;
      if (setjmp(jerr.jmp)) {
        if (decomp_created) jpeg_destroy_decompress(&cinfo);
        fclose(in);
      } else {
        jpeg_create_decompress(&cinfo);
        decomp_created = true;
        jpeg_stdio_src(&cinfo, in);
        jpeg_read_header(&cinfo, TRUE);

        // Scale so the largest dimension lands near the target size.
        int target = out.max_px;
        int max_dim = std::max(static_cast<int>(cinfo.image_width),
                                static_cast<int>(cinfo.image_height));
        int denom = 8;
        if (max_dim <= target * 2)       denom = 1;
        else if (max_dim <= target * 4)  denom = 2;
        else if (max_dim <= target * 8)  denom = 4;

        cinfo.scale_num = 1;
        cinfo.scale_denom = denom;
        cinfo.out_color_space = JCS_RGB;

        jpeg_start_decompress(&cinfo);

        int w = static_cast<int>(cinfo.output_width);
        int h = static_cast<int>(cinfo.output_height);
        if (w > 0 && h > 0) {
          std::vector<uint8_t> rgb(static_cast<size_t>(w * h * 3));
          while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW ptr = reinterpret_cast<JSAMPROW>(
                rgb.data() + cinfo.output_scanline * static_cast<size_t>(w) * 3);
            jpeg_read_scanlines(&cinfo, &ptr, 1);
          }

          jpeg_finish_decompress(&cinfo);
          jpeg_destroy_decompress(&cinfo);
          fclose(in);

          out.width = w;
          out.height = h;
          out.rgba.resize(static_cast<size_t>(w * h * 4));
          for (int i = 0; i < w * h; ++i) {
            out.rgba[static_cast<size_t>(i * 4) + 0] = rgb[static_cast<size_t>(i * 3) + 0];
            out.rgba[static_cast<size_t>(i * 4) + 1] = rgb[static_cast<size_t>(i * 3) + 1];
            out.rgba[static_cast<size_t>(i * 4) + 2] = rgb[static_cast<size_t>(i * 3) + 2];
            out.rgba[static_cast<size_t>(i * 4) + 3] = 255;
          }

          scale_rgba_thumb(out.rgba, out.width, out.height, out.max_px);
          if (wallpaper_thumbnail_pipeline_debug_enabled()) {
            std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
            std::cerr << "[wallpaper-thumb] decode: fast libjpeg IDCT scale=" << denom
                      << " src=\"" << basename_only(path) << "\" "
                      << w << "x" << h << "\n";
          }
          return out;
        }
        jpeg_destroy_decompress(&cinfo);
        fclose(in);
      }
    }
// If the fast JPEG path failed, fall through to the generic pipeline.
  }
#endif

// Fast stb_image path for the common formats (skips read_whole_file_bytes).
  if (ends_ci(path, ".png") || ends_ci(path, ".bmp") || ends_ci(path, ".gif") ||
      ends_ci(path, ".tga")) {
    int iw = 0, ih = 0, ic = 0;
    unsigned char* data = stbi_load(path.c_str(), &iw, &ih, &ic, 4);
    if (data) {
      if (thumb_source_pixels_over_cap(iw, ih)) {
        stbi_image_free(data);
        out.failed = true;
        return out;
      }
      out.width = iw;
      out.height = ih;
      out.rgba.assign(data, data + static_cast<size_t>(iw) * static_cast<size_t>(ih) * 4u);
      stbi_image_free(data);
      scale_rgba_thumb(out.rgba, out.width, out.height, out.max_px);
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: fast stb_image file=\"" << basename_only(path) << "\" "
                  << iw << "x" << ih << "\n";
      }
      return out;
    }
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] decode: fast stb_image failed file=\"" << basename_only(path) << "\"\n";
    }
  }

  std::vector<std::uint8_t> file_bytes;
  if (!read_whole_file_bytes(path, &file_bytes)) {
    out.failed = true;
    return out;
  }

  std::string primary_err;
  if (auto raster = decode_raster_from_bytes(file_bytes.data(), file_bytes.size(), &primary_err)) {
    if (thumb_source_pixels_over_cap(raster->width, raster->height)) {
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: rejected oversize raster file=\"" << basename_only(path) << "\" "
                  << raster->width << "x" << raster->height << "\n";
      }
      out.failed = true;
      return out;
    }
    out.width = raster->width;
    out.height = raster->height;
    out.rgba = std::move(raster->pixels);
    scale_rgba_thumb(out.rgba, out.width, out.height, out.max_px);
    return out;
  }
  if (wallpaper_thumbnail_pipeline_debug_enabled()) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] decode: wuffs/webp primary decode failed file=\"" << basename_only(path)
              << "\" msg=\"" << primary_err << "\"\n";
  }

#ifdef EH_HAVE_LIBJPEG
  if (ends_ci(path, ".jpg") || ends_ci(path, ".jpeg")) {
    if (decompress_jpeg_fallback_rgba(path, out.rgba, out.width, out.height)) {
      if (thumb_source_pixels_over_cap(out.width, out.height)) {
        out.rgba.clear();
        out.width = 0;
        out.height = 0;
        if (wallpaper_thumbnail_pipeline_debug_enabled()) {
          std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
          std::cerr << "[wallpaper-thumb] decode: rejected oversize libjpeg file=\"" << basename_only(path) << "\"\n";
        }
        out.failed = true;
        return out;
      } else {
        if (wallpaper_thumbnail_pipeline_debug_enabled()) {
          std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
          std::cerr << "[wallpaper-thumb] decode: libjpeg fallback ok file=\"" << basename_only(path) << "\" "
                    << out.width << "x" << out.height << "\n";
        }
        scale_rgba_thumb(out.rgba, out.width, out.height, out.max_px);
        return out;
      }
    }
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] decode: libjpeg fallback failed file=\"" << basename_only(path) << "\"\n";
    }
  }
#endif

  int w = 0, h = 0, comp = 0;
  unsigned char* d = stbi_load(path.c_str(), &w, &h, &comp, 4);
  if (d && w > 0 && h > 0) {
    if (thumb_source_pixels_over_cap(w, h)) {
      stbi_image_free(d);
      d = nullptr;
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] decode: rejected oversize stbi_load file=\"" << basename_only(path) << "\" " << w
                  << "x" << h << "\n";
      }
    } else {
      out.width = w;
      out.height = h;
      out.rgba.assign(d, d + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
      stbi_image_free(d);
      scale_rgba_thumb(out.rgba, out.width, out.height, out.max_px);
      return out;
    }
  }
  if (d) stbi_image_free(d);

#ifdef EH_HAVE_RSVG
  if (ends_ci(path, ".svg")) {
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
    if (handle) {
      double iw = 0.0, ih = 0.0;
      if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih)) {
        iw = 48.0; ih = 48.0;
      }
      if (iw > 0.0 && ih > 0.0) {
        double scale = 1.0;
        if (std::max(iw, ih) > static_cast<double>(out.max_px))
          scale = static_cast<double>(out.max_px) / std::max(iw, ih);
        int rw = std::max(1, static_cast<int>(std::lround(iw * scale)));
        int rh = std::max(1, static_cast<int>(std::lround(ih * scale)));
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
        cairo_t* cr = cairo_create(surf);
        RsvgRectangle viewport{0.0, 0.0, iw * scale, ih * scale};
        bool rendered = rsvg_handle_render_document(handle, cr, &viewport, &err);
        cairo_destroy(cr);
        if (rendered) {
          out.width = rw;
          out.height = rh;
          out.rgba.resize(static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4);
          auto* src_data = cairo_image_surface_get_data(surf);
          int src_stride = cairo_image_surface_get_stride(surf);
          for (int row = 0; row < rh; row++) {
            for (int col = 0; col < rw; col++) {
              size_t si = static_cast<size_t>(row) * static_cast<size_t>(src_stride) + static_cast<size_t>(col) * 4;
              size_t di = (static_cast<size_t>(row) * static_cast<size_t>(rw) + static_cast<size_t>(col)) * 4;
              uint8_t b = src_data[si], g = src_data[si + 1], r = src_data[si + 2], a = src_data[si + 3];
              if (a == 0) {
                out.rgba[di] = out.rgba[di + 1] = out.rgba[di + 2] = out.rgba[di + 3] = 0;
              } else {
                out.rgba[di]     = static_cast<uint8_t>(std::min(255, r * 255 / a));
                out.rgba[di + 1] = static_cast<uint8_t>(std::min(255, g * 255 / a));
                out.rgba[di + 2] = static_cast<uint8_t>(std::min(255, b * 255 / a));
                out.rgba[di + 3] = a;
              }
            }
          }
          cairo_surface_destroy(surf);
          g_object_unref(handle);
          return out;
        }
        cairo_surface_destroy(surf);
      }
      g_object_unref(handle);
    }
    if (err) g_error_free(err);
    out.failed = true;
    return out;
  }
#endif

  if (wallpaper_thumbnail_pipeline_debug_enabled()) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    const char* r = stbi_failure_reason();
    std::cerr << "[wallpaper-thumb] decode: all decoders failed file=\"" << basename_only(path)
              << "\" reason=" << (r && r[0] ? r : "(none)") << "\n";
  }
  out.failed = true;
  auto _wpdur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _wpts).count();
  WP_LOG("decoded %dx%d in %ldms", out.width, out.height, static_cast<long>(_wpdur));
  return out;
}

cairo_surface_t* thumbnail_decoded_to_surface(const ThumbnailDecoded& d) {
  WP_SCOPE();
    
  if (!d.failed && !d.rgba.empty() && d.width > 0 && d.height > 0 &&
      d.rgba.size() >= static_cast<size_t>(d.width * d.height * 4)) {
     
    cairo_surface_t* surf = surface_from_linear_rgba(d.rgba.data(), d.width, d.height);
    if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) return surf;
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      if (!surf) {
        std::cerr << "[wallpaper-thumb] cairo: surface_from_linear_rgba returned null file=\"" << basename_only(d.path)
                  << "\" " << d.width << "x" << d.height << "\n";
      } else {
        const cairo_status_t st = cairo_surface_status(surf);
        std::cerr << "[wallpaper-thumb] cairo: bad image surface file=\"" << basename_only(d.path) << "\" "
                  << cairo_status_to_string(st) << "\n";
      }
    }
    cairo_surface_destroy(surf);
  } else if (wallpaper_thumbnail_pipeline_debug_enabled() && !d.failed) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] cairo: skip RGBA→surface (size mismatch) file=\"" << basename_only(d.path)
              << "\" wh=" << d.width << "x" << d.height << " rgba_bytes=" << d.rgba.size() << "\n";
  }
  return failed_thumbnail_surface(d.path, d.max_px);
}

void write_thumbnail_disk_cache_maybe(const ThumbnailDecoded& decoded) {
  WP_SCOPE();
    
  if (decoded.from_disk_cache_hit || decoded.failed) return;
  if (decoded.max_px < 24) return;
  if (decoded.width <= 0 || decoded.height <= 0 || decoded.rgba.empty()) return;
  if (decoded.rgba.size() < static_cast<size_t>(decoded.width) * static_cast<size_t>(decoded.height) * 4u) return;

  auto p = cache_webp_path_for_source(decoded.path, decoded.max_px);
  if (!p) return;

  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(p->parent_path(), ec);
  if (ec) return;

  std::uint8_t* encoded = nullptr;
  const std::size_t encoded_size = WebPEncodeRGBA(decoded.rgba.data(), decoded.width, decoded.height,
                                                  decoded.width * 4, kDiskThumbWebpQuality, &encoded);
  if (encoded == nullptr || encoded_size == 0) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] cache: WebPEncodeRGBA failed file=\"" << basename_only(decoded.path) << "\"\n";
    }
    return;
  }

  std::ofstream file(*p, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(encoded), static_cast<std::streamsize>(encoded_size));
  WebPFree(encoded);
  if (!file) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] cache: write WebP failed file=\"" << basename_only(decoded.path) << "\" path=\""
                << p->string() << "\"\n";
    }
    std::error_code rm;
    fs::remove(*p, rm);
  }
}

cairo_surface_t* load_thumbnail(const std::string& path, int max_px) {
  WP_SCOPE();
    
  const ThumbnailDecoded d = decode_thumbnail_to_rgba(path, max_px);
  cairo_surface_t* s = thumbnail_decoded_to_surface(d);
  if (s) write_thumbnail_disk_cache_maybe(d);
  return s;
}

std::string normalize_wallpaper_path(const std::string& path) {
  WP_SCOPE();
    
  if (path.empty()) return path;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path p(path);
  fs::path c = fs::weakly_canonical(p, ec);
  return (ec || c.empty()) ? path : c.string();
}

static bool wallpaper_gallery_recursive() noexcept {
  WP_SCOPE();
    
  const char* e = std::getenv("EH_WALLPAPER_GALLERY_RECURSIVE");
  return e != nullptr && e[0] != '\0' && e[0] != '0';
}

std::vector<std::string> scan_image_files(const std::string& dir_path) {
  WP_SCOPE();
  WP_LOG("dir=%s", dir_path.c_str());
  std::vector<std::string> out;
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root(dir_path);
  const fs::file_status st = fs::status(root, ec);
  if (ec || !fs::is_directory(st)) return out;

  constexpr size_t kMaxEntries = 4000;
  if (wallpaper_gallery_recursive()) {
    try {
      for (const fs::directory_entry& ent :
           fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
         
        if (out.size() >= kMaxEntries) break;
        if (!ent.is_regular_file(ec) || ec) continue;
        const fs::path& p = ent.path();
        const std::string fname = p.filename().string();
        if (!fname.empty() && fname[0] == '.') continue;
        if (!filename_ends_with_raster_ext(fname)) continue;
        const std::string full = normalize_wallpaper_path(p.string());
        if (::access(full.c_str(), R_OK) == 0) out.push_back(full);
      }
    } catch (const fs::filesystem_error&) {
    }
  } else {
    DIR* d = opendir(dir_path.c_str());
    if (!d) return out;
    errno = 0;
    while (dirent* ent = readdir(d)) {
      if (out.size() >= kMaxEntries) break;
      if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
        continue;
      }
      const std::string name = ent->d_name;
      if (!filename_ends_with_raster_ext(name)) continue;
      std::string full = dir_path;
      if (!full.empty() && full.back() != '/') full.push_back('/');
      full += name;
      if (::access(full.c_str(), R_OK) == 0) out.push_back(full);
    }
    closedir(d);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  if (out.size() > kMaxEntries) out.resize(kMaxEntries);
  WP_LOG("found %zu files", out.size());
  return out;
}



}
