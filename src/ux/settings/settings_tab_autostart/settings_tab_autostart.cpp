#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"
#include "services/autostart/autostart_service.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/default_apps/settings_default_apps.hpp"
#include "ux/settings/settings_tab_autostart/settings_tab_autostart.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"

#include <filesystem>

extern void draw(App& app);
extern void save_settings(const Settings& s);

// New layout (spacious, modern).
static constexpr int kRowHt    = 66;
static constexpr int kBadgeSz  = 40;
static constexpr int kPillW    = 46;
static constexpr int kPillH    = 22;
static constexpr int kActBtn   = 30;

static const float kDotColors[][3] = {
  {0.133f, 0.773f, 0.369f},
  {0.055f, 0.647f, 0.914f},
  {0.961f, 0.620f, 0.043f},
  {0.925f, 0.286f, 0.600f},
  {0.545f, 0.361f, 0.965f},
};

// Icon cache for app browser.
namespace {
eh::icons::IconCache s_app_browser_icon_cache;

namespace fs = std::filesystem;

[[nodiscard]] std::string da_strip_desktop_suffix(const std::string& id) {
  constexpr std::string_view suf = ".desktop";
  if (id.size() > suf.size() && id.compare(id.size() - suf.size(), suf.size(), suf) == 0)
    return id.substr(0, id.size() - suf.size());
  return id;
}

[[nodiscard]] const std::string& da_field_ref(const eh::config::DefaultAppsSettings& d, int i) {
  switch (i) {
    case 0: return d.web;
    case 1: return d.mail;
    case 2: return d.calendar;
    case 3: return d.fileManager;
    case 4: return d.terminal;
    case 5: return d.music;
    case 6: return d.video;
    case 7: return d.images;
    case 8: return d.pdf;
    default: return d.web;
  }
}

[[nodiscard]] int da_count_configured(const eh::config::DefaultAppsSettings& d) {
  int n = 0;
  for (int i = 0; i < eh::settings::default_apps::kNumCategories; ++i) {
    if (!da_field_ref(d, i).empty()) ++n;
  }
  return n;
}

[[nodiscard]] bool da_is_autostarted(const std::vector<eh::autostart::AutostartUiEntry>& entries,
                                      const std::string& desktop_id) {
  std::string stem = da_strip_desktop_suffix(desktop_id);
  for (const auto& e : entries) {
    if (e.stem == stem) return e.enabled;
  }
  return false;
}

[[nodiscard]] std::string da_resolve_app_name(const std::string& desktop_id) {
  if (desktop_id.empty()) return {};
  for (const auto& e : eh::app_drawer::get_cached_entries()) {
    fs::path p(e.path);
    if (p.filename().string() == desktop_id) {
      if (!e.name.empty()) return e.name;
      return da_strip_desktop_suffix(desktop_id);
    }
  }
  return da_strip_desktop_suffix(desktop_id);
}

static void da_toggle_autostart(App& app, const std::string& desktop_id, bool enable) {
  std::string stem = da_strip_desktop_suffix(desktop_id);
  if (stem.empty()) return;

  if (enable) {
    bool found = false;
    auto all = eh::autostart::scan_autostart_entries();
    for (const auto& e : all) {
      fs::path p(e.desktopPath);
      std::string f = p.filename().string();
      if (f.size() > 8 && f.compare(f.size() - 8, 8, ".desktop") == 0) f.resize(f.size() - 8);
      if (f == stem) { found = true; break; }
    }

    if (!found) {
      for (const auto& de : eh::app_drawer::get_cached_entries()) {
        fs::path p(de.path);
        if (p.filename().string() == desktop_id) {
          eh::autostart::create_autostart_entry(stem, de.name, de.exec, de.icon, 0);
          break;
        }
      }
    }
    eh::autostart::set_autostart_enabled(stem, true);
  } else {
    eh::autostart::remove_autostart_override(stem);
  }

  app.autostartNeedsRefresh = true;
}

} // anonymous namespace

// Raw pill toggle.
static void raw_toggle(cairo_t* cr, int x, int cy, bool on,
                       float tr, float tg, float tb) {
  const int px = x;
  const int py = cy + (kRowHt - kPillH) / 2;
  float br, bg, bb, ba, kr, kg, kb, ka;
  if (on) {
    br = 0.133f; bg = 0.773f; bb = 0.369f; ba = 0.88f;
    kr = 1.0f;   kg = 1.0f;   kb = 1.0f;   ka = 1.0f;
  } else {
    br = tr; bg = tg; bb = tb; ba = 0.10f;
    kr = tr; kg = tg; kb = tb; ka = 0.30f;
  }
  cairo_set_source_rgba(cr, br, bg, bb, ba);
  cairo_round_rect(cr, px, py, kPillW, kPillH, kPillH * 0.5f);
  cairo_fill(cr);
  const int knobR = kPillH - 4;
  const int knobX = on ? px + kPillW - kPillH + 2 : px + 2;
  cairo_set_source_rgba(cr, kr, kg, kb, ka);
  cairo_arc(cr, knobX + knobR * 0.5, py + kPillH * 0.5, knobR * 0.5, 0, 2 * M_PI);
  cairo_fill(cr);
}

// Colored badge.
static void colored_badge(cairo_t* cr, int x, int cy, int idx) {
  const auto& c = kDotColors[static_cast<size_t>(idx) % 5];
  const int bx = x;
  const int by = cy + (kRowHt - kBadgeSz) / 2;
  cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.18f);
  cairo_round_rect(cr, bx, by, kBadgeSz, kBadgeSz, kBadgeSz * 0.25f);
  cairo_fill(cr);
  material_symbols_draw_glyph(cr, bx + kBadgeSz * 0.5, by + kBadgeSz * 0.5,
                              18.0, "apps", c[0], c[1], c[2], 0.80f);
}

