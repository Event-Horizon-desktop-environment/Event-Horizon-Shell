#include "desktop_shell/dashboard/dashboard_dispatch.hpp"

#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/dashboard/dashboard_hit.hpp"
#include "desktop_shell/dashboard/dashboard_layout.hpp"
#include "desktop_shell/dashboard/dashboard_surface.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace eh::shell::dashboard {
namespace {

namespace hooks = eh::shell::dock_slot_hooks;

// Input trace ([dash] lines on stderr), gated behind EH_DOCK_INPUT_TRACE
// like the shell-input trace. Motion is only logged near the trigger strip
// to avoid 60Hz spam while hovering cards.
bool dash_trace_enabled() {
  static const bool k = [] {
    const char* v = std::getenv("EH_DOCK_INPUT_TRACE");
    return v != nullptr && *v != '\0' && *v != '0';
  }();
  return k;
}

void dash_log(const char* what, const DockApp& app) {
  if (!dash_trace_enabled()) return;
  const auto& d = app.dash;
  std::cerr << "[dash] " << what << " xy=" << app.pointerX << "," << app.pointerY << " open=" << d.open
            << " revealT=" << d.revealT << "\n";
}

bool hits_equal(const DashboardHit& a, const DashboardHit& b) {
  return a.valid == b.valid && a.card == b.card && a.sub == b.sub && a.role == b.role && a.row == b.row &&
         a.streamId == b.streamId;
}

void apply_drag(DockApp& app, double t) {
  auto& d = app.dash;
  switch (d.dragKind) {
    case DashDragKind::Volume:
      hooks::control_center_set_audio_output_volume(t);
      break;
    case DashDragKind::InputVolume:
      hooks::control_center_set_audio_input_volume(t);
      break;
    case DashDragKind::Mixer:
      if (d.dragStreamId >= 0) hooks::control_center_set_mixer_stream_volume(d.dragStreamId, t);
      break;
    case DashDragKind::Seek:
      if (app.mpris && d.dragSeekDurationUs > 0) {
        const std::int64_t pos =
            static_cast<std::int64_t>(std::clamp(t, 0.0, 1.0) * static_cast<double>(d.dragSeekDurationUs));
        app.mpris->set_position(std::clamp<std::int64_t>(pos, 0, d.dragSeekDurationUs));
      }
      break;
    default:
      break;
  }
}

void drag_move(DockApp& app) {
  auto& d = app.dash;
  const double t = std::clamp((app.pointerX - d.dragTrackX) / std::max(1.0, d.dragTrackW), 0.0, 1.0);
  d.dragVisualT = t;
  const std::uint64_t now = eh::shell::now_mono_ms();
  if (d.dragLastApplyMs == 0 || now - d.dragLastApplyMs >= 16) {
    apply_drag(app, t);
    d.dragLastPct = static_cast<int>(std::lround(t * 100.0));
    d.dragLastApplyMs = now;
  }
  d.dirty = true;
  dashboard_draw(app);
}

void start_drag(DockApp& app, const DashboardHit& h) {
  auto& d = app.dash;
  d.dragging = true;
  d.dragKind = h.kind == DashCardKind::Volume      ? DashDragKind::Volume
               : h.kind == DashCardKind::Mic       ? DashDragKind::InputVolume
               : h.kind == DashCardKind::Media     ? DashDragKind::Seek
                                                   : DashDragKind::Mixer;
  d.dragStreamId = h.streamId;
  d.dragSeekDurationUs = 0;
  if (d.dragKind == DashDragKind::Seek && app.mpris) d.dragSeekDurationUs = app.mpris->snapshot().duration_us;
  d.dragTrackX = h.trackX;
  d.dragTrackW = std::max(1.0, h.trackW);
  d.dragVisualT = std::clamp((app.pointerX - d.dragTrackX) / d.dragTrackW, 0.0, 1.0);
  d.dragLastApplyMs = 0;
  d.dragLastPct = -1;
  drag_move(app);
}

void toggle_network(DockApp& app) {
  auto& d = app.dash;
  if (!d.netExpanded) (void)hooks::control_center_wifi_scan(true);
  d.netExpanded = !d.netExpanded;
  d.dirty = true;
  dashboard_changed(app);
}

void toggle_bluetooth(DockApp& app) {
  auto& d = app.dash;
  if (!d.btExpanded) {
    hooks::bluetooth_ensure_service();
    hooks::bluetooth_start_discovery();
  }
  d.btExpanded = !d.btExpanded;
  d.dirty = true;
  dashboard_changed(app);
}

void wifi_row_click(DockApp& app, int idx) {
  auto& d = app.dash;
  // Same sorted source the layout/paint/hit rows come from: the row index
  // must resolve to the visible network.
  const auto aps = dashboard_sorted_wifi_aps();
  if (idx < 0 || idx >= static_cast<int>(aps.size())) return;
  const auto& ap = aps[static_cast<size_t>(idx)];
  if (ap.active) return;
  std::string err;
  const bool ok = hooks::control_center_wifi_connect(ap.ssid, {}, &err);
  if (!ok && !err.empty()) {
    d.wifiError = err;
    d.wifiErrorUntilMs = eh::shell::now_mono_ms() + 4000;
  } else {
    d.wifiError.clear();
    d.wifiErrorUntilMs = 0;
  }
  d.dirty = true;
  dashboard_changed(app);
}

void bt_row_click(DockApp& app, int idx) {
  const auto devs = dashboard_sorted_bt_devices();
  if (idx < 0 || idx >= static_cast<int>(devs.size())) return;
  const auto& dv = devs[static_cast<size_t>(idx)];
  if (dv.connected) hooks::bluetooth_disconnect_device(dv.path);
  else if (dv.paired) hooks::bluetooth_connect_device(dv.path);
  else hooks::bluetooth_pair_device(dv.path);
  dashboard_changed(app);
}

}  // namespace

