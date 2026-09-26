#include "ux/disks/dialogs/format_volume_dialog.hpp"

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

static const char* kFs[] = {"ext4", "btrfs", "xfs", "ntfs", "vfat",
                            "exfat", "f2fs", "swap"};
static const char* kFsDesc[] = {"Linux", "Copy-on-write", "High-perf", "Windows",
                                "USB / SD", "Large USB", "Flash", "Swap"};
static constexpr int kFsN = 8;

static L::Rect dlg_rect(const AppState& app) {
  return L::dialog_rect(app, kFormatVolDlgW, kFormatVolDlgH);
}

static void field_box(AppState& app, cairo_t* cr, double x, double y, double w,
                      const TextField& f, bool focused) {
  W::fill_rounded(cr, x, y, w, T::kFieldH, 8,
                  focused ? W::RGBA{app.accentR, app.accentG, app.accentB, 0.14}
                          : W::RGBA{1, 1, 1, 0.06});
  W::stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, T::kFieldH - 1, 8,
                    focused ? W::RGB{app.accentR, app.accentG, app.accentB}
                            : W::RGB{app.outlineR, app.outlineG, app.outlineB});
  std::string shown = f.password ? std::string(f.text.size(), '*') : f.text;
  W::text_ellipsis(cr, shown, x + 10, y + 22, 13, {app.textR, app.textG, app.textB}, 0,
                   w - 20);
  if (focused) {
    std::string pre = shown.substr(0, std::min(f.cursor, shown.size()));
    double tw = W::text_w(cr, pre, 13, 0);
    cairo_set_source_rgb(cr, app.accentR, app.accentG, app.accentB);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, x + 10 + tw, y + 8);
    cairo_line_to(cr, x + 10 + tw, y + T::kFieldH - 8);
    cairo_stroke(cr);
  }
}

void draw_format_volume_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.fmtDlg;
  if (!d.open) return;
  auto r = dlg_rect(app);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Format Volume", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});

  double y = r.y + 60;
  W::text(cr, "Volume name", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  field_box(app, cr, r.x + 16, y, r.w - 32, d.vol_name, d.focus == 0);
  y += T::kFieldH + 12;
  W::text(cr, "Erase", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  const char* er[2] = {"Quick", "Overwrite with zeros"};
  for (int i = 0; i < 2; ++i) {
    double ix = r.x + 16 + i * ((r.w - 32) / 2);
    bool hov = d.hover_erase == i;
    if (hov) W::fill_rounded(cr, ix - 4, y - 4, (r.w - 32) / 2, 26, 7, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, ix + 8, y + 8, 7, d.erase_mode == i);
    W::text(cr, er[i], ix + 24, y + 12, 12, txt(app), 0);
  }
  y += 28;
  W::text(cr, "Type", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = y + rowi * 26;
    bool hov = d.hover_fs == i;
    if (hov) W::fill_rounded(cr, ix - 4, iy - 3, (r.w - 32) / 2 - 4, 24, 6, {1, 1, 1, 0.05});
    W::radio_dot(cr, app, ix + 8, iy + 9, 7, d.fs_idx == i);
    W::text(cr, kFs[i], ix + 24, iy + 13, 12, txt(app), 1);
    double tw = W::text_w(cr, kFs[i], 12, 1);
    W::text(cr, kFsDesc[i], ix + 24 + tw + 6, iy + 13, 11, sec(app), 0);
  }
  y += 4 * 26 + 8;
  {
    bool hov = d.hover_encrypt;
    if (hov) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 26, 7, {1, 1, 1, 0.05});
    W::checkbox(cr, app, r.x + 16, y, 18, d.encrypt, hov);
    W::text(cr, "Password-protect volume (LUKS)", r.x + 42, y + 14, 12, txt(app), 0);
  }
  y += 30;
  if (d.encrypt) {
    W::text(cr, "Passphrase", r.x + 16, y, 11, sec(app), 1);
    y += 6;
    field_box(app, cr, r.x + 16, y, r.w - 32 - 44, d.pass, d.focus == 1);
    // show toggle
    W::checkbox(cr, app, r.x + r.w - 48, y + 8, 18, d.show_pass, d.hover_show);
    W::text(cr, "Show", r.x + r.w - 48, y + 40, 10, sec(app), 0);
    y += T::kFieldH + 8;
    W::text(cr, "Confirm", r.x + 16, y, 11, sec(app), 1);
    y += 6;
    field_box(app, cr, r.x + 16, y, r.w - 32, d.pass_confirm, d.focus == 2);
    y += T::kFieldH + 4;
    if (d.pass.text.size() >= 12)
      W::text(cr, "Strong passphrase", r.x + 16, y + 8, 11, {0.35, 0.75, 0.45}, 1);
    else if (!d.pass.text.empty())
      W::text(cr, "Use 12+ characters for safety", r.x + 16, y + 8, 11,
              {0.9, 0.7, 0.35}, 0);
    y += 16;
  }
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Format", d.hover_format,
                   false, false, true, false);
  }
}

static std::shared_ptr<Block> target_block(AppState& app, int idx) {
  auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
  if (app.selected_drive < 0 || app.selected_drive >= int(drives.size())) return nullptr;
  auto blocks = filtered_blocks(drives[size_t(app.selected_drive)], app.search.text);
  if (idx < 0 || idx >= int(blocks.size())) return nullptr;
  return blocks[size_t(idx)];
}

