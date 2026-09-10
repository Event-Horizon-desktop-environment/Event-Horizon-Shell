#pragma once

#include <cairo/cairo.h>
#include <cstdio>
#include <string>

#include "m3/controls/containers/button.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "services/network/core/network_manager_service.hpp"

namespace m3::detail {

struct WiredStatusM3State {
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  m3::Button disconnectBtn;

  void syncColours(const App& app) {
    float a_r = accentR_, a_g = accentG_, a_b = accentB_;
    float t_r = textR_, t_g = textG_, t_b = textB_;
    float s_r = surfaceR_, s_g = surfaceG_, s_b = surfaceB_;
    float o_r = outlineR_, o_g = outlineG_, o_b = outlineB_;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    accentR_ = a_r; accentG_ = a_g; accentB_ = a_b;
    textR_ = t_r; textG_ = t_g; textB_ = t_b;
    surfaceR_ = s_r; surfaceG_ = s_g; surfaceB_ = s_b;
    outlineR_ = o_r; outlineG_ = o_g; outlineB_ = o_b;
  }

  void applyColours() {
    disconnectBtn.setAccentColor(accentR_, accentG_, accentB_);
    disconnectBtn.setOutlineColor(outlineR_, outlineG_, outlineB_);
    disconnectBtn.setTextColor(textR_, textG_, textB_);
  }

  void updateHover(const App& app) {
    disconnectBtn.setHovered(disconnectBtn.containsPoint(
      static_cast<float>(app.pointerX),
      static_cast<float>(app.pointerY)));
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();

    auto& nm = eh::net::NetworkManagerService::instance();
    nm.start();
    const auto& st = nm.state();

    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);

    auto addRow = [&](int& rowY, const char* label, const std::string& value) {
      if (value.empty()) return;
      std::string text = std::string(label) + ": " + value;
      settings_show_text(cr, cardX + kCardPad, rowY + 16, text.c_str(), 13, 400,
                         textR_, textG_, textB_, 1.0);
      rowY += 26;
    };

    int rowY = contentTop + 52;

    if (!st.hasWiredDevice && st.kind != NetworkConnectivity::Wired) {
      const int cardH = 120;
      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(contentTop),
                    static_cast<double>(cardW), static_cast<double>(cardH), glassOv);
      settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(contentTop + 22), "Wired Connection");
      settings_show_text(cr, cardX + kCardPad, rowY + 16, "No wired device found", 13, 400,
                         textR_, textG_, textB_, 0.46f);
      return;
    }

    const bool wiredConnected = st.carrier;

    constexpr int kStatusCardH = 800;

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(contentTop),
                  static_cast<double>(cardW), static_cast<double>(kStatusCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(contentTop + 22), "Wired Connection");

    addRow(rowY, "Interface", st.interfaceName);
    addRow(rowY, "Status", wiredConnected ? "Connected" : "Disconnected");
    if (st.speed > 0) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%u Mbps", st.speed);
      addRow(rowY, "Speed", buf);
    }
    addRow(rowY, "Duplex", st.duplex);
    addRow(rowY, "Auto-negotiate", st.autoNegotiate ? "Yes" : "No");
    addRow(rowY, "MAC", st.macAddress);
    addRow(rowY, "Permanent MAC", st.permHwAddress);
    addRow(rowY, "IPv4", st.ipv4);
    addRow(rowY, "Gateway", st.ipv4Gateway);
    addRow(rowY, "IPv6", st.ipv6);
    addRow(rowY, "IPv6 Gateway", st.ipv6Gateway);

    for (size_t i = 0; i < st.dnsServers.size(); ++i) {
      std::string lbl = (i == 0) ? "DNS" : "";
      addRow(rowY, lbl.c_str(), st.dnsServers[i]);
    }
    for (size_t i = 0; i < st.dnsDomains.size(); ++i) {
      std::string lbl = (i == 0) ? "Search domain" : "";
      addRow(rowY, lbl.c_str(), st.dnsDomains[i]);
    }

    addRow(rowY, "Driver", st.driver);
    addRow(rowY, "Driver version", st.driverVersion);
    addRow(rowY, "Firmware", st.firmwareVersion);
    addRow(rowY, "Vendor", st.vendor);
    addRow(rowY, "Product", st.product);
    if (st.mtu > 0) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%u", st.mtu);
      addRow(rowY, "MTU", buf);
    }
    if (st.deviceState > 0) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%u", st.deviceState);
      addRow(rowY, "Device state", buf);
    }

    // Disconnect button
    if (wiredConnected && st.kind == NetworkConnectivity::Wired) {
      const int btnW = 100;
      const int btnH = 30;
      const int btnX = static_cast<int>(cardX + cardW - kCardPad - btnW);
      const int btnY = rowY + 8;

      disconnectBtn.setMinSize(0, 0);
      disconnectBtn.setLabel("Disconnect");
      disconnectBtn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                                 static_cast<float>(btnW), static_cast<float>(btnH));
      disconnectBtn.setStyle(m3::Button::Style::Outlined);
      disconnectBtn.setSize(m3::Button::Size::XS);
      disconnectBtn.paint(cr);

      rowY += btnH + 16;
    }

  }

  bool handlePointerDown(App&, float px, float py) {
    (void)px; (void)py;
    return false;
  }

  bool handlePointerUp(App&, float px, float py) {
    if (disconnectBtn.containsPoint(px, py)) {
      auto& nm = eh::net::NetworkManagerService::instance();
      nm.disconnect();
      return true;
    }
    return false;
  }

  bool handlePointerMove(float px, float py) {
    (void)px; (void)py;
    return false;
  }

  void handlePointerLeave() {}
};

inline WiredStatusM3State& wiredStatusM3() {
  static WiredStatusM3State s;
  return s;
}

} // namespace m3::detail
