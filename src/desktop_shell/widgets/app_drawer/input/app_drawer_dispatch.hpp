#pragma once

#include <cstdint>

struct DockApp;

namespace eh::shell::dock::app_drawer {

bool app_drawer_handle_pointer_motion(DockApp& app);
bool app_drawer_handle_click(DockApp& app, bool left, bool right);
bool app_drawer_handle_axis(DockApp& app, double deltaPx);

}
