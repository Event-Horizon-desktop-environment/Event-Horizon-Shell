#include "desktop_shell/power_confirm/power_confirm.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/core/primitives/state_layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace eh::power_confirm {

namespace {

bool trace_enabled() {
  static const bool t = (std::getenv("EH_POWER_CONFIRM_TRACE") != nullptr);
  return t;
}

void trace_geom(const char* tag, double W, double H, double us, double px, double py) {
  if (!trace_enabled()) return;
  const CardGeom g = layout(W, H, us);
  std::fprintf(stderr,
      "[power-confirm] %s W=%.0f H=%.0f us=%.3f ptr=(%.1f,%.1f) card=(%.0f,%.0f %.0fx%.0f) "
      "cancel=(%.0f,%.0f) confirm=(%.0f,%.0f) close=(%.0f,%.0f)\n",
      tag, W, H, us, px, py, g.cx, g.cy, g.cw, g.ch,
      g.cancelX, g.cancelY, g.confirmX, g.confirmY, g.closeX, g.closeY);
}

bool in_rect(double lx, double ly, double rx, double ry, double rw, double rh) {
   
  return lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh;
}

void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
   
  cairo_new_path(cr);
  const double r = std::min({rad, rw * 0.5, rh * 0.5});
  const double x0 = rx, y0 = ry;
  const double x1 = rx + rw, y1 = ry + rh;
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

void glassy_box(cairo_t* cr, double x, double y, double w, double h, double rad,
                float r, float g, float b, float a) {
  if (w <= 0 || h <= 0 || a <= 0) return;
  m3::Box box;
  box.setColor(r, g, b, a);
  box.setRadius(static_cast<float>(rad));
  box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h));
  box.setGlassy(true);
  box.paint(cr);
}

void button_hover_layer(cairo_t* cr, double x, double y, double w, double h, double rad,
                        float r, float g, float b, bool hovered) {
  if (!hovered) return;
  m3::StateLayer layer;
  layer.setColor(r, g, b);
  layer.setRadius(static_cast<float>(rad));
  layer.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h));
  layer.setHovered(true);
  layer.paint(cr);
}

const char* title_for(int idx) {
   
  switch (idx) {
    case 1: return "Log out?";
    case 2: return "Restart?";
    case 3: return "Shut down?";
    default: return "";
  }
}

const char* message_for(int idx) {
   
  switch (idx) {
    case 1: return "End this session and return to the login screen.";
    case 2: return "The system will restart.";
    case 3: return "The system will power off.";
    default: return "";
  }
}

const char* confirm_label_for(int idx) {
   
  switch (idx) {
    case 1: return "Log out";
    case 2: return "Restart";
    case 3: return "Shut down";
    default: return "Confirm";
  }
}

} // anonymous namespace

CardGeom layout(double W, double H, double us) {
   
  CardGeom g{};
  const double kPad = 28.0 * us;
  const double kBtnH = 52.0 * us;
  const double kBtnGap = 12.0 * us;
  const double kTimerH = 28.0 * us;
  constexpr double kProgressH = 4.0;
  const double cardW = std::min(480.0 * us, std::max(0.0, W - 32.0 * us));
  const double cardH = std::min(kPad + 40.0 * us + 10.0 * us + 24.0 * us + 10.0 * us +
                                    kTimerH + 6.0 * us + kProgressH + 16.0 * us + kBtnH + kPad,
                                std::max(0.0, H - 32.0 * us));
  g.cx = (W - cardW) * 0.5;
  g.cy = (H - cardH) * 0.5;
  g.cw = cardW;
  g.ch = cardH;

  g.closeX = g.cx + g.cw - kPad - 32.0 * us;
  g.closeY = g.cy + kPad - 6.0 * us;
  g.closeW = 32.0 * us;
  g.closeH = 32.0 * us;

  const double btnY = g.cy + g.ch - kPad - kBtnH;
  const double btnW = (g.cw - 2.0 * kPad - kBtnGap) * 0.5;
  g.cancelX = g.cx + kPad;
  g.cancelY = btnY;
  g.cancelW = btnW;
  g.cancelH = kBtnH;
  g.confirmX = g.cancelX + btnW + kBtnGap;
  g.confirmY = btnY;
  g.confirmW = btnW;
  g.confirmH = kBtnH;
  return g;
}

