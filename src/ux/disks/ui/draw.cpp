#include "ux/disks/app.hpp"

#include <algorithm>
#include <cmath>

#include "ux/disks/block.hpp"
#include "ux/disks/dialogs/benchmark_dialog.hpp"
#include "ux/disks/dialogs/create_partition_dialog.hpp"
#include "ux/disks/dialogs/format_disk_dialog.hpp"
#include "ux/disks/dialogs/format_volume_dialog.hpp"
#include "ux/disks/dialogs/image_dialog.hpp"
#include "ux/disks/dialogs/mount_options_dialog.hpp"
#include "ux/disks/dialogs/resize_dialog.hpp"
#include "ux/disks/dialogs/smart_dialog.hpp"
#include "ux/disks/dialogs/unlock_dialog.hpp"
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
static W::RGB out(const AppState& a) { return {a.outlineR, a.outlineG, a.outlineB}; }

// Icon button: hover/active bg + bundled SVG tinted, text glyph fallback.
static void svg_btn(AppState& app, cairo_t* cr, double x, double y, double s,
                    cairo_surface_t* svg, const char* fallback, bool hover, bool active,
                    bool disabled, bool danger = false) {
  if (hover && !disabled)
    W::fill_rounded(cr, x, y, s, s, 8, {1, 1, 1, 0.08});
  else if (active)
    W::fill_rounded(cr, x, y, s, s, 8,
                    {app.accentR, app.accentG, app.accentB, 0.22});
  W::RGB fg = danger ? W::RGB{0.95, 0.55, 0.55} : W::RGB{app.textR, app.textG, app.textB};
  if (disabled) fg = sec(app);
  if (svg) {
    icons::paint_svg(cr, svg, x + (s - 18) / 2.0, y + (s - 18) / 2.0, 18,
                     {fg.r, fg.g, fg.b});
  } else {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 16);
    std::string g(fallback);
    cairo_text_extents_t te;
    cairo_text_extents(cr, g.c_str(), &te);
    W::RGB c = fg;
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_move_to(cr, x + (s - te.width) / 2 - te.x_bearing,
                  y + (s - te.height) / 2 + te.height);
    cairo_show_text(cr, g.c_str());
  }
}

// ---- header ----

static void paint_header(AppState& app, cairo_t* cr, const L::Layout& lo,
                         const std::vector<std::shared_ptr<Drive>>& drives) {
  // Bar
  W::fill_rounded(cr, 0, 0, lo.W, T::kHeaderH, 0, {app.surfaceR, app.surfaceG,
                                                   app.surfaceB, 1.0});
  W::hairline_h(cr, 0, lo.W, T::kHeaderH - 1, out(app));
  // Menu button
  svg_btn(app, cr, lo.btn_menu.x, lo.btn_menu.y, lo.btn_menu.w, app.svg.menu, "☰",
          app.hov_menu_btn, false, false);
  // Title + count
  W::text(cr, "Disks", lo.btn_menu.x + lo.btn_menu.w + 10, 32, 16, txt(app), 1);
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zu", drives.size());
    double tw = W::text_w(cr, buf, 11, 1) + 14;
    W::fill_rounded(cr, lo.btn_menu.x + lo.btn_menu.w + 10 + W::text_w(cr, "Disks", 16, 1) + 8,
                    13, tw, 18, 9, {app.accentR, app.accentG, app.accentB, 0.25});
    W::text(cr, buf, lo.btn_menu.x + lo.btn_menu.w + 10 + W::text_w(cr, "Disks", 16, 1) + 8 + 7,
            26, 11, txt(app), 1);
  }
  // Search field
  {
    auto r = lo.search_field;
    bool focused = app.search.focused;
    W::fill_rounded(cr, r.x, r.y, r.w, r.h, 8,
                    focused ? W::RGBA{app.accentR, app.accentG, app.accentB, 0.14}
                            : W::RGBA{1, 1, 1, 0.05});
    W::stroke_rounded(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 8,
                      focused ? W::RGB{app.accentR, app.accentG, app.accentB}
                              : out(app));
    if (app.svg.search)
      icons::paint_svg(cr, app.svg.search, r.x + 9, r.y + (r.h - 15) / 2.0, 15,
                       {sec(app).r, sec(app).g, sec(app).b});
    else
      W::text(cr, "⌕", r.x + 10, r.y + 22, 14, sec(app), 0);
    std::string shown = app.search.text.empty() && !focused ? "Search drives & volumes"
                                                            : app.search.text;
    W::RGB c = app.search.text.empty() && !focused ? sec(app) : txt(app);
    W::text_ellipsis(cr, shown, r.x + 30, r.y + 22, 13, c, 0, r.w - 36);
    if (focused) {
      // caret
      double tw = W::text_w(cr, app.search.text.substr(0, app.search.cursor).c_str(),
                            13, 0);
      double cx = r.x + 30 + tw;
      cairo_set_source_rgb(cr, app.accentR, app.accentG, app.accentB);
      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, cx, r.y + 8);
      cairo_line_to(cr, cx, r.y + r.h - 8);
      cairo_stroke(cr);
    }
  }
  // Job pill
  if (lo.job_pill.w > 0) {
    auto act = JobTracker::instance().active();
    std::string lbl = act.empty() ? "Working…" : act[0]->label;
    if (lbl.size() > 22) lbl = lbl.substr(0, 22) + "…";
    W::fill_rounded(cr, lo.job_pill.x, lo.job_pill.y, lo.job_pill.w, lo.job_pill.h, 14,
                    {app.accentR, app.accentG, app.accentB, 0.18});
    W::spinner(cr, lo.job_pill.x + 14, lo.job_pill.y + 14, 7,
               {app.accentR, app.accentG, app.accentB}, app.spinner_phase);
    W::text_ellipsis(cr, lbl, lo.job_pill.x + 26, lo.job_pill.y + 18, 11, txt(app), 0,
                     lo.job_pill.w - 32);
  }
  // Right buttons
  W::pill_button(cr, app, lo.btn_attach.x, lo.btn_attach.y, lo.btn_attach.w,
                 lo.btn_attach.h, "Attach", app.hov_attach, false, false, false,
                 false);
  W::pill_button(cr, app, lo.btn_benchmark.x, lo.btn_benchmark.y, lo.btn_benchmark.w,
                 lo.btn_benchmark.h, "Benchmark", app.hov_bench, false, false, false,
                 false);
  svg_btn(app, cr, lo.btn_headermenu.x, lo.btn_headermenu.y, lo.btn_headermenu.w,
          app.svg.more, "⋮", app.hov_headermenu,
          app.menu.open && app.menu.kind == MenuKind::Main, false);
}

