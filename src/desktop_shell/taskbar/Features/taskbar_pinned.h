#pragma once

#include <string>
#include <vector>

namespace eh::shell::taskbar {

struct TaskbarApp;

const std::vector<std::string>& taskbar_pinned_apps_source_for_layout(const TaskbarApp& app);
void taskbar_pin_drag_rebuild_paint_order(TaskbarApp& app);

}
