#include "desktop_shell/controlcenter/input/control_center_dispatch.hpp"

#include "desktop_shell/controlcenter/input/control_center_bus_hook.hpp"
#include "desktop_shell/controlcenter/input/control_center_hit.hpp"
#include "desktop_shell/controlcenter/input/control_center_pear_hit.hpp"
#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/paint/control_center_pear_paint.hpp"
#include "desktop_shell/controlcenter/persist/control_center_persist.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
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
#include <spawn.h>
#include <unistd.h>

#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/debug/control_center_log.hpp"

using eh::shell::dock::control_center::ControlCenterActiveModal;
using eh::shell::dock::control_center::CcHoverTarget;

namespace eh::shell::control_center {

// Re-create the popup at the expansion-aware height. Creation placement is
// always correct (bottom pinned above the dock), and popup_close preserves
// expansion flags, so this both resizes and keeps position. Live set_size is
// deliberately not used: on this compositor a resized surface keeps its top
// edge, which pushes growth down under the dock.
static void cc_resize_popup(DockApp& app, uint32_t serial, const char* reason) {
  namespace ccl = eh::shell::dock::control_center;
  const int wantH = static_cast<int>(std::ceil(control_center_popup_height(app)));
  ccl::cc_log(std::string("resize reason=") + reason + " open w=" + std::to_string(app.popupW) +
              " h=" + std::to_string(app.popupH) + " want=" + std::to_string(wantH) +
              " anchorX=" + std::to_string(app.popupAnchorX));
  if (wantH < app.popupH) {
    // Shrinking: leave the surface in place so the collapse animation plays
    // unclipped; the settle check downsizes once it finishes.
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return;
  }
  popup_open_control_center(app, app.popupAnchorX, serial, false);
  ccl::cc_log(std::string("resize created w=") + std::to_string(app.popupW) +
              " h=" + std::to_string(app.popupH));
  popup_draw_surface(app);
  wl_display_flush(app.display);
}

static void cc_toggle_expand(bool& flag, uint64_t& animStart, bool& animFrom, bool& animTo) {
  animFrom = flag;
  flag = !flag;
  animTo = flag;
  animStart = now_mono_ms();
}

static void cc_update_hover(DockApp& app) {
  auto& s = app.ccState;
  const double px = app.pointerX, py = app.pointerY;
  // PearCenter compact layout hover (Section A/B + networks overlay).
  {
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    std::string wid = dock_active_control_center_widget_id(app);
    if (wid.empty()) wid = "control_center";
    if (eh::shell::dock::control_center::pear_layout_enabled(sc, wid)) {
      const PearHitContext pctx{static_cast<double>(app.popupW > 0 ? app.popupW : 360), s, wid};
      CcHoverTarget t = CcHoverTarget::None;
      int row = -1;
      const int mz = pear_media_button_hit(pctx, px, py);
      if (mz == 0) t = CcHoverTarget::MediaPrev;
      else if (mz == 1) t = CcHoverTarget::MediaPlayPause;
      else if (mz == 2) t = CcHoverTarget::MediaNext;
      double dummy = 0.0;
      if (t == CcHoverTarget::None && pear_volume_mute_hit(pctx, px, py)) t = CcHoverTarget::OutputAudioMute;
      if (t == CcHoverTarget::None && pear_volume_slider_hit(pctx, px, py, &dummy)) t = CcHoverTarget::VolumeCard;
      if (t == CcHoverTarget::None && pear_input_mute_hit(pctx, px, py)) t = CcHoverTarget::InputAudioMute;
      if (t == CcHoverTarget::None && pear_input_slider_hit(pctx, px, py, &dummy)) t = CcHoverTarget::InputAudioSlider;
      if (t == CcHoverTarget::None && pear_brightness_slider_hit(pctx, px, py, &dummy))
        t = CcHoverTarget::BrightnessCard;
      if (t == CcHoverTarget::None && pear_output_device_row_hit(pctx, px, py, &row))
        t = CcHoverTarget::OutputDeviceRow;
      if (t == CcHoverTarget::None && pear_input_device_row_hit(pctx, px, py, &row))
        t = CcHoverTarget::InputDeviceRow;
      if (t == CcHoverTarget::None && pear_networks_back_hit(pctx, px, py)) t = CcHoverTarget::NetworkCard;
      if (t == CcHoverTarget::None && pear_wifi_toggle_hit(pctx, px, py)) t = CcHoverTarget::NetworkGrid;
      if (t == CcHoverTarget::None && pear_network_row_hit(pctx, px, py, &row)) t = CcHoverTarget::NetworkRow;
      if (t == CcHoverTarget::None && pear_network_card_hit(pctx, px, py)) t = CcHoverTarget::NetworkCard;
      if (t == CcHoverTarget::None && pear_bluetooth_card_hit(pctx, px, py)) t = CcHoverTarget::BluetoothCard;
      if (t == CcHoverTarget::None && pear_settings_row_hit(pctx, px, py)) t = CcHoverTarget::SettingsRow;
      if (t == CcHoverTarget::None && pear_dnd_card_hit(pctx, px, py)) t = CcHoverTarget::DndCard;
      if (t == CcHoverTarget::None) {
        const int ti = pear_toggle_hit(pctx, px, py);
        if (ti >= 0) {
          const eh::config::ShellConfig& sc2 = eh::config::shell_config_snapshot();
          const auto cfg = eh::shell::dock::control_center::pear_center_config(sc2, wid);
          const auto L = eh::shell::dock::control_center::cc_compute_pear_layout(
              pctx.popupW, s, cfg, dock_ui_scale(sc2.dock));
          if (static_cast<size_t>(ti) < L.toggles.size()) {
            switch (L.toggles[static_cast<size_t>(ti)]) {
              case eh::shell::dock::control_center::PearToggle::DeviceLink:
                t = CcHoverTarget::DeviceLinkCard;
                break;
              case eh::shell::dock::control_center::PearToggle::NightColor:
                t = CcHoverTarget::NightColorCard;
                break;
              case eh::shell::dock::control_center::PearToggle::ColorScheme:
                t = CcHoverTarget::ColorSchemeCard;
                break;
              case eh::shell::dock::control_center::PearToggle::Camera:
                t = CcHoverTarget::CameraCard;
                break;
              case eh::shell::dock::control_center::PearToggle::Cmd1:
                t = CcHoverTarget::CmdCard1;
                break;
              case eh::shell::dock::control_center::PearToggle::Cmd2:
                t = CcHoverTarget::CmdCard2;
                break;
            }
            row = ti;
          }
        }
      }
      if (t != s.hoverTarget || row != s.hoverRowIdx) {
        s.hoverTarget = t;
        s.hoverRowIdx = row;
        s.hoverStreamId = -1;
        popup_draw_surface(app);
        wl_display_flush(app.display);
      }
      return;
    }
  }
  CcHoverTarget t = CcHoverTarget::None;
  int row = -1, sid = -1;

  // Buttons first (small targets), then rows, then cards.
  const int mz = control_center_media_button_hit(app, px, py);
  if (mz == 0) t = CcHoverTarget::MediaPrev;
  else if (mz == 1) t = CcHoverTarget::MediaPlayPause;
  else if (mz == 2) t = CcHoverTarget::MediaNext;
  if (t == CcHoverTarget::None && control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Output, px, py))
    t = CcHoverTarget::OutputAudioMute;
  if (t == CcHoverTarget::None && control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Input, px, py))
    t = CcHoverTarget::InputAudioMute;
  double dummy = 0.0;
  if (t == CcHoverTarget::None && control_center_audio_slider_hit(app, CcAudioSliderKind::Output, px, py, &dummy))
    t = CcHoverTarget::OutputAudioSlider;
  if (t == CcHoverTarget::None && control_center_audio_slider_hit(app, CcAudioSliderKind::Input, px, py, &dummy))
    t = CcHoverTarget::InputAudioSlider;
  if (t == CcHoverTarget::None && control_center_network_card_hit(app, px, py)) t = CcHoverTarget::NetworkCard;
  if (t == CcHoverTarget::None && control_center_bluetooth_card_hit(app, px, py)) t = CcHoverTarget::BluetoothCard;
  if (t == CcHoverTarget::None && control_center_audio_card_hit(app, CcAudioSliderKind::Output, px, py))
    t = CcHoverTarget::OutputAudioSlider;
  if (t == CcHoverTarget::None && control_center_audio_card_hit(app, CcAudioSliderKind::Input, px, py))
    t = CcHoverTarget::InputAudioSlider;
  if (t == CcHoverTarget::None && control_center_network_row_hit(app, px, py, &row)) t = CcHoverTarget::NetworkRow;
  if (t == CcHoverTarget::None && control_center_bluetooth_row_hit(app, px, py, &row))
    t = CcHoverTarget::BluetoothRow;
  if (t == CcHoverTarget::None && control_center_output_devices_row_hit(app, px, py, &row))
    t = CcHoverTarget::OutputDeviceRow;
  if (t == CcHoverTarget::None && control_center_input_devices_row_hit(app, px, py, &row))
    t = CcHoverTarget::InputDeviceRow;
  if (t == CcHoverTarget::None && control_center_mixer_settings_hit(app, px, py)) t = CcHoverTarget::MixerCard;
  if (t == CcHoverTarget::None && control_center_weather_card_hit(app, px, py)) t = CcHoverTarget::WeatherCard;
  if (t == CcHoverTarget::None) {
    int esid = -1;
    bool isIn = false;
    if (control_center_mixer_expanded_slider_hit(app, px, py, &esid, &isIn, nullptr, nullptr, nullptr)) {
      t = CcHoverTarget::MixerSlider;
      sid = esid;
      row = -1;
    }
  }
  if (t != s.hoverTarget || row != s.hoverRowIdx || sid != s.hoverStreamId) {
    s.hoverTarget = t;
    s.hoverRowIdx = row;
    s.hoverStreamId = sid;
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
}

