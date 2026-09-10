// Input sub-tab (index 5).

static void paint_mango_input(App& app, cairo_t* cr, int contentX, int contentW,
                              double cardX, double cardW, double glassOv,
                              double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  constexpr int S = 5 * 100;
  int cy = kContentTop - yOff;

  // Card 0: Keyboard
  int h0 = 52 + 4 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy, cardW, h0, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Keyboard");
  int ry = cy + 52;

  char buf[16];
  settings_label(cr, contentX + kCardPad, ry, "Repeat rate", nullptr);
  std::snprintf(buf, sizeof(buf), "%d Hz", cfg.repeat_rate);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.repeat_rate, 10, 100, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 0) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Repeat delay", nullptr);
  std::snprintf(buf, sizeof(buf), "%dms", cfg.repeat_delay);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.repeat_delay, 100, 2000, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 1) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "NumLock on startup", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.numlockon, glassOv);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Layout:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  settings_show_text(cr, contentX + kCardPad + 80, ry + 14, cfg.xkb_rules_layout.empty() ? "(default)" : cfg.xkb_rules_layout.c_str(), 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);

  // Card 1: Mouse
  int cy1 = cy + h0 + kCardGap;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy1, cardW, h1, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy1 + 15, "Mouse");
  ry = cy1 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Natural scrolling", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.mouse_natural_scrolling, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Left handed", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.left_handed, glassOv);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Accel profile:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  static const char* accel_labels[] = {"None", "Flat", "Adaptive"};
  int ap = std::clamp(cfg.mouse_accel_profile, 0, 2);
  settings_show_text(cr, contentX + kCardPad + 120, ry + 14, accel_labels[ap], 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Accel speed:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.mouse_accel_speed);
  settings_show_text(cr, contentX + kCardPad + 120, ry + 14, buf, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Scroll factor:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.axis_scroll_factor);
  settings_show_text(cr, contentX + kCardPad + 120, ry + 14, buf, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);

  // Card 2: Trackpad
  int cy2 = cy1 + h1 + kCardGap;
  int h2 = 52 + 8 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy2, cardW, h2, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy2 + 15, "Trackpad");
  ry = cy2 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Disable trackpad", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.disable_trackpad, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Tap to click", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.tap_to_click, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Natural scrolling", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.trackpad_natural_scrolling, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Disable while typing", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.disable_while_typing, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Middle click emulation", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.middle_button_emulation, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Drag lock", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.drag_lock, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Swipe threshold", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.swipe_min_threshold, 1, 100, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 2) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Scroll method:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  static const char* scroll_labels[] = {"None", "Two-finger", "Edge", "", "Button"};
  int sm = cfg.scroll_method;
  settings_show_text(cr, contentX + kCardPad + 140, ry + 14, (sm >= 0 && sm <= 4) ? scroll_labels[sm] : "?", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);

  app.mangoContentBottom = cy2 + h2 + kSpacingL;
}

static bool mango_input_pointer(App& app, int contentX, int contentW) {
  auto& cfg = app.mangoConfig;
  const double lyA = app.pointerY + settings_scroll_px(app);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  int cardXi = contentX + 8;
  int cardWi = contentW - 16;
  int swX = cardXi + cardWi - 52 - 24;
  constexpr int S = 5 * 100;

  auto is_toggle = [&](int ry) { return point_in_rect(app.pointerX, lyA, swX, ry + 21, 52, 26); };
  auto is_slider = [&](int ry) { return point_in_rect(app.pointerX, lyA, trX - 6, ry + 20, trW + 12, 28); };

  int h0 = 52 + 4 * kSliderRowH + kSpacingXL;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;

  // Card 0
  int ry = kContentTop + 52;
  if (is_slider(ry + 0 * kSliderRowH)) { app.mangoSliderDrag = S + 0; mango_slider_apply(app, S + 0, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { app.mangoSliderDrag = S + 1; mango_slider_apply(app, S + 1, app.pointerX); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.numlockon = !cfg.numlockon; mango_commit_cfg(app); draw(app); return true; }

  // Card 1
  ry = kContentTop + h0 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.mouse_natural_scrolling = !cfg.mouse_natural_scrolling; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.left_handed = !cfg.left_handed; mango_commit_cfg(app); draw(app); return true; }

  // Card 2
  ry = kContentTop + h0 + kCardGap + h1 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.disable_trackpad = !cfg.disable_trackpad; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.tap_to_click = !cfg.tap_to_click; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.trackpad_natural_scrolling = !cfg.trackpad_natural_scrolling; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 3 * kSliderRowH)) { cfg.disable_while_typing = !cfg.disable_while_typing; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 4 * kSliderRowH)) { cfg.middle_button_emulation = !cfg.middle_button_emulation; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 5 * kSliderRowH)) { cfg.drag_lock = !cfg.drag_lock; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 6 * kSliderRowH)) { app.mangoSliderDrag = S + 2; mango_slider_apply(app, S + 2, app.pointerX); draw(app); return true; }

  return false;
}
