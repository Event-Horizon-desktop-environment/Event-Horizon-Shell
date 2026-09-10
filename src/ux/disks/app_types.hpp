#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include "ux/disks/dialogs/create_partition_dialog.hpp"
#include "ux/disks/dialogs/format_disk_dialog.hpp"
#include "ux/disks/dialogs/format_volume_dialog.hpp"
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

inline constexpr int kHeaderH = 56;
inline constexpr int kSidebarRowH = 64;
inline constexpr int kBtnH = 32;
inline constexpr int kPartitionRowH = 48;

struct AppState {
  AppState();
  ~AppState();

  eh::wayland::WaylandConnection wl{};
  eh::wayland::WaylandSeat seat{};

  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  wl_shm* shm = nullptr;

  int width = 980;
  int height = 680;
  bool running = true;
  bool pendingRedraw = false;

  std::array<eh::wayland::ShmBuffer, 2> buf{};
  wl_callback* frame_cb = nullptr;
  int last_paint_w = -1;
  int last_paint_h = -1;

  double pointerX = 0, pointerY = 0;
  bool pointerLeftDown = false;
  uint64_t last_click_ms = 0;
  int click_count = 0;

  double bgR = 0.12, bgG = 0.12, bgB = 0.14;
  double surfaceR = 0.16, surfaceG = 0.16, surfaceB = 0.18;
  double accentR = 0.40, accentG = 0.70, accentB = 0.95;
  double textR = 0.92, textG = 0.92, textB = 0.95;
  double textSecR = 0.65, textSecG = 0.65, textSecB = 0.68;
  double outlineR = 0.30, outlineG = 0.32, outlineB = 0.34;

  int selected_drive = -1;
  int selected_block = -1;
  bool needs_refresh = true;

  int sidebarW = 220;
  int sidebarHover = -1;
  int sidebarScroll = 0;

  int contentScroll = 0;
  int contentH = 0;

  int headerAttachX = 0;
  int headerCreateX = 0;
  int headerMenuX = 0;

  bool headerAttachHover = false;
  bool headerCreateHover = false;
  bool headerMenuHover = false;

  int partitionHover = -1;
  int partitionMenuHover = -1;
  int partitionBtnHover = -1;
  bool partitionMenuOpen = false;
  int partitionMenuX = 0, partitionMenuY = 0;
  int partitionMenuItemHover = -1;

  bool driveMenuOpen = false;
  int driveMenuX = 0, driveMenuY = 0;
  int driveMenuItemHover = -1;

  // Sidebar drive hamburger menu state.
  int sidebarDriveMenuIdx = -1;
  int sidebarDriveMenuHover = -1;
  bool sidebarDriveMenuOpen = false;
  int sidebarDriveMenuX = 0, sidebarDriveMenuY = 0;
  int sidebarDriveBtnHover = -1;

  // Status bar.
  std::string statusText;

  std::mutex mtx;
  std::string mountResultPath;
  bool mountResultOk = false;
  bool mountPending = false;

  std::string unmountResultPath;
  bool unmountResultOk = false;
  bool unmountPending = false;

  bool opActive = false;
  std::string opLabel;

  CreatePartitionDialog createPartDlg{};
  FormatDiskDialog fmtDiskDlg{};
  FormatVolumeDialog fmtDlg{};

  eh::icons::IconCache icons{};
  std::string lastIconTheme;
};

}
