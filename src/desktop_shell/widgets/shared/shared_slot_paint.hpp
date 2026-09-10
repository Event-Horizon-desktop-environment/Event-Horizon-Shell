#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eh::widgets::shared_slot_paint {

enum class SlotKind : uint8_t {
  App,
  Tray,
  Settings,
  Spotlight,
  AppMenu,
  Launchpad,
  AppDrawer,
  Clock,
  Weather,
  Media,
  ControlCenter,
  Workspaces,
  Trash,
  VolumeMixer,
  Separator,
  Battery,
  Bluetooth,
  Vpn,
  Smenu,
  WorldClock,
};

}
