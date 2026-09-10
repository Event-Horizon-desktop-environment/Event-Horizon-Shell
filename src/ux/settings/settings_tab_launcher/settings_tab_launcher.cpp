#include <cairo/cairo.h>
#include <algorithm>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_tab_launcher/launcher_tab_m3.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

extern void draw(App& app);

// 2-column card grid geometry
void launcher_app_geom(int contentX, int contentW, int idx,
                       int& trX, int& trY, int& trW,
                       int& cardX, int& cardY, int& cardW) {
  constexpr int kTopMargin = 72;
  constexpr int kCardH = 92;
  constexpr int kGap = 12;
  constexpr int kPad = 20;
  constexpr int kSliderY = 50;

  const int col = idx % 2;
  const int row = idx / 2;
  const int colW = (contentW * 68) / 100;
  const int colX = contentX + (contentW - colW) / 2;
  cardW = (colW - kGap) / 2;
  if (cardW < 160) cardW = 160;
  cardX = colX + col * (cardW + kGap);
  cardY = kContentTop + kTopMargin + row * (kCardH + kGap);
  trX = cardX + kPad;
  trW = cardW - kPad - kPad;
  if (trW < 40) trW = 40;
  trY = cardY + kSliderY;
}

// View mode combo positioned inside card at index 8
void launcher_view_mode_combo_geom(int contentX, int contentW, int* cx, int* cy, int* cw, int* ch) {
  int trX, trY, trW, cardX, cardY, cardW;
  launcher_app_geom(contentX, contentW, 9, trX, trY, trW, cardX, cardY, cardW);
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
  *cx = cardX + cardW - kCardPad - kSettingsComboW;
  *cy = cardY + (92 - kSettingsComboH) / 2;
}

const char* const kViewModeLabels[] = {"Grid", "List"};
const int kViewModeCount = 2;

static void draw_value_pill(cairo_t* cr, int cx, int cy, int cw, const char* text,
                            float textR, float textG, float textB) {
  if (!text || !text[0]) return;
  cairo_save(cr);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11.0);
  cairo_text_extents_t te;
  cairo_text_extents(cr, text, &te);
  const int padX = 8, padY = 3;
  const int pillW = static_cast<int>(te.width) + padX * 2;
  const int pillH = static_cast<int>(te.height) + padY * 2;
  const int pillX = cx + cw - 20 - pillW;
  const int pillY = cy + 28 - te.height / 2 - padY;

  {
    m3::Box bg;
    bg.setColor(textR, textG, textB, 0.08f);
    bg.setRadius(static_cast<float>(pillH / 2));
    bg.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                   static_cast<float>(pillW), static_cast<float>(pillH));
    bg.paint(cr);
  }
  cairo_set_source_rgba(cr, textR, textG, textB, 0.60f);
  cairo_move_to(cr, pillX + padX, pillY + te.height + padY - 2.0);
  cairo_show_text(cr, text);
  cairo_restore(cr);
}

