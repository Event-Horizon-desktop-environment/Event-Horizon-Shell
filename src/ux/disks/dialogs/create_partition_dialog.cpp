#include "ux/disks/dialogs/create_partition_dialog.hpp"

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include "ux/disks/app.hpp"
#include "ux/disks/block.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/jobs.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/ui/layout.hpp"
#include "ux/disks/ui/theme.hpp"
#include "ux/disks/ui/widgets.hpp"

namespace eh::disks {
namespace W = widgets;
namespace T = theme;
namespace L = layout;

static W::RGB txt(const AppState& a) { return {a.textR, a.textG, a.textB}; }
static W::RGB sec(const AppState& a) { return {a.textSecR, a.textSecG, a.textSecB}; }

static const char* kFs[] = {"ext4", "btrfs", "xfs",  "ntfs",  "vfat", "exfat",
                            "f2fs", "swap",  "clear", "empty"};
static constexpr int kFsN = 10;

static L::Rect dlg_rect(const AppState& app) {
  return L::dialog_rect(app, kCreatePartDlgW, kCreatePartDlgH);
}

void draw_create_partition_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.createPartDlg;
  if (!d.open) return;
  auto r = dlg_rect(app);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Create Partition", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  W::text(cr, ("Free space: " + size_str(d.free_size)).c_str(), r.x + 16, y, 11, sec(app), 1);
  y += 16;
  // Size slider
  {
    W::text(cr, ("Size: " + size_str(d.size)).c_str(), r.x + 16, y + 4, 13, txt(app), 1);
    double tx = r.x + 16, tw = r.w - 32 - 90;
    double ty = y + 12;
    W::fill_rounded(cr, tx, ty, tw, 8, 4, {1, 1, 1, 0.10});
    double f = d.free_size ? double(d.size) / double(d.free_size) : 0;
    W::fill_rounded(cr, tx, ty, tw * f, 8, 4, {app.accentR, app.accentG, app.accentB, 0.85});
    double kx = tx + tw * f;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, kx, ty + 4, 8, 0, 2 * M_PI);
    cairo_fill(cr);
    // -/+ steppers
    W::pill_button(cr, app, tx + tw + 8, ty - 9, 36, 26, "−", false, false, false, false,
                   false);
    W::pill_button(cr, app, tx + tw + 48, ty - 9, 36, 26, "+", false, false, false, false,
                   false);
  }
  y += 40;
  W::text(cr, "Create as", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  const char* kinds[3] = {"Primary", "Extended", "Logical"};
  for (int i = 0; i < 3; ++i) {
    double ix = r.x + 16 + i * 140;
    bool hov = d.hover_create_as == i;
    if (hov) W::fill_rounded(cr, ix - 4, y - 4, 132, 26, 7, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, ix + 8, y + 8, 7, d.create_as == i);
    W::text(cr, kinds[i], ix + 24, y + 12, 12, txt(app), 0);
  }
  y += 30;
  W::text(cr, "Partition name (GPT only)", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  {
    auto& f = d.name;
    W::fill_rounded(cr, r.x + 16, y, r.w - 32, T::kFieldH, 8, {1, 1, 1, 0.06});
    W::stroke_rounded(cr, r.x + 16.5, y + 0.5, r.w - 33, T::kFieldH - 1, 8,
                      {app.outlineR, app.outlineG, app.outlineB});
    W::text_ellipsis(cr, f.text, r.x + 26, y + 22, 13, txt(app), 0, r.w - 52);
  }
  y += T::kFieldH + 10;
  W::text(cr, "Filesystem", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = y + rowi * 24;
    bool hov = d.hover_fs == i;
    if (hov) W::fill_rounded(cr, ix - 4, iy - 3, (r.w - 32) / 2 - 4, 22, 6, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, ix + 8, iy + 8, 7, d.fs_idx == i);
    W::text(cr, kFs[i], ix + 24, iy + 12, 12, txt(app), 0);
  }
  y += 5 * 24 + 6;
  {
    if (d.hover_encrypt)
      W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 26, 7, {1, 1, 1, 0.05});
    W::checkbox(cr, app, r.x + 16, y, 18, d.encrypt, d.hover_encrypt);
    W::text(cr, "Encrypt with LUKS (format step)", r.x + 42, y + 14, 12, txt(app), 0);
  }
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Create", d.hover_create,
                   false, true, false, false);
  }
}

static L::Rect slider_rect(const AppState& app) {
  auto r = dlg_rect(app);
  double tx = r.x + 16, tw = r.w - 32 - 90;
  double ty = r.y + 58 + 16 + 12 + 12;
  return {int(tx), int(ty), int(tw), 8};
}

