#pragma once

#include "desktop_shell/taskbar/layout/taskbar_types.hpp"

#include <cairo.h>

#include <cstddef>
#include <string>
#include <vector>

struct TaskbarApp;

namespace eh::shell::taskbar {

void taskbar_paint_widget_bar(TaskbarApp& app, cairo_t* cr,
                               double x, double y, double boxW, double boxH,
                               const std::vector<std::string>& leftW,
                               const std::vector<std::string>& centerW,
                               const std::vector<std::string>& rightW,
                               std::vector<TaskbarWidgetHit>* out_hits,
                               int taskbarHoverSlot, int taskbarPressedSlot,
                               double taskbarHoverLiftPx,
                               bool usePanel = true);

double taskbar_measure_content_width(TaskbarApp& app,
                                      const std::vector<std::string>& leftW,
                                      const std::vector<std::string>& centerW,
                                      const std::vector<std::string>& rightW);

}
