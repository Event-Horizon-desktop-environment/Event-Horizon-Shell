#include "wallpaper/core/wallpaper.hpp"
#include "wallpaper/raster/wallpaper_raster_decode.hpp"
#include "wallpaper/wallpaper_log.hpp"

#include "wl/core/connection.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "viewporter-client-protocol.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lz4.h>

namespace eh::wallpaper {
namespace {

// Forward declarations.

static void paint_solid_background(WallpaperOutputLayer& L, WallpaperRenderer& r);
static void paint_from_surface(WallpaperOutputLayer& L, WallpaperRenderer& r,
                                cairo_surface_t* imgSurf, int imgW, int imgH);
static cairo_surface_t* decode_image(const std::string& imagePath,
                                      int& outW, int& outH,
                                      bool tryCache,
                                      void** outMmapPtr = nullptr,
                                      size_t* outMmapSize = nullptr);

// Layer surface configure / closed.

static void layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial,
                             uint32_t width, uint32_t height) {
  WP_SCOPE();
  auto* L = static_cast<WallpaperOutputLayer*>(data);
  if (!L || !L->layer || surf != L->layer) return;
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  L->pendingSerial = 0;
  if (width > 0) L->configuredW = static_cast<int>(width);
  if (height > 0) L->configuredH = static_cast<int>(height);
  L->configured = L->configuredW > 0 && L->configuredH > 0;
  if (L->configured && L->renderer) {
    paint_from_surface(*L, *L->renderer, nullptr, L->renderer->cachedW, L->renderer->cachedH);
  }
}

static void layer_closed(void*, zwlr_layer_surface_v1*) {
  WP_SCOPE();
}

static const zwlr_layer_surface_v1_listener kListener = {
  .configure = layer_configure,
  .closed = layer_closed,
};

// Pixel conversion: RGBA → Cairo RGB24 (BGRx).

static cairo_surface_t* decoded_to_cairo_surface(const RasterDecoded& img) {
  WP_SCOPE();
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, img.width, img.height);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return surf;
  unsigned char* dst = cairo_image_surface_get_data(surf);
  const int stride = cairo_image_surface_get_stride(surf);
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      const size_t si = static_cast<size_t>(y * img.width + x) * 4;
      const size_t di = static_cast<size_t>(y * stride) + static_cast<size_t>(x) * 4;
      dst[di + 0] = img.pixels[si + 2];
      dst[di + 1] = img.pixels[si + 1];
      dst[di + 2] = img.pixels[si + 0];
      dst[di + 3] = 0;
    }
  }
  cairo_surface_mark_dirty(surf);
  return surf;
}

// LZ4-compressed cache helpers.

static bool compress_surface(cairo_surface_t* surf, int w, int h,
                              cairo_format_t fmt, int stride,
                              std::vector<unsigned char>& out) {
  WP_SCOPE();
  (void)fmt;
  if (!surf || w <= 0 || h <= 0) return false;
  const unsigned char* data = cairo_image_surface_get_data(surf);
  if (!data) return false;
  const std::size_t rawSize = static_cast<std::size_t>(h) * static_cast<std::size_t>(stride);
  const int bound = LZ4_compressBound(static_cast<int>(rawSize));
  if (bound <= 0) return false;
  out.resize(static_cast<std::size_t>(bound));
  const int compressed = LZ4_compress_default(
      reinterpret_cast<const char*>(data),
      reinterpret_cast<char*>(out.data()),
      static_cast<int>(rawSize), bound);
  if (compressed <= 0) { out.clear(); return false; }
  out.resize(static_cast<std::size_t>(compressed));
  out.shrink_to_fit();
  return true;
}

