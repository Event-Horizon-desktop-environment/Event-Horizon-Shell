#pragma once
#include <cstdint>
#include <string>
#include "ux/disks/app_types.hpp"
namespace eh::disks {
inline constexpr int kResizeDlgW = 460;
inline constexpr int kResizeDlgH = 420;
struct ResizeDialog {
  bool open = false;
  int block_idx = -1;
  uint64_t cur_size = 0, min_size = 0, max_size = 0;
  uint64_t new_size = 0;
  bool missing_tools = false;
  std::string missing_msg;
  bool hover_cancel = false, hover_resize = false;
  bool pending = false;
  std::string error;
};
void draw_resize_dialog(struct AppState& app, cairo_t* cr);
bool resize_dialog_click(struct AppState& app, int x, int y);
void resize_dialog_move(struct AppState& app, int x, int y);
}  // namespace eh::disks
