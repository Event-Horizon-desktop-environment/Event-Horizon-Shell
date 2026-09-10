#include "desktop_shell/controlcenter/paint/control_center_popup_paint.hpp"

#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/layout/control_center_anim.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/ui/slider/ui_slider.hpp"
#include "desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"

#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <ctime>
#include <string>

#include "configuration/shell_config.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

namespace {

static const eh::icons::IconEntry* resolve_stream_icon(
    eh::icons::IconCache& icons,
    const std::vector<std::string>& pinnedApps,
    const eh::widgets::ControlCenterMixerStream& s) {
   
  const eh::icons::IconEntry* icon = mixer_icon_from_pinned_apps(icons, pinnedApps, s.process_binary);
  if (icon && icon->surface) return icon;

  eh::shell::mixer_icon::StreamIconIds ids{};
  ids.icon_name = s.icon_name;
  ids.app_id = s.app_id;
  ids.process_binary = s.process_binary;
  ids.process_path = s.process_path;
  ids.app_name = s.app_name;
  return eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(icons, ids);
}

}

void control_center_popup_paint(eh::shell::dock::control_center::ControlCenterState& state,
                                cairo_t* cr,
                                int popupW, int popupH,
                                const eh::config::ShellConfig& scPopupOv,
                                eh::mpris::DockMpris* mpris,
                                const std::string& ccWidgetId,
                                eh::icons::IconCache& icons,
                                const std::vector<std::string>& pinnedApps) {
   
const double W = static_cast<double>(popupW);
const double H = static_cast<double>(popupH);

auto truncate = [&](const std::string& text, double maxW) -> std::string {
  std::string out;
  eh::shell::dock::spotlight_truncate_to_width(cr, text, maxW, &out);
  return out;
};

const double pad = 18.0;
const double radius = 24.0;
auto round_rect = [&](double rx, double ry, double rw, double rh, double rr) {
  const double rad = std::min({rr, rw * 0.5, rh * 0.5});
  const double xL = rx;
  const double yT = ry;
  const double xR = rx + rw;
  const double yB = ry + rh;
  cairo_new_path(cr);
  cairo_arc(cr, xR - rad, yT + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, xR - rad, yB - rad, rad, 0, M_PI_2);
  cairo_arc(cr, xL + rad, yB - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, xL + rad, yT + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
};

const double ccShellOv = static_cast<double>(eh::config::overlay_surface_alpha_scale(
    scPopupOv, eh::config::OverlaySurfaceAlphaKind::ControlCenter));
const double ccInnerGlass =
    static_cast<double>(eh::config::control_center_inner_glass_alpha_scale(scPopupOv));
const eh::config::ChromePaintColors mcCc = eh::config::derived_chrome_colors(scPopupOv.appearance);

using CcHT = eh::shell::dock::control_center::CcHoverTarget;
auto hover_hl = [&](double x, double y, double w, double h, double r) {
  round_rect(x, y, w, h, r);
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
};
auto hover_hl_circle = [&](double cx, double cy, double r) {
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
  cairo_fill(cr);
};

constexpr double kShadowOffX = 2.0, kShadowOffY = 3.0, kShadowAlpha = 0.22;
cairo_save(cr);
round_rect(kShadowOffX, kShadowOffY, W, H, radius);
cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, kShadowAlpha);
cairo_fill(cr);
cairo_restore(cr);

eh::shell::shared::paint_glass_card(cr, 0, 0, W, H, radius, mcCc, ccShellOv);

cairo_save(cr);
round_rect(0, 0, W, H, radius);
cairo_clip(cr);

constexpr double kPillPad = 18.0;
constexpr double kRowGap = 12.0;
constexpr double kRowH = 46.0;
constexpr double kPillRadius = 20.0;
constexpr double kBadgeRadius = 10.0;
auto stroke_dashed = [&](double dashLen, double gapLen) {
  const double dashes[2] = {dashLen, gapLen};
  cairo_set_dash(cr, dashes, 2, 0.0);
  cairo_stroke(cr);
  cairo_set_dash(cr, nullptr, 0, 0.0);
};
constexpr double kTwoRowH = kRowH * 2.0;

const double rowGap = kRowGap;
const double colGap = kRowGap;
const double gridTop = kPillPad + 24.0;
const double tileW = (W - kPillPad * 2.0 - colGap) * 0.5;
const auto ns = eh::shell::dock_slot_hooks::control_center_network_state();
const auto bs = eh::shell::dock_slot_hooks::control_center_bluetooth_state();
timespec tsNow{};
clock_gettime(CLOCK_MONOTONIC, &tsNow);
const uint64_t nowMs =
    static_cast<uint64_t>(tsNow.tv_sec) * 1000ULL + static_cast<uint64_t>(tsNow.tv_nsec / 1000000ULL);
auto asDraw = eh::shell::dock_slot_hooks::control_center_audio_output_state(&state);
auto isDraw = eh::shell::dock_slot_hooks::control_center_audio_input_state(&state);
// Suppress window: keep showing last-applied value while PipeWire catches up
if (!state.audioDragActive && state.audioLastAppliedPct >= 0 && nowMs < state.audioIgnoreStateUntilMs) {
  asDraw.volume_pct = std::clamp(state.audioLastAppliedPct, 0, 150);
  asDraw.muted = (asDraw.volume_pct <= 0);
}
if (!state.inputDragActive && state.inputLastAppliedPct >= 0 && nowMs < state.inputIgnoreStateUntilMs) {
  isDraw.volume_pct = std::clamp(state.inputLastAppliedPct, 0, 150);
  isDraw.muted = (isDraw.volume_pct <= 0);
}

// Section 1: separate Network + Bluetooth pills (side by side).
const double netPillX = kPillPad;
const double btPillX = kPillPad + tileW + colGap;
const double pillW = tileW;

auto draw_compact_tile = [&](double x, const char* glyph, const char* title,
                              bool active, double gr, double gg, double gb) {
  round_rect(x, gridTop, pillW, kRowH, kPillRadius);
  cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55 * ccInnerGlass);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double icx = x + 20.0;
  const double icy = gridTop + kRowH * 0.5;
  cairo_save(cr);
  round_rect(icx - 14.0, icy - 14.0, 28.0, 28.0, kBadgeRadius);
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, active ? 0.20 : 0.09);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, gr, gg, gb, 0.96);

  cairo_set_font_size(cr, 13.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
  cairo_move_to(cr, x + 38.0, gridTop + 30.0);
  cairo_show_text(cr, title);
};

