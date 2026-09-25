#pragma once

// PearCenter compact popup geometry. All values are base px at scale=100;
// callers multiply by pear_scale() and the shell uiScale.
// Combined scale `us` = dock_ui_scale * pear_scale.
//
// Reference metrics:
//   popup width   = 360 * scale
//   pills row 1   = 64 * scale (Network + Bluetooth)
//   pills row 2   = 56 * scale (Settings + Do Not Disturb)
//   toggles row   = 64 * scale (full width)
//   rows          = 90 * scale each (Volume, Input, Brightness, Media)
//   largeSpacing=12, mediumSpacing=8, smallSpacing=6, buttonMargin=4,
//   buttonHeight=48, fonts large=15 medium=12 small=7.

#include <string>
#include <vector>

namespace eh::mpris {
class DockMpris;
}
namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::dock::control_center {

struct ControlCenterState;
struct PearCenterConfig;

// Base (scale=100) constants.
inline constexpr double kPearBaseWidth = 360.0;
inline constexpr double kPearSectionH = 180.0;
inline constexpr double kPearSliderRowH = 90.0; // sectionHeight/2
inline constexpr double kPearMediaRowH = 90.0;
inline constexpr double kPearLargeSpacing = 12.0;
inline constexpr double kPearMediumSpacing = 8.0;
inline constexpr double kPearSmallSpacing = 6.0;
inline constexpr double kPearButtonMargin = 4.0;
inline constexpr double kPearButtonH = 48.0;
inline constexpr double kPearFontLarge = 15.0;
inline constexpr double kPearFontMedium = 12.0;
inline constexpr double kPearFontSmall = 7.0;

// Section A split: left SectionButtons card fills height, right column holds
// DndButton (full-width) on top + quick-toggle row below.
inline constexpr double kPearDndH = 56.0;
inline constexpr int kPearLongButtonCount = 3;
inline constexpr int kPearMaxWifiRows = 8;
inline constexpr double kPearNetHeaderH = 48.0;

// Quick-toggle slots in the toggles row, left to right.
enum class PearToggle : unsigned char {
  DeviceLink = 0,
  NightColor,
  ColorScheme,
  Camera,
  Cmd1,
  Cmd2
};

// Single source of truth for the PearCenter compact popup. Paint, hit-testing,
// dispatch, and popup sizing ALL consume PearLayout.
struct PearLayout {
  double W = 0.0;
  double us = 1.0; // combined scale
  double pad = 12.0;
  double gapM = 8.0;
  double gapS = 6.0;
  // Section A row 1: Network + Bluetooth pills.
  double row1Y = 12.0, row1H = 64.0;
  double netX = 12.0, netW = 162.0;
  double btX = 0.0, btW = 162.0;
  // Section A row 2: Settings + Do Not Disturb pills.
  double row2Y = 0.0, row2H = 56.0;
  double setX = 12.0, setW = 162.0;
  double dndX = 0.0, dndW = 162.0;
  // Toggles row (full width).
  bool showDnd = true;
  double togX = 12.0, togY = 0.0, togH = 64.0;
  std::vector<PearToggle> toggles;
  double togW = 0.0; // per-toggle width
  // Section B rows (full width, 0 height when hidden).
  bool showVolume = true;
  double volY = 0.0, volH = 90.0;
  int outDevRows = 0;
  double outDevY = 0.0, outDevH = 0.0;
  bool showInput = true;
  double inY = 0.0, inH = 90.0;
  int inDevRows = 0;
  double inDevY = 0.0, inDevH = 0.0;
  bool showBrightness = true;
  double briY = 0.0, briH = 90.0;
  bool showMedia = true;
  double mediaY = 0.0, mediaH = 90.0;
  // Networks overlay (covers all, z=999).
  int wifiRows = 0;
  double overlayH = 0.0;
  double totalH = 0.0;
};

// Slider track geometry inside a 90px Section B row card. Shared by paint +
// hit-testing so clicks always land on the pixels (same convention as
// control_center_audio_slider_layout). Row-relative.
inline constexpr double kPearRowTrackPadX = 16.0;
inline constexpr double kPearRowTrackY = 56.0; // from card top
inline constexpr double kPearRowTrackH = 12.0;
inline constexpr double kPearRowHitPad = 10.0;
// Icon (mute) button: 28px circle at left.
inline constexpr double kPearRowIconCx = 28.0;
inline constexpr double kPearRowIconCy = 28.0;
inline constexpr double kPearRowIconR = 14.0;

inline double pear_toggle_x(const PearLayout& L, size_t idx) {
  return L.togX + static_cast<double>(idx) * (L.togW + L.gapS);
}

// Device-panel row geometry (34px pitch, 32px header), mirrors paint.
inline double pear_dev_row_y(double panelY, int idx) {
  return panelY + 32.0 + static_cast<double>(idx) * 34.0;
}

// Media transport buttons: right-aligned prev/play/next in the media row.
inline double pear_media_btn_cx(const PearLayout& L, int idx) {
  constexpr double btnR = 14.0, gap = 10.0;
  const double right = L.W - L.pad - 22.0;
  return right - static_cast<double>(2 - idx) * (btnR * 2.0 + gap);
}
inline double pear_media_btn_cy(const PearLayout& L) { return L.mediaY + L.mediaH - 24.0; }

// Backlight availability (cached scan of /sys/class/backlight).
[[nodiscard]] bool pear_brightness_available();
// Writable check (cached): false on desktop rigs without a backlight node.
[[nodiscard]] bool pear_brightness_writable();
[[nodiscard]] int pear_brightness_pct(); // 0-100, -1 when unavailable
bool pear_set_brightness_pct(int pct);  // best-effort sysfs write

// Compute the compact layout. wifiApCount lets callers inject counts in
// tests; production passes -1 to query live backends.
PearLayout cc_compute_pear_layout(double popupW, ControlCenterState& state,
                                const PearCenterConfig& cfg, double uiScale,
                                int wifiApCount = -1);

// Spawn the DE's interactive screenshot region selection
// (`EventHorizon --eh-screenshot --select --preview`, resolved next to the
// running binary with a PATH fallback). Fire-and-forget.
bool pear_spawn_screenshot_select();

// Stable hash of everything the compact popup paints (excludes hover, which
// dispatch already repaints on change). The dock timer uses it to skip
// redundant popup redraws that read as flicker.
bool pear_popup_signature_changed(ControlCenterState& state,
                                  const eh::config::ShellConfig& sc,
                                  const std::string& widgetId,
                                  eh::mpris::DockMpris* mpris, double popupW);

} // namespace eh::shell::dock::control_center