// ---- sidebar ----

static void paint_sidebar(AppState& app, cairo_t* cr, const L::Layout& lo,
                          const std::vector<std::shared_ptr<Drive>>& drives) {
  W::fill_rounded(cr, lo.sidebar.x, lo.sidebar.y, lo.sidebar.w, lo.sidebar.h, 0,
                  {app.surfaceR, app.surfaceG, app.surfaceB, 1.0});
  // Right divider
  W::hairline_v(cr, lo.sidebar.x + lo.sidebar.w - 1, lo.sidebar.y,
                lo.sidebar.y + lo.sidebar.h, out(app));
  // Section header
  W::text(cr, "DRIVES", lo.sidebar.x + 16, lo.sidebar.y + 20, 11, sec(app), 1);
  W::text_right(cr, std::to_string(drives.size()) + (drives.size() == 1 ? " disk" : " disks"),
                lo.sidebar.x + lo.sidebar.w - 16, lo.sidebar.y + 20, 11, sec(app), 0);
  // Sort hint
  W::text(cr, app.search.text.empty() ? "Internal · Removable · Images"
                                      : "Filtered results",
          lo.sidebar.x + 16, lo.sidebar.y + 38, 11, sec(app), 0);

  cairo_save(cr);
  cairo_rectangle(cr, lo.sidebar_list.x, lo.sidebar_list.y, lo.sidebar_list.w,
                  lo.sidebar_list.h);
  cairo_clip(cr);
  for (size_t i = 0; i < drives.size() && i < lo.drive_rows.size(); ++i) {
    auto r = lo.drive_rows[i];
    if (r.y + r.h < lo.sidebar_list.y || r.y > lo.sidebar_list.y + lo.sidebar_list.h)
      continue;
    bool sel = int(i) == app.selected_drive;
    bool hov = int(i) == app.hov_drive;
    if (sel)
      W::fill_rounded(cr, r.x, r.y, r.w, r.h, 10,
                      {app.accentR, app.accentG, app.accentB, 0.20});
    else if (hov)
      W::fill_rounded(cr, r.x, r.y, r.w, r.h, 10, {1, 1, 1, 0.05});
    if (sel)
      W::stroke_rounded(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10,
                        {app.accentR, app.accentG, app.accentB});
    auto& d = drives[i];
    // Icon tile
    double ix = r.x + 10, iy = r.y + (r.h - 36) / 2;
    W::fill_rounded(cr, ix, iy, 36, 36, 9, {1, 1, 1, sel ? 0.10 : 0.05});
    const auto* icon = app.icons.tray_icon(drive_icon_name(d), 48);
    if (icon && icon->surface)
      draw_icon_surface(cr, icon, int(ix) + 2, int(iy) + 2, 32);
    else {
      auto* ds = icons::drive_svg(app.svg, *d);
      if (ds)
        icons::paint_svg(cr, ds, ix + 4, iy + 4, 28,
                         {app.accentR, app.accentG, app.accentB});
      else
        W::text(cr, d->is_removable() ? "▤" : "◉", ix + 9, iy + 25, 18,
                {app.accentR, app.accentG, app.accentB}, 0);
    }
    // Texts
    double tx = ix + 44;
    double maxw = r.x + r.w - 34 - tx;
    W::text_ellipsis(cr, drive_display_name(d), tx, r.y + 24, 13, txt(app), 1, maxw);
    W::text_ellipsis(cr, drive_subtitle(d), tx, r.y + 42, 11, sec(app), 0, maxw);
    // Status: SMART fail / job spinner / mounted dot
    bool failing = d->smart_supported() && d->smart_failing();
    auto job = JobTracker::instance().for_block("");
    if (failing) {
      cairo_set_source_rgb(cr, 0.92, 0.30, 0.30);
      cairo_arc(cr, r.x + r.w - 44, r.y + 16, 4, 0, 2 * M_PI);
      cairo_fill(cr);
      W::text(cr, "Failing", r.x + r.w - 36, r.y + 20, 10, {0.92, 0.45, 0.45}, 1);
    }
    // Row menu button
    auto mb = lo.drive_row_menu_btn[i];
    if (hov || sel)
      svg_btn(app, cr, mb.x, mb.y, mb.w, app.svg.more, "⋮",
              app.hov_drive_menu_btn == int(i), false, false);
  }
  cairo_restore(cr);
  // Scrollbar
  int total = int(drives.size()) * T::kSidebarRowH;
  int view = lo.sidebar_list.h;
  if (total > view && view > 0) {
    int sh = std::max(view * view / total, 24);
    int sy = lo.sidebar_list.y +
             (view - sh) * app.sidebarScroll / (total - view);
    W::fill_rounded(cr, lo.sidebar.x + lo.sidebar.w - 8, sy, 4, sh, 2,
                    {1, 1, 1, 0.25});
  }
  if (drives.empty()) {
    W::text(cr, "No drives match", lo.sidebar.x + 16, lo.sidebar.y + 80, 12, sec(app), 0);
  }
}

