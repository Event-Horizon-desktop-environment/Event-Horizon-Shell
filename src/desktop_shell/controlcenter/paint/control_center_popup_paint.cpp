#include "desktop_shell/controlcenter/paint/control_center_popup_paint.hpp"

#include "desktop_shell/controlcenter/paint/control_center_pear_paint.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"

#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "configuration/shell_config.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/controlcenter/debug/control_center_log.hpp"

namespace {
namespace ccl = eh::shell::dock::control_center;
namespace ccu = eh::shell::dock::control_center::paint_utils;

static const eh::icons::IconEntry* resolve_stream_icon(
    eh::icons::IconCache& icons,
    const std::vector<std::string>& pinnedApps,
    const eh::widgets::ControlCenterMixerStream& s) {
  // Memoize the winning lookup per stream identity: after warm-up each frame
  // costs one cache hit instead of the full ~20-attempt cascade (theme scans,
  // desktop-file cross-references). Misses retry every few seconds so async
  // loads still pop in; the memo resets on icon-theme switches.
  struct MemoVal {
    std::string won;
    uint64_t atMs = 0;
  };
  static std::unordered_map<std::string, MemoVal> memo;
  static std::uint64_t memoGen = 0;
  const std::uint64_t gen = icons.icon_theme_generation();
  if (gen != memoGen) {
    memo.clear();
    memoGen = gen;
  }
  if (memo.size() > 256) memo.clear();
  const uint64_t nowMs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
  const std::string id = s.icon_name + "\x1f" + s.app_id + "\x1f" + s.process_binary + "\x1f" + s.app_name;
  auto it = memo.find(id);
  if (it != memo.end()) {
    const std::string& won = it->second.won;
    if (won.size() > 2 && won[1] == ':') {
      const std::string key = won.substr(2);
      const eh::icons::IconEntry* e =
          (won[0] == 'a') ? icons.app_icon(key) : icons.tray_icon(key);
      if (e && e->surface) return e;
    }
    // Single lookup missed (evicted entry / async still pending): retry the
    // full cascade at most every 5s instead of every frame.
    if (nowMs - it->second.atMs < 5000) return nullptr;
    it->second.atMs = nowMs;
  }

  const eh::icons::IconEntry* icon = mixer_icon_from_pinned_apps(icons, pinnedApps, s.process_binary);
  std::string won;
  if (!(icon && icon->surface)) {
    eh::shell::mixer_icon::StreamIconIds ids{};
    ids.icon_name = s.icon_name;
    ids.app_id = s.app_id;
    ids.process_binary = s.process_binary;
    ids.process_path = s.process_path;
    ids.app_name = s.app_name;
    icon = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(icons, ids, &won);
  }
  memo[id] = MemoVal{won, nowMs};
  if (icon && icon->surface) return icon;
  return nullptr;
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
  // PearCenter compact layout path (default ON; per-instance opt-out via pear_layout="0").
  if (ccl::pear_layout_enabled(scPopupOv, ccWidgetId)) {
    pear_center_paint(state, cr, popupW, scPopupOv, mpris, ccWidgetId);
    return;
  }
  (void)popupH;
  ccl::CcTimer tAll("paint", 8000);
  const double W = static_cast<double>(popupW);

  auto truncate = [&](const std::string& text, double maxW) -> std::string {
    std::string out;
    eh::shell::dock::spotlight_truncate_to_width(cr, text, maxW, &out);
    return out;
  };

  auto round_rect = [&](double rx, double ry, double rw, double rh, double rr) {
    ccu::rrect(cr, rx, ry, rw, rh, rr);
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
  // Standard card + standard row backgrounds (frosted treatment lives in the helper).
  auto card = [&](double x, double y, double w, double h) {
    ccu::cc_paint_glass_card_mc(cr, x, y, w, h, 20.0, ccInnerGlass, mcCc);
  };
  // Selected-row wash (connected AP / device / default sink).
  auto selected_bg = [&](double x, double y, double w, double h) {
    round_rect(x, y, w, h, 14.0);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.16);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.40);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  };
  // Right-aligned dim count for panel headers ("8").
  auto header_count = [&](double rightX, double baselineY, int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", n);
    cairo_set_font_size(cr, 12.0);
    cairo_text_extents_t te;
    cairo_text_extents(cr, buf, &te);
    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.75);
    cairo_move_to(cr, rightX - te.x_advance, baselineY);
    cairo_show_text(cr, buf);
  };
  auto row_bg = [&](double x, double y, double w, double h) {
    round_rect(x, y, w, h, 14.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    cairo_fill(cr);
  };

  timespec tsNow{};
  clock_gettime(CLOCK_MONOTONIC, &tsNow);
  const uint64_t nowMs =
      static_cast<uint64_t>(tsNow.tv_sec) * 1000ULL + static_cast<uint64_t>(tsNow.tv_nsec / 1000000ULL);

  // Single source of truth — every Y below comes from the shared layout.
  ccl::CcLayout L = ccl::cc_compute_layout(W, state, nowMs);
  {
    const int wantH = static_cast<int>(std::ceil(L.totalH));
    if (wantH != popupH) {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "SIZE-MISMATCH paint-total=%d surface=%d net=%d bt=%d out=%d in=%d mix=%d wx=%d",
                    wantH, popupH, state.networkExpanded ? 1 : 0, state.bluetoothExpanded ? 1 : 0,
                    state.outputDevicesExpanded ? 1 : 0, state.inputDevicesExpanded ? 1 : 0,
                    state.mixerExpanded ? 1 : 0, state.weatherExpanded ? 1 : 0);
      ccl::cc_log(buf);
    }
  }
  const double pad = L.pad;
  const double tileW = L.tileW;

  constexpr double kShadowOffX = 2.0, kShadowOffY = 3.0, kShadowAlpha = 0.22;
  cairo_save(cr);
  round_rect(kShadowOffX, kShadowOffY, W, L.totalH, 24.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, kShadowAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::shared::paint_glass_card(cr, 0, 0, W, L.totalH, 24.0, mcCc, ccShellOv);

  cairo_save(cr);
  round_rect(0, 0, W, L.totalH, 24.0);
  cairo_clip(cr);

  const auto ns = eh::shell::dock_slot_hooks::control_center_network_state();
  const auto bs = eh::shell::dock_slot_hooks::control_center_bluetooth_state();
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

  ccl::CcTimer tSec("paint-tiles", 1500);
  // Section 1: Network + Bluetooth tiles.
  auto draw_compact_tile = [&](double x, const char* glyph, const char* title,
                                bool active, double gr, double gg, double gb) {
    card(x, L.gridY, tileW, L.tileH);
    const double icx = x + 20.0;
    const double icy = L.gridY + L.tileH * 0.5;
    cairo_save(cr);
    round_rect(icx - 14.0, icy - 14.0, 28.0, 28.0, 10.0);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, active ? 0.20 : 0.09);
    cairo_fill(cr);
    cairo_restore(cr);
    eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, gr, gg, gb, 0.96);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_move_to(cr, x + 38.0, L.gridY + 30.0);
    cairo_show_text(cr, title);
  };

  if (state.hoverTarget == CcHT::NetworkCard)
    hover_hl(L.netX, L.gridY, tileW, L.tileH, 20.0);
  const char* netGlyph = ns.connected ? (ns.wifi ? "wifi" : "lan") : "signal_wifi_off";
  const char* netTitle = ns.wifi ? (ns.ssid.empty() ? "Wi-Fi" : ns.ssid.c_str()) : (ns.ethernet ? "Ethernet" : "Network");
  draw_compact_tile(L.netX, netGlyph, netTitle, ns.connected,
                    ns.connected ? mcCc.accentR : 0.65,
                    ns.connected ? mcCc.accentG : 0.68,
                    ns.connected ? mcCc.accentB : 0.72);

  if (state.hoverTarget == CcHT::BluetoothCard)
    hover_hl(L.btX, L.gridY, tileW, L.tileH, 20.0);
  draw_compact_tile(L.btX, bs.powered ? "bluetooth" : "bluetooth_disabled", "Bluetooth", bs.powered,
                    bs.powered ? mcCc.accentR : 0.65,
                    bs.powered ? mcCc.accentG : 0.68,
                    bs.powered ? mcCc.accentB : 0.72);

  tSec.arm("paint-net", 1500);
  // Section 2: Network expanded panel.
  if (L.netH > 2.0) {
      card(pad, L.netY, W - pad * 2.0, L.netH);
    cairo_save(cr);
    cairo_rectangle(cr, pad, L.netY, W - pad * 2.0, L.netH);
    cairo_clip(cr);

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 12.0, L.netY + 18.0);
    const std::string hdr = ns.wifi ? ("Wi-Fi \u2022 " + (ns.ssid.empty() ? ns.iface : ns.ssid))
                                    : (ns.ethernet ? ("Ethernet \u2022 " + ns.iface) : std::string("Network"));
    cairo_show_text(cr, hdr.c_str());
    header_count(W - pad - 12.0, L.netY + 18.0, L.netRows);

    double rowBaseY = L.netY + 32.0;
    if (!state.wifiLastError.empty()) {
      cairo_set_source_rgba(cr, 0.86, 0.42, 0.42, 0.92);
      cairo_move_to(cr, pad + 12.0, L.netY + 36.0);
      cairo_show_text(cr, state.wifiLastError.c_str());
    }

    const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
    for (int i = 0; i < L.netRows; ++i) {
      const auto& ap = aps[static_cast<size_t>(i)];
      const double ry = L.netY + L.netHeaderH + static_cast<double>(i) * 34.0;
      const bool rowHov = (state.hoverTarget == CcHT::NetworkRow && state.hoverRowIdx == i);
      if (rowHov) {
        hover_hl(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0, 14.0);
      } else if (ap.active) {
        selected_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      } else {
        row_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      }
      eh::shell::draw_material_glyph(cr, pad + 18.0, ry + 34.0 * 0.55, 16.0, "wifi", ap.active ? 0.94 : 0.80,
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
        eh::shell::draw_material_glyph(cr, (W - pad) - 18.0, ry + 34.0 * 0.55, 16.0, "lock", 0.86, 0.90, 0.94, 1.0);
      }
    }
    (void)rowBaseY;

    if (state.wifiPasswordPrompt) {
      const double py = L.netY + L.netHeaderH + static_cast<double>(L.netRows) * 34.0 + 8.0;
      row_bg(pad + 10.0, py, W - pad * 2.0 - 20.0, 40.0);
      cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, pad + 16.0, py + 16.0);
      const std::string prompt = "Password for " +
                                 (state.wifiPendingSsid.empty() ? std::string("Wi-Fi") : state.wifiPendingSsid);
      cairo_show_text(cr, prompt.c_str());
      cairo_move_to(cr, pad + 16.0, py + 32.0);
      std::string masked(state.wifiPassword.size(), '*');
      if (masked.empty()) masked = "\u2026";
      cairo_show_text(cr, masked.c_str());
    }
    cairo_restore(cr);
  }

  tSec.arm("paint-bt", 1500);
  // Section 2b: Bluetooth expanded panel.
  if (L.btH > 2.0) {
      card(pad, L.btY, W - pad * 2.0, L.btH);
    cairo_save(cr);
    cairo_rectangle(cr, pad, L.btY, W - pad * 2.0, L.btH);
    cairo_clip(cr);

    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 12.0, L.btY + 18.0);
    cairo_show_text(cr, "Bluetooth");
    header_count(W - pad - 12.0, L.btY + 18.0, L.btRows);

    const auto btDevs = eh::shell::dock_slot_hooks::bluetooth_devices();
    const double rightEdge = W - pad - 10.0;
    for (int i = 0; i < L.btRows; ++i) {
      const auto& d = btDevs[static_cast<size_t>(i)];
      const double ry = ccl::cc_panel_row_y(L.btY, i);
      const bool rowHov = (state.hoverTarget == CcHT::BluetoothRow && state.hoverRowIdx == i);
      if (rowHov) {
        hover_hl(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0, 14.0);
      } else if (d.connected) {
        selected_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      } else {
        row_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      }

      const char* glyph = eh::shell::dock_slot_hooks::bluetooth_device_kind_glyph(d.kind);
      eh::shell::draw_material_glyph(cr, pad + 22.0, ry + 17.0, 15.0, glyph,
                                     d.connected ? 0.30 : 0.80, d.connected ? 0.85 : 0.84,
                                     d.connected ? 0.30 : 0.86, 1.0);

      // One uniform action button; Forget is a quiet icon button.
      const auto b = ccl::cc_bt_btns(d.connected, d.paired, rightEdge);
      const char* actionText = d.connected ? "Disconnect" : (d.paired ? "Connect" : "Pair");
      const bool primary = d.connected || d.paired;
      {
        const double btnH = 24.0;
        const double btnY = ry + (34.0 - btnH) * 0.5;
        round_rect(b.actionX, btnY, b.actionW, btnH, 8.0);
        if (primary) {
          cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.85);
          cairo_fill(cr);
          cairo_set_source_rgba(cr, 0.05, 0.05, 0.07, 1.0);
        } else {
          cairo_set_source_rgba(cr, 0.22, 0.22, 0.28, 0.7);
          cairo_fill(cr);
          cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 0.9);
        }
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.0);
        cairo_text_extents_t bte;
        cairo_text_extents(cr, actionText, &bte);
        cairo_move_to(cr, b.actionX + (b.actionW - bte.x_advance) * 0.5,
                      btnY + btnH * 0.5 + bte.height * 0.35);
        cairo_show_text(cr, actionText);
      }
      if (b.showForget) {
        eh::shell::draw_material_glyph(cr, b.forgetX + b.forgetW * 0.5, ry + 17.0, 15.0, "close",
                                       0.72, 0.76, 0.80, 0.85);
      }

      const double labelX = pad + 42.0;
      const double nameMaxX = b.actionX - 6.0;
      cairo_save(cr);
      cairo_rectangle(cr, labelX, ry, std::max(0.0, nameMaxX - labelX), 34.0);
      cairo_clip(cr);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, labelX, ry + 21.0);
      const std::string label = d.alias.empty() ? d.address : d.alias;
      cairo_show_text(cr, label.c_str());
      cairo_restore(cr);
    }
    cairo_restore(cr);
  }

  tSec.arm("paint-audio", 1500);
  // Section 3: audio cards.
  auto paint_audio_card = [&](double cx, bool isOutput,
                               const eh::shell::dock::control_center::ControlCenterAudioOutputState& cAs,
                               const eh::shell::dock::control_center::ControlCenterAudioInputState& cIs,
                               CcHT sliderHover, CcHT muteHover) {
    card(cx, L.audioY, tileW, L.audioH);

    if (state.hoverTarget == sliderHover) {
      hover_hl(cx, L.audioY, tileW, L.audioH, 20.0);
    } else if (state.hoverTarget == muteHover) {
      hover_hl_circle(cx + 24.0, L.audioY + 28.0, 14.0);
    }

    const bool muted = isOutput ? cAs.muted : cIs.muted;
    const double icx = cx + 24.0;
    const double icy = L.audioY + 28.0;
    cairo_save(cr);
    round_rect(icx - 14.0, icy - 14.0, 28.0, 28.0, 10.0);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, muted ? 0.09 * ccInnerGlass : 0.20 * ccInnerGlass);
    cairo_fill(cr);
    cairo_restore(cr);

    const int volG = isOutput ? ((cAs.volume_fill_t_override >= 0.0 && cAs.volume_fill_t_override <= 1.0) ? static_cast<int>(std::lround(std::clamp(cAs.volume_fill_t_override, 0.0, 1.0) * 100.0)) : cAs.volume_pct) : 0;
    const char* aglyph = isOutput ? ((cAs.muted || volG <= 0) ? "volume_off" : (volG < 34 ? "volume_down" : "volume_up")) : (cIs.muted ? "mic_off" : "mic");
    eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, aglyph,
                                   isOutput ? (cAs.muted ? 0.65 : mcCc.accentR) : (cIs.muted ? 0.65 : mcCc.accentR),
                                   isOutput ? (cAs.muted ? 0.68 : mcCc.accentG) : (cIs.muted ? 0.68 : mcCc.accentG),
                                   isOutput ? (cAs.muted ? 0.72 : mcCc.accentB) : (cIs.muted ? 0.72 : mcCc.accentB),
                                   0.96);

    cairo_set_font_size(cr, 14.0);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_move_to(cr, cx + 48.0, L.audioY + 30.0);
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
    eh::ui::Slider sl;
    sl.setRange(0.0f, 1.0f);
    sl.setStep(0.01f);
    sl.setValue(t);
    // Widget box == hit box from control_center_audio_slider_layout():
    // same shared constants, cannot drift (see Docs/hit-testing.md).
    sl.setGeometry(static_cast<float>(cx + cc_audio::kAudioTrackXPad - cc_audio::kAudioHitPadH),
                   static_cast<float>((L.audioY + cc_audio::kAudioTrackYFromCardTop) - cc_audio::kAudioHitPadV),
                   static_cast<float>(tileW - 2.0 * cc_audio::kAudioTrackXPad + 2.0 * cc_audio::kAudioHitPadH),
                   static_cast<float>(cc_audio::kAudioTrackH + 2.0 * cc_audio::kAudioHitPadV));
    sl.setAccentColor(static_cast<float>(mcCc.accentR), static_cast<float>(mcCc.accentG), static_cast<float>(mcCc.accentB));
    sl.setTrackColor(static_cast<float>(mcCc.drawerDimR), static_cast<float>(mcCc.drawerDimG), static_cast<float>(mcCc.drawerDimB));
    sl.paint(cr);
  };

  paint_audio_card(pad, true, asDraw, isDraw, CcHT::OutputAudioSlider, CcHT::OutputAudioMute);
  paint_audio_card(pad + tileW + 12.0, false, asDraw, isDraw, CcHT::InputAudioSlider, CcHT::InputAudioMute);

  tSec.arm("paint-devices", 1500);
  // Section 3b: audio device dropdown panels.
  auto paint_device_panel = [&](double py, double panelH, int rows, const std::string& header,
                                 auto is_default, auto display_name, CcHT rowHover) {
    if (panelH <= 2.0) return;
    card(pad, py, W - pad * 2.0, panelH);
    cairo_save(cr);
    cairo_rectangle(cr, pad, py, W - pad * 2.0, panelH);
    cairo_clip(cr);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 12.0, py + 18.0);
    cairo_show_text(cr, header.c_str());
    header_count(pad + (W - pad * 2.0) - 12.0, py + 18.0, rows);
    for (int i = 0; i < rows; ++i) {
      const bool isDefault = is_default(i);
      const double ry = ccl::cc_panel_row_y(py, i);
      const bool rowHov = (state.hoverTarget == rowHover && state.hoverRowIdx == i);
      if (rowHov) {
        hover_hl(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0, 14.0);
      } else if (isDefault) {
        selected_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      } else {
        row_bg(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, 24.0);
      }
      eh::shell::draw_material_glyph(cr, pad + 18.0, ry + 34.0 * 0.55, 16.0,
                                     isDefault ? "radio_button_checked" : "radio_button_unchecked",
                                     mcCc.accentR, mcCc.accentG, mcCc.accentB, 1.0);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0);
      cairo_move_to(cr, pad + 38.0, ry + 20.0);
      cairo_show_text(cr, truncate(display_name(i), W - pad * 2.0 - 56.0).c_str());
    }
    cairo_restore(cr);
  };
  {
    const auto outDevs = eh::shell::dock_slot_hooks::control_center_output_devices();
    const bool pendingActive = (nowMs < state.outputDevicesIgnoreUntilMs && !state.outputDevicesPendingSink.empty());
    const std::string pending = state.outputDevicesPendingSink;
    paint_device_panel(
        L.outDevY, L.outDevH, L.outDevRows, "Output device",
        [&](int i) {
          return pendingActive ? (outDevs[static_cast<size_t>(i)].sink_name == pending)
                               : outDevs[static_cast<size_t>(i)].is_default;
        },
        [&](int i) -> std::string { return outDevs[static_cast<size_t>(i)].display_name; },
        CcHT::OutputDeviceRow);
  }
  {
    const auto inDevs = eh::shell::dock_slot_hooks::control_center_input_devices();
    const bool pendingActive = (nowMs < state.inputDevicesIgnoreUntilMs && !state.inputDevicesPendingSource.empty());
    const std::string pending = state.inputDevicesPendingSource;
    paint_device_panel(
        L.inDevY, L.inDevH, L.inDevRows, "Input device",
        [&](int i) {
          return pendingActive ? (inDevs[static_cast<size_t>(i)].source_name == pending)
                               : inDevs[static_cast<size_t>(i)].is_default;
        },
        [&](int i) -> std::string { return inDevs[static_cast<size_t>(i)].display_name; },
        CcHT::InputDeviceRow);
  }

  tSec.arm("paint-mixer", 2000);
  // Section 4: volume mixer.
  const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams(&state);
  const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams(&state);
  card(pad, L.mixerY, W - pad * 2.0, L.mixerH);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  if (state.hoverTarget == CcHT::MixerCard) {
    hover_hl(pad, L.mixerY, W - pad * 2.0, L.mixerH, 20.0);
  }
  namespace smx = eh::shell::cc_slider;
  eh::shell::draw_material_glyph(cr, smx::kMixerIconLeft + 8.0, L.mixerY + smx::kMixerHeaderH * 0.5 + 1.0, 18.0,
                                 state.mixerExpanded ? "expand_less" : "expand_more", 0.88, 0.93, 0.96, 1.0);
  cairo_set_font_size(cr, 15.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_move_to(cr, smx::kMixerNameLeft, L.mixerY + smx::kMixerHeaderH * 0.5 + 5.0);
  cairo_show_text(cr, "Volume Mixer");

  auto paint_mixer_row = [&](const eh::widgets::ControlCenterMixerStream& s, double rowY, bool isInput) {
    const double iconX = smx::kMixerIconLeft;
    const double iconY = rowY + smx::kMixerIconY;
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
    const double maxNameW = W - smx::kMixerNameLeft - 68.0;
    std::string displayName = truncate(s.app_name, maxNameW);
    cairo_move_to(cr, smx::kMixerNameLeft, rowY + smx::kMixerNameBaseline);
    cairo_show_text(cr, displayName.c_str());
    cairo_text_extents_t te_;
    cairo_text_extents(cr, displayName.c_str(), &te_);
    const double nameEndX = smx::kMixerNameLeft + te_.x_advance + 10.0;
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
    const double sliderCY = rowY + smx::kMixerSliderCY;
    eh::ui::Slider msl;
    msl.setRange(0.0f, 1.0f);
    msl.setStep(0.01f);
    msl.setValue(static_cast<float>(tDraw));
    msl.setGeometry(static_cast<float>(nameEndX), static_cast<float>(sliderCY - 38.0 * 0.5),
                    static_cast<float>(std::max(60.0, W - 28.0 - nameEndX)), 38.0f);
    msl.setAccentColor(static_cast<float>(mcCc.accentR), static_cast<float>(mcCc.accentG), static_cast<float>(mcCc.accentB));
    msl.setTrackColor(static_cast<float>(mcCc.drawerDimR), static_cast<float>(mcCc.drawerDimG), static_cast<float>(mcCc.drawerDimB));
    msl.paint(cr);
  };

  {
    double ry = L.mixerY + smx::kMixerHeaderH;
    size_t rowIdx = 0;
    const auto next_row = [&]() -> const ccl::CcMixerRow* {
      if (rowIdx >= L.mixRows.size()) return nullptr;
      return &L.mixRows[rowIdx++];
    };
    if (state.mixerExpanded && L.mixShowOut > 0) {
      cairo_set_font_size(cr, 12.0);
      cairo_set_source_rgba(cr, 0.80, 0.84, 0.88, 1.0);
      cairo_move_to(cr, smx::kMixerNameLeft, ry + smx::kMixerSectionH * 0.5 + 4.0);
      cairo_show_text(cr, "Output");
      ry += smx::kMixerSectionH;
    }
    for (int i = 0; i < L.mixShowOut; ++i) {
      const auto* r = next_row();
      if (!r) break;
      paint_mixer_row(streams[static_cast<size_t>(r->streamIdx)], r->y, false);
      ry = r->y + smx::kMixerRowH;
    }
    if (L.mixMore) {
      const std::string more = "+" + std::to_string(L.mixTotalOut - 1) + " more";
      cairo_set_font_size(cr, 12.0);
      cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.90);
      cairo_move_to(cr, smx::kMixerNameLeft, ry + smx::kMixerMoreH * 0.5 + 4.0);
      cairo_show_text(cr, more.c_str());
    }
    if (state.mixerExpanded && L.mixShowIn > 0) {
      // ry already advanced past out rows via mixRows positions; recompute from last in-row.
      const double inFirstY = L.mixRows[static_cast<size_t>(L.mixShowOut)].y;
      cairo_set_font_size(cr, 12.0);
      cairo_set_source_rgba(cr, 0.80, 0.84, 0.88, 1.0);
      cairo_move_to(cr, smx::kMixerNameLeft, inFirstY - smx::kMixerSectionH + smx::kMixerSectionH * 0.5 + 4.0);
      cairo_show_text(cr, "Input");
      for (int i = 0; i < L.mixShowIn; ++i) {
        const auto* r = next_row();
        if (!r) break;
        paint_mixer_row(inStreams[static_cast<size_t>(r->streamIdx)], r->y, true);
      }
    }
  }

  tSec.arm("paint-media", 1500);
  // Section 5: media card.
  card(pad, L.mediaY, W - pad * 2.0, L.mediaH);

  if (state.hoverTarget == CcHT::MediaCard) {
    hover_hl(pad, L.mediaY, W - pad * 2.0, L.mediaH, 20.0);
  }

  eh::mpris::PlayerSnapshot ms{};
  if (mpris) ms = mpris->snapshot();
  const bool active = ms.active && (!ms.title.empty() || !ms.artist.empty());

  const double artX = pad + 12.0;
  const double artY = L.mediaY + 14.0;
  const double artS = 56.0;
  row_bg(artX, artY, artS, artS);

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
  const double textX = pad + 80.0;
  const double textMaxW = W - pad - textX;
  const std::string title =
      active ? (ms.title.empty() ? std::string("Unknown title") : ms.title) : std::string("No Media");
  cairo_text_extents_t teT;
  cairo_text_extents(cr, title.c_str(), &teT);
  double textY = L.mediaY + 30.0;
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

  {
    const double cy = ccl::cc_media_btn_cy(L.mediaY, L.mediaH);
    auto paint_btn = [&](int idx, const char* glyph, bool enabled, CcHT hoverCheck, bool primary) {
      const double cx = ccl::cc_media_btn_cx(pad, W - pad * 2.0, idx);
      const bool hovered = (state.hoverTarget == hoverCheck);
      constexpr double btnR = 14.0;
      if (primary) {
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, btnR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, hovered ? 0.95 : (enabled ? 0.85 : 0.30));
        cairo_fill(cr);
        eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0, glyph, 0.05, 0.05, 0.07, 1.0);
        return;
      }
      cairo_new_path(cr);
      cairo_arc(cr, cx, cy, btnR, 0, 2 * M_PI);
      if (hovered) {
        cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
        cairo_fill(cr);
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
      paint_btn(0, "skip_previous", ms.can_go_previous, CcHT::MediaPrev, false);
      const bool playing = (ms.playback_status == "Playing");
      paint_btn(1, playing ? "pause" : "play_arrow", (ms.can_play || ms.can_pause), CcHT::MediaPlayPause, true);
      paint_btn(2, "skip_next", ms.can_go_next, CcHT::MediaNext, false);
    }
  }

  tSec.arm("paint-weather", 1500);
  // Section 6: weather card.
  card(pad, L.weatherY, W - pad * 2.0, L.weatherH);
  if (state.hoverTarget == CcHT::WeatherCard) {
    hover_hl(pad, L.weatherY, W - pad * 2.0, L.weatherH, 20.0);
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

  // Always fully shown.
  {
    constexpr double ef = 1.0;
    const double py = L.weatherY;
    const double innerX = pad + 12.0;
    const double innerW = W - pad * 2.0 - 24.0;
    cairo_save(cr);
    cairo_rectangle(cr, pad, py, W - pad * 2.0, L.weatherH);
    cairo_clip(cr);

    eh::shell::draw_material_glyph(cr, pad + 24.0, py + 32.0, 34.0, glyph, 0.94, 0.96, 0.98, 1.0 * ef);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0 * ef);
    cairo_set_font_size(cr, 44.0);
    cairo_move_to(cr, pad + 56.0, py + 46.0);
    cairo_show_text(cr, (ws.available ? (std::to_string(ws.temp) + "\u00b0" + unitCh) : "--").c_str());
    cairo_set_font_size(cr, 14.0);
    cairo_text_extents_t cte;
    cairo_text_extents(cr, justCity.c_str(), &cte);
    cairo_move_to(cr, (pad + innerW + 12.0) - cte.x_advance, py + 46.0);
    cairo_show_text(cr, justCity.c_str());

    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * ef);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, pad + 56.0, py + 65.0);
    cairo_show_text(cr, ws.fahrenheit ? "Fahrenheit" : "Celsius");

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
      chip(innerX, "Feels like", ws.available ? (std::to_string(ws.feels_like) + "\u00b0" + unitCh) : "--");
      chip(innerX + (chipW + chipGap) * 1, "Humidity", ws.available ? (std::to_string(ws.humidity_pct) + "%") : "--");
      chip(innerX + (chipW + chipGap) * 2, "Wind", ws.available ? (std::to_string(ws.wind_kmh) + " km/h") : "--");
      chip(innerX + (chipW + chipGap) * 3, "Visibility", ws.available ? (std::to_string(ws.visibility_m)) : "--");
    }

    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92 * ef);
    cairo_set_font_size(cr, 12.0);
    cairo_move_to(cr, innerX, py + 142.0);
    cairo_show_text(cr, "5-Day Forecast");

    {
      const double fy0 = py + 152.0;
      const double fGap = 8.0;
      const double fw = (innerW - fGap * 4.0) / 5.0;
      constexpr double fh = 78.0;
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
        const std::string mm = std::to_string(d.hi) + "\u00b0/" + std::to_string(d.lo) + "\u00b0";
        cairo_text_extents(cr, mm.c_str(), &te);
        cairo_rel_move_to(cr, -te.x_advance * 0.5, 0);
        cairo_show_text(cr, mm.c_str());
      }
    }
    cairo_restore(cr);
  }
  cairo_restore(cr);
}
