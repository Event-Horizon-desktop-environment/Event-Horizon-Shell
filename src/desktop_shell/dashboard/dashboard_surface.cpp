#include "desktop_shell/dashboard/dashboard_surface.hpp"

#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/dashboard/dashboard_layout.hpp"
#include "desktop_shell/dashboard/dashboard_paint.hpp"
#include "desktop_shell/dashboard/dashboard_dispatch.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/dock/core/dock_boot_log.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "configuration/shell_config.hpp"

#include <wayland-client.h>

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace eh::shell::dashboard {
namespace {

namespace hooks = eh::shell::dock_slot_hooks;

bool cfg_equal(const eh::config::DashboardConfig& a, const eh::config::DashboardConfig& b) {
  if (a.enabled != b.enabled || a.triggerHeight != b.triggerHeight || a.columns != b.columns || a.gap != b.gap ||
      a.marginX != b.marginX || a.marginTop != b.marginTop || a.marginBottom != b.marginBottom ||
      a.maxWidth != b.maxWidth || a.cards.size() != b.cards.size())
    return false;
  for (size_t i = 0; i < a.cards.size(); ++i)
    if (a.cards[i].id != b.cards[i].id || a.cards[i].span != b.cards[i].span) return false;
  return true;
}

wl_output* pick_output(DockApp& app) {
  if (app.dockLayerOutput) return app.dockLayerOutput;
  if (!app.dockLayers.empty() && app.dockLayers[0] && app.dockLayers[0]->wlOut) return app.dockLayers[0]->wlOut;
  for (auto& slot : app.outputSlots)
    if (slot && slot->ready && slot->output) return slot->output;
  for (auto& slot : app.outputSlots)
    if (slot && slot->output) return slot->output;
  return nullptr;
}

double output_width_px(const DockApp& app) {
  if (app.dash.panelW > 0) return app.dash.panelW;
  return app.primaryOutputWidthPx > 0 ? app.primaryOutputWidthPx : 1920.0;
}

int trig_height_px(DockApp& app) { return std::clamp(app.dash.cfg.triggerHeight, 4, 48); }

// Static input regions only: set at creation (and re-set on configure,
// still pre-map), never updated afterwards — post-map region updates are not
// reliably applied, so open/close must not depend on them.
void set_full_region(DockApp& app, wl_surface* surface, int w, int h) {
  if (!surface || !app.compositor || w <= 0 || h <= 0) return;
  wl_region* r = wl_compositor_create_region(app.compositor);
  if (!r) return;
  wl_region_add(r, 0, 0, w, h);
  wl_surface_set_input_region(surface, r);
  wl_region_destroy(r);
}

// The trigger strip is input-only: one static transparent buffer so the
// surface maps, never repainted afterwards.
void paint_trigger(DockApp& app) {
  auto& d = app.dash;
  if (!d.trigSurface || !app.shm) return;
  const int w = d.trigW > 0 ? d.trigW : static_cast<int>(std::ceil(output_width_px(app)));
  const int h = trig_height_px(app);
  if (!d.trigShm.ensure(app.shm, "eh-dashboard-trig", w, h)) return;
  if (cairo_t* cr = d.trigShm.cairo()) {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  }
  wl_surface_attach(d.trigSurface, d.trigShm.wl(), 0, 0);
  wl_surface_damage_buffer(d.trigSurface, 0, 0, w, h);
  wl_surface_commit(d.trigSurface);
  if (app.display) wl_display_flush(app.display);
}

// Never request a zero-height surface: configure would loop with nothing to
// anchor the input strip to.
int desired_height_px(DockApp& app) {
  const int floor = static_cast<int>(
      std::ceil(std::clamp(static_cast<double>(app.dash.cfg.triggerHeight), 4.0, 48.0))) + 24;
  const int want = static_cast<int>(std::ceil(dashboard_desired_height(app, output_width_px(app))));
  return want > 0 ? want : floor;
}

void on_buffer_release(void* user) {
  auto* app = static_cast<DockApp*>(user);
  if (!app) return;
  auto& d = app->dash;
  // Guard: without this the release -> redraw -> attach -> release loop spins
  // at refresh rate even when nothing changed.
  if (!d.needsDraw) return;
  d.needsDraw = false;
  dock_schedule_frame(*app);
}

void on_configure(void* data, zwlr_layer_surface_v1* ls, std::uint32_t serial, std::uint32_t w, std::uint32_t h) {
  auto& app = *static_cast<DockApp*>(data);
  auto& d = app.dash;
  const bool isTrig = d.trigLayer && d.trigLayer == ls;
  const bool isPanel = d.panelLayer && d.panelLayer == ls;
  if (!isTrig && !isPanel) return;
  zwlr_layer_surface_v1_ack_configure(ls, serial);

  if (isTrig) {
    if (!d.trigSurface) return;
    d.trigW = static_cast<int>(w);
    // Static region + static buffer, still pre-map.
    set_full_region(app, d.trigSurface, d.trigW > 0 ? d.trigW : static_cast<int>(std::ceil(output_width_px(app))),
                    trig_height_px(app));
    paint_trigger(app);
    return;
  }

  if (!d.panelSurface) return;
  d.panelW = static_cast<int>(w);
  d.panelH = static_cast<int>(h);
  d.panelConfigured = d.panelW > 0 && d.panelH > 0;
  eh::shell::dock::dock_boot_step("dashboard: configured %ux%u (accepted by compositor)", w, h);

  // Static full-surface region, still pre-map alongside creation.
  if (d.panelW > 0 && d.panelH > 0) set_full_region(app, d.panelSurface, d.panelW, d.panelH);

  // One set_size per configure: request the layout height, then paint on the
  // configure that reports it back (avoids attaching a mismatched buffer).
  if (d.panelW > 0) {
    const int want = desired_height_px(app);
    if (want != d.panelLastRequestedH) {
      d.panelLastRequestedH = want;
      zwlr_layer_surface_v1_set_size(d.panelLayer, 0, static_cast<std::uint32_t>(want));
      wl_surface_commit(d.panelSurface);
      return;
    }
  }

  d.dirty = true;
  dashboard_draw(app);
}

void on_closed(void* data, zwlr_layer_surface_v1* ls) {
  auto& app = *static_cast<DockApp*>(data);
  auto& d = app.dash;
  if (d.trigLayer && d.trigLayer == ls) {
    dashboard_destroy_trigger(app);
    if (d.cfgValid && d.cfg.enabled) dashboard_ensure_trigger(app);
    return;
  }
  if (d.panelLayer && d.panelLayer == ls) {
    dashboard_destroy_panel(app);
    // A compositor-side close is a close: stay shut until the next trigger enter.
    if (d.animId) {
      app.shellAnim.cancel(d.animId);
      d.animId = 0;
    }
    d.open = false;
    d.revealT = 0.f;
    d.hover = {};
  }
}

const zwlr_layer_surface_v1_listener kLayerListener = {
    .configure = on_configure,
    .closed = on_closed,
};

std::string media_signature(DockApp& app) {
  if (!app.mpris) return {};
  const auto ms = app.mpris->snapshot();
  if (!ms.active) return "idle";
  return ms.title + "|" + ms.artist + "|" + ms.playback_status + "|" + (ms.art ? "1" : "0") + "|" +
         ms.art_url_resolved + "|" + std::to_string(ms.position_us / 1000000) + "|" +
         std::to_string(ms.duration_us);
}

std::string audio_signature() {
  const auto out = hooks::control_center_audio_output_state();
  const auto in = hooks::control_center_audio_input_state();
  return std::to_string(out.volume_pct) + "|" + (out.muted ? "1" : "0") + "|" + out.device_name + "|" +
         std::to_string(in.volume_pct) + "|" + (in.muted ? "1" : "0") + "|" + in.device_name;
}

std::string net_signature() {
  const auto ns = hooks::control_center_network_state();
  const auto aps = dashboard_sorted_wifi_aps();
  std::string s = ns.status_text + "|" + ns.ssid + "|" + std::to_string(aps.size());
  for (const auto& ap : aps) s += "|" + ap.ssid + ":" + std::to_string(ap.signal_pct) + (ap.active ? "*" : "");
  return s;
}

std::string bt_signature() {
  const auto bs = hooks::control_center_bluetooth_state();
  std::string s = bs.status_text + "|" + std::to_string(bs.battery_pct);
  const auto devs = dashboard_sorted_bt_devices();
  for (const auto& d : devs)
    s += "|" + d.path + (d.connected ? "+" : "") + (d.paired ? "p" : "");
  return s;
}

std::string mixer_signature() {
  const auto streams = hooks::control_center_mixer_streams();
  std::string s;
  for (const auto& st : streams)
    s += "|" + std::to_string(st.sink_input_id) + ":" + std::to_string(st.volume_pct) + (st.muted ? "m" : "");
  return s;
}

bool refresh_size_impl(DockApp& app) {
  auto& d = app.dash;
  if (!d.panelLayer || !d.panelSurface || !d.panelConfigured || d.panelW <= 0) return false;
  const int want = desired_height_px(app);
  if (want == d.panelLastRequestedH) return false;
  d.panelLastRequestedH = want;
  zwlr_layer_surface_v1_set_size(d.panelLayer, 0, static_cast<std::uint32_t>(want));
  wl_surface_commit(d.panelSurface);
  return true;
}

}  // namespace

bool dashboard_refresh_size(DockApp& app) { return refresh_size_impl(app); }

void dashboard_changed(DockApp& app) {
  app.dash.dirty = true;
  if (refresh_size_impl(app)) return;  // configure will repaint at the new height
  dashboard_draw(app);
}

void dashboard_ensure_trigger(DockApp& app) {
  auto& d = app.dash;
  if (d.trigSurface) return;
  if (!d.cfgValid || !d.cfg.enabled) return;
  if (!app.compositor || !app.layerShell || !app.shm) return;
  wl_output* out = pick_output(app);
  if (!out) return;

  eh::wayland::LayerSurfaceConfig cfg;
  cfg.nameSpace = eh::shell::kDashboardNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width = 0;
  cfg.height = static_cast<std::uint32_t>(trig_height_px(app));
  cfg.exclusiveZone = -1;
  cfg.marginTop = cfg.marginRight = cfg.marginBottom = cfg.marginLeft = 0;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  eh::shell::dock::dock_boot_step("dashboard: trigger strip height=%d", static_cast<int>(cfg.height));
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, out, cfg, &kLayerListener, &app,
                                         &d.trigSurface, &d.trigLayer)) {
    eh::shell::dock::dock_boot_step("dashboard: trigger create FAILED (no surface)");
    d.trigSurface = nullptr;
    d.trigLayer = nullptr;
    return;
  }
  d.trigW = 0;
  // Static region from birth, bare commit only: layer-shell forbids attaching
  // a buffer before the first configure (the compositor kills the surface).
  // The transparent mapping buffer is attached in on_configure instead.
  set_full_region(app, d.trigSurface, static_cast<int>(std::ceil(output_width_px(app))), trig_height_px(app));
  wl_surface_commit(d.trigSurface);
  if (app.display) wl_display_flush(app.display);
}

