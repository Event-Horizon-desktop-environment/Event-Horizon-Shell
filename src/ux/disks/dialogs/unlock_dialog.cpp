#include "ux/disks/dialogs/unlock_dialog.hpp"

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

static L::Rect dlg_rect(const AppState& app) {
  return L::dialog_rect(app, kUnlockDlgW, kUnlockDlgH);
}

void draw_unlock_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.unlockDlg;
  if (!d.open) return;
  auto r = dlg_rect(app);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Unlock Encrypted Volume", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 60;
  W::text(cr, "Passphrase", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  {
    auto& f = d.pass;
    W::fill_rounded(cr, r.x + 16, y, r.w - 32 - 44, T::kFieldH, 8,
                    W::RGBA{1, 1, 1, 0.06});
    W::stroke_rounded(cr, r.x + 16.5, y + 0.5, r.w - 32 - 44 - 1, T::kFieldH - 1, 8,
                      {app.outlineR, app.outlineG, app.outlineB});
    std::string shown = d.show ? f.text : std::string(f.text.size(), '*');
    W::text_ellipsis(cr, shown, r.x + 26, y + 22, 13, txt(app), 0, r.w - 32 - 64);
    W::checkbox(cr, app, r.x + r.w - 48, y + 8, 18, d.show, d.hover_show);
    W::text(cr, "Show", r.x + r.w - 48, y + 40, 10, sec(app), 0);
  }
  y += T::kFieldH + 14;
  {
    if (d.hover_bg) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 26, 7, {1, 1, 1, 0.05});
    W::checkbox(cr, app, r.x + 16, y, 18, d.unlock_bg, d.hover_bg);
    W::text(cr, "Unlock in background", r.x + 42, y + 14, 12, txt(app), 0);
  }
  y += 28;
  {
    if (d.hover_rem) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 26, 7, {1, 1, 1, 0.05});
    W::checkbox(cr, app, r.x + 16, y, 18, d.remember, d.hover_rem);
    W::text(cr, "Remember in keyring", r.x + 42, y + 14, 12, txt(app), 0);
  }
  y += 28;
  W::text(cr, "Multi-keyfile volumes unlock with any enrolled key.", r.x + 16, y, 11,
          sec(app), 0);
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Unlock", d.hover_unlock,
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

bool unlock_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.unlockDlg;
  if (!d.open) return false;
  auto r = dlg_rect(app);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double py = r.y + 66;
  if (L::Rect{r.x + r.w - 48, int(py) + 8, 18, 18}.contains(x, y)) {
    d.show = !d.show;
    d.pass.password = !d.show;
    schedule_frame(app);
    return true;
  }
  double bgy = py + T::kFieldH + 14 - 4;
  if (L::Rect{r.x + 8, int(bgy), r.w - 16, 26}.contains(x, y)) {
    d.unlock_bg = !d.unlock_bg;
    schedule_frame(app);
    return true;
  }
  double rmy = bgy + 28;
  if (L::Rect{r.x + 8, int(rmy), r.w - 16, 26}.contains(x, y)) {
    d.remember = !d.remember;
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
    if (d.pass.text.empty()) {
      d.error = "Enter the passphrase.";
      schedule_frame(app);
      return true;
    }
    auto block = target_block(app, d.block_idx);
    if (!block) {
      d.error = "No volume selected.";
      schedule_frame(app);
      return true;
    }
    d.pending = true;
    auto job = JobTracker::instance().start("Unlocking " + block_short_name(block) + "…",
                                            block->get_object_path(), false);
    block->unlock_async(d.pass.text, d.remember,
                        [&app, job](bool ok, std::string) {
                          JobTracker::instance().finish(job->id, ok,
                                                        ok ? "Unlocked" : "Unlock failed");
                          app.unlockDlg->pending = false;
                          app.unlockDlg->open = false;
                          toast(app, ok ? "Volume unlocked" : "Unlock failed", !ok);
                          schedule_frame(app);
                        });
    schedule_frame(app);
    return true;
  }
  return true;
}

void unlock_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.unlockDlg;
  if (!d.open) return;
  d.hover_show = d.hover_bg = d.hover_rem = d.hover_cancel = d.hover_unlock = false;
  auto r = dlg_rect(app);
  double py = r.y + 66;
  if (L::Rect{r.x + r.w - 48, int(py) + 8, 18, 18}.contains(x, y)) d.hover_show = true;
  double bgy = py + T::kFieldH + 14 - 4;
  if (L::Rect{r.x + 8, int(bgy), r.w - 16, 26}.contains(x, y)) d.hover_bg = true;
  double rmy = bgy + 28;
  if (L::Rect{r.x + 8, int(rmy), r.w - 16, 26}.contains(x, y)) d.hover_rem = true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_unlock = true;
}

bool unlock_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.unlockDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    d.pass.backspace();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Delete) {
    d.pass.del();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Left) {
    d.pass.move_left();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Right) {
    d.pass.move_right();
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    d.pass.insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

}  // namespace eh::disks
