#include "ux/disks/ui/widgets.hpp"

#include "ux/disks/ui/theme.hpp"

#include <algorithm>
#include <cmath>

#include "ux/disks/app_types.hpp"

namespace eh::disks::widgets {
namespace {

void set_rgba(cairo_t* cr, RGBA c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }
void set_rgb(cairo_t* cr, RGB c) { cairo_set_source_rgb(cr, c.r, c.g, c.b); }

void rr_path(cairo_t* cr, double x, double y, double w, double h, double r) {
  if (r < 0) r = 0;
  if (w <= 0 || h <= 0) return;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

void select_font(cairo_t* cr, int px, int weight) {
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         weight ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, px);
}

}  // namespace

void fill_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                  RGBA c) {
  rr_path(cr, x, y, w, h, r);
  set_rgba(cr, c);
  cairo_fill(cr);
}

void stroke_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                    RGB c, double lw) {
  rr_path(cr, x, y, w, h, r);
  set_rgb(cr, c);
  cairo_set_line_width(cr, lw);
  cairo_stroke(cr);
}

void hairline_h(cairo_t* cr, double x0, double x1, double y, RGB c) {
  set_rgb(cr, c);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x0, y + 0.5);
  cairo_line_to(cr, x1, y + 0.5);
  cairo_stroke(cr);
}

void hairline_v(cairo_t* cr, double x, double y0, double y1, RGB c) {
  set_rgb(cr, c);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x + 0.5, y0);
  cairo_line_to(cr, x + 0.5, y1);
  cairo_stroke(cr);
}

void text(cairo_t* cr, std::string_view s, double x, double baseline_y, int px,
          RGB c, int weight) {
  if (s.empty()) return;
  select_font(cr, px, weight);
  set_rgb(cr, c);
  cairo_move_to(cr, x, baseline_y);
  cairo_show_text(cr, std::string(s).c_str());
}

double text_w(cairo_t* cr, std::string_view s, int px, int weight) {
  if (s.empty()) return 0;
  select_font(cr, px, weight);
  cairo_text_extents_t te;
  cairo_text_extents(cr, std::string(s).c_str(), &te);
  return te.width;
}

std::string ellipsize(cairo_t* cr, std::string_view s, int px, int weight,
                      double max_w) {
  std::string full(s);
  if (full.empty() || max_w <= 0) return "";
  select_font(cr, px, weight);
  cairo_text_extents_t te;
  cairo_text_extents(cr, full.c_str(), &te);
  if (te.width <= max_w) return full;
  std::string out = full;
  while (out.size() > 4) {
    out.pop_back();
    std::string cand = out + "…";
    cairo_text_extents(cr, cand.c_str(), &te);
    if (te.width <= max_w) return cand;
  }
  return "…";
}

void text_ellipsis(cairo_t* cr, std::string_view s, double x, double baseline_y,
                   int px, RGB c, int weight, double max_w) {
  std::string d = ellipsize(cr, s, px, weight, max_w);
  text(cr, d, x, baseline_y, px, c, weight);
}

void text_right(cairo_t* cr, std::string_view s, double right_x, double baseline_y,
                int px, RGB c, int weight) {
  double w = text_w(cr, s, px, weight);
  text(cr, s, right_x - w, baseline_y, px, c, weight);
}

bool pill_button(cairo_t* cr, const AppState& app, double x, double y, double w,
                 double h, std::string_view label, bool hover, bool pressed,
                 bool primary, bool destructive, bool disabled) {
  (void)pressed;
  double alpha = disabled ? 0.38 : 1.0;
  cairo_save(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  if (destructive) {
    RGBA bg{0.62, 0.16, 0.16, (hover && !disabled ? 1.0 : 0.85) * alpha};
    fill_rounded(cr, x, y, w, h, 8, bg);
    if (hover && !disabled)
      stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, 8, {0.85, 0.35, 0.35});
  } else if (primary) {
    RGBA bg{app.accentR, app.accentG, app.accentB, (hover && !disabled ? 0.95 : 0.75) * alpha};
    fill_rounded(cr, x, y, w, h, 8, bg);
  } else {
    RGBA bg = hover && !disabled ? RGBA{1, 1, 1, 0.10 * alpha} : RGBA{1, 1, 1, 0.05 * alpha};
    fill_rounded(cr, x, y, w, h, 8, bg);
    stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, 8,
                   {app.outlineR, app.outlineG, app.outlineB});
  }
  RGB fg = destructive ? RGB{0.98, 0.80, 0.80}
           : primary   ? RGB{1, 1, 1}
                       : RGB{app.textR, app.textG, app.textB};
  if (disabled) fg = {app.textSecR, app.textSecG, app.textSecB};
  select_font(cr, 13, primary || destructive ? 1 : 0);
  cairo_text_extents_t te;
  std::string ls(label);
  cairo_text_extents(cr, ls.c_str(), &te);
  set_rgb(cr, {fg.r, fg.g, fg.b});
  cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, alpha);
  cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, y + (h + te.height) / 2 - 1);
  cairo_show_text(cr, ls.c_str());
  cairo_restore(cr);
  return !disabled;
}

