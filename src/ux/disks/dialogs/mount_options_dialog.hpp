#pragma once
#include <string>
#include "ux/disks/app_types.hpp"
namespace eh::disks {
inline constexpr int kMountOptsDlgW = 480;
inline constexpr int kMountOptsDlgH = 520;
struct MountOptionsDialog {
  bool open = false;
  int block_idx = -1;
  bool auto_mount = true;
  bool at_startup = false;
  bool show_in_ui = true;
  TextField mount_point;
  TextField fs_type;
  TextField options;
  int focus = 0;
  bool hover_auto = false, hover_startup = false, hover_show = false;
  bool hover_defaults = false, hover_cancel = false, hover_apply = false;
  bool pending = false;
};
void draw_mount_options_dialog(struct AppState& app, cairo_t* cr);
bool mount_options_dialog_click(struct AppState& app, int x, int y);
void mount_options_dialog_move(struct AppState& app, int x, int y);
bool mount_options_dialog_key(struct AppState& app, uint32_t sym, const char* utf8,
                              int len);
}  // namespace eh::disks
