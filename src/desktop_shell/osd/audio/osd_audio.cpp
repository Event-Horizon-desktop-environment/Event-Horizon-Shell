#include "desktop_shell/osd/audio/osd_audio.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/osd/host/osd_host.hpp"
#include "services/audio/pipewire_service.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace eh::shell::osd {
namespace {

DockApp* g_dock = nullptr;
std::atomic<bool> g_audio_osd_dirty{false};

uint32_t g_sink_id = 0;
int g_sink_pct = -1;
bool g_sink_mute = false;
uint32_t g_src_id = 0;
int g_src_pct = -1;
bool g_src_mute = false;

const char* sink_glyph(int pct, bool mute) {
   
  if (mute || pct <= 0) return "volume_off";
  if (pct < 34) return "volume_down";
  return "volume_up";
}

const eh::audio::Device* find_node(const eh::audio::Snapshot& s, uint32_t id) {
   
  {
    auto it = std::find_if(s.sinks.begin(), s.sinks.end(),
                           [id](const auto& d) { return d.node_id == id; });
    if (it != s.sinks.end()) return &*it;
  }
  {
    auto it = std::find_if(s.sources.begin(), s.sources.end(),
                           [id](const auto& d) { return d.node_id == id; });
    if (it != s.sources.end()) return &*it;
  }
  return nullptr;
}

void prime_from_snapshot() {
   
  const eh::audio::Snapshot s = eh::audio::PipeWireService::instance().snapshot();
  if (!s.available) return;
  if (const eh::audio::Device* sink = find_node(s, s.default_sink)) {
    g_sink_id = sink->node_id;
    g_sink_pct = sink->volume_pct;
    g_sink_mute = sink->muted;
  }
  if (const eh::audio::Device* src = find_node(s, s.default_source)) {
    g_src_id = src->node_id;
    g_src_pct = src->volume_pct;
    g_src_mute = src->muted;
  }
}

void emit_sink_osd(int pct, bool mute) {
   
  if (!g_dock || !g_dock->osdHost) return;
  const int shown = std::clamp(pct, 0, 150);
  const float prog = std::min(1.f, static_cast<float>(shown) / 100.f);
  OsdContent c{};
  c.icon_ligature = sink_glyph(shown, mute);
  c.value_text = std::to_string(shown) + "%";
  c.progress = mute ? 0.f : prog;
  g_dock->osdHost->show(c);
}

void emit_source_osd(int pct, bool mute) {
   
  if (!g_dock || !g_dock->osdHost) return;
  const int shown = std::clamp(pct, 0, 150);
  const float prog = std::min(1.f, static_cast<float>(shown) / 100.f);
  OsdContent c{};
  c.icon_ligature = mute ? "mic_off" : "mic";
  c.value_text = std::to_string(shown) + "%";
  c.progress = mute ? 0.f : prog;
  g_dock->osdHost->show(c);
}

void on_pw_notification() {
    
  g_audio_osd_dirty.store(true, std::memory_order_release);
}

namespace {

struct BootAudioDefaults {
  bool enabled = false;
  int attempts = 0;
  eh::audio::PipeWireService::SavedDefaults d;
};

BootAudioDefaults g_boot{};
constexpr int kBootApplyMaxAttempts = 100;

bool device_present_by_name(const std::vector<eh::audio::Device>& devs, const std::string& name) {
    
  return std::any_of(devs.begin(), devs.end(),
                     [&](const auto& dev) { return dev.name == name; });
}

// PipeWire enumerates nodes asynchronously after start(); the saved default
// sink/source can only be applied once their nodes have shown up. Retry each
// poll tick (bounded) so the graph/engine + PCM fields apply on the first
// ready tick and the device defaults apply as soon as they exist.
void try_boot_audio_defaults() {
    
  if (!g_boot.enabled) return;
  auto& pw = eh::audio::PipeWireService::instance();
  if (!pw.started()) return;

  const auto& d = g_boot.d;
  const bool needSink = !d.default_sink_name.empty();
  const bool needSource = !d.default_source_name.empty();

  if (!needSink && !needSource) {
    pw.apply_saved_defaults(d);
    g_boot.enabled = false;
    return;
  }

  const eh::audio::Snapshot s = pw.snapshot();
  const bool hasSink = !needSink || device_present_by_name(s.sinks, d.default_sink_name);
  const bool hasSource = !needSource || device_present_by_name(s.sources, d.default_source_name);
  if ((hasSink && hasSource) || g_boot.attempts >= kBootApplyMaxAttempts) {
    pw.apply_saved_defaults(d);
    g_boot.enabled = false;
  } else {
    ++g_boot.attempts;
  }
}

}  // namespace

void apply_pending_audio_osd(DockApp& app) {
   
  if (!app.osdHost) return;
  const eh::audio::Snapshot s = eh::audio::PipeWireService::instance().snapshot();
  if (!s.available) return;

  const uint32_t sink_id = s.default_sink;
  const eh::audio::Device* sink = find_node(s, sink_id);
  const int sink_pct = sink ? sink->volume_pct : 0;
  const bool sink_mute = sink ? sink->muted : false;

  const uint32_t src_id = s.default_source;
  const eh::audio::Device* src = find_node(s, src_id);
  const int src_pct = src ? src->volume_pct : 0;
  const bool src_mute = src ? src->muted : false;

  if (sink && (sink_id != g_sink_id || sink_pct != g_sink_pct || sink_mute != g_sink_mute)) {
    emit_sink_osd(sink_pct, sink_mute);
  } else if (src && (src_id != g_src_id || src_pct != g_src_pct || src_mute != g_src_mute)) {
    emit_source_osd(src_pct, src_mute);
  }

  g_sink_id = sink_id;
  g_sink_pct = sink_pct;
  g_sink_mute = sink_mute;
  g_src_id = src_id;
  g_src_pct = src_pct;
  g_src_mute = src_mute;
}

}

