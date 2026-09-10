#pragma once

#include <string>
#include <string_view>

int eh_app_drawer_debug_level();

namespace eh::shell::dock::app_drawer {

void trace_line(int min_level, std::string_view component, const std::string& message);

}
