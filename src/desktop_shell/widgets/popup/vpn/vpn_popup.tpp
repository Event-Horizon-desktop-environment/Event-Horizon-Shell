#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include <cairo/cairo.h>

namespace eh::shell::dock::popup::vpn {

// Forward declarations (implemented in vpn_popup.cpp).
void vpn_popup_sync_entries(const std::vector<VpnConnectionInfo>& connections);
int  vpn_popup_entry_count();
const std::string& vpn_popup_entry_name(int idx);
bool vpn_popup_entry_active(int idx);
void vpn_popup_toggle_entry(int idx);
void vpn_popup_remove_entry(int idx);

namespace {

constexpr double kPad          = 16.0;
constexpr double kCornerRadius = 14.0;
constexpr double kRowH         = 44.0;
constexpr double kBtnW         = 84;
constexpr double kBtnH         = 26;
constexpr double kDotR         = 4.0;
constexpr double kHeaderH      = 48.0;
constexpr double kRemoveBtnSz  = 22.0;

constexpr double kShadOffX  = 2.0;
constexpr double kShadOffY  = 4.0;
constexpr double kShadAlpha = 0.28;

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

} // namespace

template<typename A>
inline void dock_vpn_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  const auto& st = nm.state();

  vpn_popup_sync_entries(st.vpnConnections);

  const int vpnCount = vpn_popup_entry_count();
  const double W = static_cast<double>(kVpnPopupW);
  const double H = kPad + kHeaderH + static_cast<double>(std::max(vpnCount, 1)) * kRowH + kPad;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const double shellOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Weather));

  // Shadow.
  cairo_save(cr);
  rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  // Background.
  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), static_cast<float>(0.78 * shellOv));
    box.setRadius(static_cast<float>(kCornerRadius));
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, W - 1.0, H - 1.0, kCornerRadius);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Close button.
  const double cbX = W - kPad - 24.0;
  const double cbY = kPad;
  const double cbW = 24.0, cbH = 24.0;
  const double px = app.pointerX, py = app.pointerY;
  const bool hc = px >= cbX && px < cbX + cbW && py >= cbY && py < cbY + cbH;
  {
    m3::Box cbBox;
    cbBox.setColor(0.3f, 0.3f, 0.35f, hc ? 0.75f : 0.45f);
    cbBox.setRadius(7.0f);
    cbBox.setGeometry(static_cast<float>(cbX), static_cast<float>(cbY),
                      static_cast<float>(cbW), static_cast<float>(cbH));
    cbBox.setGlassy(true);
    cbBox.paint(cr);
  }
  eh::shell::draw_material_glyph(cr, cbX + cbW * 0.5, cbY + cbH * 0.5,
                                 14, "close", mc.textR, mc.textG, mc.textB, hc ? 1.0 : 0.85);

  // Header.
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  cairo_move_to(cr, kPad, kPad + 30);
  cairo_show_text(cr, "VPN Connections");

  // VPN entries.
  for (int vi = 0; vi < vpnCount; ++vi) {
    const auto& name = vpn_popup_entry_name(vi);
    const bool active = vpn_popup_entry_active(vi);
    const double rowY = kPad + kHeaderH + static_cast<double>(vi) * kRowH;
    const double rowMidY = rowY + kRowH * 0.5;
    const double remX = W - kPad - kRemoveBtnSz;
    const double btnX = remX - 6.0 - kBtnW;
    const double btnY = rowMidY - kBtnH * 0.5;
    const double remY = rowMidY - kRemoveBtnSz * 0.5;
    const double pillR = std::max(2.0, std::min(kRowH, W - kPad * 2) * 0.22);

    // Pill background (glassy)
    {
      m3::Box pill;
      pill.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                    static_cast<float>(mc.drawerDimB), 0.35f);
      pill.setRadius(static_cast<float>(pillR));
      pill.setGeometry(static_cast<float>(kPad), static_cast<float>(rowY),
                       static_cast<float>(W - kPad * 2), static_cast<float>(kRowH));
      pill.setGlassy(true);
      pill.paint(cr);
    }

    // Hover overlay
    const bool pillHover = px >= kPad && px < kPad + W - kPad * 2 &&
                           py >= rowY && py < rowY + kRowH;
    if (pillHover) {
      rounded_rect(cr, kPad, rowY, W - kPad * 2, kRowH, pillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Status dot
    const double dotCX = kPad + 16;
    if (active)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
    else
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.35);
    cairo_arc(cr, dotCX, rowMidY, kDotR, 0, 2 * M_PI);
    cairo_fill(cr);

    // VPN name
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 1.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, dotCX + 16, rowMidY + 5);
    cairo_show_text(cr, name.c_str());

    // Remove button (trash icon, glassy).
    const bool remHover = px >= remX && px < remX + kRemoveBtnSz &&
                          py >= remY && py < remY + kRemoveBtnSz;
    {
      m3::Box rem;
      rem.setColor(0.85f, 0.25f, 0.20f, remHover ? 0.40f : 0.20f);
      rem.setRadius(5.0f);
      rem.setGeometry(static_cast<float>(remX), static_cast<float>(remY),
                      static_cast<float>(kRemoveBtnSz), static_cast<float>(kRemoveBtnSz));
      rem.setGlassy(true);
      rem.paint(cr);
    }
    eh::shell::draw_material_glyph(cr, remX + kRemoveBtnSz * 0.5, remY + kRemoveBtnSz * 0.5,
                                   12, "delete", mc.textR, mc.textG, mc.textB, remHover ? 0.85 : 0.55);

    // Connect / Disconnect button (glassy).
    const bool btnHover = px >= btnX && px < btnX + kBtnW &&
                          py >= btnY && py < btnY + kBtnH;
    {
      float br, bg, bb, ba;
      if (active) {
        br = 0.85f; bg = 0.25f; bb = 0.20f; ba = btnHover ? 0.85f : 0.65f;
      } else {
        br = static_cast<float>(mc.accentR); bg = static_cast<float>(mc.accentG); bb = static_cast<float>(mc.accentB);
        ba = btnHover ? 0.85f : 0.65f;
      }
      m3::Box btn;
      btn.setColor(br, bg, bb, ba);
      btn.setRadius(6.0f);
      btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                      static_cast<float>(kBtnW), static_cast<float>(kBtnH));
      btn.setGlassy(true);
      btn.paint(cr);
    }

    const char* lbl = active ? "Disconnect" : "Connect";
    cairo_set_source_rgba(cr, 1, 1, 1, btnHover ? 1.0 : 0.90);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    cairo_text_extents_t te;
    cairo_text_extents(cr, lbl, &te);
    cairo_move_to(cr, btnX + (kBtnW - te.x_advance) * 0.5, btnY + kBtnH * 0.5 + te.height * 0.35);
    cairo_show_text(cr, lbl);
  }
}

inline int vpn_popup_height(int vpnCount) {
  return static_cast<int>(kPad + kHeaderH + std::max(vpnCount, 1) * kRowH + kPad);
}

} // namespace eh::shell::dock::popup::vpn