// ---- drive card ----

static void paint_drive_card(AppState& app, cairo_t* cr, const L::Layout& lo,
                             const std::shared_ptr<Drive>& drive) {
  if (!drive) return;
  W::card(cr, app, lo.drive_card.x, lo.drive_card.y, lo.drive_card.w, lo.drive_card.h);
  // Icon
  W::fill_rounded(cr, lo.drive_icon.x, lo.drive_icon.y, lo.drive_icon.w,
                  lo.drive_icon.h, 11, {1, 1, 1, 0.05});
  const auto* icon = app.icons.tray_icon(drive_icon_name(drive), 96);
  if (icon && icon->surface)
    draw_icon_surface(cr, icon, lo.drive_icon.x + 4, lo.drive_icon.y + 4, 40);
  else {
    auto* ds = icons::drive_svg(app.svg, *drive);
    if (ds)
      icons::paint_svg(cr, ds, lo.drive_icon.x + 8, lo.drive_icon.y + 8, 32,
                       {app.accentR, app.accentG, app.accentB});
    else
      W::text(cr, "◉", lo.drive_icon.x + 12, lo.drive_icon.y + 33, 22,
              {app.accentR, app.accentG, app.accentB}, 0);
  }
  double tx = lo.drive_icon.x + lo.drive_icon.w + 12;
  double maxw = lo.btn_power.x - 8 - tx;
  W::text_ellipsis(cr, drive_display_name(drive), tx, lo.drive_card.y + 30, T::kFsTitle,
                   txt(app), 1, maxw);
  // Sub: size • serial • firmware (GParted device info parity)
  std::string sub = size_str_full(drive->get_size());
  if (!drive->get_serial().empty()) sub += "  ·  " + drive->get_serial();
  if (!drive->get_firmware_version().empty())
    sub += "  ·  FW " + drive->get_firmware_version();
  W::text_ellipsis(cr, sub, tx, lo.drive_card.y + 50, 12, sec(app), 0, maxw);
  std::string sub2;
  if (!drive->get_media().empty()) sub2 += drive->get_media() + "  ·  ";
  sub2 += drive->is_removable() ? "Removable" : "Internal";
  if (drive->is_loop() && !drive->loop_file().empty()) sub2 += "  ·  " + drive->loop_file();
  W::text_ellipsis(cr, sub2, tx, lo.drive_card.y + 66, 11, sec(app), 0, maxw);
  // Action buttons
  svg_btn(app, cr, lo.btn_power.x, lo.btn_power.y, 32, app.svg.power, "⏻",
          app.hov_power, false, false, true);
  svg_btn(app, cr, lo.btn_eject.x, lo.btn_eject.y, 32, app.svg.eject, "⏏",
          app.hov_eject, false, !drive->is_ejectable() && !drive->is_removable());
  svg_btn(app, cr, lo.btn_settings.x, lo.btn_settings.y, 32, app.svg.settings, "⚙",
          app.hov_settings, false, false);
  svg_btn(app, cr, lo.btn_drive_menu.x, lo.btn_drive_menu.y, 32, app.svg.more, "⋮",
          app.hov_drivemenu, app.menu.open && app.menu.kind == MenuKind::Drive, false);
  // SMART banner (Disks 51 assessment)
  {
    auto r = lo.smart_banner;
    bool sup = drive->smart_supported();
    bool warn = false;
    std::string a = sup ? drive->smart_one_liner_assessment(&warn) : "SMART not available";
    W::RGBA bg = !sup ? W::RGBA{1, 1, 1, 0.04}
                : warn ? W::RGBA{0.85, 0.30, 0.30, 0.14}
                       : W::RGBA{0.30, 0.68, 0.42, 0.12};
    W::fill_rounded(cr, r.x, r.y, r.w, r.h, 8, bg);
    W::text(cr, sup ? (warn ? "●" : "●") : "○", r.x + 10, r.y + 19, 12,
            !sup ? sec(app) : warn ? W::RGB{0.92, 0.35, 0.35} : W::RGB{0.35, 0.75, 0.45},
            0);
    W::text_ellipsis(cr, a, r.x + 26, r.y + 19, 12, txt(app), 0, r.w - 200);
    // Right side: power-on hours + temp when available
    std::string extra;
    if (sup) {
      auto hrs = drive->smart_power_on_hours();
      auto tmp = drive->smart_temperature();
      if (hrs >= 0) extra += std::to_string(hrs) + " h  ·  ";
      if (tmp >= 0) extra += std::to_string(int(tmp)) + "°C";
    }
    if (!extra.empty()) W::text_right(cr, extra, r.x + r.w - 10, r.y + 19, 11, sec(app), 0);
  }
}

