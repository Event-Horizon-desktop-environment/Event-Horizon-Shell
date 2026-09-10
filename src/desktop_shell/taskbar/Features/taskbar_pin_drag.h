#pragma once

#include <string>

namespace eh::shell::taskbar {

struct TaskbarApp;

struct PinDragReleaseResult {
  bool wasDragging = false;
  bool dragDirty = false;
  bool wasClick = false;
  std::string key{};
};

void taskbar_pin_drag_init(TaskbarApp& app, const std::string& widgetId);
PinDragReleaseResult taskbar_pin_drag_release(TaskbarApp& app, bool left);
void taskbar_pin_drag_motion(TaskbarApp& app);

}
