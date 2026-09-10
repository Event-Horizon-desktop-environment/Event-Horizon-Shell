#pragma once

#include <cairo/cairo.h>

#include <cstddef>

namespace eh::shell::desktop {

struct DesktopApp;



[[nodiscard]] bool desktop_menu_handle_left_press(DesktopApp& app);

[[nodiscard]] bool desktop_menu_pointer_motion(DesktopApp& app);

[[nodiscard]] bool desktop_menu_left_release(DesktopApp& app);

void desktop_menu_paint(DesktopApp& app, size_t layer_index, cairo_t* cr);
void desktop_menu_close(DesktopApp& app);

void desktop_menu_open_for_workspace(DesktopApp& app);

}
