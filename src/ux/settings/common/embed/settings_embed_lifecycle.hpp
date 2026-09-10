#pragma once

#include <cstdint>
#include <functional>
#include <memory>

struct wl_display;
struct wl_seat;
struct wl_surface;
struct App;

namespace eh::wayland {
class GammaService;
class VulkanDisplayContext;
}

namespace eh::settings {

// External-process launcher. `set_settings_launcher` lets the
// supervisor install a hook that toggles a running instance over the IPC bus;
// `request_launch_settings()` invokes it, falling back to a plain spawn.
using SettingsLauncherFn = std::function<void()>;
void set_settings_launcher(SettingsLauncherFn fn);
void request_launch_settings();

// Spawn `horizon-settings` (setsid, stdout/stderr to a log file). Returns false
// if spawn failed. Refuses to spawn when an instance is already running.
[[nodiscard]] bool spawn_settings();

// Single-instance guard (pidfile in $XDG_RUNTIME_DIR). Called by run_standalone
// on entry/exit so both `horizon-settings` and `EventHorizon --eh-settings` share
// one instance, and by the supervisor to decide spawn vs. toggle.
[[nodiscard]] bool settings_singleton_acquire();
void settings_singleton_release();
[[nodiscard]] bool settings_singleton_running();

void embed_register_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk);
void redraw_settings_application(App& app);
[[nodiscard]] int run_standalone();
[[nodiscard]] bool embed_init(::wl_display* display, ::wl_seat* shared_seat);
void embed_set_gamma_service(eh::wayland::GammaService* gs);
void embed_shutdown();
bool embed_window_visible();
void embed_toggle();
void embed_show_tab(int tab_index);
wl_surface* embed_settings_surface();
wl_surface* embed_widget_picker_surface();
wl_surface* embed_default_app_picker_surface();
int thumbnail_wake_fd();
void embed_wallpaper_thumbnail_poll();
void embed_after_display_dispatch();
void embed_request_redraw();
bool embed_is_initialized();

} // namespace eh::settings