static cairo_surface_t* decompress_to_surface(const std::vector<unsigned char>& compressed,
                                                int w, int h,
                                                cairo_format_t fmt, int stride) {
  WP_SCOPE();
  (void)fmt;
  if (compressed.empty() || w <= 0 || h <= 0) return nullptr;
  cairo_surface_t* surf = cairo_image_surface_create(fmt, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return surf;
  unsigned char* data = cairo_image_surface_get_data(surf);
  if (!data) { cairo_surface_destroy(surf); return nullptr; }
  const std::size_t rawSize = static_cast<std::size_t>(h) * static_cast<std::size_t>(stride);
  const int dec = LZ4_decompress_safe(
      reinterpret_cast<const char*>(compressed.data()),
      reinterpret_cast<char*>(data),
      static_cast<int>(compressed.size()),
      static_cast<int>(rawSize));
  if (dec < 0 || static_cast<std::size_t>(dec) != rawSize) {
    cairo_surface_destroy(surf);
    return nullptr;
  }
  cairo_surface_mark_dirty(surf);
  return surf;
}

// Image decode, backed by a disk cache.

static void wallpaper_renderer_clear_mmap(WallpaperRenderer& r) {
  WP_SCOPE();
  if (r.cacheMmapPtr) { munmap(r.cacheMmapPtr, r.cacheMmapSize); r.cacheMmapPtr = nullptr; r.cacheMmapSize = 0; }
}

static std::string cache_state_dir() {
  WP_SCOPE();
  const char* home = std::getenv("XDG_STATE_HOME");
  if (home && home[0]) return std::string(home) + "/event-horizon";
  const char* home2 = std::getenv("HOME");
  return home2 ? std::string(home2) + "/.local/state/event-horizon" : "/tmp/event-horizon";
}

// Map the on-disk rgb24 cache for `imagePath` read-only and wrap it in a cairo
// surface without copying, so the image lives in clean page-cache pages the
// kernel can evict under pressure instead of anonymous heap.
static cairo_surface_t* try_map_rgb24_cache(const std::string& imagePath,
                                             int& outW, int& outH,
                                             void** outMapPtr, size_t* outMapSize) {
  WP_SCOPE();
  const std::string stateDir = cache_state_dir();
  std::ifstream meta(stateDir + "/wallpaper.meta");
  if (!meta) return nullptr;
  std::string cachedPath;
  int cw = 0, ch = 0;
  meta >> cachedPath >> cw >> ch;
  if (cachedPath != imagePath || cw <= 0 || ch <= 0) return nullptr;
  const size_t expected = static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4;
  const int fd = open((stateDir + "/wallpaper.rgb24").c_str(), O_RDONLY);
  if (fd < 0) return nullptr;
  struct stat st{};
  cairo_surface_t* surf = nullptr;
  if (fstat(fd, &st) == 0 && static_cast<size_t>(st.st_size) >= expected) {
    void* map =
        mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (map != MAP_FAILED) {
      surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(map),
                                                 CAIRO_FORMAT_RGB24, cw, ch, cw * 4);
      if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        outW = cw;
        outH = ch;
        if (outMapPtr) *outMapPtr = map;
        if (outMapSize) *outMapSize = static_cast<size_t>(st.st_size);
        close(fd);
        return surf;
      }
      if (surf) cairo_surface_destroy(surf);
      munmap(map, static_cast<size_t>(st.st_size));
    }
  }
  close(fd);
  return nullptr;
}

