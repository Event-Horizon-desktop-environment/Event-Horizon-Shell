#include "app/file_browser/features/video_preview.hpp"
#include "app/file_browser/features/video_worker.hpp"

#include <cairo/cairo.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace eh::file_browser {
namespace {

bool ends_ci(const std::string& path, const char* suf) {
  const size_t sl = std::strlen(suf);
  if (path.size() < sl) return false;
  for (size_t i = 0; i < sl; ++i) {
    char a = path[path.size() - sl + i];
    char b = suf[i];
    if ((a >= 'A' && a <= 'Z') ? (a - 'A' + 'a') != b
        : (a >= 'a' && a <= 'z') ? a != b
        : a != b) return false;
  }
  return true;
}

std::string cache_base() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0])
    return std::string(xdg) + "/thumbnails";
  if (const char* home = std::getenv("HOME"); home && home[0])
    return std::string(home) + "/.cache/thumbnails";
  return "/tmp/thumbnails";
}

std::string file_uri(const std::string& path) {
  return "file://" + path;
}

// MD5 hex digest of a string via OpenSSL EVP API
std::string md5_hex(const std::string& input) {
  unsigned char digest[16];
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
  EVP_DigestUpdate(ctx, input.data(), input.size());
  unsigned int len = 0;
  EVP_DigestFinal_ex(ctx, digest, &len);
  EVP_MD_CTX_free(ctx);

  char hex[33];
  for (int i = 0; i < 16; ++i)
    std::sprintf(hex + i * 2, "%02x", digest[i]);
  hex[32] = 0;
  return hex;
}

} // anonymous namespace

static cairo_surface_t* scale_surface(cairo_surface_t* src, int max_px) {
  int w = cairo_image_surface_get_width(src);
  int h = cairo_image_surface_get_height(src);
  if (w <= 0 || h <= 0) return nullptr;
  if (std::max(w, h) <= max_px) {
    cairo_surface_reference(src);
    return src;
  }
  double scale = static_cast<double>(max_px) / std::max(w, h);
  int nw = std::max(1, static_cast<int>(std::lround(w * scale)));
  int nh = std::max(1, static_cast<int>(std::lround(h * scale)));
  cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
  if (!scaled || cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(scaled);
    return nullptr;
  }
  cairo_t* cr = cairo_create(scaled);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(scaled);
  return scaled;
}

bool is_video_extension(const std::string& path) {
  return ends_ci(path, ".mp4")  || ends_ci(path, ".avi")  ||
         ends_ci(path, ".mkv")  || ends_ci(path, ".mov")  ||
         ends_ci(path, ".webm") || ends_ci(path, ".m4v")  ||
         ends_ci(path, ".wmv")  || ends_ci(path, ".flv")  ||
         ends_ci(path, ".f4v")  || ends_ci(path, ".3gp")  ||
         ends_ci(path, ".3g2")  || ends_ci(path, ".ogv")  ||
         ends_ci(path, ".mpg")  || ends_ci(path, ".mpeg") ||
         ends_ci(path, ".mpe")  || ends_ci(path, ".ts")   ||
         ends_ci(path, ".mts")  || ends_ci(path, ".m2ts") ||
         ends_ci(path, ".vob");
}

cairo_surface_t* load_video_thumbnail(const std::string& path, int max_px) {
  struct stat src_st{};
  if (stat(path.c_str(), &src_st) != 0 || !S_ISREG(src_st.st_mode))
    return nullptr;

  std::string thumb_dir = cache_base() + "/large";
  std::string uri = file_uri(path);
  std::string md5 = md5_hex(uri);
  if (md5.empty()) return nullptr;
  std::string cache_path = thumb_dir + "/" + md5 + ".png";

  // Cache hit — load synchronously
  if (access(cache_path.c_str(), R_OK) == 0) {
    struct stat cache_st{};
    if (stat(cache_path.c_str(), &cache_st) == 0 &&
        cache_st.st_mtime == src_st.st_mtime) {
      cairo_surface_t* s = cairo_image_surface_create_from_png(cache_path.c_str());
      if (s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
        cairo_surface_t* result = scale_surface(s, max_px);
        cairo_surface_destroy(s);
        return result;
      }
      cairo_surface_destroy(s);
    }
  }

  // Cache miss — hand off to background worker, show generic icon for now
  video_worker().enqueue(path, max_px, cache_path, src_st.st_mtime);
  return nullptr;
}

} // namespace eh::file_browser
