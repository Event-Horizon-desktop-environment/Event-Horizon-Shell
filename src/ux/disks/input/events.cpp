#include "ux/disks/app.hpp"

#include <algorithm>
#include <xkbcommon/xkbcommon.h>

#include "ux/disks/block.hpp"
#include "ux/disks/dialogs/benchmark_dialog.hpp"
#include "ux/disks/dialogs/create_partition_dialog.hpp"
#include "ux/disks/dialogs/format_disk_dialog.hpp"
#include "ux/disks/dialogs/format_volume_dialog.hpp"
#include "ux/disks/dialogs/image_dialog.hpp"
#include "ux/disks/dialogs/mount_options_dialog.hpp"
#include "ux/disks/dialogs/resize_dialog.hpp"
#include "ux/disks/dialogs/smart_dialog.hpp"
#include "ux/disks/dialogs/unlock_dialog.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/jobs.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/ui/layout.hpp"
#include "ux/disks/ui/theme.hpp"

namespace eh::disks {
namespace L = layout;
namespace T = theme;

static std::vector<std::shared_ptr<Drive>> cur_drives(AppState& app) {
  return filtered_drives(Manager::instance().get_drives(), app.search.text);
}
static std::vector<std::shared_ptr<Block>> cur_blocks(AppState& app,
                                                      const std::shared_ptr<Drive>& d) {
  if (!d) return {};
  return filtered_blocks(d, app.search.text);
}
static L::Layout cur_layout(AppState& app) {
  auto drives = cur_drives(app);
  std::shared_ptr<Drive> d;
  if (app.selected_drive >= 0 && app.selected_drive < int(drives.size()))
    d = drives[size_t(app.selected_drive)];
  auto blocks = cur_blocks(app, d);
  return L::compute(app, drives, blocks, d ? d->get_size() : 0);
}

// ---- dialogs first (topmost = last painted) ----

static bool dialogs_click(AppState& app, int x, int y) {
  // Topmost first.
  if (app.drvSettingsDlg->open) return drive_settings_dialog_click(app, x, y);
  if (app.attachDlg->open) return attach_dialog_click(app, x, y);
  if (app.imageDlg->open) return image_dialog_click(app, x, y);
  if (app.mountOptsDlg->open) return mount_options_dialog_click(app, x, y);
  if (app.benchDlg->open) return benchmark_dialog_click(app, x, y);
  if (app.smartDlg->open) return smart_dialog_click(app, x, y);
  if (app.unlockDlg->open) return unlock_dialog_click(app, x, y);
  if (app.resizeDlg->open) return resize_dialog_click(app, x, y);
  if (app.createPartDlg->open) return create_partition_dialog_click(app, x, y);
  if (app.fmtDlg->open) return format_volume_dialog_click(app, x, y);
  if (app.fmtDiskDlg->open) return format_disk_dialog_click(app, x, y);
  return false;
}
static void dialogs_move(AppState& app, int x, int y) {
  if (app.drvSettingsDlg->open) return drive_settings_dialog_move(app, x, y);
  if (app.attachDlg->open) return attach_dialog_move(app, x, y);
  if (app.imageDlg->open) return image_dialog_move(app, x, y);
  if (app.mountOptsDlg->open) return mount_options_dialog_move(app, x, y);
  if (app.benchDlg->open) return benchmark_dialog_move(app, x, y);
  if (app.smartDlg->open) return smart_dialog_move(app, x, y);
  if (app.unlockDlg->open) return unlock_dialog_move(app, x, y);
  if (app.resizeDlg->open) return resize_dialog_move(app, x, y);
  if (app.createPartDlg->open) return create_partition_dialog_move(app, x, y);
  if (app.fmtDlg->open) return format_volume_dialog_move(app, x, y);
  if (app.fmtDiskDlg->open) return format_disk_dialog_move(app, x, y);
}
static bool dialogs_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  if (app.drvSettingsDlg->open) {
    if (sym == XKB_KEY_Escape) {
      app.drvSettingsDlg->open = false;
      schedule_frame(app);
      return true;
    }
    return true;
  }
  if (app.attachDlg->open) return attach_dialog_key(app, sym, utf8, len);
  if (app.imageDlg->open) return image_dialog_key(app, sym, utf8, len);
  if (app.mountOptsDlg->open) return mount_options_dialog_key(app, sym, utf8, len);
  if (app.benchDlg->open) {
    if (sym == XKB_KEY_Escape) {
      app.benchDlg->open = false;
      schedule_frame(app);
      return true;
    }
    return true;
  }
  if (app.smartDlg->open) {
    if (sym == XKB_KEY_Escape) {
      app.smartDlg->open = false;
      schedule_frame(app);
      return true;
    }
    return true;
  }
  if (app.unlockDlg->open) return unlock_dialog_key(app, sym, utf8, len);
  if (app.resizeDlg->open) {
    if (sym == XKB_KEY_Escape) {
      app.resizeDlg->open = false;
      schedule_frame(app);
      return true;
    }
    return true;
  }
  if (app.createPartDlg->open) return create_partition_dialog_key(app, sym, utf8, len);
  if (app.fmtDlg->open) return format_volume_dialog_key(app, sym, utf8, len);
  if (app.fmtDiskDlg->open) return format_disk_dialog_key(app, sym);
  return false;
}

