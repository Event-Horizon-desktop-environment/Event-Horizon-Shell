#include "../app.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "../manager.hpp"
#include "../drive.hpp"
#include "../block.hpp"

namespace eh::disks {

void handle_click(AppState& app, int x, int y, int button) {
  disk_log("click button=%d x=%d y=%d sel=%d", button, x, y, app.selected_drive);
  if (button != 272) return; // BTN_LEFT (Linux evdev)
  int w = app.width;
  int sidebar_w = app.sidebarW;
  int content_x = sidebar_w;

  auto drives = Manager::instance().get_drives();

  // Close menus on any click
  bool pm_was_open = app.partitionMenuOpen;
  bool dm_was_open = app.driveMenuOpen;
  bool sm_was_open = app.sidebarDriveMenuOpen;

  app.partitionMenuOpen = false;
  app.driveMenuOpen = false;
  app.sidebarDriveMenuOpen = false;
  app.sidebarDriveMenuIdx = -1;

  bool menu_was_open = pm_was_open || dm_was_open || sm_was_open;

  // Dialog (modal).
  if (app.createPartDlg.open) {
    if (handle_create_partition_dialog_click(app, x, y))
      schedule_frame(app);
    return;
  }
  if (app.fmtDiskDlg.open) {
    if (handle_format_disk_dialog_click(app, x, y))
      schedule_frame(app);
    return;
  }
  if (app.fmtDlg.open) {
    if (handle_format_dialog_click(app, x, y, app.selected_block))
      schedule_frame(app);
    return;
  }

  // Header buttons.
  if (y < kHeaderH) {
    if (x < 40) {
      app.driveMenuOpen = !pm_was_open;
      app.driveMenuX = 40;
      app.driveMenuY = kHeaderH;
      schedule_frame(app);
      return;
    }
    if (x >= app.headerAttachX && x < app.headerAttachX + 100) {
      schedule_frame(app);
      return;
    }
    if (x >= app.headerCreateX && x < app.headerCreateX + 120) {
      schedule_frame(app);
      return;
    }
    if (x >= app.headerMenuX && x < app.headerMenuX + 36) {
      app.driveMenuOpen = !dm_was_open;
      app.driveMenuX = app.headerMenuX;
      app.driveMenuY = kHeaderH;
      schedule_frame(app);
      return;
    }
    schedule_frame(app);
    return;
  }

  // If a menu was open, check for an item action, then close it.
  if (menu_was_open) {
    // Sidebar drive hamburger menu (170×206, 6 items)
    if (sm_was_open && app.sidebarDriveMenuOpen == false &&
        app.sidebarDriveMenuIdx >= 0) {
      int mx = app.sidebarDriveMenuX, my = app.sidebarDriveMenuY;
      if (x >= mx && x < mx + 170 && y >= my && y < my + 32 * 6 + 8) {
        int mi = (y - my - 4) / 32;
        if (mi == 0) {
          // "Format Disk…" → open format disk dialog
          app.fmtDiskDlg = FormatDiskDialog{};
          app.fmtDiskDlg.open = true;
          schedule_frame(app);
          return;
        }
      }
    }

    // Partition context menu (180×200, 6 items)
    if (pm_was_open && app.partitionMenuOpen == false &&
        app.selected_block >= 0) {
      int mx = app.partitionMenuX, my = app.partitionMenuY;
      if (x >= mx && x < mx + 180 && y >= my && y < my + 32 * 6 + 8) {
        int mi = (y - my - 4) / 32;
        if (mi >= 0 && mi < 6) {
          if (mi == 0) {
            app.createPartDlg = CreatePartitionDialog{};
            app.createPartDlg.open = true;
            schedule_frame(app);
            return;
          }
          if (mi == 1) {
            app.fmtDlg = FormatVolumeDialog{};
            app.fmtDlg.open = true;
            schedule_frame(app);
            return;
          }
        }
      }
    }
    schedule_frame(app);
    return;
  }

  // Sidebar.
  if (x < sidebar_w) {
    int item_h = kSidebarRowH;
    int idx = (y - kHeaderH + app.sidebarScroll) / item_h;

    // Check if clicking the drive row hamburger menu button
    if (idx >= 0 && idx < static_cast<int>(drives.size())) {
      int iy = kHeaderH + idx * item_h - app.sidebarScroll;
      int bw = 20;
      int bx = sidebar_w - 28;
      int by = iy + (item_h - bw) / 2;

      if (x >= bx && x < bx + bw && y >= by && y < by + bw) {
        app.sidebarDriveMenuOpen = true;
        app.sidebarDriveMenuIdx = idx;
        app.sidebarDriveMenuX = bx - 170 + bw;
        app.sidebarDriveMenuY = by + bw + 4;
        schedule_frame(app);
        return;
      }
    }

    disk_log("sidebar x=%d y=%d scroll=%d idx=%d ndrives=%zu",
             x, y, app.sidebarScroll, idx, drives.size());
    if (idx >= 0 && idx < static_cast<int>(drives.size())) {
      app.selected_drive = idx;
      app.selected_block = -1;
      app.contentScroll = 0;
      schedule_frame(app);
    }
    return;
  }

  // Content area.
  if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
    auto& drive = drives[app.selected_drive];
    auto blocks = filter_blocks(drive->blocks());

    int cx = content_x + 12;
    int cw = w - sidebar_w - 24;
    int content_y = kHeaderH + 8 + 110 + 8 + 8 + 24 + 58;
    int row_idx = (y + app.contentScroll - content_y) / kPartitionRowH;

    if (row_idx >= 0 && row_idx < static_cast<int>(blocks.size())) {
      int ry = content_y + row_idx * kPartitionRowH - app.contentScroll;
      int btn_right_x = cx + cw - 12;

      auto feats = blocks[row_idx]->get_features();
      int btn_w = (feats & FEATURE_CAN_MOUNT) ? 70 : 80;
      int btn_x = btn_right_x - btn_w;
      int btn_y = ry + (kPartitionRowH - kBtnH) / 2;

      if (x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + kBtnH) {
        auto block = blocks[row_idx];
        app.opActive = true;

        if (feats & FEATURE_CAN_MOUNT) {
          app.opLabel = "Mounting " + block->get_description() + "…";
          block->mount_async("", [&app, block, row_idx](bool ok, std::string path) {
            (void)row_idx;
            (void)block;
            std::lock_guard<std::mutex> lock(app.mtx);
            app.mountResultOk = ok;
            app.mountResultPath = path;
            app.mountPending = true;
          });
        } else if (feats & FEATURE_CAN_UNMOUNT) {
          app.opLabel = "Unmounting " + block->get_description() + "…";
          block->unmount_async([&app, block, row_idx](bool ok) {
            (void)row_idx;
            (void)block;
            std::lock_guard<std::mutex> lock(app.mtx);
            app.unmountResultOk = ok;
            app.unmountResultPath = block->get_device();
            app.unmountPending = true;
          });
        }
        schedule_frame(app);
        return;
      }

      int mw = 30;
      int mnu_x = btn_right_x - (feats & (FEATURE_CAN_MOUNT | FEATURE_CAN_UNMOUNT) ? btn_w + 4 : 0) - mw;
      if (x >= mnu_x && x < mnu_x + mw && y >= btn_y && y < btn_y + kBtnH) {
        app.selected_block = row_idx;
        app.partitionMenuOpen = true;
        app.partitionMenuX = mnu_x;
        app.partitionMenuY = btn_y + kBtnH + 4;
        schedule_frame(app);
        return;
      }

      app.selected_block = row_idx;
      schedule_frame(app);
    }
  }