void paint_launcher_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset) {
  (void)paintPointerYOffset;

  float textR, textG, textB;
  if (app.drawChromeMatugen) {
    textR = static_cast<float>(app.drawChrome.textR);
    textG = static_cast<float>(app.drawChrome.textG);
    textB = static_cast<float>(app.drawChrome.textB);
  } else {
    textR = static_cast<float>(Theme::TextR);
    textG = static_cast<float>(Theme::TextG);
    textB = static_cast<float>(Theme::TextB);
  }

  static const char* titles[] = {
    "Layout scale", "Icon size", "Icon spacing", "Folder size",
    "Folder spacing", "Columns", "Rows", "Widget opacity", "DPI scale", "View mode"
  };

  int sliderVals[10];
  sliderVals[0] = app.settings.launchpadLayoutScalePct;
  sliderVals[1] = app.settings.launchpadIconFillPct;
  sliderVals[2] = app.settings.launchpadCellGapPx;
  sliderVals[3] = app.settings.launchpadFolderSizePct;
  sliderVals[4] = app.settings.launchpadFolderGapPx;
  sliderVals[5] = app.settings.launchpadGridColumns;
  sliderVals[6] = app.settings.launchpadGridRows;
  sliderVals[7] = app.settings.slotPillOpacity;
  sliderVals[8] = app.settings.launchpadDpiScalePct;
  sliderVals[9] = 0;

  int sliderMin[10] = {70, 30, 0, 50, 4, 4, 3, 0, 50, 0};
  int sliderMax[10] = {150, 95, 24, 200, 48, 12, 10, 100, 300, 0};

  for (int i = 0; i < 9; ++i) {
    int trX, trY, trW, cardX, cardY, cardW;
    launcher_app_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), 92.0, glassOv);

    settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28),
                       titles[i], 13.f, 500, textR, textG, textB, 0.90f);

    char valStr[32];
    if (i == 0 || i == 1 || i == 3 || i == 7 || i == 8)
      std::snprintf(valStr, sizeof(valStr), "%d%%", sliderVals[i]);
    else
      std::snprintf(valStr, sizeof(valStr), "%d", sliderVals[i]);

    draw_value_pill(cr, cardX, cardY, cardW, valStr, textR, textG, textB);

    const double dn = (app.launcherSliderDrag == i) ? app.settingsSliderDragNormT : -1.0;
    settings_slider(app, cr, trX, trY, trW, sliderVals[i], sliderMin[i], sliderMax[i],
                    0.0, valStr, false, dn);
  }

  // View mode combo card (index 9)
  {
    int trX, trY, trW, cardX, cardY, cardW;
    launcher_app_geom(contentX, contentW, 9, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), 92.0, glassOv);

    settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28),
                       titles[9], 13.f, 500, textR, textG, textB, 0.90f);

    int vcx, vcy, vcw, vch;
    launcher_view_mode_combo_geom(contentX, contentW, &vcx, &vcy, &vcw, &vch);
    settings_paint_combo_closed(app, cr, vcx, vcy, vcw, vch, glassOv,
                                app.settings.launchpadViewMode == 0 ? "Grid" : "List",
                                app.launcherViewModeDropdownOpen);
  }
}

// M3 wrappers.
static constexpr int kLauncherContentTop = kContentTop + kDockChildTabH + 12;

void paint_launcher_tab_m3(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  float a_r = 0.769f, a_g = 0.659f, a_b = 0.941f;
  float t_r = 1.0f, t_g = 1.0f, t_b = 1.0f;
  float s_r = 0.102f, s_g = 0.075f, s_b = 0.188f;
  float o_r = 0.478f, o_g = 0.416f, o_b = 0.588f;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  (void)o_r; (void)o_g; (void)o_b;

  m3::detail::launcher_paint_child_tab_bar(cr, contentX, contentW, t_r, t_g, t_b, a_r, a_g, a_b,
                                           s_r, s_g, s_b, glassOv, app.launcherChildTab);
  m3::detail::launcherM3().paint(app, cr, contentX, contentW, glassOv, kLauncherContentTop);
}

bool launcher_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  const int tabHit = m3::detail::launcher_hit_child_tab(px, py, contentX, contentW);
  if (tabHit >= 0 && tabHit != app.launcherChildTab) {
    settings_close_mode_dropdowns(app);
    app.launcherChildTab = tabHit;
    app.settingsLauncherScrollPx = 0;
    draw(app);
    return true;
  }
  return m3::detail::launcherM3().handlePointerDown(app, px, py);
}

bool launcher_m3_handle_pointer_up(App& app, float px, float py) {
  return m3::detail::launcherM3().handlePointerUp(app, px, py);
}

bool launcher_m3_handle_pointer_move(App& app, float px, float py) {
  const bool handled = m3::detail::launcherM3().handlePointerMove(px, py);
  if (handled) m3::detail::launcherM3().flushSliderValues(app);
  return handled;
}

void launcher_m3_handle_pointer_leave() {
  m3::detail::launcherM3().handlePointerLeave();
}

bool launcher_m3_has_active_slider(const App& app) {
  (void)app;
  return m3::detail::launcherM3().activeSlider_ >= 0;
}

void settings_clamp_launcher_scroll_px(App& app) {
  const int mx = std::max(0, app.launcherContentBottom - kLauncherContentTop -
                             std::max(120, app.height - kLauncherContentTop - kSpacingL));
  app.settingsLauncherScrollPx = std::clamp(app.settingsLauncherScrollPx, 0, mx);
}