// ---- volume bar (Disks 51 proportional, GParted-correct) ----

static void paint_volumes(AppState& app, cairo_t* cr, const L::Layout& lo,
                          const std::shared_ptr<Drive>& drive,
                          const std::vector<std::shared_ptr<Block>>& blocks) {
  W::section_label(cr, app, lo.vol_section.x + T::kCardPad, lo.vol_section.y + 16,
                   "Volumes");
  {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%zu", blocks.size());
    W::text(cr, buf, lo.vol_section.x + T::kCardPad +
                        W::text_w(cr, "Volumes", T::kFsSection, 1) + 8,
            lo.vol_section.y + 16, 12, sec(app), 0);
  }
  W::pill_button(cr, app, lo.btn_add_part.x, lo.btn_add_part.y, lo.btn_add_part.w,
                 lo.btn_add_part.h, "＋ Add Partition", app.hov_add_part, false, false,
                 false, blocks.empty() && !drive);
  // Bar track
  auto bar = lo.vol_bar;
  W::fill_rounded(cr, bar.x, bar.y, bar.w, bar.h, T::kVolBarRadius, {1, 1, 1, 0.06});
  if (blocks.empty()) {
    W::text(cr, drive ? "Empty disk — no partitions" : "No drive selected",
            bar.x + 12, bar.y + 23, 12, sec(app), 0);
  }
  // Segments — strictly proportional, free hatched (no boosting).
  for (size_t i = 0; i < lo.vol_segs.size(); ++i) {
    auto& s = lo.vol_segs[i];
    double gx = T::kVolBarGap / 2.0;
    double px = s.rect.x + (i == 0 ? 0 : gx);
    double pw = s.sw - (i == 0 ? gx : gx * 2) - (i + 1 == lo.vol_segs.size() ? 0 : 0);
    if (pw < 1) continue;
    bool sel = false;
    if (!s.is_free && s.block) {
      for (size_t bi = 0; bi < blocks.size(); ++bi)
        if (blocks[bi] == s.block && int(bi) == app.selected_block) sel = true;
    }
    bool hov = int(i) == app.hov_volseg;
    cairo_save(cr);
    // Clip to bar rounded shape for first/last
    W::fill_rounded(cr, bar.x, bar.y, bar.w, bar.h, T::kVolBarRadius, {0, 0, 0, 0});
    cairo_clip(cr);
    if (s.is_free) {
      W::hatch_rect(cr, px, bar.y + 1, pw, bar.h - 2, {1, 1, 1, 0.05},
                    {app.outlineR, app.outlineG, app.outlineB});
    } else {
      W::RGB c = W::fs_color(s.block->get_fstype(), s.block->get_fsusage());
      double bright = sel ? 1.15 : hov ? 1.10 : 1.0;
      cairo_set_source_rgb(cr, std::min(1.0, c.r * bright), std::min(1.0, c.g * bright),
                           std::min(1.0, c.b * bright));
      // Used-portion overlay for mounted filesystems (Disks 51 allocation bar).
      cairo_rectangle(cr, px, bar.y + 1, pw, bar.h - 2);
      cairo_fill(cr);
      if (s.block->is_mounted()) {
        uint64_t tot = s.block->get_fs_total_bytes();
        uint64_t used = s.block->get_fs_used_bytes();
        if (tot > 0) {
          double f = double(used) / double(tot);
          cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
          cairo_rectangle(cr, px, bar.y + 1, pw * f, bar.h - 2);
          cairo_fill(cr);
        }
      }
      if (s.block->is_encrypted()) {
        // LUKS diagonal stripe hint
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        cairo_set_line_width(cr, 1);
        for (double d = 0; d < pw + bar.h; d += 6) {
          cairo_move_to(cr, px + d, bar.y + bar.h);
          cairo_line_to(cr, px + d + 4, bar.y);
        }
        cairo_stroke(cr);
      }
    }
    cairo_restore(cr);
    // Selection outline
    if (sel) {
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_set_line_width(cr, 2);
      cairo_rectangle(cr, px + 1, bar.y + 2, pw - 2, bar.h - 4);
      cairo_stroke(cr);
    } else if (hov) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
      cairo_set_line_width(cr, 1);
      cairo_rectangle(cr, px + 0.5, bar.y + 1.5, pw - 1, bar.h - 3);
      cairo_stroke(cr);
    }
    // Dividers
    if (i + 1 < lo.vol_segs.size()) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
      cairo_set_line_width(cr, 1);
      cairo_move_to(cr, s.rect.x + s.sw + 0.5, bar.y + 3);
      cairo_line_to(cr, s.rect.x + s.sw + 0.5, bar.y + bar.h - 3);
      cairo_stroke(cr);
    }
  }
  W::stroke_rounded(cr, bar.x + 0.5, bar.y + 0.5, bar.w - 1, bar.h - 1,
                    T::kVolBarRadius, out(app));
  // Legend: selected / hovered segment identity (pixel-clean single line)
  {
    std::string legend;
    if (app.hov_volseg >= 0 && app.hov_volseg < int(lo.vol_segs.size())) {
      auto& s = lo.vol_segs[size_t(app.hov_volseg)];
      legend = s.is_free ? ("Free space — " + size_str(uint64_t(s.sw / bar.w *
                                                                (drive ? drive->get_size() : 1))))
                         : (block_short_name(s.block) + " — " + size_str(s.block->get_size()) +
                            " · " + block_fs_label(s.block));
    } else if (app.selected_block >= 0 && app.selected_block < int(blocks.size())) {
      auto& b = blocks[size_t(app.selected_block)];
      legend = block_short_name(b) + " — " + size_str(b->get_size()) + " · " +
               block_fs_label(b);
      std::string m = block_mount_str(b);
      if (!m.empty()) legend += " · " + m;
    } else if (drive) {
      uint64_t used = 0;
      for (auto& b : blocks) used += b->get_size();
      uint64_t free = drive->get_size() > used ? drive->get_size() - used : 0;
      legend = size_str(free) + " free of " + size_str(drive->get_size());
    }
    W::text_ellipsis(cr, legend, bar.x + 2, bar.y + bar.h + 15, 11, sec(app), 0,
                     bar.w);
  }
}