// Left pill: Network
if (state.hoverTarget == CcHT::NetworkCard)
  hover_hl(netPillX, gridTop, pillW, kRowH, kPillRadius);
const char* netGlyph = ns.connected ? (ns.wifi ? "wifi" : "lan") : "signal_wifi_off";
const char* netTitle = ns.wifi ? (ns.ssid.empty() ? "Wi-Fi" : ns.ssid.c_str()) : (ns.ethernet ? "Ethernet" : "Network");
draw_compact_tile(netPillX, netGlyph, netTitle, ns.connected,
                  ns.connected ? mcCc.accentR : 0.65,
                  ns.connected ? mcCc.accentG : 0.68,
                  ns.connected ? mcCc.accentB : 0.72);

// Right pill: Bluetooth
if (state.hoverTarget == CcHT::BluetoothCard)
  hover_hl(btPillX, gridTop, pillW, kRowH, kPillRadius);
draw_compact_tile(btPillX, bs.powered ? "bluetooth" : "bluetooth_disabled", "Bluetooth", bs.powered,
                  bs.powered ? mcCc.accentR : 0.65,
                  bs.powered ? mcCc.accentG : 0.68,
                  bs.powered ? mcCc.accentB : 0.72);

double afterTopY = gridTop + kRowH + kRowGap;

