#include "create_partition_dialog.hpp"

#include <cairo/cairo.h>

#include "../app.hpp"
#include "../block.hpp"
#include "../drive.hpp"
#include "../manager.hpp"

namespace eh::disks {

static const char* kFsTypes[] = {
  "ext4", "btrfs", "xfs", "ntfs", "vfat", "f2fs",
};

void draw_create_partition_dialog(AppState& app, cairo_t* cr) {
  auto& dlg = app.createPartDlg;
  if (!dlg.open) return;

  int w = app.width, h = app.height;
  int dw = kCreatePartDlgW, dh = kCreatePartDlgH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  set_rgba(cr, 0, 0, 0, 0.45);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  set_rgb(cr, 0.18, 0.18, 0.20);
  draw_rounded_rect(cr, dx, dy, dw, dh, 10);
  cairo_fill(cr);

  set_rgb(cr, 0.30, 0.32, 0.34);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, dx, dy, dw, dh, 10);
  cairo_stroke(cr);

  // Title
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15);
  set_rgb(cr, app.textR, app.textG, app.textB);
  cairo_move_to(cr, dx + 16, dy + 26);

  static const char* kTitles[] = {
    "Create Partition — Size",
    "Create Partition — Filesystem",
    "Create Partition — Confirm",
  };
  cairo_show_text(cr, kTitles[dlg.page]);

  set_rgb(cr, 0.25, 0.25, 0.27);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, dx + 12, dy + 36);
  cairo_line_to(cr, dx + dw - 12, dy + 36);
  cairo_stroke(cr);

  // Page indicator
  {
    cairo_set_font_size(cr, 10);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    for (int p = 0; p < 3; ++p) {
      int px = dx + dw / 2 - 20 + p * 16;
      bool active = p == dlg.page;
      set_rgba(cr, app.accentR, app.accentG, app.accentB, active ? 0.90 : 0.30);
      cairo_arc(cr, px, dy + 50, 5, 0, 2 * M_PI);
      cairo_fill(cr);
    }
  }

  int content_y = dy + 64;

  // Page 0: Size.
  if (dlg.page == 0) {
    cairo_set_font_size(cr, 11);
    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_move_to(cr, dx + 16, content_y + 14);
    cairo_show_text(cr, "Partition Size:");

    auto drives = Manager::instance().get_drives();
    uint64_t drive_size = 0;
    if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
      drive_size = drives[app.selected_drive]->get_size();
    }

    uint64_t part_size = drive_size * dlg.size_pct / 100;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, dx + 16, content_y + 34);
    std::string size_label = size_str(part_size) + " (" + std::to_string(dlg.size_pct) + "%)";
    cairo_show_text(cr, size_label.c_str());

    // Percentage selector buttons
    int pct_vals[] = {10, 25, 50, 75, 100};
    int btn_w = 52;
    int py = content_y + 48;
    for (int i = 0; i < 5; ++i) {
      int px = dx + 16 + i * (btn_w + 6);
      bool sel = dlg.size_pct == pct_vals[i];
      bool hov = i == dlg.hover_pct;
      if (sel || hov) {
        set_rgba(cr, app.accentR, app.accentG, app.accentB, sel ? 0.25 : 0.10);
        draw_rounded_rect(cr, px, py, btn_w, 28, 5);
        cairo_fill(cr);
      }
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_set_font_size(cr, 11);
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                              sel ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_move_to(cr, px + 12, py + 19);
      cairo_show_text(cr, (std::to_string(pct_vals[i]) + "%").c_str());
    }
  }

  // Page 1: Filesystem.
  if (dlg.page == 1) {
    int ly = content_y + 8;
    cairo_set_font_size(cr, 11);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_move_to(cr, dx + 16, ly);
    cairo_show_text(cr, "Filesystem:");
    ly += 20;

    int fs_item_h = 28;
    for (int i = 0; i < 6; ++i) {
      int ix = dx + 16, iy = ly + i * fs_item_h;
      bool sel = i == dlg.selected_fs;
      bool hov = i == dlg.hover_item;

      if (hov || sel) {
        set_rgba(cr, app.accentR, app.accentG, app.accentB, sel ? 0.20 : 0.06);
        draw_rounded_rect(cr, ix, iy, dw - 32, fs_item_h, 5);
        cairo_fill(cr);
      }

      cairo_set_font_size(cr, 12);
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_move_to(cr, ix + 10, iy + 19);
      cairo_show_text(cr, kFsTypes[i]);

      if (sel) {
        set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.80);
        cairo_set_line_width(cr, 2);
        cairo_arc(cr, dx + dw - 24, iy + fs_item_h / 2, 5, 0, 2 * M_PI);
        cairo_fill(cr);
      }
    }
  }

  // Page 2: Confirm.
  if (dlg.page == 2) {
    int ly = content_y + 14;

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);

    auto drives = Manager::instance().get_drives();
    uint64_t drive_size = 0;
    if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
      drive_size = drives[app.selected_drive]->get_size();
    }

    auto draw_row = [&](const char* label, const std::string& val) {
      set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
      cairo_move_to(cr, dx + 16, ly);
      cairo_show_text(cr, label);
      cairo_text_extents_t te;
      cairo_text_extents(cr, label, &te);
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_move_to(cr, dx + 16 + te.width + 8, ly);
      cairo_show_text(cr, val.c_str());
      ly += 22;
    };

    draw_row("Filesystem:", kFsTypes[dlg.selected_fs]);
    draw_row("Size:", size_str(drive_size * dlg.size_pct / 100));
    draw_row("Name:", dlg.name.empty() ? "(none)" : dlg.name.c_str());
    draw_row("Encrypt:", dlg.encrypt ? "Yes (LUKS)" : "No");

    // Encryption toggle
    ly += 8;
    {
      int tx = dx + 16, ty = ly;
      bool eh = dlg.hover_encrypt;
      if (eh) {
        set_rgba(cr, 1, 1, 1, 0.06);
        draw_rounded_rect(cr, tx, ty - 2, dw - 32, 24, 5);
        cairo_fill(cr);
      }
      cairo_set_font_size(cr, 12);
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_move_to(cr, tx + 4, ty + 16);
      cairo_show_text(cr, "Encrypt (LUKS)");

      int cb_x = dx + dw - 44;
      set_rgb(cr, 0.25, 0.25, 0.27);
      draw_rounded_rect(cr, cb_x, ty, 20, 20, 4);
      cairo_fill(cr);
      if (dlg.encrypt) {
        set_rgb(cr, app.accentR, app.accentG, app.accentB);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, cb_x + 4, ty + 10);
        cairo_line_to(cr, cb_x + 9, ty + 16);
        cairo_line_to(cr, cb_x + 16, ty + 4);
        cairo_stroke(cr);
      }
    }
  }

  // Buttons.
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  // Cancel (always shown)
  {
    int bx = dx + dw - 16 - btn_w * 2 - 8;
    set_rgba(cr, 0, 0, 0, dlg.hover_cancel ? 0.30 : 0.20);
    draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx + 28, btn_y + 22);
    cairo_show_text(cr, "Cancel");
  }

  if (dlg.page > 0) {
    int bx = dx + dw - 16 - btn_w;
    set_rgba(cr, 1, 1, 1, dlg.hover_back ? 0.12 : 0.06);
    draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx + 32, btn_y + 22);
    cairo_show_text(cr, "Back");
  }

  if (dlg.page < 2) {
    int bx = dx + dw - 16;
    set_rgba(cr, app.accentR, app.accentG, app.accentB, dlg.hover_next ? 0.50 : 0.30);
    draw_rounded_rect(cr, bx - btn_w, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx - btn_w + 32, btn_y + 22);
    cairo_show_text(cr, "Next");
  } else {
    int bx = dx + dw - 16;
    set_rgb(cr, dlg.hover_create ? 0.45 : 0.30, 0.55, 0.35);
    draw_rounded_rect(cr, bx - btn_w, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, 0.80, 0.95, 0.80);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx - btn_w + 20, btn_y + 22);
    cairo_show_text(cr, "Create");
  }
}