void dashboard_pointer_enter(DockApp& app) {
  auto& d = app.dash;
  if (!d.cfg.enabled || !d.panelSurface) return;
  d.hover = {};
  dash_log("enter", app);
  // Already open, or mid-close with the surface still alive: motions govern
  // from here; a fully-closed panel has no surface to enter on.
  if (d.open) return;
  dashboard_open(app);
}

void dashboard_pointer_leave(DockApp& app) {
  auto& d = app.dash;
  if (d.dragging) {
    d.pendingLeave = true;
    return;
  }
  // Leaving never closes: once revealed the panel stays open until the
  // trigger strip is entered again (toggle) or Esc.
  dash_log("leave", app);
  if (d.hover.valid) {
    d.hover = {};
    d.dirty = true;
    dashboard_draw(app);
  }
}

void dashboard_pointer_motion(DockApp& app) {
  auto& d = app.dash;
  if (!d.cfg.enabled || !d.panelSurface) return;

  if (d.pendingLeave) {
    const DashboardLayout& L = d.layout;
    const bool inside = L.valid && app.pointerX >= L.panelX && app.pointerX < L.panelX + L.panelW &&
                        app.pointerY >= L.panelY && app.pointerY < L.panelY + L.panelH;
    if (inside) d.pendingLeave = false;
  }

  if (d.dragging) {
    drag_move(app);
    return;
  }

  if (d.revealT < 0.99f) return;
  const DashboardHit h = dashboard_hit_at(app, app.pointerX, app.pointerY);
  if (app.pointerY < 48.0) dash_log("motion-near-strip", app);
  if (!hits_equal(h, d.hover)) {
    d.hover = h;
    d.dirty = true;
    dashboard_draw(app);
  }
}

void dashboard_pointer_press(DockApp& app, std::uint32_t serial) {
  (void)serial;
  auto& d = app.dash;
  dash_log("press", app);
  if (!d.cfg.enabled || !d.panelSurface || d.revealT < 0.99f) return;
  const DashboardHit h = dashboard_hit_at(app, app.pointerX, app.pointerY);
  if (!h.valid) return;
  d.press = h;

  switch (h.role) {
    case DashCardRole::Toggle:
      if (h.kind == DashCardKind::Network) toggle_network(app);
      else if (h.kind == DashCardKind::Bluetooth) toggle_bluetooth(app);
      break;

    case DashCardRole::Row:
      if (h.kind == DashCardKind::Network) wifi_row_click(app, h.row);
      else if (h.kind == DashCardKind::Bluetooth) bt_row_click(app, h.row);
      break;

    case DashCardRole::Mute:
      if (h.kind == DashCardKind::Volume) {
        const auto as = hooks::control_center_audio_output_state();
        hooks::control_center_set_audio_output_mute(!as.muted);
        dashboard_changed(app);
      } else if (h.kind == DashCardKind::Mic) {
        const auto as = hooks::control_center_audio_input_state();
        hooks::control_center_set_audio_input_mute(!as.muted);
        dashboard_changed(app);
      }
      break;

    case DashCardRole::Slider:
      start_drag(app, h);
      break;

    case DashCardRole::MediaPrev:
      if (app.mpris) app.mpris->previous();
      break;
    case DashCardRole::MediaPlayPause:
      if (app.mpris) app.mpris->play_pause();
      break;
    case DashCardRole::MediaNext:
      if (app.mpris) app.mpris->next();
      break;

    case DashCardRole::Body:
    default:
      if (h.kind == DashCardKind::Network) toggle_network(app);
      else if (h.kind == DashCardKind::Bluetooth) toggle_bluetooth(app);
      break;
  }
}