// ---- block rows (Disks 51 rows + GParted density) ----

static void paint_table(AppState& app, cairo_t* cr, const L::Layout& lo,
                        const std::vector<std::shared_ptr<Block>>&) {
  // Head
  W::text(cr, "PARTITION", lo.table_head.x + 16, lo.table_head.y + 19, T::kFsTableHead,
          sec(app), 1);
  W::text(cr, "CONTENTS", lo.table_head.x + 176, lo.table_head.y + 19, T::kFsTableHead,
          sec(app), 1);
  W::text_right(cr, "SIZE", lo.table_head.x + lo.table_head.w - 200,
                lo.table_head.y + 19, T::kFsTableHead, sec(app), 1);
  W::hairline_h(cr, lo.table_head.x, lo.table_head.x + lo.table_head.w,
                lo.table_head.y + lo.table_head.h - 1, out(app));

  cairo_save(cr);
  // Clip rows to content viewport (below header, above job/status bars).
  int clip_y0 = lo.content.y;
  int clip_y1 = lo.H - T::kStatusH -
                (JobTracker::instance().has_active() ? T::kJobBarH : 0);
  cairo_rectangle(cr, lo.content.x, clip_y0, lo.content.w, clip_y1 - clip_y0);
  cairo_clip(cr);

  for (size_t i = 0; i < lo.rows.size(); ++i) {
    auto& row = lo.rows[i];
    auto r = row.rect;
    r.y -= app.contentScroll;
    if (r.y + r.h < clip_y0 || r.y > clip_y1) continue;
    auto& b = row.block;
    bool sel = int(i) == app.selected_block;
    bool hov = int(i) == app.hov_row;
    // Row bg
    if (sel)
      W::fill_rounded(cr, r.x, r.y + 2, r.w, r.h - 4, 10,
                      {app.accentR, app.accentG, app.accentB, 0.16});
    else if (hov)
      W::fill_rounded(cr, r.x, r.y + 2, r.w, r.h - 4, 10, {1, 1, 1, 0.045});
    else if (i % 2 == 0)
      W::fill_rounded(cr, r.x, r.y + 2, r.w, r.h - 4, 10, {1, 1, 1, 0.018});
    if (sel)
      W::stroke_rounded(cr, r.x + 0.5, r.y + 2.5, r.w - 1, r.h - 5, 10,
                        {app.accentR, app.accentG, app.accentB});
    // Swatch
    W::RGB fc = W::fs_color(b->get_fstype(), b->get_fsusage());
    cairo_set_source_rgb(cr, fc.r, fc.g, fc.b);
    cairo_arc(cr, r.x + 24, r.y + 20, 5, 0, 2 * M_PI);
    cairo_fill(cr);
    // Line 1: name + fs pill ... size right
    W::text_ellipsis(cr, block_short_name(b), r.x + 36, r.y + 21, 13, txt(app), 1, 110);
    {
      double pillx = r.x + 176;
      double pw = 0;
      W::fs_pill(cr, app, pillx, r.y + 10, block_fs_label(b), 96, &pw);
      // Label next to pill
      std::string lbl = b->get_label();
      if (!lbl.empty())
        W::text_ellipsis(cr, "“" + lbl + "”", pillx + pw + 8, r.y + 24, 12, txt(app), 0,
                         120);
    }
    W::text_right(cr, size_str(b->get_size()), r.x + r.w - 200, r.y + 21, 13, txt(app),
                  0);
    // Line 2: type · mount · flags ... usage right
    {
      std::string meta;
      if (b->has_partition()) {
        char nb[16];
        std::snprintf(nb, sizeof(nb), "p%llu", (unsigned long long)b->get_partition_number());
        meta += nb;
        if (!b->get_partition_name().empty()) meta += " · " + b->get_partition_name();
      } else {
        meta += "whole disk";
      }
      std::string m = block_mount_str(b);
      if (!m.empty()) meta += "  ·  " + m;
      std::string fl = partition_flags_str(b);
      if (!fl.empty() && fl != b->get_partition_name()) meta += "  ·  " + fl;
      if (b->is_encrypted() && !b->has_cleartext()) meta += "  ·  locked";
      // UUID tail
      std::string uuid = b->get_uuid();
      if (!uuid.empty() && uuid.size() > 8) meta += "  ·  " + uuid.substr(0, 8) + "…";
      W::text_ellipsis(cr, meta, r.x + 36, r.y + 41, 11, sec(app), 0,
                       r.w - 36 - 320);
    }
    // Usage (mounted only, else "—")
    {
      double ux = r.x + r.w - 200 - 110;
      if (b->is_mounted() && b->get_fs_total_bytes() > 0) {
        double f = double(b->get_fs_used_bytes()) / double(b->get_fs_total_bytes());
        W::usage_bar(cr, ux, r.y + 32, T::kUsageBarW, T::kUsageBarH, f);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d%%", int(f * 100));
        W::text(cr, pct, ux + T::kUsageBarW + 6, r.y + 41, 11, sec(app), 0);
      } else if (b->is_swap()) {
        W::text(cr, b->is_swap_active() ? "active" : "inactive", ux, r.y + 41, 11,
                sec(app), 0);
      }
    }
    // Job overlay (Disks 51 per-row spinner + progress)
    auto job = JobTracker::instance().for_block(b->get_object_path());
    if (job) {
      W::fill_rounded(cr, r.x, r.y + 2, r.w, r.h - 4, 10, {0, 0, 0, 0.45});
      W::spinner(cr, r.x + 24, r.y + r.h / 2, 8,
                 {app.accentR, app.accentG, app.accentB}, app.spinner_phase);
      W::text_ellipsis(cr, job->label, r.x + 40, r.y + r.h / 2 + 4, 12, txt(app), 0,
                       r.w - 160);
      if (job->progress >= 0)
        W::progress_bar(cr, r.x + 16, r.y + r.h - 10, r.w - 32, 4, job->progress,
                        {1, 1, 1, 0.12}, {app.accentR, app.accentG, app.accentB});
    } else {
      // Action cluster (only when row hovered/selected to stay clean)
      if (hov || sel) {
        auto f = b->get_features();
        if (row.mount_btn.w > 0) {
          bool mh = app.hov_row_mount == int(i);
          std::string lbl = (f & FEATURE_CAN_MOUNT)     ? "Mount"
                            : (f & FEATURE_CAN_UNMOUNT) ? "Unmount"
                            : (f & FEATURE_CAN_SWAPOFF) ? "Swapoff"
                            : (f & FEATURE_CAN_SWAPON)  ? "Swapon"
                            : (f & FEATURE_CAN_LOCK)    ? "Lock"
                                                        : "Unlock";
          bool green = (f & FEATURE_CAN_MOUNT) || (f & FEATURE_CAN_SWAPON) ||
                       (f & FEATURE_CAN_UNLOCK);
          bool red = (f & FEATURE_CAN_UNMOUNT) || (f & FEATURE_CAN_LOCK);
          // Adjust rect for scroll
          W::action_button(cr, app, row.mount_btn.x, r.y + (r.h - 30) / 2,
                           row.mount_btn.w, 30, lbl, mh, green, red);
        }
        if (row.lock_btn.w > 0) {
          bool lh = app.hov_row_lock == int(i);
          svg_btn(app, cr, row.lock_btn.x, r.y + (r.h - 32) / 2, 32,
                  (f & FEATURE_CAN_LOCK) ? app.svg.lock : app.svg.unlock,
                  (f & FEATURE_CAN_LOCK) ? "🔒" : "🔓", lh, false, false);
        }
        bool gh = app.hov_row_gear == int(i);
        svg_btn(app, cr, row.gear_btn.x, r.y + (r.h - 32) / 2, 32, app.svg.settings,
                "⚙", gh, false, false);
      }
    }
    W::hairline_h(cr, r.x + 12, r.x + r.w - 12, r.y + r.h - 1, out(app));
  }
  cairo_restore(cr);
}

