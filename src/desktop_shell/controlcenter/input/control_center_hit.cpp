#include "desktop_shell/controlcenter/input/control_center_hit.hpp"

#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cairo/cairo.h>

using eh::shell::dock::control_center::ControlCenterActiveModal;

namespace {

static void mixer_slider_xw(const std::string& name, double popupW,
                             double& out_x, double& out_w) {
  
  static cairo_surface_t* _surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  static cairo_t* _cr = cairo_create(_surf);
  cairo_select_font_face(_cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(_cr, 12.0);
  cairo_text_extents_t te;
  cairo_text_extents(_cr, name.c_str(), &te);
  const double nameEnd = eh::shell::cc_slider::kMixerNameLeft + te.x_advance;
  out_x = nameEnd;
  out_w = std::max(60.0, popupW - 28.0 - nameEnd);
}

using eh::shell::cc_slider::kAudioHitPadH;
using eh::shell::cc_slider::kAudioHitPadV;
using eh::shell::cc_slider::kAudioTrackH;
using eh::shell::cc_slider::kAudioTrackXPad;
using eh::shell::cc_slider::kAudioTrackYFromCardTop;
using eh::shell::cc_slider::kMixerHitPadH;
using eh::shell::cc_slider::kMixerHitPadV;
using eh::shell::cc_slider::kMixerTrackH;
using eh::shell::cc_slider::kMixerHeaderH;
using eh::shell::cc_slider::kMixerSectionH;
using eh::shell::cc_slider::kMixerRowH;
using eh::shell::cc_slider::kMixerMoreH;
using eh::shell::cc_slider::kMixerSliderCY;
}

// ═══════════════════════════════════════════════════════════════
//  New layout helpers (mirror cc_layout_* from geometry)
// ═══════════════════════════════════════════════════════════════

static double row_h() { return 34.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
static double header_h() { return 32.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }

// Modal content area: covers the main content zone (below grid start)
// with a small internal padding.
static double modal_content_y() {
  return cc_layout_grid_y();
}

static double modal_content_inner_y() {
  return modal_content_y() + cc_layout_row_gap();
}

static double modal_row_y(int index) {
  return modal_content_inner_y() + header_h() + static_cast<double>(index) * row_h();
}

// ═══════════════════════════════════════════════════════════════
//  Grid card hits (2×2, row-major)
// ═══════════════════════════════════════════════════════════════

bool control_center_network_card_hit(const CcHitContext& ctx, double px, double py) {
  const double x = cc_layout_pad();
  const double y = cc_layout_grid_y();
  const double w = cc_layout_grid_col_w(ctx.popupW);
  const double h = cc_layout_grid_tile_h();
  return (px >= x && px <= x + w && py >= y && py <= y + h);
}

bool control_center_bluetooth_card_hit(const CcHitContext& ctx, double px, double py) {
  const double x = cc_layout_pad() + cc_layout_grid_col_w(ctx.popupW) + cc_layout_row_gap();
  const double y = cc_layout_grid_y();
  const double w = cc_layout_grid_col_w(ctx.popupW);
  const double h = cc_layout_grid_tile_h();
  return (px >= x && px <= x + w && py >= y && py <= y + h);
}

bool control_center_audio_card_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py) {
  const double row2Y = cc_layout_grid_y() + cc_layout_grid_tile_h() + cc_layout_row_gap();
  const double x = (kind == CcAudioSliderKind::Output)
                       ? cc_layout_pad()
                       : (cc_layout_pad() + cc_layout_grid_col_w(ctx.popupW) + cc_layout_row_gap());
  const double y = row2Y;
  const double w = cc_layout_grid_col_w(ctx.popupW);
  const double h = cc_layout_grid_tile_h();
  return (px >= x && px <= x + w && py >= y && py <= y + h);
}

// ═══════════════════════════════════════════════════════════════
//  Audio slider and mute-icon hits (side-by-side cards below grid)
// ═══════════════════════════════════════════════════════════════

bool control_center_audio_slider_layout(const CcHitContext& ctx, CcAudioSliderKind kind, CcAudioSliderLayout* out) {
  
  if (!out) return false;
  const double cardW = cc_layout_audio_card_w(ctx.popupW);
  const double cardX = (kind == CcAudioSliderKind::Output)
                           ? cc_layout_pad()
                           : (cc_layout_pad() + cardW + cc_layout_pad());
  const double cardY = cc_layout_audio_card_y();
  out->track_x = cardX + kAudioTrackXPad;
  out->track_y = cardY + kAudioTrackYFromCardTop;
  out->track_w = std::max(1.0, cardW - 2.0 * kAudioTrackXPad);
  out->track_h = kAudioTrackH;
  out->hit_x = out->track_x - kAudioHitPadH;
  out->hit_y = out->track_y - kAudioHitPadV;
  out->hit_w = out->track_w + 2.0 * kAudioHitPadH;
  out->hit_h = out->track_h + 2.0 * kAudioHitPadV;
  return true;
}

bool control_center_audio_slider_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py, double* out_t) {
  
  CcAudioSliderLayout L{};
  if (!control_center_audio_slider_layout(ctx, kind, &L)) return false;
  if (!(px >= L.hit_x && px <= L.hit_x + L.hit_w && py >= L.hit_y && py <= L.hit_y + L.hit_h)) return false;
  const double t = std::clamp((px - L.track_x) / std::max(1.0, L.track_w), 0.0, 1.0);
  if (out_t) *out_t = t;
  return true;
}

