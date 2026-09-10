#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "m3/controls/containers/button.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_bluetooth/settings_tab_bluetooth.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kDevRowH = 56;
static constexpr int kPowerCardH = 52 + kSliderRowH + kSpacingXL;
static constexpr int kBtnRowH = 36;
static constexpr int kBtnH = 28;

static constexpr int kPowerCardTop = kCardTop;
static constexpr int kScanCardTop = kPowerCardTop + kPowerCardH + kCardGap;
static constexpr int kScanBodyTop = kScanCardTop + 52;
static constexpr int kScanCardH = 52 + kBtnRowH + kSpacingXL;

static int devices_content_height(const std::vector<eh::bt::DeviceInfo>& devices) {
   
  if (devices.empty()) return 60;
  return static_cast<int>(devices.size()) * kDevRowH + kSpacingXL;
}

void paint_bluetooth_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
    
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  const auto state = bt.full_state();
  const auto& snap = state.snap;
  const auto& devices = state.devices;

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  const double pyC = app.pointerY + settings_scroll_px(app);
  int yPos = kBodyTop;

  // Power card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kPowerCardTop),
                static_cast<double>(cardW), static_cast<double>(kPowerCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kPowerCardTop + 22, "ADAPTER");
  settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(yPos),
                 "Bluetooth", snap.available ? (snap.powered ? "Powered on" : "Powered off") : "No adapter found");
  settings_toggle(app, cr, cardX, yPos, cardW,
                   static_cast<double>(yPos - 20), 26.0,
                   snap.available && snap.powered, 0.0);
  yPos += kSliderRowH;

  if (snap.available) {
    // Scan card.
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kScanCardTop),
                  static_cast<double>(cardW), static_cast<double>(kScanCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kScanCardTop + 22, "DISCOVERY");

    const double btnW = 120.0;
    const double btnH = kBtnH;
    const double btnX = cardX + cardW - btnW - kCardPad;
    const double btnY = kScanBodyTop;

    const char* scanLbl = snap.scanning ? "Stop scan" : "Scan";
    const bool scan_hov = point_in_rect(app.pointerX, pyC,
                                         static_cast<int>(btnX), static_cast<int>(btnY),
                                         static_cast<int>(btnW), static_cast<int>(btnH));
    {
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel(scanLbl);
      btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(scan_hov);
      btn.setEnabled(!snap.scanning);
      btn.paint(cr);
    }

    // Devices card.
    const int devCardTop = kScanCardTop + kScanCardH + kCardGap;
    const int devContentH = devices_content_height(devices);
    const int devCardH = 52 + devContentH;

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(devCardTop),
                  static_cast<double>(cardW), static_cast<double>(devCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), devCardTop + 22, "DEVICES");

    const int listTop = devCardTop + 52;
    const int baseY = listTop;

    if (devices.empty()) {
      settings_show_text(cr, cardX + kCardPad, listTop + 24, snap.scanning ? "Scanning for devices..." : "No devices found", 13, 400, t_r, t_g, t_b, 0.42);
    } else {
      cairo_save(cr);
      cairo_rectangle(cr, static_cast<double>(cardX), static_cast<double>(listTop),
                      static_cast<double>(cardW), static_cast<double>(devCardH - 52));
      cairo_clip(cr);

      for (size_t i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];
        const int rowY = baseY + static_cast<int>(i) * kDevRowH;

        if (rowY + kDevRowH < listTop || rowY > listTop + devCardH - 52) continue;

        const bool hovered = (static_cast<int>(i) == app.settingsBluetoothHoverRow);

        if (hovered) {
          cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
          cairo_rectangle(cr, static_cast<double>(cardX), static_cast<double>(rowY),
                          static_cast<double>(cardW), static_cast<double>(kDevRowH));
          cairo_fill(cr);
        }

        // Device type glyph
        const char* glyph = eh::bt::bluetooth_device_kind_glyph(dev.kind);
        eh::shell::draw_material_glyph(cr,
                                       cardX + kCardPad + 14.0,
                                       rowY + kDevRowH * 0.5,
                                       18.0, glyph, t_r, t_g, t_b, 0.80);

        // Device alias
        settings_show_text(cr, cardX + kCardPad + 40.0, rowY + 22, dev.alias.c_str(), 14, 400, t_r, t_g, t_b, 0.93);

        // Kind + status text
        char statusBuf[128];
        if (dev.connected) {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s \u00b7 Connected",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        } else if (dev.paired) {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s \u00b7 Paired",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        } else {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        }
        settings_show_text(cr, cardX + kCardPad + 40.0, rowY + 40, statusBuf, 11, 400, t_r, t_g, t_b, 0.46);

        // Action buttons
        const double actBtnH = 24.0;
        const double actBtnY = static_cast<double>(rowY) + 3.0;
        double actBtnX = cardX + cardW - kCardPad - kSpacingS;

        if (dev.paired) {
          const double forgetBtnW = 50.0;
          actBtnX -= forgetBtnW;
          const bool fov = point_in_rect(app.pointerX, pyC,
                                          static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                                          static_cast<int>(forgetBtnW), static_cast<int>(actBtnH));
          {
            m3::Button btn;
            btn.setMinSize(0, 0);
            btn.setLabel("Forget");
            btn.setGeometry(static_cast<float>(actBtnX), static_cast<float>(actBtnY),
                            static_cast<float>(forgetBtnW), static_cast<float>(actBtnH));
            btn.setStyle(m3::Button::Style::Outlined);
            btn.setSize(m3::Button::Size::XS);
            btn.setAccentColor(a_r, a_g, a_b);
            btn.setOutlineColor(o_r, o_g, o_b);
            btn.setHovered(fov);
            btn.paint(cr);
          }
          actBtnX -= 6;
        }

        {
          const double connBtnW = dev.connected ? 70.0 : 60.0;
          actBtnX -= connBtnW;
          const bool cov = point_in_rect(app.pointerX, pyC,
                                          static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                                          static_cast<int>(connBtnW), static_cast<int>(actBtnH));
          const char* lbl = dev.connected ? "Disconnect" : "Connect";
          {
            m3::Button btn;
            btn.setMinSize(0, 0);
            btn.setLabel(lbl);
            btn.setGeometry(static_cast<float>(actBtnX), static_cast<float>(actBtnY),
                            static_cast<float>(connBtnW), static_cast<float>(actBtnH));
            btn.setStyle(m3::Button::Style::Outlined);
            btn.setSize(m3::Button::Size::XS);
            btn.setAccentColor(a_r, a_g, a_b);
            btn.setOutlineColor(o_r, o_g, o_b);
            btn.setHovered(cov);
            btn.paint(cr);
          }
        }

        // Battery indicator
        if (dev.has_battery) {
          char batBuf[8];
          std::snprintf(batBuf, sizeof(batBuf), "%d%%", dev.battery_percent);
          cairo_set_font_size(cr, 10);
          cairo_text_extents_t bte;
          cairo_text_extents(cr, batBuf, &bte);
          const double btx = actBtnX - bte.x_advance - 8.0;
          settings_show_text(cr, btx, rowY + kDevRowH * 0.5 + bte.height * 0.35, batBuf, 10, 400, t_r, t_g, t_b, 0.50);
        }
      }
      cairo_restore(cr);
    }
  }
}