bool handle_button_press(DockApp& app, uint32_t serial) {
  eh::shell::dock::control_center::CcTimer tPress("press", 4000);
  if (app.popupKind != DockApp::PopupKind::ControlCenter) return false;
  auto& s = app.ccState;
  s.activeModal = ControlCenterActiveModal::None; // modals retired; panels expand inline
  eh::shell::dock::control_center::cc_log(
      "press x=" + std::to_string(static_cast<int>(app.pointerX)) +
      " y=" + std::to_string(static_cast<int>(app.pointerY)));

  // PearCenter compact layout press path (Section A/B + networks overlay).
  {
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    std::string wid = dock_active_control_center_widget_id(app);
    if (wid.empty()) wid = "control_center";
    if (eh::shell::dock::control_center::pear_layout_enabled(sc, wid)) {
      namespace pearcc = eh::shell::dock::control_center;
      const PearHitContext pctx{static_cast<double>(app.popupW > 0 ? app.popupW : 360), s, wid};
      auto pear_spawn = [](const std::string& cmd) {
        if (cmd.empty()) return;
        pid_t pid = -1;
        const char* argv[] = {"sh", "-c", cmd.c_str(), nullptr};
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
        (void)posix_spawnp(&pid, "sh", nullptr, &attr,
                           const_cast<char* const*>(argv), environ);
        posix_spawnattr_destroy(&attr);
      };
      // Overlay: back + wifi toggle + AP rows.
      if (s.networksOverlay) {
        if (pear_networks_back_hit(pctx, app.pointerX, app.pointerY)) {
          s.networksOverlay = false;
          s.wifiPasswordPrompt = false;
          cc_resize_popup(app, serial, "pear-net-back");
          return true;
        }
        if (pear_wifi_toggle_hit(pctx, app.pointerX, app.pointerY)) {
          const bool on =
              eh::net::NetworkManagerService::instance().snapshot().wireless_enabled;
          eh::net::NetworkManagerService::instance().setWirelessEnabled(!on);
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
        int idx = -1;
        if (pear_network_row_hit(pctx, app.pointerX, app.pointerY, &idx)) {
          const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
          if (idx >= 0 && idx < static_cast<int>(aps.size())) {
            const auto& ap = aps[static_cast<size_t>(idx)];
            s.wifiPendingSsid = ap.ssid;
            s.wifiLastError.clear();
            std::string err;
            if (ap.needs_password) {
              (void)eh::shell::dock_slot_hooks::control_center_wifi_connect(ap.ssid, {}, &err);
              if (!err.empty()) s.wifiLastError = err;
              s.wifiPasswordPrompt = true;
              s.wifiPassword.clear();
            } else {
              const bool ok =
                  eh::shell::dock_slot_hooks::control_center_wifi_connect(ap.ssid, {}, &err);
              if (!ok) s.wifiLastError = err;
              s.wifiPasswordPrompt = false;
              s.wifiPassword.clear();
            }
            cc_resize_popup(app, serial, "pear-wifi-select");
            return true;
          }
        }
        return false;
      }
      // Network LongButton -> open the SectionNetworks overlay.
      if (pear_network_card_hit(pctx, app.pointerX, app.pointerY)) {
        (void)eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
        s.networksOverlay = true;
        s.wifiPasswordPrompt = false;
        cc_resize_popup(app, serial, "pear-net-open");
        return true;
      }
      // Bluetooth LongButton -> toggle power (reference behavior).
      if (pear_bluetooth_card_hit(pctx, app.pointerX, app.pointerY)) {
        const auto bstate = eh::shell::dock_slot_hooks::control_center_bluetooth_state();
        if (!bstate.powered) {
          eh::shell::dock_slot_hooks::bluetooth_ensure_service();
          eh::widgets::bluetooth_set_powered(true);
          eh::shell::dock_slot_hooks::bluetooth_start_discovery();
        } else {
          eh::widgets::bluetooth_set_powered(false);
        }
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      // Settings LongButton -> System Settings.
      if (pear_settings_row_hit(pctx, app.pointerX, app.pointerY)) {
        eh::settings::request_launch_settings();
        return true;
      }
      // DND -> supervisor flips notifications.doNotDisturb.
      if (pear_dnd_card_hit(pctx, app.pointerX, app.pointerY)) {
        pearcc::control_center_bus_publish("dnd.toggle");
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      // Quick toggles.
      {
        const int ti = pear_toggle_hit(pctx, app.pointerX, app.pointerY);
        if (ti >= 0) {
          const auto cfg = pearcc::pear_center_config(sc, wid);
          const auto L = pearcc::cc_compute_pear_layout(pctx.popupW, s, cfg,
                                                      dock_ui_scale(sc.dock));
          if (static_cast<size_t>(ti) < L.toggles.size()) {
            switch (L.toggles[static_cast<size_t>(ti)]) {
              case pearcc::PearToggle::DeviceLink:
                pear_spawn(cfg.deviceLinkCmd);
                break;
              case pearcc::PearToggle::NightColor: {
                const bool next = !pearcc::pear_nightlight_active(sc);
                pearcc::pear_nightlight_set_active(next);
                pearcc::control_center_bus_publish("nightlight.toggle");
                break;
              }
              case pearcc::PearToggle::ColorScheme:
                pearcc::control_center_bus_publish("color-scheme.toggle");
                break;
              case pearcc::PearToggle::Camera:
                popup_close(app);
                wl_display_flush(app.display);
                if (!cfg.cameraCmd.empty())
                  pear_spawn(cfg.cameraCmd);
                else if (!pearcc::pear_spawn_screenshot_select())
                  pearcc::cc_log("camera: screenshot selection spawn failed");
                return true;
              case pearcc::PearToggle::Cmd1:
                pear_spawn(cfg.cmdRun1);
                break;
              case pearcc::PearToggle::Cmd2:
                pear_spawn(cfg.cmdRun2);
                break;
            }
            popup_draw_surface(app);
            wl_display_flush(app.display);
            return true;
          }
        }
      }
      // Device rows (panels only exist while expanded).
      {
        int didx = -1;
        if (pear_output_device_row_hit(pctx, app.pointerX, app.pointerY, &didx)) {
          const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
          if (didx >= 0 && didx < static_cast<int>(devs.size())) {
            const std::string sink = devs[static_cast<size_t>(didx)].sink_name;
            eh::shell::dock_slot_hooks::control_center_set_default_sink(sink);
            cc_save_sink(sink);
            s.outputDevicesPendingSink = sink;
            s.outputDevicesIgnoreUntilMs = now_mono_ms() + 900;
            popup_draw_surface(app);
            wl_display_flush(app.display);
            return true;
          }
        }
        if (pear_input_device_row_hit(pctx, app.pointerX, app.pointerY, &didx)) {
          const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
          if (didx >= 0 && didx < static_cast<int>(devs.size())) {
            const std::string src = devs[static_cast<size_t>(didx)].source_name;
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
      // Volume mute + slider.
      if (pear_volume_mute_hit(pctx, app.pointerX, app.pointerY)) {
        const auto as = eh::shell::dock_slot_hooks::control_center_audio_output_state();
        eh::shell::dock_slot_hooks::control_center_set_audio_output_mute(!as.muted);
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      {
        double t = 0.0;
        if (pear_volume_slider_hit(pctx, app.pointerX, app.pointerY, &t)) {
          const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
          eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(
              static_cast<double>(pct) / 100.0);
          s.inputDragActive = false;
          s.inputDragVisualT = -1.0;
          s.mixerDragActive = false;
          s.mixerDragVisualT = -1.0;
          s.pearBriDragActive = false;
          s.pearBriDragT = -1.0;
          s.audioDragActive = true;
          s.audioDragVisualT = t;
          s.audioDragUiPct = pct;
          s.audioLastAppliedPct = pct;
          s.audioLastApplyMs = now_mono_ms();
          s.audioIgnoreStateUntilMs = s.audioLastApplyMs + 180;
          s.audioLastLoggedPct = pct;
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      // Volume card body (outside slider/mute) toggles the output switcher.
      if (pear_volume_row_hit(pctx, app.pointerX, app.pointerY)) {
        s.outputDevicesExpanded = !s.outputDevicesExpanded;
        cc_resize_popup(app, serial, "pear-outdev-toggle");
        return true;
      }
      // Input mute + slider + card body (device switcher).
      if (pear_input_mute_hit(pctx, app.pointerX, app.pointerY)) {
        const auto ii = eh::shell::dock_slot_hooks::control_center_audio_input_state();
        eh::shell::dock_slot_hooks::control_center_set_audio_input_mute(!ii.muted);
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      {
        double t = 0.0;
        if (pear_input_slider_hit(pctx, app.pointerX, app.pointerY, &t)) {
          const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
          eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(
              static_cast<double>(pct) / 100.0);
          s.audioDragActive = false;
          s.audioDragVisualT = -1.0;
          s.mixerDragActive = false;
          s.mixerDragVisualT = -1.0;
          s.pearBriDragActive = false;
          s.pearBriDragT = -1.0;
          s.inputDragActive = true;
          s.inputDragVisualT = t;
          s.inputDragUiPct = pct;
          s.inputLastAppliedPct = pct;
          s.inputLastApplyMs = now_mono_ms();
          s.inputIgnoreStateUntilMs = s.inputLastApplyMs + 180;
          s.inputLastLoggedPct = pct;
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      if (pear_input_row_hit(pctx, app.pointerX, app.pointerY)) {
        s.inputDevicesExpanded = !s.inputDevicesExpanded;
        cc_resize_popup(app, serial, "pear-indev-toggle");
        return true;
      }
      // Brightness slider (click/drag sets backlight, best-effort).
      {
        double t = 0.0;
        if (pear_brightness_slider_hit(pctx, app.pointerX, app.pointerY, &t)) {
          const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
          (void)pearcc::pear_set_brightness_pct(pct);
          s.audioDragActive = false;
          s.audioDragVisualT = -1.0;
          s.mixerDragActive = false;
          s.mixerDragVisualT = -1.0;
          s.inputDragActive = false;
          s.inputDragVisualT = -1.0;
          s.pearBriDragActive = true;
          s.pearBriDragT = t;
          s.inputLastAppliedPct = pct;
          s.inputLastApplyMs = now_mono_ms();
          s.inputIgnoreStateUntilMs = s.inputLastApplyMs + 180;
          s.inputLastLoggedPct = pct;
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      // Media transport.
      {
        const int mz = pear_media_button_hit(pctx, app.pointerX, app.pointerY);
        if (mz >= 0) {
          if (app.mpris) {
            if (mz == 0) app.mpris->previous();
            if (mz == 1) app.mpris->play_pause();
            if (mz == 2) app.mpris->next();
          }
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      return false;
    }
  }

  // Wi-Fi rows.
  {
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
        } else {
          std::string err;
          const bool ok = eh::shell::dock_slot_hooks::control_center_wifi_connect(ap.ssid, {}, &err);
          if (!ok) s.wifiLastError = err;
          s.wifiPasswordPrompt = false;
          s.wifiPassword.clear();
        }
        cc_resize_popup(app, serial, "wifi-select");
        return true;
      }
    }
  }

  // Bluetooth rows.
  {
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

  // Audio device rows.
  {
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

  // Card toggles (expand/collapse inline panels, popup resizes to fit).
  if (control_center_network_card_hit(app, app.pointerX, app.pointerY)) {
    if (!s.networkExpanded) (void)eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
    cc_toggle_expand(s.networkExpanded, s.netAnimStartMs, s.netAnimFromExpanded, s.netAnimToExpanded);
    cc_resize_popup(app, serial, "net-toggle");
    return true;
  }
  if (control_center_bluetooth_card_hit(app, app.pointerX, app.pointerY)) {
    if (!s.bluetoothExpanded) {
      eh::shell::dock_slot_hooks::bluetooth_ensure_service();
      eh::shell::dock_slot_hooks::bluetooth_start_discovery();
    }
    cc_toggle_expand(s.bluetoothExpanded, s.btAnimStartMs, s.btAnimFromExpanded, s.btAnimToExpanded);
    cc_resize_popup(app, serial, "bt-toggle");
    return true;
  }
  if (control_center_audio_card_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY) &&
      !control_center_audio_slider_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY, nullptr) &&
      !control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Output, app.pointerX, app.pointerY)) {
    s.outputDevicesExpanded = !s.outputDevicesExpanded;
    cc_resize_popup(app, serial, "outdev-toggle");
    return true;
  }
  if (control_center_audio_card_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY) &&
      !control_center_audio_slider_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY, nullptr) &&
      !control_center_audio_mute_icon_hit(app, CcAudioSliderKind::Input, app.pointerX, app.pointerY)) {
    s.inputDevicesExpanded = !s.inputDevicesExpanded;
    cc_resize_popup(app, serial, "indev-toggle");
    return true;
  }
  if (control_center_mixer_settings_hit(app, app.pointerX, app.pointerY)) {
    s.mixerExpanded = !s.mixerExpanded;
    cc_resize_popup(app, serial, "mixer-toggle");
    return true;
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
  if (!app.ccState.audioDragActive && !app.ccState.inputDragActive && !app.ccState.mixerDragActive) {
    cc_update_hover(app);
    return false;
  }

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
  // PearCenter compact drags: volume uses audioDragActive, input uses
  // inputDragActive, brightness uses pearBriDragActive.
  {
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    std::string wid = dock_active_control_center_widget_id(app);
    if (wid.empty()) wid = "control_center";
    if (eh::shell::dock::control_center::pear_layout_enabled(sc, wid) &&
        (s.audioDragActive || s.inputDragActive || s.pearBriDragActive)) {
      const PearHitContext pctx{static_cast<double>(app.popupW > 0 ? app.popupW : 360), s, wid};
      double tx = 0.0, tw = 1.0;
      bool ok = false;
      if (s.audioDragActive) {
        ok = pear_volume_track_geom(pctx, &tx, &tw);
      } else if (s.inputDragActive) {
        ok = pear_input_track_geom(pctx, &tx, &tw);
      } else {
        ok = pear_brightness_track_geom(pctx, &tx, &tw);
      }
      if (ok) {
        const double t = std::clamp((app.pointerX - tx) / std::max(1.0, tw), 0.0, 1.0);
        const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
        const uint64_t nowMs = now_mono_ms();
        if (s.audioDragActive) {
          s.audioDragVisualT = t;
          s.audioDragUiPct = pct;
          if (s.audioLastApplyMs == 0 || nowMs - s.audioLastApplyMs >= 16) {
            eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(t);
            s.audioLastAppliedPct = pct;
            s.audioLastApplyMs = nowMs;
            s.audioIgnoreStateUntilMs = nowMs + 180;
          }
        } else if (s.inputDragActive) {
          s.inputDragVisualT = t;
          s.inputDragUiPct = pct;
          if (s.inputLastApplyMs == 0 || nowMs - s.inputLastApplyMs >= 16) {
            eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(t);
            s.inputLastAppliedPct = pct;
            s.inputLastApplyMs = nowMs;
            s.inputIgnoreStateUntilMs = nowMs + 180;
          }
        } else {
          s.pearBriDragT = t;
          if (s.inputLastApplyMs == 0 || nowMs - s.inputLastApplyMs >= 16) {
            (void)eh::shell::dock::control_center::pear_set_brightness_pct(pct);
            s.inputLastAppliedPct = pct;
            s.inputLastApplyMs = nowMs;
            s.inputIgnoreStateUntilMs = nowMs + 180;
          }
        }
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
    }
  }
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

  // PearCenter compact release: volume/input commit their rows, brightness
  // was already applied during motion; just clear the drag flags.
  {
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    std::string wid = dock_active_control_center_widget_id(app);
    if (wid.empty()) wid = "control_center";
    if (eh::shell::dock::control_center::pear_layout_enabled(sc, wid) &&
        (s.audioDragActive || s.inputDragActive || s.pearBriDragActive)) {
      const PearHitContext pctx{static_cast<double>(app.popupW > 0 ? app.popupW : 360), s, wid};
      if (s.audioDragActive) {
        double tx = 0.0, tw = 1.0;
        if (pear_volume_track_geom(pctx, &tx, &tw)) {
          const int pct = std::clamp(static_cast<int>(std::lround(
                                          std::clamp((app.pointerX - tx) / std::max(1.0, tw), 0.0, 1.0) *
                                          100.0)),
                                     0, 100);
          eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(
              static_cast<double>(pct) / 100.0);
          s.audioLastAppliedPct = pct;
          s.audioLastApplyMs = now_mono_ms();
          s.audioIgnoreStateUntilMs = s.audioLastApplyMs + 180;
        }
        s.audioDragActive = false;
        s.audioDragVisualT = -1.0;
        s.audioLastLoggedPct = -1;
        s.audioDragUiPct = -1;
        handled = true;
      }
      if (s.inputDragActive) {
        double tx = 0.0, tw = 1.0;
        if (pear_input_track_geom(pctx, &tx, &tw)) {
          const int pct = std::clamp(static_cast<int>(std::lround(
                                          std::clamp((app.pointerX - tx) / std::max(1.0, tw), 0.0, 1.0) *
                                          100.0)),
                                     0, 100);
          eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(
              static_cast<double>(pct) / 100.0);
          s.inputLastAppliedPct = pct;
          s.inputLastApplyMs = now_mono_ms();
          s.inputIgnoreStateUntilMs = s.inputLastApplyMs + 180;
        }
        s.inputDragActive = false;
        s.inputDragVisualT = -1.0;
        s.inputLastLoggedPct = -1;
        s.inputDragUiPct = -1;
        handled = true;
      }
      if (s.pearBriDragActive) {
        s.pearBriDragActive = false;
        s.pearBriDragT = -1.0;
        handled = true;
      }
      if (handled) return true;
    }
  }

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
  if (!app.ccState.wifiPasswordPrompt) return false;

  auto& s = app.ccState;

  if (sym == XKB_KEY_Escape) {
    s.wifiPasswordPrompt = false;
    s.wifiPassword.clear();
    s.wifiPendingSsid.clear();
    cc_resize_popup(app, 0, "wifi-key");
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
    cc_resize_popup(app, 0, "wifi-key");
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