bool format_volume_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.fmtDlg;
  if (!d.open) return false;
  auto r = dlg_rect(app);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double fy = r.y + 66;
  if (L::Rect{r.x + 16, int(fy), r.w - 32, T::kFieldH}.contains(x, y)) {
    d.focus = 0;
    d.vol_name.focused = true;
    schedule_frame(app);
    return true;
  }
  double ey = fy + T::kFieldH + 18;
  for (int i = 0; i < 2; ++i) {
    double ix = r.x + 16 + i * ((r.w - 32) / 2);
    if (L::Rect{int(ix) - 4, int(ey) - 4, (r.w - 32) / 2, 26}.contains(x, y)) {
      d.erase_mode = i;
      schedule_frame(app);
      return true;
    }
  }
  double ty = ey + 34;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = ty + rowi * 26;
    if (L::Rect{int(ix) - 4, int(iy) - 3, (r.w - 32) / 2 - 4, 24}.contains(x, y)) {
      d.fs_idx = i;
      schedule_frame(app);
      return true;
    }
  }
  double cby = ty + 4 * 26 + 8 - 4;
  if (L::Rect{r.x + 8, int(cby), r.w - 16, 26}.contains(x, y)) {
    d.encrypt = !d.encrypt;
    schedule_frame(app);
    return true;
  }
  if (d.encrypt) {
    double py = cby + 30 + 6 + 11;
    if (L::Rect{r.x + 16, int(py), r.w - 32 - 44, T::kFieldH}.contains(x, y)) {
      d.focus = 1;
      schedule_frame(app);
      return true;
    }
    if (L::Rect{r.x + r.w - 48, int(py) + 8, 18, 18}.contains(x, y)) {
      d.show_pass = !d.show_pass;
      d.pass.password = !d.show_pass;
      d.pass_confirm.password = !d.show_pass;
      schedule_frame(app);
      return true;
    }
    double qy = py + T::kFieldH + 8 + 6 + 11;
    if (L::Rect{r.x + 16, int(qy), r.w - 32, T::kFieldH}.contains(x, y)) {
      d.focus = 2;
      schedule_frame(app);
      return true;
    }
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    d.error.clear();
    if (d.encrypt) {
      if (d.pass.text.size() < 6) {
        d.error = "Passphrase must be at least 6 characters.";
        schedule_frame(app);
        return true;
      }
      if (d.pass.text != d.pass_confirm.text) {
        d.error = "Passphrases do not match.";
        schedule_frame(app);
        return true;
      }
    }
    auto block = target_block(app, d.block_idx);
    if (!block) {
      d.error = "No volume selected.";
      schedule_frame(app);
      return true;
    }
    d.pending = true;
    std::string fs = kFs[d.fs_idx];
    std::string label = d.vol_name.text;
    std::string pw = d.encrypt ? d.pass.text : "";
    auto job = JobTracker::instance().start("Formatting " + block_short_name(block) + "…",
                                            block->get_object_path(), false);
    block->format_async(fs, label, pw, [&app, job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Formatted" : "Format failed");
      app.fmtDlg->pending = false;
      app.fmtDlg->open = false;
      toast(app, ok ? "Volume formatted" : "Format failed", !ok);
      schedule_frame(app);
    });
    schedule_frame(app);
    return true;
  }
  return true;
}

void format_volume_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.fmtDlg;
  if (!d.open) return;
  d.hover_fs = -1;
  d.hover_erase = -1;
  d.hover_encrypt = false;
  d.hover_show = false;
  d.hover_cancel = false;
  d.hover_format = false;
  auto r = dlg_rect(app);
  double fy = r.y + 66;
  (void)fy;
  double ey = r.y + 66 + T::kFieldH + 18;
  for (int i = 0; i < 2; ++i) {
    double ix = r.x + 16 + i * ((r.w - 32) / 2);
    if (L::Rect{int(ix) - 4, int(ey) - 4, (r.w - 32) / 2, 26}.contains(x, y)) d.hover_erase = i;
  }
  double ty = ey + 34;
  for (int i = 0; i < kFsN; ++i) {
    int col = i % 2, rowi = i / 2;
    double ix = r.x + 16 + col * ((r.w - 32) / 2);
    double iy = ty + rowi * 26;
    if (L::Rect{int(ix) - 4, int(iy) - 3, (r.w - 32) / 2 - 4, 24}.contains(x, y)) d.hover_fs = i;
  }
  double cby = ty + 4 * 26 + 8 - 4;
  if (L::Rect{r.x + 8, int(cby), r.w - 16, 26}.contains(x, y)) d.hover_encrypt = true;
  if (d.encrypt) {
    double py = cby + 30 + 6 + 11;
    if (L::Rect{r.x + r.w - 48, int(py) + 8, 18, 18}.contains(x, y)) d.hover_show = true;
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_format = true;
}

bool format_volume_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.fmtDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  TextField* f = nullptr;
  if (d.focus == 0) f = &d.vol_name;
  if (d.focus == 1 && d.encrypt) f = &d.pass;
  if (d.focus == 2 && d.encrypt) f = &d.pass_confirm;
  if (sym == XKB_KEY_BackSpace) {
    if (f) f->backspace();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Delete) {
    if (f) f->del();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Left) {
    if (f) f->move_left();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Right) {
    if (f) f->move_right();
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_Tab) {
    d.focus = (d.focus + 1) % (d.encrypt ? 3 : 1);
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    if (f) f->insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

}  // namespace eh::disks
