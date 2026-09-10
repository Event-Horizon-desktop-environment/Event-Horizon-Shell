#pragma once

#include "ux/disks/app_types.hpp"

namespace eh::disks {

// Utilities (defined in app.cpp).
std::string size_str(uint64_t bytes);
void set_rgba(cairo_t* cr, double r, double g, double b, double a);
void set_rgb(cairo_t* cr, double r, double g, double b);
void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);
void draw_icon_surface(cairo_t* cr, const eh::icons::IconEntry* icon,
                        int x, int y, int target_size);
const char* drive_icon_name(const std::shared_ptr<Drive>& d);
std::vector<std::shared_ptr<Block>> filter_blocks(
    const std::vector<std::shared_ptr<Block>>& all);
void disk_log(const char* fmt, ...);

// UI draw functions (defined in ui/draw.cpp).
void draw_header(AppState& app, cairo_t* cr, int w);
void draw_sidebar(AppState& app, cairo_t* cr, int w, int view_h);
void draw_drive_details(AppState& app, cairo_t* cr, int x, int y,
                         const std::shared_ptr<Drive>& drive);
void draw_partition_bar(AppState& app, cairo_t* cr,
                         const std::vector<std::shared_ptr<Block>>& blocks,
                         int x, int y, int w,
                         uint64_t drive_size = 0);
void draw_partition_list(AppState& app, cairo_t* cr, int x, int y, int w,
                          const std::vector<std::shared_ptr<Block>>& blocks);
void draw_drive_header_menu(AppState& app, cairo_t* cr);
void draw_partition_context_menu(AppState& app, cairo_t* cr);
void draw_sidebar_drive_menu(AppState& app, cairo_t* cr);
void draw_status_bar(AppState& app, cairo_t* cr, int w);

// Input handlers (defined in input/events.cpp).
void handle_click(AppState& app, int x, int y, int button);
void handle_move(AppState& app, int x, int y);
void handle_scroll(AppState& app, int x, int, double dy);

// Embed / main loop (defined in embed/embed.cpp).
[[nodiscard]] int run_standalone();

// Core app (defined in app.cpp).
void draw(AppState& app);
void schedule_frame(AppState& app);

// Dialogs (defined in dialogs/).
void draw_format_disk_dialog(AppState& app, cairo_t* cr);
bool handle_format_disk_dialog_click(AppState& app, int x, int y);
void handle_format_disk_dialog_move(AppState& app, int x, int y);
void draw_format_volume_dialog(AppState& app, cairo_t* cr, int block_idx);
bool handle_format_dialog_click(AppState& app, int x, int y, int block_idx);
void handle_format_dialog_move(AppState& app, int x, int y);
void draw_create_partition_dialog(AppState& app, cairo_t* cr);
bool handle_create_partition_dialog_click(AppState& app, int x, int y);
void handle_create_partition_dialog_move(AppState& app, int x, int y);

}