// ---- details card (GParted device-info parity) ----

static void paint_details(AppState& app, cairo_t* cr, const L::Layout& lo,
                          const std::shared_ptr<Block>& b) {
  if (!b || lo.details_card.w == 0) return;
  auto r = lo.details_card;
  int ry = r.y - app.contentScroll;
  int clip_y1 = lo.H - T::kStatusH -
                (JobTracker::instance().has_active() ? T::kJobBarH : 0);
  if (ry + r.h < lo.content.y || ry > clip_y1) return;
  W::card(cr, app, r.x, ry, r.w, r.h);
  W::text(cr, "Details", r.x + T::kCardPad, ry + 24, 13, txt(app), 1);
  // 2-col grid
  struct KV {
    const char* k;
    std::string v;
  };
  std::string mp = block_mount_str(b);
  std::vector<KV> left = {
      {"Device", b->get_device().empty() ? block_short_name(b) : b->get_device()},
      {"UUID", b->get_uuid().empty() ? "—" : b->get_uuid()},
      {"Label", b->get_label().empty() ? "—" : b->get_label()},
      {"Type", block_fs_label(b) + (b->get_fsversion().empty() ? "" : " " + b->get_fsversion())},
  };
  std::vector<KV> right = {
      {"Size", size_str_full(b->get_size())},
      {"Mounted at", mp.empty() ? "—" : mp},
      {"Usage", b->is_mounted() && b->get_fs_total_bytes() > 0
                    ? size_str(b->get_fs_used_bytes()) + " / " +
                          size_str(b->get_fs_total_bytes())
                    : "—"},
      {"Partition", b->has_partition() ? ("#" + std::to_string(b->get_partition_number()) +
                                          " · " + b->get_partition_type_guid())
                                       : "—"},
  };
  auto col = [&](double x, const std::vector<KV>& kvs) {
    double y = ry + 48;
    for (auto& kv : kvs) {
      W::text(cr, kv.k, x, y, 11, sec(app), 0);
      W::text_ellipsis(cr, kv.v, x + 92, y, 12, txt(app), 0,
                       (r.w - 32) / 2 - 100);
      y += 20;
    }
  };
  col(r.x + T::kCardPad, left);
  col(r.x + r.w / 2, right);
  // Buttons
  auto btn = [&](L::Rect br, const char* label, bool hov, bool dis = false) {
    // br is unscrolled; adjust
    W::pill_button(cr, app, br.x, ry + br.h * 0 + (lo.details_card.h - T::kCardPad - 30),
                   br.w, 30, label, hov, false, false, false, dis);
  };
  btn(lo.btn_mount_opts, "Mount Options", false);
  btn(lo.btn_edit_part, "Edit Partition", false);
  btn(lo.btn_resize, "Resize", false,
      !(b->get_features() & FEATURE_RESIZE_PARTITION));
  btn(lo.btn_check, "Check", false,
      !(b->get_features() & FEATURE_CHECK_FILESYSTEM));
  btn(lo.btn_ownership, "Ownership", false,
      !(b->get_features() & FEATURE_TAKE_OWNERSHIP));
}