  schedule_frame(app);
}

void handle_move(AppState& app, int x, int y) {
  app.pointerX = x;
  app.pointerY = y;

  int w = app.width;
  int sidebar_w = app.sidebarW;

  app.headerAttachHover = false;
  app.headerCreateHover = false;
  app.headerMenuHover = false;
  app.sidebarHover = -1;
  app.partitionHover = -1;
  app.partitionBtnHover = -1;
  app.partitionMenuHover = -1;
  app.partitionMenuItemHover = -1;
  app.driveMenuItemHover = -1;
  app.sidebarDriveBtnHover = -1;
  app.sidebarDriveMenuHover = -1;

  // Dialog hover.
  if (app.createPartDlg.open) {
    handle_create_partition_dialog_move(app, x, y);
    schedule_frame(app);
    return;
  }
  if (app.fmtDiskDlg.open) {
    handle_format_disk_dialog_move(app, x, y);
    schedule_frame(app);
    return;
  }
  if (app.fmtDlg.open) {
    handle_format_dialog_move(app, x, y);
    schedule_frame(app);
    return;
  }

  // Header
  if (y < kHeaderH) {
    if (x >= app.headerAttachX && x < app.headerAttachX + 100)
      app.headerAttachHover = true;
    else if (x >= app.headerCreateX && x < app.headerCreateX + 120)
      app.headerCreateHover = true;
    else if (x >= app.headerMenuX && x < app.headerMenuX + 36)
      app.headerMenuHover = true;
    schedule_frame(app);
    return;
  }

  // Drive header menu items
  if (app.driveMenuOpen) {
    int mx = app.driveMenuX, my = app.driveMenuY;
    if (x >= mx && x < mx + 160 && y >= my && y < my + 32 * 3 + 8) {
      int idx = (y - my - 4) / 32;
      if (idx >= 0 && idx < 3) app.driveMenuItemHover = idx;
      schedule_frame(app);
      return;
    }
  }

  // Sidebar drive row menu items
  if (app.sidebarDriveMenuOpen) {
    int mx = app.sidebarDriveMenuX, my = app.sidebarDriveMenuY;
    if (x >= mx && x < mx + 170 && y >= my && y < my + 32 * 6 + 8) {
      int idx = (y - my - 4) / 32;
      if (idx >= 0 && idx < 6) app.sidebarDriveMenuHover = idx;
      schedule_frame(app);
      return;
    }
  }

  // Partition context menu items
  if (app.partitionMenuOpen) {
    int mx = app.partitionMenuX, my = app.partitionMenuY;
    if (x >= mx && x < mx + 180 && y >= my && y < my + 32 * 6 + 8) {
      int idx = (y - my - 4) / 32;
      if (idx >= 0 && idx < 6) app.partitionMenuItemHover = idx;
      schedule_frame(app);
      return;
    }
  }

  // Sidebar
  if (x < sidebar_w) {
    auto drives = Manager::instance().get_drives();
    int item_h = kSidebarRowH;
    int iy_base = kHeaderH - app.sidebarScroll;

    int idx = (y - kHeaderH + app.sidebarScroll) / item_h;
    if (idx >= 0 && idx < static_cast<int>(drives.size())) {
      app.sidebarHover = idx;

      // Check hamburger button hover
      int iy = iy_base + idx * item_h;
      int bw = 20;
      int bx = sidebar_w - 28;
      int by = iy + (item_h - bw) / 2;
      if (x >= bx && x < bx + bw && y >= by && y < by + bw) {
        app.sidebarDriveBtnHover = idx;
      }

      schedule_frame(app);
    }
    return;
  }

  // Content area - partition list
  if (app.selected_drive >= 0) {
    auto drives = Manager::instance().get_drives();
    if (app.selected_drive < static_cast<int>(drives.size())) {
      auto blocks = filter_blocks(drives[app.selected_drive]->blocks());
      int content_x = sidebar_w + 12;
      int cw = w - sidebar_w - 24;
    int content_y = kHeaderH + 8 + 110 + 8 + 8 + 24 + 58;
      int row_idx = (y + app.contentScroll - content_y) / kPartitionRowH;

      if (row_idx >= 0 && row_idx < static_cast<int>(blocks.size())) {
        app.partitionHover = row_idx;

        int ry = content_y + row_idx * kPartitionRowH - app.contentScroll;
        int btn_right_x = content_x + cw - 12;

        auto feats = blocks[row_idx]->get_features();
        int btn_w = (feats & FEATURE_CAN_MOUNT) ? 70 : 80;
        btn_w = (feats & (FEATURE_CAN_MOUNT | FEATURE_CAN_UNMOUNT)) ? btn_w : 0;
        if (btn_w > 0) {
          int btn_x = btn_right_x - btn_w;
          int btn_y = ry + (kPartitionRowH - kBtnH) / 2;
          if (x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + kBtnH) {
            app.partitionBtnHover = row_idx;
          }
        }

        int mw = 30;
        int mnu_x = btn_right_x - (btn_w > 0 ? btn_w + 4 : 0) - mw;
        int mnu_y = ry + (kPartitionRowH - kBtnH) / 2;
        if (x >= mnu_x && x < mnu_x + mw && y >= mnu_y && y < mnu_y + kBtnH) {
          app.partitionMenuHover = row_idx;
        }
        schedule_frame(app);
      }
    }
  }
}

void handle_scroll(AppState& app, int x, int, double dy) {
  (void)x;
  int sidebar_w = app.sidebarW;

  if (x < sidebar_w) {
    auto drives = Manager::instance().get_drives();
    int total_h = static_cast<int>(drives.size()) * kSidebarRowH;
    int view_h = app.height - kHeaderH;
    int max_scroll = std::max(0, total_h - view_h);
    app.sidebarScroll = std::clamp(app.sidebarScroll + static_cast<int>(dy * 40), 0, max_scroll);
  } else {
    int view_content_h = app.height - kHeaderH - 8 - 110 - 8 - 8 - 24 - 58 - 20;
    int max_scroll = std::max(0, app.contentH - view_content_h);
    app.contentScroll = std::clamp(app.contentScroll + static_cast<int>(dy * 40), 0, max_scroll);
  }
  schedule_frame(app);
}

}
