#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"

#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/input/dock_position.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
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

int control_center_popup_width_for(const eh::config::ShellConfig& sc, const std::string& widgetId) {
  namespace ccl = eh::shell::dock::control_center;
  const double us = dock_ui_scale(sc.dock);
  if (ccl::pear_layout_enabled(sc, widgetId)) return static_cast<int>(360.0 * us);
  return static_cast<int>(620.0 * us);
}

double control_center_popup_height() {
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  if (eh::shell::dock::control_center::pear_layout_enabled(sc, "control_center")) {
    eh::shell::dock::control_center::ControlCenterState def{};
    const auto cfg = eh::shell::dock::control_center::pear_center_config(sc, "control_center");
    return eh::shell::dock::control_center::cc_compute_pear_layout(
               static_cast<double>(eh::shell::dock::kControlCenterPopupW()), def, cfg,
               dock_ui_scale(sc.dock))
        .totalH;
  }
  eh::shell::dock::control_center::ControlCenterState def{};
  return eh::shell::dock::control_center::cc_compute_layout(
             static_cast<double>(eh::shell::dock::kControlCenterPopupW()), def,
             eh::shell::now_mono_ms())
      .totalH;
}

double control_center_popup_height(const DockApp& app) {
  namespace ccl = eh::shell::dock::control_center;
  const double W = static_cast<double>(app.popupW > 0 ? app.popupW : eh::shell::dock::kControlCenterPopupW());
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  // Size with animations resolved to their targets so the surface is born at
  // its resting height; paint/hit keep using the interpolated (animated) form.
  int wantH;
  const std::string wid = dock_active_control_center_widget_id(app);
  const std::string& widRef = wid.empty() ? std::string("control_center") : wid;
  if (ccl::pear_layout_enabled(sc, widRef)) {
    const auto cfg = ccl::pear_center_config(sc, widRef);
    wantH = static_cast<int>(std::ceil(
        ccl::cc_compute_pear_layout(W, const_cast<ccl::ControlCenterState&>(app.ccState), cfg,
                                   dock_ui_scale(sc.dock))
            .totalH));
  } else {
    wantH = static_cast<int>(std::ceil(
        ccl::cc_compute_layout(W, const_cast<ccl::ControlCenterState&>(app.ccState),
                               eh::shell::now_mono_ms(), true)
            .totalH));
  }
  // Clamp so fully-expanded content never runs under the dock: reserve the
  // bottom clearance plus a breathing row at the top.
  if (DockOutputLayer* L = dock_popup_margin_reference_layer(const_cast<DockApp&>(app))) {
    for (const auto& u : app.outputSlots) {
      if (u && u->output == L->wlOut && u->logical_h > 0) {
        int marginBottom = 0, marginLeft = 0;
        if (dock_popup_compute_layer_margins(const_cast<DockApp&>(app), L, app.popupAnchorX,
                                             &marginLeft, &marginBottom))
          wantH = std::min(wantH, u->logical_h - marginBottom - 12);
        break;
      }
    }
  }
  return static_cast<double>(std::max(200, wantH));
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