// Icon button.
static void icon_btn(cairo_t* cr, int x, int cy, const char* glyph,
                     float r, float g, float b, float a,
                     bool hover, bool accent) {
  const int y = cy + (kRowHt - kActBtn) / 2;
  if (hover) {
    m3::Box hb;
    if (accent) hb.setColor(r, g, b, 0.18f);
    else        hb.setColor(r, g, b, 0.10f);
    hb.setRadius(6.0f);
    hb.setGeometry(static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(kActBtn), static_cast<float>(kActBtn));
    hb.paint(cr);
  }
  material_symbols_draw_glyph(cr, x + kActBtn * 0.5, y + kActBtn * 0.5 + 0.5,
                              17.0, glyph, r, g, b, a);
}

// Text field helper.
static void draw_input_label(cairo_t* cr, int x, int y, int h,
                             const std::string& text,
                             const char* hint,
                             float r, float g, float b, float a) {
  m3::Label lbl;
  lbl.setFontSize(13.0f);
  lbl.setFontWeight(400);
  lbl.setColor(r, g, b, a);
  float tw, th;
  if (text.empty()) {
    lbl.setText(hint);
    lbl.setColor(r, g, b, a * 0.33f);
  } else {
    lbl.setText(text.c_str());
  }
  lbl.measureExtents(tw, th);
  lbl.paintAt(cr, static_cast<float>(x + 10),
              static_cast<float>(y) + (static_cast<float>(h) - th) * 0.5f);
}

static void form_text_field(cairo_t* cr, int x, int y, int w, int h,
                            const std::string& text, bool active,
                            const char* hint,
                            float tr, float tg, float tb,
                            float ar, float ag, float ab,
                            float sr, float sg, float sb) {
  m3::Box fbg;
  fbg.setColor(sr, sg, sb, 0.50f);
  fbg.setRadius(7.0f);
  fbg.setGeometry(static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h));
  fbg.paint(cr);
  cairo_set_source_rgba(cr, tr, tg, tb, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_round_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, 7.0);
  cairo_stroke(cr);
  if (active) {
    cairo_set_source_rgba(cr, ar, ag, ab, 0.60);
    cairo_set_line_width(cr, 1.5);
    cairo_round_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, 7.0);
    cairo_stroke(cr);
  }
  draw_input_label(cr, x, y, h, text, hint, tr, tg, tb, 0.85f);
}

// Form overlay.
static void draw_form_card(App& app, cairo_t* cr, double) {
  const int fw = 420, fh = 350;
  const int fx = (app.width - fw) / 2;
  const int fy = std::max(40, (app.height - fh) / 3);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);

  settings_card(app, cr, fx, fy, fw, fh, 1.0);

  float ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob);

  const int px = fx + kCardPad;
  const int pw = fw - kCardPad * 2;
  const int fh_ = 34;
  const int lblW = 110;

  settings_show_text(cr, px, fy + 28, app.autostartEditMode ? "Edit Entry" : "Add Entry",
                     18.f, 600, tr, tg, tb, 0.92f);

  int yy = fy + 66;

  // Name row.
  settings_show_text(cr, px, yy + fh_ * 0.5 + 2, "Name", 13.f, 500, tr, tg, tb, 0.65f);
  {
    int tfx = px + lblW;
    int tfw = pw - lblW;
    form_text_field(cr, tfx, yy, tfw, fh_, app.autostartEditName,
                    app.autostartActiveField == AutostartField::Name,
                    "e.g. Firefox", tr, tg, tb, ar, ag, ab, sr, sg, sb);
  }
  yy += fh_ + 18;

  // Command row + browse.
  settings_show_text(cr, px, yy + fh_ * 0.5 + 2, "Command", 13.f, 500, tr, tg, tb, 0.65f);
  int tfx = px + lblW;
  int tfw = pw - lblW;
  int cmdW = tfw - kActBtn - 10;
  form_text_field(cr, tfx, yy, cmdW, fh_, app.autostartEditExec,
                  app.autostartActiveField == AutostartField::Exec,
                  "e.g. firefox", tr, tg, tb, ar, ag, ab, sr, sg, sb);
  int browseX = tfx + cmdW + 10;
  {
    bool bh = app.pointerX >= browseX && app.pointerX < browseX + kActBtn &&
              app.pointerY >= yy && app.pointerY < yy + fh_;
    m3::Box fbg;
    fbg.setColor(ar, ag, ab, bh ? 0.22f : 0.0f);
    fbg.setRadius(7.0f);
    fbg.setGeometry(static_cast<float>(browseX), static_cast<float>(yy),
                    static_cast<float>(kActBtn), static_cast<float>(fh_));
    fbg.paint(cr);
    cairo_set_source_rgba(cr, ar, ag, ab, 0.45);
    cairo_set_line_width(cr, 1.0);
    cairo_round_rect(cr, browseX + 0.5, yy + 0.5, kActBtn - 1.0, fh_ - 1.0, 7.0);
    cairo_stroke(cr);
    material_symbols_draw_glyph(cr, browseX + kActBtn * 0.5, yy + fh_ * 0.5,
                                16.0, "apps", ar, ag, ab, 0.75f);
  }
  yy += fh_ + 24;

  // Start delay (text input + unit label).
  settings_show_text(cr, px, yy + fh_ * 0.5 + 2, "Start delay", 13.f, 500, tr, tg, tb, 0.65f);
  {
    int dlyX = px + lblW;
    int dlyW = 70;
    form_text_field(cr, dlyX, yy, dlyW, fh_, app.autostartEditIcon,
                    app.autostartActiveField == AutostartField::Delay,
                    "0", tr, tg, tb, ar, ag, ab, sr, sg, sb);
    settings_show_text(cr, dlyX + dlyW + 10, yy + fh_ * 0.5 + 2, "seconds", 13.f, 400,
                       tr, tg, tb, 0.50f);
  }
  yy += 54;

  // Save / Cancel.
  const int bW = 110, bH = 34;
  bool svH = point_in_rect(app.pointerX, app.pointerY, px + pw - bW * 2 - 12, yy, bW, bH);
  {
    m3::Box sb;
    sb.setColor(ar, ag, ab, svH ? 1.0f : 0.80f);
    sb.setRadius(bH * 0.5f);
    sb.setGeometry(static_cast<float>(px + pw - bW * 2 - 12), static_cast<float>(yy),
                   static_cast<float>(bW), static_cast<float>(bH));
    sb.paint(cr);
    m3::Label sl;
    sl.setText("Save");
    sl.setFontSize(13.0f);
    sl.setFontWeight(600);
    sl.setColor(0, 0, 0, 0.92f);
    float sw, sh;
    sl.measureExtents(sw, sh);
    sl.paintAt(cr, static_cast<float>(px + pw - bW * 2 - 12) + (bW - sw) * 0.5f,
               static_cast<float>(yy) + (bH - sh) * 0.5f);
  }
  bool cvH = point_in_rect(app.pointerX, app.pointerY, px + pw - bW, yy, bW, bH);
  {
    m3::Box cb;
    cb.setColor(tr, tg, tb, 0.0f);
    cb.setRadius(bH * 0.5f);
    cb.setGeometry(static_cast<float>(px + pw - bW), static_cast<float>(yy),
                   static_cast<float>(bW), static_cast<float>(bH));
    cb.paint(cr);
    cairo_set_source_rgba(cr, tr, tg, tb, cvH ? 0.30f : 0.16f);
    cairo_set_line_width(cr, 1.0);
    cairo_round_rect(cr, px + pw - bW + 0.5, yy + 0.5, bW - 1.0, bH - 1.0, bH * 0.5f);
    cairo_stroke(cr);
    m3::Label cl;
    cl.setText("Cancel");
    cl.setFontSize(13.0f);
    cl.setFontWeight(600);
    cl.setColor(tr, tg, tb, 0.70f);
    float cw_s, ch_s;
    cl.measureExtents(cw_s, ch_s);
    cl.paintAt(cr, static_cast<float>(px + pw - bW) + (bW - cw_s) * 0.5f,
               static_cast<float>(yy) + (bH - ch_s) * 0.5f);
  }
}

