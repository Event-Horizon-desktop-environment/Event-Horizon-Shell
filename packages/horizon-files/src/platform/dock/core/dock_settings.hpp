#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct LaunchpadFolderDef {
  std::string name;
  std::vector<std::string> appIds;
  bool operator==(const LaunchpadFolderDef&) const = default;
};

struct DockSettings {

  bool dockShowDock = true;
  bool dockAutoHide = false;
  int dockRadius = 18;
  int dockIconSize = 44;
  int dockIconSpacing = 15;
  int dockBottomGap = 6;
  bool dockBorderEnabled = false;
  int dockBorderSize = 1;

  int dockBorderHue = 200;
  int dockBorderOpacity = 100;

  int dockOpacity = 100;
  std::string iconTheme = "";
  std::vector<std::string> pinnedApps{};

  std::vector<std::string> drawerPinnedApps{};

  std::vector<std::string> startMenuPinnedApps{};

  std::vector<std::string> leftWidgets{};
  std::vector<std::string> centerWidgets{};
  std::vector<std::string> rightWidgets{};

  std::string outputName{};

  bool dockWidgetsEnabled = true;

  bool dockGroupApps = true;

  bool dockTooltipsEnabled = true;

  bool dockPinnedAppsTrayPill = false;

  int slotPillOpacity = 100;

  double dockScale = 1.0;
  double shellUiScale = 1.0;

  bool dockBarFollowsIcons = true;
  int dockManualBarHeightPx = 72;

};

[[nodiscard]] inline double dock_ui_scale(const DockSettings& s) {
  return std::clamp(s.dockScale, 0.5, 2.0) * std::clamp(s.shellUiScale, 0.5, 2.0);
}

[[nodiscard]] inline double shell_ui_scale(const DockSettings& s) {
  return std::clamp(s.shellUiScale, 0.5, 2.0);
}

[[nodiscard]] inline int dock_height_for_icon_px(int iconPx) { return std::min(200, std::max(32, iconPx + 28)); }
