#include "ux/disks/dialogs/resize_dialog.hpp"

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

void draw_resize_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.resizeDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kResizeDlgW, kResizeDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Resize Volume", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 60;
  W::text(cr, ("Current: " + size_str(d.cur_size)).c_str(), r.x + 16, y, 12, txt(app), 0);
  W::text_right(cr, ("Maximum: " + size_str(d.max_size)).c_str(), r.x + r.w - 16, y, 11,
                sec(app), 0);
  y += 10;
  {
    double tx = r.x + 16, tw = r.w - 32;
    W::fill_rounded(cr, tx, y, tw, 10, 5, {1, 1, 1, 0.10});
    double fmin = d.max_size ? double(d.min_size) / double(d.max_size) : 0;
    double fnew = d.max_size ? double(d.new_size) / double(d.max_size) : 0;
    W::fill_rounded(cr, tx, y, tw * fmin, 10, 5, {0.85, 0.45, 0.30, 0.5});
    W::fill_rounded(cr, tx, y, tw * fnew, 10, 5, {app.accentR, app.accentG, app.accentB, 0.9});
    double kx = tx + tw * fnew;
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, kx, y + 5, 9, 0, 2 * M_PI);
    cairo_fill(cr);
  }
  y += 26;
  W::text(cr, ("New size: " + size_str(d.new_size)).c_str(), r.x + 16, y, 13, txt(app), 1);
  W::text(cr, ("Minimum: " + size_str(d.min_size)).c_str(), r.x + 16, y + 18, 11, sec(app),
          0);
  y += 40;
  if (d.missing_tools) {
    W::fill_rounded(cr, r.x + 16, y, r.w - 32, 56, 8, {0.85, 0.60, 0.20, 0.12});
    W::text(cr, "Resize utilities are not installed.", r.x + 28, y + 22, 12,
            {0.95, 0.75, 0.40}, 1);
    W::text(cr, d.missing_msg, r.x + 28, y + 40, 11, sec(app), 0);
    y += 64;
  } else {
    W::text(cr, "Shrinking is safe; growing needs free space after the volume.", r.x + 16,
            y + 8, 11, sec(app), 0);
    y += 24;
  }
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Resize", d.hover_resize,
                   false, true, false, d.missing_tools);
  }
}

static std::shared_ptr<Block> target_block(AppState& app, int idx) {
  auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
  if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return nullptr;
  auto blocks = filtered_blocks(drives[size_t(app.selected_drive)], app.search.text);
  if (idx < 0 || idx >= int(blocks.size())) return nullptr;
  return blocks[size_t(idx)];
}

bool resize_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.resizeDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kResizeDlgW, kResizeDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double ty = r.y + 70;
  double tx = r.x + 16, tw = r.w - 32;
  if (L::Rect{int(tx) - 8, int(ty) - 10, int(tw) + 16, 30}.contains(x, y)) {
    double f = double(x - tx) / tw;
    f = std::clamp(f, 0.0, 1.0);
    uint64_t v = uint64_t(f * double(d.max_size));
    d.new_size = std::clamp(v, d.min_size, d.max_size);
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
    if (d.missing_tools) return true;
    auto block = target_block(app, d.block_idx);
    if (!block) return true;
    d.pending = true;
    auto job = JobTracker::instance().start("Resizing " + block_short_name(block) + "…",
                                            block->get_object_path(), false);
    block->resize_async(d.new_size, [&app, job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Resized" : "Resize failed");
      app.resizeDlg->pending = false;
      app.resizeDlg->open = false;
      toast(app, ok ? "Volume resized" : "Resize failed", !ok);
      schedule_frame(app);
    });
    schedule_frame(app);
    return true;
  }
  return true;
}

void resize_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.resizeDlg;
  if (!d.open) return;
  d.hover_cancel = d.hover_resize = false;
  auto r = L::dialog_rect(app, kResizeDlgW, kResizeDlgH);
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_resize = true;
}

}  // namespace eh::disks