// Kebab popup.
static void draw_kebab_popup(App& app, cairo_t* cr, int popX, int popY) {
  float ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob);
  const int pw = 130, ph = 72;
  const int px = popX;
  const int py = popY;
  {
    m3::Box pbg;
    pbg.setColor(sr, sg, sb, 0.96f);
    pbg.setRadius(8.0f);
    pbg.setGeometry(static_cast<float>(px), static_cast<float>(py),
                    static_cast<float>(pw), static_cast<float>(ph));
    pbg.paint(cr);
    cairo_set_source_rgba(cr, or_, og, ob, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_round_rect(cr, px + 0.5, py + 0.5, pw - 1.0, ph - 1.0, 8.0);
    cairo_stroke(cr);
  }
  const double pyC = app.pointerY + settings_scroll_px(app);
  bool editH = app.pointerX >= px + 4 && app.pointerX < px + pw - 4 &&
               pyC >= py + 2 && pyC < py + 36;
  bool delH  = app.pointerX >= px + 4 && app.pointerX < px + pw - 4 &&
               pyC >= py + 36 && pyC < py + 70;
  {
    if (editH) {
      m3::Box hb;
      hb.setColor(tr, tg, tb, 0.08f);
      hb.setRadius(4.0f);
      hb.setGeometry(px + 4.0f, py + 2.0f, pw - 8.0f, 34.0f);
      hb.paint(cr);
    }
    material_symbols_draw_glyph(cr, px + 20, py + 20, 15.0, "edit", tr, tg, tb, 0.70f);
    settings_show_text(cr, px + 34, py + 22, "Edit", 13.f, 500, tr, tg, tb, 0.80f);
  }
  {
    if (delH) {
      m3::Box hb;
      hb.setColor(1.0f, 0.30f, 0.30f, 0.10f);
      hb.setRadius(4.0f);
      hb.setGeometry(px + 4.0f, py + 36.0f, pw - 8.0f, 34.0f);
      hb.paint(cr);
    }
    material_symbols_draw_glyph(cr, px + 20, py + 55, 15.0, "delete", tr, tg, tb, 0.70f);
    settings_show_text(cr, px + 34, py + 57, "Delete", 13.f, 500, tr, tg, tb, delH ? 0.90f : 0.70f);
  }
}