bool handle_create_partition_dialog_click(AppState& app, int x, int y) {
  auto& dlg = app.createPartDlg;
  if (!dlg.open) return false;

  int w = app.width, h = app.height;
  int dw = kCreatePartDlgW, dh = kCreatePartDlgH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  if (x < dx || x > dx + dw || y < dy || y > dy + dh) {
    dlg.open = false;
    schedule_frame(app);
    return true;
  }

  // Page 0: Size buttons.
  if (dlg.page == 0) {
    int pct_vals[] = {10, 25, 50, 75, 100};
    int btn_w = 52;
    int py = dy + 64 + 48;
    for (int i = 0; i < 5; ++i) {
      int px = dx + 16 + i * (btn_w + 6);
      if (x >= px && x < px + btn_w && y >= py && y < py + 28) {
        dlg.size_pct = pct_vals[i];
        schedule_frame(app);
        return true;
      }
    }
  }

  // Page 1: Filesystem selection.
  if (dlg.page == 1) {
    int ly = dy + 64 + 8 + 20;
    int fs_item_h = 28;
    for (int i = 0; i < 6; ++i) {
      int ix = dx + 16, iy = ly + i * fs_item_h;
      if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + fs_item_h) {
        dlg.selected_fs = i;
        schedule_frame(app);
        return true;
      }
    }
  }

  // Page 2: Encrypt toggle.
  if (dlg.page == 2) {
    int ly = dy + 64 + 14 + 22 * 4 + 8;
    int tx = dx + 16, ty = ly;
    if (x >= tx && x < tx + dw - 32 && y >= ty && y < ty + 24) {
      dlg.encrypt = !dlg.encrypt;
      schedule_frame(app);
      return true;
    }
  }

  // Buttons.
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  // Cancel (leftmost)
  {
    int bx = dx + dw - 16 - btn_w * 2 - 8;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.open = false;
      schedule_frame(app);
      return true;
    }
  }

  // Back (previous page)
  if (dlg.page > 0) {
    int bx = dx + dw - 16 - btn_w;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.page--;
      schedule_frame(app);
      return true;
    }
  }

  // Next / Create
  if (dlg.page < 2) {
    int bx = dx + dw - 16 - btn_w;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.page++;
      schedule_frame(app);
      return true;
    }
  } else {
    int bx = dx + dw - 16 - btn_w;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      if (!dlg.pending) {
        dlg.pending = true;
        auto drives = Manager::instance().get_drives();
        if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
          auto& drive = drives[app.selected_drive];
          auto all_blocks = drive->blocks();

          // Find the disk block (no Partition interface) and calculate free space offset
          std::shared_ptr<Block> disk_block;
          uint64_t offset = 0;
          for (auto& b : all_blocks) {
            if (!b->has_partition()) {
              disk_block = b;
            } else {
              uint64_t end = b->get_partition_offset() + b->get_size();
              if (end > offset) offset = end;
            }
          }

          if (disk_block) {
            uint64_t part_size = drive->get_size() * dlg.size_pct / 100;
            std::string type_guid = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
            disk_block->create_partition_async(offset, part_size, type_guid,
              dlg.name, [&app](bool ok) {
                app.createPartDlg.pending = false;
                app.createPartDlg.result_ok = ok;
                app.createPartDlg.open = false;
                schedule_frame(app);
              });
          } else {
            dlg.pending = false;
          }
        }
      }
      schedule_frame(app);
      return true;
    }
  }

  return true;
}

