// Keybinds sub-tab (index 3).

static void paint_mango_keybinds(App& app, cairo_t* cr, int contentX, int contentW,
                                 double cardX, double cardW, double glassOv,
                                 double dockMatA, double paintPointerYOffset) {
  (void)contentW; (void)dockMatA;
  int yOff = static_cast<int>(paintPointerYOffset);
  int cy = kContentTop - yOff;

  settings_card(app, cr, cardX, cy, cardW, 64, glassOv);
  settings_cat_label(cr, cardX + kCardPad, cy + 15, "Keybinds");
  settings_show_text(cr, contentX + kCardPad, cy + 44, "Edit directly in ~/.config/mango/config.conf.", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);

  int ry = cy + 64 + kCardGap;
  int count = 0;
  const int rowH = 20;
  const int maxRows = 30;

  for (const auto& line : app.mangoConfigLines) {
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) trimmed.erase(0, 1);
    if (trimmed.rfind("bind", 0) == 0) {
      if (count == 0) {
        int nh = 52 + std::min(maxRows, 20) * rowH + kSpacingXL;
        settings_card(app, cr, cardX, ry, cardW, nh, glassOv);
        settings_cat_label(cr, cardX + kCardPad, ry + 15, "Current Keybinds");
        ry += 52;
      }
      if (count < maxRows) {
        std::string display = trimmed;
        if (display.size() > 80) display = display.substr(0, 77) + "...";
        settings_show_text(cr, contentX + kCardPad, ry + 14, display.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70f);
        ry += rowH;
        ++count;
      }
    }
  }

  if (count == 0) {
    settings_show_text(cr, contentX + kCardPad, cy + 64 + 20, "No keybinds found in config.", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.40f);
  }

  app.mangoContentBottom = ry + kSpacingL;
}

static bool mango_keybinds_pointer(App& app, int contentX, int contentW) {
  (void)app; (void)contentX; (void)contentW;
  return false;
}
