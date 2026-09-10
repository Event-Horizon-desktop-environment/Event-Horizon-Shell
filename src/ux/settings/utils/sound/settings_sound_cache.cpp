#include <cstdint>
#include <string>
#include <vector>

#include "ux/settings/utils/sound/settings_sound_cache.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "services/audio/pipewire_service.hpp"

std::vector<eh::audio::BluetoothCardProfiles> g_settings_sound_bt_cards_cache;
std::uint64_t g_settings_sound_bt_cards_cache_mono_ms = 0;
bool g_settings_sound_bt_cards_cache_valid = false;
int g_settings_sound_bt_cards_cache_prev_tab = -1;

void settings_sound_bt_invalidate_cache() noexcept {
   
  g_settings_sound_bt_cards_cache_valid = false;
  g_settings_sound_bt_cards_cache_mono_ms = 0;
}

struct SettingsSoundEnginePaintCache {
  eh::audio::EngineSettings es{};
  eh::audio::CompatDefaultSinkFormat cf{};
  std::uint64_t mono_ms = 0;
  bool valid = false;
};
SettingsSoundEnginePaintCache g_settings_sound_engine_paint_cache;

void settings_sound_engine_paint_cache_invalidate() noexcept {
  g_settings_sound_engine_paint_cache.valid = false;
  g_settings_sound_engine_paint_cache.mono_ms = 0;
}

void settings_sound_engine_paint_cache_read(const App& app, eh::audio::EngineSettings* es_out,
                                            eh::audio::CompatDefaultSinkFormat* cf_out) {
  const std::uint64_t now = eh::shell::now_mono_ms();
  constexpr std::uint64_t kPeriodMs = 1200;
  const bool vol_drag = app.soundVolDragCode >= 0;
  if (g_settings_sound_engine_paint_cache.valid) {
    if (vol_drag || (now - g_settings_sound_engine_paint_cache.mono_ms) < kPeriodMs) {
      *es_out = g_settings_sound_engine_paint_cache.es;
      *cf_out = g_settings_sound_engine_paint_cache.cf;
      return;
    }
  }
  auto& pw = eh::audio::PipeWireService::instance();
  g_settings_sound_engine_paint_cache.es = pw.query_engine_settings();
  g_settings_sound_engine_paint_cache.cf = pw.query_compat_default_sink_format();
  g_settings_sound_engine_paint_cache.mono_ms = now;
  g_settings_sound_engine_paint_cache.valid = true;
  *es_out = g_settings_sound_engine_paint_cache.es;
  *cf_out = g_settings_sound_engine_paint_cache.cf;
}

const std::vector<eh::audio::BluetoothCardProfiles>& settings_sound_bt_cards_cached(App& app) {
  constexpr std::uint64_t kRefreshPeriodMs = 2500;
  const std::uint64_t now = eh::shell::now_mono_ms();

  if (app.activeTab != 8) {
    g_settings_sound_bt_cards_cache_prev_tab = app.activeTab;
    return g_settings_sound_bt_cards_cache;
  }

  const bool entered_sound = g_settings_sound_bt_cards_cache_prev_tab != 8;
  g_settings_sound_bt_cards_cache_prev_tab = 8;

  const bool freeze_while_vol_drag = app.soundVolDragCode >= 0 && g_settings_sound_bt_cards_cache_valid;
  const bool stale = !g_settings_sound_bt_cards_cache_valid || entered_sound ||
                     (now - g_settings_sound_bt_cards_cache_mono_ms) >= kRefreshPeriodMs;

  if (freeze_while_vol_drag) return g_settings_sound_bt_cards_cache;
  if (!stale) return g_settings_sound_bt_cards_cache;

  eh::audio::PipeWireService::instance().start();
  g_settings_sound_bt_cards_cache = eh::audio::PipeWireService::instance().query_bluetooth_cards();
  g_settings_sound_bt_cards_cache_mono_ms = now;
  g_settings_sound_bt_cards_cache_valid = true;
  return g_settings_sound_bt_cards_cache;
}

std::vector<std::string> sound_bt_profile_row_labels(const eh::audio::BluetoothCardProfiles& bc) {
  std::vector<std::string> out;
  out.reserve(bc.profiles.size());
  for (const auto& p : bc.profiles) {
    out.push_back(p.description.empty() ? p.key : (p.description + " (" + p.key + ")"));
  }
  return out;
}
