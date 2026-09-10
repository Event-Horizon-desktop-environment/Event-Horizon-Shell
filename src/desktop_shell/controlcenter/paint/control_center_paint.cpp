#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"

#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/audio/pipewire_service.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <cairo.h>

namespace eh::shell::dock::control_center {

namespace slot_pill_style = eh::widgets::slot_pill_style;
namespace pu = paint_utils;

using eh::widgets::widget_setting;

constexpr double kPrimR = 0.90, kPrimG = 0.90, kPrimB = 0.90;

struct CcWidgetCfg {
  bool show_net = true;
  bool show_audio = true;
  bool show_mic = true;
  bool show_bt = true;
};

struct CcRuntime {
  bool net_up = false;
  bool net_wifi = false;
  bool bt_connected = false;
  bool bt_powered = false;
  bool sink_muted = false;
  int sink_volume_pct = 100;
  bool mic_running = false;
  bool mic_muted = false;
  timespec last_poll{};
};

CcWidgetCfg load_cfg(const eh::config::ShellConfig& sc, std::string_view instance_id) {
  CcWidgetCfg c{};
  c.show_net = pu::parse_bool(widget_setting(sc, instance_id, "show_network"), true);
  c.show_audio = pu::parse_bool(widget_setting(sc, instance_id, "show_audio"), true);
  c.show_mic = pu::parse_bool(widget_setting(sc, instance_id, "show_mic"), true);
  c.show_bt = pu::parse_bool(widget_setting(sc, instance_id, "show_bluetooth"), true);
  return c;
}

void poll_runtime(CcRuntime& rt) {
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  const eh::net::Snapshot s = nm.snapshot();
  rt.net_up = s.connected;
  rt.net_wifi = (s.kind == eh::net::ConnectivityKind::Wireless) && s.connected;

  const auto bs = eh::shell::dock_slot_hooks::bluetooth_snapshot();
  rt.bt_powered = bs.powered;
  rt.bt_connected = bs.connected;

  // PipeWire is loaded lazily: the bar pill shows neutral defaults until an
  // audio surface (control-center panel, mixer, OSD, settings) calls
  // PipeWireService::start(). Keeps ~10MB of pipewire mappings out of idle
  // dock RSS when audio UI is never opened.
  if (eh::audio::PipeWireService::instance().started()) {
    const eh::audio::Snapshot ss = eh::audio::PipeWireService::instance().snapshot();

    rt.sink_muted = false;
    for (const auto& d : ss.sinks) {
      if (d.is_default) {
        rt.sink_muted = d.muted;
        rt.sink_volume_pct = d.volume_pct;
        break;
      }
    }

    rt.mic_muted = true;
    for (const auto& d : ss.sources) {
      if (d.is_default) {
        rt.mic_muted = d.muted;
        break;
      }
    }
    rt.mic_running = !ss.input_streams.empty();
  }
}

const CcRuntime& runtime_snapshot() {
  static CcRuntime rt{};
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (rt.last_poll.tv_sec != 0 || rt.last_poll.tv_nsec != 0) {
    const int64_t dt_ms =
        (now.tv_sec - rt.last_poll.tv_sec) * 1000 + (now.tv_nsec - rt.last_poll.tv_nsec) / 1000000;
    if (dt_ms >= 0 && dt_ms < 260) return rt;
  }
  rt.last_poll = now;
  poll_runtime(rt);
  return rt;
}

int icon_count(const CcWidgetCfg& c, const CcRuntime& rt) {
  int n = 0;
  if (c.show_net) n++;
  if (c.show_audio) n++;
  if (c.show_mic && rt.mic_running) n++;
  if (c.show_bt) n++;
  return std::max(1, n);
}

bool widget_list_contains_control_center(const eh::config::ShellConfig&, const std::vector<std::string>& widgets) {
  for (const auto& id : widgets)
    if (eh::config::widget_implementation_type(id) == "control_center") return true;
  return false;
}

double dock_control_center_slot_width(const eh::config::ShellConfig& sc, std::string_view instance_id, double icon_ref_px,
                                      double bar_height) {
  const CcWidgetCfg cfg = load_cfg(sc, instance_id);
  const CcRuntime& rt = runtime_snapshot();
  const double s = icon_ref_px / 30.0;
  const double hpad = std::max(6.0, 8.0 * s);
  const double gap = std::max(5.0, 6.0 * s);
  const double glyph = std::clamp(icon_ref_px * 0.45, 12.0, 24.0);
  const int n = icon_count(cfg, rt);
  const double w = hpad * 2.0 + static_cast<double>(n) * glyph + static_cast<double>(std::max(0, n - 1)) * gap;
  return std::clamp(w, 48.0, std::min(bar_height * 6.5, 240.0));
}

bool paint_control_center_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                               double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed) {
  if (slot_w <= 1.0) return false;
  (void)hovered;
  (void)pressed;
  const CcWidgetCfg cfg = load_cfg(sc, instance_id);
  const CcRuntime& rt = runtime_snapshot();

  const double pill_h = slot_h * slot_pill_style::kPillHeightMul;
  const double pill_y = y + (slot_h - pill_h) * 0.5;
  slot_pill_style::paint_pill(cr, x, pill_y, slot_w, pill_h);

  const double s = icon_ref_px / 30.0;
  const double hpad = std::max(6.0, 8.0 * s);
  const double gap = std::max(5.0, 6.0 * s);
  const double glyph = std::clamp(icon_ref_px * 0.45, 12.0, 24.0);
  const double cy = pill_y + pill_h * 0.5;

  std::vector<const char*> ligatures;
  if (cfg.show_net) ligatures.push_back(rt.net_up ? (rt.net_wifi ? "wifi" : "lan") : "signal_wifi_off");
  if (cfg.show_audio) {
    if (rt.sink_muted || rt.sink_volume_pct <= 0) ligatures.push_back("volume_off");
    else if (rt.sink_volume_pct < 33) ligatures.push_back("volume_down");
    else ligatures.push_back("volume_up");
  }
  if (cfg.show_mic && rt.mic_running) ligatures.push_back(rt.mic_muted ? "mic_off" : "mic");
  if (cfg.show_bt) ligatures.push_back(rt.bt_powered ? "bluetooth" : "bluetooth_disabled");
  if (ligatures.empty()) ligatures.push_back("settings");

  const double run_w = static_cast<double>(ligatures.size()) * glyph + static_cast<double>(ligatures.size() - 1) * gap;
  double cx = x + hpad + glyph * 0.5 + std::max(0.0, (slot_w - 2.0 * hpad - run_w) * 0.5);
  for (size_t i = 0; i < ligatures.size(); ++i) {
    const std::string_view glyph_id = ligatures[i];
    const bool accent = (glyph_id == "lan" || glyph_id == "bluetooth");
    const bool warn = (glyph_id == "signal_wifi_off" || glyph_id == "bluetooth_disabled");
    const bool hot_mic = (glyph_id == "mic");
    const double rr = hot_mic ? 0.92 : (warn ? 0.80 : (accent ? kPrimR : pu::kSurfR));
    const double gg = hot_mic ? 0.22 : (warn ? 0.28 : (accent ? kPrimG : pu::kSurfG));
    const double bb = hot_mic ? 0.24 : (warn ? 0.30 : (accent ? kPrimB : pu::kSurfB));
    eh::shell::draw_material_glyph(cr, cx, cy, glyph, ligatures[i], rr, gg, bb, 1.0);
    cx += glyph + gap;
  }
  return false;
}

