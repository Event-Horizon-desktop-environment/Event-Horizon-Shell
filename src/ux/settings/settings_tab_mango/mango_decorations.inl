// Decorations sub-tab (index 0).

static void paint_mango_decorations(App& app, cairo_t* cr, int contentX, int contentW,
                                    double cardX, double cardW, double glassOv,
                                    double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  constexpr int S = 0 * 100;
  int cy = kContentTop - yOff;

  // Card 0: Border & Opacity
  int h0 = 52 + 6 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy, cardW, h0, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Border & Opacity");
  int ry = cy + 52;

  settings_label(cr, contentX + kCardPad, ry, "Border width", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.borderpx, 0, 20, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 0) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Corner radius", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.border_radius, 0, 50, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 1) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "No border when single", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.no_border_when_single, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "No radius when single", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.no_radius_when_single, glassOv);
  ry += kSliderRowH;

  char buf[16];
  settings_label(cr, contentX + kCardPad, ry, "Focused opacity", nullptr);
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.focused_opacity);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.focused_opacity * 100.0), 10, 100, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 2) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Unfocused opacity", nullptr);
  std::snprintf(buf, sizeof(buf), "%.2f", cfg.unfocused_opacity);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.unfocused_opacity * 100.0), 10, 100, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 3) ? app.settingsSliderDragNormT : -1.0);

  // Card 1: Shadows
  int cy1 = cy + h0 + kCardGap;
  int h1 = 52 + 7 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy1, cardW, h1, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy1 + 15, "Shadows");
  ry = cy1 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Enable shadows", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.shadows, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Layer shadows", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.layer_shadows, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Floating only", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.shadow_only_floating, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Shadow size", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.shadows_size, 0, 50, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 4) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Shadow blur", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.shadows_blur, 0, 50, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 5) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Offset X", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.shadows_position_x, -30, 30, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 6) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Offset Y", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.shadows_position_y, -30, 30, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 7) ? app.settingsSliderDragNormT : -1.0);

  // Card 2: Blur
  int cy2 = cy1 + h1 + kCardGap;
  int h2 = 52 + 9 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy2, cardW, h2, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy2 + 15, "Blur");
  ry = cy2 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Enable blur", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.blur, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Blur layers", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.blur_layer, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Optimized blur", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.blur_optimized, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Radius", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.blur_params_radius, 1, 20, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 8) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Passes", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.blur_params_num_passes, 1, 6, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 9) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Noise", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.blur_params_noise * 100.0), 0, 10, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 10) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Brightness", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.blur_params_brightness * 100.0), 0, 200, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 11) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Contrast", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.blur_params_contrast * 100.0), 0, 200, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 12) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Saturation", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, static_cast<int>(cfg.blur_params_saturation * 100.0), 0, 200, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 13) ? app.settingsSliderDragNormT : -1.0);

  app.mangoContentBottom = cy2 + h2 + kSpacingL;
}

static bool mango_decorations_pointer(App& app, int contentX, int contentW) {
  auto& cfg = app.mangoConfig;
  const double lyA = app.pointerY + settings_scroll_px(app);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  int cardXi = contentX + 8;
  int cardWi = contentW - 16;
  int swX = cardXi + cardWi - 52 - 24;
  constexpr int S = 0 * 100;

  auto is_toggle = [&](int ry) { return point_in_rect(app.pointerX, lyA, swX, ry + 21, 52, 26); };
  auto is_slider = [&](int ry) { return point_in_rect(app.pointerX, lyA, trX - 6, ry + 20, trW + 12, 28); };
  auto set_slider = [&](int idx) { app.mangoSliderDrag = S + idx; mango_slider_apply(app, S + idx, app.pointerX); draw(app); };

  int h0 = 52 + 6 * kSliderRowH + kSpacingXL;
  int h1 = 52 + 7 * kSliderRowH + kSpacingXL;
  int c0y = kContentTop;

  // Card 0
  int ry = c0y + 52;
  if (is_slider(ry + 0 * kSliderRowH)) { set_slider(0); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { set_slider(1); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.no_border_when_single = !cfg.no_border_when_single; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 3 * kSliderRowH)) { cfg.no_radius_when_single = !cfg.no_radius_when_single; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 4 * kSliderRowH)) { set_slider(2); return true; }
  if (is_slider(ry + 5 * kSliderRowH)) { set_slider(3); return true; }

  // Card 1
  ry = c0y + h0 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.shadows = !cfg.shadows; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.layer_shadows = !cfg.layer_shadows; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.shadow_only_floating = !cfg.shadow_only_floating; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 3 * kSliderRowH)) { set_slider(4); return true; }
  if (is_slider(ry + 4 * kSliderRowH)) { set_slider(5); return true; }
  if (is_slider(ry + 5 * kSliderRowH)) { set_slider(6); return true; }
  if (is_slider(ry + 6 * kSliderRowH)) { set_slider(7); return true; }

  // Card 2
  ry = c0y + h0 + kCardGap + h1 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.blur = !cfg.blur; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.blur_layer = !cfg.blur_layer; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.blur_optimized = !cfg.blur_optimized; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 3 * kSliderRowH)) { set_slider(8); return true; }
  if (is_slider(ry + 4 * kSliderRowH)) { set_slider(9); return true; }
  if (is_slider(ry + 5 * kSliderRowH)) { set_slider(10); return true; }
  if (is_slider(ry + 6 * kSliderRowH)) { set_slider(11); return true; }
  if (is_slider(ry + 7 * kSliderRowH)) { set_slider(12); return true; }
  if (is_slider(ry + 8 * kSliderRowH)) { set_slider(13); return true; }

  return false;
}
