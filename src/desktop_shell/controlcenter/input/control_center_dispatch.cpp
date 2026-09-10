#include "desktop_shell/controlcenter/input/control_center_dispatch.hpp"

#include "desktop_shell/controlcenter/input/control_center_hit.hpp"
#include "desktop_shell/controlcenter/persist/control_center_persist.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"

using eh::shell::str::utf8_pop_back;

#include <iostream>
#include <algorithm>
#include <cmath>

using eh::shell::dock::control_center::ControlCenterActiveModal;

namespace eh::shell::control_center {

bool handle_button_press(DockApp& app, uint32_t serial) {
  if (app.popupKind != DockApp::PopupKind::ControlCenter) return false;
  auto& s = app.ccState;

  // Grid card clicks (open modals).
  if (s.activeModal == ControlCenterActiveModal::None) {
    if (control_center_network_card_hit(app, app.pointerX, app.pointerY)) {
      s.activeModal = ControlCenterActiveModal::Network;
      (void)eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
      popup_open_control_center(app, app.popupAnchorX, serial);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (control_center_bluetooth_card_hit(app, app.pointerX, app.pointerY)) {
      s.activeModal = ControlCenterActiveModal::Bluetooth;
      eh::shell::dock_slot_hooks::bluetooth_ensure_service();
      eh::shell::dock_slot_hooks::bluetooth_start_discovery();
      popup_open_control_center(app, app.popupAnchorX, serial);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (control_center_audio_card_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY) &&
        !control_center_audio_slider_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY, nullptr) &&
        !control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY)) {
      s.activeModal = ControlCenterActiveModal::AudioOutput;
      popup_open_control_center(app, app.popupAnchorX, serial);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (control_center_audio_card_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY) &&
        !control_center_audio_slider_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY, nullptr) &&
        !control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY)) {
      s.activeModal = ControlCenterActiveModal::AudioInput;
      popup_open_control_center(app, app.popupAnchorX, serial);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
  }

  // Modal backdrop / close dismiss.
  if (s.activeModal != ControlCenterActiveModal::None) {
    if (control_center_modal_backdrop_hit(app, app.pointerX, app.pointerY) ||
        control_center_modal_close_hit(app, app.pointerX, app.pointerY)) {
      s.activeModal = ControlCenterActiveModal::None;
      popup_open_control_center(app, app.popupAnchorX, serial);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
  }

  // Modal row selections.
  if (s.activeModal == ControlCenterActiveModal::Network) {
    int idx = -1;
    if (control_center_network_row_hit(app, app.pointerX, app.pointerY, &idx)) {
      const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
      if (idx >= 0 && idx < static_cast<int>(aps.size())) {
        const auto& ap = aps[static_cast<size_t>(idx)];
        s.wifiPendingSsid = ap.ssid;
        s.wifiLastError.clear();
        if (ap.needs_password) {
          std::string err;
          (void)eh::shell::dock_slot_hooks::control_center_wifi_connect(ap.ssid, {}, &err);
          if (!err.empty()) s.wifiLastError = err;
          s.wifiPasswordPrompt = true;
          s.wifiPassword.clear();
          popup_open_control_center(app, app.popupAnchorX, serial);
        } else {
          std::string err;
          const bool ok = eh::shell::dock_slot_hooks::control_center_wifi_connect(ap.ssid, {}, &err);
          if (!ok) s.wifiLastError = err;
          s.wifiPasswordPrompt = false;
          s.wifiPassword.clear();
          popup_open_control_center(app, app.popupAnchorX, serial);
        }
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
  }

  if (s.activeModal == ControlCenterActiveModal::Bluetooth) {
    int idx = -1;
    if (control_center_bluetooth_row_forget_hit(app, app.pointerX, app.pointerY, &idx)) {
      const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
      if (idx >= 0 && idx < static_cast<int>(devs.size())) {
        eh::shell::dock_slot_hooks::bluetooth_forget_device(devs[static_cast<size_t>(idx)].path);
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
    if (control_center_bluetooth_row_hit(app, app.pointerX, app.pointerY, &idx)) {
      const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
      if (idx >= 0 && idx < static_cast<int>(devs.size())) {
        const auto& d = devs[static_cast<size_t>(idx)];
        if (d.connected)
          eh::shell::dock_slot_hooks::bluetooth_disconnect_device(d.path);
        else if (d.paired)
          eh::shell::dock_slot_hooks::bluetooth_connect_device(d.path);
        else
          eh::shell::dock_slot_hooks::bluetooth_pair_device(d.path);
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
  }

  if (s.activeModal == ControlCenterActiveModal::AudioOutput) {
    int idx = -1;
    if (control_center_output_devices_row_hit(app, app.pointerX, app.pointerY, &idx)) {
      const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
      if (idx >= 0 && idx < static_cast<int>(devs.size())) {
        const std::string sink = devs[static_cast<size_t>(idx)].sink_name;
        std::cout << "[cc-audio][outdev][dock] click_select idx=" << idx << " sink_name=\"" << sink << "\"\n";
        eh::shell::dock_slot_hooks::control_center_set_default_sink(sink);
        cc_save_sink(sink);
        s.outputDevicesPendingSink = sink;
        s.outputDevicesIgnoreUntilMs = now_mono_ms() + 900;
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
  }

  if (s.activeModal == ControlCenterActiveModal::AudioInput) {
    int idx = -1;
    if (control_center_input_devices_row_hit(app, app.pointerX, app.pointerY, &idx)) {
      const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
      if (idx >= 0 && idx < static_cast<int>(devs.size())) {
        const std::string src = devs[static_cast<size_t>(idx)].source_name;
        std::cout << "[cc-audio][indev][dock] click_select idx=" << idx << " source_name=\"" << src << "\"\n";
        eh::shell::dock_slot_hooks::control_center_set_default_source(src);
        cc_save_source(src);
        s.inputDevicesPendingSource = src;
        s.inputDevicesIgnoreUntilMs = now_mono_ms() + 900;
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
  }

  // Audio controls (always available).
  if (control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY)) {
    const auto as = eh::shell::dock_slot_hooks::control_center_audio_output_state();
    eh::shell::dock_slot_hooks::control_center_set_audio_output_mute(!as.muted);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY)) {
    const auto is = eh::shell::dock_slot_hooks::control_center_audio_input_state();
    eh::shell::dock_slot_hooks::control_center_set_audio_input_mute(!is.muted);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  double t = 0.0;
  if (control_center_audio_slider_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY, &t)) {
    const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
    eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(static_cast<double>(pct) / 100.0);
    s.inputDragActive = false;
    s.inputDragVisualT = -1.0;
    s.mixerDragActive = false;
    s.mixerDragVisualT = -1.0;
    s.audioDragActive = true;
    s.audioDragVisualT = t;
    s.audioDragUiPct = pct;
    s.audioLastAppliedPct = pct;
    s.audioLastApplyMs = now_mono_ms();
    s.audioIgnoreStateUntilMs = s.audioLastApplyMs + 180;
    s.audioLastLoggedPct = pct;
    std::cout << "[cc-audio][dock] drag_start x=" << app.pointerX << " y=" << app.pointerY
              << " t=" << (static_cast<double>(pct) / 100.0) << " pct=" << s.audioLastLoggedPct << "\n";
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (control_center_audio_slider_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY, &t)) {
    const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
    eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(static_cast<double>(pct) / 100.0);
    s.audioDragActive = false;
    s.audioDragVisualT = -1.0;
    s.mixerDragActive = false;
    s.mixerDragVisualT = -1.0;
    s.inputDragActive = true;
    s.inputDragVisualT = t;
    s.inputDragUiPct = pct;
    s.inputLastAppliedPct = pct;
    s.inputLastApplyMs = now_mono_ms();
    s.inputIgnoreStateUntilMs = s.inputLastApplyMs + 180;
    s.inputLastLoggedPct = pct;
    std::cout << "[cc-audio][input][dock] drag_start x=" << app.pointerX << " y=" << app.pointerY
              << " t=" << (static_cast<double>(pct) / 100.0) << " pct=" << s.inputLastLoggedPct << "\n";
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  {
    int streamId = -1;
    bool isInput = false;
    double sxMix = 0.0, swMix = 1.0;
    bool hit = control_center_mixer_expanded_slider_hit(app, app.pointerX, app.pointerY, &streamId, &isInput, &sxMix,
                                                        &swMix, &t);
    if (!hit) {
      const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
      const int rows = std::min(static_cast<int>(streams.size()), 1);
      for (int i = 0; i < rows; ++i) {
        if (control_center_mixer_slider_hit(app, i, app.pointerX, app.pointerY, &t)) {
          streamId = streams[static_cast<size_t>(i)].sink_input_id;
          isInput = false;
          double syMix = 0.0, shMix = 0.0;
          (void)control_center_mixer_slider_geom(app, i, &sxMix, &syMix, &swMix, &shMix);
          sxMix += eh::shell::cc_slider::kMixerHitPadH;
          swMix -= 2.0 * eh::shell::cc_slider::kMixerHitPadH;
          hit = true;
          break;
        }
      }
    }
    if (hit && streamId >= 0) {
      const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
      s.audioDragActive = false;
      s.audioDragVisualT = -1.0;
      s.inputDragActive = false;
      s.inputDragVisualT = -1.0;
      s.mixerDragActive = true;
      s.mixerDragStreamId = streamId;
      s.mixerDragIsInput = isInput;
      s.mixerDragSx = sxMix;
      s.mixerDragSw = swMix;
      s.mixerDragVisualT = t;
      s.mixerDragUiPct = pct;
      s.mixerLastAppliedPct = pct;
      s.mixerLastApplyMs = now_mono_ms();
      s.mixerIgnoreStateUntilMs = s.mixerLastApplyMs + 180;
      s.mixerLastLoggedPct = pct;
      if (isInput)
        eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(streamId, static_cast<double>(pct) / 100.0);
      else
        eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(streamId, static_cast<double>(pct) / 100.0);
      std::cout << "[cc-audio][mixer][dock] drag_start id=" << streamId << " x=" << app.pointerX << " y="
                << app.pointerY << " pct=" << pct << "\n";
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
  }

  // Media and weather.
  const int mediaZone = control_center_media_button_hit(app, app.pointerX, app.pointerY);
  if (mediaZone >= 0) {
    if (app.mpris) {
      if (mediaZone == 0) app.mpris->previous();
      if (mediaZone == 1) app.mpris->play_pause();
      if (mediaZone == 2) app.mpris->next();
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (control_center_weather_card_hit(app, app.pointerX, app.pointerY)) {
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  return false;
}

bool handle_motion(DockApp& app) {
  if (!app.popupOpen || app.popupKind != DockApp::PopupKind::ControlCenter ||
      app.pointerSurface != app.popupSurface)
    return false;
  if (!app.ccState.audioDragActive && !app.ccState.inputDragActive && !app.ccState.mixerDragActive)
    return false;

  auto& s = app.ccState;

  if (s.mixerDragActive) {
    const double t = std::clamp((app.pointerX - s.mixerDragSx) /
                                    std::max(1.0, s.mixerDragSw),
                                0.0, 1.0);
    s.mixerDragVisualT = t;
    const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
    s.mixerDragUiPct = pct;
    if (pct != s.mixerLastLoggedPct) {
      s.mixerLastLoggedPct = pct;
      std::cout << "[cc-audio][mixer][dock] drag_move x=" << app.pointerX << " t=" << t << " pct=" << pct << "\n";
    }
    const uint64_t nowMs = now_mono_ms();
    if (s.mixerLastApplyMs == 0 || nowMs - s.mixerLastApplyMs >= 16) {
      if (s.mixerDragIsInput)
        eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(s.mixerDragStreamId, t);
      else
        eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(s.mixerDragStreamId, t);
      s.mixerLastAppliedPct = pct;
      s.mixerLastApplyMs = nowMs;
      s.mixerIgnoreStateUntilMs = nowMs + 180;
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  const CcAudioSliderKind kind = s.audioDragActive ? CcAudioSliderKind::Output : CcAudioSliderKind::Input;
  const double t = control_center_audio_slider_value_from_x(app, kind, app.pointerX);
  const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
  const uint64_t nowMs = now_mono_ms();
  if (kind == CcAudioSliderKind::Output) {
    s.audioDragVisualT = t;
    s.audioDragUiPct = pct;
    if (pct != s.audioLastLoggedPct) {
      s.audioLastLoggedPct = pct;
      std::cout << "[cc-audio][dock] drag_move x=" << app.pointerX << " t=" << t << " pct=" << pct << "\n";
    }
    if (s.audioLastApplyMs == 0 || nowMs - s.audioLastApplyMs >= 16) {
      eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(t);
      s.audioLastAppliedPct = pct;
      s.audioLastApplyMs = nowMs;
      s.audioIgnoreStateUntilMs = nowMs + 180;
    }
  } else {
    s.inputDragVisualT = t;
    s.inputDragUiPct = pct;
    if (pct != s.inputLastLoggedPct) {
      s.inputLastLoggedPct = pct;
      std::cout << "[cc-audio][input][dock] drag_move x=" << app.pointerX << " t=" << t << " pct=" << pct << "\n";
    }
    if (s.inputLastApplyMs == 0 || nowMs - s.inputLastApplyMs >= 16) {
      eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(t);
      s.inputLastAppliedPct = pct;
      s.inputLastApplyMs = nowMs;
      s.inputIgnoreStateUntilMs = nowMs + 180;
    }
  }
  popup_draw_surface(app);
  wl_display_flush(app.display);
  return true;
}

bool handle_button_release(DockApp& app) {
  bool handled = false;
  auto& s = app.ccState;

  if (s.audioDragActive) {
    const int pct = control_center_audio_pct_from_x(app, CcAudioSliderKind::Output, app.pointerX);
    eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(static_cast<double>(pct) / 100.0);
    std::cout << "[cc-audio][dock] drag_end x=" << app.pointerX << " pct=" << pct << "\n";
    s.audioDragActive = false;
    s.audioDragVisualT = -1.0;
    s.audioLastLoggedPct = -1;
    s.audioDragUiPct = -1;
    s.audioLastAppliedPct = pct;
    s.audioLastApplyMs = now_mono_ms();
    s.audioIgnoreStateUntilMs = s.audioLastApplyMs + 180;
    handled = true;
  }

  if (s.inputDragActive) {
    const int pct = control_center_audio_pct_from_x(app, CcAudioSliderKind::Input, app.pointerX);
    eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(static_cast<double>(pct) / 100.0);
    std::cout << "[cc-audio][input][dock] drag_end x=" << app.pointerX << " pct=" << pct << "\n";
    s.inputDragActive = false;
    s.inputDragVisualT = -1.0;
    s.inputLastLoggedPct = -1;
    s.inputDragUiPct = -1;
    s.inputLastAppliedPct = pct;
    s.inputLastApplyMs = now_mono_ms();
    s.inputIgnoreStateUntilMs = s.inputLastApplyMs + 180;
    handled = true;
  }

  if (s.mixerDragActive) {
    const int pct = std::clamp(static_cast<int>(std::lround(
                                   std::clamp((app.pointerX - s.mixerDragSx) /
                                                  std::max(1.0, s.mixerDragSw),
                                              0.0, 1.0) *
                                   100.0)),
                              0, 100);
    if (s.mixerDragIsInput)
      eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(s.mixerDragStreamId,
                                                                 static_cast<double>(pct) / 100.0);
    else
      eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(s.mixerDragStreamId,
                                                            static_cast<double>(pct) / 100.0);
    std::cout << "[cc-audio][mixer][dock] drag_end x=" << app.pointerX << " pct=" << pct << "\n";
    s.mixerDragActive = false;
    s.mixerDragVisualT = -1.0;
    s.mixerLastLoggedPct = -1;
    s.mixerDragUiPct = -1;
    s.mixerLastAppliedPct = pct;
    s.mixerLastApplyMs = now_mono_ms();
    s.mixerIgnoreStateUntilMs = s.mixerLastApplyMs + 180;
    s.mixerDragSx = 0.0;
    s.mixerDragSw = 1.0;
    handled = true;
  }

  return handled;
}

bool handle_axis(DockApp& app, double deltaPx) {
  if (!app.popupOpen || app.pointerSurface != app.popupSurface ||
      app.popupKind != DockApp::PopupKind::ControlCenter)
    return false;

  const double dv = deltaPx / 20.0;
  const int dPct = static_cast<int>(std::lround(-dv * 5.0));
  if (dPct == 0) return true;

  const double px = app.pointerX;
  const double py = app.pointerY;

  if (control_center_audio_slider_hit(app, CcAudioSliderKind::Output, px, py, nullptr)) {
    const auto as = eh::shell::dock_slot_hooks::control_center_audio_output_state();
    const int pct = std::clamp(as.volume_pct + dPct, 0, 100);
    eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(static_cast<double>(pct) / 100.0);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  if (control_center_audio_slider_hit(app, CcAudioSliderKind::Input, px, py, nullptr)) {
    const auto ins = eh::shell::dock_slot_hooks::control_center_audio_input_state();
    const int pct = std::clamp(ins.volume_pct + dPct, 0, 100);
    eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(static_cast<double>(pct) / 100.0);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  int sid = -1;
  bool isIn = false;
  double sxMix = 0.0, swMix = 0.0;
  double tMix = 0.0;
  if (control_center_mixer_expanded_slider_hit(app, px, py, &sid, &isIn, &sxMix, &swMix, &tMix)) {
    int cpct = 0;
    bool found = false;
    if (!isIn) {
      for (const auto& s : eh::shell::dock_slot_hooks::control_center_mixer_streams()) {
        if (s.sink_input_id == sid) {
          cpct = s.volume_pct;
          found = true;
          break;
        }
      }
    } else {
      for (const auto& s : eh::shell::dock_slot_hooks::control_center_input_mixer_streams()) {
        if (s.sink_input_id == sid) {
          cpct = s.volume_pct;
          found = true;
          break;
        }
      }
    }
    if (found) {
      const int np = std::clamp(cpct + dPct, 0, 100);
      if (isIn)
        eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(sid, static_cast<double>(np) / 100.0);
      else
        eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(sid, static_cast<double>(np) / 100.0);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    return true;
  }

  const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
  const int rows = std::min(1, static_cast<int>(streams.size()));
  for (int i = 0; i < rows; ++i) {
    if (control_center_mixer_slider_hit(app, i, px, py, nullptr)) {
      const auto& s = streams[static_cast<size_t>(i)];
      const int np = std::clamp(s.volume_pct + dPct, 0, 100);
      eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(s.sink_input_id,
                                                                         static_cast<double>(np) / 100.0);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
  }

  return true;
}

bool handle_keyboard(DockApp& app, xkb_keysym_t sym, uint32_t keycode) {
  if (app.popupKind != DockApp::PopupKind::ControlCenter) return false;
  if (app.ccState.activeModal != ControlCenterActiveModal::Network) return false;
  if (!app.ccState.wifiPasswordPrompt) return false;

  auto& s = app.ccState;

  if (sym == XKB_KEY_Escape) {
    s.wifiPasswordPrompt = false;
    s.wifiPassword.clear();
    s.wifiPendingSsid.clear();
    s.activeModal = ControlCenterActiveModal::None;
    popup_open_control_center(app, app.popupAnchorX, 0);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    utf8_pop_back(s.wifiPassword);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    std::string err;
    const bool ok = eh::shell::dock_slot_hooks::control_center_wifi_connect(s.wifiPendingSsid, s.wifiPassword, &err);
    if (!ok) s.wifiLastError = err;
    else s.wifiLastError.clear();
    s.wifiPasswordPrompt = false;
    s.wifiPassword.clear();
    s.activeModal = ControlCenterActiveModal::None;
    popup_open_control_center(app, app.popupAnchorX, 0);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  char utf8Pw[128]{};
  const int nPw = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8Pw, sizeof(utf8Pw) - 1);
  if (nPw > 0) {
    s.wifiPassword.append(utf8Pw, static_cast<size_t>(nPw));
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
  return true;
}

}
