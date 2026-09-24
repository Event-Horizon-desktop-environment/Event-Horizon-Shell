#pragma once

#include <string>

#include <cairo/cairo.h>

namespace eh::shell::dock::control_center {

struct ControlCenterBluetoothState {
  bool powered = false;
  bool connected = false;
  int paired_count = 0;
  int battery_pct = -1;  // lowest connected-device battery, -1 when unknown
  std::string status_text{};
};

[[nodiscard]] ControlCenterBluetoothState control_center_bluetooth_state();
void paint_control_center_bluetooth_card(cairo_t* cr, double x, double y, double w, double h,
                                         const ControlCenterBluetoothState& bs, double inner_glass_scale,
                                         double corner_radius = -1.0);  // <0 = auto radius

} // namespace eh::shell::dock::control_center