static cairo_surface_t* decode_image(const std::string& imagePath,
                                      int& outW, int& outH,
                                      bool tryCache,
                                      void** outMmapPtr,
                                      size_t* outMmapSize) {
  WP_SCOPE();
  WP_LOG("path=%s tryCache=%d", imagePath.c_str(), tryCache);
  const auto t0 = std::chrono::steady_clock::now();

  void* mapPtr = nullptr;
  size_t mapSize = 0;
  cairo_surface_t* mapped =
      tryCache ? try_map_rgb24_cache(imagePath, outW, outH, &mapPtr, &mapSize) : nullptr;
  if (mapped) {
    if (outMmapPtr) *outMmapPtr = mapPtr;
    if (outMmapSize) *outMmapSize = mapSize;
    std::cerr << "[wp-bench] mmap_cache " << outW << "x" << outH << " = "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - t0)
                     .count()
              << "ms\n";
    return mapped;
  }

  std::ifstream ifs(imagePath, std::ios::binary);
  if (!ifs) { std::cerr << "[wallpaper] can't open: " << imagePath << "\n"; return nullptr; }
  std::vector<uint8_t> filedata((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  auto decoded = decode_raster_from_bytes(filedata.data(), filedata.size());
  if (!decoded) { std::cerr << "[wallpaper] decode failed: " << imagePath << "\n"; return nullptr; }
  cairo_surface_t* surf = decoded_to_cairo_surface(*decoded);
  if (!surf) { std::cerr << "[wallpaper] cairo surface failed\n"; return nullptr; }

  outW = decoded->width; outH = decoded->height;

  const std::string stateDir = cache_state_dir();
  (void)mkdir(stateDir.c_str(), 0755);
  {
    std::ofstream meta(stateDir + "/wallpaper.meta");
    if (meta) meta << imagePath << " " << outW << " " << outH << "\n";
  }
  {
    unsigned char* px = cairo_image_surface_get_data(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    std::ofstream data(stateDir + "/wallpaper.rgb24", std::ios::binary);
    if (data) {
      for (int y = 0; y < outH; ++y)
        data.write(reinterpret_cast<const char*>(px + static_cast<size_t>(y) * stride),
                   static_cast<std::streamsize>(outW) * 4);
    }
  }

  std::cerr << "[wp-bench] decode " << outW << "x" << outH << " = "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() << "ms\n";
  return surf;
}

// Solid background fill.

static void paint_solid_background(WallpaperOutputLayer& L, WallpaperRenderer& r) {
  WP_SCOPE();
  if (!L.configured || L.configuredW <= 0 || L.configuredH <= 0) return;
  if (!r.shm) return;
  int bufW = L.configuredW;
  int bufH = L.configuredH;
  if (L.viewport) { bufW = 1; bufH = 1; }
  if (!L.buf.ensure(r.shm, "event-horizon-wallpaper", bufW, bufH)) return;
  cairo_t* cr = L.buf.cairo();
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, 0.06, 0.06, 0.07, 1.0);
  cairo_paint(cr);
  cairo_surface_flush(L.buf.cairo_surface());
  if (L.viewport) wp_viewport_set_destination(L.viewport, L.configuredW, L.configuredH);
  wl_surface_attach(L.surface, L.buf.wl(), 0, 0);
  wl_surface_damage_buffer(L.surface, 0, 0, bufW, bufH);
  L.buf.mark_busy();
  wl_surface_commit(L.surface);
  if (r.display) wl_display_flush(r.display);
}

// Paint the full output surface from a decoded image.

static void paint_from_surface(WallpaperOutputLayer& L, WallpaperRenderer& r,
                                 cairo_surface_t* imgSurf, int imgW, int imgH) {
  WP_SCOPE();
  WP_LOG("imgSurf=%p imgW=%d imgH=%d", (void*)imgSurf, imgW, imgH);
  if (!L.configured || L.configuredW <= 0 || L.configuredH <= 0) return;
  if (!r.shm) return;
  bool ownsTemp = false;
  if (!imgSurf && r.cachedImage) {
    imgSurf = r.cachedImage;
  }
  if (!imgSurf && !r.compressedImage.empty()) {
    imgSurf = decompress_to_surface(r.compressedImage, r.cachedW, r.cachedH, r.cachedFormat, r.cachedStride);
    ownsTemp = true;
  }
  if (!imgSurf) return;
  int bufW = L.configuredW;
  int bufH = L.configuredH;
  (void)L.viewport;
  (void)r;
  if (!L.buf.ensure(r.shm, "event-horizon-wallpaper", bufW, bufH)) { if (ownsTemp) cairo_surface_destroy(imgSurf); return; }
  cairo_t* cr = L.buf.cairo();
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const double imgWf = static_cast<double>(imgW);
  const double imgHf = static_cast<double>(imgH);
  const double surfWf = static_cast<double>(bufW);
  const double surfHf = static_cast<double>(bufH);

  if (r.enabled) {
    switch (r.currentMode) {
      case 0: {
        const double sc = std::max(surfWf / imgWf, surfHf / imgHf);
        cairo_save(cr);
        cairo_translate(cr, (surfWf - imgWf * sc) * 0.5, (surfHf - imgHf * sc) * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, imgSurf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
        break;
      }
      case 1: {
        const double sc = std::min(surfWf / imgWf, surfHf / imgHf);
        cairo_set_source_rgba(cr, 0.06, 0.06, 0.07, 1.0);
        cairo_paint(cr);
        cairo_save(cr);
        cairo_translate(cr, (surfWf - imgWf * sc) * 0.5, (surfHf - imgHf * sc) * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, imgSurf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
        break;
      }
      case 2: {
        cairo_save(cr);
        cairo_scale(cr, surfWf / imgWf, surfHf / imgHf);
        cairo_set_source_surface(cr, imgSurf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        break;
      }
      case 3: {
        cairo_set_source_rgba(cr, 0.06, 0.06, 0.07, 1.0);
        cairo_paint(cr);
        cairo_set_source_surface(cr, imgSurf, (surfWf - imgWf) * 0.5, (surfHf - imgHf) * 0.5);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        break;
      }
      case 4: {
        cairo_set_source_surface(cr, imgSurf, 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        break;
      }
      default: break;
    }
  } else {
    cairo_set_source_rgba(cr, 0.06, 0.06, 0.07, 1.0);
    cairo_paint(cr);
  }

  if (r.overlayPainter) {
    r.overlayPainter(r.overlayUserData, cr, L.configuredW, L.configuredH);
  }

  cairo_surface_flush(L.buf.cairo_surface());
  if (L.viewport) wp_viewport_set_destination(L.viewport, L.configuredW, L.configuredH);
  wl_surface_attach(L.surface, L.buf.wl(), 0, 0);
  wl_surface_damage_buffer(L.surface, 0, 0, bufW, bufH);
  L.buf.mark_busy();
  wl_surface_commit(L.surface);
  if (r.display) wl_display_flush(r.display);
  if (ownsTemp) cairo_surface_destroy(imgSurf);
}

} // anonymous namespace

// Public API.

WallpaperRenderer::~WallpaperRenderer() {
  MANGOWM_INFO("{}", __func__);
  conn.reset();
}

bool wallpaper_renderer_init(WallpaperRenderer& r) {
  WP_SCOPE();
  if (!r.conn) {
    r.conn = std::make_unique<eh::wayland::WaylandConnection>();
    if (!r.conn->connect(false)) {
      WP_LOG("connect failed");
      r.conn.reset();
      return false;
    }
  }
  r.display = r.conn->display();
  r.compositor = r.conn->compositor();
  r.shm = r.conn->shm();
  r.layerShell = r.conn->layer_shell();
  r.viewporter = r.conn->viewporter();
  return r.compositor && r.shm && r.layerShell;
}

void wallpaper_renderer_add_output(WallpaperRenderer& r, wl_output* output) {
  WP_SCOPE();
  WP_LOG("output=%p", (void*)output);
  if (!r.compositor || !r.layerShell || !output) return;
  for (const auto& up : r.layers)
    if (up && up->wlOut == output) return;

  auto L = std::make_unique<WallpaperOutputLayer>();
  L->renderer = &r;
  L->wlOut = output;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = "event-horizon-wallpaper";
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width = 0;
  cfg.height = 0;
  cfg.exclusiveZone = -1;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  if (!eh::wayland::create_layer_surface(r.compositor, r.layerShell, output, cfg,
                                          &kListener, L.get(),
                                          &L->surface, &L->layer)) {
    std::cerr << "[wallpaper] failed to create layer surface\n";
    return;
  }
  if (r.viewporter)
    L->viewport = wp_viewporter_get_viewport(r.viewporter, L->surface);
  wl_surface_commit(L->surface);
  if (r.display) (void)wl_display_roundtrip(r.display);
  r.layers.push_back(std::move(L));
}

void wallpaper_renderer_apply(WallpaperRenderer& r, bool enabled,
                               const std::string& imagePath, int modeIndex) {
  WP_SCOPE();
  WP_LOG("enabled=%d path=%s mode=%d", enabled, imagePath.c_str(), modeIndex);
  if (r.cachedImage) { cairo_surface_destroy(r.cachedImage); r.cachedImage = nullptr; }
  wallpaper_renderer_clear_mmap(r);
  r.cachedW = r.cachedH = 0;
  r.enabled = enabled;
  r.currentImage = imagePath;
  r.currentMode = modeIndex;

  if (!enabled || imagePath.empty()) {
    for (auto& up : r.layers)
      if (up) paint_solid_background(*up, r);
  }

  r.pendingDecode = enabled && !imagePath.empty();
}

void wallpaper_renderer_poll_decode(WallpaperRenderer& r) {
  WP_SCOPE();
  WP_LOG("pendingDecode=%d path=%s", r.pendingDecode, r.currentImage.c_str());
  if (!r.pendingDecode) return;
  r.pendingDecode = false;
  if (r.currentImage.empty()) return;

  void* mmapPtr = nullptr;
  size_t mmapSize = 0;
  cairo_surface_t* surf = decode_image(r.currentImage, r.cachedW, r.cachedH, true, &mmapPtr, &mmapSize);
  WP_LOG("decode_result: %dx%d surf=%p", r.cachedW, r.cachedH, (void*)surf);
  if (!surf) { r.pendingDecode = true; return; }

  constexpr int kMaxWallpaperDim = 4096;
  const bool overCap = r.cachedW > kMaxWallpaperDim || r.cachedH > kMaxWallpaperDim;

  if (mmapPtr && overCap) {
    // Oversize images need an editable heap copy so we can downscale below;
    // otherwise the read-only mapping is adopted verbatim at the end here.
    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_RGB24, r.cachedW, r.cachedH);
    if (copy && cairo_surface_status(copy) == CAIRO_STATUS_SUCCESS) {
      cairo_t* cr = cairo_create(copy);
      cairo_set_source_surface(cr, surf, 0, 0);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      cairo_paint(cr);
      cairo_destroy(cr);
      cairo_surface_destroy(surf);
      surf = copy;
    }
    munmap(mmapPtr, mmapSize);
    mmapPtr = nullptr;
    mmapSize = 0;
  }

  if (overCap) {
    const double sc = std::min(static_cast<double>(kMaxWallpaperDim) / r.cachedW,
                               static_cast<double>(kMaxWallpaperDim) / r.cachedH);
    const int newW = static_cast<int>(static_cast<double>(r.cachedW) * sc);
    const int newH = static_cast<int>(static_cast<double>(r.cachedH) * sc);
    cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_RGB24, newW, newH);
    if (scaled && cairo_surface_status(scaled) == CAIRO_STATUS_SUCCESS) {
      cairo_t* cr = cairo_create(scaled);
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, surf, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      cairo_paint(cr);
      cairo_destroy(cr);
      cairo_surface_destroy(surf);
      if (mmapPtr) munmap(mmapPtr, mmapSize);
      surf = scaled;
      r.cachedW = newW;
      r.cachedH = newH;
      mmapPtr = nullptr;
      mmapSize = 0;
    }
  }

  if (r.cachedImage) cairo_surface_destroy(r.cachedImage);
  wallpaper_renderer_clear_mmap(r);

  // If the fresh decode is within the size cap, adopt a mapping of the rgb24
  // cache that was just written, so the image stays in reclaimable page cache
  // instead of anonymous heap.
  if (!overCap && !mmapPtr) {
    int mw = r.cachedW;
    int mh = r.cachedH;
    void* mp = nullptr;
    size_t ms = 0;
    cairo_surface_t* mapped = try_map_rgb24_cache(r.currentImage, mw, mh, &mp, &ms);
    if (mapped && mw == r.cachedW && mh == r.cachedH) {
      cairo_surface_destroy(surf);
      surf = mapped;
      mmapPtr = mp;
      mmapSize = ms;
    } else if (mapped) {
      munmap(mp, ms);
      cairo_surface_destroy(mapped);
    }
  }

  r.cachedStride = cairo_image_surface_get_stride(surf);
  r.cachedFormat = cairo_image_surface_get_format(surf);

  if (mmapPtr) {
    r.cacheMmapPtr = mmapPtr;
    r.cacheMmapSize = mmapSize;
    r.cachedImage = surf;
    r.compressedImage.clear();
    r.compressedImage.shrink_to_fit();
    std::cerr << "[wp] storing " << r.cachedW << "x" << r.cachedH << " as page-cache mapping ("
              << mmapSize << " bytes file-backed, 0 anonymous)\n";
  } else if (compress_surface(surf, r.cachedW, r.cachedH, r.cachedFormat, r.cachedStride,
                              r.compressedImage)) {
    const std::size_t rawBytes = static_cast<std::size_t>(r.cachedH) * static_cast<std::size_t>(r.cachedStride);
    std::cerr << "[wp] LZ4 compressed " << r.cachedW << "x" << r.cachedH
              << " from " << rawBytes << " -> " << r.compressedImage.size() << " bytes ("
              << (rawBytes ? (100 - r.compressedImage.size() * 100 / rawBytes) : 0) << "% saved)\n";
    cairo_surface_destroy(surf);
    r.cachedImage = nullptr;
  } else {
    std::cerr << "[wp] LZ4 compression failed, keeping uncompressed surface\n";
    r.compressedImage.clear();
    r.cachedImage = surf;
  }

  for (auto& up : r.layers)
    if (up) paint_from_surface(*up, r, nullptr, r.cachedW, r.cachedH);
}

void wallpaper_renderer_redraw(WallpaperRenderer& r) {
  WP_SCOPE();
  WP_LOG("layers=%zu", r.layers.size());
  for (auto& up : r.layers) {
    if (!up || !up->configured) continue;
    if (r.cachedImage || !r.compressedImage.empty()) {
      paint_from_surface(*up, r, nullptr, r.cachedW, r.cachedH);
    } else {
      paint_solid_background(*up, r);
    }
  }
}

void wallpaper_renderer_clear(WallpaperRenderer& r) {
  WP_SCOPE();
  WP_LOG("layers=%zu", r.layers.size());
  r.pendingDecode = false;
  for (auto& up : r.layers) {
    if (!up) continue;
    if (up->viewport) { wp_viewport_destroy(up->viewport); up->viewport = nullptr; }
    if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
    if (up->surface) wl_surface_destroy(up->surface);
    up->layer = nullptr;
    up->surface = nullptr;
  }
  r.layers.clear();
  if (r.cachedImage) { cairo_surface_destroy(r.cachedImage); r.cachedImage = nullptr; }
  r.compressedImage.clear();
  r.compressedImage.shrink_to_fit();
  wallpaper_renderer_clear_mmap(r);
  r.cachedW = r.cachedH = 0;
}

}