void dashboard_ensure_panel(DockApp& app) {
  auto& d = app.dash;
  if (d.panelSurface) return;
  if (!d.cfgValid || !d.cfg.enabled) return;
  if (!app.compositor || !app.layerShell || !app.shm) return;
  wl_output* out = pick_output(app);
  if (!out) return;

  eh::wayland::LayerSurfaceConfig cfg;
  cfg.nameSpace = eh::shell::kDashboardNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width = 0;
  // Never commit a zero-height surface: Hyprland rejects layer surfaces whose
  // height is 0 unless the anchor has both top AND bottom (the anchor here is
  // top-only), poisoning the whole connection. Start at the desired layout
  // height; the first configure event then negotiates the real size.
  const int wantH = desired_height_px(app);
  cfg.height = static_cast<std::uint32_t>(wantH);
  cfg.exclusiveZone = -1;
  cfg.marginTop = cfg.marginRight = cfg.marginBottom = cfg.marginLeft = 0;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;

  eh::shell::dock::dock_boot_step("dashboard: create panel surface height=%d", wantH);
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, out, cfg, &kLayerListener, &app,
                                         &d.panelSurface, &d.panelLayer)) {
    eh::shell::dock::dock_boot_step("dashboard: panel create FAILED (no surface)");
    d.panelSurface = nullptr;
    d.panelLayer = nullptr;
    return;
  }
  eh::shell::dock::dock_boot_surface("dashboard", cfg.nameSpace, cfg.anchor, cfg.width, cfg.height,
                                     cfg.exclusiveZone, cfg.marginTop, cfg.marginRight, cfg.marginBottom,
                                     cfg.marginLeft, "primary");

  d.panelW = d.panelH = 0;
  d.panelConfigured = false;
  d.panelLastRequestedH = wantH;
  d.panelEnsureFailures = 0;
  d.needsDraw = false;
  d.dirty = true;
  d.layout.valid = false;
  // Static full-surface region from birth; the configure handler re-sets it
  // with the exact size (still pre-map). Never updated after that.
  set_full_region(app, d.panelSurface, static_cast<int>(std::ceil(output_width_px(app))), wantH);
  wl_surface_commit(d.panelSurface);
}

