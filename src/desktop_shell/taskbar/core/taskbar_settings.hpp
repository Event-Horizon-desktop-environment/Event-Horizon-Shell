#pragma once

#include <algorithm>
#include <cmath>
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
  // Snapshot of the shell's global UI scale (sc.dock.shellUiScale) at the
  // last settings apply. Not user config: it lets the reload compare notice
  // when a global scale change alters the effective bar geometry.
  double uiGlobalScale = 1.0;
};

// Effective painted bar height. Guarantees at least 8 ui-scale px of
// breathing room above and below the icon square: when iconSize (times the
// UI scale) would otherwise fill the bar edge to edge, the bar grows so
// widgets never paint flush against its rounded ends. Idempotent with a
// settings.height that has already been normalized.
inline int effective_height_px(const TaskbarSettings& s, double globalScale) {
  const double ui = std::clamp(s.scale, 0.5, 2.0) * std::clamp(globalScale, 0.5, 2.0);
  const double need =
      std::ceil(static_cast<double>(s.iconSize) * ui) + 2.0 * std::ceil(8.0 * ui);
  return std::max(s.height, static_cast<int>(std::lround(need)));
}

// Inner padding reserved between the widget strip and the bar's rounded ends
// (per side), and the minimum breathing gap between the left/center/right
// sections of the panel layout.
inline double strip_pad_px(double uiScale) { return 16.0 * uiScale; }
inline double section_gap_px(double uiScale) { return 20.0 * uiScale; }

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

// Bottom clearance a popup needs to clear the bar: the bar's edge margin
// (edgeGap, plus the floating inset in floating mode) plus the bar's painted
// height (6px taller in floating mode). Mirrors the layer margins set at
// surface creation so popups never overlap the bar.
inline int popup_bottom_clearance_px(const TaskbarSettings& s) {
  const bool floating = s.widthMode == 0;
  const int barH = s.height + (floating ? 6 : 0);
  return (floating ? s.floatingAmount : 0) + edge_gap_px(s) + barH;
}

}
