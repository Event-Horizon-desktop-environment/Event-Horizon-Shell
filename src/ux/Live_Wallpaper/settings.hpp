#pragma once

#include <string>

namespace eh::live_wallpaper {

struct AppState;
struct LiveWallpaperConfig;  // defined in app_types.hpp

// Settings TOML.
// Persists live-wallpaper-relevant fields to its own TOML file
// so the standalone binary remembers wallpaper location etc.
// Uses LiveWallpaperConfig (local to this standalone; no ::Settings or main settings headers).
void lw_load_settings(LiveWallpaperConfig& cfg);
void lw_save_settings(const LiveWallpaperConfig& cfg);
void lw_load_ui_config(AppState& app);
void lw_save_ui_config(const AppState& app);
std::string lw_settings_path();

} // namespace eh::live_wallpaper
