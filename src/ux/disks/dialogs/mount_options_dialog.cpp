#include "ux/disks/dialogs/mount_options_dialog.hpp"

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

static void field(AppState& app, cairo_t* cr, double x, double y, double w,
                  const TextField& f) {
  W::fill_rounded(cr, x, y, w, T::kFieldH, 8, {1, 1, 1, 0.06});
  W::stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, T::kFieldH - 1, 8,
                    {app.outlineR, app.outlineG, app.outlineB});
  W::text_ellipsis(cr, f.text, x + 10, y + 22, 13, txt(app), 0, w - 20);
}

void draw_mount_options_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.mountOptsDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kMountOptsDlgW, kMountOptsDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Edit Mount Options", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  auto trow = [&](const char* label, bool on, bool& hov) {
    if (hov) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 28, 7, {1, 1, 1, 0.05});
    W::text(cr, label, r.x + 16, y + 14, 12, txt(app), 0);
    W::toggle(cr, app, r.x + r.w - 16 - 44, y - 2, 44, 24, on, hov);
    y += 30;
  };
  trow("Automatic mount", d.auto_mount, d.hover_auto);
  trow("Mount at system startup", d.at_startup, d.hover_startup);
  trow("Show in user interface", d.show_in_ui, d.hover_show);
  y += 4;
  W::text(cr, "Mount point", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  field(app, cr, r.x + 16, y, r.w - 32, d.mount_point);
  y += T::kFieldH + 10;
  W::text(cr, "Filesystem type", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  field(app, cr, r.x + 16, y, r.w - 32, d.fs_type);
  y += T::kFieldH + 10;
  W::text(cr, "Mount options (comma-separated)", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  field(app, cr, r.x + 16, y, r.w - 32, d.options);
  double by = r.y + r.h - 16 - 34;
  W::pill_button(cr, app, r.x + 16, by, 140, 34, "Restore Defaults", d.hover_defaults,
                 false, false, false, false);
  if (d.pending) {
    W::spinner(cr, r.x + r.w - 120, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Apply", d.hover_apply,
                   false, true, false, false);
  }
}

static std::shared_ptr<Block> target_block(AppState& app, int idx) {
  auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
  if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return nullptr;
  auto blocks = filtered_blocks(drives[size_t(app.selected_drive)], app.search.text);
  if (idx < 0 || idx >= int(blocks.size())) return nullptr;
  return blocks[size_t(idx)];
}

bool mount_options_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.mountOptsDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kMountOptsDlgW, kMountOptsDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double yy0 = r.y + 58 - 4;
  for (int i = 0; i < 3; ++i) {
    if (L::Rect{r.x + 8, int(yy0) + i * 30, r.w - 16, 28}.contains(x, y)) {
      if (i == 0) d.auto_mount = !d.auto_mount;
      if (i == 1) d.at_startup = !d.at_startup;
      if (i == 2) d.show_in_ui = !d.show_in_ui;
      schedule_frame(app);
      return true;
    }
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + 16, int(by), 140, 34}.contains(x, y)) {
    d.auto_mount = true;
    d.at_startup = false;
    d.show_in_ui = true;
    d.mount_point.text.clear();
    d.mount_point.cursor = 0;
    d.fs_type.text = "auto";
    d.fs_type.cursor = 4;
    d.options.text = "defaults";
    d.options.cursor = 8;
    toast(app, "Mount options restored to defaults");
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    auto block = target_block(app, d.block_idx);
    if (!block) return true;
    d.pending = true;
    std::string mp = d.mount_point.text.empty() ? ("/mnt/" + block_short_name(block))
                                                : d.mount_point.text;
    std::string fstype = d.fs_type.text.empty() ? "auto" : d.fs_type.text;
    auto job = JobTracker::instance().start("Saving mount options…",
                                            block->get_object_path(), false);
    block->add_fstab_entry_async(mp, fstype, block->get_uuid(),
                                 [&app, job](bool ok) {
                                   JobTracker::instance().finish(
                                       job->id, ok, ok ? "Saved" : "Save failed");
                                   app.mountOptsDlg->pending = false;
                                   app.mountOptsDlg->open = false;
                                   toast(app, ok ? "Mount options saved" : "Save failed", !ok);
                                   schedule_frame(app);
                                 });
    schedule_frame(app);
    return true;
  }
  // Focus fields (mount_point, fs_type, options)
  double fy = r.y + 58 + 3 * 30 + 4 + 6 + 11; // fields
  if (L::Rect{r.x + 16, int(fy), r.w - 32, T::kFieldH}.contains(x, y)) {
    d.focus = 0;
    schedule_frame(app);
    return true;
  }
  double fy2 = fy + T::kFieldH + 10 + 6 + 11;
  if (L::Rect{r.x + 16, int(fy2), r.w - 32, T::kFieldH}.contains(x, y)) {
    d.focus = 1;
    schedule_frame(app);
    return true;
  }
  double fy3 = fy2 + T::kFieldH + 10 + 6 + 11;
  if (L::Rect{r.x + 16, int(fy3), r.w - 32, T::kFieldH}.contains(x, y)) {
    d.focus = 2;
    schedule_frame(app);
    return true;
  }
  return true;
}

void mount_options_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.mountOptsDlg;
  if (!d.open) return;
  d.hover_auto = d.hover_startup = d.hover_show = false;
  d.hover_defaults = d.hover_cancel = d.hover_apply = false;
  auto r = L::dialog_rect(app, kMountOptsDlgW, kMountOptsDlgH);
  double y0 = r.y + 58 - 4;
  if (L::Rect{r.x + 8, int(y0), r.w - 16, 28}.contains(x, y)) d.hover_auto = true;
  if (L::Rect{r.x + 8, int(y0) + 30, r.w - 16, 28}.contains(x, y)) d.hover_startup = true;
  if (L::Rect{r.x + 8, int(y0) + 60, r.w - 16, 28}.contains(x, y)) d.hover_show = true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + 16, int(by), 140, 34}.contains(x, y)) d.hover_defaults = true;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_apply = true;
}

bool mount_options_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.mountOptsDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  TextField* f = d.focus == 0 ? &d.mount_point : d.focus == 1 ? &d.fs_type : &d.options;
  if (sym == XKB_KEY_BackSpace) {
    f->backspace();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Tab) {
    d.focus = (d.focus + 1) % 3;
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    f->insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

}  // namespace eh::disks
