#pragma once

#include <cairo/cairo.h>
#include <cstdint>

namespace eh::config {
struct ShellConfig;
}

namespace eh::power_confirm {

enum class Pick : int {
  None = -1,
  Cancel = 0,
  Confirm = 1,
  Close = 2,
};

struct CardGeom {
  double cx, cy, cw, ch;
  double closeX, closeY, closeW, closeH;
  double cancelX, cancelY, cancelW, cancelH;
  double confirmX, confirmY, confirmW, confirmH;
};

CardGeom layout(double W, double H, double us);

Pick pick(double W, double H, int powerIdx, double pointerX, double pointerY, double us);

void paint(cairo_t* cr, double W, double H, int powerIdx,
           double pointerX, double pointerY,
           int remainingSec,
           const eh::config::ShellConfig& sc);

}
