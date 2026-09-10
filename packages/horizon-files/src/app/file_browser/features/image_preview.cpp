#include "app/file_browser/features/image_preview.hpp"

#include "stb/stb_image.h"

#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <setjmp.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <jpeglib.h>

namespace eh::file_browser {

bool is_image_extension(const std::string& path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext;
  for (size_t i = dot; i < path.size(); ++i) {
    char c = path[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    ext += c;
  }
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
         ext == ".gif" || ext == ".bmp" || ext == ".webp" ||
         ext == ".tiff" || ext == ".tif";
}

// ── helpers ────────────────────────────────────────────────────────────

static cairo_surface_t* create_cairo_surface_from_rgba(
    const unsigned char* data, int w, int h) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    return nullptr;
  }
  int stride = cairo_image_surface_get_stride(surf);
  auto* dst = cairo_image_surface_get_data(surf);
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(
        dst + static_cast<size_t>(y) * stride);
    const unsigned char* src = data + static_cast<size_t>(y) * 4 * w;
    for (int x = 0; x < w; ++x) {
      unsigned char pr = static_cast<unsigned char>(
          (static_cast<unsigned>(src[0]) * src[3] + 127) / 255);
      unsigned char pg = static_cast<unsigned char>(
          (static_cast<unsigned>(src[1]) * src[3] + 127) / 255);
      unsigned char pb = static_cast<unsigned char>(
          (static_cast<unsigned>(src[2]) * src[3] + 127) / 255);
      row[x] = (static_cast<uint32_t>(src[3]) << 24) |
               (static_cast<uint32_t>(pr) << 16) |
               (static_cast<uint32_t>(pg) << 8) |
               static_cast<uint32_t>(pb);
      src += 4;
    }
  }
  cairo_surface_mark_dirty_rectangle(surf, 0, 0, w, h);
  return surf;
}

static bool ext_is(const std::string& path, const char* target) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext;
  for (size_t i = dot; i < path.size(); ++i) {
    char c = path[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    ext += c;
  }
  return ext == target;
}

static bool is_webp_ext(const std::string& path) {
  return ext_is(path, ".webp");
}

static bool is_jpeg_ext(const std::string& path) {
  return ext_is(path, ".jpg") || ext_is(path, ".jpeg");
}

static std::vector<unsigned char> read_whole_file(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  if (len <= 0) { std::fclose(f); return {}; }
  std::rewind(f);
  std::vector<unsigned char> buf(static_cast<size_t>(len));
  size_t n = std::fread(buf.data(), 1, static_cast<size_t>(len), f);
  std::fclose(f);
  if (static_cast<long>(n) != len) return {};
  return buf;
}

// ── JPEG fast path (IDCT downscaling via libjpeg) ─────────────────────

struct jpeg_error_mgr_wrap {
  struct jpeg_error_mgr pub;
  jmp_buf jmp;
};

static void jpeg_error_handler(j_common_ptr cinfo) {
  auto* myerr = reinterpret_cast<jpeg_error_mgr_wrap*>(cinfo->err);
  longjmp(myerr->jmp, 1);
}

static cairo_surface_t* load_jpeg_thumbnail(const std::string& path,
                                            int max_px) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return nullptr;

  // Verify JPEG magic bytes
  unsigned char magic[2];
  if (std::fread(magic, 1, 2, file) != 2 || magic[0] != 0xFF ||
      magic[1] != 0xD8) {
    std::fclose(file);
    return nullptr;
  }
  std::rewind(file);

  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr_wrap jerr;
  cinfo.err           = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpeg_error_handler;

  if (setjmp(jerr.jmp) != 0) {
    jpeg_destroy_decompress(&cinfo);
    std::fclose(file);
    return nullptr;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  jpeg_read_header(&cinfo, TRUE);

  // Choose the largest power-of-2 scale factor that still gives us >= max_px
  int factor = 1;
  while (factor < 8 &&
         cinfo.image_width / (factor * 2) >= static_cast<unsigned>(max_px) &&
         cinfo.image_height / (factor * 2) >= static_cast<unsigned>(max_px)) {
    factor *= 2;
  }
  cinfo.scale_num   = 1;
  cinfo.scale_denom = factor;

  jpeg_start_decompress(&cinfo);

  int out_w      = static_cast<int>(cinfo.output_width);
  int out_h      = static_cast<int>(cinfo.output_height);
  int num_chans  = cinfo.output_components; // 1 (grey) or 3 (RGB)

  std::vector<unsigned char> row(static_cast<size_t>(out_w) * num_chans);
  std::vector<unsigned char> rgba(static_cast<size_t>(out_w) * out_h * 4);

  while (cinfo.output_scanline < cinfo.output_height) {
    unsigned char* row_ptr = row.data();
    jpeg_read_scanlines(&cinfo, &row_ptr, 1);
    unsigned int y = cinfo.output_scanline - 1;
    auto* src = row.data();
    auto* dst = rgba.data() + static_cast<size_t>(y) * out_w * 4;
    if (num_chans == 3) {
      for (int x = 0; x < out_w; ++x) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
        src += 3; dst += 4;
      }
    } else {
      for (int x = 0; x < out_w; ++x) {
        dst[0] = src[0]; dst[1] = src[0]; dst[2] = src[0]; dst[3] = 255;
        src += 1; dst += 4;
      }
    }
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  std::fclose(file);

  // If output is still larger than max_px, let cairo finish the job
  int dw = out_w;
  int dh = out_h;
  if (dw > max_px || dh > max_px) {
    double sc = std::min(static_cast<double>(max_px) / dw,
                         static_cast<double>(max_px) / dh);
    dw = std::max(1, static_cast<int>(std::round(dw * sc)));
    dh = std::max(1, static_cast<int>(std::round(dh * sc)));

    cairo_surface_t* src = create_cairo_surface_from_rgba(rgba.data(), out_w, out_h);
    if (!src) return nullptr;
    cairo_surface_t* dst =
        cairo_surface_create_similar_image(src, CAIRO_FORMAT_ARGB32, dw, dh);
    if (!dst || cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(src);
      cairo_surface_destroy(dst);
      return nullptr;
    }
    cairo_t* cr = cairo_create(dst);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(dst);
    return dst;
  }

  return create_cairo_surface_from_rgba(rgba.data(), out_w, out_h);
}

