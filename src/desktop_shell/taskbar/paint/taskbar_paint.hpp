#pragma once

#include "desktop_shell/taskbar/layout/taskbar_types.hpp"
#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "services/mpris/mpris_player.hpp"

#include <cairo.h>

#include <cstddef>
#include <string>
#include <vector>

struct TaskbarApp;

namespace eh::shell::taskbar {

/* Snapshots built once per draw in taskbar_draw() and shared by the measure and
   paint passes. build_running_snapshot() walks every toplevel and allocates the
   group vectors, and DockMpris::snapshot() copies the player strings; building
   each of them three times per frame (measure + layout + paint) was pure
   redundant work on the animation path. */
void taskbar_paint_widget_bar(TaskbarApp& app, cairo_t* cr,
                               double x, double y, double boxW, double boxH,
                               const std::vector<std::string>& leftW,
                               const std::vector<std::string>& centerW,
                               const std::vector<std::string>& rightW,
                               std::vector<TaskbarWidgetHit>* out_hits,
                               int taskbarHoverSlot, int taskbarPressedSlot,
                               double taskbarHoverLiftPx,
                               bool usePanel,
                               const eh::shell::shared::RunningSnapshot& runningSnap,
                               const eh::mpris::PlayerSnapshot& mprisSnap);

// Widths of the three taskbar sections (widget strip inner gaps included),
// measured with the same rules the paint pass uses to lay them out.
struct TaskbarSectionWidths {
  double left = 0.0;
  double center = 0.0;
  double right = 0.0;
  double total = 0.0;
};

TaskbarSectionWidths taskbar_measure_sections(TaskbarApp& app,
                                              const std::vector<std::string>& leftW,
                                              const std::vector<std::string>& centerW,
                                              const std::vector<std::string>& rightW,
                                              const eh::shell::shared::RunningSnapshot& runningSnap,
                                              const eh::mpris::PlayerSnapshot& mprisSnap);

}