void dashboard_destroy_trigger(DockApp& app) {
  auto& d = app.dash;
  d.trigShm.set_release_hook(nullptr, nullptr);
  d.trigShm.destroy();
  if (d.trigLayer) {
    zwlr_layer_surface_v1_destroy(d.trigLayer);
    d.trigLayer = nullptr;
  }
  if (d.trigSurface) {
    wl_surface_destroy(d.trigSurface);
    d.trigSurface = nullptr;
  }
  d.trigW = 0;
}

void dashboard_destroy_panel(DockApp& app) {
  auto& d = app.dash;
  d.panelShm.set_release_hook(nullptr, nullptr);
  d.panelShm.destroy();
  if (d.panelLayer) {
    zwlr_layer_surface_v1_destroy(d.panelLayer);
    d.panelLayer = nullptr;
  }
  if (d.panelSurface) {
    wl_surface_destroy(d.panelSurface);
    d.panelSurface = nullptr;
  }
  d.panelW = d.panelH = 0;
  d.panelConfigured = false;
  d.panelLastRequestedH = 0;
  d.dirty = false;
  d.needsDraw = false;
  d.layout.valid = false;
}

void dashboard_shutdown(DockApp& app) {
  auto& d = app.dash;
  if (d.animId) {
    app.shellAnim.cancel(d.animId);
    d.animId = 0;
  }
  d.open = false;
  d.revealT = 0.f;
  d.dragging = false;
  d.pendingLeave = false;
  d.kbdFocus = false;
  d.hover = {};
  // A broken display must not receive any Wayland destroy call.
  const bool broken = app.display != nullptr && wl_display_get_error(app.display) != 0;
  if (broken) {
    d.trigShm.set_release_hook(nullptr, nullptr);
    d.panelShm.set_release_hook(nullptr, nullptr);
    d.trigLayer = nullptr;
    d.trigSurface = nullptr;
    d.panelLayer = nullptr;
    d.panelSurface = nullptr;
    return;
  }
  dashboard_destroy_trigger(app);
  dashboard_destroy_panel(app);
}

