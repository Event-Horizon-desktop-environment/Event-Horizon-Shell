#pragma once

#include "wayland/core/protocols.hpp"

#include <algorithm>
#include <cstdint>

struct wl_compositor;
struct wl_surface;

namespace eh::wayland {

struct LayerSurfaceConfig {
  const char* nameSpace = "";
  zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;

  std::uint32_t anchor = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  int exclusiveZone = 0;
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;

  zwlr_layer_surface_v1_keyboard_interactivity keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
};

inline bool create_layer_surface(wl_compositor* compositor,
                                 zwlr_layer_shell_v1* layerShell,
                                 wl_output* output,
                                 const LayerSurfaceConfig& cfg,
                                 const zwlr_layer_surface_v1_listener* listener,
                                 void* listenerData,
                                 wl_surface** outSurface,
                                 zwlr_layer_surface_v1** outLayerSurface) {
  if (!compositor || !layerShell || !output || !outSurface || !outLayerSurface) return false;
  *outSurface = nullptr;
  *outLayerSurface = nullptr;

  wl_surface* surface = wl_compositor_create_surface(compositor);
  if (!surface) return false;

  const char* ns = cfg.nameSpace ? cfg.nameSpace : "";
  zwlr_layer_surface_v1* ls =
      zwlr_layer_shell_v1_get_layer_surface(layerShell, surface, output, cfg.layer, ns);
  if (!ls) {
    wl_surface_destroy(surface);
    return false;
  }

  if (listener) zwlr_layer_surface_v1_add_listener(ls, listener, listenerData);

  zwlr_layer_surface_v1_set_anchor(ls, cfg.anchor);
  zwlr_layer_surface_v1_set_size(ls, cfg.width, cfg.height);
  zwlr_layer_surface_v1_set_exclusive_zone(ls, cfg.exclusiveZone);
  zwlr_layer_surface_v1_set_margin(ls, cfg.marginTop, cfg.marginRight, cfg.marginBottom, cfg.marginLeft);
  zwlr_layer_surface_v1_set_keyboard_interactivity(ls, cfg.keyboard);

  *outSurface = surface;
  *outLayerSurface = ls;
  return true;
}

}
