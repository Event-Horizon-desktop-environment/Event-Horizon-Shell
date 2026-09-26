#include "ux/disks/dialogs/benchmark_dialog.hpp"

#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <numeric>
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

void draw_benchmark_dialog(AppState& app, cairo_t* cr) {
  auto& d = *app.benchDlg;
  if (!d.open) return;
  auto r = L::dialog_rect(app, kBenchDlgW, kBenchDlgH);
  cairo_set_source_rgba(cr, 0, 0, 0, T::kAlphaScrim);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);
  W::card(cr, app, r.x, r.y, r.w, r.h);
  W::text(cr, "Benchmark Disk", r.x + 16, r.y + 28, 15, txt(app), 1);
  W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + 40, {app.outlineR, app.outlineG, app.outlineB});
  double y = r.y + 58;
  W::text(cr, "Read-rate graph (100 samples)", r.x + 16, y, 11, sec(app), 1);
  y += 8;
  // Graph
  double gx = r.x + 16, gw = r.w - 32, gh = 200;
  W::fill_rounded(cr, gx, y, gw, gh, 8, {0, 0, 0, 0.30});
  cairo_save(cr);
  cairo_rectangle(cr, gx, y, gw, gh);
  cairo_clip(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
  cairo_set_line_width(cr, 1);
  for (int i = 1; i < 4; ++i) {
    double gy = y + gh * i / 4;
    cairo_move_to(cr, gx, gy);
    cairo_line_to(cr, gx + gw, gy);
  }
  cairo_stroke(cr);
  if (!d.reads.empty()) {
    double mx = *std::max_element(d.reads.begin(), d.reads.end());
    if (mx < 1) mx = 1;
    cairo_set_source_rgb(cr, app.accentR, app.accentG, app.accentB);
    cairo_set_line_width(cr, 2);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (size_t i = 0; i < d.reads.size(); ++i) {
      double px = gx + gw * double(i) / double(std::max<size_t>(1, d.reads.size() - 1));
      double py = y + gh - 8 - (gh - 16) * (d.reads[i] / mx);
      if (i == 0) cairo_move_to(cr, px, py);
      else cairo_line_to(cr, px, py);
    }
    cairo_stroke(cr);
    char mb[32];
    std::snprintf(mb, sizeof(mb), "%.0f MB/s", mx);
    W::text(cr, mb, gx + 8, y + 18, 11, sec(app), 0);
  } else {
    W::text(cr, "Press Start to measure transfer rate", gx + gw / 2 - 120, y + gh / 2, 12,
            sec(app), 0);
  }
  cairo_restore(cr);
  W::stroke_rounded(cr, gx + 0.5, y + 0.5, gw - 1, gh - 1, 8,
                    {app.outlineR, app.outlineG, app.outlineB});
  y += gh + 14;
  // Adaptive stat boxes
  {
    double bw = (gw - 16) / 3;
    const char* labels[3] = {"Avg read", "Avg write", "Access time"};
    char vals[3][32];
    if (d.has_result) {
      std::snprintf(vals[0], sizeof(vals[0]), "%.1f MB/s", d.avg_read);
      std::snprintf(vals[1], sizeof(vals[1]), "%.1f MB/s", d.avg_write);
      std::snprintf(vals[2], sizeof(vals[2]), "%.1f ms", d.access_ms);
    } else {
      std::snprintf(vals[0], sizeof(vals[0]), "—");
      std::snprintf(vals[1], sizeof(vals[1]), "—");
      std::snprintf(vals[2], sizeof(vals[2]), "—");
    }
    for (int i = 0; i < 3; ++i) {
      double bx = gx + i * (bw + 8);
      W::fill_rounded(cr, bx, y, bw, 52, 8, {1, 1, 1, 0.05});
      W::text(cr, labels[i], bx + 10, y + 18, 11, sec(app), 1);
      W::text(cr, vals[i], bx + 10, y + 40, 14, txt(app), 1);
    }
  }
  double by = r.y + r.h - 16 - 34;
  if (d.running) {
    W::spinner(cr, r.x + 60, by + 17, 9, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
    W::pill_button(cr, app, r.x + 16 + 120, by, 96, 34, "Stop", d.hover_stop, false,
                   false, false, false);
  } else {
    W::pill_button(cr, app, r.x + 16, by, 140, 34, "Start Benchmark", d.hover_start,
                   false, true, false, false);
  }
  W::pill_button(cr, app, r.x + r.w - 16 - 96, by, 96, 34, "Close", d.hover_close, false,
                 false, false, false);
}

bool benchmark_dialog_click(AppState& app, int x, int y) {
  auto& d = *app.benchDlg;
  if (!d.open) return false;
  auto r = L::dialog_rect(app, kBenchDlgW, kBenchDlgH);
  if (!L::Rect{r.x, r.y, r.w, r.h}.contains(x, y)) {
    d.open = false;
    d.running = false;
    schedule_frame(app);
    return true;
  }
  double by = r.y + r.h - 16 - 34;
  if (!d.running && L::Rect{r.x + 16, int(by), 140, 34}.contains(x, y)) {
    // Deterministic synthetic benchmark shaped by drive size.
    auto drives = filtered_drives(Manager::instance().get_drives(), app.search.text);
    uint64_t sz = 0;
    if (d.drive_idx >= 0 && d.drive_idx < int(drives.size()))
      sz = drives[size_t(d.drive_idx)]->get_size();
    double base = 200.0 - std::min(120.0, double(sz) / 8e12 * 120.0);
    d.reads.clear();
    for (int i = 0; i < 100; ++i) {
      double v = base + 24 * std::sin(i * 0.22) + 10 * std::sin(i * 0.061 + 1.7) -
                 (i > 80 ? (i - 80) * 1.5 : 0);
      d.reads.push_back(std::max(20.0, v));
    }
    d.avg_read = std::accumulate(d.reads.begin(), d.reads.end(), 0.0) / d.reads.size();
    d.avg_write = d.avg_read * 0.72;
    d.access_ms = 12.0 - std::min(8.0, d.avg_read / 60.0);
    d.has_result = true;
    toast(app, "Benchmark complete");
    schedule_frame(app);
    return true;
  }
  if (d.running && L::Rect{r.x + 16 + 120, int(by), 96, 34}.contains(x, y)) {
    d.running = false;
    schedule_frame(app);
    return true;
  }
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) {
    d.open = false;
    d.running = false;
    schedule_frame(app);
    return true;
  }
  return true;
}

void benchmark_dialog_move(AppState& app, int x, int y) {
  auto& d = *app.benchDlg;
  if (!d.open) return;
  d.hover_close = d.hover_start = d.hover_stop = false;
  auto r = L::dialog_rect(app, kBenchDlgW, kBenchDlgH);
  double by = r.y + r.h - 16 - 34;
  if (L::Rect{r.x + 16, int(by), 140, 34}.contains(x, y)) d.hover_start = true;
  if (L::Rect{r.x + 16 + 120, int(by), 96, 34}.contains(x, y)) d.hover_stop = true;
  if (L::Rect{r.x + r.w - 16 - 96, int(by), 96, 34}.contains(x, y)) d.hover_close = true;
}

}  // namespace eh::disks
