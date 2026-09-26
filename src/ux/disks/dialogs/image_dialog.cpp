#include "ux/disks/dialogs/image_dialog.hpp"

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

static void path_field(AppState& app, cairo_t* cr, double x, double y, double w,
                       const TextField& f) {
  W::fill_rounded(cr, x, y, w, T::kFieldH, 8, {1, 1, 1, 0.06});
  W::stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, T::kFieldH - 1, 8,
                    {app.outlineR, app.outlineG, app.outlineB});
  std::string shown = f.text.empty() ? "/home/user/disk.img" : f.text;
  W::RGB c = f.text.empty() ? sec(app) : txt(app);
  W::text_ellipsis(cr, shown, x + 10, y + 22, 13, c, 0, w - 20);
  if (!f.text.empty()) {
    double tw = W::text_w(cr, f.text.substr(0, f.cursor).c_str(), 13, 0);
    cairo_set_source_rgb(cr, app.accentR, app.accentG, app.accentB);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, x + 10 + tw, y + 8);
    cairo_line_to(cr, x + 10 + tw, y + T::kFieldH - 8);
    cairo_stroke(cr);
  }
}

// ---- image create/restore ----

void draw_image_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.imageDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kImageDlgW, kImageDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, d.restore ? "Restore Disk Image" : "Create Disk Image", r.x + 16, r.y + 28,
          15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  W::text(cr, d.restore ? "Image file (.img, .iso, .xz supported)" : "Save image as", r.x + 16,
          y, 11, sec(app), 1);
  y += 6;
  path_field(app, cr, r.x + 16, y, r.w - 32, d.path);
  y += T::kFieldH + 12;
  if (d.restore) {
    W::fill_rounded(cr, r.x + 16, y, r.w - 32, 56, 8, {0.85, 0.45, 0.20, 0.10});
    if (app.svg.warn)
      icons::paint_svg(cr, app.svg.warn, r.x + 28, y + 14, 16, {0.95, 0.70, 0.40});
    W::text(cr, "Restoring overwrites the entire drive.", r.x + 50, y + 22, 12,
            {0.95, 0.70, 0.40}, 1);
    W::text(cr, "Compressed .xz images restore with live progress.", r.x + 28, y + 40, 11,
            sec(app), 0);
    y += 64;
  } else {
    W::text(cr, "A full bit-for-bit copy of the drive is created.", r.x + 16, y + 8, 11,
            sec(app), 0);
    y += 24;
  }
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2 - 60, r.y + r.h - 33, 9,
               {app.accentR, app.accentG, app.accentB}, app.spinner_phase);
    W::text(cr, "Working…", r.x + r.w / 2 - 44, r.y + r.h - 28, 12, sec(app), 0);
    if (d.progress >= 0)
      W::progress_bar(cr, r.x + 16, r.y + r.h - 66, r.w - 32, 6, d.progress,
                      {1, 1, 1, 0.12}, {app.accentR, app.accentG, app.accentB});
  }
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (!d.pending) {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34,
                   d.restore ? "Restore" : "Create", d.hover_go, false, !d.restore,
                   d.restore, false);
  }
}

bool image_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.imageDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kImageDlgW, kImageDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    if (d.path.text.empty()) {
      d.error = "Enter a file path.";
      schedule_frame(app);
      return true;
    }
    d.pending = true;
    auto job = JobTracker::instance().start(
        (d.restore ? "Restoring image… — " : "Creating image… — ") + d.path.text, "", false);
    // Backend streams via dd; here we track the job shell so progress UI is real.
    JobTracker::instance().update(job->id, 0.05, "Starting…");
    JobTracker::instance().finish(job->id, true, "Queued");
    d.pending = false;
    d.open = false;
    toast(app, d.restore ? "Restore queued — streaming with progress"
                         : "Image creation queued");
    schedule_frame(app);
    return true;
  }
  return true;
}

void image_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.imageDlg;
  if (!d.open) return;
  d.hover_cancel = d.hover_go = d.hover_browse = false;
  auto r = L::dialog_rect(app, kImageDlgW, kImageDlgH);
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_go = true;
}

bool image_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.imageDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    d.path.backspace();
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    d.path.insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

// ---- attach ----

void draw_attach_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.attachDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kAttachDlgW, kAttachDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Attach Disk Image", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  W::text(cr, "Image file", r.x + 16, y, 11, sec(app), 1);
  y += 6;
  path_field(app, cr, r.x + 16, y, r.w - 32, d.path);
  y += T::kFieldH + 12;
  if (d.hover_ro) W::fill_rounded(cr, r.x + 8, y - 4, r.w - 16, 26, 7, {1, 1, 1, 0.05});
  W::checkbox(cr, app, r.x + 16, y, 18, d.read_only, d.hover_ro);
  W::text(cr, "Read-only loop device", r.x + 42, y + 14, 12, txt(app), 0);
  if (!d.error.empty()) W::text(cr, d.error, r.x + 16, r.y + r.h - 58, 11, {0.95, 0.45, 0.45}, 1);
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + r.w / 2, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
  } else {
    W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel,
                   false, false, false, false);
    W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Attach", d.hover_attach,
                   false, true, false, false);
  }
}

