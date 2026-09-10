#include "ux/Live_Wallpaper/settings.hpp"
#include "ux/Live_Wallpaper/app_types.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <toml++/toml.hpp>

#include "configuration/shell_config.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"

// Standalone live wallpaper: uses its own LiveWallpaperConfig (ported inside this codebase).
// No dependency on main settings ::Settings or ux/settings/* headers.

// Settings TOML helpers (standalone live, local types).

namespace eh::live_wallpaper {

std::string lw_settings_path() {
  return eh::config::state_component_toml_path("live_wallpaper");
}

void lw_load_settings(LiveWallpaperConfig& cfg) {
  const std::string path = lw_settings_path();
  if (path.empty() || !std::filesystem::exists(path)) return;
  try {
    toml::table tbl = toml::parse_file(path);
    // Prefer dedicated key (our own toml). Legacy "wallpaper" fallback for old standalone state files.
    toml::table* wp = tbl.get_as<toml::table>("live_wallpaper");
    if (!wp) wp = tbl.get_as<toml::table>("wallpaper");
    if (wp) {
      if (auto v = (*wp)["folder"].value<std::string>())
        if (!v->empty()) cfg.folder = eh::wallpaper::normalize_wallpaper_path(*v);
      if (auto v = (*wp)["image"].value<std::string>())
        if (!v->empty()) cfg.image = eh::wallpaper::normalize_wallpaper_path(*v);
      if (auto v = (*wp)["mode"].value<int64_t>())
        cfg.mode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(4)));
      if (auto v = (*wp)["enabled"].value<bool>())
        cfg.enabled = *v;
      if (auto v = (*wp)["folder_picker_mode"].value<int64_t>())
        cfg.folderPickerMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    }
  } catch (const toml::parse_error& e) {
    std::cerr << "[horizon-wallpaper] failed to parse " << path << ": " << e.description() << "\n";
  }
}

void lw_save_settings(const eh::live_wallpaper::LiveWallpaperConfig& cfg) {
  const std::string path = lw_settings_path();
  if (path.empty()) return;

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

  // Load existing file to preserve ui/markers sections written by lw_save_ui_config
  toml::table root;
  if (std::filesystem::exists(path)) {
    try { root = toml::parse_file(path); } catch (...) {}
  }

  // Purge legacy key that would leak into global wallpaper config via component merge.
  root.erase("wallpaper");

  toml::table lwp;
  lwp.insert_or_assign("folder", cfg.folder);
  lwp.insert_or_assign("image", cfg.image);
  lwp.insert_or_assign("mode", static_cast<int64_t>(cfg.mode));
  lwp.insert_or_assign("enabled", cfg.enabled);
  lwp.insert_or_assign("folder_picker_mode", static_cast<int64_t>(cfg.folderPickerMode));
  root.insert_or_assign("live_wallpaper", std::move(lwp));

  const std::string tmp = path + ".__lwtmp";
  std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    std::cerr << "[horizon-wallpaper] failed to write " << tmp << "\n";
    return;
  }
  ofs << root;
  ofs.close();

  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::cerr << "[horizon-wallpaper] failed to rename " << tmp << " -> " << path << ": " << ec.message() << "\n";
  }
}

} // namespace eh::live_wallpaper

// UI state (AppState-only fields).

namespace eh::live_wallpaper {

void lw_save_ui_config(const AppState& app) {
  const std::string path = lw_settings_path();
  if (path.empty()) return;

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

  // Merge into existing TOML or create fresh
  toml::table root;
  if (std::filesystem::exists(path)) {
    try {
      root = toml::parse_file(path);
    } catch (...) {}
  }

  // Purge legacy [wallpaper] key (used by older versions) so it cannot override
  // the main wallpaper component's TOML during global config merge.
  root.erase("wallpaper");

  toml::table ui;
  ui.insert_or_assign("opacity_pct", static_cast<int64_t>(std::clamp(app.liveWallpaperUiOpacityPct, 0, 100)));
  ui.insert_or_assign("gallery_scale_pct", static_cast<int64_t>(app.liveWallpaperGalleryScalePct));
  ui.insert_or_assign("gallery_sort_mode", static_cast<int64_t>(app.liveWallpaperGallerySortMode));
  ui.insert_or_assign("gallery_page", static_cast<int64_t>(app.liveWallpaperGalleryPage));
  ui.insert_or_assign("gallery_thumb_radius", static_cast<int64_t>(app.liveWallpaperGalleryThumbRadiusPx));
  root.insert_or_assign("ui", std::move(ui));

  toml::table markers;
  toml::array adultArr;
  for (const auto& [path, isAdult] : app.liveWallpaperAdultFlags) {
    if (isAdult) adultArr.push_back(path);
  }
  markers.insert_or_assign("adult", std::move(adultArr));
  root.insert_or_assign("markers", std::move(markers));

  const std::string tmp = path + ".__lwtmp";
  std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
  if (!ofs) { std::cerr << "[horizon-wallpaper] failed to write " << tmp << "\n"; return; }
  ofs << root;
  ofs.close();

  std::filesystem::rename(tmp, path, ec);
  if (ec) std::cerr << "[horizon-wallpaper] failed to rename: " << ec.message() << "\n";
}

void lw_load_ui_config(AppState& app) {
  const std::string path = lw_settings_path();
  if (path.empty() || !std::filesystem::exists(path)) return;
  try {
    toml::table tbl = toml::parse_file(path);
    if (auto* ui = tbl.get_as<toml::table>("ui")) {
      if (auto v = (*ui)["opacity_pct"].value<int64_t>())
        app.liveWallpaperUiOpacityPct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
      if (auto v = (*ui)["gallery_scale_pct"].value<int64_t>())
        app.liveWallpaperGalleryScalePct = static_cast<int>(std::clamp(*v, INT64_C(50), INT64_C(200)));
      if (auto v = (*ui)["gallery_sort_mode"].value<int64_t>())
        app.liveWallpaperGallerySortMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(2)));
      if (auto v = (*ui)["gallery_page"].value<int64_t>())
        app.liveWallpaperGalleryPage = static_cast<int>(std::max(*v, INT64_C(0)));
      if (auto v = (*ui)["gallery_thumb_radius"].value<int64_t>())
        app.liveWallpaperGalleryThumbRadiusPx = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(48)));
    }
    if (auto* markers = tbl.get_as<toml::table>("markers")) {
      if (auto* adultArr = (*markers)["adult"].as_array()) {
        app.liveWallpaperAdultFlags.clear();
        adultArr->for_each([&](auto&& v) {
          if constexpr (toml::is_string<decltype(v)>) {
            app.liveWallpaperAdultFlags[std::string(v)] = true;
          }
        });
      }
    }
  } catch (const toml::parse_error& e) {
    std::cerr << "[horizon-wallpaper] failed to parse " << path << ": " << e.description() << "\n";
  }
}

} // namespace eh::live_wallpaper (ui functions)
