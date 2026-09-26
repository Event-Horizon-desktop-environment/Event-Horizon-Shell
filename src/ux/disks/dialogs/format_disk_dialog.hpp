#include <cairo/cairo.h>
#pragma once
// Format Disk — Disks 51: erase mode + partition scheme + confirm.

#include <string>

namespace eh::disks {

inline constexpr int kFormatDiskDlgW = 440;
inline constexpr int kFormatDiskDlgH = 380;

struct FormatDiskDialog {
  bool open = false;
  int erase_mode = 0;  // 0=don't overwrite, 1=overwrite with zeros
  int scheme = 0;      // 0=GPT, 1=MBR/DOS
  bool confirm = false;
  int hover_radio_erase = -1;
  int hover_scheme = -1;
  bool hover_confirm = false;
  bool hover_cancel = false;
  bool hover_format = false;
  bool pending = false;
};

void draw_format_disk_dialog(struct AppState& app, cairo_t* cr);
bool format_disk_dialog_click(struct AppState& app, int x, int y);
void format_disk_dialog_move(struct AppState& app, int x, int y);
bool format_disk_dialog_key(struct AppState& app, uint32_t sym);

}  // namespace eh::disks