// Section 2: Network expanded panel.
{

  constexpr int kMaxWifiRows = 8;
  const bool to = state.netAnimStartMs ? state.netAnimToExpanded : state.networkExpanded;

  const auto apsNow = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
  const int wifiRows = std::min(kMaxWifiRows, static_cast<int>(apsNow.size()));
  double fullH = 44.0 + static_cast<double>(wifiRows) * 34.0 + 12.0;
  if (state.wifiPasswordPrompt) fullH += 56.0;
  if (!state.wifiLastError.empty()) fullH += 18.0;

  const bool from = state.netAnimStartMs ? state.netAnimFromExpanded : state.networkExpanded;
  const double curH =
      (state.netAnimStartMs ? cc_anim_panel_h(nowMs, state.netAnimStartMs, from, to, fullH)
                                       : (state.networkExpanded ? fullH : 0.0));

  if (state.netAnimStartMs && (nowMs - state.netAnimStartMs) >= 200) {
    state.netAnimStartMs = 0;
    state.netAnimFromExpanded = state.networkExpanded;
    state.netAnimToExpanded = state.networkExpanded;
  }

  if (curH > 2.0) {
    round_rect(pad, afterTopY, W - pad * 2.0, curH, kPillRadius);
    cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_save(cr);
    cairo_rectangle(cr, pad, afterTopY, W - pad * 2.0, curH);
    cairo_clip(cr);

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 12.0, afterTopY + 18.0);
    const std::string hdr = ns.wifi ? ("Wi-Fi • " + (ns.ssid.empty() ? ns.iface : ns.ssid))
                                    : (ns.ethernet ? ("Ethernet • " + ns.iface) : std::string("Network"));
    cairo_show_text(cr, hdr.c_str());

    if (!state.wifiLastError.empty()) {
      cairo_set_source_rgba(cr, 0.86, 0.42, 0.42, 0.92);
      cairo_move_to(cr, pad + 12.0, afterTopY + 36.0);
      cairo_show_text(cr, state.wifiLastError.c_str());
    }

    const auto aps = apsNow;
    const int rows = wifiRows;
    const double headerH = 32.0;
    const double rowH = 34.0;
    for (int i = 0; i < rows; ++i) {
      const auto& ap = aps[static_cast<size_t>(i)];
      const double ry = afterTopY + headerH + static_cast<double>(i) * rowH;
      const bool rowHov = (state.hoverTarget == CcHT::NetworkRow && state.hoverRowIdx == i);
      if (rowHov) {
        hover_hl(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0, 14.0);
      } else {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        round_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0, 14.0);
        cairo_fill(cr);
      }

      eh::shell::draw_material_glyph(cr, pad + 18.0, ry + rowH * 0.55, 16.0, "wifi", ap.active ? 0.94 : 0.80,
                                 ap.active ? 0.97 : 0.84, ap.active ? 0.98 : 0.86, 1.0);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, pad + 38.0, ry + 20.0);
    const std::string label = ap.ssid.empty() ? std::string("(hidden)") : ap.ssid;
      cairo_show_text(cr, label.c_str());
      const std::string right = (ap.signal_pct >= 0 ? std::to_string(ap.signal_pct) + "%" : std::string("?"));
      cairo_move_to(cr, (W - pad) - 54.0, ry + 20.0);
      cairo_show_text(cr, right.c_str());
      if (ap.needs_password) {
        eh::shell::draw_material_glyph(cr, (W - pad) - 18.0, ry + rowH * 0.55, 16.0, "lock", 0.86, 0.90, 0.94, 1.0);
      }
    }

    if (state.wifiPasswordPrompt) {
      const double py = afterTopY + headerH + static_cast<double>(rows) * rowH + 8.0;
      round_rect(pad + 10.0, py, W - pad * 2.0 - 20.0, 40.0, 12.0);
      cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.46);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, pad + 16.0, py + 16.0);
      const std::string prompt = "Password for " +
                                 (state.wifiPendingSsid.empty() ? std::string("Wi-Fi") : state.wifiPendingSsid);
      cairo_show_text(cr, prompt.c_str());
      cairo_move_to(cr, pad + 16.0, py + 32.0);
      std::string masked(state.wifiPassword.size(), '*');
      if (masked.empty()) masked = "…";
      cairo_show_text(cr, masked.c_str());
    }

    cairo_restore(cr);

    afterTopY += curH + rowGap;
  }
}
// Section 2b: Bluetooth expanded panel.
{
  constexpr int kMaxBtRows = 8;
  const bool btTo = state.btAnimStartMs ? state.btAnimToExpanded : state.bluetoothExpanded;

  const auto btDevsNow = eh::shell::dock_slot_hooks::bluetooth_devices();
  const int btRows = std::min(kMaxBtRows, static_cast<int>(btDevsNow.size()));
  const double rowH = 34.0;
  double btFullH = 44.0 + static_cast<double>(btRows) * rowH + 12.0;

  const bool btFrom = state.btAnimStartMs ? state.btAnimFromExpanded : state.bluetoothExpanded;
  const double btCurH =
      (state.btAnimStartMs ? cc_anim_panel_h(nowMs, state.btAnimStartMs, btFrom, btTo, btFullH)
                                       : (state.bluetoothExpanded ? btFullH : 0.0));

  if (state.btAnimStartMs && (nowMs - state.btAnimStartMs) >= 200) {
    state.btAnimStartMs = 0;
    state.btAnimFromExpanded = state.bluetoothExpanded;
    state.btAnimToExpanded = state.bluetoothExpanded;
  }

  if (btCurH > 2.0) {
    round_rect(pad, afterTopY, W - pad * 2.0, btCurH, kPillRadius);
    cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_save(cr);
    cairo_rectangle(cr, pad, afterTopY, W - pad * 2.0, btCurH);
    cairo_clip(cr);

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 12.0, afterTopY + 18.0);
    cairo_show_text(cr, "Bluetooth");

    const auto btDevs = btDevsNow;
    const int rows = btRows;
    const double headerH = 32.0;
    for (int i = 0; i < rows; ++i) {
      const auto& d = btDevs[static_cast<size_t>(i)];
      const double ry = afterTopY + headerH + static_cast<double>(i) * rowH;
      const bool rowHov = (state.hoverTarget == CcHT::BluetoothRow && state.hoverRowIdx == i);
      const double rowCardX = pad;
      const double rowCardW = W - pad * 2.0;
      const double rowCardR = 14.0;
      if (rowHov) {
        hover_hl(rowCardX, ry, rowCardW, rowH, rowCardR);
      } else {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        round_rect(rowCardX, ry, rowCardW, rowH, rowCardR);
        cairo_fill(cr);
      }

      const char* glyph = eh::shell::dock_slot_hooks::bluetooth_device_kind_glyph(d.kind);
      eh::shell::draw_material_glyph(cr, pad + 14.0, ry + rowH * 0.5, 14.0, glyph,
                                 d.connected ? 0.30 : 0.80, d.connected ? 0.85 : 0.84, d.connected ? 0.30 : 0.86, 1.0);

      const double labelX = pad + 30.0;
      const double rightEdge = W - pad;

      // Action buttons
      const char* actionText = d.connected ? "Disconnect" : (d.paired ? "Connect" : "Pair");
      const double actionBtnW = d.connected ? 74.0 : (d.paired ? 60.0 : 40.0);
      const bool showForget = d.paired;
      const double forgetBtnW = 48.0;
      const double btnGap = 6.0;
      const double btnAreaW = actionBtnW + (showForget ? (btnGap + forgetBtnW) : 0.0);
      const double btnStartX = rightEdge - btnAreaW - 8.0;
      const double btnH = 24.0;
      const double btnY = ry + (rowH - btnH) * 0.5;

      auto draw_btn = [&](const char* text, double bx, double bw) {
        round_rect(bx, btnY, bw, btnH, 8.0);
        cairo_set_source_rgba(cr, 0.22, 0.22, 0.28, 0.7);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 0.85);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.0);
        cairo_text_extents_t bte;
        cairo_text_extents(cr, text, &bte);
        cairo_move_to(cr, bx + (bw - bte.x_advance) * 0.5, btnY + btnH * 0.5 + bte.height * 0.35);
        cairo_show_text(cr, text);
      };

      draw_btn(actionText, btnStartX, actionBtnW);
      if (showForget)
        draw_btn("Forget", btnStartX + actionBtnW + btnGap, forgetBtnW);

      // Device name clipped to not overlap buttons
      const double nameMaxX = btnStartX - 6.0;
      cairo_save(cr);
      cairo_rectangle(cr, labelX, ry, std::max(0.0, nameMaxX - labelX), rowH);
      cairo_clip(cr);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, labelX, ry + 21.0);
      const std::string label = d.alias.empty() ? d.address : d.alias;
      cairo_show_text(cr, label.c_str());
      cairo_restore(cr);
    }

    cairo_restore(cr);

    afterTopY += btCurH + rowGap;
  }
}

// Section 3: audio section (cards only).
const double audioY = afterTopY;
const double audioTileH = kTwoRowH;

