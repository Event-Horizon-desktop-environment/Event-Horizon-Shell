#pragma once

#include <cstdint>

struct DockApp;

namespace eh::shell::dock {

void dock_handle_slot_press(DockApp& app, uint32_t serial, bool left, bool right, bool onDockSurface);

}
