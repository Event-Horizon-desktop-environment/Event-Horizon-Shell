#include "ux/disks/dialogs/format_disk_dialog.hpp"

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include "ux/disks/app.hpp"
#include "ux/disks/block.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/jobs.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/ui/icons.hpp"
#include "ux/disks/ui/layout.hpp"
#include "ux/disks/ui/theme.hpp"
#include "ux/disks/ui/widgets.hpp"

namespace eh::disks {
namespace W = widgets;
namespace T = theme;
namespace L = layout;

static W::RGB txt(const AppState& a) { return {a.textR, a.textG, a.textB}; }
static W::RGB sec(const AppState& a) { return {a.textSecR, a.textSecG, a.textSecB}; }

static L::Rect dlg_rect(const AppState& app) {
  return L::dialog_rect(app, kFormatDiskDlgW, kFormatDiskDlgH);
}

void draw_format_disk_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.fmtDiskDlg;
  if (!d.open) return;
  auto r = dlg_rect(app);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Format Disk", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});

  double y = r.y + 62;
  W::text(cr, "Erase", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  const char* erase[2] = {"Don't overwrite existing data (quick)", "Overwrite with zeros (slow)"};
  for (int i = 0; i < 2; ++i) {
    double iy = y + i * 30;
    bool hov = d.hover_radio_erase == i;
    if (hov) W::fill_rounded(cr, r.x + 8, iy - 2, r.w - 16, 28, 7, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, r.x + 24, iy + 12, 7, d.erase_mode == i);
    W::text(cr, erase[i], r.x + 40, iy + 16, 12, txt(app), 0);
  }
  y += 64;
  W::text(cr, "Partitioning", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  const char* sch[2] = {"GPT — modern systems, UEFI", "MBR / DOS — legacy BIOS"};
  for (int i = 0; i < 2; ++i) {
    double iy = y + i * 30;
    bool hov = d.hover_scheme == i;
    if (hov) W::fill_rounded(cr, r.x + 8, iy - 2, r.w - 16, 28, 7, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, r.x + 24, iy + 12, 7, d.scheme == i);
    W::text(cr, i == 0 ? "GUID Partition Table (GPT)" : "Master Boot Record (DOS)", r.x + 40,
            iy + 16, 12, txt(app), 1);
    W::text(cr, sch[i], r.x + 40, iy + 16, 11, sec(app), 0);  // right side appended below
    // Draw desc right-aligned to avoid overlap: measure title first.
    double tw = W::text_w(cr, i == 0 ? "GUID Partition Table (GPT)" : "Master Boot Record (DOS)", 12, 1);
    W::text(cr, sch[i], r.x + 40 + tw + 8, iy + 16, 11, sec(app), 0);
  }
  y += 64;
  // Warning
  W::fill_rounded(cr, r.x + 16, y, r.w - 32, 40, 8, {0.85, 0.45, 0.20, 0.12});
  if (app.svg.warn)
    icons::paint_svg(cr, app.svg.warn, r.x + 28, y + 12, 16, {0.95, 0.70, 0.40});
  else
    W::text(cr, "⚠", r.x + 28, y + 25, 12, {0.95, 0.70, 0.40}, 1);
  W::text(cr, "All data on the disk will be lost.", r.x + 50, y + 25, 12,
          {0.95, 0.70, 0.40}, 1);
  y += 52;
  // Confirm
  {
    bool hov = d.hover_confirm;
    if (hov) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 28, 7, {1, 1, 1, 0.05});
    W::checkbox(cr, app, r.x + 16, y, 18, d.confirm, hov);
    W::text(cr, "I understand, format this disk", r.x + 42, y + 14, 12, txt(app), 0);
  }
  // Buttons
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
    W::text(cr, "Working…", r.x + r.w / 2 + 16, by + 22, 12, sec(app), 0);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Format", d.hover_format,
                   false, false, true, !d.confirm);
  }
}

