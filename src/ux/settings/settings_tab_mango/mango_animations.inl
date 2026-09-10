// Animations sub-tab (index 2).

static void paint_mango_animations(App& app, cairo_t* cr, int contentX, int contentW,
                                   double cardX, double cardW, double glassOv,
                                   double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  constexpr int S = 2 * 100;
  int cy = kContentTop - yOff;

  // Card 0: Toggles
  int h0 = 52 + 4 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy, cardW, h0, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Animation Toggles");
  int ry = cy + 52;

  settings_label(cr, contentX + kCardPad, ry, "Enable animations", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.animations, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Layer animations", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.layer_animations, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Fade in", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.animation_fade_in, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Fade out", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.animation_fade_out, glassOv);

  // Card 1: Durations
  int cy1 = cy + h0 + kCardGap;
  int h1 = 52 + 4 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy1, cardW, h1, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy1 + 15, "Durations (ms)");
  ry = cy1 + 52;

  char buf[16];
  settings_label(cr, contentX + kCardPad, ry, "Move duration", nullptr);
  std::snprintf(buf, sizeof(buf), "%dms", cfg.animation_duration_move);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.animation_duration_move, 50, 2000, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 0) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Open duration", nullptr);
  std::snprintf(buf, sizeof(buf), "%dms", cfg.animation_duration_open);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.animation_duration_open, 50, 2000, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 1) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Close duration", nullptr);
  std::snprintf(buf, sizeof(buf), "%dms", cfg.animation_duration_close);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.animation_duration_close, 50, 2000, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 2) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Tag switch duration", nullptr);
  std::snprintf(buf, sizeof(buf), "%dms", cfg.animation_duration_tag);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.animation_duration_tag, 50, 2000, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 3) ? app.settingsSliderDragNormT : -1.0);

  app.mangoContentBottom = cy1 + h1 + kSpacingL;
}

static bool mango_animations_pointer(App& app, int contentX, int contentW) {
  auto& cfg = app.mangoConfig;
  const double lyA = app.pointerY + settings_scroll_px(app);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  int cardXi = contentX + 8;
  int cardWi = contentW - 16;
  int swX = cardXi + cardWi - 52 - 24;
  constexpr int S = 2 * 100;

  auto is_toggle = [&](int ry) { return point_in_rect(app.pointerX, lyA, swX, ry + 21, 52, 26); };
  auto is_slider = [&](int ry) { return point_in_rect(app.pointerX, lyA, trX - 6, ry + 20, trW + 12, 28); };

  int h0 = 52 + 4 * kSliderRowH + kSpacingXL;

  // Card 0
  int ry = kContentTop + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.animations = !cfg.animations; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.layer_animations = !cfg.layer_animations; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.animation_fade_in = !cfg.animation_fade_in; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 3 * kSliderRowH)) { cfg.animation_fade_out = !cfg.animation_fade_out; mango_commit_cfg(app); draw(app); return true; }

  // Card 1
  ry = kContentTop + h0 + kCardGap + 52;
  if (is_slider(ry + 0 * kSliderRowH)) { app.mangoSliderDrag = S + 0; mango_slider_apply(app, S + 0, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { app.mangoSliderDrag = S + 1; mango_slider_apply(app, S + 1, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 2 * kSliderRowH)) { app.mangoSliderDrag = S + 2; mango_slider_apply(app, S + 2, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 3 * kSliderRowH)) { app.mangoSliderDrag = S + 3; mango_slider_apply(app, S + 3, app.pointerX); draw(app); return true; }

  return false;
}
