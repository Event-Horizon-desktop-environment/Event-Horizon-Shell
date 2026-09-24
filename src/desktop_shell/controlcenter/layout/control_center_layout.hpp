#pragma once

// Single source of truth for the control-center popup vertical stack.
//
// Paint, hit-testing, dispatch, and popup sizing ALL consume CcLayout, so a
// click always lands where the pixels are. Previously paint used its own
// inline numbers while hit/geometry used a stale fixed layout plus a modal
// system paint never implemented — which is why nothing lined up.

#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/debug/control_center_log.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace eh::shell::dock::control_center {

struct CcMixerRow {
  double y = 0.0;      // row top (46px tall)
  bool isInput = false;
  int streamIdx = 0;   // index into out/in stream vectors
};

struct CcLayout {
  double W = 0.0;
  double pad = 18.0;
  double gap = 12.0;
  double cardR = 20.0;
  double rowR = 14.0;

  // Top tiles.
  double gridY = 18.0;
  double tileH = 46.0;
  double tileW = 0.0;
  double netX = 18.0;
  double btX = 0.0;

  // Expandable panels (h == 0 when collapsed).
  double netY = 0.0, netH = 0.0;
  int netRows = 0;
  double netHeaderH = 32.0;
  double btY = 0.0, btH = 0.0;
  int btRows = 0;

  // Audio cards + device panels.
  double audioY = 0.0;
  double audioH = 92.0;
  double outDevY = 0.0, outDevH = 0.0;
  int outDevRows = 0;
  double inDevY = 0.0, inDevH = 0.0;
  int inDevRows = 0;

  // Mixer.
  double mixerY = 0.0, mixerH = 0.0;
  int mixTotalOut = 0, mixTotalIn = 0;
  int mixShowOut = 0, mixShowIn = 0;
  bool mixMore = false;
  std::vector<CcMixerRow> mixRows;

  // Media + weather.
  double mediaY = 0.0;
  double mediaH = 92.0;
  double weatherY = 0.0, weatherH = 92.0;

  double totalH = 0.0;
};

inline double cc_anim_h(uint64_t nowMs, uint64_t startMs, bool from, bool to, double fullH) {
  if (!startMs) return to ? fullH : 0.0;
  return cc_anim_panel_h(nowMs, startMs, from, to, fullH);
}