// App browser overlay.
static void draw_app_browser(App& app, cairo_t* cr) {
  const int bw = 420, bh = 360;
  const int bx = (app.width - bw) / 2;
  const int by = std::max(30, (app.height - bh) / 2);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);

  float ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob);

  settings_card(app, cr, bx, by, bw, bh, 1.0);
  settings_show_text(cr, bx + kCardPad, by + 26, "Installed Applications", 15.f, 600, tr, tg, tb, 0.90f);

  // Close button
  const int clX = bx + bw - 32;
  bool clH = point_in_rect(app.pointerX, app.pointerY, clX, by + 8, 22, 22);
  icon_btn(cr, clX, by + 8 - (kRowHt - kActBtn) / 2, "close", tr, tg, tb, 0.55f, clH, false);

  // List with icons + scrollbar
  const int kBrowserEntryH = 40;
  const int kBrowserIconSz = 24;
  const int kPadL = kCardPad;
  const int lt = by + 48;
  const int lh = bh - 58;
  const auto& list = app.autostartInstalledApps;
  const int total = static_cast<int>(list.size());
  const int maxVis = std::max(1, lh / kBrowserEntryH);
  const int vis = std::min(total, maxVis);
  const int scrollPx = app.autostartAppBrowserScrollPx;
  const int startIdx = scrollPx / kBrowserEntryH;
  const int endIdx = std::min(startIdx + vis, total);

  cairo_save(cr);
  cairo_rectangle(cr, bx + 8, lt, bw - 16, lh);
  cairo_clip(cr);

  for (int i = startIdx; i < endIdx; ++i) {
    const int ry = lt + (i - startIdx) * kBrowserEntryH - (scrollPx % kBrowserEntryH);
    const bool rh = (app.autostartAppBrowserHoverRow == i);

    // Hover highlight
    if (rh) {
      m3::Box hb;
      hb.setColor(tr, tg, tb, 0.06f);
      hb.setRadius(5.0f);
      hb.setGeometry(static_cast<float>(bx + kPadL + 2),
                     static_cast<float>(ry) + 1.0f,
                     static_cast<float>(bw - kPadL * 2 - 4),
                     static_cast<float>(kBrowserEntryH) - 2.0f);
      hb.paint(cr);
    }

    // App icon
    const int iconX = bx + kPadL + 4;
    const int iconY = ry + (kBrowserEntryH - kBrowserIconSz) / 2;
    std::string appId = list[i].desktopPath;
    if (!appId.empty()) {
      auto pos = appId.rfind('/');
      if (pos != std::string::npos) appId = appId.substr(pos + 1);
      pos = appId.rfind('.');
      if (pos != std::string::npos) appId = appId.substr(0, pos);
    }
    const auto* iconEntry = s_app_browser_icon_cache.app_icon(appId);
    if (iconEntry && iconEntry->surface) {
      double iw = static_cast<double>(iconEntry->width);
      double ih = static_cast<double>(iconEntry->height);
      double scale = kBrowserIconSz / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      cairo_translate(cr, iconX, iconY);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, iconEntry->surface,
                               ((kBrowserIconSz / scale) - iw) * 0.5,
                               ((kBrowserIconSz / scale) - ih) * 0.5);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      // Fallback: colored circle with first letter
      cairo_set_source_rgba(cr, ar, ag, ab, 0.35f);
      cairo_arc(cr, iconX + kBrowserIconSz * 0.5, iconY + kBrowserIconSz * 0.5,
                kBrowserIconSz * 0.5, 0, 2.0 * M_PI);
      cairo_fill(cr);
      char letter[2] = {list[i].name.empty() ? '?' : list[i].name[0], '\0'};
      cairo_set_source_rgba(cr, 1, 1, 1, 0.85f);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, kBrowserIconSz * 0.45);
      cairo_text_extents_t te;
      cairo_text_extents(cr, letter, &te);
      cairo_move_to(cr, iconX + (kBrowserIconSz - te.width) * 0.5 - te.x_bearing,
                    iconY + (kBrowserIconSz + te.height) * 0.5 - te.y_bearing);
      cairo_show_text(cr, letter);
    }

    // Name
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, tr, tg, tb, 0.88f);
    cairo_move_to(cr, iconX + kBrowserIconSz + 10, ry + kBrowserEntryH * 0.5 + 5);
    cairo_show_text(cr, list[i].name.c_str());

    // Separator line between entries
    if (i < endIdx - 1) {
      cairo_set_source_rgba(cr, tr, tg, tb, 0.06f);
      cairo_set_line_width(cr, 0.5);
      cairo_move_to(cr, bx + kPadL + 6, ry + kBrowserEntryH - 0.5);
      cairo_line_to(cr, bx + bw - kPadL - 6, ry + kBrowserEntryH - 0.5);
      cairo_stroke(cr);
    }
  }

  cairo_restore(cr);

  // Scrollbar
  if (total > vis) {
    const double scrollbarW = 4.0;
    const double sbTrackH = static_cast<double>(lh);
    const double sbH = std::max(scrollbarW * 2.0,
                                sbTrackH * static_cast<double>(vis) / static_cast<double>(total));
    const double sbMax = sbTrackH - sbH;
    const double maxScrollPx = static_cast<double>(std::max(1, (total - vis) * kBrowserEntryH));
    const double frac = static_cast<double>(scrollPx) / maxScrollPx;
    const double sbY = static_cast<double>(lt) + frac * sbMax;
    const double sx = static_cast<double>(bx + bw - 10) - scrollbarW;
    cairo_set_source_rgba(cr, tr, tg, tb, 0.20);
    cairo_round_rect(cr, sx, sbY, scrollbarW, sbH, scrollbarW * 0.5);
    cairo_fill(cr);
  }
}

