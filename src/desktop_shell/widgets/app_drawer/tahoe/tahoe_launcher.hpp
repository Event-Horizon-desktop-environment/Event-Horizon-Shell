#pragma once

// Tahoe launcher — single source for the unified Applications window.
//
// macOS Tahoe (26) retired Launchpad and folded the app browser into
// Spotlight: one floating panel with a search field, category pills with
// counts, and an icon grid. EH merges its two pre-Tahoe popups (Spotlight
// list palette + AppMenu grid) into the same window: the query filters live
// (Spotlight behavior) and the grid browses by category (app-menu behavior).
//
// Both hosts (dock `start_menu.cpp`, taskbar/standalone
// `app_drawer_overlay.cpp`) include this header and keep only their state
// plumbing. Same code, same pixels — the two surfaces cannot drift apart.
//
// Layout (runtime W/H, no fixed assumptions):
//   search row  -> full-width field, magnifier + placeholder/query + caret,
//                  dim "Esc to close" hint right (mirrors Tahoe's right hint)
//   pills row   -> Tahoe category buckets with catalog counts, selected pill
//                  is accent-filled, single scrollable row
//   grid        -> 6 columns, 60px icons, two-line labels, hover wash +
//                  selected accent ring
//   footer      -> slim power strip (EH need; Tahoe has none, kept minimal)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

