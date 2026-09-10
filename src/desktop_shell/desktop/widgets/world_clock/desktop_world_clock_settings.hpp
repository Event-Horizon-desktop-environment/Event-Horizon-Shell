#pragma once

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <cstddef>

namespace eh::shell::desktop {

struct DesktopApp;

void world_clock_settings_open(DesktopApp& app);
void world_clock_settings_close(DesktopApp& app);
[[nodiscard]] bool world_clock_settings_hit(DesktopApp& app, double lx, double ly);
[[nodiscard]] bool world_clock_settings_handle_left_press(DesktopApp& app);
[[nodiscard]] bool world_clock_settings_left_release(DesktopApp& app);
[[nodiscard]] bool world_clock_settings_pointer_motion(DesktopApp& app);
[[nodiscard]] bool world_clock_settings_handle_scroll(DesktopApp& app, double dy);
[[nodiscard]] bool world_clock_settings_key(DesktopApp& app, xkb_keysym_t sym, const char* utf8,
                                            size_t utf8Len);
void world_clock_settings_paint(DesktopApp& app, cairo_t* cr);

}
