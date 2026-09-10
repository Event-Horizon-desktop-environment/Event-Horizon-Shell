#include "ux/settings/settings_tab_desktop/settings_tab_desktop.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_serialize.hpp"

#include <cstring>

extern void draw(App& app);
extern void save_settings(const struct Settings& s);

static constexpr int kDesktopToggleCardH = 120;
static constexpr int kDesktopToggleBandH = 52;

void paint_desktop_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  cairo_save(cr);

  int cy = kContentTop + 12;

  {
    settings_card(app, cr, cardX, static_cast<double>(cy), cardW, static_cast<double>(kDesktopToggleCardH), glassOv);
    settings_cat_label(cr, cardX + kCardPad, cy + 22, "Desktop");

    const int bandTop = cy + 52;
    settings_label(cr, cardX + kCardPad, bandTop + 14, "Show desktop",
                   "Desktop icons, right-click menu, and widgets on the desktop background.");
    settings_toggle(app, cr, cardX, cy, cardW,
                    static_cast<double>(cy), static_cast<double>(kDesktopToggleBandH), app.settings.desktopEnabled, glassOv);
    cy += kDesktopToggleCardH + kCardGap;
  }

  cairo_restore(cr);
}

bool settings_desktop_consume_pointer_down(App& app, int contentX, int contentW) {
   
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  int cy = kContentTop + 12;

  {
    const int swX = cardX + cardW - 52 - kSpacingXL;
    const int swY = cy + (kDesktopToggleBandH - 26) / 2;
    if (app.pointerX >= swX && app.pointerX < swX + 52 &&
        app.pointerY >= swY && app.pointerY < swY + 26) {
      app.settings.desktopEnabled = !app.settings.desktopEnabled;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  return false;
}
