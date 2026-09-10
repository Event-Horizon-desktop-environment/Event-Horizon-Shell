#pragma once

#include "ux/Live_Wallpaper/app_types.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"

#include <cinttypes>
#include <cstdio>

namespace eh::live_wallpaper {

FILE* lw_log_file();
int64_t lw_now_us();

#define LW_LOG(fmt, ...) do { \
  FILE* _lf = lw_log_file(); \
  if (_lf) { \
    fprintf(_lf, "[lw %6.3f] " fmt "\n", lw_now_us() / 1000000.0, ##__VA_ARGS__); \
    fflush(_lf); \
  } \
} while(0)

void draw(AppState& app);

void schedule_frame(AppState& app);

void handle_click(AppState& app, int x, int y, int button);

void handle_pointer_move(AppState& app, int x, int y);

void handle_scroll(AppState& app, int x, int y, double dx, double dy);

// Internal helpers shared between app.cpp and input/events.cpp (gallery port
// lives in gallery.{h,cpp} in this dir).

void ensure_live_wallpaper_gallery(AppState& app);
void live_wallpaper_invalidate_hero(AppState& app);
void live_wallpaper_cycle_selection(AppState& app, int delta);
void wallpaper_clamp_page_lw(AppState& app, int perPage);
void live_wallpaper_process_video_thumbnails(AppState& app);
std::vector<std::string> live_wallpaper_scan_video_files(const std::string& dir_path);
void live_wallpaper_apply_video(const std::string& videoPath);
void live_wallpaper_clear_video_wallpaper();
void live_wallpaper_apply_video_with_matugen(AppState& app, const std::string& videoPath);
void live_wallpaper_start_hover_video_preview(AppState& app, const std::string& path, size_t galleryIndex = 0);
void live_wallpaper_stop_hover_video_preview(AppState& app);
void lw_remove_blurred_thumb_cache(const std::string& videoPath);

} // namespace eh::live_wallpaper
