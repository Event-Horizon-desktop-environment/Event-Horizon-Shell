#pragma once

#include <cairo/cairo.h>

namespace eh::config {
struct ChromePaintColors;
}

namespace eh::shell::dock::app_drawer {

enum class PowerConfirmPick : int {
  Outside = -1,
  Cancel = 0,
  Confirm = 1,
  CloseX = 2,
};

[[nodiscard]] PowerConfirmPick pick_power_confirm_modal(double popupW, double popupH, int pendingPowerIdx, double lx,
                                                        double ly);

void paint_power_confirm_modal(cairo_t* cr, double popupW, double popupH, int pendingPowerIdx, double pointerX,
                               double pointerY, const eh::config::ChromePaintColors& chrome);

}