void dashboard_draw(DockApp& app) {
  auto& d = app.dash;
  if (!d.panelSurface || !d.panelConfigured || d.panelW <= 0 || d.panelH <= 0) return;

  if (d.panelShm.busy()) {
    d.dirty = true;
    d.needsDraw = true;
    return;
  }
  if (!d.panelShm.ensure(app.shm, "eh-dashboard-shm", d.panelW, d.panelH)) {
    d.dirty = true;
    if (d.panelEnsureFailures < 8) {
      ++d.panelEnsureFailures;
      d.needsDraw = true;
    } else {
      d.panelEnsureFailures = 0;
    }
    return;
  }
  d.panelEnsureFailures = 0;
  d.panelShm.set_release_hook(on_buffer_release, &app);

  cairo_t* cr = d.panelShm.cairo();
  if (!cr) return;
  dashboard_paint(app, cr, static_cast<double>(d.panelW), static_cast<double>(d.panelH));

  d.dirty = false;
  d.needsDraw = false;
  d.panelShm.mark_busy();
  wl_surface_attach(d.panelSurface, d.panelShm.wl(), 0, 0);
  wl_surface_damage_buffer(d.panelSurface, 0, 0, d.panelW, d.panelH);
  wl_surface_commit(d.panelSurface);
  if (app.display) wl_display_flush(app.display);
}