bool control_center_audio_mute_icon_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py) {
  
  const double cardW = cc_layout_audio_card_w(ctx.popupW);
  const double cardX = (kind == CcAudioSliderKind::Output)
                           ? cc_layout_pad()
                           : (cc_layout_pad() + cardW + cc_layout_pad());
  const double cardY = cc_layout_audio_card_y();
  const double cx = cardX + 24.0;
  const double cy = cardY + 28.0;
  const double bw = 28.0, bh = 28.0;
  const double bx = cx - bw * 0.5;
  const double by = cy - bh * 0.5;
  return (px >= bx && px <= bx + bw && py >= by && py <= by + bh);
}

// ═══════════════════════════════════════════════════════════════
//  Modal row hits (network, bluetooth, output, input)
// ═══════════════════════════════════════════════════════════════

bool control_center_network_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  if (ctx.state.activeModal != ControlCenterActiveModal::Network) return false;
  constexpr int kMaxRows = 8;
  const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
  const int maxRows = std::min(kMaxRows, static_cast<int>(aps.size()));
  const double x = cc_layout_pad();
  const double w = ctx.popupW - cc_layout_pad() * 2.0;
  for (int i = 0; i < maxRows; ++i) {
    const double y = modal_row_y(i);
    if (px >= x && px <= x + w && py >= y && py <= y + row_h()) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool control_center_output_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  if (ctx.state.activeModal != ControlCenterActiveModal::AudioOutput) return false;
  constexpr int kMaxDeviceRows = 12;
  const int maxRows = std::min(kMaxDeviceRows, static_cast<int>(eh::shell::dock_slot_hooks::control_center_output_devices().size()));
  const double x = cc_layout_pad();
  const double w = ctx.popupW - cc_layout_pad() * 2.0;
  for (int i = 0; i < maxRows; ++i) {
    const double y = modal_row_y(i);
    if (px >= x && px <= x + w && py >= y && py <= y + row_h()) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool control_center_input_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  if (ctx.state.activeModal != ControlCenterActiveModal::AudioInput) return false;
  constexpr int kMaxDeviceRows = 12;
  const int maxRows = std::min(kMaxDeviceRows, static_cast<int>(eh::shell::dock_slot_hooks::control_center_input_devices().size()));
  const double x = cc_layout_pad();
  const double w = ctx.popupW - cc_layout_pad() * 2.0;
  for (int i = 0; i < maxRows; ++i) {
    const double y = modal_row_y(i);
    if (px >= x && px <= x + w && py >= y && py <= y + row_h()) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

static bool bt_row_positions(const CcHitContext& /*ctx*/, int* out_count, double* out_panelY) {
  constexpr int kMaxRows = 8;
  const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
  const int maxRows = std::min(kMaxRows, static_cast<int>(devs.size()));
  if (out_count) *out_count = maxRows;
  if (out_panelY) *out_panelY = modal_content_inner_y();
  return maxRows > 0;
}

bool control_center_bluetooth_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  if (ctx.state.activeModal != ControlCenterActiveModal::Bluetooth) return false;
  int maxRows = 0;
  double panelY = 0.0;
  if (!bt_row_positions(ctx, &maxRows, &panelY)) return false;
  const double pad = cc_layout_pad();
  const double W = ctx.popupW;
  const double rightEdge = W - pad;
  const double x = pad;
  const double w = W - pad * 2.0;
  for (int i = 0; i < maxRows; ++i) {
    const double ry = modal_row_y(i);
    if (!(px >= x && px <= x + w && py >= ry && py <= ry + row_h())) continue;
    const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
    if (i >= static_cast<int>(devs.size())) return false;
    const auto& d = devs[static_cast<size_t>(i)];
    const double actionBtnW = d.connected ? 74.0 : (d.paired ? 60.0 : 40.0);
    const bool showForget = d.paired;
    const double forgetBtnW = 48.0;
    const double btnGap = 6.0;
    const double btnAreaW = actionBtnW + (showForget ? (btnGap + forgetBtnW) : 0.0);
    const double btnStartX = rightEdge - btnAreaW - 8.0;
    if (px >= x && px < btnStartX) {
      if (out_index) *out_index = i;
      return true;
    }
    if (px >= btnStartX && px < btnStartX + actionBtnW) {
      if (out_index) *out_index = i;
      return true;
    }
    return false;
  }
  return false;
}

bool control_center_bluetooth_row_forget_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  if (ctx.state.activeModal != ControlCenterActiveModal::Bluetooth) return false;
  int maxRows = 0;
  double panelY = 0.0;
  if (!bt_row_positions(ctx, &maxRows, &panelY)) return false;
  const double pad = cc_layout_pad();
  const double W = ctx.popupW;
  const double rightEdge = W - pad;
  const double x = pad;
  const double w = W - pad * 2.0;
  for (int i = 0; i < maxRows; ++i) {
    const double ry = modal_row_y(i);
    if (!(px >= x && px <= x + w && py >= ry && py <= ry + row_h())) continue;
    const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
    if (i >= static_cast<int>(devs.size())) return false;
    const auto& d = devs[static_cast<size_t>(i)];
    if (!d.paired) return false;
    const double actionBtnW = d.connected ? 74.0 : 60.0;
    const double forgetBtnW = 48.0;
    const double btnGap = 6.0;
    const double btnAreaW = actionBtnW + btnGap + forgetBtnW;
    const double btnStartX = rightEdge - btnAreaW - 8.0;
    const double forgetX = btnStartX + actionBtnW + btnGap;
    if (px >= forgetX && px < forgetX + forgetBtnW) {
      if (out_index) *out_index = i;
      return true;
    }
    return false;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════
//  Modal backdrop & close hits
// ═══════════════════════════════════════════════════════════════

bool control_center_modal_backdrop_hit(const CcHitContext& ctx, double px, double py) {
  if (ctx.state.activeModal == ControlCenterActiveModal::None) return false;
  // Backdrop is the entire popup area; dispatch checks modal-specific
  // content hits first, so any click in the popup while modal is open
  // that isn't on a close button or row falls through to backdrop.
  const double pad = cc_layout_pad();
  const double modalH = ctx.popupH - modal_content_y() - pad;
  const double mx = pad;
  const double my = modal_content_y();
  const double mw = ctx.popupW - pad * 2.0;
  const double mh = modalH;
  return (px >= mx && px <= mx + mw && py >= my && py <= my + mh);
}

bool control_center_modal_close_hit(const CcHitContext& ctx, double px, double py) {
  if (ctx.state.activeModal == ControlCenterActiveModal::None) return false;
  const double pad = cc_layout_pad();
  const double btnSize = 28.0;
  const double x = ctx.popupW - pad - btnSize;
  const double y = modal_content_y() + 4.0;
  return (px >= x && px <= x + btnSize && py >= y && py <= y + btnSize);
}

// ═══════════════════════════════════════════════════════════════
//  Mixer hits
// ═══════════════════════════════════════════════════════════════

bool control_center_mixer_slider_hit(const CcHitContext& ctx, int row, double px, double py, double* out_t) {
  
  if (row < 0) return false;
  const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  if (static_cast<size_t>(row) >= streams.size()) return false;
  const double ry = cc_layout_mixer_y() + kMixerHeaderH + static_cast<double>(row) * kMixerRowH;
  const double sliderCY = ry + kMixerSliderCY;
  double track_x, track_w;
  mixer_slider_xw(streams[static_cast<size_t>(row)].app_name, ctx.popupW, track_x, track_w);
  const double track_y = sliderCY - kMixerTrackH * 0.5;
  const double hx = track_x - kMixerHitPadH;
  const double hy = track_y - kMixerHitPadV;
  const double hw = track_w + 2.0 * kMixerHitPadH;
  const double hh = kMixerTrackH + 2.0 * kMixerHitPadV;
  if (!(px >= hx && px <= hx + hw && py >= hy && py <= hy + hh)) return false;
  const double t = std::clamp((px - track_x) / std::max(1.0, track_w), 0.0, 1.0);
  if (out_t) *out_t = t;
  return true;
}

bool control_center_mixer_expanded_slider_hit(const CcHitContext& ctx, double px, double py, int* out_stream_id, bool* out_is_input,
                                                    double* out_sx, double* out_sw, double* out_t) {
  
  if (ctx.state.activeModal != ControlCenterActiveModal::Mixer) return false;
  const double mixerY = cc_layout_mixer_y();
  const auto outStreams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams();
  const int outRows = std::min(4, static_cast<int>(outStreams.size()));
  const int inRows = std::min(3, static_cast<int>(inStreams.size()));

  const auto check_row = [&](const auto& streams, double rowY, int idx, bool isInput) -> bool {
    double rtrack_x, rtrack_w;
    mixer_slider_xw(streams[static_cast<size_t>(idx)].app_name, ctx.popupW, rtrack_x, rtrack_w);
    const double sliderCY = rowY + kMixerSliderCY;
    const double track_y = sliderCY - kMixerTrackH * 0.5;
    const double hit_x = rtrack_x - kMixerHitPadH;
    const double hit_y = track_y - kMixerHitPadV;
    const double hit_w = rtrack_w + 2.0 * kMixerHitPadH;
    const double hit_h = kMixerTrackH + 2.0 * kMixerHitPadV;
    if (px >= hit_x && px <= hit_x + hit_w && py >= hit_y && py <= hit_y + hit_h) {
      if (out_stream_id) *out_stream_id = streams[static_cast<size_t>(idx)].sink_input_id;
      if (out_is_input) *out_is_input = isInput;
      if (out_sx) *out_sx = rtrack_x;
      if (out_sw) *out_sw = rtrack_w;
      if (out_t) *out_t = std::clamp((px - rtrack_x) / std::max(1.0, rtrack_w), 0.0, 1.0);
      return true;
    }
    return false;
  };

  double ry = mixerY + kMixerHeaderH;
  if (outRows > 0) ry += kMixerSectionH;
  for (int i = 0; i < outRows; ++i, ry += kMixerRowH) {
    if (check_row(outStreams, ry, i, false)) return true;
  }
  if (inRows > 0) ry += kMixerSectionH;
  for (int i = 0; i < inRows; ++i, ry += kMixerRowH) {
    if (check_row(inStreams, ry, i, true)) return true;
  }
  return false;
}

bool control_center_mixer_settings_hit(const CcHitContext& ctx, double px, double py) {
  
  const double mixerY = cc_layout_mixer_y();
  const double x = cc_layout_pad();
  const double w = ctx.popupW - cc_layout_pad() * 2.0;
  return (px >= x && px <= x + w && py >= mixerY && py <= mixerY + kMixerHeaderH);
}

// ═══════════════════════════════════════════════════════════════
//  Media and weather card hits
// ═══════════════════════════════════════════════════════════════

double control_center_media_card_y(const CcHitContext& ctx) {
  
  (void)ctx;
  return cc_layout_media_y();
}

int control_center_media_button_hit(const CcHitContext& ctx, double px, double py) {
  
  const double pad = cc_layout_pad();
  const double cardX = pad;
  const double cardW = ctx.popupW - pad * 2.0;
  constexpr double cardH = 92.0;
  const double cardY = control_center_media_card_y(ctx);
  if (!(px >= cardX && px <= cardX + cardW && py >= cardY && py <= cardY + cardH)) return -1;

  const double btnR = 14.0;
  const double btnGap = 10.0;
  const double cy = cardY + cardH - 24.0;
  const double cx2 = cardX + cardW - 22.0;
  const double cx1 = cx2 - (btnR * 2.0 + btnGap);
  const double cx0 = cx1 - (btnR * 2.0 + btnGap);

  auto hit = [&](double cx, double cy0) -> bool {
    const double dx = px - cx;
    const double dy = py - cy0;
    return (dx * dx + dy * dy) <= (btnR * btnR);
  };
  if (hit(cx0, cy)) return 0;
  if (hit(cx1, cy)) return 1;
  if (hit(cx2, cy)) return 2;
  return -1;
}

bool control_center_weather_card_hit(const CcHitContext& ctx, double px, double py) {
  
  const double pad = cc_layout_pad();
  const double cardW = ctx.popupW - pad * 2.0;
  const double cardH = 92.0;
  const double weatherY = control_center_media_card_y(ctx) + cardH + cc_layout_row_gap();
  return (px >= pad && px <= pad + cardW && py >= weatherY && py <= weatherY + cardH);
}

// ═══════════════════════════════════════════════════════════════
//  DockApp-based APIs (backward-compatible — forward to CcHitContext)
// ═══════════════════════════════════════════════════════════════

static CcHitContext dock_hit_ctx(const DockApp& app) {
  return CcHitContext{static_cast<double>(app.popupW), app.popupH, app.ccState};
}

bool control_center_audio_slider_layout(const DockApp& app, CcAudioSliderKind kind, CcAudioSliderLayout* out) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_audio_slider_layout(dock_hit_ctx(app), kind, out);
}

bool control_center_audio_slider_geom(const DockApp& app, CcAudioSliderKind kind, double* out_sx, double* out_sy,
                                             double* out_sw, double* out_sh) {
  CcAudioSliderLayout L{};
  if (!control_center_audio_slider_layout(app, kind, &L)) return false;
  if (out_sx) *out_sx = L.hit_x;
  if (out_sy) *out_sy = L.hit_y;
  if (out_sw) *out_sw = L.hit_w;
  if (out_sh) *out_sh = L.hit_h;
  return true;
}

bool control_center_audio_slider_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py, double* out_t) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_audio_slider_hit(dock_hit_ctx(app), kind, px, py, out_t);
}