bool create_partition_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.createPartDlg;
  if (!d.open) return false;
  auto r = dlg_rect(app);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  // Slider track + knob.
  auto sl = slider_rect(app);
  if (L::Rect{sl.x - 8, sl.y - 10, sl.w + 16, 28}.contains(x, y)) {
    double f = double(x - sl.x) / double(std::max(1, sl.w));
    f = std::clamp(f, 0.0, 1.0);
    d.size = uint64_t(f * double(d.free_size));
    if (d.size < 1024 * 1024) d.size = 1024 * 1024;
    schedule_frame(app);
    return true;
  }
  // Steppers.
  {
    double tx = r.x + 16, tw = r.w - 32 - 90, ty = r.y + 58 + 16 + 12 + 12;
    if (L::Rect{int(tx + tw + 8), int(ty) - 9, 36, 26}.contains(x, y)) {
      if (d.size > 1024 * 1024 * 1024) d.size -= 1024 * 1024 * 1024;
      schedule_frame(app);
      return true;
    }
    if (L::Rect{int(tx + tw + 48), int(ty) - 9, 36, 26}.contains(x, y)) {
      d.size = std::min(d.free_size, d.size + 1024 * 1024 * 1024);
      schedule_frame(app);
      return true;
    }
  }
  double ky = r.y + 58 + 16 + 40 + 6 + 11;
  for (int i = 0; i < 3; ++i) {
    double ix = r.x + 16 + i * 140;
    if (L::Rect{int(ix) - 4, int(ky) - 4, 132, 26}.contains(x, y)) {
      d.create_as = i;
      schedule_frame(app);
      return true;
    }
  }
  double ny = ky + 30 + 6 + 11;
  if (L::Rect{r.x + 16, int(ny), r.w - 32, T::kFieldH}.contains(x, y)) {
    d.focus = 1;
    schedule_frame(app);
    return true;
  }
  double ty2 = ny + T::kFieldH + 10 + 6 + 11;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = ty2 + rowi * 24;
    if (L::Rect{int(ix) - 4, int(iy) - 3, (r.w - 32) / 2 - 4, 22}.contains(x, y)) {
      d.fs_idx = i;
      schedule_frame(app);
      return true;
    }
  }
  double ency = ty2 + 5 * 24 + 6 - 4;
  if (L::Rect{r.x + 8, int(ency), r.w - 16, 26}.contains(x, y)) {
    d.encrypt = !d.encrypt;
    schedule_frame(app);
    return true;
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    d.error.clear();
    if (d.size == 0 || d.free_size == 0) {
      d.error = "No free space selected.";
      schedule_frame(app);
      return true;
    }
    auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
    if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return true;
    auto drive = drives[size_t(app.selected_drive)];
    std::shared_ptr<Block> whole;
    for (auto& b : drive->blocks())
      if (!b->has_partition()) {
        whole = b;
        break;
      }
    if (!whole) {
      d.error = "Partition table block not found.";
      schedule_frame(app);
      return true;
    }
    d.pending = true;
    auto job = JobTracker::instance().start("Creating partition…", "", false);
    std::string guid = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
    whole->create_partition_async(d.free_offset, d.size, guid, d.name.text,
                                  [&app, job](bool ok) {
                                    JobTracker::instance().finish(
                                        job->id, ok, ok ? "Partition created" : "Create failed");
                                    app.createPartDlg->pending = false;
                                    app.createPartDlg->open = false;
                                    toast(app, ok ? "Partition created — format it from the list"
                                                  : "Create failed",
                                          !ok);
                                    schedule_frame(app);
                                  });
    schedule_frame(app);
    return true;
  }
  return true;
}

void create_partition_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.createPartDlg;
  if (!d.open) return;
  d.hover_create_as = -1;
  d.hover_fs = -1;
  d.hover_encrypt = d.hover_cancel = d.hover_create = false;
  auto r = dlg_rect(app);
  double ky = r.y + 58 + 16 + 40 + 6 + 11;
  for (int i = 0; i < 3; ++i) {
    double ix = r.x + 16 + i * 140;
    if (L::Rect{int(ix) - 4, int(ky) - 4, 132, 26}.contains(x, y)) d.hover_create_as = i;
  }
  double ny = ky + 30 + 6 + 11;
  double ty2 = ny + T::kFieldH + 10 + 6 + 11;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = ty2 + rowi * 24;
    if (L::Rect{int(ix) - 4, int(iy) - 3, (r.w - 32) / 2 - 4, 22}.contains(x, y)) d.hover_fs = i;
  }
  double ency = ty2 + 5 * 24 + 6 - 4;
  if (L::Rect{r.x + 8, int(ency), r.w - 16, 26}.contains(x, y)) d.hover_encrypt = true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_create = true;
}

bool create_partition_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.createPartDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    d.name.backspace();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Left) {
    d.size = d.size > 256 * 1024 * 1024 ? d.size - 256 * 1024 * 1024 : d.size;
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Right) {
    d.size = std::min(d.free_size, d.size + 256 * 1024 * 1024);
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    d.name.insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

}  // namespace eh::disks