auto paint_audio_card = [&](double cx, bool isOutput, const eh::shell::dock::control_center::ControlCenterAudioOutputState& cAs, const eh::shell::dock::control_center::ControlCenterAudioInputState& cIs,
                             CcHT sliderHover, CcHT muteHover) {
  round_rect(cx, audioY, tileW, audioTileH, kPillRadius);
  cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55 * ccInnerGlass);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  if (state.hoverTarget == sliderHover) {
    hover_hl(cx, audioY, tileW, kTwoRowH, kPillRadius);
  } else if (state.hoverTarget == muteHover) {
    const double mx = cx + 24.0 - 14.0;
    const double my = audioY + 28.0 - 14.0;
    hover_hl(mx, my, 28.0, 28.0, 8.0);
  }

  const bool muted = isOutput ? cAs.muted : cIs.muted;
  const double icx = cx + 24.0;
  const double icy = audioY + 28.0;
  cairo_save(cr);
  round_rect(icx - 14.0, icy - 14.0, 28.0, 28.0, kBadgeRadius);
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, muted ? 0.09 * ccInnerGlass : 0.20 * ccInnerGlass);
  cairo_fill(cr);
  cairo_restore(cr);

  const int volG = isOutput ? ((cAs.volume_fill_t_override >= 0.0 && cAs.volume_fill_t_override <= 1.0) ? static_cast<int>(std::lround(std::clamp(cAs.volume_fill_t_override, 0.0, 1.0) * 100.0)) : cAs.volume_pct) : 0;
  const char* aglyph = isOutput ? ((cAs.muted || volG <= 0) ? "volume_off" : (volG < 34 ? "volume_down" : "volume_up")) : (cIs.muted ? "mic_off" : "mic");
  const double agr = isOutput ? (cAs.muted ? 0.65 : mcCc.accentR) : (cIs.muted ? 0.65 : mcCc.accentR);
  const double agg = isOutput ? (cAs.muted ? 0.68 : mcCc.accentG) : (cIs.muted ? 0.68 : mcCc.accentG);
  const double agb = isOutput ? (cAs.muted ? 0.72 : mcCc.accentB) : (cIs.muted ? 0.72 : mcCc.accentB);
  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, aglyph, agr, agg, agb, 0.96);

  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
  cairo_move_to(cr, cx + 48.0, audioY + 30.0);
  const double kNameMaxW = tileW - 78.0;
  std::string nameTrunc;
  eh::shell::dock::spotlight_truncate_to_width(cr, isOutput ? cAs.device_name : cIs.device_name, kNameMaxW, &nameTrunc);
  cairo_show_text(cr, nameTrunc.c_str());

  namespace cc_audio = eh::shell::cc_slider;
  const float t = static_cast<float>(
    isOutput
      ? ((cAs.volume_fill_t_override >= 0.0 && cAs.volume_fill_t_override <= 1.0)
          ? std::clamp(cAs.volume_fill_t_override, 0.0, 1.0)
          : std::clamp(static_cast<double>(cAs.volume_pct) / 100.0, 0.0, 1.0))
      : ((cIs.volume_fill_t_override >= 0.0 && cIs.volume_fill_t_override <= 1.0)
          ? std::clamp(cIs.volume_fill_t_override, 0.0, 1.0)
          : std::clamp(static_cast<double>(cIs.volume_pct) / 100.0, 0.0, 1.0)));
  const double as_h = 38.0;
  eh::ui::Slider sl;
  sl.setRange(0.0f, 1.0f);
  sl.setStep(0.01f);
  sl.setValue(t);
  sl.setGeometry(static_cast<float>(cx + 10.0),
                 static_cast<float>((audioY + cc_audio::kAudioTrackYFromCardTop) - as_h * 0.5),
                 static_cast<float>(tileW - 20.0),
                 static_cast<float>(as_h));
  sl.setAccentColor(static_cast<float>(mcCc.accentR), static_cast<float>(mcCc.accentG), static_cast<float>(mcCc.accentB));
  sl.setTrackColor(static_cast<float>(mcCc.drawerDimR), static_cast<float>(mcCc.drawerDimG), static_cast<float>(mcCc.drawerDimB));
  sl.paint(cr);
};

paint_audio_card(kPillPad, true, asDraw, isDraw, CcHT::OutputAudioSlider, CcHT::OutputAudioMute);
paint_audio_card(kPillPad + tileW + colGap, false, asDraw, isDraw, CcHT::InputAudioSlider, CcHT::InputAudioMute);

double nextPanelY = audioY + audioTileH + kRowGap;

