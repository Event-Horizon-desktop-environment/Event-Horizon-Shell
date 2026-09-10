#pragma once

#include <cmath>
#include <cstdio>

#include <cairo/cairo.h>

#include "m3/controls/input/slider.hpp"
#include "m3/controls/input/toggle.hpp"
#include "ux/settings/common/settings_common.hpp"

namespace m3::detail {

// Section-card metrics (match the dock's "Visibility & behavior" card).
static constexpr int kHyprSectionHeaderH = 52;  // title + subtitle zone at card top
static constexpr int kHyprToggleRowH     = 60;  // kDockVisRowPitch
static constexpr int kHyprSliderRowH     = 68;  // kSliderRowH
static constexpr int kHyprSectionPadB    = 24;  // bottom padding inside the card
static constexpr int kHyprSectionGap     = 12;  // gap between section cards

// Animation card 3-dot menu trigger (paint + hit test must agree).
static constexpr int kHyprAnimDotRight = kCardPad + 12;  // inset from card right edge
static constexpr int kHyprAnimDotHitW  = 12;             // hit-test half width
static constexpr int kHyprAnimDotHitH  = 12;             // hit-test half height around center

// Shared dock-style section layout for the workspace sub-tabs.
// Each section is one glassy card spanning the column, with a header
// (uppercase label + subtitle) and rows of toggles / sliders inside, exactly
// like the dock settings sub-tab's "Visibility & behavior" card.
struct HyprlandCardLayout {
  int colX = 0;
  int colW = 0;
  int cardW = 0;
  int groupY = 0;      // top of the next section card
  int cardX = 0;       // current section card bounds
  int cardY = 0;
  int cardH = 0;
  int row = 0;         // next row index inside the current card
  int nToggles = 0;
  int nSliders = 0;

  void init(int contentX, int contentW, int contentTop) {
    colW = (contentW * 68) / 100;
    colX = contentX + (contentW - colW) / 2;
    cardW = colW;
    groupY = contentTop;
  }

  static int sectionCardH(int nTg, int nSl) {
    return kHyprSectionHeaderH + nTg * kHyprToggleRowH + nSl * kHyprSliderRowH + kHyprSectionPadB;
  }

  // Top of row r inside the current card (toggles are placed before sliders).
  int rowTop(int r) const {
    if (r < nToggles) return cardY + kHyprSectionHeaderH + r * kHyprToggleRowH;
    return cardY + kHyprSectionHeaderH + nToggles * kHyprToggleRowH + (r - nToggles) * kHyprSliderRowH;
  }

  // Draw the section background card + header. Row counts are passed so the
  // card height is known before the rows paint on top.
  void beginGroup(App& app, cairo_t* cr, double glassOv, const char* title,
                  const char* subtitle, int nTg, int nSl,
                  float tr, float tg, float tb) {
    nToggles = nTg;
    nSliders = nSl;
    row = 0;
    cardX = colX;
    cardY = groupY;
    cardH = sectionCardH(nTg, nSl);
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), static_cast<double>(cardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(cardY + 21), title);
    if (subtitle && subtitle[0])
      settings_show_text(cr, cardX + kCardPad, cardY + 38, subtitle, 11.f, 400, tr, tg, tb, 0.46f);
  }

  void endGroup() {
    groupY = cardY + cardH + kHyprSectionGap;
  }

  // Advance past a section without drawing (mirrors beginGroup/endGroup).
  void skipSection(int nTg, int nSl) {
    cardX = colX;
    cardY = groupY;
    cardH = sectionCardH(nTg, nSl);
    nToggles = nTg;
    nSliders = nSl;
    row = 0;
    groupY = cardY + cardH + kHyprSectionGap;
  }
};

// Divider line between rows inside a section card.
inline void hyprland_paint_row_divider(cairo_t* cr, const HyprlandCardLayout& lay,
                                       int rowY, float r, float g, float b) {
  cairo_set_source_rgba(cr, r, g, b, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, static_cast<double>(lay.cardX + kCardPad + 8), static_cast<double>(rowY));
  cairo_line_to(cr, static_cast<double>(lay.cardX + lay.cardW - kCardPad - 8), static_cast<double>(rowY));
  cairo_stroke(cr);
}