bool settings_bluetooth_consume_pointer_down(App& app, int contentX, int contentW) {
    
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  const auto state = bt.full_state();
  const auto& snap = state.snap;
  const auto& devices = state.devices;

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const double ly = app.pointerY + settings_scroll_px(app);

  // Power toggle
  {
    constexpr int swW = 52, swH = 26;
    const int swX = cardX + cardW - swW - kSpacingXL;
    const int swY = kBodyTop - 20;
    if (point_in_rect(app.pointerX, ly, swX, swY, swW, swH)) {
      bt.set_powered(!snap.powered);
      draw(app);
      return true;
    }
  }

  if (!snap.available) return false;

  // Scan button
  {
    const double btnW = 120.0;
    const double btnH = kBtnH;
    const double btnX = cardX + cardW - btnW - kCardPad;
    const double btnY = kScanBodyTop;
    if (point_in_rect(app.pointerX, ly,
                       static_cast<int>(btnX), static_cast<int>(btnY),
                       static_cast<int>(btnW), static_cast<int>(btnH))) {
      if (snap.scanning)
        bt.stop_discovery();
      else
        bt.start_discovery();
      draw(app);
      return true;
    }
  }

  // Device list buttons
  const int devCardTop = kScanCardTop + kScanCardH + kCardGap;
  const int listTop = devCardTop + 52;
  const int baseY = listTop;

  for (size_t i = 0; i < devices.size(); ++i) {
    const auto& dev = devices[i];
    const int rowY = baseY + static_cast<int>(i) * kDevRowH;
    const double actBtnH = 24.0;
    const double actBtnY = static_cast<double>(rowY) + 3.0;
    double actBtnX = cardX + cardW - kCardPad - kSpacingS;

    // Forget button
    if (dev.paired) {
      const double forgetBtnW = 50.0;
      actBtnX -= forgetBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(forgetBtnW), static_cast<int>(actBtnH))) {
        bt.forget_device(dev.path);
        draw(app);
        return true;
      }
      actBtnX -= 6;
    }

    // Connect/Disconnect button
    {
      const double connBtnW = dev.connected ? 70.0 : 60.0;
      actBtnX -= connBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(connBtnW), static_cast<int>(actBtnH))) {
        if (dev.connected)
          bt.disconnect_device(dev.path);
        else
          bt.connect_device(dev.path);
        draw(app);
        return true;
      }
    }
  }

  return false;
}
