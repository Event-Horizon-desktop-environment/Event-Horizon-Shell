#include "format_volume_dialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <cairo/cairo.h>

#include "../app.hpp"
#include "../block.hpp"
#include "../manager.hpp"

namespace eh::disks {

static const char* kFsTypes[] = {
  "ext4", "btrfs", "xfs", "ntfs", "vfat", "f2fs",
};

void draw_format_volume_dialog(AppState& app, cairo_t* cr, int block_idx) {
  auto& dlg = app.fmtDlg;
  if (!dlg.open) return;

  auto drives = Manager::instance().get_drives();
  if (app.selected_drive < 0 || app.selected_drive >= static_cast<int>(drives.size()))
    return;
  auto blocks = filter_blocks(drives[app.selected_drive]->blocks());
  if (block_idx < 0 || block_idx >= static_cast<int>(blocks.size())) return;

  int w = app.width, h = app.height;
  int dw = kFormatDialogW, dh = kFormatDialogH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  // Dim background
  set_rgba(cr, 0, 0, 0, 0.45);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  // Dialog surface
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
  cairo_show_text(cr, "Format Volume");

  // Divider
  set_rgb(cr, 0.25, 0.25, 0.27);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, dx + 12, dy + 36);
  cairo_line_to(cr, dx + dw - 12, dy + 36);
  cairo_stroke(cr);

  // Filesystem list header
  int ly = dy + 52;
  cairo_set_font_size(cr, 11);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
  cairo_move_to(cr, dx + 16, ly);
  cairo_show_text(cr, "Filesystem:");
  ly += 18;

  int fs_item_h = 28;
  for (int i = 0; i < 6; ++i) {
    int ix = dx + 16, iy = ly + i * fs_item_h;
    bool sel = i == dlg.selected_fs;
    bool hov = i == dlg.hover_item;

    if (hov) {
      set_rgba(cr, 1, 1, 1, 0.06);
      draw_rounded_rect(cr, ix, iy, dw - 32, fs_item_h, 5);
      cairo_fill(cr);
    }

    if (sel) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.20);
      draw_rounded_rect(cr, ix, iy, dw - 32, fs_item_h, 5);
      cairo_fill(cr);
    }

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
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

  int list_h = 6 * fs_item_h;
  ly += list_h + 8;

  // Encryption toggle
  {
    int tx = dx + 16, ty = ly;
    bool eh = dlg.hover_encrypt;

    if (eh) {
      set_rgba(cr, 1, 1, 1, 0.06);
      draw_rounded_rect(cr, tx, ty - 2, dw - 32, 24, 5);
      cairo_fill(cr);
    }

    cairo_set_font_size(cr, 12);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, tx + 4, ty + 16);
    cairo_show_text(cr, "Encrypt (LUKS)");

    // Checkbox
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
    ly += 30;
  }

  // Buttons
  ly += 8;
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  // Cancel
  {
    int bx = dx + dw - 16 - btn_w * 2 - 8;
    bool bh = dlg.hover_cancel;
    if (bh) {
      set_rgba(cr, 1, 1, 1, 0.08);
    } else {
      set_rgba(cr, 0, 0, 0, 0.20);
    }
    draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx + 28, btn_y + 22);
    cairo_show_text(cr, "Cancel");
  }

  // Format
  {
    int bx = dx + dw - 16 - btn_w;
    bool bh = dlg.hover_format;
    if (bh) {
      set_rgb(cr, 0.55, 0.15, 0.15);
    } else {
      set_rgb(cr, 0.40, 0.10, 0.10);
    }
    draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, 0.95, 0.70, 0.70);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx + 20, btn_y + 22);
    cairo_show_text(cr, "Format");
  }
}

bool handle_format_dialog_click(AppState& app, int x, int y, int block_idx) {
  auto& dlg = app.fmtDlg;
  if (!dlg.open) return false;

  int w = app.width, h = app.height;
  int dw = kFormatDialogW, dh = kFormatDialogH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  // Click outside dialog → close
  if (x < dx || x > dx + dw || y < dy || y > dy + dh) {
    dlg.open = false;
    schedule_frame(app);
    return true;
  }

  // Filesystem list
  int fs_item_h = 28;
  int ly = dy + 52 + 18;
  for (int i = 0; i < 6; ++i) {
    int ix = dx + 16, iy = ly + i * fs_item_h;
    if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + fs_item_h) {
      dlg.selected_fs = i;
      schedule_frame(app);
      return true;
    }
  }

  int list_h = 6 * fs_item_h;
  int toggle_y = dy + 52 + 18 + list_h + 8;

  // Encryption toggle
  {
    int tx = dx + 16, ty = toggle_y;
    if (x >= tx && x < tx + dw - 32 && y >= ty && y < ty + 24) {
      dlg.encrypt = !dlg.encrypt;
      schedule_frame(app);
      return true;
    }
  }

  // Buttons
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  // Cancel
  {
    int bx = dx + dw - 16 - btn_w * 2 - 8;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.open = false;
      schedule_frame(app);
      return true;
    }
  }

  // Format
  {
    int bx = dx + dw - 16 - btn_w;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      if (!dlg.pending) {
        dlg.pending = true;
        auto drives = Manager::instance().get_drives();
        if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
          auto blocks = filter_blocks(drives[app.selected_drive]->blocks());
          if (block_idx >= 0 && block_idx < static_cast<int>(blocks.size())) {
            auto block = blocks[block_idx];
            std::string passphrase = dlg.encrypt ? "luks" : "";
            block->format_async(kFsTypes[dlg.selected_fs], "", passphrase,
              [&app](bool ok) {
                app.fmtDlg.pending = false;
                app.fmtDlg.result_ok = ok;
                app.fmtDlg.result_msg = ok ? "Format complete" : "Format failed";
                schedule_frame(app);
              });
          }
        }
      }
      schedule_frame(app);
      return true;
    }
  }

  return true;
}

void handle_format_dialog_move(AppState& app, int x, int y) {
  auto& dlg = app.fmtDlg;
  if (!dlg.open) return;

  dlg.hover_item = -1;
  dlg.hover_cancel = false;
  dlg.hover_format = false;
  dlg.hover_encrypt = false;

  int w = app.width, h = app.height;
  int dw = kFormatDialogW, dh = kFormatDialogH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  // Filesystem items
  int fs_item_h = 28;
  int ly = dy + 52 + 18;
  for (int i = 0; i < 6; ++i) {
    int ix = dx + 16, iy = ly + i * fs_item_h;
    if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + fs_item_h) {
      dlg.hover_item = i;
      return;
    }
  }

  int list_h = 6 * fs_item_h;
  int toggle_y = dy + 52 + 18 + list_h + 8;

  // Encryption
  {
    int tx = dx + 16, ty = toggle_y;
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

  int bx_f = dx + dw - 16 - btn_w;
  if (x >= bx_f && x < bx_f + btn_w && y >= btn_y && y < btn_y + btn_h) {
    dlg.hover_format = true;
    return;
  }
}

}

