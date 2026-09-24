#include "desktop_shell/controlcenter/input/control_center_hit.hpp"

#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/common/time/mono_time.hpp"

#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cairo/cairo.h>

using eh::shell::dock::control_center::ControlCenterActiveModal;

namespace {
namespace ccl = eh::shell::dock::control_center;

static ccl::CcLayout layout_for(const CcHitContext& ctx) {
  ccl::CcTimer t("hit-layout", 2000);
  return ccl::cc_compute_layout(ctx.popupW,
                                const_cast<ccl::ControlCenterState&>(ctx.state),
                                eh::shell::now_mono_ms());
}

static void mixer_slider_xw(const std::string& name, double popupW,
                             double& out_x, double& out_w) {
  static cairo_surface_t* _surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  static cairo_t* _cr = cairo_create(_surf);
  cairo_select_font_face(_cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(_cr, 12.0);
  // Truncate exactly like paint does (same helper, same font, same budget)
  // so the measured slider origin matches the painted one.
  std::string shown;
  eh::shell::dock::spotlight_truncate_to_width(
      _cr, name, popupW - eh::shell::cc_slider::kMixerNameLeft - 68.0, &shown);
  cairo_text_extents_t te;
  cairo_text_extents(_cr, shown.c_str(), &te);
  const double nameEnd = eh::shell::cc_slider::kMixerNameLeft + te.x_advance + 10.0;
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

static bool in_rect(double px, double py, double x, double y, double w, double h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}
} // namespace

// ═══════════════════════════════════════════════════════════════
//  Grid card hits (positions come from the shared layout)
// ═══════════════════════════════════════════════════════════════

bool control_center_network_card_hit(const CcHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  return in_rect(px, py, L.netX, L.gridY, L.tileW, L.tileH);
}

bool control_center_bluetooth_card_hit(const CcHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  return in_rect(px, py, L.btX, L.gridY, L.tileW, L.tileH);
}

bool control_center_audio_card_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py) {
  const auto L = layout_for(ctx);
  const double x = (kind == CcAudioSliderKind::Output) ? L.pad : (L.pad + L.tileW + L.gap);
  return in_rect(px, py, x, L.audioY, L.tileW, L.audioH);
}

// ═══════════════════════════════════════════════════════════════
//  Audio slider and mute-icon hits
// ═══════════════════════════════════════════════════════════════

bool control_center_audio_slider_layout(const CcHitContext& ctx, CcAudioSliderKind kind, CcAudioSliderLayout* out) {
  if (!out) return false;
  const auto L = layout_for(ctx);
  const double cardX = (kind == CcAudioSliderKind::Output) ? L.pad : (L.pad + L.tileW + L.gap);
  const double cardY = L.audioY;
  out->track_x = cardX + kAudioTrackXPad;
  out->track_y = cardY + kAudioTrackYFromCardTop;
  out->track_w = std::max(1.0, L.tileW - 2.0 * kAudioTrackXPad);
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
  if (!in_rect(px, py, L.hit_x, L.hit_y, L.hit_w, L.hit_h)) return false;
  const double t = std::clamp((px - L.track_x) / std::max(1.0, L.track_w), 0.0, 1.0);
  if (out_t) *out_t = t;
  return true;
}

bool control_center_audio_mute_icon_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py) {
  const auto L = layout_for(ctx);
  const double cardX = (kind == CcAudioSliderKind::Output) ? L.pad : (L.pad + L.tileW + L.gap);
  const double cx = cardX + 24.0;
  const double cy = L.audioY + 28.0;
  return in_rect(px, py, cx - 14.0, cy - 14.0, 28.0, 28.0);
}

// ═══════════════════════════════════════════════════════════════
//  Panel row hits (gated on expansion, not modals)
// ═══════════════════════════════════════════════════════════════

