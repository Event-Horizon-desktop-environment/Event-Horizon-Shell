#pragma once
#include <string>
#include "ux/disks/app_types.hpp"
namespace eh::disks {
inline constexpr int kImageDlgW = 480;
inline constexpr int kImageDlgH = 440;
struct ImageDialog {
  bool open = false;
  bool restore = false;  // false=create, true=restore
  TextField path;        // file path
  int focus = 0;
  bool hover_browse = false, hover_cancel = false, hover_go = false;
  bool pending = false;
  double progress = -1;
  std::string error;
};
void draw_image_dialog(struct AppState& app, cairo_t* cr);
bool image_dialog_click(struct AppState& app, int x, int y);
void image_dialog_move(struct AppState& app, int x, int y);
bool image_dialog_key(struct AppState& app, uint32_t sym, const char* utf8, int len);
inline constexpr int kAttachDlgW = 460;
inline constexpr int kAttachDlgH = 300;
struct AttachDialog {
  bool open = false;
  TextField path;
  bool read_only = false;
  int focus = 0;
  bool hover_ro = false, hover_cancel = false, hover_attach = false;
  bool pending = false;
  std::string error;
};
void draw_attach_dialog(struct AppState& app, cairo_t* cr);
bool attach_dialog_click(struct AppState& app, int x, int y);
void attach_dialog_move(struct AppState& app, int x, int y);
bool attach_dialog_key(struct AppState& app, uint32_t sym, const char* utf8, int len);
inline constexpr int kDrvSetDlgW = 460;
inline constexpr int kDrvSetDlgH = 420;
struct DriveSettingsDialog {
  bool open = false;
  int drive_idx = -1;
  bool standby = true;
  int standby_mins = 20;
  bool apm = false;
  int apm_level = 128;
  bool aam = false;
  bool write_cache = true;
  bool hover_standby = false, hover_apm = false, hover_aam = false;
  bool hover_cache = false, hover_cancel = false, hover_apply = false;
};
void draw_drive_settings_dialog(struct AppState& app, cairo_t* cr);
bool drive_settings_dialog_click(struct AppState& app, int x, int y);
void drive_settings_dialog_move(struct AppState& app, int x, int y);
}  // namespace eh::disks