bool format_disk_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.fmtDiskDlg;
  if (!d.open) return false;
  auto r = dlg_rect(app);
  L::Rect rr{r.x, r.y, r.w, r.h};
  if (!rr.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double ey = r.y + 68;
  for (int i = 0; i < 2; ++i) {
    L::Rect row{r.x + 8, int(ey + i * 30 - 2), r.w - 16, 28};
    if (row.contains(x, y)) {
      d.erase_mode = i;
      schedule_frame(app);
      return true;
    }
  }
  double sy = r.y + 68 + 64 + 6;
  for (int i = 0; i < 2; ++i) {
    L::Rect row{r.x + 8, int(sy + i * 30 - 2), r.w - 16, 28};
    if (row.contains(x, y)) {
      d.scheme = i;
      schedule_frame(app);
      return true;
    }
  }
  {
    L::Rect row{r.x + 8, int(r.y + 68 + 64 + 64 + 52 - 4), r.w - 16, 28};
    if (row.contains(x, y)) {
      d.confirm = !d.confirm;
      schedule_frame(app);
      return true;
    }
  }
  double by = r.y + r.h - 16 - 34;
  L::Rect cancel{r.x + r.w - 16 - 200, int(by), 96, 34};
  L::Rect go{r.x + r.w - 16 - 96, int(by), 96, 34};
  if (cancel.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  if (go.contains(x, y)) {
    if (!d.confirm) return true;
    d.pending = true;
    auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
    std::shared_ptr<Drive> drive;
    if (app.selected_drive >= 0 && app.selected_drive < int(drives.size()))
      drive = drives[size_t(app.selected_drive)];
    if (!drive) {
      d.pending = false;
      schedule_frame(app);
      return true;
    }
    // Whole-disk block: first without Partition interface.
    std::shared_ptr<Block> whole;
    for (auto& b : drive->blocks())
      if (!b->has_partition()) {
        whole = b;
        break;
      }
    if (!whole && !drive->blocks().empty()) whole = drive->blocks()[0];
    if (!whole) {
      d.pending = false;
      toast(app, "No block device found", true);
      schedule_frame(app);
      return true;
    }
    std::string scheme = d.scheme == 0 ? "gpt" : "dos";
    auto job = JobTracker::instance().start("Formatting disk (" + scheme + ")…", "", false);
    whole->create_partition_table_async(scheme, [&app, job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Disk formatted" : "Format failed");
      app.fmtDiskDlg->pending = false;
      app.fmtDiskDlg->open = false;
      toast(app, ok ? "Disk formatted" : "Format failed", !ok);
      schedule_frame(app);
    });
    schedule_frame(app);
    return true;
  }
  return true;
}

void format_disk_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.fmtDiskDlg;
  if (!d.open) return;
  d.hover_radio_erase = -1;
  d.hover_scheme = -1;
  d.hover_confirm = false;
  d.hover_cancel = false;
  d.hover_format = false;
  auto r = dlg_rect(app);
  double ey = r.y + 68;
  for (int i = 0; i < 2; ++i) {
    L::Rect row{r.x + 8, int(ey + i * 30 - 2), r.w - 16, 28};
    if (row.contains(x, y)) d.hover_radio_erase = i;
  }
  double sy = r.y + 68 + 64 + 6;
  for (int i = 0; i < 2; ++i) {
    L::Rect row{r.x + 8, int(sy + i * 30 - 2), r.w - 16, 28};
    if (row.contains(x, y)) d.hover_scheme = i;
  }
  {
    L::Rect row{r.x + 8, int(r.y + 68 + 64 + 64 + 52 - 4), r.w - 16, 28};
    if (row.contains(x, y)) d.hover_confirm = true;
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_format = true;
}

bool format_disk_dialog_key(AppState& app, uint32_t sym) {
  auto& d = *app.fmtDiskDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  return false;
}

}  // namespace eh::disks