bool control_center_audio_mute_icon_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_audio_mute_icon_hit(dock_hit_ctx(app), kind, px, py);
}

bool control_center_network_card_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_network_card_hit(dock_hit_ctx(app), px, py);
}

bool control_center_audio_card_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_audio_card_hit(dock_hit_ctx(app), kind, px, py);
}

bool control_center_network_row_hit(const DockApp& app, double px, double py, int* out_index) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_network_row_hit(dock_hit_ctx(app), px, py, out_index);
}

bool control_center_output_devices_row_hit(const DockApp& app, double px, double py, int* out_index) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_output_devices_row_hit(dock_hit_ctx(app), px, py, out_index);
}

bool control_center_input_devices_row_hit(const DockApp& app, double px, double py, int* out_index) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_input_devices_row_hit(dock_hit_ctx(app), px, py, out_index);
}

double control_center_audio_slider_value_from_x(const DockApp& app, CcAudioSliderKind kind, double px) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return 0.0;
  CcAudioSliderLayout L{};
  if (!control_center_audio_slider_layout(dock_hit_ctx(app), kind, &L)) return 0.0;
  return std::clamp((px - L.track_x) / std::max(1.0, L.track_w), 0.0, 1.0);
}

int control_center_audio_pct_from_x(const DockApp& app, CcAudioSliderKind kind, double px) {
  return std::clamp(static_cast<int>(std::lround(control_center_audio_slider_value_from_x(app, kind, px) * 100.0)), 0, 100);
}

