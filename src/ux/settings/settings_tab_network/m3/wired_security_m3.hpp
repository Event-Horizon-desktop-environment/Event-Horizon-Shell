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

struct WiredSecurityM3State {
  Toggle enable8021x;
  Toggle systemCaCerts;

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
    enable8021x.setAccentColor(accentR_, accentG_, accentB_);
    enable8021x.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
    enable8021x.setTextColor(textR_, textG_, textB_);
    enable8021x.setOutlineColor(outlineR_, outlineG_, outlineB_);
    systemCaCerts.setAccentColor(accentR_, accentG_, accentB_);
    systemCaCerts.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
    systemCaCerts.setTextColor(textR_, textG_, textB_);
    systemCaCerts.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App&) {}

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();

    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);
    const float t_r = textR_, t_g = textG_, t_b = textB_;

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
      settings_show_text(cr, cardX + kCardPad, titleY, label, 14.f, 500, t_r, t_g, t_b, 0.93f);
      if (desc) settings_show_text(cr, cardX + kCardPad, titleY + 17, desc, 11.f, 400, t_r, t_g, t_b, 0.46f);
      tg.paint(cr, 0);
    };

    // Card 1: 802.1X Security
    constexpr int h0 = 52 + 3 * kSliderRowH + kSpacingXL;
    const int cardTop = contentTop;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardTop),
                  static_cast<double>(cardW), static_cast<double>(h0), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(cardTop + 15), "802.1X Security");

    const int secBand = cardTop + 52;
    paintToggle(secBand, 0, enable8021x, false, "Enable 802.1X", "Use IEEE 802.1X authentication");
    paintToggle(secBand, 1, systemCaCerts, true, "System CA certificates", "Use system CA certificate store");

    // Placeholder note for PPPoE and other settings
    const int noteY = cardTop + 52 + 2 * kSliderRowH + 12;
    settings_show_text(cr, cardX + kCardPad, noteY,
                       "Text entry fields (EAP method, identity, password) require keyboard input support.",
                       12, 400, t_r, t_g, t_b, 0.46f);
  }

  bool handlePointerDown(App&, float, float) { return false; }
  bool handlePointerUp(App&, float, float) { return false; }
  bool handlePointerMove(float, float) { return false; }
  void handlePointerLeave() {}
};

inline WiredSecurityM3State& wiredSecurityM3() {
  static WiredSecurityM3State s;
  return s;
}

} // namespace m3::detail
