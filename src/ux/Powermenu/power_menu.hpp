#pragma once

#include "wl/buffer/shm_buffer.hpp"

#include <cstdint>

struct wl_callback;
struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_surface;
struct wl_shm;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace eh::ui::powermenu {

struct PowerMenu {
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;

  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layerSurface = nullptr;

  eh::wayland::ShmBuffer buf{};

  bool open = false;
  int actionIdx = -1;
  uint64_t startMs = 0;

  bool configured = false;
  int configuredW = 0;
  int configuredH = 0;
  int outputW = 0;
  int outputH = 0;
  int outputScale = 1;

  wl_callback* frameCallback = nullptr;
  bool wantRedraw = false;
  double pointerX = 0.0;
  double pointerY = 0.0;
};

bool open(PowerMenu& pm, wl_display* display, wl_compositor* compositor, wl_shm* shm,
          zwlr_layer_shell_v1* layerShell, wl_output* output, int outputW, int outputH, int outputScale, int actionIdx);

void close(PowerMenu& pm);

void draw(PowerMenu& pm, double pointerX, double pointerY);

enum class ClickResult {
  None,
  Cancel,
  Confirm,
  Close,
};

ClickResult handle_click(PowerMenu& pm, double pointerX, double pointerY);

bool tick(PowerMenu& pm);

}
