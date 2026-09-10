#pragma once

#include <optional>
#include <string>
#include <vector>

struct wl_compositor;
struct wl_display;
struct wl_seat;
struct wl_shm;
struct wl_output;
struct zwlr_layer_shell_v1;

namespace eh::wayland {
struct LogicalOutputBounds;
}

namespace eh::app {

// Shows a fullscreen overlay on @p output, lets the user drag-select a
// rectangle, and returns "WxH+X+Y" in global coordinates (or std::nullopt if
// cancelled with right-click or Escape).
[[nodiscard]] std::optional<std::string> interactive_region_select(
    wl_display* display,
    wl_compositor* compositor,
    wl_shm* shm,
    zwlr_layer_shell_v1* layer_shell,
    wl_seat* seat,
    const std::vector<eh::wayland::LogicalOutputBounds>& output_bounds,
    wl_output* target_output,
    const std::string&);

// Read the current global cursor position: create transparent fullscreen layer
// surfaces on every output and wait for a pointer-enter event.
// Returns (global_x, global_y) or std::nullopt on failure/timeout.
[[nodiscard]] std::optional<std::pair<int, int>> get_cursor_global_position(
    wl_display* display,
    wl_compositor* compositor,
    wl_shm* shm,
    zwlr_layer_shell_v1* layer_shell,
    wl_seat* seat,
    const std::vector<eh::wayland::LogicalOutputBounds>& output_bounds);

} // namespace eh::app
