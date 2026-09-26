#pragma once

#include "ux/disks/app_types.hpp"

namespace eh::disks {

// ---- formatting / model helpers (app.cpp) ----
std::string size_str(uint64_t bytes);           // decimal GB/MB (Disks style)
std::string size_str_full(uint64_t bytes);      // "500 GB (500,107,862,016 bytes)"
void set_rgba(cairo_t* cr, double r, double g, double b, double a);
void set_rgb(cairo_t* cr, double r, double g, double b);
void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);
void draw_icon_surface(cairo_t* cr, const eh::icons::IconEntry* icon, int x, int y,
                       int target_size);
const char* drive_icon_name(const std::shared_ptr<Drive>& d);
std::string drive_display_name(const std::shared_ptr<Drive>& d);
std::string drive_subtitle(const std::shared_ptr<Drive>& d);
std::string block_short_name(const std::shared_ptr<Block>& b);
std::string block_fs_label(const std::shared_ptr<Block>& b);
std::string block_mount_str(const std::shared_ptr<Block>& b);
std::string partition_flags_str(const std::shared_ptr<Block>& b);
std::vector<std::shared_ptr<Block>> filtered_blocks(const std::shared_ptr<Drive>& drive,
                                                    const std::string& query);
std::vector<std::shared_ptr<Drive>> filtered_drives(
    const std::vector<std::shared_ptr<Drive>>& all, const std::string& query);
void drive_groups(const std::vector<std::shared_ptr<Drive>>& drives,
                  std::vector<std::shared_ptr<Drive>>* internal,
                  std::vector<std::shared_ptr<Drive>>* removable,
                  std::vector<std::shared_ptr<Drive>>* loop);
void disk_log(const char* fmt, ...);
void toast(AppState& app, const std::string& text, bool error = false);

// ---- menus (app.cpp) ----
std::vector<MenuItem> build_main_menu(AppState& app);
std::vector<MenuItem> build_drive_menu(AppState& app,
                                       const std::shared_ptr<Drive>& drive);
std::vector<MenuItem> build_block_menu(AppState& app,
                                       const std::shared_ptr<Block>& block);
void run_menu_action(AppState& app, const std::string& id, int drive_idx,
                     int block_idx);

// ---- core ----
void draw(AppState& app);
void schedule_frame(AppState& app);

// ---- input (input/events.cpp) ----
void handle_click(AppState& app, int x, int y, int button);
void handle_move(AppState& app, int x, int y);
void handle_scroll(AppState& app, int x, int y, double dy);
void handle_key(AppState& app, uint32_t sym, uint32_t state, const char* utf8,
                int utf8_len);
void handle_text_for_dialogs(AppState& app, uint32_t sym, const char* utf8,
                             int utf8_len);

// ---- embed ----
[[nodiscard]] int run_standalone();

}  // namespace eh::disks
