#include "desktop_shell/controlcenter/input/control_center_pear_hit.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/dock/core/dock_bar.h"

#include <algorithm>
#include <cmath>

namespace ccl = eh::shell::dock::control_center;

namespace {
bool in_rect(double px, double py, double x, double y, double w, double h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

ccl::PearLayout layout_for(const PearHitContext& ctx) {
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const ccl::PearCenterConfig cfg = ccl::pear_center_config(sc, ctx.widgetId);
  return ccl::cc_compute_pear_layout(ctx.popupW, const_cast<ccl::ControlCenterState&>(ctx.state),
                                    cfg, dock_ui_scale(sc.dock));
}
} // namespace

bool pear_network_card_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return false;
  return in_rect(px, py, L.netX, L.row1Y, L.netW, L.row1H);
}

bool pear_bluetooth_card_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return false;
  return in_rect(px, py, L.btX, L.row1Y, L.btW, L.row1H);
}

bool pear_settings_row_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return false;
  return in_rect(px, py, L.setX, L.row2Y, L.setW, L.row2H);
}

bool pear_dnd_card_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showDnd) return false;
  return in_rect(px, py, L.dndX, L.row2Y, L.dndW, L.row2H);
}

int pear_toggle_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return -1;
  for (size_t i = 0; i < L.toggles.size(); ++i) {
    if (in_rect(px, py, ccl::pear_toggle_x(L, i), L.togY, L.togW, L.togH)) return static_cast<int>(i);
  }
  return -1;
}

bool pear_volume_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showVolume) return false;
  const double tx = L.pad + ccl::kPearRowTrackPadX * L.us;
  const double tw = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  const double ty = L.volY + ccl::kPearRowTrackY * L.us;
  const double th = ccl::kPearRowTrackH * L.us;
  const double hx = tx - ccl::kPearRowHitPad * L.us;
  const double hy = ty - ccl::kPearRowHitPad * L.us;
  const double hw = tw + 2.0 * ccl::kPearRowHitPad * L.us;
  const double hh = th + 2.0 * ccl::kPearRowHitPad * L.us;
  if (!in_rect(px, py, hx, hy, hw, hh)) return false;
  if (out_t) *out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
  return true;
}

bool pear_volume_mute_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showVolume) return false;
  const double cx = L.pad + ccl::kPearRowIconCx * L.us;
  const double cy = L.volY + ccl::kPearRowIconCy * L.us;
  const double dx = px - cx, dy = py - cy;
  const double r = ccl::kPearRowIconR * L.us + 4.0;
  return dx * dx + dy * dy <= r * r;
}

bool pear_volume_row_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showVolume) return false;
  return in_rect(px, py, L.pad, L.volY, L.W - L.pad * 2.0, L.volH);
}

bool pear_output_device_row_hit(const PearHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return false;
  for (int i = 0; i < L.outDevRows; ++i) {
    const double ry = ccl::pear_dev_row_y(L.outDevY, i);
    if (in_rect(px, py, L.pad + 10.0 * L.us, ry + 6.0 * L.us, L.W - L.pad * 2.0 - 20.0 * L.us,
                24.0 * L.us)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool pear_input_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showInput) return false;
  const double tx = L.pad + ccl::kPearRowTrackPadX * L.us;
  const double tw = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  const double ty = L.inY + ccl::kPearRowTrackY * L.us;
  const double th = ccl::kPearRowTrackH * L.us;
  const double hx = tx - ccl::kPearRowHitPad * L.us;
  const double hy = ty - ccl::kPearRowHitPad * L.us;
  const double hw = tw + 2.0 * ccl::kPearRowHitPad * L.us;
  const double hh = th + 2.0 * ccl::kPearRowHitPad * L.us;
  if (!in_rect(px, py, hx, hy, hw, hh)) return false;
  if (out_t) *out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
  return true;
}

bool pear_input_mute_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showInput) return false;
  const double cx = L.pad + ccl::kPearRowIconCx * L.us;
  const double cy = L.inY + ccl::kPearRowIconCy * L.us;
  const double dx = px - cx, dy = py - cy;
  const double r = ccl::kPearRowIconR * L.us + 4.0;
  return dx * dx + dy * dy <= r * r;
}

