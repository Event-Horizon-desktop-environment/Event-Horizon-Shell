#pragma once

#include <cairo/cairo.h>
#include <cstdio>
#include <string>

#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"

namespace m3::detail {

struct WiredIpv6M3State {
  Slider dnsPrioritySlider;
  Slider dhcpTimeoutSlider;
  Slider routeMetricSlider;
  Slider raTimeoutSlider;
  Toggle sendHostname;
  Toggle ignoreAutoDns;
  Toggle neverDefault;
  Toggle mayFail;

  int activeSlider_ = -1;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

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
    auto apply = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
    };
    apply(dnsPrioritySlider); apply(dhcpTimeoutSlider);
    apply(routeMetricSlider);
    apply(raTimeoutSlider);
    apply(sendHostname); apply(ignoreAutoDns);
    apply(neverDefault); apply(mayFail);
    sendHostname.setOutlineColor(outlineR_, outlineG_, outlineB_);
    ignoreAutoDns.setOutlineColor(outlineR_, outlineG_, outlineB_);
    neverDefault.setOutlineColor(outlineR_, outlineG_, outlineB_);
    mayFail.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App&) {}

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();

    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);
    const float trX = static_cast<float>(contentX + kCardPad);
    const float trW = cardW - static_cast<float>(kCardPad) - static_cast<float>(kCardPad) - 52.0f;
    auto paintSlider = [&](int bandTop, Slider& sl, int val, int lo, int hi, const char* label) {
      const int titleY = bandTop + 24;
      settings_show_text(cr, cardX + kCardPad, titleY, label, 14.f, 500, textR_, textG_, textB_, 0.93f);
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%d", val);
      draw_value_pill(cr, static_cast<int>(cardX), bandTop, static_cast<int>(cardW), buf);
      const float slY = static_cast<float>(bandTop) + 40.0f;
      sl.setRange(static_cast<float>(lo), static_cast<float>(hi));
      sl.setStep(1.0f);
      sl.setValue(static_cast<float>(val));
      sl.setGeometry(trX, slY, trW, 28.0f);
      sl.setEnabled(true);
      sl.setShowValueLabel(true);
      sl.setValueLabel(buf);
      sl.paint(cr);
    };

    auto paintToggle = [&](int bandTop, int local_row, Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      const int tBand = bandTop + local_row * kSliderRowH;
      if (local_row > 0) {
        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX) + kCardPad + 8.0, tBand);
        cairo_line_to(cr, static_cast<double>(cardX + cardW) - kCardPad - 8.0, tBand);
        cairo_stroke(cr);
      }
      const int titleY = tBand + 36;
      constexpr float tgH = 24.0f;
      constexpr float tgW = 40.0f;
      const float tgX = cardX + cardW - kCardPad - tgW;
      const float tgY = static_cast<float>(tBand) + (kSliderRowH - tgH) * 0.5f;
      tg.setSize(Toggle::Size::L);
      tg.setGeometry(tgX, tgY, tgW, tgH);
      tg.setOn(val);
      tg.setEnabled(true);
      tg.setAccentColor(accentR_, accentG_, accentB_);
      settings_show_text(cr, cardX + kCardPad, titleY, label, 14.f, 500, textR_, textG_, textB_, 0.93f);
      if (desc) settings_show_text(cr, cardX + kCardPad, titleY + 17, desc, 11.f, 400, textR_, textG_, textB_, 0.46f);
      tg.paint(cr, 0);
    };

    // Card 1: IPv6 Settings
    constexpr int h0 = 52 + 3 * kSliderRowH + kSpacingXL;
    const int cardTop = contentTop;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardTop),
                  static_cast<double>(cardW), static_cast<double>(h0), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(cardTop + 15), "IPv6 Settings");

    const int ipv6Band = cardTop + 52;
    paintSlider(ipv6Band + 0 * kSliderRowH, dnsPrioritySlider, 0, -100, 100, "DNS priority");
    paintSlider(ipv6Band + 1 * kSliderRowH, dhcpTimeoutSlider, 30, 0, 120, "DHCP timeout (s)");
    paintSlider(ipv6Band + 2 * kSliderRowH, raTimeoutSlider, 0, 0, 60, "RA timeout (s)");

    // Card 2: Advanced
    const int cy1 = cardTop + h0 + kCardGap;
    constexpr int h1 = 52 + 5 * kSliderRowH + kSpacingXL;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cy1),
                  static_cast<double>(cardW), static_cast<double>(h1), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(cy1 + 15), "Advanced");

    const int advBand = cy1 + 52;
    paintToggle(advBand, 0, sendHostname, true, "Send hostname", "Send hostname to DHCP server");
    paintToggle(advBand, 1, ignoreAutoDns, false, "Ignore auto DNS", "Use manual DNS only");
    paintToggle(advBand, 2, neverDefault, false, "Never use as default route", "Do not set as default gateway");
    paintToggle(advBand, 3, mayFail, true, "May fail", "Connection succeeds even if IPv6 fails");
    paintSlider(advBand + 4 * kSliderRowH, routeMetricSlider, 100, 0, 10000, "Route metric");
  }

  bool handlePointerDown(App&, float, float) { return false; }
  bool handlePointerUp(App&, float, float) { return false; }
  bool handlePointerMove(float, float) { return false; }
  void handlePointerLeave() {}
};

inline WiredIpv6M3State& wiredIpv6M3() {
  static WiredIpv6M3State s;
  return s;
}

} // namespace m3::detail
