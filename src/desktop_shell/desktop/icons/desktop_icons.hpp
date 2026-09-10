#pragma once

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace eh::shell::desktop {

struct DesktopApp;



enum class DesktopIconKind : std::uint8_t {

  Application = 0,

  File = 1,

  Folder = 2,

  Drive = 3,

  Trash = 4,

  MyComputer = 5,
};

struct DesktopIconItem {
  std::string desktop_path;
  std::string name;
  std::string exec;
  std::string icon_key;
  std::string drive_object_path;
  std::string drive_uuid;
  std::string drive_fstype;
  double cell_x = 0;
  double cell_y = 0;
  int grid_col = 0;
  int grid_row = 0;
  int layer = 0;
  DesktopIconKind kind = DesktopIconKind::Application;
  bool drive_mounted = true;
};

void desktop_icons_clear(DesktopApp& app);

void desktop_icons_purge_raster_cache_for(DesktopApp& app);

void desktop_icons_paint_layer(DesktopApp& app, size_t layer_index, int width, int height, cairo_t* cr);

bool desktop_icons_try_launch_at(DesktopApp& app, size_t layer_index, double x, double y);

void desktop_icons_launch_by_index(DesktopApp& app, int idx);

void desktop_icons_reload_from_disk(DesktopApp& app);

void desktop_icons_defer_rescan_after_background_io(DesktopApp& app);

void desktop_icons_poll_deferred_rescan(DesktopApp& app);

void desktop_icons_kick_initial_scan(DesktopApp& app);

[[nodiscard]] bool desktop_icons_hit_index(const DesktopApp& app, size_t layer_index, double x, double y, int* out_idx);

[[nodiscard]] bool desktop_icons_pointer_motion(DesktopApp& app);
[[nodiscard]] bool desktop_icons_left_release(DesktopApp& app);

[[nodiscard]] bool desktop_icons_left_press(DesktopApp& app);

void desktop_workspace_right_press(DesktopApp& app);

void desktop_icons_clear_marquee_selection(DesktopApp& app);
void desktop_icons_select_in_rect(DesktopApp& app, size_t layer_idx, double x0, double y0, double x1, double y1, int layer_w, int layer_h);
void desktop_icons_copy_marquee_selection_to_clipboard(DesktopApp& app);
[[nodiscard]] std::string desktop_icons_user_desktop_dir();
void desktop_icons_paste_from_clipboard(DesktopApp& app);
void desktop_icons_delete_marquee_selection(DesktopApp& app);
void desktop_icons_undo_last_paste(DesktopApp& app);
void desktop_icons_refresh_workspace(DesktopApp& app);
void desktop_icons_create_new_text_file(DesktopApp& app);
void desktop_icons_create_new_folder(DesktopApp& app);

}