bool pear_input_row_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showInput) return false;
  return in_rect(px, py, L.pad, L.inY, L.W - L.pad * 2.0, L.inH);
}

bool pear_input_device_row_hit(const PearHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay) return false;
  for (int i = 0; i < L.inDevRows; ++i) {
    const double ry = ccl::pear_dev_row_y(L.inDevY, i);
    if (in_rect(px, py, L.pad + 10.0 * L.us, ry + 6.0 * L.us, L.W - L.pad * 2.0 - 20.0 * L.us,
                24.0 * L.us)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool pear_brightness_slider_hit(const PearHitContext& ctx, double px, double py, double* out_t) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showBrightness) return false;
  const double tx = L.pad + ccl::kPearRowTrackPadX * L.us;
  const double tw = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  const double ty = L.briY + ccl::kPearRowTrackY * L.us;
  const double th = ccl::kPearRowTrackH * L.us;
  const double hx = tx - ccl::kPearRowHitPad * L.us;
  const double hy = ty - ccl::kPearRowHitPad * L.us;
  const double hw = tw + 2.0 * ccl::kPearRowHitPad * L.us;
  const double hh = th + 2.0 * ccl::kPearRowHitPad * L.us;
  if (!in_rect(px, py, hx, hy, hw, hh)) return false;
  if (out_t) *out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
  return true;
}

bool pear_volume_track_geom(const PearHitContext& ctx, double* out_x, double* out_w) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showVolume) return false;
  if (out_x) *out_x = L.pad + ccl::kPearRowTrackPadX * L.us;
  if (out_w)
    *out_w = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  return true;
}

bool pear_input_track_geom(const PearHitContext& ctx, double* out_x, double* out_w) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showInput) return false;
  if (out_x) *out_x = L.pad + ccl::kPearRowTrackPadX * L.us;
  if (out_w)
    *out_w = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  return true;
}

bool pear_brightness_track_geom(const PearHitContext& ctx, double* out_x, double* out_w) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showBrightness) return false;
  if (out_x) *out_x = L.pad + ccl::kPearRowTrackPadX * L.us;
  if (out_w)
    *out_w = L.W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * L.us;
  return true;
}

int pear_media_button_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (ctx.state.networksOverlay || !L.showMedia) return -1;
  const double cy = ccl::pear_media_btn_cy(L);
  for (int i = 0; i < 3; ++i) {
    const double cx = ccl::pear_media_btn_cx(L, i);
    const double dx = px - cx, dy = py - cy;
    const double r = 14.0 * L.us + 4.0;
    if (dx * dx + dy * dy <= r * r) return i;
  }
  return -1;
}

bool pear_networks_back_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (!ctx.state.networksOverlay) return false;
  return in_rect(px, py, L.pad, L.pad, 32.0 * L.us, 32.0 * L.us);
}

bool pear_wifi_toggle_hit(const PearHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  if (!ctx.state.networksOverlay) return false;
  return in_rect(px, py, L.W - L.pad - 30.0 * L.us, L.pad + 6.0 * L.us, 30.0 * L.us, 30.0 * L.us);
}

bool pear_network_row_hit(const PearHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (!ctx.state.networksOverlay) return false;
  for (int i = 0; i < L.wifiRows; ++i) {
    const double ry = L.pad + ccl::kPearNetHeaderH * L.us + static_cast<double>(i) * ccl::kPearButtonH * L.us;
    if (in_rect(px, py, L.pad + 10.0 * L.us, ry + 6.0 * L.us, L.W - L.pad * 2.0 - 20.0 * L.us,
                36.0 * L.us)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}
