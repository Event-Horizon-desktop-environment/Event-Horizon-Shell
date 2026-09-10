#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <cmath>

double cc_layout_pad() {
  return 18.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_row_gap() {
  return 12.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_status_bar_y() {
  return cc_layout_pad();
}

double cc_layout_status_bar_h() {
  return 28.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_grid_y() {
  return cc_layout_status_bar_y() + cc_layout_status_bar_h() + cc_layout_row_gap();
}

double cc_layout_grid_tile_h() {
  return 80.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_grid_col_w(double popupW) {
  const double gap = cc_layout_row_gap();
  return (popupW - 2.0 * cc_layout_pad() - gap) / 2.0;
}

double cc_layout_audio_card_y() {
  return cc_layout_grid_y() + cc_layout_grid_tile_h() * 2.0 + cc_layout_row_gap() * 2.0;
}

double cc_layout_audio_card_h() {
  return 92.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_audio_card_w(double popupW) {
  return (popupW - 3.0 * cc_layout_pad()) / 2.0;
}

double cc_layout_mixer_y() {
  return cc_layout_audio_card_y() + cc_layout_audio_card_h() + cc_layout_row_gap();
}

double cc_layout_mixer_h() {
  return 64.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double cc_layout_media_y() {
  return cc_layout_mixer_y() + cc_layout_mixer_h() + cc_layout_row_gap();
}

double cc_layout_card_h() {
  return 92.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

static double compute_popup_height() {
  return cc_layout_media_y() + cc_layout_card_h() + cc_layout_row_gap()
       + cc_layout_card_h() + cc_layout_pad();
}

double control_center_popup_height() {
  return compute_popup_height();
}

double control_center_popup_height(const DockApp& /*app*/) {
  return compute_popup_height();
}

double control_center_network_offset_y(const eh::shell::dock::control_center::ControlCenterState& /*state*/) {
  return cc_layout_grid_y() + cc_layout_grid_tile_h() + cc_layout_row_gap();
}

double control_center_output_devices_panel_h(const eh::shell::dock::control_center::ControlCenterState& /*state*/) {
  return 92.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double control_center_input_devices_panel_h(const eh::shell::dock::control_center::ControlCenterState& /*state*/) {
  return 92.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock);
}

double control_center_mixer_card_y(const eh::shell::dock::control_center::ControlCenterState& /*state*/, uint64_t /*nowMs*/, int /*popupW*/, int /*popupH*/) {
  return cc_layout_mixer_y();
}