void dashboard_frame_draw(DockApp& app) {
  if (app.dash.dirty) dashboard_draw(app);
}

void dashboard_weather_redraw(DockApp& app) {
  auto& d = app.dash;
  if (!d.panelSurface || !d.cfg.enabled) return;
  if (!d.open && d.revealT <= 0.f) return;
  d.dirty = true;
  if (refresh_size_impl(app)) return;
  dashboard_draw(app);
}

void dashboard_timer_tick(DockApp& app) {
  auto& d = app.dash;
  if (!d.cfgValid || !d.cfg.enabled) return;
  dashboard_ensure_trigger(app);
  if (!d.panelSurface || (!d.open && d.revealT <= 0.f)) return;

  bool changed = false;
  const std::uint64_t now = eh::shell::now_mono_ms();
  if (d.wifiErrorUntilMs != 0 && now >= d.wifiErrorUntilMs) {
    d.wifiError.clear();
    d.wifiErrorUntilMs = 0;
    changed = true;
  }

  const auto& sc = eh::config::shell_config_snapshot();

  if (dashboard_has_card(app, "clock")) {
    const auto t = dashboard_clock_text(sc);
    std::string sig = t.time + "|" + t.date + "|" + t.zone;
    if (sig != d.clockSig) {
      d.clockSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "weather")) {
    const auto ws = hooks::control_center_weather_state(sc, dashboard_weather_instance_id(app));
    std::string sig = ws.available ? (ws.location + "|" + std::to_string(ws.temp) + "|" + ws.condition + "|" +
                                      std::to_string(ws.forecast.size()))
                                   : ws.status_text;
    if (sig != d.weatherSig) {
      d.weatherSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "media")) {
    std::string sig = media_signature(app);
    if (sig != d.mediaSig) {
      d.mediaSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "volume") || dashboard_has_card(app, "mic")) {
    std::string sig = audio_signature();
    if (sig != d.audioSig) {
      d.audioSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "network")) {
    std::string sig = net_signature();
    if (sig != d.netSig) {
      d.netSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "bluetooth")) {
    std::string sig = bt_signature();
    if (sig != d.btSig) {
      d.btSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "mixer")) {
    std::string sig = mixer_signature();
    if (sig != d.mixerSig) {
      d.mixerSig = std::move(sig);
      changed = true;
    }
  }
  if (dashboard_has_card(app, "system")) {
    dashboard_sample_system(app);
    char b[128];
    std::snprintf(b, sizeof(b), "%.1f|%.0f|%.1f", d.sysCpuPct, d.sysMemMb, d.sysTempC);
    std::string sig = b;
    if (sig != d.sysSig) {
      d.sysSig = std::move(sig);
      changed = true;
    }
  }

  if (changed) dashboard_changed(app);
}

void dashboard_on_config_changed(DockApp& app, const eh::config::DashboardConfig& next) {
  auto& d = app.dash;
  const bool first = !d.cfgValid;
  const bool changed = first || cfg_equal(d.cfg, next) == false;
  const bool trigHChanged = first || d.cfg.triggerHeight != next.triggerHeight;

  d.cfg = next;
  d.cfgValid = true;
  if (!changed) return;

  if (!next.enabled) {
    if (d.animId) {
      app.shellAnim.cancel(d.animId);
      d.animId = 0;
    }
    d.open = false;
    d.revealT = 0.f;
    d.hover = {};
    dashboard_destroy_panel(app);
    dashboard_destroy_trigger(app);
    return;
  }

  // The trigger height is baked into the static strip surface: recreate it
  // when it changes (rare; creation is the reliable path).
  if (d.trigSurface && trigHChanged) dashboard_destroy_trigger(app);
  dashboard_ensure_trigger(app);

  d.layout.valid = false;
  if (d.panelSurface) {
    d.dirty = true;
    if (!refresh_size_impl(app)) dashboard_draw(app);
  }
}

}  // namespace eh::shell::dashboard