bool control_center_tick_signature_changed(int& io_signature, const eh::config::ShellConfig& sc, std::string_view instance_id) {
  const CcWidgetCfg cfg = load_cfg(sc, instance_id);
  const CcRuntime& rt = runtime_snapshot();
  int sig = 0;
  sig = sig * 131 + (cfg.show_net ? 1 : 0);
  sig = sig * 131 + (cfg.show_audio ? 1 : 0);
  sig = sig * 131 + (cfg.show_mic ? 1 : 0);
  sig = sig * 131 + (cfg.show_bt ? 1 : 0);
  sig = sig * 131 + (rt.net_up ? 1 : 0);
  sig = sig * 131 + (rt.net_wifi ? 1 : 0);
  sig = sig * 131 + (rt.bt_connected ? 1 : 0);
  sig = sig * 131 + (rt.bt_powered ? 1 : 0);
  sig = sig * 131 + (rt.sink_muted ? 1 : 0);
  sig = sig * 131 + std::clamp(rt.sink_volume_pct, 0, 200);
  sig = sig * 131 + (rt.mic_running ? 1 : 0);
  sig = sig * 131 + (rt.mic_muted ? 1 : 0);
  if (sig != io_signature) {
    io_signature = sig;
    return true;
  }
  return false;
}

} // namespace eh::shell::dock::control_center