static void open_menu(AppState& app, MenuKind kind, int x, int y, int drive, int block,
                      std::vector<MenuItem> items) {
  app.menu.open = true;
  app.menu.kind = kind;
  app.menu.x = x;
  app.menu.y = y;
  app.menu.drive_idx = drive;
  app.menu.block_idx = block;
  app.menu.items = std::move(items);
  app.menu.hover = -1;
  schedule_frame(app);
}

void handle_click(AppState& app, int x, int y, int button) {
  disk_log("click btn=%d x=%d y=%d", button, x, y);
  if (app.any_dialog_open()) {
    if (dialogs_click(app, x, y)) {
      schedule_frame(app);
      return;
    }
  }
  auto drives = cur_drives(app);
  auto lo = cur_layout(app);

  // Menu open: select or dismiss.
  bool is_right = (button == 273);
  if (app.menu.open) {
    int idx = -1;
    for (size_t i = 0; i < app.menu.items.size(); ++i) {
      L::Rect row{lo.menu_rect.x + 4, lo.menu_rect.y + T::kMenuPad + int(i) * T::kMenuItemH,
                  lo.menu_rect.w - 8, T::kMenuItemH};
      if (row.contains(x, y)) {
        idx = int(i);
        break;
      }
    }
    if (idx >= 0 && idx < int(app.menu.items.size()) && app.menu.items[size_t(idx)].enabled) {
      std::string id = app.menu.items[size_t(idx)].id;
      int di = app.menu.drive_idx, bi = app.menu.block_idx;
      app.menu.open = false;
      run_menu_action(app, id, di, bi);
      return;
    }
    // Right-click always re-contextualizes: close old menu and fall through.
    // Left-click outside the menu dismisses; on-menu disabled click only
    // shows the reason (via hover) and keeps it open.
    if (is_right) {
      app.menu.open = false;
      schedule_frame(app);
    } else {
      if (lo.menu_rect.contains(x, y)) return;
      app.menu.open = false;
      schedule_frame(app);
      return;
    }
  }

  bool right = is_right;
  if (button != 272 && !right) return;

  // Header.
  if (lo.btn_menu.contains(x, y)) {
    if (right) return;
    open_menu(app, MenuKind::Main, lo.btn_menu.x, lo.btn_menu.y + lo.btn_menu.h, -1, -1,
              build_main_menu(app));
    return;
  }
  if (lo.search_field.contains(x, y)) {
    app.search.focused = true;
    schedule_frame(app);
    return;
  }
  app.search.focused = false;
  if (lo.btn_attach.contains(x, y)) {
    if (!right) run_menu_action(app, "attach", app.selected_drive, -1);
    return;
  }
  if (lo.btn_benchmark.contains(x, y)) {
    if (!right) {
      app.benchDlg->open = true;
      app.benchDlg->drive_idx = app.selected_drive;
      app.benchDlg->running = false;
      schedule_frame(app);
    }
    return;
  }
  if (lo.btn_headermenu.contains(x, y)) {
    if (!right)
      open_menu(app, MenuKind::Main, lo.btn_headermenu.x - 200, lo.btn_headermenu.y + 36, -1,
                -1, build_main_menu(app));
    return;
  }
  if (lo.job_pill.w > 0 && lo.job_pill.contains(x, y)) {
    app.statusText = JobTracker::instance().active().empty()
                         ? ""
                         : JobTracker::instance().active()[0]->label;
    schedule_frame(app);
    return;
  }

  // Sidebar rows.
  for (size_t i = 0; i < lo.drive_rows.size() && i < drives.size(); ++i) {
    if (lo.drive_row_menu_btn[i].contains(x, y)) {
      app.selected_drive = int(i);
      app.selected_block = -1;
      auto d = drives[i];
      open_menu(app, MenuKind::Drive, lo.drive_row_menu_btn[i].x - 210,
                lo.drive_row_menu_btn[i].y + 30, int(i), -1, build_drive_menu(app, d));
      return;
    }
    if (lo.drive_rows[i].contains(x, y)) {
      if (right) {
        app.selected_drive = int(i);
        app.selected_block = -1;
        open_menu(app, MenuKind::Drive, x, y, int(i), -1,
                  build_drive_menu(app, drives[i]));
        return;
      }
      app.selected_drive = int(i);
      app.selected_block = -1;
      app.contentScroll = 0;
      schedule_frame(app);
      return;
    }
  }

  if (drives.empty()) return;
  if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return;
  auto drive = drives[size_t(app.selected_drive)];
  auto blocks = cur_blocks(app, drive);

  // Drive card actions.
  if (lo.btn_power.contains(x, y)) {
    if (!right) run_menu_action(app, "poweroff", app.selected_drive, -1);
    return;
  }
  if (lo.btn_eject.contains(x, y)) {
    if (!right) run_menu_action(app, "eject", app.selected_drive, -1);
    return;
  }
  if (lo.btn_settings.contains(x, y)) {
    if (!right) run_menu_action(app, "drive-settings", app.selected_drive, -1);
    return;
  }
  if (lo.btn_drive_menu.contains(x, y)) {
    if (!right)
      open_menu(app, MenuKind::Drive, lo.btn_drive_menu.x - 210, lo.btn_drive_menu.y + 34,
                app.selected_drive, -1, build_drive_menu(app, drive));
    return;
  }
  if (lo.smart_banner.contains(x, y)) {
    if (!right) run_menu_action(app, "smart", app.selected_drive, -1);
    return;
  }
  // Add partition.
  if (lo.btn_add_part.contains(x, y)) {
    if (!right) run_menu_action(app, "add-part", app.selected_drive, -1);
    return;
  }
  // Volume bar segments.
  for (size_t i = 0; i < lo.vol_segs.size(); ++i) {
    if (lo.vol_segs[i].rect.contains(x, y)) {
      auto& s = lo.vol_segs[i];
      if (s.is_free) {
        if (right)
          open_menu(app, MenuKind::Drive, x, y, app.selected_drive, -1,
                    build_drive_menu(app, drive));
        else
          run_menu_action(app, "add-part", app.selected_drive, -1);
        return;
      }
      // Find block index.
      for (size_t bi = 0; bi < blocks.size(); ++bi) {
        if (blocks[bi] == s.block) {
          app.selected_block = int(bi);
          if (right)
            open_menu(app, MenuKind::Block, x, y, app.selected_drive, int(bi),
                      build_block_menu(app, blocks[bi]));
          else
            schedule_frame(app);
          return;
        }
      }
    }
  }
  // Table rows (scroll-adjusted).
  for (size_t i = 0; i < lo.rows.size(); ++i) {
    L::Rect r = lo.rows[i].rect;
    r.y -= app.contentScroll;
    if (!r.contains(x, y)) continue;
    auto& row = lo.rows[i];
    L::Rect mount = row.mount_btn;
    mount.y = r.y + (r.h - 30) / 2;
    L::Rect lock = row.lock_btn;
    if (lock.w > 0) lock.y = r.y + (r.h - 32) / 2;
    L::Rect gear = row.gear_btn;
    gear.y = r.y + (r.h - 32) / 2;
    app.selected_block = int(i);
    if (mount.w > 0 && mount.contains(x, y)) {
      if (!right) {
        auto f = row.block->get_features();
        if (f & (FEATURE_CAN_LOCK | FEATURE_CAN_UNLOCK) &&
            !(f & (FEATURE_CAN_MOUNT | FEATURE_CAN_UNMOUNT | FEATURE_CAN_SWAPON |
                   FEATURE_CAN_SWAPOFF)))
          run_menu_action(app, "lock-toggle", app.selected_drive, int(i));
        else
          run_menu_action(app, "mount-toggle", app.selected_drive, int(i));
      }
      return;
    }
    if (lock.w > 0 && lock.contains(x, y)) {
      if (!right) run_menu_action(app, "lock-toggle", app.selected_drive, int(i));
      return;
    }
    if (gear.contains(x, y)) {
      open_menu(app, MenuKind::Block, gear.x - 200, gear.y + 34, app.selected_drive, int(i),
                build_block_menu(app, row.block));
      return;
    }
    if (right) {
      open_menu(app, MenuKind::Block, x, y, app.selected_drive, int(i),
                build_block_menu(app, row.block));
      return;
    }
    schedule_frame(app);
    return;
  }
  // Details buttons.
  if (app.selected_block >= 0 && lo.details_card.w > 0) {
    auto hit = [&](L::Rect b) {
      L::Rect r = b;
      // details buttons were laid out unscrolled; adjust like paint.
      r.y = lo.details_card.y + lo.details_card.h - T::kCardPad - 30 - app.contentScroll;
      return r.contains(x, y);
    };
    if (hit(lo.btn_mount_opts)) {
      run_menu_action(app, "mount-opts", app.selected_drive, app.selected_block);
      return;
    }
    if (hit(lo.btn_edit_part)) {
      run_menu_action(app, "edit-part", app.selected_drive, app.selected_block);
      return;
    }
    if (hit(lo.btn_resize)) {
      run_menu_action(app, "resize", app.selected_drive, app.selected_block);
      return;
    }
    if (hit(lo.btn_check)) {
      run_menu_action(app, "check", app.selected_drive, app.selected_block);
      return;
    }
    if (hit(lo.btn_ownership)) {
      run_menu_action(app, "take-ownership", app.selected_drive, app.selected_block);
      return;
    }
  }
  // Clicked empty content: close menu / deselect block.
  app.selected_block = -1;
  schedule_frame(app);
}

