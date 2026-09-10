#pragma once

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/core/running_snapshot.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct PickSlot {
  enum class Kind : uint8_t {
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
    Vpn,
    Battery,
    Bluetooth,
    Smenu,
    WorldClock
  } kind = Kind::App;
  std::string key;
  std::uint64_t chosenSerial = 0;
  bool anyActivated = false;
  bool isPinned = false;
};

struct DockPickResult {
  std::vector<PickSlot> all;
  std::vector<DockApp::TrayItem> traySnap;
  std::uint64_t settingsChosenSerial = 0;
  bool settingsActivated = false;
  int idx = -1;

  std::size_t leftSlotCount = 0;
  std::size_t centerSlotCount = 0;
  std::size_t rightSlotCount = 0;
};

void dock_fill_pick_result_slots(DockApp& app, const eh::shell::shared::RunningSnapshot& hitSnap, const std::vector<std::string>& leftW,
                                 const std::vector<std::string>& centerW, const std::vector<std::string>& rightW,
                                 DockPickResult& r);
double dock_pick_layout_total_width(const DockApp& app, const std::vector<PickSlot>& all);
double dock_gap_after_pick(const DockSettings& st, const std::vector<PickSlot>& v, size_t idx, double defGap);

DockPickResult dock_pick_at(DockApp& app, double px, double py);
double dock_strip_slot_center_x(const DockApp& app, const DockPickResult& pr, int idx);
std::pair<double, double> dock_strip_slot_xw(const DockApp& app, const DockPickResult& pr, int idx);
int dock_pick_workspace_index(const DockApp& app, const DockPickResult& pr, int idx, double pointer_x);
