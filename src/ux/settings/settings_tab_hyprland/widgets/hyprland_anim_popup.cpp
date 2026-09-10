#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/common/hyprland_anim_common.hpp"
#include "ux/settings/settings_tab_hyprland/widgets/hyprland_anim_popup.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

extern void draw(App& app);

// Popup paint.
void paint_hyprland_anim_popup(App& app, cairo_t* cr, int contentX, int contentW) {
  auto& animCfg = app.hyprlandConfig.animations;
  if (app.hyprlandAnimEditIdx < 0 || app.hyprlandAnimEditIdx >= static_cast<int>(animCfg.entries.size()))
    return;
  auto& entry = animCfg.entries[app.hyprlandAnimEditIdx];

  constexpr int popW = 340;
  constexpr int popH = 300;
  int popX = contentX + (contentW - popW) / 2;
  int popY = kContentTop + 40;

  settings_card(app, cr, popX, popY, popW, popH, 1.0);

  settings_show_text(cr, popX + 16, popY + 22, entry.name.c_str(), 14, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.93f);

  {
    int closeX = popX + popW - 32;
    int closeY = popY + 8;
    bool hovered = app.pointerX >= closeX && app.pointerX < closeX + 20 &&
                   app.pointerY >= closeY && app.pointerY < closeY + 20;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setGlyph("close");
    btn.setGeometry(static_cast<float>(closeX), static_cast<float>(closeY), 20.0f, 20.0f);
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hovered);
    btn.paint(cr);
  }

  int ry = popY + 50;

  {
    bool hovered = app.pointerX >= popX + 16 && app.pointerX < popX + popW - 16 &&
                   app.pointerY >= ry && app.pointerY < ry + 28;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel("Bezier Curve Editor");
    btn.setGeometry(static_cast<float>(popX + 16), static_cast<float>(ry),
                    static_cast<float>(popW - 32), 28.0f);
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hovered);
    btn.paint(cr);
  }

  ry += 42;

  {
    settings_show_text(cr, popX + 16, ry + 14, "Speed:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);

    {
      bool hovered = app.pointerX >= popX + 72 && app.pointerX < popX + 100 &&
                     app.pointerY >= ry - 2 && app.pointerY < ry + 26;
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setGlyph("remove");
      btn.setGeometry(static_cast<float>(popX + 72), static_cast<float>(ry - 2), 28.0f, 28.0f);
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(hovered);
      btn.paint(cr);
    }

    int speedTextX = popX + 104;
    int speedTextW = popW - 104 - 56;
    cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, app.hyprlandAnimSpeedEditActive ? 0.30 : 0.15);
    cairo_rectangle(cr, speedTextX, ry - 2, speedTextW, 28);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.50);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, speedTextX, ry - 2, speedTextW, 28);
    cairo_stroke(cr);

    char spBuf[32];
    if (app.hyprlandAnimSpeedEditActive) {
      std::snprintf(spBuf, sizeof(spBuf), "%s", app.hyprlandAnimSpeedEditBuf.c_str());
    } else {
      std::snprintf(spBuf, sizeof(spBuf), "%.1f", entry.speed);
    }
    settings_show_text(cr, speedTextX + 8, ry + 18, spBuf, 13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.85f);

    {
      bool hovered = app.pointerX >= popX + popW - 16 - 28 && app.pointerX < popX + popW - 16 &&
                     app.pointerY >= ry - 2 && app.pointerY < ry + 26;
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setGlyph("add");
      btn.setGeometry(static_cast<float>(popX + popW - 16 - 28), static_cast<float>(ry - 2), 28.0f, 28.0f);
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(hovered);
      btn.paint(cr);
    }
  }

  ry += 40;

  {
    settings_show_text(cr, popX + 16, ry + 14, "Curve:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);

    int cbX = popX + 72;
    int cbW = popW - 72 - 16;
    int cbH = kSettingsComboH;

    cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, app.hyprlandAnimCurveDdOpen ? 0.30 : 0.15);
    cairo_rectangle(cr, cbX, ry - 2, cbW, cbH);
    cairo_fill(cr);
    settings_show_text(cr, cbX + 8, ry + cbH / 2 + 4, entry.curve.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);
    cairo_move_to(cr, cbX + cbW - 16, ry + cbH / 2 - 3);
    cairo_line_to(cr, cbX + cbW - 8, ry + cbH / 2 - 3);
    cairo_line_to(cr, cbX + cbW - 12, ry + cbH / 2 + 3);
    cairo_close_path(cr);
    cairo_fill(cr);

    if (app.hyprlandAnimCurveDdOpen) {
      constexpr int ddRowH = 24;
      int ddH = kCurveCount * ddRowH;
      int ddTop = ry + cbH + 2;
      cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, 0.95);
      cairo_rectangle(cr, cbX, ddTop, cbW, ddH);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.50);
      cairo_set_line_width(cr, 1);
      cairo_rectangle(cr, cbX, ddTop, cbW, ddH);
      cairo_stroke(cr);

      for (int i = 0; i < kCurveCount; ++i) {
        int rY = ddTop + i * ddRowH;
        if (i == app.hyprlandAnimCurveDdHover) {
          cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.20);
          cairo_rectangle(cr, cbX, rY, cbW, ddRowH);
          cairo_fill(cr);
        }
        settings_show_text(cr, cbX + 8, rY + ddRowH / 2 + 4, kCurveNames[i], 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);
        if (entry.curve == kCurveNames[i]) {
          settings_show_text(cr, cbX + cbW - 20, rY + ddRowH / 2 + 4, "\u2713", 11, 400, Theme::AccR, Theme::AccG, Theme::AccB, 0.80f);
        }
      }
    }
  }

  ry += kSettingsComboH + 12;

  auto styles = split_styles(get_anim_styles(entry.name.c_str()));
  if (!styles.empty()) {
    settings_show_text(cr, popX + 16, ry + 14, "Style:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);

    int stX = popX + 72;
    int stW = popW - 72 - 16;
    std::vector<std::string> styleOpts;
    styleOpts.push_back("none");
    for (auto& s : styles) styleOpts.push_back(s);
    int styleCount = static_cast<int>(styleOpts.size());

    cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, app.hyprlandAnimStyleDdOpen ? 0.30 : 0.15);
    cairo_rectangle(cr, stX, ry - 2, stW, kSettingsComboH);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.50);
    cairo_set_font_size(cr, 11);
    std::string styleDisp = entry.style.empty() ? "none" : entry.style;
    settings_show_text(cr, stX + 8, ry + kSettingsComboH / 2 + 4, styleDisp.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);
    cairo_move_to(cr, stX + stW - 16, ry + kSettingsComboH / 2 - 3);
    cairo_line_to(cr, stX + stW - 8, ry + kSettingsComboH / 2 - 3);
    cairo_line_to(cr, stX + stW - 12, ry + kSettingsComboH / 2 + 3);
    cairo_close_path(cr);
    cairo_fill(cr);

    if (app.hyprlandAnimStyleDdOpen) {
      constexpr int ddRowH = 24;
      int ddH = styleCount * ddRowH;
      int ddTop = ry + kSettingsComboH + 2;
      cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, 0.95);
      cairo_rectangle(cr, stX, ddTop, stW, ddH);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.50);
      cairo_set_line_width(cr, 1);
      cairo_rectangle(cr, stX, ddTop, stW, ddH);
      cairo_stroke(cr);

      for (int i = 0; i < styleCount; ++i) {
        int rY = ddTop + i * ddRowH;
        if (i == app.hyprlandAnimStyleDdHover) {
          cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.20);
          cairo_rectangle(cr, stX, rY, stW, ddRowH);
          cairo_fill(cr);
        }
        settings_show_text(cr, stX + 8, rY + ddRowH / 2 + 4, styleOpts[i].c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);

        bool isSelected = (i == 0 && entry.style.empty()) || (i > 0 && entry.style == styleOpts[i]);
        if (isSelected) {
          settings_show_text(cr, stX + stW - 20, rY + ddRowH / 2 + 4, "\u2713", 11, 400, Theme::AccR, Theme::AccG, Theme::AccB, 0.80f);
        }
      }
    }
  }
}