namespace eh::shell::tahoe {

// ---------------------------------------------------------------- buckets
// Tahoe bucket order matches Apple's Apps view; each lists the freedesktop
// categories that belong to it. First match wins for counting; filtering
// keeps a hit when any of its raw categories intersects the bucket.

struct TahoeBucket {
  const char* label;
  const char* members[16];
};

inline constexpr TahoeBucket kTahoeBuckets[] = {
    {"Productivity", {"Office", "Finance", "ProjectManagement", "Spreadsheet", "WordProcessor", "Presentation", nullptr}},
    {"Creativity", {"Graphics", "Photography", "Publishing", "ImageProcessing", "VectorGraphics", "RasterGraphics", nullptr}},
    {"Media", {"AudioVideo", "Audio", "Video", "Music", "Player", "Recorder", nullptr}},
    {"Games", {"Game", "Amusement", "ActionGame", "ArcadeGame", "BoardGame", "CardGame", "KidsGame", "LogicGame", "RolePlaying", "Shooter", "Simulation", "SportsGame", "StrategyGame", nullptr}},
    {"Developer", {"Development", "IDE", "GUIDesigner", "Profiling", nullptr}},
    {"Utilities", {"Utility", "System", "Settings", "Administration", "PackageManager", "FileTools", "TerminalEmulator", nullptr}},
    {"Other", {nullptr}},
};

inline constexpr int kTahoeBucketCount =
    static_cast<int>(sizeof(kTahoeBuckets) / sizeof(kTahoeBuckets[0]));

// Index of the bucket for a raw `;`-separated freedesktop category string.
// Falls through to "Other".
inline int tahoe_bucket_for(std::string_view cats) {
  for (int b = 0; b < kTahoeBucketCount - 1; ++b) {
    for (int m = 0; kTahoeBuckets[b].members[m] != nullptr; ++m) {
      const std::string_view want(kTahoeBuckets[b].members[m]);
      size_t start = 0;
      while (start <= cats.size()) {
        size_t end = cats.find(';', start);
        if (end == std::string_view::npos) end = cats.size();
        if (cats.substr(start, end - start) == want) return b;
        if (end == cats.size()) break;
        start = end + 1;
      }
    }
  }
  return kTahoeBucketCount - 1;
}

// True when a hit's raw categories belong in bucket `b` (-1 = All).
inline bool tahoe_hit_in_bucket(std::string_view cats, int b) {
  if (b < 0) return true;
  if (b >= kTahoeBucketCount) return false;
  if (b == kTahoeBucketCount - 1) return tahoe_bucket_for(cats) == b;
  return tahoe_bucket_for(cats) == b;
}

// ---------------------------------------------------------------- layout

struct TahoeLayout {
  // Search field.
  double searchX = 0, searchY = 0, searchW = 0, searchH = 46.0;
  // Pills row.
  double pillsY = 0, pillsH = 30.0;
  double pillsAvailX = 0, pillsAvailW = 0;
  // Grid.
  int cols = 6;
  double gridX = 0, gridY = 0, gridW = 0;
  double cellW = 0, cellH = 0;
  double iconSz = 60.0;
  // List rows (Icons-or-List, mirrors Tahoe's view switch).
  double listX = 0, listY = 0, listW = 0;
  double rowPitch = 64.0;
  // Footer.
  double footerY = 0, footerH = 52.0;
  // Scrollable content bottom (grid/list clip end).
  double contentBottom = 0;
};

inline constexpr double kTahoeMar = 20.0;
inline constexpr double kTahoeSearchH = 46.0;
inline constexpr double kTahoePillH = 30.0;
inline constexpr double kTahoePillGap = 6.0;
inline constexpr double kTahoePillFontPx = 12.0;
inline constexpr double kTahoePillPadX = 10.0;
inline constexpr double kTahoeGridGap = 8.0;
inline constexpr double kTahoeIconSz = 60.0;
inline constexpr double kTahoeLabelFontPx = 11.0;
inline constexpr double kTahoeFooterH = 40.0; // 32px buttons + 4px top/bottom, centered
inline constexpr double kTahoeFooterBottomMar = 8.0;
inline constexpr double kTahoeRadius = 24.0;
inline constexpr int kTahoeCols = 6;

inline TahoeLayout tahoe_layout(double W, double H) {
  TahoeLayout l;
  l.searchX = kTahoeMar;
  l.searchY = kTahoeMar;
  l.searchW = W - kTahoeMar * 2.0;
  l.searchH = kTahoeSearchH;
  const double sepY = l.searchY + l.searchH + 12.0;
  l.pillsY = sepY + 12.0;
  l.pillsAvailX = kTahoeMar;
  l.pillsAvailW = W - kTahoeMar * 2.0;
  l.gridX = kTahoeMar;
  l.gridY = l.pillsY + l.pillsH + 12.0;
  l.gridW = W - kTahoeMar * 2.0;
  l.cellW = (l.gridW - (kTahoeCols - 1) * kTahoeGridGap) / kTahoeCols;
  // Icon + gap + up to two label lines + bottom breathing room.
  l.cellH = kTahoeIconSz + 6.0 + kTahoeLabelFontPx * 2.0 + 12.0;
  l.listX = kTahoeMar;
  l.listY = l.gridY;
  l.listW = l.gridW;
  l.footerH = kTahoeFooterH;
  l.footerY = H - kTahoeFooterBottomMar - l.footerH;
  l.contentBottom = l.footerY - 4.0;
  return l;
}

inline double tahoe_grid_content_h(size_t nHits, const TahoeLayout& l) {
  const int rows = static_cast<int>((nHits + l.cols - 1) / l.cols);
  return rows * (l.cellH + kTahoeGridGap);
}

// Grid cell origin for hit index.
inline void tahoe_cell_xy(int idx, const TahoeLayout& l, double scrollPx, double& outX, double& outY) {
  const int col = idx % l.cols;
  const int row = idx / l.cols;
  outX = l.gridX + col * (l.cellW + kTahoeGridGap);
  outY = l.gridY + row * (l.cellH + kTahoeGridGap) - scrollPx;
}

// Pick a grid index from local coords. Returns -1 when outside content.
inline int tahoe_pick_grid(double lx, double ly, size_t nHits, const TahoeLayout& l, double scrollPx) {
  if (lx < l.gridX || lx >= l.gridX + l.gridW) return -1;
  const double relY = ly - l.gridY + scrollPx;
  if (relY < 0) return -1;
  const double rowH = l.cellH + kTahoeGridGap;
  const int row = static_cast<int>(std::floor(relY / rowH));
  const int col = static_cast<int>(std::floor((lx - l.gridX) / (l.cellW + kTahoeGridGap)));
  if (col < 0 || col >= l.cols) return -1;
  const int idx = row * l.cols + col;
  if (idx < 0 || static_cast<size_t>(idx) >= nHits) return -1;
  return idx;
}

// Pill row scroll offset, deterministic from selection so no extra state is
// needed: centered when everything fits, otherwise shifted to show the
// selected pill.
inline double tahoe_pills_xoff(double totalW, double availW, double selX, double selW) {
  if (totalW <= availW) return (availW - totalW) * 0.5;
  double off = selX + selW * 0.5 - availW * 0.5;
  if (off < 0) off = 0;
  if (off > totalW - availW) off = totalW - availW;
  return -off;
}

// Pill rects for n entries (index 0 = All, 1.. = buckets). Selected pill
// index drives the scroll offset deterministically.
inline std::vector<double> tahoe_pill_xs(const std::vector<double>& widths, double availX, double availW,
                                         int selIdx) {
  const int n = static_cast<int>(widths.size());
  std::vector<double> xs(static_cast<size_t>(n), 0.0);
  if (n == 0) return xs;
  double total = 0;
  for (double w : widths) total += w + kTahoePillGap;
  total -= kTahoePillGap;
  double selX = 0, selW = widths[0];
  double x = 0;
  for (int i = 0; i < n; ++i) {
    if (i == selIdx) {
      selX = x;
      selW = widths[static_cast<size_t>(i)];
    }
    x += widths[static_cast<size_t>(i)] + kTahoePillGap;
  }
  const double off = tahoe_pills_xoff(total, availW, selX, selW);
  x = availX + off;
  for (int i = 0; i < n; ++i) {
    xs[static_cast<size_t>(i)] = x;
    x += widths[static_cast<size_t>(i)] + kTahoePillGap;
  }
  return xs;
}

// ---------------------------------------------------------------- paint helpers

inline void tahoe_rr(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_path(cr);
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
  cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

// Dark glass card + single hairline, same recipe as the desktop widget cards.
inline void tahoe_paint_card(cairo_t* cr, double W, double H, double fillR, double fillG, double fillB,
                             double alpha) {
  tahoe_rr(cr, 0, 0, W, H, kTahoeRadius);
  cairo_set_source_rgba(cr, fillR * 0.30, fillG * 0.30, fillB * 0.30, 0.92 * alpha);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

// Split a name into up to two centered lines. Returns 1 or 2 and fills
// line1/line2 (line2 empty when single). Splits at the space nearest the
// middle so "Microsoft OneNote" and "Progressive Downloader" wrap like Tahoe.
inline int tahoe_two_lines(const std::string& name, std::string& line1, std::string& line2) {
  line1 = name;
  line2.clear();
  const size_t n = name.size();
  if (n <= 16) return 1;
  size_t best = std::string::npos;
  size_t bestDist = n;
  for (size_t i = 0; i < n; ++i) {
    if (name[i] != ' ') continue;
    const size_t dist = (i > n / 2) ? (i - n / 2) : (n / 2 - i);
    if (dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  if (best == std::string::npos || best == 0 || best + 1 >= n) return 1;
  line1.assign(name, 0, best);
  line2.assign(name, best + 1, n - best - 1);
  if (line2.size() > 18) {
    line2.resize(17);
    line2 += "…";
  }
  return 2;
}

} // namespace eh::shell::tahoe
