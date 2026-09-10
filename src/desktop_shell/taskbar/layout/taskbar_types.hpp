#pragma once

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "services/tray/manager/tray_item.hpp"

namespace eh::shell::taskbar {

enum class TaskbarPopupKind : uint8_t {
  None = 0,
  Tray = 1,
  App = 2,
  Calendar = 3,
  Weather = 4,
  VolumeMixer = 5,
  AppDrawer = 6,
  ControlCenter = 7,
  Vpn = 8,
  Battery = 9,
  MediaPlayer = 10,
  PowerConfirm = 11,
  Bluetooth = 12,
};

struct TaskbarPopupItem {
  int32_t id = 0;
  std::string label;
  bool enabled = true;
};

using TaskbarTrayItem = eh::tray::TrayItem;

using TaskbarRunningGroup = eh::shell::shared::RunningGroup;
using TaskbarRunningSnapshot = eh::shell::shared::RunningSnapshot;

struct TaskbarWidgetHit {
  std::string widgetId;
  double x = 0, y = 0, w = 0, h = 0;
  std::uint64_t chosenSerial = 0;
  bool isPinned = false;
  int slotKind = 0;
};

struct TaskbarApp;
struct TaskbarOutputLayer;

}
