#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"

extern void save_settings(const struct Settings& s);

// Layout helpers.

static constexpr int kLayoutToggleCol = 74;
static constexpr int kLayoutDotRowH = 46;
static constexpr int kLayoutMaxCardW = 800;

static void layout_card_geom(int contentX, int contentW, int* cardX, int* cardW) {
  *cardW = std::min(contentW - 16, kLayoutMaxCardW);
  *cardX = contentX + (contentW - *cardW) / 2;
}

static std::string trim_layout_str(std::string s) {
   
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

static bool layout_output_auto(const std::string& s) {
   
  std::string t = trim_layout_str(s);
  if (t.empty()) return true;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "auto";
}

static bool layout_output_all(const std::string& s) {
   
  std::string t = trim_layout_str(s);
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "all";
}

static bool layout_names_match(const std::string& a, const std::string& b) {
   
  if (a.empty() || b.empty()) return false;
  if (a == b) return true;
  std::string aa = a, bb = b;
  for (char& c : aa) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (char& c : bb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return aa == bb;
}

static void layout_fill_round_card(App& app, cairo_t* cr, double cx, double cy, double cw, double ch, double glassOv) {
  settings_card(app, cr, cx, cy, cw, ch, glassOv);
}

static void layout_draw_category_label(cairo_t* cr, double x, double y, const char* text) {
  settings_show_text(cr, x, y, text, 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
}

// Paint.

void paint_layout_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  app.wl.sync_logical_outputs_from_cache();
  const auto& outs = app.wl.logical_outputs();
  int cardX, cardW;
  layout_card_geom(contentX, contentW, &cardX, &cardW);

  const double sec1H = outs.empty() ? 132.0 : 198.0;
  const double sec1Top = static_cast<double>(kContentTop);
  layout_fill_round_card(app, cr, cardX, sec1Top, cardW, sec1H, glassOv);
  layout_draw_category_label(cr, cardX + kCardPad, sec1Top + 15.0, "Connected displays");
  {
    std::ostringstream oss;
    oss << outs.size() << " screen" << (outs.size() == 1 ? "" : "s") << " detected";
    settings_show_text(cr, cardX + kCardPad, sec1Top + 38, oss.str().c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  }

  if (!outs.empty()) {
    const double gap = 10.0;
    const double innerPad = 14.0;
    const double rowTop = sec1Top + 54;
    const double cardInnerH = 118.0;
    const double n = static_cast<double>(outs.size());
    const double avail = static_cast<double>(cardW) - 2 * innerPad - gap * (n - 1.0);
    const double cw = std::max(72.0, avail / n);
    for (size_t i = 0; i < outs.size(); ++i) {
      const double cx = static_cast<double>(cardX) + innerPad + static_cast<double>(i) * (cw + gap);
      {
        m3::Box box;
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR;
          g = app.drawChrome.panelFillG;
          b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR);
          g = static_cast<float>(Theme::BgG);
          b = static_cast<float>(Theme::BgB);
        }
        box.setColor(r, g, b, 0.06f);
        box.setRadius(12.0f);
        box.setGeometry(static_cast<float>(cx), static_cast<float>(rowTop),
                        static_cast<float>(cw), static_cast<float>(cardInnerH));
        box.paint(cr);
      }
      cairo_round_rect(cr, cx, rowTop, cw, cardInnerH, 12.0);
      paint_src_glass_hi(app, cr, 0.08);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, std::clamp(cw * 0.22, 15.0, 26.0));
      cairo_text_extents_t te{};
      cairo_text_extents(cr, outs[i].name.c_str(), &te);
      settings_show_text(cr, cx + (cw - te.width) / 2.0 - te.x_bearing, rowTop + 48, outs[i].name.c_str(), std::clamp(cw * 0.22, 15.0, 26.0), 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

      std::ostringstream dim;
      dim << outs[i].logical_width << "\u00d7" << outs[i].logical_height;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_text_extents(cr, dim.str().c_str(), &te);
      settings_show_text(cr, cx + (cw - te.width) / 2.0 - te.x_bearing, rowTop + cardInnerH - 18, dim.str().c_str(), 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }
  } else {
    settings_show_text(cr, cardX + kCardPad, sec1Top + 78, "No outputs listed yet \u2014 compositor may lack xdg-output.", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  }

  const double sec2Top = sec1Top + sec1H + static_cast<double>(kSpacingXL);
  const double sec2H = 274.0;
  layout_fill_round_card(app, cr, cardX, sec2Top, cardW, sec2H, glassOv);
  layout_draw_category_label(cr, cardX + kCardPad, sec2Top + 15.0, "Shell on display");
  settings_show_text(cr, cardX + kCardPad, sec2Top + 38, "Auto = one monitor (heuristic). All = shell bar on every connected display.", 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  const int ncols = 2 + static_cast<int>(outs.size());
  const int dotsBlockW = ncols * kLayoutToggleCol;
  const int dotsLeft = cardX + cardW - kCardPad - dotsBlockW;
  const int hdrY = static_cast<int>(sec2Top) + 54;
  const int rowDockY = hdrY + 26;
  const int rowWidgetsY = rowDockY + kLayoutDotRowH;
  const int rowTaskbarY = rowWidgetsY + kLayoutDotRowH;

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 10);
  for (int c = 0; c < ncols; ++c) {
    const int cx = dotsLeft + c * kLayoutToggleCol + kLayoutToggleCol / 2;
    const char* lab = (c == 0) ? "Auto" : (c == 1) ? "All" : outs[static_cast<size_t>(c - 2)].name.c_str();
    cairo_text_extents_t te{};
    cairo_text_extents(cr, lab, &te);
    settings_show_text(cr, cx - te.width / 2.0 - te.x_bearing, hdrY, lab, 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  }

  auto draw_assign_row = [&](const char* title, int rowY, const std::string& assign) {
    settings_show_text(cr, cardX + kCardPad, rowY + 4, title, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    const bool autoOn = layout_output_auto(assign);
    const bool allOn = layout_output_all(assign);
    for (int c = 0; c < ncols; ++c) {
      const int cx = dotsLeft + c * kLayoutToggleCol + kLayoutToggleCol / 2;
      bool on = false;
      if (c == 0) on = autoOn;
      else if (c == 1) on = allOn;
      else if (!autoOn && !allOn && layout_names_match(assign, outs[static_cast<size_t>(c - 2)].name)) on = true;

      const bool dim = (autoOn && c != 0) || (allOn && c != 1) || (!autoOn && !allOn && c < 2);

      cairo_new_sub_path(cr);
      cairo_arc(cr, cx, rowY + 14, 13, 0, 2 * M_PI);
      if (on) {
        paint_src_glass_hi(app, cr, dim ? 0.12 : 0.22);
        cairo_fill_preserve(cr);
        paint_src_glass_hi(app, cr, dim ? 0.22 : 0.72);
        cairo_set_line_width(cr, on ? 2 : 1);
        cairo_stroke(cr);
        if (on && !dim) {
          cairo_arc(cr, cx, rowY + 14, 5.5, 0, 2 * M_PI);
          paint_src_accent(app, cr, 0.95);
          cairo_fill(cr);
        }
      } else {
        paint_src_dim(app, cr, dim ? 0.28 : 0.72);
        cairo_fill_preserve(cr);
        paint_src_glass_hi(app, cr, dim ? 0.06 : 0.12);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
      }
    }
  };

  draw_assign_row("Dock", rowDockY, app.settings.dockOutputName);
  draw_assign_row("Desktop Widgets", rowWidgetsY, app.settings.desktopWidgetsOutputName);
  draw_assign_row("Taskbar", rowTaskbarY, app.settings.taskbarOutputName);

  settings_show_text(cr, cardX + kCardPad, sec2Top + sec2H - 22, "EH_DOCK_OUTPUT / EH_TASKBAR_OUTPUT override saved settings for one session.", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
}

// Pointer down.

bool settings_layout_consume_pointer_down(App& app, int contentX, int contentW) {
   
  app.wl.refresh_logical_outputs();
  const auto& outs = app.wl.logical_outputs();
  int cardX, cardW;
  layout_card_geom(contentX, contentW, &cardX, &cardW);
  const double sec1H = outs.empty() ? 132.0 : 198.0;
  const double sec2Top = static_cast<double>(kContentTop) + sec1H + static_cast<double>(kSpacingXL);
  const double sec2H = 274.0;
  if (app.pointerY < sec2Top || app.pointerY >= sec2Top + sec2H) return false;
  if (app.pointerX < cardX || app.pointerX >= cardX + cardW) return false;

  const int ncols = 2 + static_cast<int>(outs.size());
  const int dotsBlockW = ncols * kLayoutToggleCol;
  const int dotsLeft = cardX + cardW - kCardPad - dotsBlockW;
  const int hdrY = static_cast<int>(sec2Top) + 54;
  const int rowDockY = hdrY + 26;
  const int rowWidgetsY = rowDockY + kLayoutDotRowH;
  const int rowTaskbarY = rowWidgetsY + kLayoutDotRowH;

  auto hit_dot = [&](int rowY, std::string& assignField) -> bool {
    const int cy = rowY + 14;
    if (app.pointerY < rowY - 4 || app.pointerY > rowY + 32) return false;
    for (int c = 0; c < ncols; ++c) {
      const int cx = dotsLeft + c * kLayoutToggleCol + kLayoutToggleCol / 2;
      const double dx = app.pointerX - cx;
      const double dy = app.pointerY - cy;
      if (dx * dx + dy * dy <= 18.0 * 18.0) {
        if (c == 0) assignField.clear();
        else if (c == 1) assignField = "all";
        else assignField = outs[static_cast<size_t>(c - 2)].name;
        return true;
      }
    }
    return false;
  };

  bool hit = false;
  if (hit_dot(rowDockY, app.settings.dockOutputName)) hit = true;
  else if (hit_dot(rowWidgetsY, app.settings.desktopWidgetsOutputName)) hit = true;
  else if (hit_dot(rowTaskbarY, app.settings.taskbarOutputName)) hit = true;
  if (hit) save_settings(app.settings);
  return hit;
}