// ---- job bar + status ----

static void paint_jobbar(AppState& app, cairo_t* cr, const L::Layout& lo) {
  if (lo.job_bar.h == 0) return;
  W::fill_rounded(cr, lo.job_bar.x, lo.job_bar.y, lo.job_bar.w, lo.job_bar.h, 0,
                  {app.accentR, app.accentG, app.accentB, 0.10});
  W::hairline_h(cr, 0, lo.W, lo.job_bar.y, out(app));
  auto act = JobTracker::instance().active();
  if (!act.empty()) {
    W::spinner(cr, 20, lo.job_bar.y + 15, 8, {app.accentR, app.accentG, app.accentB},
               app.spinner_phase);
    W::text_ellipsis(cr, act[0]->label, 36, lo.job_bar.y + 19, 12, txt(app), 0,
                     lo.W - 120);
    if (act[0]->progress >= 0)
      W::progress_bar(cr, lo.W - 220, lo.job_bar.y + 11, 160, 8, act[0]->progress,
                      {1, 1, 1, 0.12}, {app.accentR, app.accentG, app.accentB});
    else
      W::text(cr, "working…", lo.W - 80, lo.job_bar.y + 19, 11, sec(app), 0);
  }
}

static void paint_status(AppState& app, cairo_t* cr, const L::Layout& lo,
                         size_t ndrives, size_t nvols) {
  W::fill_rounded(cr, lo.status_bar.x, lo.status_bar.y, lo.status_bar.w,
                  lo.status_bar.h, 0, {app.surfaceR, app.surfaceG, app.surfaceB, 1.0});
  W::hairline_h(cr, 0, lo.W, lo.status_bar.y, out(app));
  std::string left = std::to_string(ndrives) + (ndrives == 1 ? " drive" : " drives");
  left += "  ·  " + std::to_string(nvols) + (nvols == 1 ? " volume" : " volumes");
  if (!app.statusText.empty()) left += "  ·  " + app.statusText;
  W::text_ellipsis(cr, left, 12, lo.status_bar.y + 20, 11, sec(app), 0, lo.W * 0.7);
  W::text_right(cr, "Disks 51 model · UDisks2", lo.W - 12, lo.status_bar.y + 20, 11,
                sec(app), 0);
}

// ---- empty states ----

static void paint_empty(AppState& app, cairo_t* cr, const L::Layout& lo) {
  double cx = lo.content.x + lo.content.w / 2;
  double cy = lo.content.y + lo.content.h / 2 - 20;
  // Illustration ring
  cairo_set_source_rgba(cr, app.accentR, app.accentG, app.accentB, 0.25);
  cairo_set_line_width(cr, 2);
  cairo_arc(cr, cx, cy - 30, 34, 0, 2 * M_PI);
  cairo_stroke(cr);
  if (app.svg.hard_drive)
    icons::paint_svg(cr, app.svg.hard_drive, cx - 20, cy - 50, 40,
                     {app.accentR, app.accentG, app.accentB});
  else
    W::text(cr, "◉", cx - 13, cy - 8, 26, {app.accentR, app.accentG, app.accentB}, 0);
  W::text(cr, "No drives found", cx - 62, cy + 32, 15, txt(app), 1);
  W::text(cr, "Attach a disk image or connect a drive", cx - 118, cy + 52, 12, sec(app),
          0);
}

// ---- main ----