// Main paint.
void paint_autostart_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  if (app.autostartNeedsRefresh) settings_autostart_refresh_entries(app);
  const auto& entries = app.autostartEntries;

  float ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob);

  const double pyC = app.pointerY + settings_scroll_px(app);

  const int cx = contentX + 8;
  const int cw = contentW - 16;
  const int eh = static_cast<int>(entries.size()) * kRowHt;

  // Default apps section
  static constexpr int daRowH = 44;
  const int daCount = da_count_configured(app.settings.defaultApps);
  const int daH = (daCount > 0) ? (28 + daCount * daRowH + 16) : 0;
  const int daPad = 16;
  const int daY = kContentTop + 68 + eh + daPad;

  const int ch = std::max(200, 70 + eh + 20 + daH + (daCount > 0 ? daPad : 0));

  settings_card(app, cr, cx, kContentTop, cw, ch, glassOv);

  // Header.
  const int hh = 48;
  const int hy = kContentTop + 10;
  settings_show_text(cr, cx + kCardPad, hy + hh * 0.5 + 7, "Startup Applications", 21.f, 600, tr, tg, tb, 0.92f);

  const int abW = 150, abH = 34;
  const int abX = cx + cw - kCardPad - abW;
  const int abY = hy + (hh - abH) / 2;
  bool addH = point_in_rect(app.pointerX, pyC, abX, abY, abW, abH);
  {
    m3::Box btn;
    btn.setColor(0.133f, 0.773f, 0.369f, addH ? 1.0f : 0.82f);
    btn.setRadius(abH * 0.5f);
    btn.setGeometry(abX, abY, abW, abH);
    btn.paint(cr);
    m3::Label bl;
    bl.setText("+ Add");
    bl.setFontSize(14.0f);
    bl.setFontWeight(600);
    bl.setColor(0, 0, 0, 0.95f);
    float bw, bh;
    bl.measureExtents(bw, bh);
    bl.paintAt(cr, abX + (abW - bw) * 0.5f, abY + (abH - bh) * 0.5f);
  }

  // Empty (no entries + no default apps).
  if (entries.empty() && daCount == 0) {
    settings_show_text(cr, cx + kCardPad, kContentTop + 110,
                       "Nothing starts automatically.\nAdd your first app with the button above.", 13.f, 400,
                       tr, tg, tb, 0.35f);
    return;
  }

  // Entry list + default apps section.
  const int ly = kContentTop + 68;
  const int clipBottom = daCount > 0 ? (daY + daH + 4) : (ly + eh + 4);
  const int clipH = clipBottom - ly + 4;

  cairo_save(cr);
  cairo_rectangle(cr, cx, ly - 2, cw, clipH);
  cairo_clip(cr);

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    const int ry = ly + static_cast<int>(i) * kRowHt;
    if (ry + kRowHt < ly - 4 || ry > ly + eh + daH + daPad + 100) continue;

    const bool hv = (app.autostartHoverRow == static_cast<int>(i));

    // Background
    if (hv) {
      m3::Box hb;
      hb.setColor(tr, tg, tb, 0.05f);
      hb.setRadius(6.0f);
      hb.setGeometry(cx + 6.0f, ry + 2.0f, cw - 12.0f, kRowHt - 4.0f);
      hb.paint(cr);
    }

    // Badge
    colored_badge(cr, cx + kCardPad, ry, static_cast<int>(i));

    // Name + subtitle
    const int tx = cx + kCardPad + kBadgeSz + 14;
    {
      m3::Label nl;
      nl.setText(e.name.c_str());
      nl.setFontSize(15.0f);
      nl.setFontWeight(500);
      nl.setColor(tr, tg, tb, 0.92f);
      float nw, nh;
      nl.measureExtents(nw, nh);
      nl.paintAt(cr, tx, ry + 18 - nh + 3.0f);
    }
    {
      std::string sub;
      if (e.isUserOverride) sub += "Custom  \xc2\xb7  ";
      sub += e.enabled ? "Enabled" : "Disabled";
      if (e.delaySec > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "  \xc2\xb7  +%ds delay", e.delaySec);
        sub += buf;
      }
      m3::Label sl;
      sl.setText(sub.c_str());
      sl.setFontSize(11.0f);
      sl.setFontWeight(400);
      sl.setColor(tr, tg, tb, 0.42f);
      float sw, sh;
      sl.measureExtents(sw, sh);
      sl.paintAt(cr, tx, ry + kRowHt - 16 - sh + 3.0f);
    }

    // Toggle
    const int tgX = cx + cw - kCardPad - kPillW - kActBtn - 10;
    raw_toggle(cr, tgX, ry, e.enabled, tr, tg, tb);

    // Kebab button
    const int kbX = cx + cw - kCardPad - kActBtn;
    const bool kbHv = hv && point_in_rect(app.pointerX, pyC, kbX, ry, kActBtn, kRowHt);
    icon_btn(cr, kbX, ry, "more_vert", tr, tg, tb, hv ? 0.55f : 0.12f, kbHv, false);

    // Kebab popup (shown for the row with menu open)
    if (app.autostartDeleteConfirmRow == static_cast<int>(i) && !app.autostartFormOpen) {
      const int popX = kbX - 130 + kActBtn;
      const int popY = ry + kRowHt;
      draw_kebab_popup(app, cr, popX, popY);
    }
  }

  // Default apps section.
  if (daCount > 0) {
    const int daHeaderY = daY;
    const int daLabelX = cx + kCardPad + 4;

    // Separator
    cairo_set_source_rgba(cr, tr, tg, tb, 0.10);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, cx + kCardPad + 4, daHeaderY - 8);
    cairo_line_to(cr, cx + cw - kCardPad - 4, daHeaderY - 8);
    cairo_stroke(cr);

    // Section header
    settings_show_text(cr, daLabelX, daHeaderY + 18, "Default Applications at Login", 14.f, 600, tr, tg, tb, 0.85f);

    // Rows
    const int daToggleX = cx + cw - kCardPad - kPillW - kActBtn - 10;
    const int daIconSz = 18;
    const int daTextX = daLabelX + daIconSz + 8;

    int rowIdx = 0;
    for (int cat = 0; cat < eh::settings::default_apps::kNumCategories; ++cat) {
      const auto& id = da_field_ref(app.settings.defaultApps, cat);
      if (id.empty()) continue;

      const int rry = daY + 28 + rowIdx * daRowH;
      const bool rh = (app.autostartDefAppsHoverIdx == rowIdx);

      // Background
      if (rh) {
        m3::Box hb;
        hb.setColor(tr, tg, tb, 0.05f);
        hb.setRadius(5.0f);
        hb.setGeometry(static_cast<float>(cx + kCardPad + 2),
                       static_cast<float>(rry) + 1.0f,
                       static_cast<float>(cw - kCardPad * 2 - 4),
                       static_cast<float>(daRowH) - 2.0f);
        hb.paint(cr);
      }

      // Category icon
      const char* iconGlyph = eh::settings::default_apps::category_material_icon(cat);
      material_symbols_draw_glyph(cr, daLabelX + daIconSz * 0.5, rry + daRowH * 0.5,
                                  static_cast<double>(daIconSz) - 2.0, iconGlyph, tr, tg, tb, 0.70f);

      // Category label + "·" + app name
      std::string line = std::string(eh::settings::default_apps::category_label(cat)) + "  \xc2\xb7  " + da_resolve_app_name(id);

      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13.0);
      cairo_set_source_rgba(cr, tr, tg, tb, 0.80f);
      cairo_move_to(cr, daTextX, rry + daRowH * 0.5 + 5);
      cairo_show_text(cr, line.c_str());

      // Toggle
      bool daOn = da_is_autostarted(app.autostartEntries, id);
      raw_toggle(cr, daToggleX, rry, daOn, tr, tg, tb);

      ++rowIdx;
    }
  }

  cairo_restore(cr);

  if (app.autostartFormOpen || app.autostartAppBrowserOpen) {
    cairo_save(cr);
    cairo_translate(cr, 0.0, settings_scroll_px_int(app));
    if (app.autostartFormOpen && !app.autostartAppBrowserOpen) {
      draw_form_card(app, cr, glassOv);
    }
    if (app.autostartAppBrowserOpen) {
      draw_app_browser(app, cr);
    }
    cairo_restore(cr);
  }
}

