#include "desktop_shell/controlcenter/paint/control_center_pear_paint.hpp"

#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/ui/slider/ui_slider.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "services/mpris/mpris_player.hpp"
#include "services/network/core/network_manager_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "configuration/shell_config.hpp"

namespace ccl = eh::shell::dock::control_center;
namespace ccu = eh::shell::dock::control_center::paint_utils;

namespace {

bool g_nightActiveInit = false;
bool g_nightActive = false;

void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
  ccu::rrect(cr, x, y, w, h, r);
}

} // namespace

bool eh::shell::dock::control_center::pear_nightlight_active(const eh::config::ShellConfig& sc) {
  if (!g_nightActiveInit) {
    g_nightActiveInit = true;
    g_nightActive = sc.nightLight.enabled;
  }
  return g_nightActive;
}

void eh::shell::dock::control_center::pear_nightlight_set_active(bool active) {
  g_nightActiveInit = true;
  g_nightActive = active;
}

void pear_center_paint(ccl::ControlCenterState& state, cairo_t* cr, int popupW,
                              const eh::config::ShellConfig& scPopupOv,
                              eh::mpris::DockMpris* mpris, const std::string& ccWidgetId) {
  const double W = static_cast<double>(popupW);
  const ccl::PearCenterConfig cfg = ccl::pear_center_config(scPopupOv, ccWidgetId);
  const double uiScale = dock_ui_scale(scPopupOv.dock);
  ccl::PearLayout L = ccl::cc_compute_pear_layout(W, state, cfg, uiScale);
  const double us = L.us;

  timespec tsNow{};
  clock_gettime(CLOCK_MONOTONIC, &tsNow);
  const uint64_t nowMs =
      static_cast<uint64_t>(tsNow.tv_sec) * 1000ULL + static_cast<uint64_t>(tsNow.tv_nsec / 1000000ULL);

  using CcHT = ccl::CcHoverTarget;
  const double ccShellOv = static_cast<double>(eh::config::overlay_surface_alpha_scale(
      scPopupOv, eh::config::OverlaySurfaceAlphaKind::ControlCenter));
  const double ccInnerGlass =
      static_cast<double>(eh::config::control_center_inner_glass_alpha_scale(scPopupOv));
  const eh::config::ChromePaintColors mcCc = eh::config::derived_chrome_colors(scPopupOv.appearance);

  auto card = [&](double x, double y, double w, double h, double r = 20.0) {
    ccu::cc_paint_glass_card_mc(cr, x, y, w, h, r, ccInnerGlass, mcCc);
  };
  auto row_bg = [&](double x, double y, double w, double h, double r = 14.0) {
    rrect(cr, x, y, w, h, r);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    cairo_fill(cr);
  };
  auto selected_bg = [&](double x, double y, double w, double h, double r = 14.0) {
    rrect(cr, x, y, w, h, r);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.16);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.40);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  };
  auto hover_hl = [&](double x, double y, double w, double h, double r = 14.0) {
    rrect(cr, x, y, w, h, r);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  };

  // Shadow + shell glass (same treatment as the legacy popup).
  cairo_save(cr);
  rrect(cr, 2.0, 3.0, W, L.totalH, 24.0);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
  cairo_fill(cr);
  cairo_restore(cr);
  eh::shell::shared::paint_glass_card(cr, 0, 0, W, L.totalH, 24.0, mcCc, ccShellOv);
  cairo_save(cr);
  rrect(cr, 0, 0, W, L.totalH, 24.0);
  cairo_clip(cr);

  const auto ns = eh::shell::dock_slot_hooks::control_center_network_state();
  const auto bs = eh::shell::dock_slot_hooks::control_center_bluetooth_state();

  // ── Section A: Network + Bluetooth pills, then Settings + DND pills.
  auto long_button = [&](double x, double y, double w, double h, const char* glyph,
                          const char* title, const char* subtitle, bool active,
                          bool hovered) {
    card(x, y, w, h);
    if (hovered) hover_hl(x, y, w, h, 20.0);
    const double icx = x + 8.0 + 20.0 * us;
    const double icy = y + h * 0.5;
    cairo_save(cr);
    rrect(cr, icx - 14.0 * us, icy - 14.0 * us, 28.0 * us, 28.0 * us, 10.0 * us);
    cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, active ? 0.20 : 0.09);
    cairo_fill(cr);
    cairo_restore(cr);
    eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0 * us, glyph,
                                   active ? mcCc.accentR : 0.65,
                                   active ? mcCc.accentG : 0.68,
                                   active ? mcCc.accentB : 0.72, 0.96);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_move_to(cr, x + (8.0 + 44.0) * us, y + h * 0.5 - 2.0 * us);
    cairo_show_text(cr, title);
    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.90);
    cairo_set_font_size(cr, 11.0 * us);
    cairo_move_to(cr, x + (8.0 + 44.0) * us, y + h * 0.5 + 13.0 * us);
    cairo_show_text(cr, subtitle);
  };

  // CardButton: icon + optional title (quick toggles). An empty title
  // renders an icon-only pill (used for the theme switcher).
  auto card_button = [&](double x, double y, double w, double h, const char* glyph,
                          const char* title, bool active, bool hovered) {
    card(x, y, w, h);
    if (hovered) hover_hl(x, y, w, h, 20.0);
    const bool hasTitle = title && title[0] != '\0';
    const double glyphCy = hasTitle ? (y + h * 0.5 - 8.0 * us) : (y + h * 0.5);
    eh::shell::draw_material_glyph(cr, x + w * 0.5, glyphCy, 22.0 * us, glyph,
                                   active ? mcCc.accentR : 0.80,
                                   active ? mcCc.accentG : 0.84,
                                   active ? mcCc.accentB : 0.86, 1.0);
    if (!hasTitle) return;
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, std::max(10.0, 12.0 * us));
    cairo_text_extents_t te;
    cairo_text_extents(cr, title, &te);
    cairo_move_to(cr, x + (w - te.x_advance) * 0.5, y + h - 8.0 * us);
    cairo_show_text(cr, title);
  };

  if (state.networksOverlay) {
    // ── SectionNetworks overlay (z=999): back + title + wifi toggle + AP list.
    const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
    const bool wifiOn = eh::net::NetworkManagerService::instance().snapshot().wireless_enabled;
    const double hx = L.pad, hy = L.pad;
    eh::shell::draw_material_glyph(cr, hx + 16.0 * us, hy + 24.0 * us, 20.0 * us, "arrow_back",
                                   0.88, 0.93, 0.96, 1.0);
    if (state.hoverTarget == CcHT::NetworksBack) hover_hl(hx, hy, 32.0 * us, 32.0 * us, 10.0 * us);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18.0 * us);
    cairo_move_to(cr, hx + 44.0 * us, hy + 30.0 * us);
    cairo_show_text(cr, "Network Connections");
    const double tglX = W - L.pad - 30.0 * us;
    eh::shell::draw_material_glyph(cr, tglX + 11.0 * us, hy + 24.0 * us, 22.0 * us,
                                   wifiOn ? "check_box" : "check_box_outline_blank",
                                   mcCc.accentR, mcCc.accentG, mcCc.accentB, 1.0);
    if (state.hoverTarget == CcHT::WifiToggle) hover_hl(tglX, hy + 6.0 * us, 30.0 * us, 30.0 * us, 8.0);
    if (!state.wifiLastError.empty()) {
      cairo_set_source_rgba(cr, 0.86, 0.42, 0.42, 0.92);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_move_to(cr, hx + 12.0 * us, hy + 52.0 * us);
      cairo_show_text(cr, state.wifiLastError.c_str());
    }
    for (int i = 0; i < L.wifiRows; ++i) {
      const auto& ap = aps[static_cast<size_t>(i)];
      const double ry = hy + ccl::kPearNetHeaderH * us + static_cast<double>(i) * ccl::kPearButtonH * us;
      const bool rowHov = (state.hoverTarget == CcHT::NetworkRow && state.hoverRowIdx == i);
      if (rowHov) hover_hl(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 36.0 * us);
      else if (ap.active) selected_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 36.0 * us);
      else row_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 36.0 * us);
      eh::shell::draw_material_glyph(cr, L.pad + 20.0 * us, ry + 24.0 * us, 16.0 * us, "wifi",
                                     ap.active ? 0.94 : 0.80, ap.active ? 0.97 : 0.84,
                                     ap.active ? 0.98 : 0.86, 1.0);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_move_to(cr, L.pad + 42.0 * us, ry + 29.0 * us);
      cairo_show_text(cr, ap.ssid.empty() ? "(hidden)" : ap.ssid.c_str());
      char pct[16];
      std::snprintf(pct, sizeof(pct), "%d%%", ap.signal_pct);
      cairo_move_to(cr, W - L.pad - 58.0 * us, ry + 29.0 * us);
      cairo_show_text(cr, pct);
    }
    if (state.wifiPasswordPrompt) {
      const double py = hy + ccl::kPearNetHeaderH * us + static_cast<double>(L.wifiRows) * ccl::kPearButtonH * us + 8.0 * us;
      row_bg(L.pad + 10.0 * us, py, W - L.pad * 2.0 - 20.0 * us, 44.0 * us);
      cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 1.0);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_move_to(cr, L.pad + 18.0 * us, py + 18.0 * us);
      const std::string prompt =
          "Password for " + (state.wifiPendingSsid.empty() ? std::string("Wi-Fi") : state.wifiPendingSsid);
      cairo_show_text(cr, prompt.c_str());
      cairo_move_to(cr, L.pad + 18.0 * us, py + 36.0 * us);
      const std::string masked = state.wifiPassword.empty() ? "..." : std::string(state.wifiPassword.size(), '*');
      cairo_show_text(cr, masked.c_str());
    }
    cairo_restore(cr);
    return;
  }

  // ── Section A: Network + Bluetooth pills, then Settings + DND pills.
  const std::string netSub = ns.connected ? (ns.ssid.empty() ? "Connected" : ns.ssid) : "Disconnected";
  const char* netGlyph = ns.connected ? (ns.wifi ? "wifi" : "lan") : "signal_wifi_off";
  long_button(L.netX, L.row1Y, L.netW, L.row1H, netGlyph,
              "Network", netSub.c_str(), ns.connected,
              state.hoverTarget == CcHT::NetworkCard);
  std::string btSub = "Off";
  if (bs.powered) {
    btSub = "On";
    const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
    for (const auto& d : devs) {
      if (d.connected) {
        btSub = d.alias.empty() ? d.address : d.alias;
        break;
      }
    }
  }
  long_button(L.btX, L.row1Y, L.btW, L.row1H,
              bs.powered ? "bluetooth" : "bluetooth_disabled", "Bluetooth", btSub.c_str(),
              bs.powered, state.hoverTarget == CcHT::BluetoothCard);
  long_button(L.setX, L.row2Y, L.setW, L.row2H,
              "settings", "Settings", "System Settings", false,
              state.hoverTarget == CcHT::SettingsRow);
  if (L.showDnd) {
    const bool dnd = scPopupOv.notifications.doNotDisturb;
    long_button(L.dndX, L.row2Y, L.dndW, L.row2H,
                dnd ? "notifications_off" : "notifications", "Do Not Disturb",
                dnd ? "On" : "Off", dnd, state.hoverTarget == CcHT::DndCard);
  }
  const bool nightOn = ccl::pear_nightlight_active(scPopupOv);
  bool isDark = cfg.isDarkTheme != -1 ? (cfg.isDarkTheme == 1)
                                      : ((0.2126 * mcCc.dockFillR + 0.7152 * mcCc.dockFillG +
                                          0.0722 * mcCc.dockFillB) <= 0.4);
  for (size_t i = 0; i < L.toggles.size(); ++i) {
    const double tx = ccl::pear_toggle_x(L, i);
    const bool hov = (state.hoverTarget == CcHT::DeviceLinkCard ||
                      state.hoverTarget == CcHT::NightColorCard ||
                      state.hoverTarget == CcHT::ColorSchemeCard ||
                      state.hoverTarget == CcHT::CameraCard ||
                      state.hoverTarget == CcHT::CmdCard1 ||
                      state.hoverTarget == CcHT::CmdCard2) &&
                     state.hoverRowIdx == static_cast<int>(i);
    switch (L.toggles[i]) {
      case ccl::PearToggle::DeviceLink:
        card_button(tx, L.togY, L.togW, L.togH,
                    cfg.deviceLinkIcon.empty() ? "smartphone" : cfg.deviceLinkIcon.c_str(),
                    cfg.deviceLinkTitle.c_str(), false, hov);
        break;
      case ccl::PearToggle::NightColor:
        card_button(tx, L.togY, L.togW, L.togH, nightOn ? "nightlight" : "nightlight_off",
                    "Night Color", nightOn, hov);
        break;
      case ccl::PearToggle::ColorScheme:
        // Icon-only pill: no Light/Dark text, the glyph shows the target.
        card_button(tx, L.togY, L.togW, L.togH, isDark ? "light_mode" : "dark_mode",
                    "", false, hov);
        break;
      case ccl::PearToggle::Camera:
        card_button(tx, L.togY, L.togW, L.togH, "photo_camera", "Camera", false, hov);
        break;
      case ccl::PearToggle::Cmd1:
        card_button(tx, L.togY, L.togW, L.togH,
                    cfg.cmdIcon1.empty() ? "terminal" : cfg.cmdIcon1.c_str(),
                    cfg.cmdTitle1.c_str(), false, hov);
        break;
      case ccl::PearToggle::Cmd2:
        card_button(tx, L.togY, L.togW, L.togH,
                    cfg.cmdIcon2.empty() ? "terminal" : cfg.cmdIcon2.c_str(),
                    cfg.cmdTitle2.c_str(), false, hov);
        break;
    }
  }

  // ── Section B: Volume / Input / Brightness slider rows. `subtitle` names
  // the active device; `chevron` (-1 none, 0 collapsed, 1 expanded) draws the
  // device-switcher affordance.
  auto slider_row = [&](double rowY, const char* glyph, const char* title, const std::string& subtitle,
                         int chevron, int pct, bool muted, bool showPct, CcHT sliderHt,
                         CcHT muteHt) {
    card(L.pad, rowY, W - L.pad * 2.0, ccl::kPearSliderRowH * us);
    if (state.hoverTarget == sliderHt) hover_hl(L.pad, rowY, W - L.pad * 2.0, ccl::kPearSliderRowH * us);
    const double icx = L.pad + ccl::kPearRowIconCx * us;
    const double icy = rowY + ccl::kPearRowIconCy * us;
    if (state.hoverTarget == muteHt) {
      cairo_new_path(cr);
      cairo_arc(cr, icx, icy, ccl::kPearRowIconR * us, 0, 2 * M_PI);
      cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
      cairo_fill(cr);
    }
    eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0 * us, glyph,
                                   muted ? 0.65 : mcCc.accentR, muted ? 0.68 : mcCc.accentG,
                                   muted ? 0.72 : mcCc.accentB, 0.96);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.95);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_move_to(cr, L.pad + 48.0 * us, rowY + 30.0 * us);
    cairo_show_text(cr, title);
    if (!subtitle.empty()) {
      cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92);
      cairo_set_font_size(cr, 11.0 * us);
      cairo_move_to(cr, L.pad + 48.0 * us, rowY + 46.0 * us);
      cairo_show_text(cr, subtitle.substr(0, 40).c_str());
    }
    if (chevron >= 0) {
      eh::shell::draw_material_glyph(cr, W - L.pad - 20.0 * us, rowY + 30.0 * us, 18.0 * us,
                                     chevron ? "expand_less" : "expand_more", 0.80, 0.84, 0.86,
                                     1.0);
    }
    if (showPct) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d%%", pct);
      cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_text_extents_t te;
      cairo_text_extents(cr, buf, &te);
      cairo_move_to(cr, W - L.pad - (chevron >= 0 ? 40.0 : 12.0) * us - te.x_advance,
                    rowY + (subtitle.empty() ? 32.0 : 30.0) * us);
      cairo_show_text(cr, buf);
    }
    eh::ui::Slider sl;
    sl.setRange(0.0f, 1.0f);
    sl.setStep(0.01f);
    sl.setValue(std::clamp(static_cast<float>(pct) / 100.0f, 0.0f, 1.0f));
    sl.setGeometry(static_cast<float>(L.pad + ccl::kPearRowTrackPadX * us),
                   static_cast<float>(rowY + ccl::kPearRowTrackY * us),
                   static_cast<float>(W - L.pad * 2.0 - ccl::kPearRowTrackPadX * 2.0 * us),
                   static_cast<float>(ccl::kPearRowTrackH * us));
    sl.setAccentColor(static_cast<float>(mcCc.accentR), static_cast<float>(mcCc.accentG),
                      static_cast<float>(mcCc.accentB));
    sl.setTrackColor(static_cast<float>(mcCc.drawerDimR), static_cast<float>(mcCc.drawerDimG),
                     static_cast<float>(mcCc.drawerDimB));
    sl.paint(cr);
  };

  if (L.showVolume) {
    const auto ao = eh::shell::dock_slot_hooks::control_center_audio_output_state();
    const char* g = (ao.muted || ao.volume_pct <= 0) ? "volume_off" : (ao.volume_pct < 34 ? "volume_down" : "volume_up");
    slider_row(L.volY, g, "Volume", ao.device_name, state.outputDevicesExpanded ? 1 : 0,
               ao.volume_pct, ao.muted, cfg.showPercentage,
               CcHT::VolumeCard, CcHT::OutputAudioMute);
  }
  // Output device switcher.
  if (L.outDevH > 2.0) {
    card(L.pad, L.outDevY, W - L.pad * 2.0, L.outDevH);
    const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_move_to(cr, L.pad + 12.0 * us, L.outDevY + 18.0 * us);
    cairo_show_text(cr, "Output device");
    for (int i = 0; i < L.outDevRows; ++i) {
      const auto& d = devs[static_cast<size_t>(i)];
      const double ry = ccl::pear_dev_row_y(L.outDevY, i);
      const bool rowHov = (state.hoverTarget == CcHT::OutputDeviceRow && state.hoverRowIdx == i);
      const bool isDefault =
          (nowMs < state.outputDevicesIgnoreUntilMs && !state.outputDevicesPendingSink.empty())
              ? (d.sink_name == state.outputDevicesPendingSink)
              : d.is_default;
      if (rowHov) hover_hl(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      else if (isDefault) selected_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      else row_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      eh::shell::draw_material_glyph(cr, L.pad + 20.0 * us, ry + 18.0 * us, 15.0 * us,
                                     isDefault ? "radio_button_checked" : "radio_button_unchecked",
                                     mcCc.accentR, mcCc.accentG, mcCc.accentB, 1.0);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_move_to(cr, L.pad + 40.0 * us, ry + 22.0 * us);
      cairo_show_text(cr, d.display_name.substr(0, 36).c_str());
    }
  }
  if (L.showInput) {
    const auto ai = eh::shell::dock_slot_hooks::control_center_audio_input_state();
    const char* g = (ai.muted || ai.volume_pct <= 0) ? "mic_off" : "mic";
    slider_row(L.inY, g, "Input", ai.device_name, state.inputDevicesExpanded ? 1 : 0,
               ai.volume_pct, ai.muted, cfg.showPercentage,
               CcHT::InputAudioSlider, CcHT::InputAudioMute);
  }
  // Input device switcher.
  if (L.inDevH > 2.0) {
    card(L.pad, L.inDevY, W - L.pad * 2.0, L.inDevH);
    const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_move_to(cr, L.pad + 12.0 * us, L.inDevY + 18.0 * us);
    cairo_show_text(cr, "Input device");
    for (int i = 0; i < L.inDevRows; ++i) {
      const auto& d = devs[static_cast<size_t>(i)];
      const double ry = ccl::pear_dev_row_y(L.inDevY, i);
      const bool rowHov = (state.hoverTarget == CcHT::InputDeviceRow && state.hoverRowIdx == i);
      const bool isDefault =
          (nowMs < state.inputDevicesIgnoreUntilMs && !state.inputDevicesPendingSource.empty())
              ? (d.source_name == state.inputDevicesPendingSource)
              : d.is_default;
      if (rowHov) hover_hl(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      else if (isDefault) selected_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      else row_bg(L.pad + 10.0 * us, ry + 6.0 * us, W - L.pad * 2.0 - 20.0 * us, 24.0 * us, 12.0);
      eh::shell::draw_material_glyph(cr, L.pad + 20.0 * us, ry + 18.0 * us, 15.0 * us,
                                     isDefault ? "radio_button_checked" : "radio_button_unchecked",
                                     mcCc.accentR, mcCc.accentG, mcCc.accentB, 1.0);
      cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
      cairo_set_font_size(cr, 12.0 * us);
      cairo_move_to(cr, L.pad + 40.0 * us, ry + 22.0 * us);
      cairo_show_text(cr, d.display_name.substr(0, 36).c_str());
    }
  }
  if (L.showBrightness) {
    const int bp = ccl::pear_brightness_pct();
    slider_row(L.briY, "brightness_high", "Display Brightness", "", -1, std::max(0, bp), false,
               cfg.showPercentage, CcHT::BrightnessCard, CcHT::BrightnessCard);
  }

  // ── Media row (Lib.Card + art + title/artist + prev/play/next).
  if (L.showMedia) {
    card(L.pad, L.mediaY, W - L.pad * 2.0, L.mediaH);
    if (state.hoverTarget == CcHT::MediaCard) hover_hl(L.pad, L.mediaY, W - L.pad * 2.0, L.mediaH);
    eh::mpris::PlayerSnapshot ms{};
    if (mpris) ms = mpris->snapshot();
    const bool active = ms.active && (!ms.title.empty() || !ms.artist.empty());
    const double artS = 56.0 * us;
    const double artX = L.pad + 12.0 * us;
    const double artY = L.mediaY + (L.mediaH - artS) * 0.5;
    rrect(cr, artX, artY, artS, artS, 12.0 * us);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    cairo_fill(cr);
    if (active && ms.art) {
      cairo_save(cr);
      rrect(cr, artX, artY, artS, artS, 12.0 * us);
      cairo_clip(cr);
      const int iw = cairo_image_surface_get_width(ms.art.get());
      const int ih = cairo_image_surface_get_height(ms.art.get());
      const double s = artS / std::max(1, std::max(iw, ih));
      cairo_translate(cr, artX + (artS - iw * s) * 0.5, artY + (artS - ih * s) * 0.5);
      cairo_scale(cr, s, s);
      cairo_set_source_surface(cr, ms.art.get(), 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      eh::shell::draw_material_glyph(cr, artX + artS * 0.5, artY + artS * 0.55, 22.0 * us,
                                     "music_note", 0.88, 0.93, 0.96, 1.0);
    }
    const double textX = artX + artS + 12.0 * us;
    const std::string title = active ? (ms.title.empty() ? std::string("Unknown Media") : ms.title) : std::string("No Media");
    const std::string artist = active ? (ms.artist.empty() ? std::string("Unknown Artist") : ms.artist) : std::string("No Media");
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 15.0 * us);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
    cairo_move_to(cr, textX, L.mediaY + 34.0 * us);
    cairo_show_text(cr, title.substr(0, 32).c_str());
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_set_source_rgba(cr, 0.72, 0.78, 0.82, 0.92);
    cairo_move_to(cr, textX, L.mediaY + 54.0 * us);
    cairo_show_text(cr, artist.substr(0, 36).c_str());
    const double cy = ccl::pear_media_btn_cy(L);
    auto mbtn = [&](int idx, const char* glyph, bool enabled, CcHT hov, bool primary) {
      const double cx = ccl::pear_media_btn_cx(L, idx);
      const bool hovered = (state.hoverTarget == hov);
      cairo_new_path(cr);
      cairo_arc(cr, cx, cy, 14.0 * us, 0, 2 * M_PI);
      if (primary) {
        cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB,
                              hovered ? 0.95 : (enabled ? 0.85 : 0.30));
        cairo_fill(cr);
        eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0 * us, glyph, 0.05, 0.05, 0.07, 1.0);
        return;
      }
      if (hovered) {
        cairo_set_source_rgba(cr, mcCc.accentR, mcCc.accentG, mcCc.accentB, 0.18);
        cairo_fill(cr);
      } else {
        cairo_set_source_rgba(cr, 1, 1, 1, enabled ? 0.10 : 0.04);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, mcCc.outlineR, mcCc.outlineG, mcCc.outlineB, 0.13);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      }
      eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0 * us, glyph, 0.88, 0.93, 0.96, 1.0);
    };
    if (active) {
      mbtn(0, "skip_previous", ms.can_go_previous, CcHT::MediaPrev, false);
      mbtn(1, (ms.playback_status == "Playing") ? "pause" : "play_arrow",
           (ms.can_play || ms.can_pause), CcHT::MediaPlayPause, true);
      mbtn(2, "skip_next", ms.can_go_next, CcHT::MediaNext, false);
    }
  }

  cairo_restore(cr);
}
