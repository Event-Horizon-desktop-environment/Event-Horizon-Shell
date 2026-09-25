#pragma once

// PearCenter compact layout hit-testing. Mirrors control_center_pear_layout.hpp geometry;
// paint and dispatch both go through here so clicks land on pixels.

#include <cstdint>
#include <string>

namespace eh::shell::dock::control_center {
struct ControlCenterState;
struct PearLayout;
} // namespace eh::shell::dock::control_center

struct PearHitContext {
  double popupW;
  const eh::shell::dock::control_center::ControlCenterState& state;
  const std::string& widgetId;
};

[[nodiscard]] bool pear_network_card_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_bluetooth_card_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_settings_row_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_dnd_card_hit(const PearHitContext& ctx, double px, double py);
// Toggle index (into PearLayout::toggles) or -1.
[[nodiscard]] int pear_toggle_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_volume_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t);
[[nodiscard]] bool pear_volume_mute_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_volume_row_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_output_device_row_hit(const PearHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool pear_input_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t);
[[nodiscard]] bool pear_input_mute_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_input_row_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_input_device_row_hit(const PearHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool pear_brightness_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t);
// Media button 0/1/2 or -1.
[[nodiscard]] int pear_media_button_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_networks_back_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_wifi_toggle_hit(const PearHitContext& ctx, double px, double py);
[[nodiscard]] bool pear_network_row_hit(const PearHitContext& ctx, double px, double py, int* out_index);
// Track x/w for drag motion (volume / input / brightness rows).
[[nodiscard]] bool pear_volume_track_geom(const PearHitContext& ctx, double* out_x, double* out_w);
[[nodiscard]] bool pear_input_track_geom(const PearHitContext& ctx, double* out_x, double* out_w);
[[nodiscard]] bool pear_brightness_track_geom(const PearHitContext& ctx, double* out_x, double* out_w);
