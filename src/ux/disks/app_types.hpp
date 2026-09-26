#pragma once
// Horizon Disks — application state (Disks 51 rebuild).
// Single selection model shared by the volume bar + block table (GParted sync).

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include "ux/disks/feature.h"
#include "ux/disks/jobs.hpp"
#include "ux/disks/ui/icons.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/block.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"

struct wl_callback;
struct wl_shm;
struct xdg_surface;
struct xdg_toplevel;

namespace eh::disks {

// Forward decls for dialogs (defined in dialogs/*.hpp).
struct FormatDiskDialog;
struct FormatVolumeDialog;
struct CreatePartitionDialog;
struct ResizeDialog;
struct UnlockDialog;
struct SmartDialog;
struct BenchmarkDialog;
struct MountOptionsDialog;
struct ImageDialog;
struct AttachDialog;
struct DriveSettingsDialog;

struct TextField {
  std::string text;
  size_t cursor = 0;
  bool focused = false;
  bool password = false;
  int max_len = 128;
  void insert_text(const char* s, int n) {
    if (n <= 0 || !s) return;
    if (int(text.size()) + n > max_len) n = max_len - int(text.size());
    if (n <= 0) return;
    text.insert(cursor, s, size_t(n));
    cursor += size_t(n);
  }
  void backspace() {
    if (cursor == 0 || text.empty()) return;
    // Erase one UTF-8 codepoint.
    size_t p = cursor - 1;
    while (p > 0 && (text[p] & 0xC0) == 0x80) --p;
    text.erase(p, cursor - p);
    cursor = p;
  }
  void del() {
    if (cursor >= text.size()) return;
    size_t p = cursor;
    size_t q = p + 1;
    while (q < text.size() && (text[q] & 0xC0) == 0x80) ++q;
    text.erase(p, q - p);
  }
  void move_left() {
    if (cursor == 0) return;
    size_t p = cursor - 1;
    while (p > 0 && (text[p] & 0xC0) == 0x80) --p;
    cursor = p;
  }
  void move_right() {
    if (cursor >= text.size()) return;
    size_t q = cursor + 1;
    while (q < text.size() && (text[q] & 0xC0) == 0x80) ++q;
    cursor = q;
  }
};

enum class MenuKind { None, Main, Drive, Block };

struct MenuItem {
  std::string id;       // action id
  std::string label;    // "Format Disk…"
  std::string glyph;    // cairo glyph (unicode)
  bool enabled = true;
  std::string disabled_reason;  // shown in status bar on hover (Disks 51 hint)
  bool danger = false;
  bool separator_after = false;
};

struct Menu {
  bool open = false;
  MenuKind kind = MenuKind::None;
  int x = 0, y = 0;
  int drive_idx = -1;
  int block_idx = -1;
  std::vector<MenuItem> items;
  int hover = -1;
};

struct Toast {
  std::string text;
  double mono = 0;
  bool error = false;
};

struct AppState {
  AppState();
  ~AppState();

  eh::wayland::WaylandConnection wl{};
  eh::wayland::WaylandSeat seat{};

  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  wl_shm* shm = nullptr;

  int width = 1180;
  int height = 720;
  bool running = true;
  bool pendingRedraw = false;

  std::array<eh::wayland::ShmBuffer, 2> buf{};
  wl_callback* frame_cb = nullptr;
  int last_paint_w = -1;
  int last_paint_h = -1;

  double pointerX = 0, pointerY = 0;
  bool pointerLeftDown = false;

  // Palette (from shell chrome).
  double bgR = 0.10, bgG = 0.10, bgB = 0.12;
  double surfaceR = 0.14, surfaceG = 0.14, surfaceB = 0.16;
  double accentR = 0.35, accentG = 0.62, accentB = 0.92;
  double textR = 0.93, textG = 0.93, textB = 0.95;
  double textSecR = 0.62, textSecG = 0.63, textSecB = 0.66;
  double outlineR = 0.28, outlineG = 0.29, outlineB = 0.32;

  // Selection model (shared bar ↔ table, like GParted graphic ↔ list sync).
  int selected_drive = -1;
  int selected_block = -1;  // index into filtered blocks
  bool needs_refresh = true;

  // Search / filter (header field, filters drives + partitions).
  TextField search;
  bool search_focused = false;

  // Scroll
  int sidebarScroll = 0;
  int contentScroll = 0;
  int contentH = 0;

  // Hover (single frame, recomputed on move)
  bool hov_attach = false, hov_bench = false, hov_headermenu = false;
  bool hov_menu_btn = false;
  int hov_drive = -1;
  int hov_drive_menu_btn = -1;
  int hov_volseg = -1;  // index into layout vol_segs
  int hov_row = -1;
  int hov_row_mount = -1, hov_row_lock = -1, hov_row_gear = -1;
  bool hov_add_part = false;
  bool hov_power = false, hov_eject = false, hov_settings = false;
  bool hov_drivemenu = false;
  bool hov_job_cancel = false;

  // Header search hover/focus rect handled via TextField.focused.

  Menu menu{};

  std::string statusText;  // transient hint (menu disabled reasons, etc.)
  std::vector<Toast> toasts;

  std::mutex mtx;  // guards async result flags below
  bool opActive = false;
  std::string opLabel;

  // Dialogs (owned here to keep AppState aggregate simple).
  std::unique_ptr<FormatDiskDialog> fmtDiskDlg;
  std::unique_ptr<FormatVolumeDialog> fmtDlg;
  std::unique_ptr<CreatePartitionDialog> createPartDlg;
  std::unique_ptr<ResizeDialog> resizeDlg;
  std::unique_ptr<UnlockDialog> unlockDlg;
  std::unique_ptr<SmartDialog> smartDlg;
  std::unique_ptr<BenchmarkDialog> benchDlg;
  std::unique_ptr<MountOptionsDialog> mountOptsDlg;
  std::unique_ptr<ImageDialog> imageDlg;
  std::unique_ptr<AttachDialog> attachDlg;
  std::unique_ptr<DriveSettingsDialog> drvSettingsDlg;

  bool any_dialog_open() const;

  eh::icons::IconCache icons{};
  icons::DiskIconSet svg{};
  double spinner_phase = 0.0;
  double last_frame_mono = 0.0;
};

}  // namespace eh::disks
