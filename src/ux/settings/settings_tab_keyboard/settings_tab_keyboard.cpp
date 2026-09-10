#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_keyboard/settings_tab_keyboard.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

using namespace keyboard_tab;

static const std::vector<std::pair<const char*, const char*>> kKnownLayouts = {
  {"us", "US (QWERTY)"},
  {"us(dvorak)", "US (Dvorak)"},
  {"us(colemak)", "US (Colemak)"},
  {"gb", "UK (QWERTY)"},
  {"de", "German (QWERTZ)"},
  {"fr", "French (AZERTY)"},
  {"it", "Italian"},
  {"es", "Spanish"},
  {"pt", "Portuguese"},
  {"br", "Portuguese (Brazil)"},
  {"ru", "Russian"},
  {"jp", "Japanese"},
  {"kr", "Korean"},
  {"se", "Swedish"},
  {"no", "Norwegian"},
  {"dk", "Danish"},
  {"fi", "Finnish"},
  {"pl", "Polish"},
  {"cz", "Czech"},
  {"sk", "Slovak"},
  {"hu", "Hungarian"},
  {"ro", "Romanian"},
  {"bg", "Bulgarian"},
  {"gr", "Greek"},
  {"il", "Hebrew"},
  {"tr", "Turkish"},
  {"ara", "Arabic"},
  {"ca", "Canadian"},
};

static void ensure_layouts_populated(App& app) {
   
  if (app.settings.keyboardLayouts.size() > 1) return;
  app.settings.keyboardLayouts.clear();
  for (const auto& [code, name] : kKnownLayouts) {
    app.settings.keyboardLayouts.push_back(code);
  }
}
static constexpr int kLayoutBodyTop = kBodyTop;

extern const char* kSwitchShortcutLabels[];
extern const int kSwitchShortcutCount;
extern const char* kCapsLockLabels[];
extern const int kCapsLockCount;
extern const char* kComposeKeyLabels[];
extern const int kComposeKeyCount;

const char* kSwitchShortcutLabels[] = {
  "Alt+Shift",
  "Ctrl+Shift",
  "Super+Space",
  "Caps Lock",
  "Alt+Shift (toggle)",
  "Ctrl+Shift (toggle)",
  "None (disabled)"
};
const int kSwitchShortcutCount = 7;

const char* kCapsLockLabels[] = {
  "Caps Lock (default)",
  "Ctrl (both)",
  "Swap Escape/Caps Lock",
  "Caps Lock disabled"
};
const int kCapsLockCount = 4;

const char* kComposeKeyLabels[] = {
  "None (disabled)",
  "Right Alt (AltGr)",
  "Right Ctrl",
  "Menu key",
  "Right Win/Super"
};
const int kComposeKeyCount = 5;

static const char* switch_shortcut_label(int mode) {
  if (mode >= 0 && mode < kSwitchShortcutCount) return kSwitchShortcutLabels[mode];
  return "Alt+Shift";
}

static const char* caps_lock_label(int mode) {
  if (mode >= 0 && mode < kCapsLockCount) return kCapsLockLabels[mode];
  return "Caps Lock (default)";
}

static const char* compose_key_label(int mode) {
  if (mode >= 0 && mode < kComposeKeyCount) return kComposeKeyLabels[mode];
  return "None (disabled)";
}

static const char* layout_display_name(const std::string& layout) {
  for (const auto& [code, name] : kKnownLayouts) {
    if (layout == code) return name;
  }
  return layout.c_str();
}

