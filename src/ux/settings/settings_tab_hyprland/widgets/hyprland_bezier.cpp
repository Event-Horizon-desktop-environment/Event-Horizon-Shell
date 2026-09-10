#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/common/hyprland_anim_common.hpp"
#include "ux/settings/settings_tab_hyprland/widgets/hyprland_bezier.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"

extern void draw(App& app);

void paint_hyprland_bezier_editor(App& app, cairo_t* cr, int contentX, int contentW) {
  (void)contentX;
  if (!app.hyprlandBezierOpen) return;

  constexpr int popW = 360;
  constexpr int popH = 360;
  int popX = (contentW - popW) / 2;
  int popY = kContentTop + 20;

  settings_card(app, cr, popX, popY, popW, popH, 1.0);

  settings_show_text(cr, popX + 16, popY + 24, "Bezier Curve Editor", 14, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.93f);

  constexpr int gridOffX = 50;
  constexpr int gridOffY = 45;
  constexpr int gridSize = 230;
  int gx = popX + gridOffX;
  int gy = popY + gridOffY;

  cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.50);
  cairo_rectangle(cr, gx, gy, gridSize, gridSize);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.10);
  cairo_set_line_width(cr, 0.5);
  for (int i = 0; i <= 4; ++i) {
    double p = i * gridSize / 4.0;
    cairo_move_to(cr, gx + p, gy);
    cairo_line_to(cr, gx + p, gy + gridSize);
    cairo_move_to(cr, gx, gy + p);
    cairo_line_to(cr, gx + gridSize, gy + p);
  }
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.15);
  cairo_set_line_width(cr, 0.5);
  cairo_move_to(cr, gx, gy + gridSize);
  cairo_line_to(cr, gx + gridSize, gy);
  cairo_stroke(cr);

  double p1x = gx + app.hyprlandBezierP1x * gridSize;
  double p1y = gy + (1.0 - app.hyprlandBezierP1y) * gridSize;
  double p2x = gx + app.hyprlandBezierP2x * gridSize;
  double p2y = gy + (1.0 - app.hyprlandBezierP2y) * gridSize;

  double cp1x = std::clamp(p1x, static_cast<double>(gx), static_cast<double>(gx + gridSize));
  double cp1y = std::clamp(p1y, static_cast<double>(gy), static_cast<double>(gy + gridSize));
  double cp2x = std::clamp(p2x, static_cast<double>(gx), static_cast<double>(gx + gridSize));
  double cp2y = std::clamp(p2y, static_cast<double>(gy), static_cast<double>(gy + gridSize));

  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.25);
  cairo_set_line_width(cr, 1);
  cairo_set_dash(cr, nullptr, 0, 0);
  cairo_move_to(cr, gx, gy + gridSize);
  cairo_line_to(cr, cp1x, cp1y);
  cairo_stroke(cr);
  cairo_move_to(cr, gx + gridSize, gy);
  cairo_line_to(cr, cp2x, cp2y);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.85);
  cairo_set_line_width(cr, 2.5);
  cairo_move_to(cr, gx, gy + gridSize);
  cairo_curve_to(cr, cp1x, cp1y, cp2x, cp2y, gx + gridSize, gy);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.50);
  cairo_arc(cr, gx, gy + gridSize, 4, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_arc(cr, gx + gridSize, gy, 4, 0, 2 * M_PI);
  cairo_fill(cr);

  auto draw_cp = [&](double cx, double cy, bool active) {
    if (active) {
      cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.95);
      cairo_arc(cr, cx, cy, 7, 0, 2 * M_PI);
      cairo_fill(cr);
    } else {
      cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.60);
      cairo_arc(cr, cx, cy, 6, 0, 2 * M_PI);
      cairo_fill(cr);
    }
  };
  draw_cp(cp1x, cp1y, app.hyprlandBezierDrag == 0);
  draw_cp(cp2x, cp2y, app.hyprlandBezierDrag == 1);

  settings_show_text(cr, gx - 8, gy + gridSize + 14, "0", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45f);
  settings_show_text(cr, gx + gridSize - 6, gy + gridSize + 14, "1", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45f);
  settings_show_text(cr, gx - 16, gy + gridSize, "0", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45f);
  settings_show_text(cr, gx - 16, gy - 3, "1", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45f);

  char coordBuf[128];
  std::snprintf(coordBuf, sizeof(coordBuf), "P1: (%.2f, %.2f)    P2: (%.2f, %.2f)",
                app.hyprlandBezierP1x, app.hyprlandBezierP1y,
                app.hyprlandBezierP2x, app.hyprlandBezierP2y);
  settings_show_text(cr, popX + 16, gy + gridSize + 36, coordBuf, 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.65f);

  constexpr int btnW = 80;
  constexpr int btnH = 28;
  int closeX = popX + popW - btnW - 16;
  int closeY = popY + popH - btnH - 14;
  {
    bool hovered = app.pointerX >= closeX && app.pointerX < closeX + btnW &&
                   app.pointerY >= closeY && app.pointerY < closeY + btnH;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel("Close");
    btn.setGeometry(static_cast<float>(closeX), static_cast<float>(closeY),
                    static_cast<float>(btnW), static_cast<float>(btnH));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hovered);
    btn.paint(cr);
  }

  int saveX = closeX - btnW - 8;
  int saveY = closeY;
  {
    bool hovered = app.pointerX >= saveX && app.pointerX < saveX + btnW &&
                   app.pointerY >= saveY && app.pointerY < saveY + btnH;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel("Save");
    btn.setGeometry(static_cast<float>(saveX), static_cast<float>(saveY),
                    static_cast<float>(btnW), static_cast<float>(btnH));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hovered);
    btn.paint(cr);
  }
}