Pick pick(double W, double H, int powerIdx, double pointerX, double pointerY, double us) {
    
  trace_geom("pick", W, H, us, pointerX, pointerY);
  if (powerIdx < 1 || powerIdx > 3) return Pick::None;
  if (W <= 0 || H <= 0) return Pick::None;
  const CardGeom g = layout(W, H, us);
  if (in_rect(pointerX, pointerY, g.closeX, g.closeY, g.closeW, g.closeH)) return Pick::Close;
  if (in_rect(pointerX, pointerY, g.cancelX, g.cancelY, g.cancelW, g.cancelH)) return Pick::Cancel;
  if (in_rect(pointerX, pointerY, g.confirmX, g.confirmY, g.confirmW, g.confirmH)) return Pick::Confirm;
  return Pick::None;
}

void paint(cairo_t* cr, double W, double H, int powerIdx,
           double pointerX, double pointerY,
           int remainingSec,
           const eh::config::ShellConfig& sc) {
   
  if (powerIdx < 1 || powerIdx > 3) return;
  if (W <= 0 || H <= 0) return;

  const double us = dock_ui_scale(sc.dock);
  trace_geom("paint", W, H, us, pointerX, pointerY);

  const auto chrome = eh::config::derived_chrome_colors(sc.appearance);
  const double dimR = chrome.drawerDimR;
  const double dimG = chrome.drawerDimG;
  const double dimB = chrome.drawerDimB;
  const double surfR = chrome.dockFillR;
  const double surfG = chrome.dockFillG;
  const double surfB = chrome.dockFillB;
  const double outR = chrome.outlineR;
  const double outG = chrome.outlineG;
  const double outB = chrome.outlineB;
  constexpr double kErrR = 0.93, kErrG = 0.38, kErrB = 0.42;

  const CardGeom g = layout(W, H, us);

  // card shadow
  glassy_box(cr, g.cx + 2.0 * us, g.cy + 3.0 * us, g.cw, g.ch, 18.0 * us, 0.0f, 0.0f, 0.0f, 0.28f);

  // card background (glassy)
  glassy_box(cr, g.cx, g.cy, g.cw, g.ch, 18.0 * us,
             static_cast<float>(surfR), static_cast<float>(surfG), static_cast<float>(surfB), 0.97f);

  // card outline
  rr(cr, g.cx, g.cy, g.cw, g.ch, 18.0 * us);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // close button
  const bool hClose = in_rect(pointerX, pointerY, g.closeX, g.closeY, g.closeW, g.closeH);
  glassy_box(cr, g.closeX, g.closeY, g.closeW, g.closeH, 10.0 * us,
             static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB),
             static_cast<float>(hClose ? 0.55 : 0.40));
  button_hover_layer(cr, g.closeX, g.closeY, g.closeW, g.closeH, 10.0 * us,
                     0.9f, 0.94f, 0.97f, hClose);
  eh::shell::draw_material_glyph(cr, g.closeX + g.closeW * 0.5,
                                   g.closeY + g.closeH * 0.5, 20.0 * us, "close",
                                   0.88, 0.92, 0.95, 1.0);

  // title
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 20.0 * us);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_move_to(cr, g.cx + 28.0 * us, g.cy + 28.0 * us + 16.0 * us);
  cairo_show_text(cr, title_for(powerIdx));

  // message
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0 * us);
  cairo_set_source_rgba(cr, outR + 0.15, outG + 0.12, outB + 0.12, 0.88);
  cairo_move_to(cr, g.cx + 28.0 * us, g.cy + 28.0 * us + 40.0 * us + 10.0 * us + 16.0 * us);
  cairo_show_text(cr, message_for(powerIdx));

  // timer position
  const double timerY = g.cy + 28.0 * us + 40.0 * us + 10.0 * us + 24.0 * us + 10.0 * us;
  const double pbarX = g.cx + 28.0 * us;
  const double pbarW = g.cw - 56.0 * us;
  const double pbarY = timerY + 28.0 * us + 6.0 * us;

  // countdown progress bar background (glassy)
  glassy_box(cr, pbarX, pbarY, pbarW, 4.0, 2.0,
             static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB), 0.55f);

  // countdown progress bar fill
  const double fraction = std::clamp(static_cast<double>(remainingSec) / 60.0, 0.0, 1.0);
  if (fraction > 0.0) {
    glassy_box(cr, pbarX, pbarY, pbarW * fraction, 4.0, 2.0,
               static_cast<float>(chrome.accentR), static_cast<float>(chrome.accentG),
               static_cast<float>(chrome.accentB), 0.85f);
  }

  // countdown timer text (bigger)
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 22.0 * us);
  cairo_set_source_rgba(cr, 0.88, 0.92, 0.95, 0.85);
  std::string timerText;
  if (remainingSec > 0) {
    timerText = std::to_string(remainingSec) + "s";
  } else {
    timerText = "0s";
  }
  {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, timerText.c_str(), &ex);
    cairo_move_to(cr, g.cx + g.cw * 0.5 - ex.x_advance * 0.5,
                  timerY + ex.height);
    cairo_show_text(cr, timerText.c_str());
  }

  // cancel button (glassy)
  const bool hCan = in_rect(pointerX, pointerY, g.cancelX, g.cancelY, g.cancelW, g.cancelH);
  glassy_box(cr, g.cancelX, g.cancelY, g.cancelW, g.cancelH, 12.0 * us,
             static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB),
             static_cast<float>(hCan ? 0.55 : 0.42));
  rr(cr, g.cancelX, g.cancelY, g.cancelW, g.cancelH, 12.0 * us);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.45);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  button_hover_layer(cr, g.cancelX, g.cancelY, g.cancelW, g.cancelH, 12.0 * us,
                     0.9f, 0.94f, 0.97f, hCan);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 15.0 * us);
  cairo_set_source_rgba(cr, 0.90, 0.94, 0.96, 0.95);
  {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, "Cancel", &ex);
    cairo_move_to(cr, g.cancelX + (g.cancelW - ex.x_advance) * 0.5,
                  g.cancelY + g.cancelH * 0.5 + ex.height * 0.35);
    cairo_show_text(cr, "Cancel");
  }

  // confirm button (glassy, danger tint)
  const bool hOk = in_rect(pointerX, pointerY, g.confirmX, g.confirmY, g.confirmW, g.confirmH);
  glassy_box(cr, g.confirmX, g.confirmY, g.confirmW, g.confirmH, 12.0 * us,
             static_cast<float>(kErrR), static_cast<float>(kErrG), static_cast<float>(kErrB),
             static_cast<float>(hOk ? 0.26 : 0.16));
  rr(cr, g.confirmX, g.confirmY, g.confirmW, g.confirmH, 12.0 * us);
  cairo_set_source_rgba(cr, kErrR, kErrG, kErrB, 0.85);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  button_hover_layer(cr, g.confirmX, g.confirmY, g.confirmW, g.confirmH, 12.0 * us,
                     0.93f, 0.38f, 0.42f, hOk);
  cairo_set_source_rgba(cr, kErrR + 0.05, kErrG + 0.35, kErrB + 0.35, 0.98);
  {
    const char* cl = confirm_label_for(powerIdx);
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, cl, &ex);
    cairo_move_to(cr, g.confirmX + (g.confirmW - ex.x_advance) * 0.5,
                  g.confirmY + g.confirmH * 0.5 + ex.height * 0.35);
    cairo_show_text(cr, cl);
  }
}

} // namespace eh::power_confirm
