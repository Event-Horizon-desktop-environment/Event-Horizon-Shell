#pragma once
#include <string>
#include "ux/disks/app_types.hpp"
namespace eh::disks {
inline constexpr int kUnlockDlgW = 420;
inline constexpr int kUnlockDlgH = 360;
struct UnlockDialog {
  bool open = false;
  int block_idx = -1;
  TextField pass;
  bool show = false;
  bool unlock_bg = false;   // Disks 51: background unlocking
  bool remember = true;     // keyring
  bool hover_show = false, hover_bg = false, hover_rem = false;
  bool hover_cancel = false, hover_unlock = false;
  bool pending = false;
  std::string error;
};
void draw_unlock_dialog(struct AppState& app, cairo_t* cr);
bool unlock_dialog_click(struct AppState& app, int x, int y);
void unlock_dialog_move(struct AppState& app, int x, int y);
bool unlock_dialog_key(struct AppState& app, uint32_t sym, const char* utf8, int len);
}  // namespace eh::disks