// Section 3b: audio device dropdown panels (full width).
auto paint_device_panel = [&](double py, const std::string& header,
                               const auto& devs, bool /*isOutput*/,
                               bool expanded, uint64_t ignoreUntil,
                               const std::string& pending, CcHT rowHover) {
  constexpr int kMaxDeviceRows = 12;
  constexpr double dheaderH = 32.0;
  constexpr double drowH = 34.0;
  const int rows = std::min(kMaxDeviceRows, static_cast<int>(devs.size()));
  const double panelW = W - pad * 2.0;
  const double panelH = expanded ? (dheaderH + static_cast<double>(rows) * drowH + 12.0) : 0.0;
  if (panelH <= 2.0) return py;
  round_rect(pad, py, panelW, panelH, kPillRadius);
  cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  cairo_save(cr);
  cairo_rectangle(cr, pad, py, panelW, panelH);
  cairo_clip(cr);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_set_font_size(cr, 12.0);
  cairo_move_to(cr, pad + 12.0, py + 18.0);
  cairo_show_text(cr, header.c_str());
  for (int i = 0; i < rows; ++i) {
    const auto& d = devs[static_cast<size_t>(i)];
    const bool pendingActive = (nowMs < ignoreUntil && !pending.empty());
    bool isDefault = d.is_default;
    if (pendingActive) {
      if constexpr (std::is_same_v<std::decay_t<decltype(d)>, eh::shell::dock::control_center::ControlCenterOutputDevice>) {
        isDefault = (d.sink_name == pending);
      } else {
        isDefault = (d.source_name == pending);
      }
    }
    const double ry = py + dheaderH + static_cast<double>(i) * drowH;
    const bool rowHov = (state.hoverTarget == rowHover && state.hoverRowIdx == i);
    const double rowCardX = pad + 10.0;
    const double rowCardW = panelW - 20.0;
    const double rowCardR = 14.0;
    if (rowHov) {
      hover_hl(rowCardX, ry + 6.0, rowCardW, drowH - 10.0, rowCardR);
    } else {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
      round_rect(rowCardX, ry + 6.0, rowCardW, drowH - 10.0, rowCardR);
      cairo_fill(cr);
    }
    eh::shell::draw_material_glyph(cr, pad + 18.0, ry + drowH * 0.55, 16.0, isDefault ? "radio_button_checked" : "radio_button_unchecked",
                               mcCc.accentR, mcCc.accentG, mcCc.accentB, 1.0);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 38.0, ry + 20.0);
    const double nameMaxW = panelW - 56.0;
    cairo_show_text(cr, truncate(d.display_name, nameMaxW).c_str());
  }
  cairo_restore(cr);
  return py + panelH + kRowGap;
};
{
  const auto outDevs = eh::shell::dock_slot_hooks::control_center_output_devices();
  nextPanelY = paint_device_panel(nextPanelY, "Output device", outDevs, true,
                                   state.outputDevicesExpanded, state.outputDevicesIgnoreUntilMs,
                                   state.outputDevicesPendingSink, CcHT::OutputDeviceRow);
}
{
  const auto inDevs = eh::shell::dock_slot_hooks::control_center_input_devices();
  nextPanelY = paint_device_panel(nextPanelY, "Input device", inDevs, false,
                                   state.inputDevicesExpanded, state.inputDevicesIgnoreUntilMs,
                                   state.inputDevicesPendingSource, CcHT::InputDeviceRow);
}

// Section 4: volume mixer (single unified pill).
const double mixerY = nextPanelY;
const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams(&state);
const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams(&state);
const int totalOutRows = static_cast<int>(streams.size());
const int totalInRows = static_cast<int>(inStreams.size());

constexpr double kMixerRowH = 46.0;
constexpr double kMixerHeaderH = 36.0;
constexpr double kMixerSectionH = 28.0;
constexpr double kMixerMoreH = 26.0;

const int showOutRows = state.mixerExpanded ? std::min(4, totalOutRows) : std::min(1, totalOutRows);
const int showInRows = state.mixerExpanded ? std::min(3, totalInRows) : 0;

double mixerBodyH = kMixerHeaderH;
if (showOutRows > 0) {
  if (state.mixerExpanded) mixerBodyH += kMixerSectionH;
  mixerBodyH += showOutRows * kMixerRowH;
}
if (showInRows > 0 && state.mixerExpanded) {
  mixerBodyH += kMixerSectionH;
  mixerBodyH += showInRows * kMixerRowH;
}
if (!state.mixerExpanded && totalOutRows > 1) mixerBodyH += kMixerMoreH;
const double mixerH = mixerBodyH + kPillPad;

round_rect(kPillPad, mixerY, W - kPillPad * 2.0, mixerH, kPillRadius);
cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
cairo_fill_preserve(cr);
if (state.mixerExpanded) {
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
} else {
  // Collapsed preview reads as tappable: dashed outline hints "more inside".
  cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.35);
  cairo_set_line_width(cr, 1.4);
  stroke_dashed(5.0, 4.0);
}

cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
if (state.hoverTarget == CcHT::MixerCard) {
  hover_hl(kPillPad, mixerY, W - kPillPad * 2.0, mixerH, kPillRadius);
}
eh::shell::draw_material_glyph(cr, eh::shell::cc_slider::kMixerIconLeft + 8.0, mixerY + kMixerHeaderH * 0.5 + 1.0, 18.0,
  state.mixerExpanded ? "expand_less" : "expand_more", 0.88, 0.93, 0.96, 1.0);
cairo_set_font_size(cr, 15.0);
cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
cairo_move_to(cr, eh::shell::cc_slider::kMixerNameLeft, mixerY + kMixerHeaderH * 0.5 + 5.0);
cairo_show_text(cr, "Volume Mixer");