void handle_create_partition_dialog_move(AppState& app, int x, int y) {
  auto& dlg = app.createPartDlg;
  if (!dlg.open) return;

  dlg.hover_item = -1;
  dlg.hover_cancel = false;
  dlg.hover_next = false;
  dlg.hover_back = false;
  dlg.hover_create = false;
  dlg.hover_encrypt = false;
  dlg.hover_pct = -1;

  int w = app.width, h = app.height;
  int dw = kCreatePartDlgW, dh = kCreatePartDlgH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  // Page 0: pct buttons
  if (dlg.page == 0) {
    int btn_w = 52;
    int py = dy + 64 + 48;
    for (int i = 0; i < 5; ++i) {
      int px = dx + 16 + i * (btn_w + 6);
      if (x >= px && x < px + btn_w && y >= py && y < py + 28) {
        dlg.hover_pct = i;
        return;
      }
    }
  }

  // Page 1: fs items
  if (dlg.page == 1) {
    int ly = dy + 64 + 8 + 20;
    int fs_item_h = 28;
    for (int i = 0; i < 6; ++i) {
      int ix = dx + 16, iy = ly + i * fs_item_h;
      if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + fs_item_h) {
        dlg.hover_item = i;
        return;
      }
    }
  }

  // Page 2: encrypt toggle
  if (dlg.page == 2) {
    int ly = dy + 64 + 14 + 22 * 4 + 8;
    int tx = dx + 16, ty = ly;
    if (x >= tx && x < tx + dw - 32 && y >= ty && y < ty + 24) {
      dlg.hover_encrypt = true;
      return;
    }
  }

  // Buttons
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  int bx_c = dx + dw - 16 - btn_w * 2 - 8;
  if (x >= bx_c && x < bx_c + btn_w && y >= btn_y && y < btn_y + btn_h) {
    dlg.hover_cancel = true;
    return;
  }

  if (dlg.page > 0) {
    int bx_b = dx + dw - 16 - btn_w;
    if (x >= bx_b && x < bx_b + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.hover_back = true;
      return;
    }
  }

  if (dlg.page < 2) {
    int bx_n = dx + dw - 16 - btn_w;
    if (x >= bx_n && x < bx_n + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.hover_next = true;
      return;
    }
  } else {
    int bx_cr = dx + dw - 16 - btn_w;
    if (x >= bx_cr && x < bx_cr + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.hover_create = true;
      return;
    }
  }
}

}

