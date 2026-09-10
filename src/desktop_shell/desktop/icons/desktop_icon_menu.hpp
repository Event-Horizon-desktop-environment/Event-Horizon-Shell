#pragma once

#include <cairo/cairo.h>

#include <cstddef>

namespace eh::shell::desktop {

struct DesktopApp;



[[nodiscard]] bool desktop_icon_menu_handle_left_press(DesktopApp& app);
[[nodiscard]] bool desktop_icon_menu_pointer_motion(DesktopApp& app);
[[nodiscard]] bool desktop_icon_menu_left_release(DesktopApp& app);

void desktop_icon_menu_paint(DesktopApp& app, size_t layer_index, cairo_t* cr);
void desktop_icon_menu_close(DesktopApp& app);

void desktop_icon_menu_open(DesktopApp& app, int targetIdx);

void desktop_mount_dialog_close(DesktopApp& app);
[[nodiscard]] bool desktop_mount_dialog_hit(DesktopApp& app, double lx, double ly);
[[nodiscard]] bool desktop_mount_dialog_handle_left_press(DesktopApp& app);
[[nodiscard]] bool desktop_mount_dialog_left_release(DesktopApp& app);
[[nodiscard]] bool desktop_mount_dialog_pointer_motion(DesktopApp& app);
void desktop_mount_dialog_process_deferred_action(DesktopApp& app);

}