bool hyprland_bezier_pointer_down(App& app, int contentX, int contentW) {
  (void)contentX;
  if (!app.hyprlandBezierOpen) return false;

  constexpr int popW = 360;
  constexpr int popH = 360;
  int popX = (contentW - popW) / 2;
  int popY = kContentTop + 20;

  constexpr int btnW = 80;
  constexpr int btnH = 28;
  int closeX = popX + popW - btnW - 16;
  int closeY = popY + popH - btnH - 14;
  int saveX = closeX - btnW - 8;
  int saveY = closeY;

  if (app.pointerX >= closeX && app.pointerX < closeX + btnW &&
      app.pointerY >= closeY && app.pointerY < closeY + btnH) {
    app.hyprlandBezierOpen = false;
    app.hyprlandBezierDrag = -1;
    draw(app);
    return true;
  }

  if (app.pointerX >= saveX && app.pointerX < saveX + btnW &&
      app.pointerY >= saveY && app.pointerY < saveY + btnH) {
    auto& animCfg = app.hyprlandConfig.animations;
    if (app.hyprlandAnimEditIdx >= 0 && app.hyprlandAnimEditIdx < static_cast<int>(animCfg.entries.size())) {
      auto& entry = animCfg.entries[app.hyprlandAnimEditIdx];
      entry.curve = "eh_custom";
      hyprland_commit_cfg(app);
    }
    app.hyprlandBezierOpen = false;
    app.hyprlandBezierDrag = -1;
    draw(app);
    return true;
  }

  constexpr int gridOffX = 50;
  constexpr int gridOffY = 45;
  constexpr int gridSize = 230;
  int gx = popX + gridOffX;
  int gy = popY + gridOffY;

  double p1sx = gx + app.hyprlandBezierP1x * gridSize;
  double p1sy = gy + (1.0 - app.hyprlandBezierP1y) * gridSize;
  double p2sx = gx + app.hyprlandBezierP2x * gridSize;
  double p2sy = gy + (1.0 - app.hyprlandBezierP2y) * gridSize;

  double dx1 = app.pointerX - p1sx;
  double dy1 = app.pointerY - p1sy;
  double dx2 = app.pointerX - p2sx;
  double dy2 = app.pointerY - p2sy;

  if (dx1 * dx1 + dy1 * dy1 < 12.0 * 12.0) {
    app.hyprlandBezierDrag = 0;
    return true;
  }
  if (dx2 * dx2 + dy2 * dy2 < 12.0 * 12.0) {
    app.hyprlandBezierDrag = 1;
    return true;
  }

  return false;
}

void hyprland_bezier_motion(App& app, int contentX, int contentW, bool& needDdDraw) {
  (void)contentX;
  if (!app.hyprlandBezierOpen || app.hyprlandBezierDrag < 0) return;

  constexpr int popW = 360;
  constexpr int gridOffX = 50;
  constexpr int gridOffY = 45;
  constexpr int gridSize = 230;
  int popX = (contentW - popW) / 2;
  int popY = kContentTop + 20;
  int gx = popX + gridOffX;
  int gy = popY + gridOffY;

  double nx = (app.pointerX - gx) / static_cast<double>(gridSize);
  double ny = 1.0 - (app.pointerY - gy) / static_cast<double>(gridSize);
  nx = std::clamp(nx, 0.0, 1.0);
  ny = std::clamp(ny, 0.0, 1.0);

  if (app.hyprlandBezierDrag == 0) {
    app.hyprlandBezierP1x = nx;
    app.hyprlandBezierP1y = ny;
  } else if (app.hyprlandBezierDrag == 1) {
    app.hyprlandBezierP2x = nx;
    app.hyprlandBezierP2y = ny;
  }
  needDdDraw = true;
}
