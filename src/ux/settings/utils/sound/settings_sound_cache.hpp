#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct App;

namespace eh::audio {
struct BluetoothCardProfiles;
struct EngineSettings;
struct CompatDefaultSinkFormat;
}

void settings_sound_bt_invalidate_cache() noexcept;
void settings_sound_engine_paint_cache_invalidate() noexcept;
void settings_sound_engine_paint_cache_read(const App& app, eh::audio::EngineSettings* es_out,
                                            eh::audio::CompatDefaultSinkFormat* cf_out);
const std::vector<eh::audio::BluetoothCardProfiles>& settings_sound_bt_cards_cached(App& app);
std::vector<std::string> sound_bt_profile_row_labels(const eh::audio::BluetoothCardProfiles& bc);