// Pointer down.
bool settings_autostart_consume_pointer_down(App& app, int contentX, int contentW) {
  if (app.activeTab != 48) return false;
  if (app.autostartNeedsRefresh) settings_autostart_refresh_entries(app);

  const auto& entries = app.autostartEntries;
  const int cx = contentX + 8;
  const int cw = contentW - 16;

  float ar, ag, ab, tr, tg, tb, sr_, sg_, sb_, or_, og_, ob_;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr_, sg_, sb_, or_, og_, ob_);

  const double pyC = app.pointerY + settings_scroll_px(app);

  // App browser (checked first — drawn on top of form).
  if (app.autostartAppBrowserOpen) {
    const int bw = 420, bh = 360;
    const int bx = (app.width - bw) / 2;
    const int by = std::max(30, (app.height - bh) / 2);

    // Close button
    if (point_in_rect(app.pointerX, app.pointerY, bx + bw - 32, by + 8, 22, 22)) {
      app.autostartAppBrowserOpen = false;
      draw(app);
      return true;
    }

    // Outside click
    if (!point_in_rect(app.pointerX, app.pointerY, bx, by, bw, bh)) {
      app.autostartAppBrowserOpen = false;
      draw(app);
      return true;
    }

    // Select app
    const int kBrowserEntryH = 40;
    const int lt = by + 48;
    const int lh = bh - 58;
    const auto& list = app.autostartInstalledApps;
    const int total = static_cast<int>(list.size());
    const int maxVis = std::max(1, lh / kBrowserEntryH);
    const int vis = std::min(total, maxVis);
    const int scrollPx = app.autostartAppBrowserScrollPx;
    const int startIdx = scrollPx / kBrowserEntryH;
    const int endIdx = std::min(startIdx + vis, total);
    for (int i = startIdx; i < endIdx; ++i) {
      const int ry = lt + (i - startIdx) * kBrowserEntryH - (scrollPx % kBrowserEntryH);
      if (point_in_rect(app.pointerX, app.pointerY, bx + 10, ry, bw - 20, kBrowserEntryH)) {
        app.autostartEditName = list[i].name;
        app.autostartEditExec = list[i].exec;
        app.autostartEditIcon = list[i].icon;
        app.autostartAppBrowserOpen = false;
        draw(app);
        return true;
      }
    }
    return true;
  }

  // Form overlay active.
  if (app.autostartFormOpen) {
    const int fw = 420, fh = 350;
    const int fx = (app.width - fw) / 2;
    const int fy = std::max(40, (app.height - fh) / 3);
    const int px = fx + kCardPad;
    const int pw = fw - kCardPad * 2;
    const int fh_ = 34;
    const int lblW = 110;

    if (!point_in_rect(app.pointerX, app.pointerY, fx, fy, fw, fh)) {
      app.autostartFormOpen = false;
      app.autostartActiveField = AutostartField::None;
      app.autostartDeleteConfirmRow = -1;
      draw(app);
      return true;
    }

    int yy = fy + 66;

    // Name field
    int tfx = px + lblW;
    int tfw = pw - lblW;
    if (point_in_rect(app.pointerX, app.pointerY, tfx, yy, tfw, fh_)) {
      app.autostartActiveField = AutostartField::Name;
      draw(app);
      return true;
    }
    yy += fh_ + 18;

    // Command field + browse
    int cmdW = tfw - kActBtn - 10;
    if (point_in_rect(app.pointerX, app.pointerY, tfx, yy, cmdW, fh_)) {
      app.autostartActiveField = AutostartField::Exec;
      draw(app);
      return true;
    }
    int browseX = tfx + cmdW + 10;
    if (point_in_rect(app.pointerX, app.pointerY, browseX, yy, kActBtn, fh_)) {
      app.autostartInstalledApps = eh::autostart::scan_installed_apps();
      app.autostartAppBrowserOpen = true;
      app.autostartAppBrowserScrollPx = 0;
      app.autostartAppBrowserHoverRow = -1;
      app.autostartActiveField = AutostartField::None;
      draw(app);
      return true;
    }
    yy += fh_ + 24;

    // Delay text field
    int dlyX = px + lblW;
    int dlyW = 70;
    if (point_in_rect(app.pointerX, app.pointerY, dlyX, yy, dlyW, fh_)) {
      if (app.autostartEditIcon.empty()) {
        char db[16];
        std::snprintf(db, sizeof(db), "%d", app.autostartEditDelay);
        app.autostartEditIcon = db;
      }
      if (app.autostartEditIcon == "0") {
        app.autostartEditIcon.clear();
      }
      app.autostartActiveField = AutostartField::Delay;
      draw(app);
      return true;
    }
    yy += 54;

    // Save
    const int bW = 110, bH = 34;
    if (point_in_rect(app.pointerX, app.pointerY, px + pw - bW * 2 - 12, yy, bW, bH)) {
      if (!app.autostartEditName.empty() && !app.autostartEditExec.empty()) {
        int delayVal = 0;
        if (!app.autostartEditIcon.empty()) {
          delayVal = std::atoi(app.autostartEditIcon.c_str());
          if (delayVal < 0) delayVal = 0;
          if (delayVal > 999) delayVal = 999;
        }
        app.autostartEditDelay = delayVal;
        std::string stem = app.autostartEditStem;
        if (stem.empty()) {
          stem = app.autostartEditName;
          for (auto& c : stem) {
            if (c == ' ' || c == '\t') c = '_';
            else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
        }
        if (app.autostartEditMode) {
          eh::autostart::edit_autostart_entry(stem, app.autostartEditName,
                                              app.autostartEditExec,
                                              "",
                                              app.autostartEditDelay);
        } else {
          eh::autostart::create_autostart_entry(stem, app.autostartEditName,
                                                app.autostartEditExec,
                                                "",
                                                app.autostartEditDelay);
        }
        app.autostartFormOpen = false;
        app.autostartEditMode = false;
        app.autostartActiveField = AutostartField::None;
        app.autostartNeedsRefresh = true;
        app.autostartScrollPx = 0;
        app.autostartDeleteConfirmRow = -1;
        draw(app);
        return true;
      }
    }

    // Cancel
    if (point_in_rect(app.pointerX, app.pointerY, px + pw - bW, yy, bW, bH)) {
      app.autostartFormOpen = false;
      app.autostartEditMode = false;
      app.autostartActiveField = AutostartField::None;
      app.autostartEditName.clear();
      app.autostartEditExec.clear();
      app.autostartEditStem.clear();
      app.autostartEditIcon.clear();
      app.autostartEditDelay = 0;
      app.autostartDeleteConfirmRow = -1;
      draw(app);
      return true;
    }

    app.autostartActiveField = AutostartField::None;
    draw(app);
    return true;
  }

  // Header add button.
  {
    const int hh = 48;
    const int hy = kContentTop + 10;
    const int abW = 150, abH = 34;
    const int abX = cx + cw - kCardPad - abW;
    const int abY = hy + (hh - abH) / 2;
    if (point_in_rect(app.pointerX, pyC, abX, abY, abW, abH)) {
      app.autostartFormOpen = true;
      app.autostartEditMode = false;
      app.autostartActiveField = AutostartField::Name;
      app.autostartEditStem.clear();
      app.autostartEditName.clear();
      app.autostartEditExec.clear();
      app.autostartEditIcon = "0";
      app.autostartEditDelay = 0;
      app.autostartDeleteConfirmRow = -1;
      draw(app);
      return true;
    }
  }

  // Default apps toggles.
  {
    const int daCount = da_count_configured(app.settings.defaultApps);
    if (daCount > 0) {
      static constexpr int daRowH = 44;
      const int eh_ = static_cast<int>(entries.size()) * kRowHt;
      const int daY = kContentTop + 68 + eh_ + 16;
      const int daToggleX = cx + cw - kCardPad - kPillW - kActBtn - 10;

      int rowIdx = 0;
      for (int cat = 0; cat < eh::settings::default_apps::kNumCategories; ++cat) {
        const auto& id = da_field_ref(app.settings.defaultApps, cat);
        if (id.empty()) { ++rowIdx; continue; }

        const int rry = daY + 28 + rowIdx * daRowH;
        const int tgY = rry + (daRowH - kPillH) / 2;

        if (point_in_rect(app.pointerX, pyC, daToggleX, tgY, kPillW, kPillH)) {
          bool currentlyOn = da_is_autostarted(app.autostartEntries, id);
          da_toggle_autostart(app, id, !currentlyOn);
          app.autostartDeleteConfirmRow = -1;
          draw(app);
          return true;
        }

        ++rowIdx;
      }
    }
  }

  if (entries.empty()) {
    app.autostartDeleteConfirmRow = -1;
    return false;
  }

  // Entry rows.
  const int ly = kContentTop + 68;

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    const int ry = ly + static_cast<int>(i) * kRowHt;

    // Kebab popup actions
    if (app.autostartDeleteConfirmRow == static_cast<int>(i) && !app.autostartFormOpen) {
      const int kbX = cx + cw - kCardPad - kActBtn;
      const int popX = kbX - 130 + kActBtn;
      const int popY = ry + kRowHt;

      // Edit
      if (point_in_rect(app.pointerX, pyC, popX + 4, popY + 2, 122, 34)) {
        app.autostartFormOpen = true;
        app.autostartEditMode = true;
        app.autostartActiveField = AutostartField::Name;
        app.autostartEditStem = e.stem;
        app.autostartEditName = e.name;
        app.autostartEditExec.clear();
        {
          char db[16];
          std::snprintf(db, sizeof(db), "%d", e.delaySec);
          app.autostartEditIcon = db;
        }
        app.autostartEditDelay = e.delaySec;
        app.autostartDeleteConfirmRow = -1;
        draw(app);
        return true;
      }
      // Delete
      if (point_in_rect(app.pointerX, pyC, popX + 4, popY + 36, 122, 34)) {
        if (e.isUserOverride) {
          eh::autostart::remove_autostart_override(e.stem);
          app.autostartNeedsRefresh = true;
        }
        app.autostartDeleteConfirmRow = -1;
        draw(app);
        return true;
      }
      // Click outside popup closes it
      if (!point_in_rect(app.pointerX, pyC, popX, popY, 130, 72)) {
        app.autostartDeleteConfirmRow = -1;
        draw(app);
        return true;
      }
      return true;
    }

    // Kebab button
    const int kbX = cx + cw - kCardPad - kActBtn;
    if (point_in_rect(app.pointerX, pyC, kbX, ry, kActBtn, kRowHt)) {
      if (app.autostartDeleteConfirmRow == static_cast<int>(i)) {
        app.autostartDeleteConfirmRow = -1;
      } else {
        app.autostartDeleteConfirmRow = static_cast<int>(i);
      }
      draw(app);
      return true;
    }

    // Toggle
    const int tgX = cx + cw - kCardPad - kPillW - kActBtn - 10;
    const int tgY = ry + (kRowHt - kPillH) / 2;
    if (point_in_rect(app.pointerX, pyC, tgX, tgY, kPillW, kPillH)) {
      eh::autostart::set_autostart_enabled(e.stem, !e.enabled);
      app.autostartNeedsRefresh = true;
      app.autostartDeleteConfirmRow = -1;
      draw(app);
      return true;
    }

    // Row hover for other rows closes kebab
    if (point_in_rect(app.pointerX, pyC, cx + 6, ry, cw - 12, kRowHt)) {
      app.autostartHoverRow = static_cast<int>(i);
      return true;
    }
  }

  return false;
}

