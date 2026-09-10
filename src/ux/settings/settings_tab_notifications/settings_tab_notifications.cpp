#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_notifications/settings_tab_notifications.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"

// Layout constants.
static constexpr int kNotifPosCardTop = kContentTop;
static constexpr int kNotifPosCardH = 52;

static constexpr int kNotifBehavCardTop = kNotifPosCardTop + kNotifPosCardH + kCardGap;
static constexpr int kNotifBehavBodyTop = kNotifBehavCardTop + 52;
static constexpr int kNotifBehavCardH = 52 + 2 * kSliderRowH + kSpacingXL;

static constexpr int kNotifGridTop = kNotifBehavCardTop + kNotifBehavCardH + kCardGap;

static constexpr int kNotifTestCardTopGap = kCardGap;

// Grid constants (same as launcher/workspaces).
static constexpr int kCardH = 92;
static constexpr int kGap = 12;
static constexpr int kPad = 20;
static constexpr int kSliderY = 50;

// Two-column card grid geometry.
void notif_app_geom(int contentX, int contentW, int idx,
                    int& trX, int& trY, int& trW,
                    int& cardX, int& cardY, int& cardW) {
  const int col = idx % 2;
  const int row = idx / 2;
  const int colW = (contentW * 68) / 100;
  const int colX = contentX + (contentW - colW) / 2;
  cardW = (colW - kGap) / 2;
  if (cardW < 160) cardW = 160;
  cardX = colX + col * (cardW + kGap);
  cardY = kNotifGridTop + row * (kCardH + kGap);
  trX = cardX + kPad;
  trW = cardW - kPad - kPad;
  if (trW < 40) trW = 40;
  trY = cardY + kSliderY;
}

// Geometry helpers.
void notifications_primary_test_button_geom(int contentX, int contentW, int& x, int& y, int& w, int& h) {
  int trX, trY, trW, cardX, cardY, cardW;
  notif_app_geom(contentX, contentW, 0, trX, trY, trW, cardX, cardY, cardW);
  const int gridBottom = cardY + kCardH;
  const int testCardTop = gridBottom + kNotifTestCardTopGap;
  x = contentX + kCardPad;
  y = testCardTop + 52 + 38;
  const int cardWfull = contentW - 16;
  w = std::max(120, cardWfull - 2 * kCardPad);
  h = 40;
}

// Apply function.
bool apply_notifications_slider_x(App& app, int row, double px) {
  int trX, trY, trW;
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  int cardX, cardY, cardW;
  notif_app_geom(contentX, contentW, row, trX, trY, trW, cardX, cardY, cardW);
  (void)trY;
  (void)cardX;
  (void)cardY;
  (void)cardW;
  if (row == 0) {
    const int v = slider_value_from_x(px, trX, trW, 50, 200);
    if (v == app.settings.notificationsScalePct) return false;
    app.settings.notificationsScalePct = v;
    return true;
  }
  if (row == 1) {
    const int v = slider_value_from_x(px, trX, trW, 1000, 120000);
    if (v == app.settings.notificationsDefaultTimeoutMs) return false;
    app.settings.notificationsDefaultTimeoutMs = v;
    return true;
  }
  if (row == 2) {
    const int v = slider_value_from_x(px, trX, trW, 8, 64);
    if (v == app.settings.notificationsToastMarginPx) return false;
    app.settings.notificationsToastMarginPx = v;
    return true;
  }
  if (row == 3) {
    const int v = slider_value_from_x(px, trX, trW, 0, 40);
    if (v == app.settings.notificationsToastCornerRadiusPx) return false;
    app.settings.notificationsToastCornerRadiusPx = v;
    return true;
  }
  if (row == 4) {
    const int v = slider_value_from_x(px, trX, trW, 200, 900);
    if (v == app.settings.notificationsToastMaxWidthPx) return false;
    app.settings.notificationsToastMaxWidthPx = v;
    return true;
  }
  return false;
}