auto paint_mixer_row = [&](const eh::widgets::ControlCenterMixerStream& s, double rowY,
                            bool isInput) {
  namespace cc = eh::shell::cc_slider;
  const double iconX = cc::kMixerIconLeft;
  const double iconY = rowY + cc::kMixerIconY;
  const eh::icons::IconEntry* icon = resolve_stream_icon(icons, pinnedApps, s);
  if (icon && icon->surface) {
    cairo_save(cr);
    cairo_translate(cr, iconX, iconY);
    cairo_scale(cr, 16.0 / std::max(1, icon->width), 16.0 / std::max(1, icon->height));
    cairo_set_source_surface(cr, icon->surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    cairo_arc(cr, iconX + 8.0, iconY + 8.0, 6.0, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.72, 0.76, 0.80, 0.92);
    cairo_fill(cr);
  }
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.92);
  const double maxNameW = W - cc::kMixerNameLeft - 68.0;
  std::string displayName = truncate(s.app_name, maxNameW);
  cairo_move_to(cr, cc::kMixerNameLeft, rowY + cc::kMixerNameBaseline);
  cairo_show_text(cr, displayName.c_str());
  cairo_text_extents_t te_;
  cairo_text_extents(cr, displayName.c_str(), &te_);
  const double nameEndX = cc::kMixerNameLeft + te_.x_advance;
  int rowPct = s.volume_pct;
  if (state.mixerDragActive && state.mixerDragIsInput == isInput &&
      state.mixerDragStreamId == s.sink_input_id && state.mixerDragUiPct >= 0) {
    rowPct = state.mixerDragUiPct;
  } else if (state.mixerDragIsInput == isInput && state.mixerDragStreamId == s.sink_input_id &&
             state.mixerLastAppliedPct >= 0 && nowMs < state.mixerIgnoreStateUntilMs) {
    rowPct = state.mixerLastAppliedPct;
  }
  double tDraw = std::clamp(static_cast<double>(rowPct) / 100.0, 0.0, 1.0);
  if (state.mixerDragActive && state.mixerDragIsInput == isInput &&
      state.mixerDragStreamId == s.sink_input_id && state.mixerDragVisualT >= 0.0) {
    tDraw = std::clamp(state.mixerDragVisualT, 0.0, 1.0);
  }
  const double sliderCY = rowY + cc::kMixerSliderCY;
  const double mh = 38.0;
  const double sliderX = nameEndX;
  const double sliderW = std::max(60.0, W - 28.0 - nameEndX);
  eh::ui::Slider msl;
  msl.setRange(0.0f, 1.0f);
  msl.setStep(0.01f);
  msl.setValue(static_cast<float>(tDraw));
  msl.setGeometry(static_cast<float>(sliderX),
                  static_cast<float>(sliderCY - mh * 0.5),
                  static_cast<float>(sliderW),
                  static_cast<float>(mh));
  msl.setAccentColor(static_cast<float>(mcCc.accentR), static_cast<float>(mcCc.accentG), static_cast<float>(mcCc.accentB));
  msl.setTrackColor(static_cast<float>(mcCc.drawerDimR), static_cast<float>(mcCc.drawerDimG), static_cast<float>(mcCc.drawerDimB));
  msl.paint(cr);
};

double ry = mixerY + kMixerHeaderH;
if (showOutRows > 0 && state.mixerExpanded) {
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 0.80, 0.84, 0.88, 1.0);
  cairo_move_to(cr, eh::shell::cc_slider::kMixerNameLeft, ry + kMixerSectionH * 0.5 + 4.0);
  cairo_show_text(cr, "Output");
  ry += kMixerSectionH;
}
for (int i = 0; i < showOutRows; ++i) {
  paint_mixer_row(streams[static_cast<size_t>(i)], ry, false);
  ry += kMixerRowH;
}
if (!state.mixerExpanded && totalOutRows > 1) {
  const int remaining = totalOutRows - 1;
  const std::string more = "+" + std::to_string(remaining) + " more";
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.90);
  cairo_move_to(cr, eh::shell::cc_slider::kMixerNameLeft, ry + kMixerMoreH * 0.5 + 4.0);
  cairo_show_text(cr, more.c_str());
  ry += kMixerMoreH;
}
if (state.mixerExpanded && showInRows > 0) {
  cairo_set_font_size(cr, 12.0);
  cairo_set_source_rgba(cr, 0.80, 0.84, 0.88, 1.0);
  cairo_move_to(cr, eh::shell::cc_slider::kMixerNameLeft, ry + kMixerSectionH * 0.5 + 4.0);
  cairo_show_text(cr, "Input");
  ry += kMixerSectionH;
  for (int i = 0; i < showInRows; ++i) {
    paint_mixer_row(inStreams[static_cast<size_t>(i)], ry, true);
    ry += kMixerRowH;
  }
}

