#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.hpp"

#include "configuration/shell_config.hpp"
#include "m3/core/primitives/box.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <algorithm>
#include <cmath>
namespace eh::shell::dock::app_drawer {
namespace {

struct Rect {
  double x, y, w, h;
};

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

bool in_rect(double lx, double ly, const Rect& r) {
   
  return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
}

struct ModalGeom {
  Rect card{};
  Rect closeHit{};
  Rect cancelBtn{};
  Rect confirmBtn{};
};

const char* title_for(int pending) {
   
  switch (pending) {
    case 1:
      return "Log out?";
    case 2:
      return "Restart?";
    case 3:
      return "Shut down?";
    default:
      return "";
  }
}

const char* message_for(int pending) {
   
  switch (pending) {
    case 1:
      return "End this session and return to the login screen.";
    case 2:
      return "The system will restart.";
    case 3:
      return "The system will power off.";
    default:
      return "";
  }
}

const char* confirm_label_for(int pending) {
   
  switch (pending) {
    case 1:
      return "Log out";
    case 2:
      return "Restart";
    case 3:
      return "Shut down";
    default:
      return "Confirm";
  }
}

ModalGeom layout_modal(double W, double H, int pendingPowerIdx) {
   
  ModalGeom g{};
  if (pendingPowerIdx < 1 || pendingPowerIdx > 3) return g;

  constexpr double kPad = 20.0;
  constexpr double kBtnH = 48.0;
  constexpr double kBtnGap = 10.0;
  const double cardW = std::min(400.0, W - 32.0);
  const double cardH = kPad + 36.0 + 8.0 + 40.0 + 20.0 + kBtnH + kPad;
  g.card.x = (W - cardW) * 0.5;
  g.card.y = (H - cardH) * 0.5;
  g.card.w = cardW;
  g.card.h = cardH;

  g.closeHit = {g.card.x + g.card.w - kPad - 28.0, g.card.y + kPad - 4.0, 28.0, 28.0};

  const double btnY = g.card.y + g.card.h - kPad - kBtnH;
  const double btnW = (g.card.w - 2.0 * kPad - kBtnGap) * 0.5;
  g.cancelBtn = {g.card.x + kPad, btnY, btnW, kBtnH};
  g.confirmBtn = {g.cancelBtn.x + btnW + kBtnGap, btnY, btnW, kBtnH};
  return g;
}

}

PowerConfirmPick pick_power_confirm_modal(double popupW, double popupH, int pendingPowerIdx, double lx, double ly) {
   
  if (pendingPowerIdx < 1 || pendingPowerIdx > 3) return PowerConfirmPick::Outside;
  const ModalGeom g = layout_modal(popupW, popupH, pendingPowerIdx);
  if (in_rect(lx, ly, g.closeHit)) return PowerConfirmPick::CloseX;
  if (in_rect(lx, ly, g.cancelBtn)) return PowerConfirmPick::Cancel;
  if (in_rect(lx, ly, g.confirmBtn)) return PowerConfirmPick::Confirm;
  if (in_rect(lx, ly, g.card)) return PowerConfirmPick::Outside;
  return PowerConfirmPick::Outside;
}

void paint_power_confirm_modal(cairo_t* cr, double popupW, double popupH, int pendingPowerIdx, double pointerX,
                               double pointerY, const eh::config::ChromePaintColors& chrome) {
   
  if (pendingPowerIdx < 1 || pendingPowerIdx > 3) return;

  const ModalGeom g = layout_modal(popupW, popupH, pendingPowerIdx);
  const double surfR = chrome.dockFillR, surfG = chrome.dockFillG, surfB = chrome.dockFillB;
  const double outR = chrome.outlineR, outG = chrome.outlineG, outB = chrome.outlineB;
  const double dimR = chrome.drawerDimR, dimG = chrome.drawerDimG, dimB = chrome.drawerDimB;
  constexpr double kErrR = 0.93, kErrG = 0.38, kErrB = 0.42;

  cairo_save(cr);

  {
    m3::Box box;
    box.setColor(0.0f, 0.0f, 0.0f, 0.22f);
    box.setRadius(14.0f);
    box.setGeometry(g.card.x + 2.0, g.card.y + 3.0, g.card.w, g.card.h);
    box.setGlassy(true);
    box.paint(cr);
  }

  {
    m3::Box box;
    box.setColor(static_cast<float>(surfR), static_cast<float>(surfG), static_cast<float>(surfB), 0.98f);
    box.setRadius(14.0f);
    box.setGeometry(g.card.x, g.card.y, g.card.w, g.card.h);
    box.setGlassy(true);
    box.paint(cr);
  }

  rr(cr, g.card.x, g.card.y, g.card.w, g.card.h, 14.0);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const bool hClose = in_rect(pointerX, pointerY, g.closeHit);
  {
    m3::Box box;
    box.setColor(static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB),
                 static_cast<float>(hClose ? 0.55 : 0.40));
    box.setRadius(8.0f);
    box.setGeometry(g.closeHit.x, g.closeHit.y, g.closeHit.w, g.closeHit.h);
    box.setGlassy(true);
    box.paint(cr);
  }
  eh::shell::draw_material_glyph(cr, g.closeHit.x + g.closeHit.w * 0.5, g.closeHit.y + g.closeHit.h * 0.5, 18.0, "close",
                                   0.88, 0.92, 0.95, 1.0);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 17.0);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_move_to(cr, g.card.x + 20.0, g.card.y + 20.0 + 14.0);
  cairo_show_text(cr, title_for(pendingPowerIdx));

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  cairo_set_source_rgba(cr, outR + 0.15, outG + 0.12, outB + 0.12, 0.88);
  const char* msg = message_for(pendingPowerIdx);
  cairo_move_to(cr, g.card.x + 20.0, g.card.y + 52.0 + 14.0);
  cairo_show_text(cr, msg);

  const bool hCan = in_rect(pointerX, pointerY, g.cancelBtn);
  {
    m3::Box box;
    box.setColor(static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB),
                 static_cast<float>(hCan ? 0.52 : 0.42));
    box.setRadius(10.0f);
    box.setGeometry(g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h);
    box.setGlassy(true);
    box.paint(cr);
  }

  rr(cr, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h, 10.0);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.45);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, 0.90, 0.94, 0.96, 0.95);
  {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, "Cancel", &ex);
    cairo_move_to(cr, g.cancelBtn.x + (g.cancelBtn.w - ex.x_advance) * 0.5,
                  g.cancelBtn.y + g.cancelBtn.h * 0.5 + ex.height * 0.35);
    cairo_show_text(cr, "Cancel");
  }

  const bool hOk = in_rect(pointerX, pointerY, g.confirmBtn);
  {
    m3::Box box;
    box.setColor(static_cast<float>(kErrR), static_cast<float>(kErrG), static_cast<float>(kErrB),
                 static_cast<float>(hOk ? 0.22 : 0.12));
    box.setRadius(10.0f);
    box.setGeometry(g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h);
    box.setGlassy(true);
    box.paint(cr);
  }

  rr(cr, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h, 10.0);
  cairo_set_source_rgba(cr, kErrR, kErrG, kErrB, 0.85);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  cairo_set_source_rgba(cr, kErrR + 0.05, kErrG + 0.35, kErrB + 0.35, 0.98);
  {
    const char* cl = confirm_label_for(pendingPowerIdx);
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, cl, &ex);
    cairo_move_to(cr, g.confirmBtn.x + (g.confirmBtn.w - ex.x_advance) * 0.5,
                  g.confirmBtn.y + g.confirmBtn.h * 0.5 + ex.height * 0.35);
    cairo_show_text(cr, cl);
  }

  cairo_restore(cr);
}

}
