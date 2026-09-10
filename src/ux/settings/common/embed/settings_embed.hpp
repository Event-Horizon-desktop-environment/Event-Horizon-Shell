#pragma once

#include <cstdint>
#include <memory>

struct wl_display;
struct wl_seat;
struct wl_surface;
struct xkb_state;

namespace eh::wayland {
class GammaService;
class VulkanDisplayContext;
}

namespace eh::settings {

[[nodiscard]] int run_standalone();

[[nodiscard]] bool embed_init(wl_display* display, wl_seat* shared_seat);
void embed_set_gamma_service(eh::wayland::GammaService* gs);

void embed_register_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk);
void embed_shutdown();
void embed_toggle();
void embed_minimize();
void embed_maximize_toggle();

void embed_show_tab(int tab_index);
[[nodiscard]] bool embed_window_visible();

[[nodiscard]] wl_surface* embed_settings_surface();

[[nodiscard]] wl_surface* embed_widget_picker_surface();

[[nodiscard]] wl_surface* embed_default_app_picker_surface();

void embed_dispatch_pointer_motion(wl_surface* localSurface, double sx, double sy);
void embed_dispatch_pointer_button(wl_surface* localSurface, std::uint32_t button, std::uint32_t state,
                                   std::uint32_t pointer_serial);

void embed_dispatch_pointer_axis_vertical(double delta_px);
void embed_dispatch_pointer_leave();

void embed_dispatch_widget_picker_pointer_leave();

void embed_dispatch_default_app_picker_pointer_leave();

[[nodiscard]] bool embed_try_keyboard(std::uint32_t keycode, std::uint32_t state, xkb_state* xkb);

[[nodiscard]] int thumbnail_wake_fd();
void embed_wallpaper_thumbnail_poll();

void embed_after_display_dispatch();

}
