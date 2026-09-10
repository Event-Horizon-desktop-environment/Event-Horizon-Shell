#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "services/mpris/mpris_album_art.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <curl/curl.h>

#include <cairo/cairo.h>

#ifdef EH_HAVE_LIBJPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif

namespace eh::mpris {

namespace {

constexpr std::int64_t kMaxAlbumArtPixels = 4LL * 1024 * 1024;
constexpr std::size_t kMaxAlbumArtBytes = 15U * 1024U * 1024U;

std::once_flag g_curl_init;

void curl_ensure() {
   
  std::call_once(g_curl_init, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

bool starts_with_ci(std::string_view s, std::string_view p) {
   
  if (s.size() < p.size()) return false;
  for (size_t i = 0; i < p.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(s[i]);
    const unsigned char b = static_cast<unsigned char>(p[i]);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return true;
}

std::string lower_copy_sv(std::string_view s) {
   
  std::string r;
  r.reserve(s.size());
  for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return r;
}

bool contains_ci(std::string_view hay, std::string_view needle) {
   
  if (needle.empty()) return true;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
         
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

std::string getenv_str(const char* k) {
   
  const char* v = std::getenv(k);
  return v ? std::string(v) : std::string();
}

cairo_surface_t* scale_surface_to_max(cairo_surface_t* src, int max_px) {
   
  if (!src || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) return nullptr;
  const int w = cairo_image_surface_get_width(src);
  const int h = cairo_image_surface_get_height(src);
  if (w <= 0 || h <= 0) return src;
  const double sx = max_px / static_cast<double>(w);
  const double sy = max_px / static_cast<double>(h);
  const double sc = std::min(1.0, std::min(sx, sy));
  if (sc >= 1.0 - 1e-6) return src;
  const int nw = std::max(1, static_cast<int>(std::lround(w * sc)));
  const int nh = std::max(1, static_cast<int>(std::lround(h * sc)));
  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
  if (!out || cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(out);
    cairo_surface_destroy(src);
    return nullptr;
  }
  cairo_t* cr = cairo_create(out);
  cairo_pattern_t* pat = cairo_pattern_create_for_surface(src);
  cairo_matrix_t mat{};
  cairo_matrix_init_scale(&mat, 1.0 / sc, 1.0 / sc);
  cairo_pattern_set_matrix(pat, &mat);
  cairo_set_source(cr, pat);
  cairo_pattern_destroy(pat);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(src);
  return out;
}

struct PngReadCtx {
  const uint8_t* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
};

cairo_status_t png_read_cb(void* closure, unsigned char* data, unsigned int length) {
   
  auto* ctx = static_cast<PngReadCtx*>(closure);
  if (!ctx->data || ctx->pos + static_cast<size_t>(length) > ctx->len) return CAIRO_STATUS_READ_ERROR;
  std::memcpy(data, ctx->data + ctx->pos, static_cast<size_t>(length));
  ctx->pos += static_cast<size_t>(length);
  return CAIRO_STATUS_SUCCESS;
}

[[nodiscard]] std::uint32_t read_be32(const uint8_t* p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
         (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

[[nodiscard]] bool png_ihdr_dimensions(const uint8_t* data, size_t len, int* ow, int* oh) {
  constexpr std::array<uint8_t, 8> kPngSig{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
  if (len < 24 || ow == nullptr || oh == nullptr) return false;
  if (std::memcmp(data, kPngSig.data(), 8) != 0) return false;
  if (std::memcmp(data + 12, "IHDR", 4) != 0) return false;
  const std::uint32_t w = read_be32(data + 16);
  const std::uint32_t h = read_be32(data + 20);
  if (w == 0u || h == 0u || w > (1u << 24) || h > (1u << 24)) return false;
  *ow = static_cast<int>(w);
  *oh = static_cast<int>(h);
  return true;
}

[[nodiscard]] bool album_art_pixel_count_ok(int w, int h) noexcept {
  if (w <= 0 || h <= 0) return false;
  const auto p = static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h);
  return p <= kMaxAlbumArtPixels;
}

cairo_surface_t* load_png_from_bytes(const uint8_t* bytes, size_t len) {
   
  if (!bytes || len < 8) return nullptr;
  {
    int pw = 0, ph = 0;
    if (png_ihdr_dimensions(bytes, len, &pw, &ph) && !album_art_pixel_count_ok(pw, ph)) return nullptr;
  }
  PngReadCtx ctx{bytes, len, 0};
  cairo_surface_t* s = cairo_image_surface_create_from_png_stream(&png_read_cb, &ctx);
  if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return nullptr;
  }
  return s;
}

#ifdef EH_HAVE_LIBJPEG
struct JpegErrorMgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

void jpeg_error_exit(j_common_ptr cinfo) {
   
  auto* jerr = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
  longjmp(jerr->setjmp_buffer, 1);
}

cairo_surface_t* load_jpeg_from_bytes(const uint8_t* data, size_t len) {
   
  if (!data || len < 2) return nullptr;
  FILE* mem = fmemopen(const_cast<uint8_t*>(data), len, "rb");
  if (!mem) return nullptr;

  struct jpeg_decompress_struct cinfo {};
  JpegErrorMgr jerr{};
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpeg_error_exit;

  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(mem);
    return nullptr;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, mem);
  jpeg_read_header(&cinfo, TRUE);
  const int w = static_cast<int>(cinfo.image_width);
  const int h = static_cast<int>(cinfo.image_height);
  if (w <= 0 || h <= 0 || !album_art_pixel_count_ok(w, h)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(mem);
    return nullptr;
  }
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);

  std::vector<uint8_t> row(static_cast<size_t>(w) * 3u);
  cairo_surface_t* surf =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(mem);
    return nullptr;
  }

  uint8_t* dst = cairo_image_surface_get_data(surf);
  const int stride = cairo_image_surface_get_stride(surf);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW rows[1] = {row.data()};
    jpeg_read_scanlines(&cinfo, rows, 1);
    const int y = static_cast<int>(cinfo.output_scanline) - 1;
    uint8_t* line = dst + static_cast<size_t>(y) * static_cast<size_t>(stride);
    for (int x = 0; x < w; ++x) {
      const uint8_t r = row[static_cast<size_t>(x) * 3u + 0u];
      const uint8_t g = row[static_cast<size_t>(x) * 3u + 1u];
      const uint8_t b = row[static_cast<size_t>(x) * 3u + 2u];
      line[static_cast<size_t>(x) * 4u + 0u] = b;
      line[static_cast<size_t>(x) * 4u + 1u] = g;
      line[static_cast<size_t>(x) * 4u + 2u] = r;
      line[static_cast<size_t>(x) * 4u + 3u] = 255u;
    }
  }

  cairo_surface_mark_dirty(surf);
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(mem);
  return surf;
}
#else
cairo_surface_t* load_jpeg_from_bytes(const uint8_t*, size_t) { return nullptr; }
#endif

cairo_surface_t* decode_image_bytes(const uint8_t* data, size_t len) {
   
  if (!data || len < 8) return nullptr;
  if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
    return load_png_from_bytes(data, len);
  }
  if (data[0] == 0xff && data[1] == 0xd8) {
    return load_jpeg_from_bytes(data, len);
  }
  return nullptr;
}

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
   
  auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
  const size_t n = size * nmemb;
  if (buf->size() + n > kMaxAlbumArtBytes) return 0;
  buf->insert(buf->end(), reinterpret_cast<uint8_t*>(ptr), reinterpret_cast<uint8_t*>(ptr) + n);
  return n;
}

bool curl_fetch_url(const std::string& url, std::vector<uint8_t>& out, long timeout_sec = 8) {
   
  curl_ensure();
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "EventHorizon/1.0");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  const CURLcode res = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) return false;
  if (http != 0 && (http < 200 || http >= 300)) return false;
  return !out.empty();
}

std::vector<uint8_t> read_file_all(const char* path) {
   
  std::vector<uint8_t> out;
  FILE* f = std::fopen(path, "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::rewind(f);
  if (sz <= 0 || static_cast<std::size_t>(sz) > kMaxAlbumArtBytes) {
    std::fclose(f);
    return out;
  }
  out.resize(static_cast<size_t>(sz));
  const size_t rd = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (rd != out.size()) out.clear();
  return out;
}

std::string extract_query_param_sv(std::string_view url, std::string_view key) {
   
  const size_t q = url.find('?');
  if (q == std::string_view::npos) return {};
  std::string_view query = url.substr(q + 1);
  while (!query.empty()) {
    const size_t amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    const size_t eq = pair.find('=');
    const std::string_view k = (eq == std::string_view::npos) ? pair : pair.substr(0, eq);
    if (k == key) {
      if (eq == std::string_view::npos || eq + 1 >= pair.size()) return {};
      return std::string(pair.substr(eq + 1));
    }
    if (amp == std::string_view::npos) break;
    query.remove_prefix(amp + 1);
  }
  return {};
}

std::string youtube_thumb_from_xesam_url(std::string_view sourceUrl) {
   
  if (sourceUrl.empty()) return {};
  const std::string low = lower_copy_sv(sourceUrl);
  std::string videoId;

  if (low.find("youtube.com/watch") != std::string::npos) {
    videoId = extract_query_param_sv(sourceUrl, "v");
  } else {
    const char* kShort = "youtu.be/";
    const size_t m = low.find(kShort);
    if (m != std::string::npos) {
      const size_t start = m + std::strlen(kShort);
      if (start < sourceUrl.size()) {
        const size_t end = sourceUrl.find_first_of("?#&/", start);
        videoId.assign(sourceUrl.substr(start, end == std::string_view::npos ? sourceUrl.size() - start : end - start));
      }
    } else {
      const char* kSh = "youtube.com/shorts/";
      const size_t m2 = low.find(kSh);
      if (m2 != std::string::npos) {
        const size_t start = m2 + std::strlen(kSh);
        if (start < sourceUrl.size()) {
          const size_t end = sourceUrl.find_first_of("?#&/", start);
          videoId.assign(sourceUrl.substr(start, end == std::string_view::npos ? sourceUrl.size() - start : end - start));
        }
      }
    }
  }

  auto trim = [](std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  };
  trim(videoId);
  if (videoId.empty()) return {};
  return "https://i.ytimg.com/vi/" + videoId + "/hqdefault.jpg";
}

}