void handle_move(AppState& app, int x, int y) {
  app.pointerX = x;
  app.pointerY = y;
  if (app.any_dialog_open()) {
    dialogs_move(app, x, y);
    schedule_frame(app);
    return;
  }
  app.hov_attach = app.hov_bench = app.hov_headermenu = app.hov_menu_btn = false;
  app.hov_drive = app.hov_drive_menu_btn = -1;
  app.hov_volseg = -1;
  app.hov_row = app.hov_row_mount = app.hov_row_lock = app.hov_row_gear = -1;
  app.hov_add_part = false;
  app.hov_power = app.hov_eject = app.hov_settings = app.hov_drivemenu = false;
  app.statusText.clear();

  auto lo = cur_layout(app);
  if (lo.btn_menu.contains(x, y)) app.hov_menu_btn = true;
  if (lo.btn_attach.contains(x, y)) app.hov_attach = true;
  if (lo.btn_benchmark.contains(x, y)) app.hov_bench = true;
  if (lo.btn_headermenu.contains(x, y)) app.hov_headermenu = true;
  if (lo.btn_add_part.contains(x, y)) app.hov_add_part = true;
  if (lo.btn_power.contains(x, y)) app.hov_power = true;
  if (lo.btn_eject.contains(x, y)) app.hov_eject = true;
  if (lo.btn_settings.contains(x, y)) app.hov_settings = true;
  if (lo.btn_drive_menu.contains(x, y)) app.hov_drivemenu = true;

  // Menu hover + disabled reason.
  if (app.menu.open) {
    app.menu.hover = -1;
    for (size_t i = 0; i < app.menu.items.size(); ++i) {
      L::Rect row{lo.menu_rect.x + 4, lo.menu_rect.y + T::kMenuPad + int(i) * T::kMenuItemH,
                  lo.menu_rect.w - 8, T::kMenuItemH};
      if (row.contains(x, y)) {
        app.menu.hover = int(i);
        if (!app.menu.items[i].enabled && !app.menu.items[i].disabled_reason.empty())
          app.statusText = app.menu.items[i].disabled_reason;
        break;
      }
    }
    schedule_frame(app);
    return;
  }

  for (size_t i = 0; i < lo.drive_rows.size(); ++i) {
    if (lo.drive_rows[i].contains(x, y)) app.hov_drive = int(i);
    if (lo.drive_row_menu_btn[i].contains(x, y)) app.hov_drive_menu_btn = int(i);
  }
  for (size_t i = 0; i < lo.vol_segs.size(); ++i)
    if (lo.vol_segs[i].rect.contains(x, y)) app.hov_volseg = int(i);
  for (size_t i = 0; i < lo.rows.size(); ++i) {
    L::Rect r = lo.rows[i].rect;
    r.y -= app.contentScroll;
    if (!r.contains(x, y)) continue;
    app.hov_row = int(i);
    auto& row = lo.rows[i];
    L::Rect mount = row.mount_btn;
    mount.y = r.y + (r.h - 30) / 2;
    L::Rect lock = row.lock_btn;
    if (lock.w > 0) {
      lock.y = r.y + (r.h - 32) / 2;
      if (lock.contains(x, y)) app.hov_row_lock = int(i);
    }
    L::Rect gear = row.gear_btn;
    gear.y = r.y + (r.h - 32) / 2;
    if (mount.w > 0 && mount.contains(x, y)) app.hov_row_mount = int(i);
    if (gear.contains(x, y)) app.hov_row_gear = int(i);
  }
  schedule_frame(app);
}

