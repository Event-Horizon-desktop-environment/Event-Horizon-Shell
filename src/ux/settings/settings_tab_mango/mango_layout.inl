// Layout sub-tab (index 4).

static void paint_mango_layout(App& app, cairo_t* cr, int contentX, int contentW,
                               double cardX, double cardW, double glassOv,
                               double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  constexpr int S = 4 * 100;
  int cy = kContentTop - yOff;

  // Card 0: Gaps
  int h0 = 52 + 5 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy, cardW, h0, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Gaps");
  int ry = cy + 52;

  settings_label(cr, contentX + kCardPad, ry, "Inner gap H", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.gappih, 0, 100, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 0) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Inner gap V", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.gappiv, 0, 100, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 1) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Outer gap H", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.gappoh, 0, 100, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 2) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Outer gap V", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.gappov, 0, 100, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 3) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Smart gaps", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.smartgaps, glassOv);

  // Card 1: Master-Stack
  int cy1 = cy + h0 + kCardGap;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy1, cardW, h1, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy1 + 15, "Master-Stack");
  ry = cy1 + 52;

  settings_label(cr, contentX + kCardPad, ry, "New window is master", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.new_is_master, glassOv);
  ry += kSliderRowH;

  char buf[16];
  settings_label(cr, contentX + kCardPad, ry, "Master factor", nullptr);
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.default_mfact);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.default_mfact * 100.0), 5, 95, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 4) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Master count", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.default_nmaster, 1, 9, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 5) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Center when single stack", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.center_when_single_stack, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Master overspread", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.center_master_overspread, glassOv);

  // Card 2: Scroller / Dwindle / Circle
  int cy2 = cy1 + h1 + kCardGap;
  int h2 = 52 + 2 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy2, cardW, h2, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy2 + 15, "Scroller");
  ry = cy2 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Focus center", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.scroller_focus_center, glassOv);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Proportion presets:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);
  settings_show_text(cr, contentX + kCardPad + 150, ry + 14, cfg.scroller_proportion_preset.c_str(), 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);

  app.mangoContentBottom = cy2 + h2 + kSpacingL;
}

static bool mango_layout_pointer(App& app, int contentX, int contentW) {
  auto& cfg = app.mangoConfig;
  const double lyA = app.pointerY + settings_scroll_px(app);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  int cardXi = contentX + 8;
  int cardWi = contentW - 16;
  int swX = cardXi + cardWi - 52 - 24;
  constexpr int S = 4 * 100;

  auto is_toggle = [&](int ry) { return point_in_rect(app.pointerX, lyA, swX, ry + 21, 52, 26); };
  auto is_slider = [&](int ry) { return point_in_rect(app.pointerX, lyA, trX - 6, ry + 20, trW + 12, 28); };

  int h0 = 52 + 5 * kSliderRowH + kSpacingXL;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;

  // Card 0
  int ry = kContentTop + 52;
  if (is_slider(ry + 0 * kSliderRowH)) { app.mangoSliderDrag = S + 0; mango_slider_apply(app, S + 0, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { app.mangoSliderDrag = S + 1; mango_slider_apply(app, S + 1, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 2 * kSliderRowH)) { app.mangoSliderDrag = S + 2; mango_slider_apply(app, S + 2, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 3 * kSliderRowH)) { app.mangoSliderDrag = S + 3; mango_slider_apply(app, S + 3, app.pointerX); draw(app); return true; }
  if (is_toggle(ry + 4 * kSliderRowH)) { cfg.smartgaps = !cfg.smartgaps; mango_commit_cfg(app); draw(app); return true; }

  // Card 1
  ry = kContentTop + h0 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.new_is_master = !cfg.new_is_master; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { app.mangoSliderDrag = S + 4; mango_slider_apply(app, S + 4, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 2 * kSliderRowH)) { app.mangoSliderDrag = S + 5; mango_slider_apply(app, S + 5, app.pointerX); draw(app); return true; }
  if (is_toggle(ry + 3 * kSliderRowH)) { cfg.center_when_single_stack = !cfg.center_when_single_stack; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 4 * kSliderRowH)) { cfg.center_master_overspread = !cfg.center_master_overspread; mango_commit_cfg(app); draw(app); return true; }

  // Card 2
  ry = kContentTop + h0 + kCardGap + h1 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.scroller_focus_center = !cfg.scroller_focus_center; mango_commit_cfg(app); draw(app); return true; }

  return false;
}
