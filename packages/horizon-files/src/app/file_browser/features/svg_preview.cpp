#include "app/file_browser/features/svg_preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifdef EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define NANOSVG_IMPLEMENTATION
#include <nanosvg/src/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/src/nanosvgrast.h>
#pragma GCC diagnostic pop
#endif

namespace eh::file_browser {

bool is_svg_extension(const std::string& path) {
  const char suf[] = ".svg";
  if (path.size() < 4) return false;
  for (int i = 0; i < 4; ++i) {
    char a = path[path.size() - 4 + i];
    char b = suf[i];
    if ((a >= 'A' && a <= 'Z') ? (a - 'A' + 'a') != b
        : (a >= 'a' && a <= 'z') ? a != b
        : a != b) return false;
  }
  return true;
}

cairo_surface_t* load_svg_thumbnail(const std::string& path, int max_px) {
#ifdef EH_HAVE_RSVG
  GError* err = nullptr;
  RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
  if (!handle) {
    if (err) g_error_free(err);
    return nullptr;
  }

  double iw = 0.0, ih = 0.0;
  if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih)) {
    iw = 128.0;
    ih = 128.0;
  }
  if (iw <= 0.0 || ih <= 0.0) {
    g_object_unref(handle);
    return nullptr;
  }

  double scale = 1.0;
  if (std::max(iw, ih) > static_cast<double>(max_px))
    scale = static_cast<double>(max_px) / std::max(iw, ih);

  int rw = std::max(1, static_cast<int>(std::lround(iw * scale)));
  int rh = std::max(1, static_cast<int>(std::lround(ih * scale)));

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    g_object_unref(handle);
    return nullptr;
  }

  cairo_t* cr = cairo_create(surf);
  RsvgRectangle viewport{0.0, 0.0, static_cast<double>(rw), static_cast<double>(rh)};
  GError* render_err = nullptr;
  bool ok = rsvg_handle_render_document(handle, cr, &viewport, &render_err);
  cairo_destroy(cr);
  g_object_unref(handle);
  if (render_err) g_error_free(render_err);

  if (!ok) {
    cairo_surface_destroy(surf);
    return nullptr;
  }

  cairo_surface_flush(surf);
  return surf;

#else // NanoSVG fallback

  NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  if (!image) return nullptr;

  float iw = image->width;
  float ih = image->height;
  if (iw <= 0.0f || ih <= 0.0f) {
    nsvgDelete(image);
    return nullptr;
  }

  float scale = 1.0f;
  if (std::max(iw, ih) > static_cast<float>(max_px))
    scale = static_cast<float>(max_px) / std::max(iw, ih);

  int rw = std::max(1, static_cast<int>(std::lround(iw * scale)));
  int rh = std::max(1, static_cast<int>(std::lround(ih * scale)));

  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(image);
    return nullptr;
  }

  std::vector<uint8_t> rgba(static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4, 0);
  nsvgRasterize(rast, image, 0, 0, scale, rgba.data(), rw, rh, rw * 4);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    return nullptr;
  }

  int stride = cairo_image_surface_get_stride(surf);
  auto* dst = cairo_image_surface_get_data(surf);

  for (int y = 0; y < rh; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(dst + y * stride);
    const uint8_t* src = rgba.data() + static_cast<size_t>(y * rw * 4);
    for (int x = 0; x < rw; ++x) {
      uint32_t r = src[0];
      uint32_t g = src[1];
      uint32_t b = src[2];
      uint32_t a = src[3];
      uint32_t pr = (r * a + 127) / 255;
      uint32_t pg = (g * a + 127) / 255;
      uint32_t pb = (b * a + 127) / 255;
      row[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
      src += 4;
    }
  }

  cairo_surface_mark_dirty_rectangle(surf, 0, 0, rw, rh);
  cairo_surface_flush(surf);
  return surf;
#endif
}

} // namespace eh::file_browser