// Popup pointer down.
bool hyprland_anim_popup_pointer_down(App& app, int contentX, int contentW) {
  auto& animCfg = app.hyprlandConfig.animations;
  if (app.hyprlandAnimEditIdx < 0 || app.hyprlandAnimEditIdx >= static_cast<int>(animCfg.entries.size()))
    return false;
  auto& entry = animCfg.entries[app.hyprlandAnimEditIdx];

  constexpr int popW = 340;
  constexpr int popH = 300;
  int popX = contentX + (contentW - popW) / 2;
  int popY = kContentTop + 40;

  int bezierBtnY = popY + 50;
  int speedY = bezierBtnY + 42;
  int curveY = speedY + 40;
  int styleY = curveY + kSettingsComboH + 12;
  int cbX = popX + 72;
  int cbW = popW - 72 - 16;
  int cbH = kSettingsComboH;

  bool insidePopup = (app.pointerX >= popX && app.pointerX < popX + popW &&
                      app.pointerY >= popY && app.pointerY < popY + popH);
  bool inCurveDd = false;
  bool inStyleDd = false;
  if (app.hyprlandAnimCurveDdOpen) {
    int ddTop = curveY + cbH + 2;
    int ddH = kCurveCount * 24;
    inCurveDd = (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
                 app.pointerY >= ddTop && app.pointerY < ddTop + ddH);
  }
  if (app.hyprlandAnimStyleDdOpen) {
    auto styles = split_styles(get_anim_styles(entry.name.c_str()));
    int styleCount = 1 + static_cast<int>(styles.size());
    int ddTop = styleY + cbH + 2;
    int ddH = styleCount * 24;
    inStyleDd = (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
                 app.pointerY >= ddTop && app.pointerY < ddTop + ddH);
  }
  if (!insidePopup && !inCurveDd && !inStyleDd) {
    app.hyprlandAnimEditIdx = -1;
    app.hyprlandAnimSpeedEditActive = false;
    app.hyprlandAnimCurveDdOpen = false;
    app.hyprlandAnimCurveDdHover = -1;
    app.hyprlandAnimStyleDdOpen = false;
    app.hyprlandAnimStyleDdHover = -1;
    draw(app);
    return true;
  }

  {
    int closeX = popX + popW - 32;
    int closeY = popY + 8;
    if (app.pointerX >= closeX && app.pointerX < closeX + 20 &&
        app.pointerY >= closeY && app.pointerY < closeY + 20) {
      app.hyprlandAnimEditIdx = -1;
      app.hyprlandAnimSpeedEditActive = false;
      draw(app);
      return true;
    }
  }

  if (app.pointerX >= popX + 16 && app.pointerX < popX + popW - 16 &&
      app.pointerY >= bezierBtnY && app.pointerY < bezierBtnY + 28) {
    if (!get_bezier_for_curve(entry.curve, app.hyprlandBezierP1x, app.hyprlandBezierP1y,
                              app.hyprlandBezierP2x, app.hyprlandBezierP2y)) {
      app.hyprlandBezierP1x = 0.25; app.hyprlandBezierP1y = 0.1;
      app.hyprlandBezierP2x = 0.25; app.hyprlandBezierP2y = 1.0;
    }
    app.hyprlandBezierOpen = true;
    app.hyprlandBezierDrag = -1;
    app.hyprlandAnimCurveDdOpen = false;
    app.hyprlandAnimCurveDdHover = -1;
    app.hyprlandAnimStyleDdOpen = false;
    app.hyprlandAnimStyleDdHover = -1;
    draw(app);
    return true;
  }

  if (app.pointerX >= popX + 72 && app.pointerX < popX + 100 &&
      app.pointerY >= speedY - 2 && app.pointerY < speedY + 26) {
    entry.speed = std::max(0.5, entry.speed - 0.5);
    if (entry.speed < 0.1) entry.speed = 0.5;
    app.hyprlandAnimSpeedEditActive = false;
    hyprland_commit_cfg(app);
    draw(app);
    return true;
  }

  int speedTextX = popX + 104;
  int speedTextW = popW - 104 - 56;
  if (app.pointerX >= speedTextX && app.pointerX < speedTextX + speedTextW &&
      app.pointerY >= speedY - 2 && app.pointerY < speedY + 26) {
    app.hyprlandAnimSpeedEditActive = true;
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%.1f", entry.speed);
    app.hyprlandAnimSpeedEditBuf = tmp;
    draw(app);
    return true;
  }

  if (app.pointerX >= popX + popW - 16 - 28 && app.pointerX < popX + popW - 16 &&
      app.pointerY >= speedY - 2 && app.pointerY < speedY + 26) {
    entry.speed = std::min(30.0, entry.speed + 0.5);
    app.hyprlandAnimSpeedEditActive = false;
    hyprland_commit_cfg(app);
    draw(app);
    return true;
  }

  if (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
      app.pointerY >= curveY - 2 && app.pointerY < curveY + cbH) {
    if (app.hyprlandAnimCurveDdOpen) {
      if (app.hyprlandAnimCurveDdHover >= 0 && app.hyprlandAnimCurveDdHover < kCurveCount) {
        entry.curve = kCurveNames[app.hyprlandAnimCurveDdHover];
        hyprland_commit_cfg(app);
      }
      app.hyprlandAnimCurveDdOpen = false;
      app.hyprlandAnimCurveDdHover = -1;
    } else {
      app.hyprlandAnimCurveDdOpen = true;
      app.hyprlandAnimStyleDdOpen = false;
      app.hyprlandAnimStyleDdHover = -1;
    }
    draw(app);
    return true;
  }

  auto styles = split_styles(get_anim_styles(entry.name.c_str()));
  if (!styles.empty()) {
    if (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
        app.pointerY >= styleY - 2 && app.pointerY < styleY + cbH) {
      if (app.hyprlandAnimStyleDdOpen) {
        int styleCount = 1 + static_cast<int>(styles.size());
        if (app.hyprlandAnimStyleDdHover >= 0 && app.hyprlandAnimStyleDdHover < styleCount) {
          if (app.hyprlandAnimStyleDdHover == 0)
            entry.style = "";
          else
            entry.style = styles[static_cast<size_t>(app.hyprlandAnimStyleDdHover) - 1];
          hyprland_commit_cfg(app);
        }
        app.hyprlandAnimStyleDdOpen = false;
        app.hyprlandAnimStyleDdHover = -1;
      } else {
        app.hyprlandAnimStyleDdOpen = true;
        app.hyprlandAnimCurveDdOpen = false;
        app.hyprlandAnimCurveDdHover = -1;
      }
      draw(app);
      return true;
    }
  }

  if (app.hyprlandAnimCurveDdOpen) {
    constexpr int ddRowH = 24;
    int ddTop = curveY + cbH + 2;
    if (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
        app.pointerY >= ddTop && app.pointerY < ddTop + kCurveCount * ddRowH) {
      int idx = static_cast<int>(app.pointerY - ddTop) / ddRowH;
      if (idx >= 0 && idx < kCurveCount) {
        entry.curve = kCurveNames[idx];
        hyprland_commit_cfg(app);
      }
      app.hyprlandAnimCurveDdOpen = false;
      app.hyprlandAnimCurveDdHover = -1;
      draw(app);
      return true;
    }
  }

  if (app.hyprlandAnimStyleDdOpen) {
    constexpr int ddRowH = 24;
    int ddTop = styleY + cbH + 2;
    int styleCount = 1 + static_cast<int>(styles.size());
    if (app.pointerX >= cbX && app.pointerX < cbX + cbW &&
        app.pointerY >= ddTop && app.pointerY < ddTop + styleCount * ddRowH) {
      int idx = static_cast<int>(app.pointerY - ddTop) / ddRowH;
      if (idx >= 0 && idx < styleCount) {
        if (idx == 0)
          entry.style = "";
        else
          entry.style = styles[static_cast<size_t>(idx) - 1];
        hyprland_commit_cfg(app);
      }
      app.hyprlandAnimStyleDdOpen = false;
      app.hyprlandAnimStyleDdHover = -1;
      draw(app);
      return true;
    }
  }

  return false;
}