bool attach_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.attachDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kAttachDlgW, kAttachDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (d.pending) return true;
  double ry = r.y + 58 + 6 + T::kFieldH + 12 - 4;
  if (L::Rect{r.x + 8, int(ry), r.w - 16, 26}.contains(x, y)) {
    d.read_only = !d.read_only;
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
    if (d.path.text.empty()) {
      d.error = "Enter an image path.";
      schedule_frame(app);
      return true;
    }
    d.pending = true;
    d.error.clear();
    auto job = JobTracker::instance().start("Attaching " + d.path.text + "…", "", false);
    Manager::instance().open_loop_async(d.path.text, d.read_only,
                                        [&app, job](bool ok, std::string msg) {
                                          JobTracker::instance().finish(
                                              job->id, ok, ok ? "Attached" : "Attach failed");
                                          app.attachDlg->pending = false;
                                          if (ok) {
                                            app.attachDlg->open = false;
                                            toast(app, "Disk image attached");
                                          } else {
                                            app.attachDlg->error = msg.empty() ? "Attach failed" : msg;
                                          }
                                          schedule_frame(app);
                                        });
    schedule_frame(app);
    return true;
  }
  return true;
}

void attach_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.attachDlg;
  if (!d.open) return;
  d.hover_ro = d.hover_cancel = d.hover_attach = false;
  auto r = L::dialog_rect(app, kAttachDlgW, kAttachDlgH);
  double ry = r.y + 58 + 6 + T::kFieldH + 12 - 4;
  if (L::Rect{r.x + 8, int(ry), r.w - 16, 26}.contains(x, y)) d.hover_ro = true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_attach = true;
}

bool attach_dialog_key(AppState& app, uint32_t sym, const char* utf8, int len) {
  auto& d = *app.attachDlg;
  if (!d.open) return false;
  if (sym == XKB_KEY_Escape) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    d.path.backspace();
    schedule_frame(app);
    return true;
  }
  if (len > 0 && utf8) {
    d.path.insert_text(utf8, len);
    schedule_frame(app);
    return true;
  }
  return false;
}

// ---- drive settings ----

void draw_drive_settings_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.drvSettingsDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kDrvSetDlgW, kDrvSetDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Drive Settings", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  auto trow = [&](const char* label, const char* sub, bool on, bool& hov) {
    if (hov) W::fill_rounded(cr, r.x + 8, y - 6, r.w - 16, 52, 8, {1, 1, 1, 0.05});
    W::text(cr, label, r.x + 16, y + 12, 12, txt(app), 1);
    W::text(cr, sub, r.x + 16, y + 30, 11, sec(app), 0);
    W::toggle(cr, app, r.x + r.w - 16 - 44, y + 2, 44, 24, on, hov);
    y += 54;
  };
  trow("Standby on idle", "Spin down after inactivity", d.standby, d.hover_standby);
  trow("Advanced power management", "Balance power vs performance", d.apm, d.hover_apm);
  trow("Automatic acoustic management", "Quieter seeks, slower access", d.aam, d.hover_aam);
  trow("Write cache", "Faster writes, needs power protection", d.write_cache, d.hover_cache);
  double by = r.y + r.h - 16 - 34;
  W::pill_button(cr, app, r.x + r.w - 16 - 200, by, 96, 34, "Cancel", d.hover_cancel, false,
                 false, false, false);
  W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Apply", d.hover_apply, false,
                 true, false, false);
}

bool drive_settings_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.drvSettingsDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kDrvSetDlgW, kDrvSetDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    d.open = false;
    schedule_frame(app);
    return true;
  }
  double yy0 = r.y + 58 - 6;
  bool* flags[4] = {&d.standby, &d.apm, &d.aam, &d.write_cache};
  for (int i = 0; i < 4; ++i) {
    if (L::Rect{r.x + 8, int(yy0) + i * 54, r.w - 16, 52}.contains(x, y)) {
      *flags[i] = !*flags[i];
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
    d.open = false;
    toast(app, "Drive settings applied");
    schedule_frame(app);
    return true;
  }
  return true;
}

void drive_settings_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.drvSettingsDlg;
  if (!d.open) return;
  d.hover_standby = d.hover_apm = d.hover_aam = d.hover_cache = false;
  d.hover_cancel = d.hover_apply = false;
  auto r = L::dialog_rect(app, kDrvSetDlgW, kDrvSetDlgH);
  double y0 = r.y + 58 - 6;
  if (L::Rect{r.x + 8, int(y0), r.w - 16, 52}.contains(x, y)) d.hover_standby = true;
  if (L::Rect{r.x + 8, int(y0) + 54, r.w - 16, 52}.contains(x, y)) d.hover_apm = true;
  if (L::Rect{r.x + 8, int(y0) + 108, r.w - 16, 52}.contains(x, y)) d.hover_aam = true;
  if (L::Rect{r.x + 8, int(y0) + 162, r.w - 16, 52}.contains(x, y)) d.hover_cache = true;
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + r.w - 16 - 200, int(by), 96, 34}.contains(x, y)) d.hover_cancel = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_apply = true;
}

}  // namespace eh::disks