bool dashboard_button_release(DockApp& app) {
  auto& d = app.dash;
  bool handled = false;

  if (d.dragging) {
    const double t = std::clamp((app.pointerX - d.dragTrackX) / std::max(1.0, d.dragTrackW), 0.0, 1.0);
    apply_drag(app, t);
    d.dragLastPct = static_cast<int>(std::lround(t * 100.0));
    d.dragging = false;
    d.dragKind = DashDragKind::None;
    d.dragVisualT = -1.0;
    d.dragLastApplyMs = 0;
    d.dirty = true;
    dashboard_draw(app);
    handled = true;
  }

  if (d.pendingLeave) {
    // A leave mid-drag parks here; the panel still stays open (only the strip
    // gesture or Esc unreveals), so just clear the flag.
    d.pendingLeave = false;
  }
  return handled;
}

bool dashboard_axis(DockApp& app, double deltaPx) {
  auto& d = app.dash;
  if (!d.panelSurface || app.pointerSurface != d.panelSurface) return false;
  if (d.revealT < 0.99f) return false;

  const DashboardHit h = dashboard_hit_at(app, app.pointerX, app.pointerY);
  if (!h.valid) return false;

  const int dPct = static_cast<int>(std::lround(-(deltaPx / 20.0) * 5.0));
  if (h.kind == DashCardKind::Volume) {
    const auto as = hooks::control_center_audio_output_state();
    const int pct = std::clamp(as.volume_pct + dPct, 0, 100);
    hooks::control_center_set_audio_output_volume(static_cast<double>(pct) / 100.0);
  } else if (h.kind == DashCardKind::Mic) {
    const auto as = hooks::control_center_audio_input_state();
    const int pct = std::clamp(as.volume_pct + dPct, 0, 100);
    hooks::control_center_set_audio_input_volume(static_cast<double>(pct) / 100.0);
  } else if (h.kind == DashCardKind::Mixer && h.streamId >= 0) {
    if (dPct == 0) return true;
    const auto streams = hooks::control_center_mixer_streams();
    for (const auto& st : streams) {
      if (st.sink_input_id != h.streamId) continue;
      const int pct = std::clamp(st.volume_pct + dPct, 0, 100);
      hooks::control_center_set_mixer_stream_volume(st.sink_input_id, static_cast<double>(pct) / 100.0);
      break;
    }
  } else {
    return true;
  }
  d.dirty = true;
  dashboard_draw(app);
  return true;
}

void dashboard_trigger_enter(DockApp& app) {
  auto& d = app.dash;
  if (!d.cfg.enabled || !d.trigSurface) return;
  d.hover = {};
  dash_log("trigger", app);
  if (d.open) dashboard_close(app);
  else dashboard_open(app);
}

void dashboard_open(DockApp& app) {
  auto& d = app.dash;
  dash_log("open", app);
  if (!d.cfg.enabled) return;
  if (d.open) return;
  // The panel surface is created per open with a static full-surface region;
  // a mid-close reopen finds it still alive and just reverses the animation.
  dashboard_ensure_panel(app);
  if (!d.panelSurface) return;
  d.open = true;
  d.pendingLeave = false;
  if (d.animId) {
    app.shellAnim.cancel(d.animId);
    d.animId = 0;
  }

  const float from = d.revealT;
  d.animId = app.shellAnim.animate(
      from, 1.0f, 220.0f, eh::shell::Easing::EaseOutCubic,
      [&app](float v) {
        app.dash.revealT = v;
        app.dash.dirty = true;
      },
      [&app]() {
        app.dash.animId = 0;
        app.dash.revealT = 1.0f;
        app.dash.dirty = true;
        dashboard_draw(app);
      });
  d.dirty = true;
  dock_schedule_frame(app);
}

void dashboard_close(DockApp& app) {
  auto& d = app.dash;
  dash_log("close", app);
  if (!d.panelSurface) return;
  if (!d.open && d.revealT <= 0.f) return;
  d.open = false;
  d.pendingLeave = false;
  if (d.dragging) {
    d.dragging = false;
    d.dragKind = DashDragKind::None;
    d.dragVisualT = -1.0;
  }
  if (d.animId) {
    app.shellAnim.cancel(d.animId);
    d.animId = 0;
  }
  d.animId = app.shellAnim.animate(
      d.revealT, 0.0f, 180.0f, eh::shell::Easing::EaseInCubic,
      [&app](float v) {
        app.dash.revealT = v;
        app.dash.dirty = true;
      },
      [&app]() {
        app.dash.animId = 0;
        app.dash.revealT = 0.0f;
        app.dash.dirty = true;
        // Destroy, don't hide: the panel carries a static full-surface
        // region, so removing input means removing the surface. A mid-close
        // reopen flips `open` back and skips this.
        if (!app.dash.open) dashboard_destroy_panel(app);
      });
  d.dirty = true;
  dock_schedule_frame(app);
}

}  // namespace eh::shell::dashboard
