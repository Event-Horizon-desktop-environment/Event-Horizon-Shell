#pragma once

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <string>
#include <vector>

#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "services/tray/manager/tray_item.hpp"

namespace eh::shell::panel {

enum class PanelPopupKind : std::uint8_t {
  None = 0,
  Tray = 1,
  Calendar = 2,
  Weather = 3,
  VolumeMixer = 4,
  ControlCenter = 5,
  Vpn = 6,
  Battery = 7,
  MediaPlayer = 8,
  Bluetooth = 9,
};

struct PanelPopupItem {
  std::int32_t id = 0;
  std::string label;
  bool enabled = true;
};

using PanelTrayItem = eh::tray::TrayItem;
using PanelRunningGroup = eh::shell::shared::RunningGroup;
using PanelRunningSnapshot = eh::shell::shared::RunningSnapshot;

struct PanelWidgetHit {
  std::string widgetId;
  double x = 0, y = 0, w = 0, h = 0;
  std::uint64_t chosenSerial = 0;
  int slotKind = 0;
};

struct PanelApp;
struct PanelOutputLayer;

}  // namespace eh::shell::panel