// Pointer up.
void settings_autostart_consume_pointer_up(App& app) {
  (void)app;
}

// Pointer move.
bool settings_autostart_consume_pointer_move(App& app, int contentX, int contentW) {
  if (app.activeTab != 48) return false;

  // App browser hover
  if (app.autostartAppBrowserOpen) {
    const int bw = 420, bh = 360;
    const int bx = (app.width - bw) / 2;
    const int by = std::max(30, (app.height - bh) / 2);
    const int kBrowserEntryH = 40;
    const int lt = by + 48;
    const int lh = bh - 58;
    const auto& list = app.autostartInstalledApps;
    const int total = static_cast<int>(list.size());
    const int maxVis = std::max(1, lh / kBrowserEntryH);
    const int vis = std::min(total, maxVis);
    const int scrollPx = app.autostartAppBrowserScrollPx;
    const int startIdx = scrollPx / kBrowserEntryH;
    const int endIdx = std::min(startIdx + vis, total);
    int nh = -1;
    for (int i = startIdx; i < endIdx; ++i) {
      const int ry = lt + (i - startIdx) * kBrowserEntryH - (scrollPx % kBrowserEntryH);
      if (point_in_rect(app.pointerX, app.pointerY, bx + 10, ry, bw - 20, kBrowserEntryH)) {
        nh = i;
        break;
      }
    }
    if (nh != app.autostartAppBrowserHoverRow) {
      app.autostartAppBrowserHoverRow = nh;
      draw(app);
    }
    return true;
  }

  const auto& entries = app.autostartEntries;
  const int cx = contentX + 8;
  const int cw = contentW - 16;
  const int ly = kContentTop + 68;
  const int eh = static_cast<int>(entries.size()) * kRowHt;
  const double pyC = app.pointerY + settings_scroll_px(app);
  int nh = -1;

  // Entry rows hover
  for (size_t i = 0; i < entries.size(); ++i) {
    const int ry = ly + static_cast<int>(i) * kRowHt;
    if (point_in_rect(app.pointerX, pyC, cx + 6, ry, cw - 12, kRowHt)) {
      nh = static_cast<int>(i);
      break;
    }
  }

  // Default apps rows hover
  int daNh = -1;
  const int daCount = da_count_configured(app.settings.defaultApps);
  if (daCount > 0 && nh < 0) {
    static constexpr int daRowH = 44;
    const int daY = ly + eh + 16;
    const int daRowBase = daY + 28;

    int rowIdx = 0;
    for (int cat = 0; cat < eh::settings::default_apps::kNumCategories; ++cat) {
      const auto& id = da_field_ref(app.settings.defaultApps, cat);
      if (id.empty()) { ++rowIdx; continue; }
      const int rry = daRowBase + rowIdx * daRowH;
      if (point_in_rect(app.pointerX, pyC, cx + 6, rry, cw - 12, daRowH)) {
        daNh = rowIdx;
        break;
      }
      ++rowIdx;
    }
  }

  bool changed = false;
  if (nh != app.autostartHoverRow) {
    app.autostartHoverRow = nh;
    changed = true;
  }
  if (daNh != app.autostartDefAppsHoverIdx) {
    app.autostartDefAppsHoverIdx = daNh;
    changed = true;
  }
  if (changed) draw(app);

  return true;
}

