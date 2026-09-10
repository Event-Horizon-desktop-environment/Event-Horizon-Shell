#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <string>

namespace eh::shell::dock::tray_menu {

std::string dock_get_menu_object_path(sdbus::IProxy& statusNotifierItemProxy);

}
