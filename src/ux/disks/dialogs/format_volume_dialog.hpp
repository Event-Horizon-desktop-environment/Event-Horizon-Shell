#pragma once
// Format Volume — Disks 51: name + erase + type + LUKS passphrase/confirm.

#include <string>

#include "ux/disks/app_types.hpp"

namespace eh::disks {

inline constexpr int kFormatVolDlgW = 460;
inline constexpr int kFormatVolDlgH = 560;

struct FormatVolumeDialog {
  bool open = false;
  int block_idx = -1;
  TextField vol_name;
  int erase_mode = 0;  // 0=quick, 1=zero
  int fs_idx = 0;      // into kFsList
  bool encrypt = false;
  TextField pass;
  TextField pass_confirm;
  bool show_pass = false;
  int focus = 0;  // 0=name,1=pass,2=confirm
  int hover_fs = -1;
  int hover_erase = -1;
  bool hover_encrypt = false;
  bool hover_show = false;
  bool hover_cancel = false;
  bool hover_format = false;
  bool pending = false;
  std::string error;
};

void draw_format_volume_dialog(struct AppState& app, cairo_t* cr);
bool format_volume_dialog_click(struct AppState& app, int x, int y);
void format_volume_dialog_move(struct AppState& app, int x, int y);
bool format_volume_dialog_key(struct AppState& app, uint32_t sym, const char* utf8,
                              int len);

}  // namespace eh::disks
