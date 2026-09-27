#pragma once

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>

#include <cstddef>
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
  Spotlight = 13,
  Thumbs = 14,
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

// Overflow chevron slot key: shown when labeled window buttons don't fit.
constexpr const char* kTbChevronKey = "__eh_chevron";

// Win7-style hover window previews (thumbnail cards): popup-local geometry
// shared by paint, motion and click so the three can never disagree. Cards
// reserve a preview rect (title bar + content area); v1 paints the app icon
// there and live window pixels plug into the same rect later.
struct ThumbGeom {
  bool list = false;
  int cols = 1;
  int rows = 0;
  double cardW = 200.0;
  double cardH = 122.0;
  double w = 0.0;
  double h = 0.0;
};

// Win7-style hover window previews (thumbnail cards): popup-local geometry
// shared by paint, motion and click so the three can never disagree. Cards
// reserve a preview rect (title bar + content area); v1 paints the app icon
// there and live window pixels plug into the same rect later.
constexpr double kThumbPad = 10.0;
constexpr double kThumbGap = 8.0;
constexpr double kThumbCardW = 200.0;
constexpr double kThumbTitleH = 26.0;
constexpr double kThumbPreviewH = 96.0;
constexpr double kThumbCloseSz = 22.0;
constexpr double kThumbListRowH = 30.0;
constexpr double kThumbListW = 300.0;

inline ThumbGeom thumbs_geom(size_t n, bool listMode) {
  ThumbGeom g;
  g.list = listMode;
  if (listMode) {
    g.rows = static_cast<int>(n);
    g.w = kThumbListW;
    g.h = kThumbPad * 2.0 + static_cast<double>(n) * kThumbListRowH + 4.0;
    return g;
  }
  int cols = 1;
  if (n <= 1) cols = 1;
  else if (n <= 2) cols = 2;
  else if (n <= 4) cols = 2;
  else if (n <= 6) cols = 3;
  else if (n <= 9) cols = 3;
  else cols = 4;
  g.cols = cols;
  g.rows = static_cast<int>((n + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
  g.w = kThumbPad * 2.0 + cols * g.cardW + (cols - 1) * kThumbGap;
  g.h = kThumbPad * 2.0 + g.rows * g.cardH + (g.rows - 1) * kThumbGap + 4.0;
  return g;
}

inline void thumbs_card_rect(const ThumbGeom& g, size_t idx, double* x, double* y) {
  const int col = static_cast<int>(idx) % g.cols;
  const int row = static_cast<int>(idx) / g.cols;
  *x = kThumbPad + col * (g.cardW + kThumbGap);
  *y = kThumbPad + row * (g.cardH + kThumbGap);
}

// Returns the row under (px,py) in popup-local coords, or -1.
inline int thumbs_row_at(const ThumbGeom& g, size_t n, double px, double py) {
  if (g.list) {
    for (size_t i = 0; i < n; ++i) {
      const double ry = kThumbPad + i * kThumbListRowH;
      if (px >= kThumbPad && px < kThumbPad + (g.w - kThumbPad * 2.0) &&
          py >= ry && py < ry + kThumbListRowH)
        return static_cast<int>(i);
    }
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    double cx = 0.0, cy = 0.0;
    thumbs_card_rect(g, i, &cx, &cy);
    if (px >= cx && px < cx + g.cardW && py >= cy && py < cy + g.cardH)
      return static_cast<int>(i);
  }
  return -1;
}

// Close-button rect for a row, in popup-local coords.
inline void thumbs_close_rect(const ThumbGeom& g, size_t idx, double* x, double* y) {
  if (g.list) {
    const double ry = kThumbPad + idx * kThumbListRowH;
    *x = g.w - kThumbPad - kThumbCloseSz;
    *y = ry + (kThumbListRowH - kThumbCloseSz) * 0.5;
    return;
  }
  double cx = 0.0, cy = 0.0;
  thumbs_card_rect(g, idx, &cx, &cy);
  *x = cx + g.cardW - kThumbCloseSz - 4.0;
  *y = cy + (kThumbTitleH - kThumbCloseSz) * 0.5;
}

inline bool thumbs_point_in_close(const ThumbGeom& g, size_t idx, double px, double py) {
  double cx = 0.0, cy = 0.0;
  thumbs_close_rect(g, idx, &cx, &cy);
  return px >= cx && px < cx + kThumbCloseSz && py >= cy && py < cy + kThumbCloseSz;
}

}
