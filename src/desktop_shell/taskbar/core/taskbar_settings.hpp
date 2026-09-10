#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace eh::shell::taskbar {

struct TaskbarSettings {
  bool enabled = false;
  int widthMode = 0;  // 0 = floating, 1 = edge, 2 = fill
  int height = 48;
  int radius = 12;
  int opacity = 100;
  int iconSize = 32;
  int iconSpacing = 8;
  int floatingAmount = 8;
  int edgeGap = 0;
  int exclusiveZoneGap = 0;
  double scale = 1.0;
  std::string iconTheme = "";
  std::string outputName{};
  std::vector<std::string> leftWidgets{};
  std::vector<std::string> centerWidgets{};
  std::vector<std::string> rightWidgets{};
  std::vector<std::string> pinnedApps{};
  bool groupApps = true;
  int slotPillOpacity = 100;
  bool positionTop = false;
  bool autoHide = false;
  bool tooltipsEnabled = true;
  bool pinnedAppsTrayPill = false;
  bool runningAppsTrayPill = false;
  bool widgetsEnabled = true;
  bool border = false;
  int borderSize = 1;
};

// Extra space reserved above (or below when positionTop) the taskbar for
// tiled/maximized windows. The layer surface's exclusive zone is the taskbar
// height plus this gap, so windows rest a configurable distance from the bar.
inline int exclusive_zone_gap_px(const TaskbarSettings& s) {
  return std::max(0, std::min(100, s.exclusiveZoneGap));
}

// Margin between the taskbar and the screen edge (bottom, or top when
// positionTop), moving the bar away from the edge in every width mode.
inline int edge_gap_px(const TaskbarSettings& s) {
  return std::max(0, std::min(25, s.edgeGap));
}

}