bool icon_button(cairo_t* cr, const AppState& app, double x, double y, double s,
                 std::string_view glyph, bool hover, bool active,
                 bool disabled, bool danger) {
  double alpha = disabled ? 0.38 : 1.0;
  if (hover && !disabled)
    fill_rounded(cr, x, y, s, s, 8, {1, 1, 1, 0.08 * alpha});
  else if (active)
    fill_rounded(cr, x, y, s, s, 8,
                 {app.accentR, app.accentG, app.accentB, 0.22 * alpha});
  RGB fg = danger ? RGB{0.95, 0.55, 0.55} : RGB{app.textR, app.textG, app.textB};
  if (disabled) fg = {app.textSecR, app.textSecG, app.textSecB};
  select_font(cr, 16, 0);
  std::string g(glyph);
  cairo_text_extents_t te;
  cairo_text_extents(cr, g.c_str(), &te);
  cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, alpha);
  cairo_move_to(cr, x + (s - te.width) / 2 - te.x_bearing,
                y + (s - te.height) / 2 + te.height);
  cairo_show_text(cr, g.c_str());
  return !disabled;
}

bool action_button(cairo_t* cr, const AppState& app, double x, double y, double w,
                   double h, std::string_view label, bool hover, bool tone_green,
                   bool tone_red) {
  RGBA bg;
  RGB fg;
  if (tone_green) {
    bg = hover ? RGBA{0.22, 0.42, 0.28, 1.0} : RGBA{0.16, 0.32, 0.21, 1.0};
    fg = {0.75, 0.95, 0.78};
  } else if (tone_red) {
    bg = hover ? RGBA{0.42, 0.22, 0.22, 1.0} : RGBA{0.32, 0.17, 0.17, 1.0};
    fg = {0.95, 0.75, 0.75};
  } else {
    bg = hover ? RGBA{1, 1, 1, 0.10} : RGBA{1, 1, 1, 0.05};
    fg = {app.textR, app.textG, app.textB};
  }
  fill_rounded(cr, x, y, w, h, 7, bg);
  select_font(cr, 12, 0);
  std::string ls(label);
  cairo_text_extents_t te;
  cairo_text_extents(cr, ls.c_str(), &te);
  set_rgb(cr, fg);
  cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, y + (h + te.height) / 2 - 1);
  cairo_show_text(cr, ls.c_str());
  return true;
}

void checkbox(cairo_t* cr, const AppState& app, double x, double y, double s,
              bool on, bool hover) {
  fill_rounded(cr, x, y, s, s, 5,
               hover ? RGBA{1, 1, 1, 0.12} : RGBA{1, 1, 1, 0.06});
  stroke_rounded(cr, x + 0.5, y + 0.5, s - 1, s - 1, 5,
                 {app.outlineR, app.outlineG, app.outlineB});
  if (on) {
    set_rgb(cr, {app.accentR, app.accentG, app.accentB});
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x + s * 0.22, y + s * 0.52);
    cairo_line_to(cr, x + s * 0.45, y + s * 0.74);
    cairo_line_to(cr, x + s * 0.78, y + s * 0.28);
    cairo_stroke(cr);
  }
}

void radio_dot(cairo_t* cr, const AppState& app, double cx, double cy, double r,
               bool on) {
  cairo_set_line_width(cr, 1.5);
  if (on) {
    set_rgb(cr, {app.accentR, app.accentG, app.accentB});
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_fill(cr);
    set_rgb(cr, {1, 1, 1});
    cairo_arc(cr, cx, cy, r * 0.42, 0, 2 * M_PI);
    cairo_fill(cr);
  } else {
    set_rgb(cr, {app.outlineR, app.outlineG, app.outlineB});
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_stroke(cr);
  }
}

void toggle(cairo_t* cr, const AppState& app, double x, double y, double w,
            double h, bool on, bool hover) {
  double r = h / 2;
  RGBA bg = on ? RGBA{app.accentR, app.accentG, app.accentB, hover ? 1.0 : 0.85}
               : RGBA{1, 1, 1, hover ? 0.14 : 0.08};
  fill_rounded(cr, x, y, w, h, r, bg);
  double kx = on ? x + w - h + 2 : x + 2;
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_arc(cr, kx + (h - 4) / 2, y + h / 2, (h - 4) / 2, 0, 2 * M_PI);
  cairo_fill(cr);
}

void fs_pill(cairo_t* cr, const AppState& app, double x, double y,
             std::string_view fs, double max_w, double* out_w) {
  std::string s(fs.empty() ? "—" : std::string(fs));
  select_font(cr, 11, 0);
  cairo_text_extents_t te;
  cairo_text_extents(cr, s.c_str(), &te);
  double w = std::min(max_w, te.width + 16);
  RGB c = fs_color(fs, "");
  fill_rounded(cr, x, y, w, 20, 10, {c.r, c.g, c.b, 0.18});
  stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, 19, 10, {c.r, c.g, c.b});
  std::string d = ellipsize(cr, s, 11, 0, w - 12);
  cairo_text_extents(cr, d.c_str(), &te);
  set_rgb(cr, {app.textR, app.textG, app.textB});
  cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, y + 14);
  cairo_show_text(cr, d.c_str());
  if (out_w) *out_w = w;
}