void paint_main(AppState& app, cairo_t* cr, const L::Layout& lo,
                const std::vector<std::shared_ptr<Drive>>& drives,
                const std::vector<std::shared_ptr<Block>>& blocks) {
  paint_header(app, cr, lo, drives);
  paint_sidebar(app, cr, lo, drives);
  // Content bg
  W::fill_rounded(cr, lo.content.x, lo.content.y, lo.content.w, lo.content.h, 0,
                  {app.bgR, app.bgG, app.bgB, 1.0});
  if (drives.empty()) {
    paint_empty(app, cr, lo);
  } else {
    auto drive = (app.selected_drive >= 0 && app.selected_drive < int(drives.size()))
                     ? drives[size_t(app.selected_drive)]
                     : nullptr;
    if (!drive) {
      paint_empty(app, cr, lo);
    } else {
      // Vertical scroll: translate content sections.
      cairo_save(cr);
      int clip_y1 = lo.H - T::kStatusH -
                    (JobTracker::instance().has_active() ? T::kJobBarH : 0);
      cairo_rectangle(cr, lo.content.x, lo.content.y, lo.content.w,
                      clip_y1 - lo.content.y);
      cairo_clip(cr);
      // NOTE: layout rects were computed with scroll already applied for
      // rows/details; drive card + volumes are above the fold and painted
      // with manual offset so short lists don't need scrolling.
      cairo_translate(cr, 0, -app.contentScroll);
      paint_drive_card(app, cr, lo, drive);
      paint_volumes(app, cr, lo, drive, blocks);
      cairo_restore(cr);
      paint_table(app, cr, lo, blocks);
      // Details (handles its own scroll offset)
      std::shared_ptr<Block> sel;
      if (app.selected_block >= 0 && app.selected_block < int(blocks.size()))
        sel = blocks[size_t(app.selected_block)];
      if (sel) paint_details(app, cr, lo, sel);
      // Content scrollbar
      int view_h = lo.content.h;
      if (app.contentH > view_h && view_h > 0) {
        int sh = std::max(view_h * view_h / app.contentH, 24);
        int sy = lo.content.y + (view_h - sh) * app.contentScroll /
                                        (app.contentH - view_h);
        W::fill_rounded(cr, lo.content.x + lo.content.w - 8, sy, 4, sh, 2,
                        {1, 1, 1, 0.25});
      }
    }
  }
  paint_jobbar(app, cr, lo);
  size_t nvols = 0;
  // Count visible volumes for status (cheap: current drive only — matches selection ctx).
  {
    auto drive = (app.selected_drive >= 0 && app.selected_drive < int(drives.size()))
                     ? drives[size_t(app.selected_drive)]
                     : nullptr;
    if (drive) nvols = blocks.size();
  }
  paint_status(app, cr, lo, drives.size(), nvols);
}

void paint_menu(AppState& app, cairo_t* cr, const L::Layout& lo) {
  if (!app.menu.open || app.menu.items.empty()) return;
  auto r = lo.menu_rect;
  // Scrim-less floating menu (Adwaita popover)
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  W::fill_rounded(cr, r.x + 2, r.y + 3, r.w, r.h, T::kMenuRadius, {0, 0, 0, 0.35});
  cairo_restore(cr);
  W::fill_rounded(cr, r.x, r.y, r.w, r.h, T::kMenuRadius,
                  {app.surfaceR, app.surfaceG, app.surfaceB, 1.0});
  W::stroke_rounded(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, T::kMenuRadius,
                    out(app));
  double y = r.y + T::kMenuPad;
  for (size_t i = 0; i < app.menu.items.size(); ++i) {
    auto& it = app.menu.items[i];
    bool hov = int(i) == app.menu.hover;
    if (hov && it.enabled)
      W::fill_rounded(cr, r.x + 4, y, r.w - 8, T::kMenuItemH, 7,
                      {app.accentR, app.accentG, app.accentB, 0.20});
    W::RGB fg = it.enabled ? (it.danger ? W::RGB{0.95, 0.55, 0.55} : txt(app))
                           : sec(app);
    if (!it.glyph.empty()) {
      auto* mi = icons::menu_icon_for_glyph(app.svg, it.glyph);
      if (mi)
        icons::paint_svg(cr, mi, r.x + 11, y + (T::kMenuItemH - 16) / 2.0, 16,
                         {fg.r, fg.g, fg.b});
      else
        W::text(cr, it.glyph, r.x + 14, y + 22, 13, fg, 0);
    }
    W::text_ellipsis(cr, it.label, r.x + 36, y + 22, 13, fg, 0, r.w - 48);
    y += T::kMenuItemH;
    if (it.separator_after && i + 1 < app.menu.items.size()) {
      W::hairline_h(cr, r.x + 12, r.x + r.w - 12, y - 1, out(app));
    }
  }
}

void paint_dialogs(AppState& app, cairo_t* cr) {
  if (app.fmtDiskDlg && app.fmtDiskDlg->open) draw_format_disk_dialog(app, cr);
  if (app.fmtDlg && app.fmtDlg->open) draw_format_volume_dialog(app, cr);
  if (app.createPartDlg && app.createPartDlg->open) draw_create_partition_dialog(app, cr);
  if (app.resizeDlg && app.resizeDlg->open) draw_resize_dialog(app, cr);
  if (app.unlockDlg && app.unlockDlg->open) draw_unlock_dialog(app, cr);
  if (app.smartDlg && app.smartDlg->open) draw_smart_dialog(app, cr);
  if (app.benchDlg && app.benchDlg->open) draw_benchmark_dialog(app, cr);
  if (app.mountOptsDlg && app.mountOptsDlg->open) draw_mount_options_dialog(app, cr);
  if (app.imageDlg && app.imageDlg->open) draw_image_dialog(app, cr);
  if (app.attachDlg && app.attachDlg->open) draw_attach_dialog(app, cr);
  if (app.drvSettingsDlg && app.drvSettingsDlg->open)
    draw_drive_settings_dialog(app, cr);
}

}  // namespace eh::disks