void handle_scroll(AppState& app, int x, int y, double dy) {
  if (app.smartDlg->open) {
    if (smart_dialog_scroll(app, dy)) return;
  }
  if (app.any_dialog_open()) return;
  auto lo = cur_layout(app);
  if (x < lo.sidebar.x + lo.sidebar.w) {
    auto drives = cur_drives(app);
    int total = int(drives.size()) * T::kSidebarRowH;
    int view = lo.sidebar_list.h;
    int mx = std::max(0, total - view);
    app.sidebarScroll = std::clamp(app.sidebarScroll + int(dy * 40), 0, mx);
  } else {
    int view = lo.content.h;
    int mx = std::max(0, app.contentH - view);
    app.contentScroll = std::clamp(app.contentScroll + int(dy * 40), 0, mx);
  }
  (void)y;
  schedule_frame(app);
}

void handle_key(AppState& app, uint32_t sym, uint32_t state, const char* utf8, int utf8_len) {
  const uint32_t kPressed = 1;  // WL_KEYBOARD_KEY_STATE_PRESSED
  if (state != kPressed && state != 2) return;  // + repeated
  // Dialogs get first shot (text fields included).
  if (app.any_dialog_open()) {
    if (dialogs_key(app, sym, utf8, utf8_len)) {
      schedule_frame(app);
      return;
    }
  }
  // Search field editing.
  if (app.search.focused) {
    if (sym == XKB_KEY_Escape) {
      app.search.focused = false;
      app.search.text.clear();
      app.search.cursor = 0;
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_BackSpace) {
      app.search.backspace();
      app.selected_drive = 0;
      app.selected_block = -1;
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Delete) {
      app.search.del();
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Left) {
      app.search.move_left();
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Right) {
      app.search.move_right();
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Return) {
      app.search.focused = false;
      schedule_frame(app);
      return;
    }
    if (utf8_len > 0 && utf8) {
      app.search.insert_text(utf8, utf8_len);
      app.selected_drive = 0;
      app.selected_block = -1;
      schedule_frame(app);
      return;
    }
    return;
  }
  if (app.menu.open) {
    if (sym == XKB_KEY_Escape) {
      app.menu.open = false;
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Up || sym == XKB_KEY_Down) {
      int n = int(app.menu.items.size());
      if (n > 0) {
        int h = app.menu.hover < 0 ? (sym == XKB_KEY_Up ? n - 1 : 0)
                                   : (app.menu.hover + (sym == XKB_KEY_Up ? -1 : 1) + n) % n;
        // Skip disabled.
        for (int k = 0; k < n; ++k) {
          if (app.menu.items[size_t(h)].enabled) break;
          h = (h + (sym == XKB_KEY_Up ? -1 : 1) + n) % n;
        }
        app.menu.hover = h;
      }
      schedule_frame(app);
      return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      int h = app.menu.hover;
      if (h >= 0 && h < int(app.menu.items.size()) && app.menu.items[size_t(h)].enabled) {
        std::string id = app.menu.items[size_t(h)].id;
        int di = app.menu.drive_idx, bi = app.menu.block_idx;
        app.menu.open = false;
        run_menu_action(app, id, di, bi);
      }
      return;
    }
  }
  auto drives = cur_drives(app);
  if (sym == XKB_KEY_Escape) {
    if (app.menu.open) app.menu.open = false;
    else app.selected_block = -1;
    schedule_frame(app);
    return;
  }
  if (sym == XKB_KEY_slash) {
    app.search.focused = true;
    schedule_frame(app);
    return;
  }
  if (sym == XKB_KEY_Up || sym == XKB_KEY_Down) {
    if (drives.empty()) return;
    int d = app.selected_drive < 0 ? 0 : app.selected_drive + (sym == XKB_KEY_Up ? -1 : 1);
    d = std::clamp(d, 0, int(drives.size()) - 1);
    app.selected_drive = d;
    app.selected_block = -1;
    app.contentScroll = 0;
    schedule_frame(app);
    return;
  }
  if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
    if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return;
    auto blocks = cur_blocks(app, drives[size_t(app.selected_drive)]);
    if (blocks.empty()) return;
    int b = app.selected_block < 0 ? 0 : app.selected_block + (sym == XKB_KEY_Left ? -1 : 1);
    b = std::clamp(b, 0, int(blocks.size()) - 1);
    app.selected_block = b;
    schedule_frame(app);
    return;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (app.selected_block >= 0)
      run_menu_action(app, "mount-toggle", app.selected_drive, app.selected_block);
    schedule_frame(app);
    return;
  }
  if (sym == XKB_KEY_Delete || sym == XKB_KEY_BackSpace) {
    if (app.selected_block >= 0)
      run_menu_action(app, "delete", app.selected_drive, app.selected_block);
    return;
  }
}

void handle_text_for_dialogs(AppState& app, uint32_t sym, const char* utf8, int utf8_len) {
  handle_key(app, sym, 1, utf8, utf8_len);
}

}  // namespace eh::disks
