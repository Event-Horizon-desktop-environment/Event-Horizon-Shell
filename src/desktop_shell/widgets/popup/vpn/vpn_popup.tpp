#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

namespace eh::shell::dock::popup::vpn {

void vpn_popup_sync_entries(const std::vector<VpnConnectionInfo>& connections);
int  vpn_popup_entry_count();
const std::string& vpn_popup_entry_name(int idx);
bool vpn_popup_entry_active(int idx);
void vpn_popup_toggle_entry(int idx);
void vpn_popup_remove_entry(int idx);

namespace {

constexpr double kPad          = 16.0;
constexpr double kCornerRadius = 20.0;
constexpr double kRowH         = 44.0;
constexpr double kBtnW         = 84;
constexpr double kBtnH         = 26;
constexpr double kDotR         = 4.0;
constexpr double kHeaderH      = 48.0;
constexpr double kRemoveBtnSz  = 22.0;

constexpr double kShadOffX  = 2.0;
constexpr double kShadOffY  = 4.0;
constexpr double kShadAlpha = 0.28;

void utf8_pop_back(std::string& s) {
  if (s.empty()) return;
  size_t i = s.size();
  while (i > 0) {
    --i;
    if ((static_cast<unsigned char>(s[i]) & 0xc0u) != 0x80u) {
      s.resize(i);
      return;
    }
  }
  s.clear();
}

void truncate_to_width(cairo_t* cr, const std::string& text, double maxAdv, std::string* out) {
  *out = text;
  cairo_text_extents_t ex{};
  for (;;) {
    cairo_text_extents(cr, out->c_str(), &ex);
    if (ex.x_advance <= maxAdv || out->size() < 4) break;
    utf8_pop_back(*out);
  }
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

  cairo_save(cr);
  eh::shell::shared::rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::shared::rounded_rect(cr, 0, 0, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double cbX = W - kPad - 24.0;
  const double cbY = kPad;
  const double cbW = 24.0, cbH = 24.0;
  const double px = app.pointerX, py = app.pointerY;
  const bool hc = px >= cbX && px < cbX + cbW && py >= cbY && py < cbY + cbH;
  eh::shell::shared::rounded_rect(cr, cbX, cbY, cbW, cbH, 7.0);
  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, hc ? 0.75 : 0.45);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, cbX + cbW * 0.5, cbY + cbH * 0.5,
                                  14, "close", mc.textR, mc.textG, mc.textB, hc ? 1.0 : 0.85);

  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  cairo_move_to(cr, kPad, kPad + 30);
  cairo_show_text(cr, "VPN Connections");

  if (vpnCount == 0) {
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    const char* msg = "No VPN connections";
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, msg, &ex);
    cairo_move_to(cr, (W - ex.x_advance) * 0.5, kPad + kHeaderH + kRowH * 0.5 + 4);
    cairo_show_text(cr, msg);
    return;
  }

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

    eh::shell::shared::rounded_rect(cr, kPad, rowY, W - kPad * 2, kRowH, pillR);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
    cairo_fill(cr);

    const bool pillHover = px >= kPad && px < kPad + W - kPad * 2 &&
                           py >= rowY && py < rowY + kRowH;
    if (pillHover) {
      eh::shell::shared::rounded_rect(cr, kPad, rowY, W - kPad * 2, kRowH, pillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    const double dotCX = kPad + 16;
    if (active)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
    else
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.35);
    cairo_arc(cr, dotCX, rowMidY, kDotR, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 1.0);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    std::string nameShown;
    truncate_to_width(cr, name, btnX - 6.0 - (dotCX + 16), &nameShown);
    cairo_move_to(cr, dotCX + 16, rowMidY + 5);
    cairo_show_text(cr, nameShown.c_str());

    const bool remHover = px >= remX && px < remX + kRemoveBtnSz &&
                          py >= remY && py < remY + kRemoveBtnSz;
    eh::shell::shared::rounded_rect(cr, remX, remY, kRemoveBtnSz, kRemoveBtnSz, 5.0);
    cairo_set_source_rgba(cr, 0.85, 0.25, 0.20, remHover ? 0.40 : 0.20);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, remX + kRemoveBtnSz * 0.5, remY + kRemoveBtnSz * 0.5,
                                    12, "delete", mc.textR, mc.textG, mc.textB, remHover ? 0.85 : 0.55);

    const bool btnHover = px >= btnX && px < btnX + kBtnW &&
                          py >= btnY && py < btnY + kBtnH;
    if (active) {
      eh::shell::shared::rounded_rect(cr, btnX, btnY, kBtnW, kBtnH, 6.0);
      cairo_set_source_rgba(cr, 0.85, 0.25, 0.20, btnHover ? 0.85 : 0.65);
    } else {
      eh::shell::shared::rounded_rect(cr, btnX, btnY, kBtnW, kBtnH, 6.0);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, btnHover ? 0.85 : 0.65);
    }
    cairo_fill(cr);

    const char* lbl = active ? "Disconnect" : "Connect";
    cairo_set_source_rgba(cr, 1, 1, 1, btnHover ? 1.0 : 0.90);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
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