inline CcLayout cc_compute_layout(double popupW, ControlCenterState& state, uint64_t nowMs,
                                   bool resolveAnims = false) {
  CcTimer t("layout", 2000);
  CcLayout L;
  L.W = popupW;
  L.gridY = L.pad;
  L.tileW = (popupW - L.pad * 2.0 - L.gap) * 0.5;
  L.netX = L.pad;
  L.btX = L.pad + L.tileW + L.gap;

  double y = L.gridY + L.tileH + L.gap;

  // Network panel (header grows when an error line is shown).
  {
    const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
    L.netRows = std::min(8, static_cast<int>(aps.size()));
    L.netHeaderH = state.wifiLastError.empty() ? 32.0 : 50.0;
    double fullH = L.netHeaderH + static_cast<double>(L.netRows) * 34.0 + 12.0;
    if (state.wifiPasswordPrompt) fullH += 56.0;
    const bool to = state.netAnimStartMs ? state.netAnimToExpanded : state.networkExpanded;
    const bool from = state.netAnimStartMs ? state.netAnimFromExpanded : state.networkExpanded;
    if (resolveAnims) {
      L.netH = to ? fullH : 0.0;
    } else {
      L.netH = cc_anim_h(nowMs, state.netAnimStartMs, from, to, fullH);
      if (state.netAnimStartMs && nowMs - state.netAnimStartMs >= 200) {
        state.netAnimStartMs = 0;
        state.netAnimFromExpanded = state.networkExpanded;
        state.netAnimToExpanded = state.networkExpanded;
      }
    }
    L.netY = y;
    if (L.netH > 2.0) y += L.netH + L.gap;
    else L.netH = 0.0;
  }

  // Bluetooth panel.
  {
    const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
    L.btRows = std::min(8, static_cast<int>(devs.size()));
    const double fullH = 44.0 + static_cast<double>(L.btRows) * 34.0 + 12.0;
    const bool to = state.btAnimStartMs ? state.btAnimToExpanded : state.bluetoothExpanded;
    const bool from = state.btAnimStartMs ? state.btAnimFromExpanded : state.bluetoothExpanded;
    if (resolveAnims) {
      L.btH = to ? fullH : 0.0;
    } else {
      L.btH = cc_anim_h(nowMs, state.btAnimStartMs, from, to, fullH);
      if (state.btAnimStartMs && nowMs - state.btAnimStartMs >= 200) {
        state.btAnimStartMs = 0;
        state.btAnimFromExpanded = state.bluetoothExpanded;
        state.btAnimToExpanded = state.bluetoothExpanded;
      }
    }
    L.btY = y;
    if (L.btH > 2.0) y += L.btH + L.gap;
    else L.btH = 0.0;
  }

  // Audio cards.
  L.audioY = y;
  y += L.audioH + L.gap;

  // Output devices panel.
  {
    const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
    L.outDevRows = std::min(12, static_cast<int>(devs.size()));
    L.outDevH = state.outputDevicesExpanded ? (32.0 + static_cast<double>(L.outDevRows) * 34.0 + 12.0) : 0.0;
    L.outDevY = y;
    if (L.outDevH > 2.0) y += L.outDevH + L.gap;
    else L.outDevH = 0.0;
  }

  // Input devices panel.
  {
    const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
    L.inDevRows = std::min(12, static_cast<int>(devs.size()));
    L.inDevH = state.inputDevicesExpanded ? (32.0 + static_cast<double>(L.inDevRows) * 34.0 + 12.0) : 0.0;
    L.inDevY = y;
    if (L.inDevH > 2.0) y += L.inDevH + L.gap;
    else L.inDevH = 0.0;
  }

  // Mixer.
  {
    const auto out = eh::shell::dock_slot_hooks::control_center_mixer_streams();
    const auto in = eh::shell::dock_slot_hooks::control_center_input_mixer_streams();
    L.mixTotalOut = static_cast<int>(out.size());
    L.mixTotalIn = static_cast<int>(in.size());
    L.mixShowOut = state.mixerExpanded ? std::min(4, L.mixTotalOut) : std::min(1, L.mixTotalOut);
    L.mixShowIn = state.mixerExpanded ? std::min(3, L.mixTotalIn) : 0;
    L.mixMore = !state.mixerExpanded && L.mixTotalOut > 1;
    namespace sm = eh::shell::cc_slider;
    double body = sm::kMixerHeaderH;
    double ry = 0.0;
    if (L.mixShowOut > 0) {
      if (state.mixerExpanded) {
        body += sm::kMixerSectionH;
        ry = sm::kMixerHeaderH + sm::kMixerSectionH;
      } else {
        ry = sm::kMixerHeaderH;
      }
      for (int i = 0; i < L.mixShowOut; ++i) {
        L.mixRows.push_back({ry, false, i});
        ry += sm::kMixerRowH;
      }
      body += static_cast<double>(L.mixShowOut) * sm::kMixerRowH;
    }
    if (L.mixMore) {
      body += sm::kMixerMoreH;
      ry += sm::kMixerMoreH;
    }
    if (state.mixerExpanded && L.mixShowIn > 0) {
      body += sm::kMixerSectionH;
      ry += sm::kMixerSectionH;
      for (int i = 0; i < L.mixShowIn; ++i) {
        L.mixRows.push_back({ry, true, i});
        ry += sm::kMixerRowH;
      }
      body += static_cast<double>(L.mixShowIn) * sm::kMixerRowH;
    }
    L.mixerY = y;
    L.mixerH = body + L.pad;
    // mixRows store offsets — shift to absolute.
    for (auto& r : L.mixRows) r.y += L.mixerY;
    y += L.mixerH + L.gap;
  }

  // Media.
  L.mediaY = y;
  y += L.mediaH + L.gap;

  // Weather is always fully shown (no expand/collapse).
  {
    constexpr double kWeatherFullH = 240.0;
    L.weatherH = kWeatherFullH;
    L.weatherY = y;
    y += L.weatherH + L.pad;
  }

  L.totalH = y;
  return L;
}

// Row-band helpers (full-width hit bands, 34px pitch, 32px header).
inline double cc_panel_row_y(double panelY, int idx) { return panelY + 32.0 + static_cast<double>(idx) * 34.0; }

// Bluetooth row action geometry (mirrors paint): one uniform action button +
// a small icon button for Forget. rightEdge = inner right of the inset row.
struct CcBtBtns {
  double actionX = 0.0;
  double actionW = 76.0;
  double forgetX = -1.0;
  double forgetW = 32.0;
  bool showForget = false;
};
inline CcBtBtns cc_bt_btns(bool connected, bool paired, double rightEdge) {
  (void)connected;
  CcBtBtns b;
  b.showForget = paired;
  if (b.showForget) {
    b.forgetX = rightEdge - 8.0 - b.forgetW;
    b.actionX = b.forgetX - 6.0 - b.actionW;
  } else {
    b.actionX = rightEdge - 8.0 - b.actionW;
  }
  return b;
}

// Media transport buttons (mirrors paint).
inline double cc_media_btn_cx(double cardX, double cardW, int idx) {
  constexpr double btnR = 14.0, gap = 10.0;
  const double cx2 = cardX + cardW - 22.0;
  return cx2 - static_cast<double>(2 - idx) * (btnR * 2.0 + gap);
}
inline double cc_media_btn_cy(double cardY, double cardH) { return cardY + cardH - 24.0; }

} // namespace eh::shell::dock::control_center
