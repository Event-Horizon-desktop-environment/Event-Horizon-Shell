#pragma once

#include "wl/buffer/shm_buffer.hpp"

#include <wayland-client.h>

#include <cstdint>

struct wp_viewport;
struct wp_viewporter;
struct zwlr_layer_surface_v1;
struct ext_background_effect_surface_v1;

namespace eh::shell::desktop {
struct DesktopApp;
}

namespace eh::shell {

struct DesktopLayer {
  eh::shell::desktop::DesktopApp* desktopApp = nullptr;
  wl_output* wlOut = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  ext_background_effect_surface_v1* bgEffect = nullptr;
  wp_viewport* shellViewport = nullptr;
  wl_surface* widgetSurface = nullptr;
  zwlr_layer_surface_v1* widgetLayer = nullptr;
  wp_viewport* widgetViewport = nullptr;
  ext_background_effect_surface_v1* widgetBgEffect = nullptr;
  wl_surface* menuSurface = nullptr;
  zwlr_layer_surface_v1* menuLayer = nullptr;
  ext_background_effect_surface_v1* menuBgEffect = nullptr;
  wp_viewport* menuViewport = nullptr;
  eh::wayland::ShmBuffer shmBuf{};
  eh::wayland::ShmBuffer widgetShmBuf{};
  eh::wayland::ShmBuffer menuShmBuf{};
  int configuredWidth = 0;
  int configuredHeight = 0;
  int widgetConfiguredWidth = 0;
  int widgetConfiguredHeight = 0;
  int menuConfiguredWidth = 0;
  int menuConfiguredHeight = 0;
  bool configured = false;
  bool widgetConfigured = false;
  bool menuConfigured = false;
  bool menuSurfaceHidden = false;
};

namespace desktop {

[[nodiscard]] bool layer_enabled();
[[nodiscard]] DesktopLayer* layer_from_surface(const DesktopApp& app, wl_surface* s);
[[nodiscard]] bool pointer_on_desktop_layer(const DesktopApp& app);

[[nodiscard]] bool pointer_enter_desktop(DesktopApp& app, wl_surface* surface);

void paint_layer(DesktopApp& app, DesktopLayer& L);
void paint_all_layers(DesktopApp& app);
void clear_layers(DesktopApp& app);

[[nodiscard]] bool create_layers(DesktopApp& app);

}
}