// Keyboard.
bool settings_autostart_consume_key(App& app, unsigned sym, unsigned state,
                                    const char* utf8, int utf8Len) {
  if (app.activeTab != 48) return false;
  if (app.autostartActiveField == AutostartField::None) return false;
  if (state != 0) return false;

  auto& buf = (app.autostartActiveField == AutostartField::Name)
                  ? app.autostartEditName
                  : (app.autostartActiveField == AutostartField::Exec)
                      ? app.autostartEditExec
                      : app.autostartEditIcon;

  if (sym == 0xff0d || sym == 0xff8d || sym == 0xff1b) {
    app.autostartActiveField = AutostartField::None;
    draw(app);
    return true;
  }
  if (sym == 0xff09) {
    if (app.autostartActiveField == AutostartField::Name)
      app.autostartActiveField = AutostartField::Exec;
    else if (app.autostartActiveField == AutostartField::Exec)
      app.autostartActiveField = AutostartField::Delay;
    else
      app.autostartActiveField = AutostartField::Name;
    draw(app);
    return true;
  }
  if (sym == 0xff08) {
    if (!buf.empty()) buf.pop_back();
    draw(app);
    return true;
  }
  if (utf8Len > 0) {
    for (int i = 0; i < utf8Len; ++i) {
      char c = utf8[i];
      if (app.autostartActiveField == AutostartField::Delay) {
        if (c >= '0' && c <= '9' && buf.size() < 4) buf.push_back(c);
      } else if (c >= 32 && c < 127) {
        buf.push_back(c);
      }
    }
    draw(app);
    return true;
  }

  return false;
}

void settings_autostart_refresh_entries(App& app) {
  app.autostartEntries = eh::autostart::get_autostart_ui_entries();
  app.autostartNeedsRefresh = false;
}
