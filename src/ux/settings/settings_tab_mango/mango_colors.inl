// Colors sub-tab (index 1).

static void paint_mango_colors(App& app, cairo_t* cr, int contentX, int contentW,
                               double cardX, double cardW, double glassOv,
                               double dockMatA, double paintPointerYOffset) {
  (void)contentW; (void)dockMatA;
  auto& cfg = app.mangoConfig;
  int yOff = static_cast<int>(paintPointerYOffset);
  int cy = kContentTop - yOff;
  int h = 52 + 10 * kSliderRowH + kSpacingXL;

  settings_card(app, cr, cardX, cy, cardW, h, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Window Colors (0xRRGGBBAA)");
  int ry = cy + 52;

  auto color_row = [&](const char* label, const std::string& val) {
    settings_label(cr, contentX + kCardPad, ry, label, nullptr);
    settings_show_text(cr, contentX + kCardPad + 160, ry + 28, val.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60);
    ry += kSliderRowH;
  };

  color_row("Root", cfg.rootcolor);
  color_row("Border", cfg.bordercolor);
  color_row("Focus", cfg.focuscolor);
  color_row("Urgent", cfg.urgentcolor);
  color_row("Maximize", cfg.maximizescreencolor);
  color_row("Scratchpad", cfg.scratchpadcolor);
  color_row("Global", cfg.globalcolor);
  color_row("Overlay", cfg.overlaycolor);
  color_row("Drop shadow", cfg.dropcolor);
  color_row("Split", cfg.splitcolor);

  app.mangoContentBottom = cy + h + kSpacingL;
}

static bool mango_colors_pointer(App& app, int contentX, int contentW) {
  (void)app; (void)contentX; (void)contentW;
  return false;
}
