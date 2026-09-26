#include "ux/disks/dialogs/smart_dialog.hpp"

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include "ux/disks/app.hpp"
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

void draw_smart_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.smartDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kSmartDlgW, kSmartDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "SMART Data & Self-Tests", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});

  auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
  std::shared_ptr<Drive> drive;
  if (d.drive_idx >= 0 && d.drive_idx < int(drives.size())) drive = drives[size_t(d.drive_idx)];

  double y = r.y + 56;
  if (!drive) {
    W::text(cr, "No drive selected.", r.x + 16, y + 16, 12, sec(app), 0);
  } else if (!drive->smart_supported()) {
    W::fill_rounded(cr, r.x + 16, y, r.w - 32, 44, 8, {1, 1, 1, 0.05});
    W::text(cr, "SMART is not available for this drive.", r.x + 28, y + 27, 12, txt(app),
            1);
    y += 56;
  } else {
    bool warn = false;
    std::string a = drive->smart_one_liner_assessment(&warn);
    W::fill_rounded(cr, r.x + 16, y, r.w - 32, 40, 8,
                    warn ? W::RGBA{0.85, 0.30, 0.30, 0.14} : W::RGBA{0.30, 0.68, 0.42, 0.12});
    W::text(cr, a, r.x + 28, y + 25, 12, txt(app), 1);
    y += 52;
    // Attr rows (clipped, scrollable)
    cairo_save(cr);
    cairo_rectangle(cr, r.x + 16, y, r.w - 32, 220);
    cairo_clip(cr);
    struct Row {
      const char* k;
      std::string v;
    };
    std::vector<Row> rows;
    {
      auto hrs = drive->smart_power_on_hours();
      auto tmp = drive->smart_temperature();
      rows.push_back({"Overall", a});
      rows.push_back({"Power-on hours", hrs >= 0 ? std::to_string(hrs) + " h" : "—"});
      rows.push_back({"Temperature", tmp >= 0 ? std::to_string(int(tmp)) + " °C" : "—"});
      rows.push_back({"Self-test", d.pending ? "Running…" : "Idle"});
      rows.push_back({"Note", "Full attribute table needs ATA passthrough"});
    }
    double ry = y + 16 - d.scroll;
    W::text(cr, "ATTRIBUTE", r.x + 28, ry - 12, 10, sec(app), 1);
    W::text_right(cr, "VALUE", r.x + r.w - 28, ry - 12, 10, sec(app), 1);
    for (auto& row : rows) {
      W::text(cr, row.k, r.x + 28, ry + 8, 12, sec(app), 0);
      W::text_right(cr, row.v, r.x + r.w - 28, ry + 8, 12, txt(app), 0);
      ry += 26;
    }
    cairo_restore(cr);
    y += 232;
  }
  double by = r.y + r.h - 16 - 34;
  if (d.pending) {
    W::spinner(cr, r.x + 120, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
    W::text(cr, "Self-test running…", r.x + 136, by + 22, 12, sec(app), 0);
  } else {
    W::pill_button(cr, app, r.x + 16, by, 120, 34, "Short Test", d.hover_short, false,
                   false, false, !drive || !drive->smart_supported());
    W::pill_button(cr, app, r.x + 144, by, 140, 34, "Extended Test", d.hover_ext, false,
                   false, false, !drive || !drive->smart_supported());
  }
  W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Close", d.hover_close, false,
                 false, false, false);
}

static std::shared_ptr<Drive> dlg_drive(AppState& app) {
  auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
  int i = app.smartDlg->drive_idx;
  if (i < 0 || i >= int(drives.size())) return nullptr;
  return drives[size_t(i)];
}

bool smart_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.smartDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kSmartDlgW, kSmartDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + 16, int(by), 120, 34}.contains(x, y)) {
    auto drive = dlg_drive(app);
    if (!drive || d.pending) return true;
    d.pending = true;
    auto job = JobTracker::instance().start("SMART short test…", "", false);
    drive->run_smart_test_async("short", [&app, job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Test started" : "Test failed");
      app.smartDlg->pending = false;
      toast(app, ok ? "Short self-test started" : "Self-test failed", !ok);
      schedule_frame(app);
    });
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + 144, int(by), 140, 34}.contains(x, y)) {
    auto drive = dlg_drive(app);
    if (!drive || d.pending) return true;
    d.pending = true;
    auto job = JobTracker::instance().start("SMART extended test…", "", false);
    drive->run_smart_test_async("extended", [&app, job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Test started" : "Test failed");
      app.smartDlg->pending = false;
      toast(app, ok ? "Extended self-test started" : "Self-test failed", !ok);
      schedule_frame(app);
    });
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    if (!d.pending) d.open = false;
    schedule_frame(app);
    return true;
  }
  return true;
}

void smart_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.smartDlg;
  if (!d.open) return;
  d.hover_close = d.hover_short = d.hover_ext = false;
  auto r = L::dialog_rect(app, kSmartDlgW, kSmartDlgH);
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + 16, int(by), 120, 34}.contains(x, y)) d.hover_short = true;
  if (L::Rect{r.x + 144, int(by), 140, 34}.contains(x, y)) d.hover_ext = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_close = true;
}

bool smart_dialog_scroll(AppState& app, double dy) {
  auto& d = *app.smartDlg;
  if (!d.open) return false;
  d.scroll = std::clamp(d.scroll + int(dy * 20), 0, 120);
  schedule_frame(app);
  return true;
}

}  // namespace eh::disks