void osd_audio_bind(DockApp& app) {
   
  g_dock = &app;
  // PipeWire connects at boot so previously saved audio defaults (default
  // output device, engine/graph rates, PCM format) are reapplied after a DE
  // restart. The OSD shows an overlay whenever PipeWire notifies a change;
  // before that it stays dormant, external volume changes (e.g. wpctl
  // keybinds) still apply but show no overlay.
  auto& pw = eh::audio::PipeWireService::instance();
  if (pw.started()) prime_from_snapshot();
  pw.set_change_callback([]() { on_pw_notification(); });
}

void osd_audio_apply_saved_defaults(DockApp& app) {
   
  (void)app;
  eh::audio::PipeWireService::instance().start();

  const auto& au = eh::config::shell_config_snapshot_skip_matugen().audio;
  g_boot.d.default_sink_name = au.default_sink_name;
  g_boot.d.default_source_name = au.default_source_name;
  g_boot.d.default_sink_volume_pct = au.default_sink_volume_pct;
  g_boot.d.default_sink_muted = au.default_sink_muted;
  g_boot.d.default_source_volume_pct = au.default_source_volume_pct;
  g_boot.d.default_source_muted = au.default_source_muted;
  g_boot.d.engine_clock_rate_hz = au.engine_clock_rate_hz;
  g_boot.d.engine_force_rate_hz = au.engine_force_rate_hz;
  g_boot.d.engine_allowed_rates_hz = au.engine_allowed_rates_hz;
  g_boot.d.compat_pcm_format = au.compat_pcm_format;
  g_boot.attempts = 0;
  g_boot.enabled = true;
  try_boot_audio_defaults();
}

void osd_audio_poll_pending(DockApp& app) {
   
  try_boot_audio_defaults();
  if (!g_audio_osd_dirty.exchange(false, std::memory_order_acq_rel)) return;
  apply_pending_audio_osd(app);
}

void osd_audio_shutdown() noexcept {
   
  eh::audio::PipeWireService::instance().set_change_callback({});
  g_audio_osd_dirty.store(false, std::memory_order_relaxed);
  g_boot.enabled = false;
  g_dock = nullptr;
}

}
