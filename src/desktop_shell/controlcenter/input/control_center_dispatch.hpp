#pragma once

#include <cstdint>
#include <xkbcommon/xkbcommon.h>

struct DockApp;

namespace eh::shell::control_center {

bool handle_button_press(DockApp& app, uint32_t serial);
bool handle_motion(DockApp& app);
bool handle_button_release(DockApp& app);
bool handle_axis(DockApp& app, double deltaPx);
bool handle_keyboard(DockApp& app, xkb_keysym_t sym, uint32_t keycode);

}
