#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wl/buffer/shm_buffer.hpp"

#include <memory>

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_shm;
struct wl_surface;
struct wp_viewport;
struct wp_viewporter;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace eh::wayland {
class WaylandConnection;
}  // namespace eh::wayland

namespace eh::wallpaper {

using WallpaperOverlayPainter = void (*)(void* userData, cairo_t* cr, int w, int h);

struct WallpaperRenderer;

struct WallpaperOutputLayer {
  WallpaperRenderer* renderer = nullptr;
  wl_output* wlOut = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  wp_viewport* viewport = nullptr;
  eh::wayland::ShmBuffer buf{};
  int configuredW = 0;
  int configuredH = 0;
  bool configured = false;
  uint32_t pendingSerial = 0;
};

struct WallpaperRenderer {
  std::unique_ptr<eh::wayland::WaylandConnection> conn{};
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;
  wp_viewporter* viewporter = nullptr;
  std::vector<std::unique_ptr<WallpaperOutputLayer>> layers{};
  cairo_surface_t* cachedImage = nullptr;
  void* cacheMmapPtr = nullptr;
  size_t cacheMmapSize = 0;
  int cachedW = 0;
  int cachedH = 0;
  cairo_format_t cachedFormat = CAIRO_FORMAT_RGB24;
  int cachedStride = 0;
  std::vector<unsigned char> compressedImage{};
  std::string currentImage{};
  int currentMode = 0;
  bool enabled = false;
  bool pendingDecode = false;
  WallpaperOverlayPainter overlayPainter = nullptr;
  void* overlayUserData = nullptr;
  ~WallpaperRenderer();
};

[[nodiscard]] bool wallpaper_renderer_init(WallpaperRenderer& r);
void wallpaper_renderer_add_output(WallpaperRenderer& r, wl_output* output);
void wallpaper_renderer_apply(WallpaperRenderer& r, bool enabled,
                               const std::string& imagePath, int modeIndex);
void wallpaper_renderer_poll_decode(WallpaperRenderer& r);
void wallpaper_renderer_clear(WallpaperRenderer& r);
void wallpaper_renderer_redraw(WallpaperRenderer& r);

}
