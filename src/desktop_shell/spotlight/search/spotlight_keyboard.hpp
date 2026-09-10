#pragma once

#include <cstdint>
#include <xkbcommon/xkbcommon.h>

struct DockApp;

namespace eh::shell::dock::spotlight {

bool spotlight_handle_keyboard(DockApp& app, xkb_keysym_t sym, uint32_t keycode, uint32_t state);
bool spotlight_handle_click(DockApp& app);

}