void badge(cairo_t* cr, const AppState& app, double x, double y,
           std::string_view label, RGB bg, RGB fg, double* out_w) {
  (void)app;
  std::string s(label);
  select_font(cr, 10, 1);
  cairo_text_extents_t te;
  cairo_text_extents(cr, s.c_str(), &te);
  double w = te.width + 14;
  fill_rounded(cr, x, y, w, 18, 9, {bg.r, bg.g, bg.b, 1.0});
  set_rgb(cr, fg);
  cairo_move_to(cr, x + 7, y + 13);
  cairo_show_text(cr, s.c_str());
  if (out_w) *out_w = w;
}

void usage_bar(cairo_t* cr, double x, double y, double w, double h, double frac) {
  frac = std::clamp(frac, 0.0, 1.0);
  fill_rounded(cr, x, y, w, h, h / 2, {1, 1, 1, 0.10});
  if (frac > 0) {
    RGB f = frac < 0.75 ? RGB{0.32, 0.68, 0.42}
            : frac < 0.90 ? RGB{0.85, 0.72, 0.28}
                          : RGB{0.85, 0.32, 0.32};
    double fw = std::max(h, w * frac);
    cairo_save(cr);
    rr_path(cr, x, y, w, h, h / 2);
    cairo_clip(cr);
    set_rgb(cr, f);
    cairo_rectangle(cr, x, y, fw, h);
    cairo_fill(cr);
    cairo_restore(cr);
  }
}

void hatch_rect(cairo_t* cr, double x, double y, double w, double h, RGBA base,
                RGB line) {
  set_rgba(cr, base);
  cairo_rectangle(cr, x, y, w, h);
  cairo_fill(cr);
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  set_rgb(cr, line);
  cairo_set_line_width(cr, 1.0);
  for (double d = -h; d < w + h; d += 7) {
    cairo_move_to(cr, x + d, y + h);
    cairo_line_to(cr, x + d + h, y);
  }
  cairo_stroke(cr);
  cairo_restore(cr);
}

void spinner(cairo_t* cr, double cx, double cy, double r, RGB c, double phase) {
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  for (int i = 0; i < 8; ++i) {
    double a = phase + i * M_PI / 4;
    double alpha = 0.15 + 0.85 * (i / 7.0);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx + std::cos(a) * r * 0.55, cy + std::sin(a) * r * 0.55);
    cairo_line_to(cr, cx + std::cos(a) * r, cy + std::sin(a) * r);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

void progress_bar(cairo_t* cr, double x, double y, double w, double h,
                  double frac, RGBA track, RGB fill) {
  fill_rounded(cr, x, y, w, h, h / 2, track);
  if (frac >= 0) {
    double fw = std::clamp(frac, 0.0, 1.0) * w;
    if (fw > 0.5) {
      cairo_save(cr);
      rr_path(cr, x, y, w, h, h / 2);
      cairo_clip(cr);
      set_rgb(cr, fill);
      cairo_rectangle(cr, x, y, fw, h);
      cairo_fill(cr);
      cairo_restore(cr);
    }
  }
}

void card(cairo_t* cr, const AppState& app, double x, double y, double w, double h) {
  fill_rounded(cr, x, y, w, h, 12,
               {app.surfaceR, app.surfaceG, app.surfaceB, 1.0});
  stroke_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, 12,
                 {app.outlineR, app.outlineG, app.outlineB});
}

void section_label(cairo_t* cr, const AppState& app, double x, double baseline_y,
                   std::string_view s) {
  text(cr, s, x, baseline_y, 14, {app.textR, app.textG, app.textB}, 1);
}

RGB fs_color(std::string_view fstype, std::string_view usage) {
  // GParted-inspired, tuned for dark Adwaita.
  if (usage == "crypto" || fstype == "crypto_LUKS") return {0.55, 0.55, 0.60};
  if (fstype == "ext4" || fstype == "ext3" || fstype == "ext2") return {0.30, 0.62, 0.88};
  if (fstype == "btrfs") return {0.35, 0.72, 0.45};
  if (fstype == "xfs") return {0.55, 0.75, 0.30};
  if (fstype == "ntfs") return {0.45, 0.55, 0.90};
  if (fstype == "vfat" || fstype == "exfat") return {0.88, 0.72, 0.28};
  if (fstype == "f2fs") return {0.50, 0.70, 0.75};
  if (fstype == "swap" || usage == "other" || fstype == "linux-swap") return {0.75, 0.45, 0.80};
  if (fstype == "LVM2_member") return {0.80, 0.50, 0.25};
  if (fstype == "iso9660" || fstype == "udf") return {0.70, 0.45, 0.30};
  if (fstype == "zfs_member") return {0.30, 0.65, 0.70};
  if (fstype == "" || fstype == "—") return {0.45, 0.45, 0.48};
  return {0.55, 0.50, 0.75};
}

}  // namespace eh::disks::widgets
