#include "format_disk_dialog.hpp"

#include <cairo/cairo.h>

#include "../app.hpp"
#include "../drive.hpp"
#include "../manager.hpp"

namespace eh::disks {

static const char* kSchemes[] = {
  "GPT", "DOS (MBR)",
};

static const char* kSchemeDesc[] = {
  "GUID Partition Table — modern, UEFI",
  "Master Boot Record — legacy, BIOS",
};

void draw_format_disk_dialog(AppState& app, cairo_t* cr) {
  auto& dlg = app.fmtDiskDlg;
  if (!dlg.open) return;

  int w = app.width, h = app.height;
  int dw = kFormatDiskDialogW, dh = kFormatDiskDialogH;
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
  cairo_show_text(cr, "Format Disk");

  set_rgb(cr, 0.25, 0.25, 0.27);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, dx + 12, dy + 36);
  cairo_line_to(cr, dx + dw - 12, dy + 36);
  cairo_stroke(cr);

  // Scheme list
  int ly = dy + 52;
  cairo_set_font_size(cr, 11);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
  cairo_move_to(cr, dx + 16, ly);
  cairo_show_text(cr, "Partition Table:");
  ly += 20;

  int item_h = 44;
  for (int i = 0; i < 2; ++i) {
    int ix = dx + 16, iy = ly + i * item_h;
    bool sel = i == dlg.selected_scheme;
    bool hov = i == dlg.hover_item;

    if (hov || sel) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, sel ? 0.20 : 0.08);
      draw_rounded_rect(cr, ix, iy, dw - 32, item_h, 6);
      cairo_fill(cr);
    }

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, ix + 12, iy + 18);
    cairo_show_text(cr, kSchemes[i]);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_move_to(cr, ix + 12, iy + 36);
    cairo_show_text(cr, kSchemeDesc[i]);
  }

  // Warning
  {
    int wy = dy + 52 + 2 * item_h + 12;
    cairo_set_font_size(cr, 10);
    set_rgba(cr, 0.95, 0.70, 0.30, 0.80);
    cairo_move_to(cr, dx + 16, wy);
    cairo_show_text(cr, "This will erase all data on the disk.");
  }

  // Buttons
  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

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

  {
    int bx = dx + dw - 16 - btn_w;
    set_rgb(cr, dlg.hover_format ? 0.55 : 0.40, 0.10, 0.10);
    draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 6);
    cairo_fill(cr);
    set_rgb(cr, 0.95, 0.70, 0.70);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, bx + 20, btn_y + 22);
    cairo_show_text(cr, "Format");
  }
}

bool handle_format_disk_dialog_click(AppState& app, int x, int y) {
  auto& dlg = app.fmtDiskDlg;
  if (!dlg.open) return false;

  int w = app.width, h = app.height;
  int dw = kFormatDiskDialogW, dh = kFormatDiskDialogH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  if (x < dx || x > dx + dw || y < dy || y > dy + dh) {
    dlg.open = false;
    schedule_frame(app);
    return true;
  }

  int item_h = 44;
  int ly = dy + 52 + 20;
  for (int i = 0; i < 2; ++i) {
    int ix = dx + 16, iy = ly + i * item_h;
    if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + item_h) {
      dlg.selected_scheme = i;
      schedule_frame(app);
      return true;
    }
  }

  int btn_w = 90, btn_h = 32;
  int btn_y = dy + dh - 16 - btn_h;

  {
    int bx = dx + dw - 16 - btn_w * 2 - 8;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      dlg.open = false;
      schedule_frame(app);
      return true;
    }
  }

  {
    int bx = dx + dw - 16 - btn_w;
    if (x >= bx && x < bx + btn_w && y >= btn_y && y < btn_y + btn_h) {
      if (!dlg.pending) {
        dlg.pending = true;
        auto drives = Manager::instance().get_drives();
        if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
          auto& drive = drives[app.selected_drive];
          auto blocks = drive->blocks();
          if (!blocks.empty()) {
            auto block = blocks[0];
            std::string scheme = dlg.selected_scheme == 0 ? "gpt" : "dos";
            block->create_partition_table_async(scheme,
              [&app](bool ok) {
                app.fmtDiskDlg.pending = false;
                app.fmtDiskDlg.result_ok = ok;
                app.fmtDiskDlg.open = false;
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

void handle_format_disk_dialog_move(AppState& app, int x, int y) {
  auto& dlg = app.fmtDiskDlg;
  if (!dlg.open) return;

  dlg.hover_item = -1;
  dlg.hover_cancel = false;
  dlg.hover_format = false;

  int w = app.width, h = app.height;
  int dw = kFormatDiskDialogW, dh = kFormatDiskDialogH;
  int dx = (w - dw) / 2, dy = (h - dh) / 2;

  int item_h = 44;
  int ly = dy + 52 + 20;
  for (int i = 0; i < 2; ++i) {
    int ix = dx + 16, iy = ly + i * item_h;
    if (x >= ix && x < ix + dw - 32 && y >= iy && y < iy + item_h) {
      dlg.hover_item = i;
      return;
    }
  }

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

