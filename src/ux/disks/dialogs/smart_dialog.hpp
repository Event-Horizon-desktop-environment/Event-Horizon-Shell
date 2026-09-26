#include <cairo/cairo.h>
#pragma once
#include <string>
namespace eh::disks {
struct AppState;
inline constexpr int kSmartDlgW = 620;
inline constexpr int kSmartDlgH = 480;
struct SmartDialog {
  bool open = false;
  int drive_idx = -1;
  bool hover_close = false;
  bool hover_short = false, hover_ext = false;
  bool pending = false;
  int scroll = 0;
};
void draw_smart_dialog(struct AppState& app, cairo_t* cr);
bool smart_dialog_click(struct AppState& app, int x, int y);
void smart_dialog_move(struct AppState& app, int x, int y);
bool smart_dialog_scroll(struct AppState& app, double dy);
}  // namespace eh::disks
