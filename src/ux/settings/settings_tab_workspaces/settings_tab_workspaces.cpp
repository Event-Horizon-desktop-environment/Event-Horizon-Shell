#include <cairo/cairo.h>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_workspaces/settings_tab_workspaces.hpp"

// 2-column card grid geometry
void workspace_app_geom(int contentX, int contentW, int idx,
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

void paint_workspaces_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                           double paintPointerYOffset, double dockMatA) {
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

  static const char* titles[] = {"Number of workspaces", "Max workspace icons", "Show app icons"};

  int sliderVals[2];
  sliderVals[0] = app.settings.workspacesMaxSlots;
  sliderVals[1] = app.settings.workspacesMaxIcons;

  // Items 0 and 1: slider cards
  for (int i = 0; i < 2; ++i) {
    int trX, trY, trW, cardX, cardY, cardW;
    workspace_app_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), 92.0, glassOv);

    settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28),
                       titles[i], 13.f, 500, textR, textG, textB, 0.90f);

    char valStr[16];
    if (i == 0 && app.settings.workspacesMaxSlots == 0)
      std::snprintf(valStr, sizeof(valStr), "Auto");
    else
      std::snprintf(valStr, sizeof(valStr), "%d", sliderVals[i]);

    // Value pill
    {
      cairo_save(cr);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 11.0);
      cairo_text_extents_t te;
      cairo_text_extents(cr, valStr, &te);
      const int padX = 8, padY = 3;
      const int pillW = static_cast<int>(te.width) + padX * 2;
      const int pillH = static_cast<int>(te.height) + padY * 2;
      const int pillX = cardX + cardW - 20 - pillW;
      const int pillY = cardY + 28 - te.height / 2 - padY;
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
      cairo_show_text(cr, valStr);
      cairo_restore(cr);
    }

    int sMin = (i == 0) ? 0 : 1;
    int sMax = (i == 0) ? 16 : 8;
    const double dn = (app.wsSliderDrag == i) ? app.settingsSliderDragNormT : -1.0;
    settings_slider(app, cr, trX, trY, trW, sliderVals[i], sMin, sMax,
                    paintPointerYOffset, valStr, false, dn);
  }

  // Item 2: toggle card
  {
    int trX, trY, trW, cardX, cardY, cardW;
    workspace_app_geom(contentX, contentW, 2, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), 92.0, glassOv);

    settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28),
                       titles[2], 13.f, 500, textR, textG, textB, 0.90f);

    settings_toggle(app, cr, cardX, 0, cardW,
                    static_cast<double>(cardY - 24), 92.0,
                    app.settings.workspacesShowApps, dockMatA);
  }
}