static void paint_slider_pill(App& app, cairo_t* cr, int titleY, int trX, int trY, int trW) {
   
  const double textCap = static_cast<double>(titleY) - 11.0;
  const double slTop = static_cast<double>(trY) - 10.0;
  const double slBot = static_cast<double>(trY) + 26.0;
  constexpr double kPad = 2.0;
  const double pX = static_cast<double>(trX) - 4.0;
  const double pY = std::min(textCap, slTop) - kPad;
  const double pW = static_cast<double>(trW) + 56.0;
  const double pH = std::max(static_cast<double>(titleY), slBot) + kPad - pY;
  const double pR = std::max(4.0, std::min(pH, pW) * 0.18);
  cairo_save(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
  {
    m3::Box box;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.panelFillR;
      g = app.drawChrome.panelFillG;
      b = app.drawChrome.panelFillB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    box.setColor(r, g, b, 0.22f);
    box.setRadius(static_cast<float>(pR));
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY),
                    static_cast<float>(pW), static_cast<float>(pH));
    box.paint(cr);
  }
  cairo_round_rect(cr, pX, pY, pW, pH, pR);
  paint_src_glass_hi(app, cr, 0.06);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void paint_keyboard_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  ensure_layouts_populated(app);
  const double cardX = static_cast<double>(contentX + 8);
  const double cardW = static_cast<double>(contentW - 16);
  const int rightRail = static_cast<int>(cardX + cardW - kSpacingXL);

  // Layout card.
  settings_card(app, cr, cardX, static_cast<double>(kCardTop),
                cardW, static_cast<double>(kLayoutCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, kCardTop + 22, "KEYBOARD LAYOUTS");

  int rowY = kLayoutBodyTop;

  auto paint_combo = [&](int ry, const char* val) {
    int cbx = rightRail - kComboW;
    settings_paint_combo_closed(app, cr, cbx, ry + 40,
                                kComboW, kComboH, glassOv, val, false, settings_scroll_px_int(app));
  };

  // Row 0: Keyboard layout
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Keyboard layout", nullptr);
  paint_combo(rowY, layout_display_name(app.settings.keyboardLayout));

  rowY += kSliderRowH;

  // Row 1: Switch layout shortcut
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Switch layout shortcut", nullptr);
  paint_combo(rowY, switch_shortcut_label(app.settings.keyboardSwitchShortcut));

  rowY += kSliderRowH + 12;

  // Row 2: Show layout indicator
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Show layout indicator", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardShowLayout, 0.0);

  rowY += kSliderRowH;

  // Row 3: NumLock on startup
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "NumLock on startup", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardNumlock, 0.0);

  rowY += kSliderRowH;

  // Row 4: Input method
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Input method", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardInputMethodEnabled, 0.0);

  // Behavior card.
  settings_card(app, cr, cardX, static_cast<double>(kBehaviorCardTop),
                cardW, static_cast<double>(kBehaviorCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, kBehaviorCardTop + 22, "KEY BEHAVIOR");

  rowY = kBehaviorCardTop + 52;

  // Row 0: Caps Lock behavior
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Caps Lock behavior", nullptr);
  paint_combo(rowY, caps_lock_label(app.settings.keyboardCapsLockBehavior));

  rowY += kSliderRowH;

  // Row 1: Compose key
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Compose key", nullptr);
  paint_combo(rowY, compose_key_label(app.settings.keyboardComposeKey));

  rowY += kSliderRowH + 12;

  // Row 2: Middle-click paste
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Middle-click paste", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardMiddleClickPaste, 0.0);

  // Repeat rate card.
  settings_card(app, cr, cardX, static_cast<double>(kRepeatCardTop),
                cardW, static_cast<double>(kRepeatCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, kRepeatCardTop + 22, "KEY REPEAT");

  rowY = kRepeatCardTop + 52;

  auto paint_slider_row = [&](const char* title, const char* valFmt, int value,
                              int vmin, int vmax, int which) {
    const int titleY = rowY + 14;
    settings_label(cr, cardX + kCardPad, static_cast<double>(titleY), title, nullptr);
    int trX = static_cast<int>(cardX + kCardPad);
    int trY = titleY + 26;
    int trW = rightRail - trX;
    if (trW < 40) trW = 40;
    paint_slider_pill(app, cr, titleY, trX, trY, trW);
    char buf[32];
    std::snprintf(buf, sizeof(buf), valFmt, value);
    settings_slider(app, cr, trX, trY, trW, value, vmin, vmax, 0.0, buf, false, (app.keyboardSliderDrag == which) ? app.settingsSliderDragNormT : -1.0);
  };

  // Row 0: Repeat rate
  paint_slider_row("Repeat rate", "%d cps", app.settings.keyboardRepeatRate, 15, 50, 0);

  rowY += kSliderRowH + 8;

  // Row 1: Repeat delay
  paint_slider_row("Repeat delay", "%d ms", app.settings.keyboardRepeatDelay, 150, 1000, 1);
}

bool settings_keyboard_consume_pointer_down(App& app, int contentX, int contentW) {
   
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  const int rightRail = cardXi + cardWi - kSpacingXL;
  const double py = app.pointerY + settings_scroll_px(app);

  if (app.settingsKeyboardDdKind >= 0) {
    int cbx = rightRail - kComboW;
    int cbw = kComboW;
    int cbh = kComboH;
    const int rowH = kSettingsDdRowH;
    const int nItems = [&]() -> int {
      switch (app.settingsKeyboardDdKind) {
        case 0: return static_cast<int>(app.settings.keyboardLayouts.size());
        case 1: return kSwitchShortcutCount;
        case 2: return kCapsLockCount;
        case 3: return kComposeKeyCount;
        default: return 0;
      }
    }();
    if (nItems > 0) {
      int ddY = 0;
      switch (app.settingsKeyboardDdKind) {
        case 0: ddY = kLayoutBodyTop + 0 * kSliderRowH + 40 + cbh + 2; break;
        case 1: ddY = kLayoutBodyTop + 1 * kSliderRowH + 40 + cbh + 2; break;
        case 2: ddY = kBehaviorCardTop + 52 + 0 * kSliderRowH + 40 + cbh + 2; break;
        case 3: ddY = kBehaviorCardTop + 52 + 1 * kSliderRowH + 40 + cbh + 2; break;
      }
      const int ddH = nItems * rowH;
      if (point_in_rect(app.pointerX, py, cbx, ddY, cbw, ddH)) {
        const int relY = static_cast<int>(py) - ddY;
        const int idx = relY / rowH;
        if (idx >= 0 && idx < nItems) {
          switch (app.settingsKeyboardDdKind) {
            case 0:
              if (static_cast<size_t>(idx) < app.settings.keyboardLayouts.size())
                app.settings.keyboardLayout = app.settings.keyboardLayouts[idx];
              break;
            case 1: app.settings.keyboardSwitchShortcut = idx; break;
            case 2: app.settings.keyboardCapsLockBehavior = idx; break;
            case 3: app.settings.keyboardComposeKey = idx; break;
          }
          app.settingsKeyboardDdKind = -1;
          app.settingsKeyboardDdHoverRow = -1;
          save_settings(app.settings);
          draw(app);
          return true;
        }
      }
    }
    // Click outside closes
    if (nItems > 0) {
      int ddY = 0;
      switch (app.settingsKeyboardDdKind) {
        case 0: ddY = kLayoutBodyTop + 0 * kSliderRowH + 40; break;
        case 1: ddY = kLayoutBodyTop + 1 * kSliderRowH + 40; break;
        case 2: ddY = kBehaviorCardTop + 52 + 0 * kSliderRowH + 40; break;
        case 3: ddY = kBehaviorCardTop + 52 + 1 * kSliderRowH + 40; break;
      }
      if (point_in_rect(app.pointerX, py, cbx, ddY - 4, cbw, nItems * rowH + cbh + 8)) {
        app.settingsKeyboardDdKind = -1;
        app.settingsKeyboardDdHoverRow = -1;
        draw(app);
        return true;
      }
    }
    app.settingsKeyboardDdKind = -1;
    app.settingsKeyboardDdHoverRow = -1;
    draw(app);
    return true;
  }

  // Toggle hits.
  {
    constexpr int swW = 52;
    constexpr int swH = 26;
    const int swX = rightRail - swW;
    int swY = 0;

    swY = (kLayoutBodyTop + 2 * kSliderRowH) + 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardShowLayout = !app.settings.keyboardShowLayout;
      save_settings(app.settings);
      draw(app);
      return true;
    }

    swY = (kLayoutBodyTop + 3 * kSliderRowH) + 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardNumlock = !app.settings.keyboardNumlock;
      save_settings(app.settings);
      draw(app);
      return true;
    }

    swY = (kLayoutBodyTop + 4 * kSliderRowH) + 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardInputMethodEnabled = !app.settings.keyboardInputMethodEnabled;
      save_settings(app.settings);
      draw(app);
      return true;
    }

    swY = (kBehaviorCardTop + 52 + 2 * kSliderRowH) + 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardMiddleClickPaste = !app.settings.keyboardMiddleClickPaste;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Combo box hits.
  {
    const int cbx = rightRail - kComboW;
    const int cbw = kComboW;
    const int cbh = kComboH;

    auto combo_hit = [&](int cby, int ddKind) -> bool {
      if (point_in_rect(app.pointerX, py, cbx, cby, cbw, cbh)) {
        app.settingsKeyboardDdKind = ddKind;
        app.settingsKeyboardDdHoverRow = -1;
        draw(app);
        return true;
      }
      return false;
    };

    if (combo_hit(kLayoutBodyTop + 0 * kSliderRowH + 40, 0)) return true;
    if (combo_hit(kLayoutBodyTop + 1 * kSliderRowH + 40, 1)) return true;
    if (combo_hit(kBehaviorCardTop + 52 + 0 * kSliderRowH + 40, 2)) return true;
    if (combo_hit(kBehaviorCardTop + 52 + 1 * kSliderRowH + 40, 3)) return true;
  }

  // Slider hits.
  {
    auto slider_hit = [&](int sliderRowY, int which) -> bool {
      const int titleY = sliderRowY + 14;
      int trX = static_cast<int>(cardXi + kCardPad);
      int trY = titleY + 26;
      int trW = rightRail - trX;
      if (trW < 40) trW = 40;
      if (point_in_rect(app.pointerX, py, trX - 6, trY, trW + 12, 28)) {
        app.keyboardSliderDrag = which;
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
        if (which == 0) {
          app.settings.keyboardRepeatRate = slider_value_from_x(app.pointerX, trX, trW, 15, 50);
        } else {
          app.settings.keyboardRepeatDelay = slider_value_from_x(app.pointerX, trX, trW, 150, 1000);
        }
        draw(app);
        return true;
      }
      return false;
    };

    if (slider_hit(kRepeatCardTop + 52, 0)) return true;
    if (slider_hit(kRepeatCardTop + 52 + kSliderRowH + 8, 1)) return true;
  }

  return false;
}