// Slider row (title + value pill + track).
inline void hyprland_paint_slider_card(App& app, cairo_t* cr, HyprlandCardLayout& lay,
                                       double glassOv, Slider& sl, int val, int lo, int hi,
                                       const char* label, float r, float g, float b,
                                       const char* fmt = nullptr,
                                       const char* const* value_labels = nullptr) {
  (void)app;
  (void)glassOv;
  const int rowY = lay.rowTop(lay.row);
  ++lay.row;
  if (lay.row > 1)
    hyprland_paint_row_divider(cr, lay, rowY, r, g, b);

  settings_show_text(cr, lay.cardX + kCardPad, rowY + 24, label, 13.f, 500, r, g, b, 0.93f);
  char buf[32];
  if (value_labels && val >= lo && val <= hi && value_labels[val - lo])
    std::snprintf(buf, sizeof(buf), "%s", value_labels[val - lo]);
  else if (fmt) std::snprintf(buf, sizeof(buf), fmt, val);
  else std::snprintf(buf, sizeof(buf), "%d", val);
  draw_value_pill(cr, lay.cardX, rowY + 4, lay.cardW, buf);

  sl.setRange(static_cast<float>(lo), static_cast<float>(hi));
  sl.setStep(1.0f);
  sl.setValue(static_cast<float>(val));
  sl.setGeometry(static_cast<float>(lay.cardX + kCardPad),
                 static_cast<float>(rowY + 40) - 10.0f,
                 static_cast<float>(lay.cardW - 2 * kCardPad), 36.0f);
  sl.setEnabled(true);
  sl.setShowValueLabel(true);
  sl.setValueLabel(buf);
  sl.paint(cr);
}

// Toggle row (title + description + right-aligned switch, dock style).
inline void hyprland_paint_toggle_card(App& app, cairo_t* cr, HyprlandCardLayout& lay,
                                       double glassOv, Toggle& tg, bool val, const char* label,
                                       float ar, float ag, float ab, float r, float g, float b,
                                       const char* desc = nullptr) {
  (void)app;
  (void)glassOv;
  const int rowY = lay.rowTop(lay.row);
  ++lay.row;
  if (lay.row > 1)
    hyprland_paint_row_divider(cr, lay, rowY, r, g, b);

  settings_show_text(cr, lay.cardX + kCardPad, rowY + 22, label, 14.f, 500, r, g, b, 0.93f);
  if (desc && desc[0])
    settings_show_text(cr, lay.cardX + kCardPad, rowY + 42, desc, 11.f, 400, r, g, b, 0.46f);

  constexpr float tgW = 40.0f;
  constexpr float tgH = 24.0f;
  tg.setSize(Toggle::Size::L);
  tg.setGeometry(static_cast<float>(lay.cardX + lay.cardW - kCardPad) - tgW,
                 static_cast<float>(rowY) + static_cast<float>(kHyprToggleRowH - 24) * 0.5f, tgW, tgH);
  tg.setOn(val);
  tg.setEnabled(true);
  tg.setAccentColor(ar, ag, ab);
  tg.paint(cr, 0);
}

// Animation entry row (title + info + switch + 3-dot menu).
inline void hyprland_paint_anim_card(App& app, cairo_t* cr, HyprlandCardLayout& lay,
                                     double glassOv, Toggle& tg, bool val, const char* label,
                                     const char* info, float infoAlpha,
                                     float ar, float ag, float ab, float r, float g, float b) {
  (void)app;
  (void)glassOv;
  const int rowY = lay.rowTop(lay.row);
  ++lay.row;
  if (lay.row > 1)
    hyprland_paint_row_divider(cr, lay, rowY, r, g, b);

  settings_show_text(cr, lay.cardX + kCardPad, rowY + 22, label, 14.f, 500, r, g, b, val ? 0.93f : 0.46f);
  if (info && info[0])
    settings_show_text(cr, lay.cardX + kCardPad, rowY + 42, info, 11.f, 400, r, g, b, infoAlpha);

  constexpr float tgW = 40.0f;
  constexpr float tgH = 24.0f;
  const float tgX = static_cast<float>(lay.cardX + lay.cardW - kCardPad) - tgW - 34.0f;
  const float tgY = static_cast<float>(rowY) + static_cast<float>(kHyprToggleRowH - 24) * 0.5f;
  tg.setSize(Toggle::Size::L);
  tg.setGeometry(tgX, tgY, tgW, tgH);
  tg.setOn(val);
  tg.setEnabled(true);
  tg.setAccentColor(ar, ag, ab);
  tg.paint(cr, 0);

  const int dotX = lay.cardX + lay.cardW - kHyprAnimDotRight;
  const int dotCY = rowY + kHyprToggleRowH / 2;
  cairo_set_source_rgba(cr, r, g, b, 0.40f);
  for (int d = -1; d <= 1; ++d) {
    cairo_arc(cr, static_cast<double>(dotX), static_cast<double>(dotCY + d * 7), 1.8, 0, 2 * M_PI);
    cairo_fill(cr);
  }
}

} // namespace m3::detail