// Section 5: media card.
{
  const double mediaY = mixerY + mixerH + kRowGap;
  constexpr double kCardH = 92.0;
  round_rect(kPillPad, mediaY, W - kPillPad * 2.0, kCardH, kPillRadius);
  {
    // Soft diagonal accent wash instead of flat glass, echoing the "Now Playing"
    // gradient card in the new design — built from theme accent, not a fixed hex.
    cairo_pattern_t* grad = cairo_pattern_create_linear(kPillPad, mediaY, kPillPad + (W - kPillPad * 2.0), mediaY + kCardH);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.16);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
    cairo_set_source(cr, grad);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(grad);
  }
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  if (state.hoverTarget == CcHT::MediaCard) {
    hover_hl(kPillPad, mediaY, W - kPillPad * 2.0, kCardH, kPillRadius);
  }

  eh::mpris::PlayerSnapshot ms{};
  if (mpris) ms = mpris->snapshot();
  const bool active = ms.active && (!ms.title.empty() || !ms.artist.empty());

  const double artX = kPillPad + 12.0;
  const double artY = mediaY + 14.0;
  const double artS = 56.0;
  round_rect(artX, artY, artS, artS, 12.0);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  if (active && ms.art) {
    cairo_save(cr);
    round_rect(artX, artY, artS, artS, 12.0);
    cairo_clip(cr);
    const int iw = cairo_image_surface_get_width(ms.art.get());
    const int ih = cairo_image_surface_get_height(ms.art.get());
    const double s = artS / std::max(1, std::max(iw, ih));
    cairo_translate(cr, artX + (artS - iw * s) * 0.5, artY + (artS - ih * s) * 0.5);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, ms.art.get(), 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    eh::shell::draw_material_glyph(cr, artX + artS * 0.5, artY + artS * 0.55, 22.0, "music_note", 0.88, 0.93, 0.96, 1.0);
  }

  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_set_font_size(cr, 13.0);
  const double textX = kPillPad + 80.0;
  const double textMaxW = W - kPillPad - textX;
  const std::string title =
      active ? (ms.title.empty() ? std::string("Unknown title") : ms.title) : std::string("No Media");
  cairo_text_extents_t teT;
  cairo_text_extents(cr, title.c_str(), &teT);
  double textY = mediaY + 30.0;
  constexpr double kTitleLineH = 17.0;
  if (teT.x_advance <= textMaxW) {
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, title.c_str());
    textY += kTitleLineH;
  } else {
    size_t split = std::string::npos;
    const size_t mid = title.length() / 2;
    for (size_t off = 0; off < mid; ++off) {
      if (mid + off < title.length() && title[mid + off] == ' ') { split = mid + off; break; }
      if (mid > off + 1 && title[mid - off - 1] == ' ') { split = mid - off - 1; break; }
    }
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, split != std::string::npos ? title.substr(0, split).c_str() : title.substr(0, mid).c_str());
    textY += kTitleLineH;
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, split != std::string::npos ? title.substr(split + 1).c_str() : title.substr(mid).c_str());
    textY += kTitleLineH;
  }
  cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92);
  cairo_set_font_size(cr, 12.0);
  const std::string artist =
      active ? (ms.artist.empty() ? std::string("Unknown artist") : ms.artist) : std::string("No Media");
  cairo_move_to(cr, textX, textY);
  cairo_show_text(cr, artist.c_str());

  const double btnR = 14.0;
  const double btnGap = 10.0;
  const double cy = mediaY + kCardH - 24.0;
  const double cx2 = kPillPad + (W - kPillPad * 2.0) - 22.0;
  const double cx1 = cx2 - (btnR * 2.0 + btnGap);
  const double cx0 = cx1 - (btnR * 2.0 + btnGap);
  auto paint_btn = [&](double cx, const char* glyph, bool enabled, CcHT hoverCheck, bool primary) {
    const bool hovered = (state.hoverTarget == hoverCheck);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, btnR, 0, 2 * M_PI);
    if (primary) {
      // Play/pause reads as the primary action, filled with the theme accent
      // (mirrors the solid "now playing" button in the new design).
      cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, hovered ? 0.95 : (enabled ? 0.85 : 0.30));
      cairo_fill(cr);
      eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0, glyph, 0.05, 0.05, 0.07, 1.0);
      return;
    }
    if (hovered) {
      hover_hl_circle(cx, cy, btnR);
    } else {
      cairo_set_source_rgba(cr, 1, 1, 1, enabled ? 0.10 : 0.04);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }
    eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0, glyph, 0.88, 0.93, 0.96, 1.0);
  };
  if (active) {
  paint_btn(cx0, "skip_previous", ms.can_go_previous, CcHT::MediaPrev, false);
  const bool playing = (ms.playback_status == "Playing");
  paint_btn(cx1, playing ? "pause" : "play_arrow", (ms.can_play || ms.can_pause), CcHT::MediaPlayPause, true);
  paint_btn(cx2, "skip_next", ms.can_go_next, CcHT::MediaNext, false);
  }
}

