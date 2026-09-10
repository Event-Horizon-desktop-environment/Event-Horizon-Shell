#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"

int eh_app_drawer_debug_level() { return 0; }

void eh::shell::dock::app_drawer::trace_line(int, std::string_view, const std::string&) {}
  