std::string derive_youtube_thumbnail_url(std::string_view sourceUrl) { return youtube_thumb_from_xesam_url(sourceUrl); }

AlbumArtSurfacePtr adopt_album_art_surface(cairo_surface_t* raw) {
   
  if (!raw) return nullptr;
  if (cairo_surface_status(raw) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(raw);
    return nullptr;
  }
  return AlbumArtSurfacePtr(raw, [](cairo_surface_t* p) { cairo_surface_destroy(p); });
}

std::string resolve_mpris_art_url(std::string_view dbus_art_url) {
   
  std::string path(dbus_art_url);
  while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(path.begin());
  while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
  if (path.empty()) return {};

  if (starts_with_ci(path, "file://")) path = path.substr(7);

  if (contains_ci(path, ".org.chromium.Chromium")) {
    if (starts_with_ci(path, "/var/tmp/")) {
      const size_t slash = path.rfind('/');
      const std::string fname = (slash == std::string::npos) ? path : path.substr(slash + 1);
      const std::string home = getenv_str("HOME");
      if (!home.empty() && !fname.empty()) {
        path = home + "/.var/app/sh.cider.Cider/cache/tmp/" + fname;
      }
    }
  }

  if (starts_with_ci(path, "http")) return path;
  if (!path.empty() && path.front() == '/') return path;
  return {};
}

cairo_surface_t* load_album_art_surface(const std::string& resolved_url, int max_edge_px) {
   
  if (resolved_url.empty() || max_edge_px < 16) return nullptr;

  std::vector<uint8_t> bytes;
  if (starts_with_ci(resolved_url, "http")) {
    if (!curl_fetch_url(resolved_url, bytes)) return nullptr;
  } else {
    bytes = read_file_all(resolved_url.c_str());
    if (bytes.empty()) return nullptr;
  }

  cairo_surface_t* raw = decode_image_bytes(bytes.data(), bytes.size());
  if (!raw) return nullptr;
  return scale_surface_to_max(raw, max_edge_px);
}

}