// Section 6: weather card (expands down, cross-fade).
{
  const double mediaY = mixerY + mixerH + kRowGap;
  constexpr double kWeatherCollapsedH = 92.0;
  constexpr double kWeatherExpFullH = 240.0;
  const double weatherY = mediaY + kWeatherCollapsedH + kRowGap;

  const uint64_t nowWs = nowMs;
  const bool wTo = state.weatherAnimStartMs ? state.weatherAnimToExpanded : state.weatherExpanded;
  const bool wFrom = state.weatherAnimStartMs ? state.weatherAnimFromExpanded : state.weatherExpanded;
  const double wCurH =
      (state.weatherAnimStartMs ? cc_anim_panel_h(nowWs, state.weatherAnimStartMs, wFrom, wTo, kWeatherExpFullH)
                                           : (state.weatherExpanded ? kWeatherExpFullH : 0.0));
  if (state.weatherAnimStartMs && (nowWs - state.weatherAnimStartMs) >= 200) {
    state.weatherAnimStartMs = 0;
    state.weatherAnimFromExpanded = state.weatherExpanded;
    state.weatherAnimToExpanded = state.weatherExpanded;
  }

  const double weatherH = (wCurH > 2.0) ? wCurH : kWeatherCollapsedH;
  const double fadeT = std::clamp(wCurH / kWeatherExpFullH, 0.0, 1.0);

  round_rect(kPillPad, weatherY, W - kPillPad * 2.0, weatherH, kPillRadius);
  cairo_set_source_rgba(cr, mcCc.drawerDimR, mcCc.drawerDimG, mcCc.drawerDimB, 0.55);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  if (state.hoverTarget == CcHT::WeatherCard) {
    hover_hl(kPillPad, weatherY, W - kPillPad * 2.0, weatherH, kPillRadius);
  }

  const std::string wid = ccWidgetId.empty() ? std::string("control_center") : ccWidgetId;
  const auto ws = eh::shell::dock_slot_hooks::control_center_weather_state(eh::config::shell_config_snapshot(), wid);
  const char unitCh = ws.fahrenheit ? 'F' : 'C';
  const char* glyph = ws.available ? (ws.icon.empty() ? "cloud" : ws.icon.c_str()) : "cloud_off";
  auto city_name = [&]() -> std::string {
    const auto p = ws.location.find(',');
    return p != std::string::npos ? ws.location.substr(0, p) : ws.location;
  };
  const std::string justCity = city_name();

  // Collapsed card content (fades out during expansion).
  {
    const double cf = 1.0 - fadeT;
    const double icx = kPillPad + 24.0;
    const double icy = weatherY + 28.0;
    cairo_save(cr);
    round_rect(icx - 16.0, icy - 16.0, 32.0, 32.0, kBadgeRadius);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.16 * cf);
    cairo_fill(cr);
    cairo_restore(cr);
    eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, 0.94, 0.96, 0.98, 1.0 * cf);

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * cf);
    cairo_set_font_size(cr, 14.0);
    cairo_move_to(cr, kPillPad + 48.0, weatherY + 30.0);
    cairo_show_text(cr, (ws.location.empty() ? "Weather" : justCity).c_str());

    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * cf);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, kPillPad + 48.0, weatherY + 50.0);
    const std::string sub =
        ws.available ? (ws.condition + " \xe2\x80\xa2 H " + std::to_string(ws.hi) + "\xc2\xb0" + unitCh + "  L " +
                        std::to_string(ws.lo) + "\xc2\xb0" + unitCh)
                                         : ws.status_text;
    cairo_show_text(cr, sub.c_str());

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * cf);
    cairo_set_font_size(cr, 26.0);
    cairo_move_to(cr, (W - kPillPad) - 86.0, weatherY + 44.0);
    const std::string t = ws.available ? (std::to_string(ws.temp) + "\xc2\xb0" + unitCh) : std::string("--");
    cairo_show_text(cr, t.c_str());
  }

  // Expanded panel design principles: even padding, aligned rows, consistent radii.
  if (wCurH > 2.0) {
    const double ef = fadeT;
    const double py = weatherY;
    const double innerX = kPillPad + 12.0;
    const double innerW = W - kPillPad * 2.0 - 24.0;
    cairo_save(cr);
    cairo_rectangle(cr, kPillPad, py, W - kPillPad * 2.0, wCurH);
    cairo_clip(cr);

    // Row 1: Icon (center y=py+32) + temp (baseline py+46) + city (baseline py+46, right-aligned)
    {
      eh::shell::draw_material_glyph(cr, kPillPad + 24.0, py + 32.0, 34.0, glyph, 0.94, 0.96, 0.98, 1.0 * ef);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * ef);
      cairo_set_font_size(cr, 44.0);
      cairo_move_to(cr, kPillPad + 56.0, py + 46.0);
      cairo_show_text(cr, (ws.available ? (std::to_string(ws.temp) + "\xc2\xb0" + unitCh) : "--").c_str());
      cairo_set_font_size(cr, 14.0);
      cairo_text_extents_t cte;
      cairo_text_extents(cr, justCity.c_str(), &cte);
      cairo_move_to(cr, (kPillPad + innerW + 12.0) - cte.x_advance, py + 46.0);
      cairo_show_text(cr, justCity.c_str());
    }

    // Row 2: Unit toggle
    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * ef);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, kPillPad + 56.0, py + 65.0);
    cairo_show_text(cr, ws.fahrenheit ? "Fahrenheit" : "Celsius");

    // Row 3: Chips (46px tall, consistent 12px radius)
    {
      const double chipY = py + 80.0;
      const double chipGap = 10.0;
      const double chipW = (innerW - chipGap * 3.0) / 4.0;
      auto chip = [&](double cx, const char* label, const std::string& val) {
        round_rect(cx, chipY, chipW, 46.0, 12.0);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06 * ef);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.90 * ef);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, cx + 12.0, chipY + 16.0);
        cairo_show_text(cr, label);
        cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * ef);
        cairo_set_font_size(cr, 16.0);
        cairo_move_to(cr, cx + 12.0, chipY + 38.0);
        cairo_show_text(cr, val.c_str());
      };
      chip(innerX, "Feels like", ws.available ? (std::to_string(ws.feels_like) + "\xc2\xb0" + unitCh) : "--");
      chip(innerX + (chipW + chipGap) * 1, "Humidity", ws.available ? (std::to_string(ws.humidity_pct) + "%") : "--");
      chip(innerX + (chipW + chipGap) * 2, "Wind", ws.available ? (std::to_string(ws.wind_kmh) + " km/h") : "--");
      chip(innerX + (chipW + chipGap) * 3, "Visibility", ws.available ? (std::to_string(ws.visibility_m)) : "--");
    }

    // Row 4: Forecast heading
    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * ef);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, innerX, py + 142.0);
    cairo_show_text(cr, "5-Day Forecast");

    // Row 5: Forecast cards (78px tall, consistent 12px radius)
    {
      const double fy0 = py + 152.0;
      const double fGap = 8.0;
      const double fw = (innerW - fGap * 4.0) / 5.0;
      const double fh = 78.0;
      const int nf = std::min(5, static_cast<int>(ws.forecast.size()));
      for (int i = 0; i < nf; ++i) {
        const auto& d = ws.forecast[static_cast<size_t>(i)];
        const double fx = innerX + static_cast<double>(i) * (fw + fGap);
        round_rect(fx, fy0, fw, fh, 12.0);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06 * ef);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * ef);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, fx + fw * 0.5, fy0 + 16.0);
        cairo_text_extents_t te;
        cairo_text_extents(cr, d.day.c_str(), &te);
        cairo_rel_move_to(cr, -te.x_advance * 0.5, 0);
        cairo_show_text(cr, d.day.c_str());
        eh::shell::draw_material_glyph(cr, fx + fw * 0.5, fy0 + 36.0, 18.0, d.icon.c_str(), 0.88, 0.93, 0.96, 1.0 * ef);
        cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * ef);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, fx + fw * 0.5, fy0 + 62.0);
        const std::string mm = std::to_string(d.hi) + "\xc2\xb0/" + std::to_string(d.lo) + "\xc2\xb0";
        cairo_text_extents(cr, mm.c_str(), &te);
        cairo_rel_move_to(cr, -te.x_advance * 0.5, 0);
        cairo_show_text(cr, mm.c_str());
      }
    }

    cairo_restore(cr);
  }
 }
cairo_restore(cr);
}