bool control_center_network_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (L.netH <= 2.0) return false;
  for (int i = 0; i < L.netRows; ++i) {
    if (in_rect(px, py, L.pad, L.netY + L.netHeaderH + static_cast<double>(i) * 34.0, L.W - L.pad * 2.0, 34.0)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool control_center_output_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (L.outDevH <= 2.0) return false;
  for (int i = 0; i < L.outDevRows; ++i) {
    if (in_rect(px, py, L.pad, ccl::cc_panel_row_y(L.outDevY, i), L.W - L.pad * 2.0, 34.0)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool control_center_input_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (L.inDevH <= 2.0) return false;
  for (int i = 0; i < L.inDevRows; ++i) {
    if (in_rect(px, py, L.pad, ccl::cc_panel_row_y(L.inDevY, i), L.W - L.pad * 2.0, 34.0)) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

bool control_center_bluetooth_row_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (L.btH <= 2.0) return false;
  const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
  const double rightEdge = L.W - L.pad - 10.0;
  for (int i = 0; i < L.btRows; ++i) {
    const double ry = ccl::cc_panel_row_y(L.btY, i);
    if (!in_rect(px, py, L.pad, ry, L.W - L.pad * 2.0, 34.0)) continue;
    if (i >= static_cast<int>(devs.size())) return false;
    const auto& d = devs[static_cast<size_t>(i)];
    const auto b = ccl::cc_bt_btns(d.connected, d.paired, rightEdge);
    if (b.showForget && px >= b.forgetX && px < b.forgetX + b.forgetW) return false;
    if (out_index) *out_index = i;
    return true;
  }
  return false;
}

bool control_center_bluetooth_row_forget_hit(const CcHitContext& ctx, double px, double py, int* out_index) {
  const auto L = layout_for(ctx);
  if (L.btH <= 2.0) return false;
  const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
  const double rightEdge = L.W - L.pad - 10.0;
  for (int i = 0; i < L.btRows; ++i) {
    const double ry = ccl::cc_panel_row_y(L.btY, i);
    if (!in_rect(px, py, L.pad, ry, L.W - L.pad * 2.0, 34.0)) continue;
    if (i >= static_cast<int>(devs.size())) return false;
    const auto& d = devs[static_cast<size_t>(i)];
    if (!d.paired) return false;
    const auto b = ccl::cc_bt_btns(d.connected, d.paired, rightEdge);
    if (b.showForget && px >= b.forgetX && px < b.forgetX + b.forgetW) {
      if (out_index) *out_index = i;
      return true;
    }
    return false;
  }
  return false;
}

// Modals no longer exist (panels expand inline) — kept for API compatibility.
bool control_center_modal_backdrop_hit(const CcHitContext&, double, double) { return false; }
bool control_center_modal_close_hit(const CcHitContext&, double, double) { return false; }

// ═══════════════════════════════════════════════════════════════
//  Mixer hits
// ═══════════════════════════════════════════════════════════════

bool control_center_mixer_slider_hit(const CcHitContext& ctx, int row, double px, double py, double* out_t) {
  if (row < 0) return false;
  const auto L = layout_for(ctx);
  const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  for (const auto& r : L.mixRows) {
    if (r.isInput || r.streamIdx != row) continue;
    if (static_cast<size_t>(row) >= streams.size()) return false;
    double tx, tw;
    mixer_slider_xw(streams[static_cast<size_t>(row)].app_name, ctx.popupW, tx, tw);
    const double track_y = r.y + kMixerSliderCY - kMixerTrackH * 0.5;
    if (!in_rect(px, py, tx - kMixerHitPadH, track_y - kMixerHitPadV,
                tw + 2.0 * kMixerHitPadH, kMixerTrackH + 2.0 * kMixerHitPadV))
      return false;
    if (out_t) *out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
    return true;
  }
  return false;
}

bool control_center_mixer_expanded_slider_hit(const CcHitContext& ctx, double px, double py, int* out_stream_id, bool* out_is_input,
                                              double* out_sx, double* out_sw, double* out_t) {
  const auto L = layout_for(ctx);
  if (!ctx.state.mixerExpanded) return false;
  const auto outStreams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams();
  for (const auto& r : L.mixRows) {
    const auto& vec = r.isInput ? inStreams : outStreams;
    if (r.streamIdx < 0 || static_cast<size_t>(r.streamIdx) >= vec.size()) continue;
    const auto& s = vec[static_cast<size_t>(r.streamIdx)];
    double tx, tw;
    mixer_slider_xw(s.app_name, ctx.popupW, tx, tw);
    const double track_y = r.y + kMixerSliderCY - kMixerTrackH * 0.5;
    if (!in_rect(px, py, tx - kMixerHitPadH, track_y - kMixerHitPadV,
                tw + 2.0 * kMixerHitPadH, kMixerTrackH + 2.0 * kMixerHitPadV))
      continue;
    if (out_stream_id) *out_stream_id = s.sink_input_id;
    if (out_is_input) *out_is_input = r.isInput;
    if (out_sx) *out_sx = tx;
    if (out_sw) *out_sw = tw;
    if (out_t) *out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
    return true;
  }
  return false;
}

bool control_center_mixer_settings_hit(const CcHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  return in_rect(px, py, L.pad, L.mixerY, L.W - L.pad * 2.0, kMixerHeaderH);
}

// ═══════════════════════════════════════════════════════════════
//  Media and weather card hits
// ═══════════════════════════════════════════════════════════════

double control_center_media_card_y(const CcHitContext& ctx) {
  return layout_for(ctx).mediaY;
}

int control_center_media_button_hit(const CcHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  constexpr double cardH = 92.0;
  if (!in_rect(px, py, L.pad, L.mediaY, L.W - L.pad * 2.0, cardH)) return -1;
  const double cy = ccl::cc_media_btn_cy(L.mediaY, cardH);
  auto hit = [&](int idx) -> bool {
    const double cx = ccl::cc_media_btn_cx(L.pad, L.W - L.pad * 2.0, idx);
    const double dx = px - cx, dy = py - cy;
    return (dx * dx + dy * dy) <= 14.0 * 14.0;
  };
  if (hit(0)) return 0;
  if (hit(1)) return 1;
  if (hit(2)) return 2;
  return -1;
}

bool control_center_weather_card_hit(const CcHitContext& ctx, double px, double py) {
  const auto L = layout_for(ctx);
  return in_rect(px, py, L.pad, L.weatherY, L.W - L.pad * 2.0, L.weatherH);
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
  const auto L = layout_for(ctx);
  for (const auto& r : L.mixRows) {
    if (r.isInput || r.streamIdx != row) continue;
    const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
    if (static_cast<size_t>(row) >= streams.size()) return false;
    double tx, tw;
    mixer_slider_xw(streams[static_cast<size_t>(row)].app_name, ctx.popupW, tx, tw);
    const double track_y = r.y + kMixerSliderCY - kMixerTrackH * 0.5;
    if (out_sx) *out_sx = tx - kMixerHitPadH;
    if (out_sy) *out_sy = track_y - kMixerHitPadV;
    if (out_sw) *out_sw = tw + 2.0 * kMixerHitPadH;
    if (out_sh) *out_sh = kMixerTrackH + 2.0 * kMixerHitPadV;
    return true;
  }
  return false;
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