// Helper to draw a simple round card.
static void draw_notif_card(App& app, cairo_t* cr, double cx, double cy, double cw, double ch, double glassOv) {
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
    box.setColor(r, g, b, static_cast<float>(0.84 * glassOv));
    box.setRadius(static_cast<float>(kCardRad));
    box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                    static_cast<float>(cw), static_cast<float>(ch));
    box.setGlassy(true);
    box.paint(cr);
  }
  cairo_round_rect(cr, cx, cy, cw, ch, kCardRad);
  paint_src_glass_hi(app, cr, 0.12 * glassOv);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

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
    bg.setGlassy(true);
    bg.paint(cr);
  }
  cairo_set_source_rgba(cr, textR, textG, textB, 0.60f);
  cairo_move_to(cr, pillX + padX, pillY + te.height + padY - 2.0);
  cairo_show_text(cr, text);
  cairo_restore(cr);
}

// Painting.
void paint_notifications_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
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

  int cardX, cardW;
  {
    const int cX = contentX + 8;
    const int cW = contentW - 16;
    cardX = cX;
    cardW = cW;
  }

  // Card 0: Position
  draw_notif_card(app, cr, static_cast<double>(cardX), static_cast<double>(kNotifPosCardTop),
                  static_cast<double>(cardW), static_cast<double>(kNotifPosCardH), glassOv);
  {
    static constexpr const char* kToastPositions[] = {
      "top_right", "top_left", "bottom_left", "bottom_right", "top_center", "bottom_center"
    };
    constexpr int kNumToastPositions = 6;
    int posIdx = 0;
    for (int i = 0; i < kNumToastPositions; ++i) {
      if (app.settings.notificationsToastPosition == kToastPositions[i]) { posIdx = i; break; }
    }
    settings_label(cr, static_cast<double>(contentX + kCardPad), kNotifPosCardTop + 30, "Position", "");
    const int posBx = cardX + cardW - kCardPad - kSettingsComboW;
    const int posBy = kNotifPosCardTop + (kNotifPosCardH - kSettingsComboH) / 2;
    settings_paint_combo_closed(app, cr, posBx, posBy, kSettingsComboW, kSettingsComboH, glassOv,
                                kToastPositions[posIdx], app.notifPosDropdownOpen);
  }

  // Card 1: Server
  draw_notif_card(app, cr, static_cast<double>(cardX), static_cast<double>(kNotifBehavCardTop),
                  static_cast<double>(cardW), static_cast<double>(kNotifBehavCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kNotifBehavCardTop + 22, "Server");
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kNotifBehavBodyTop),
                "DBus notification server",
                "");
  settings_toggle(app, cr, cardX, kNotifBehavBodyTop, cardW,
                  static_cast<double>(kNotifBehavBodyTop) - 34.0, 54.0, app.settings.notificationsDbusEnabled, 0.0);

  const int ndDndRowY = kNotifBehavBodyTop + kSliderRowH;
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(ndDndRowY), "Do not disturb",
                "");
  settings_toggle(app, cr, cardX, ndDndRowY, cardW,
                  static_cast<double>(ndDndRowY) - 34.0, 54.0, app.settings.notificationsDoNotDisturb, 0.0);

  // Card 2: Layout & Toast Appearance (2-column card grid)
  static const char* titles[] = {
    "Layout scale", "Default timeout (ms)", "Edge margin (px)",
    "Corner radius (px)", "Max width (px)", "Layer-shell toasts"
  };

  int sliderVals[5];
  sliderVals[0] = app.settings.notificationsScalePct;
  sliderVals[1] = app.settings.notificationsDefaultTimeoutMs;
  sliderVals[2] = app.settings.notificationsToastMarginPx;
  sliderVals[3] = app.settings.notificationsToastCornerRadiusPx;
  sliderVals[4] = app.settings.notificationsToastMaxWidthPx;

  int sliderMin[5] = {50, 1000, 8, 0, 200};
  int sliderMax[5] = {200, 120000, 64, 40, 900};

  // Items 0–4: slider cards
  for (int i = 0; i < 5; ++i) {
    int trX, trY, trW, cX, cY, cW;
    notif_app_geom(contentX, contentW, i, trX, trY, trW, cX, cY, cW);

    settings_card(app, cr, static_cast<double>(cX), static_cast<double>(cY),
                  static_cast<double>(cW), static_cast<double>(kCardH), glassOv);

    settings_show_text(cr, static_cast<double>(cX + kPad), static_cast<double>(cY + 28),
                       titles[i], 13.f, 500, textR, textG, textB, 0.90f);

    char valStr[32];
    if (i == 0)
      std::snprintf(valStr, sizeof(valStr), "%d%%", sliderVals[i]);
    else if (i == 1)
      std::snprintf(valStr, sizeof(valStr), "%d", sliderVals[i]);
    else
      std::snprintf(valStr, sizeof(valStr), "%d", sliderVals[i]);

    draw_value_pill(cr, cX, cY, cW, valStr, textR, textG, textB);

    const double dn = (app.notifSliderDrag == i) ? app.settingsSliderDragNormT : -1.0;
    settings_slider(app, cr, trX, trY, trW, sliderVals[i], sliderMin[i], sliderMax[i],
                    0.0, valStr, false, dn);
  }

  // Item 5: Layer-shell toggles (toggle card)
  {
    int trX, trY, trW, cX, cY, cW;
    notif_app_geom(contentX, contentW, 5, trX, trY, trW, cX, cY, cW);

    settings_card(app, cr, static_cast<double>(cX), static_cast<double>(cY),
                  static_cast<double>(cW), static_cast<double>(kCardH), glassOv);

    settings_show_text(cr, static_cast<double>(cX + kPad), static_cast<double>(cY + 28),
                       titles[5], 13.f, 500, textR, textG, textB, 0.90f);

    settings_toggle(app, cr, cX, 0, cW,
                    static_cast<double>(cY) - 24.0, static_cast<double>(kCardH),
                    app.settings.notificationsToastLayerShellEnabled, 0.0);
  }

  // Card 3: Test notification
  {
    // Find actual grid bottom by computing last grid row
    int lastX, lastY, lastW, lastCX, lastCY, lastCW;
    notif_app_geom(contentX, contentW, 5, lastX, lastY, lastW, lastCX, lastCY, lastCW);
    const int actualGridBottom = lastCY + kCardH;
    const int testCardTop = actualGridBottom + kNotifTestCardTopGap;
    const int testCardH = 52 + 38 + 40 + kSpacingXL + 26;

    draw_notif_card(app, cr, static_cast<double>(cardX), static_cast<double>(testCardTop),
                    static_cast<double>(cardW), static_cast<double>(testCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), testCardTop + 22, "Test");
    settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(testCardTop + 52),
                  "Send a test notification",
                  "");
    int tbX, tbY, tbW, tbH;
    notifications_primary_test_button_geom(contentX, contentW, tbX, tbY, tbW, tbH);
    tbY = testCardTop + 52 + 38;
    {
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      app.testNotifBtn.setMinSize(0, 0);
      app.testNotifBtn.setLabel("Test notification");
      app.testNotifBtn.setGeometry(static_cast<float>(tbX), static_cast<float>(tbY),
                                    static_cast<float>(tbW), static_cast<float>(tbH));
      app.testNotifBtn.setStyle(m3::Button::Style::Outlined);
      app.testNotifBtn.setSize(m3::Button::Size::S);
      app.testNotifBtn.setAccentColor(a_r, a_g, a_b);
      app.testNotifBtn.setOutlineColor(o_r, o_g, o_b);
      app.testNotifBtn.setOnClick([]{
        eh::notify::info(std::string("Event Horizon"), std::string("Test notification"),
                         std::string("Sample from Settings \u2192 Notifications."));
      });
      app.testNotifBtn.paint(cr);
    }
    if (!eh::notify::canPush()) {
      const int hintY = tbY + tbH + 8;
      settings_show_text(cr, contentX + kCardPad, hintY, "No notification bus sender in this process \u2014 toasts only appear when the shell or a notify helper is running.", 11.f, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);
    }
  }

  // Position dropdown popup (painted last so it's on top of all cards)
  if (app.notifPosDropdownOpen) {
    static constexpr const char* kToastPositions[] = {
      "top_right", "top_left", "bottom_left", "bottom_right", "top_center", "bottom_center"
    };
    constexpr int kNumToastPositions = 6;
    int posIdx = 0;
    for (int i = 0; i < kNumToastPositions; ++i) {
      if (app.settings.notificationsToastPosition == kToastPositions[i]) { posIdx = i; break; }
    }
    const int posBx = cardX + cardW - kCardPad - kSettingsComboW;
    const int posBy = kNotifPosCardTop + (kNotifPosCardH - kSettingsComboH) / 2;
    const int listTop = posBy + kSettingsComboH + 2;
    settings_paint_combo_list_popup(app, cr, posBx, listTop, kSettingsComboW, kSettingsDdRowH,
                                    kNumToastPositions, kToastPositions, posIdx,
                                    app.notifPosDropdownHoverRow, glassOv, 0, 0, true);
  }
}

