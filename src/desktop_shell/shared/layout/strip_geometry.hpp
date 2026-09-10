#pragma once

#include <algorithm>

namespace eh::shell::shared {

inline constexpr double kLrSectionGapPx = 4.0;

struct LrStripLayout {
  bool side_by_side = false;
  double x_left = 0;
  double x_right = 0;
};

inline LrStripLayout lr_strip_layout(double pill_x, double pill_box_w, double strip_inner_full,
                                     double left_total_w, double center_total_w, double right_total_w,
                                     double section_gap_px) {
  LrStripLayout L{};
  if (center_total_w > 1e-6) return L;
  const double halfStrip = strip_inner_full * 0.5;
  const double min_x = pill_x + halfStrip;
  const double max_r = pill_x + pill_box_w - halfStrip;
  const double need = left_total_w + section_gap_px + right_total_w;
  if (need > max_r - min_x + 1e-9) return L;
  const double right_start = max_r - right_total_w;
  const double ideal_left = right_start - left_total_w - section_gap_px;
  if (ideal_left < min_x - 1e-9) return L;
  L.side_by_side = true;
  L.x_left = ideal_left;
  L.x_right = right_start;
  return L;
}

inline double widget_strip_h_scale(double box_w, double total_w, double inner_margin_px) {
  const double avail = std::max(1.0, box_w - inner_margin_px);
  if (total_w <= avail) return 1.0;
  return avail / total_w;
}

inline double strip_surface_x_to_layout(double sx, double mid_x, double h_scale) {
  return mid_x + (sx - mid_x) / h_scale;
}

inline double strip_layout_x_to_surface(double lx, double mid_x, double h_scale) {
  return mid_x + (lx - mid_x) * h_scale;
}

}