// Popup motion.
void hyprland_anim_popup_motion(App& app, int contentX, int contentW, bool& needDdDraw) {
  auto& animCfg = app.hyprlandConfig.animations;
  if (app.hyprlandAnimEditIdx < 0 || app.hyprlandAnimEditIdx >= static_cast<int>(animCfg.entries.size())) {
    if (app.hyprlandAnimCurveDdHover != -1) { app.hyprlandAnimCurveDdHover = -1; needDdDraw = true; }
    if (app.hyprlandAnimStyleDdHover != -1) { app.hyprlandAnimStyleDdHover = -1; needDdDraw = true; }
    return;
  }
  auto& entry = animCfg.entries[app.hyprlandAnimEditIdx];
  constexpr int popW = 340;
  int popX = contentX + (contentW - popW) / 2;
  int popY = kContentTop + 40;
  bool hoverChanged = false;

  if (app.hyprlandAnimCurveDdOpen) {
    constexpr int ddRowH = 24;
    int cbX = popX + 72;
    int cbW = popW - 72 - 16;
    int curveY = popY + 132;
    int ddTop = curveY + kSettingsComboH + 2;
    int nr = settings_mode_dd_pointer_row(app.pointerX, app.pointerY, cbX, ddTop, cbW, ddRowH, kCurveCount);
    if (nr != app.hyprlandAnimCurveDdHover) { app.hyprlandAnimCurveDdHover = nr; hoverChanged = true; }
  } else if (app.hyprlandAnimCurveDdHover != -1) {
    app.hyprlandAnimCurveDdHover = -1; hoverChanged = true;
  }

  if (app.hyprlandAnimStyleDdOpen) {
    constexpr int ddRowH = 24;
    int stX = popX + 72;
    int stW = popW - 72 - 16;
    auto styles = split_styles(get_anim_styles(entry.name.c_str()));
    int styleCount = 1 + static_cast<int>(styles.size());
    int styleY = popY + 132 + kSettingsComboH + 12;
    int ddTop = styleY + kSettingsComboH + 2;
    int nr2 = settings_mode_dd_pointer_row(app.pointerX, app.pointerY, stX, ddTop, stW, ddRowH, styleCount);
    if (nr2 != app.hyprlandAnimStyleDdHover) { app.hyprlandAnimStyleDdHover = nr2; hoverChanged = true; }
  } else if (app.hyprlandAnimStyleDdHover != -1) {
    app.hyprlandAnimStyleDdHover = -1; hoverChanged = true;
  }

  if (hoverChanged) needDdDraw = true;
}
