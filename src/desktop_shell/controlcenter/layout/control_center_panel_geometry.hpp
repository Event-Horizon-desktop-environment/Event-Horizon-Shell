#pragma once

#include <cstdint>

struct DockApp;
namespace eh::shell::dock::control_center { struct ControlCenterState; }

[[nodiscard]] double control_center_popup_height();
[[nodiscard]] double control_center_popup_height(const DockApp& app);

// New layout position helpers (computed at runtime with current scale)
[[nodiscard]] double cc_layout_pad();
[[nodiscard]] double cc_layout_status_bar_y();
[[nodiscard]] double cc_layout_status_bar_h();
[[nodiscard]] double cc_layout_grid_y();
[[nodiscard]] double cc_layout_grid_tile_h();
[[nodiscard]] double cc_layout_grid_col_w(double popupW);
[[nodiscard]] double cc_layout_audio_card_y();
[[nodiscard]] double cc_layout_audio_card_h();
[[nodiscard]] double cc_layout_audio_card_w(double popupW);
[[nodiscard]] double cc_layout_mixer_y();
[[nodiscard]] double cc_layout_mixer_h();
[[nodiscard]] double cc_layout_media_y();
[[nodiscard]] double cc_layout_card_h();
[[nodiscard]] double cc_layout_row_gap();

// Panel height and position helpers
[[nodiscard]] double control_center_network_offset_y(const eh::shell::dock::control_center::ControlCenterState& state);
[[nodiscard]] double control_center_output_devices_panel_h(const eh::shell::dock::control_center::ControlCenterState& state);
[[nodiscard]] double control_center_input_devices_panel_h(const eh::shell::dock::control_center::ControlCenterState& state);
[[nodiscard]] double control_center_mixer_card_y(const eh::shell::dock::control_center::ControlCenterState& state, uint64_t nowMs, int popupW, int popupH);

// Animation helper
[[nodiscard]] inline double cc_anim_panel_h(uint64_t nowMs, uint64_t startMs, bool from, bool to, double fullH) {
  const auto elapsedMs = nowMs - startMs;
  if (elapsedMs >= 200) return to ? fullH : 0.0;
  const double t = static_cast<double>(elapsedMs) / 200.0;
  const double eased = t * t * (3.0 - 2.0 * t);  // Ease-in-out cubic
  return from ? (fullH * (1.0 - eased)) : (fullH * eased);
}