// ── WebP fast path (decode with built-in scaling) ──────────────────────

static cairo_surface_t* load_webp_thumbnail(const std::string& path,
                                            int max_px) {
  auto file_data = read_whole_file(path);
  if (file_data.empty()) return nullptr;

  WebPBitstreamFeatures features;
  if (WebPGetFeatures(file_data.data(), file_data.size(), &features) !=
      VP8_STATUS_OK)
    return nullptr;

  int w = features.width;
  int h = features.height;
  if (w <= 0 || h <= 0) return nullptr;

  double sc  = 1.0;
  if (w > max_px || h > max_px)
    sc = std::min(static_cast<double>(max_px) / w,
                  static_cast<double>(max_px) / h);
  int dw = std::max(1, static_cast<int>(std::round(w * sc)));
  int dh = std::max(1, static_cast<int>(std::round(h * sc)));

  // Decode directly at target size via WebP's built-in scaler
  WebPDecoderConfig config;
  if (!WebPInitDecoderConfig(&config)) return nullptr;
  config.options.use_scaling    = 1;
  config.options.scaled_width   = dw;
  config.options.scaled_height  = dh;
  config.output.colorspace       = MODE_RGBA;

  if (WebPDecode(file_data.data(), file_data.size(), &config) !=
      VP8_STATUS_OK) {
    WebPFreeDecBuffer(&config.output);
    return nullptr;
  }

  cairo_surface_t* surf = create_cairo_surface_from_rgba(
      config.output.u.RGBA.rgba, dw, dh);
  WebPFreeDecBuffer(&config.output);
  return surf;
}

// ── Main entry point ───────────────────────────────────────────────────

cairo_surface_t* load_image_thumbnail(const std::string& path, int max_px) {
  // JPEG fast path — IDCT downscaling via libjpeg (huge win for large photos)
  if (is_jpeg_ext(path)) {
    if (auto* surf = load_jpeg_thumbnail(path, max_px))
      return surf;
  }

  // WebP fast path — decode directly at target resolution
  if (is_webp_ext(path)) {
    if (auto* surf = load_webp_thumbnail(path, max_px))
      return surf;
  }

  // Fallback: stb_image for PNG, GIF, BMP, TIFF, etc.
  int w = 0, h = 0, channels = 0;
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);

  // stb_image may not support webp in this build — try libwebp directly
  if (!data && is_webp_ext(path)) {
    auto file_data = read_whole_file(path);
    if (!file_data.empty()) {
      uint8_t* webp = WebPDecodeRGBA(file_data.data(),
                                      file_data.size(), &w, &h);
      if (webp && w > 0 && h > 0) {
        cairo_surface_t* result = nullptr;
        cairo_surface_t* src = create_cairo_surface_from_rgba(webp, w, h);
        if (src) {
          double sc = 1.0;
          if (w > max_px || h > max_px)
            sc = std::min(static_cast<double>(max_px) / w,
                          static_cast<double>(max_px) / h);
          int dw = static_cast<int>(std::round(w * sc));
          int dh = static_cast<int>(std::round(h * sc));
          if (dw < 1) dw = 1;
          if (dh < 1) dh = 1;
          cairo_surface_t* dst =
              cairo_surface_create_similar_image(src, CAIRO_FORMAT_ARGB32, dw, dh);
          if (dst && cairo_surface_status(dst) == CAIRO_STATUS_SUCCESS) {
            cairo_t* cr = cairo_create(dst);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, src, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint(cr);
            cairo_destroy(cr);
            cairo_surface_flush(dst);
            result = dst;
          } else {
            cairo_surface_destroy(dst);
          }
          cairo_surface_destroy(src);
        }
        WebPFree(webp);
        return result;
      }
      WebPFree(webp);
    }
    return nullptr;
  }

  if (!data) return nullptr;
  if (w <= 0 || h <= 0) {
    stbi_image_free(data);
    return nullptr;
  }

  // Scale down if larger than max_px
  double scale = 1.0;
  if (w > max_px || h > max_px)
    scale = std::min(static_cast<double>(max_px) / w,
                     static_cast<double>(max_px) / h);

  int dw = static_cast<int>(std::round(w * scale));
  int dh = static_cast<int>(std::round(h * scale));
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;

  cairo_surface_t* src = create_cairo_surface_from_rgba(data, w, h);
  stbi_image_free(data);
  if (!src) return nullptr;

  cairo_surface_t* dst =
      cairo_surface_create_similar_image(src, CAIRO_FORMAT_ARGB32, dw, dh);
  if (!dst || cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(src);
    cairo_surface_destroy(dst);
    return nullptr;
  }

  cairo_t* cr = cairo_create(dst);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(src);
  cairo_surface_flush(dst);
  return dst;
}

} // namespace eh::file_browser
