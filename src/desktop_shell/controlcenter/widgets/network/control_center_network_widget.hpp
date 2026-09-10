#pragma once

#include <string>
#include <vector>

#include <cairo/cairo.h>

namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::dock::control_center {

struct ControlCenterNetworkState {
  bool connected = false;
  bool ethernet = false;
  bool wifi = false;
  std::string iface{};
  std::string ssid{};
  int wifi_signal_pct = -1;
  std::string status_text{};
};

struct ControlCenterWifiAp {
  bool active = false;
  std::string ssid{};
  int signal_pct = -1;
  std::string security{};
  bool needs_password = true;
};

[[nodiscard]] ControlCenterNetworkState control_center_network_state();
[[nodiscard]] std::vector<ControlCenterWifiAp> control_center_wifi_scan(bool rescan);
bool control_center_wifi_connect(const std::string& ssid, const std::string& password, std::string* out_error);
void paint_control_center_network_card(cairo_t* cr, double x, double y, double w, double h,
                                       const ControlCenterNetworkState& ns, double inner_glass_scale);

} // namespace eh::shell::dock::control_center
