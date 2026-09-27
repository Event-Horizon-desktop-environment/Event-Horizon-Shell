#pragma once

// Event Horizon top/bottom panel — brand-new settings block.
//
// Design notes:
//  - Top bar pattern: a thin edge-anchored strip with three zones
//    (left = activities/workspaces, center = clock, right = indicators).
//    Popups anchor to the slot that opened them and clear the bar.
//  - This panel uses our own geometry: three width modes
//    shared with the taskbar vocabulary — Floating (inset pill),
//    Edge-to-edge (full-bleed strip), Fill (content-sized centered pill) —
//    plus a top/bottom anchor, auto-hide, and the full widget set we already
//    ship (clock, workspaces, tray, battery, bluetooth, media, ...).
//
// All helpers below are original to the panel; value ranges mirror the
// taskbar's so the settings UI feels consistent.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eh::shell::panel {

struct PanelSettings {
  bool enabled = false;
  // 0 = floating (inset pill), 1 = edge-to-edge (full bleed), 2 = fill (content pill).
  int widthMode = 1;
  int height = 36;
  int radius = 12;
  int opacity = 96;
  int iconSize = 22;
  int iconSpacing = 8;
  int floatingAmount = 8;
  int edgeGap = 0;
  int exclusiveZoneGap = 0;
  double scale = 1.0;
  std::string iconTheme{};
  std::string outputName{};
  std::vector<std::string> leftWidgets{};
  std::vector<std::string> centerWidgets{};
  std::vector<std::string> rightWidgets{};
  bool positionTop = true;
  bool autoHide = false;
  bool tooltipsEnabled = true;
  bool widgetsEnabled = true;
  bool border = false;
  int borderSize = 1;
  // Extra behavior options (all original implementation).
  bool reserveSpace = true;      // keep a compositor exclusive zone (windows avoid the bar)
  bool overlayLayer = false;     // layer-shell OVERLAY (above fullscreen) instead of TOP
  bool hoverHighlight = true;    // tint the widget under the pointer
  bool smartAutoHide = false;    // hide when any workspace has windows; show when all empty (wins over autoHide)
  bool revealOnWorkspaceSwitch = true;  // briefly reveal a hidden bar when the active workspace changes
  bool scrollChangesVolume = true;      // vertical scroll over the bar adjusts the default sink volume
  std::string deadZoneLeft = "none";
  std::string deadZoneMiddle = "none";
  std::string deadZoneRight = "none";
  bool capsuleEnabled = true;    // pill backgrounds behind indicator slots
  int capsuleOpacity = 100;
  int cornerTL = 12;             // per-corner radii; the Appearance "Corner Radius" slider sets all four
  int cornerTR = 12;
  int cornerBL = 12;
  int cornerBR = 12;
  std::vector<std::string> clickThroughWidgets{};  // widget ids that never take pointer events
  // Snapshot of the shell's global UI scale at the last settings apply.
  // Not user config: lets the reload path notice when a global scale change
  // alters the effective bar geometry.
  double uiGlobalScale = 1.0;
};

// Effective painted bar height. Guarantees breathing room above/below the
// icon square so widgets never paint flush against the rounded ends.
inline int panel_effective_height_px(const PanelSettings& s, double globalScale) {
  const double ui = std::clamp(s.scale, 0.5, 2.0) * std::clamp(globalScale, 0.5, 2.0);
  const double need = std::ceil(static_cast<double>(s.iconSize) * ui) + 2.0 * std::ceil(6.0 * ui);
  return std::max(s.height, static_cast<int>(std::lround(need)));
}

// Inner padding between the widget strip and the bar's rounded ends, and the
// minimum gap between left/center/right sections. Slightly tighter than the
// taskbar: a top panel is thinner and GNOME-like density matters.
inline double panel_strip_pad_px(double uiScale) { return 16.0 * uiScale; }
inline double panel_section_gap_px(double uiScale) { return 20.0 * uiScale; }

inline int panel_exclusive_zone_gap_px(const PanelSettings& s) {
  return std::max(0, std::min(100, s.exclusiveZoneGap));
}

inline int panel_edge_gap_px(const PanelSettings& s) { return std::max(0, std::min(25, s.edgeGap)); }

// Clearance a popup needs to clear the bar: edge margin (+ floating inset in
// floating mode) plus the painted bar height. Mirrors the layer margins set
// at surface creation so popups never overlap the bar.
inline int panel_popup_clearance_px(const PanelSettings& s) {
  const bool floating = s.widthMode == 0;
  const int barH = s.height + (floating ? 4 : 0);
  return (floating ? s.floatingAmount : 0) + panel_edge_gap_px(s) + barH;
}

inline bool panel_is_floating(const PanelSettings& s) { return s.widthMode == 0; }
inline bool panel_is_fill(const PanelSettings& s) { return s.widthMode == 2; }

}  // namespace eh::shell::panel