bool control_center_mixer_slider_geom(const DockApp& app, int row, double* out_sx, double* out_sy, double* out_sw,
                                            double* out_sh) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  auto ctx = dock_hit_ctx(app);
  if (row < 0) return false;
  const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  if (static_cast<size_t>(row) >= streams.size()) return false;
  const double mixerY = cc_layout_mixer_y();
  const double ry = mixerY + kMixerHeaderH + static_cast<double>(row) * kMixerRowH;
  const double sliderCY = ry + kMixerSliderCY;
  double track_x, track_w;
  mixer_slider_xw(streams[static_cast<size_t>(row)].app_name, ctx.popupW, track_x, track_w);
  const double track_y = sliderCY - kMixerTrackH * 0.5;
  if (out_sx) *out_sx = track_x - kMixerHitPadH;
  if (out_sy) *out_sy = track_y - kMixerHitPadV;
  if (out_sw) *out_sw = track_w + 2.0 * kMixerHitPadH;
  if (out_sh) *out_sh = kMixerTrackH + 2.0 * kMixerHitPadV;
  return true;
}

bool control_center_mixer_slider_hit(const DockApp& app, int row, double px, double py, double* out_t) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_mixer_slider_hit(dock_hit_ctx(app), row, px, py, out_t);
}

bool control_center_mixer_expanded_slider_hit(const DockApp& app, double px, double py, int* out_stream_id, bool* out_is_input,
                                                    double* out_sx, double* out_sw, double* out_t) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_mixer_expanded_slider_hit(dock_hit_ctx(app), px, py, out_stream_id, out_is_input, out_sx, out_sw, out_t);
}

bool control_center_mixer_settings_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_mixer_settings_hit(dock_hit_ctx(app), px, py);
}

double control_center_media_card_y(const DockApp& app) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return 0.0;
  return control_center_media_card_y(dock_hit_ctx(app));
}

int control_center_media_button_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return -1;
  return control_center_media_button_hit(dock_hit_ctx(app), px, py);
}

bool control_center_weather_card_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_weather_card_hit(dock_hit_ctx(app), px, py);
}

bool control_center_bluetooth_card_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_bluetooth_card_hit(dock_hit_ctx(app), px, py);
}

bool control_center_bluetooth_row_hit(const DockApp& app, double px, double py, int* out_index) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_bluetooth_row_hit(dock_hit_ctx(app), px, py, out_index);
}

bool control_center_bluetooth_row_forget_hit(const DockApp& app, double px, double py, int* out_index) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_bluetooth_row_forget_hit(dock_hit_ctx(app), px, py, out_index);
}

bool control_center_modal_backdrop_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_modal_backdrop_hit(dock_hit_ctx(app), px, py);
}

bool control_center_modal_close_hit(const DockApp& app, double px, double py) {
  if (!(app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter)) return false;
  return control_center_modal_close_hit(dock_hit_ctx(app), px, py);
}
