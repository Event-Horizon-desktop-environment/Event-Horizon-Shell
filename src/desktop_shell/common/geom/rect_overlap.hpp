#pragma once

namespace eh::shell::geom {

[[nodiscard]] inline bool rects_overlap(double ax, double ay, double aw, double ah, double bx, double by, double bw,
                                       double bh) {
  return ax < (bx + bw) && (ax + aw) > bx && ay < (by + bh) && (ay + ah) > by;
}

}
