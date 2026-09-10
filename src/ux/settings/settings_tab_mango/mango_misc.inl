// Misc sub-tab (index 6).

static void paint_mango_misc(App& app, cairo_t* cr, int contentX, int contentW,
                             double cardX, double cardW, double glassOv,
                             double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  constexpr int S = 6 * 100;
  int cy = kContentTop - yOff;

  // Card 0: Focus & Cursor
  int h0 = 52 + 6 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy, cardW, h0, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Focus & Cursor");
  int ry = cy + 52;

  settings_label(cr, contentX + kCardPad, ry, "Focus on activate", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.focus_on_activate, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Sloppy focus", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.sloppyfocus, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Warp cursor", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.warpcursor, glassOv);
  ry += kSliderRowH;

  char buf[16];
  settings_label(cr, contentX + kCardPad, ry, "Cursor size", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.cursor_size, 16, 64, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 0) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Hide cursor timeout", nullptr);
  std::snprintf(buf, sizeof(buf), "%ds", cfg.cursor_hide_timeout);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.cursor_hide_timeout, 0, 60, paintPointerYOffset, buf, false, (app.mangoSliderDrag == S + 3) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_show_text(cr, contentX + kCardPad, ry + 14, "Cursor theme:", 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
  settings_show_text(cr, contentX + kCardPad + 110, ry + 14, cfg.cursor_theme.empty() ? "(default)" : cfg.cursor_theme.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);

  // Card 1: Window Behavior
  int cy1 = cy + h0 + kCardGap;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy1, cardW, h1, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy1 + 15, "Window Behavior");
  ry = cy1 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Floating snap", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.enable_floating_snap, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Snap distance", nullptr);
  settings_slider(app, cr, trX, ry + 20, trW, cfg.snap_distance, 0, 200, paintPointerYOffset, nullptr, false, (app.mangoSliderDrag == S + 1) ? app.settingsSliderDragNormT : -1.0);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Drag tile to tile", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.drag_tile_to_tile, glassOv);
  ry += kSliderRowH;

  {
    std::snprintf(buf, sizeof(buf), "%d", cfg.allow_tearing);
    settings_show_text(cr, contentX + kCardPad, ry + 14, "Allow tearing:", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);
    settings_show_text(cr, contentX + kCardPad + 140, ry + 14, buf, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);
  }
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "XWayland persistence", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.xwayland_persistence, glassOv);

  // Card 2: Cross-Monitor & Tags
  int cy2 = cy1 + h1 + kCardGap;
  int h2 = 52 + 5 * kSliderRowH + kSpacingXL;
  settings_card(app, cr, cardX, cy2, cardW, h2, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy2 + 15, "Cross-Monitor & Tags");
  ry = cy2 + 52;

  settings_label(cr, contentX + kCardPad, ry, "Cross-monitor focus", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.focus_cross_monitor, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Cross-tag focus", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.focus_cross_tag, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "View current to back", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.view_current_to_back, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Single scratchpad", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.single_scratchpad, glassOv);
  ry += kSliderRowH;

  settings_label(cr, contentX + kCardPad, ry, "Allow lock transparent", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), ry, static_cast<int>(cardW), static_cast<double>(ry), kSliderRowH, cfg.allow_lock_transparent, glassOv);

  app.mangoContentBottom = cy2 + h2 + kSpacingL;
}

static bool mango_misc_pointer(App& app, int contentX, int contentW) {
  auto& cfg = app.mangoConfig;
  const double lyA = app.pointerY + settings_scroll_px(app);
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;
  int cardXi = contentX + 8;
  int cardWi = contentW - 16;
  int swX = cardXi + cardWi - 52 - 24;
  constexpr int S = 6 * 100;

  auto is_toggle = [&](int ry) { return point_in_rect(app.pointerX, lyA, swX, ry + 21, 52, 26); };
  auto is_slider = [&](int ry) { return point_in_rect(app.pointerX, lyA, trX - 6, ry + 20, trW + 12, 28); };

  int h0 = 52 + 6 * kSliderRowH + kSpacingXL;
  int h1 = 52 + 5 * kSliderRowH + kSpacingXL;

  // Card 0
  int ry = kContentTop + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.focus_on_activate = !cfg.focus_on_activate; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.sloppyfocus = !cfg.sloppyfocus; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.warpcursor = !cfg.warpcursor; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 3 * kSliderRowH)) { app.mangoSliderDrag = S + 0; mango_slider_apply(app, S + 0, app.pointerX); draw(app); return true; }
  if (is_slider(ry + 4 * kSliderRowH)) { app.mangoSliderDrag = S + 3; mango_slider_apply(app, S + 3, app.pointerX); draw(app); return true; }

  // Card 1
  ry = kContentTop + h0 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.enable_floating_snap = !cfg.enable_floating_snap; mango_commit_cfg(app); draw(app); return true; }
  if (is_slider(ry + 1 * kSliderRowH)) { app.mangoSliderDrag = S + 1; mango_slider_apply(app, S + 1, app.pointerX); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.drag_tile_to_tile = !cfg.drag_tile_to_tile; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 4 * kSliderRowH)) { cfg.xwayland_persistence = !cfg.xwayland_persistence; mango_commit_cfg(app); draw(app); return true; }

  // Card 2
  ry = kContentTop + h0 + kCardGap + h1 + kCardGap + 52;
  if (is_toggle(ry + 0 * kSliderRowH)) { cfg.focus_cross_monitor = !cfg.focus_cross_monitor; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 1 * kSliderRowH)) { cfg.focus_cross_tag = !cfg.focus_cross_tag; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 2 * kSliderRowH)) { cfg.view_current_to_back = !cfg.view_current_to_back; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 3 * kSliderRowH)) { cfg.single_scratchpad = !cfg.single_scratchpad; mango_commit_cfg(app); draw(app); return true; }
  if (is_toggle(ry + 4 * kSliderRowH)) { cfg.allow_lock_transparent = !cfg.allow_lock_transparent; mango_commit_cfg(app); draw(app); return true; }

  return false;
}