// Click handler.
bool settings_notifications_consume_pointer_down(App& app, int contentX, int contentW) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  constexpr int swWn = 52;
  constexpr int swHn = 26;
  const int swXBus = cardXi + cardWi - swWn - kSpacingXL;
  const int swYBus = kNotifBehavBodyTop - 34 + (54 - swHn) / 2;
  extern void draw(App& app);
  extern void save_settings(const struct Settings& s);

  settings_close_non_default_app_dropdowns(app);

  if (point_in_rect(app.pointerX, app.pointerY, swXBus, swYBus, swWn, swHn)) {
    app.settings.notificationsDbusEnabled = !app.settings.notificationsDbusEnabled;
    save_settings(app.settings);
    draw(app);
    return true;
  }
  const int ndDndToggleCY = kNotifBehavBodyTop + kSliderRowH;
  const int swYDnd = ndDndToggleCY - 34 + (54 - swHn) / 2;
  if (point_in_rect(app.pointerX, app.pointerY, swXBus, swYDnd, swWn, swHn)) {
    app.settings.notificationsDoNotDisturb = !app.settings.notificationsDoNotDisturb;
    save_settings(app.settings);
    draw(app);
    return true;
  }
  if (app.testNotifBtn.handlePointerDown(static_cast<float>(app.pointerX), static_cast<float>(app.pointerY))) {
    draw(app);
    return true;
  }

  // Grid slider items (0-4)
  for (int i = 0; i < 5; ++i) {
    int trX, trY, trW, cX, cY, cW;
    notif_app_geom(contentX, contentW, i, trX, trY, trW, cX, cY, cW);
    if (point_in_rect(app.pointerX, app.pointerY, trX - 6, trY, trW + 12, 28)) {
      app.notifSliderDrag = i;
      apply_notifications_slider_x(app, i, app.pointerX);
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      draw(app);
      return true;
    }
  }

  // Toggle for layer-shell toasts (item 5)
  {
    int trX, trY, trW, cX, cY, cW;
    notif_app_geom(contentX, contentW, 5, trX, trY, trW, cX, cY, cW);
    const int swY = cY - 24 + (kCardH - swHn) / 2;
    if (point_in_rect(app.pointerX, app.pointerY, cX + cW - swWn - kSpacingXL, swY, swWn, swHn)) {
      app.settings.notificationsToastLayerShellEnabled = !app.settings.notificationsToastLayerShellEnabled;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Position dropdown
  {
    static constexpr const char* kToastPositions[] = {
      "top_right", "top_left", "bottom_left", "bottom_right", "top_center", "bottom_center"
    };
    constexpr int kNumToastPositions = 6;
    const int posBx = cardXi + cardWi - kCardPad - kSettingsComboW;
    const int posBy = kNotifPosCardTop + (kNotifPosCardH - kSettingsComboH) / 2;
    if (point_in_rect(app.pointerX, app.pointerY, posBx, posBy, kSettingsComboW, kSettingsComboH)) {
      settings_close_non_default_app_dropdowns(app);
      app.notifPosDropdownOpen = !app.notifPosDropdownOpen;
      if (!app.notifPosDropdownOpen) app.notifPosDropdownHoverRow = -1;
      draw(app);
      return true;
    }
    if (app.notifPosDropdownOpen) {
      const int listTop = posBy + kSettingsComboH + 2;
      const int listH = kNumToastPositions * kSettingsDdRowH;
      if (point_in_rect(app.pointerX, app.pointerY, posBx, listTop, kSettingsComboW, listH)) {
        const int relY = static_cast<int>(app.pointerY) - listTop;
        const int idx = relY / kSettingsDdRowH;
        if (idx >= 0 && idx < kNumToastPositions) {
          app.settings.notificationsToastPosition = kToastPositions[idx];
          app.notifPosDropdownOpen = false;
          app.notifPosDropdownHoverRow = -1;
          save_settings(app.settings);
          draw(app);
          return true;
        }
      }
      if (!point_in_rect(app.pointerX, app.pointerY, posBx, posBy - 4, kSettingsComboW, listTop - posBy + listH + 8)) {
        app.notifPosDropdownOpen = false;
        app.notifPosDropdownHoverRow = -1;
        draw(app);
        return true;
      }
    }
  }

  return false;
}
