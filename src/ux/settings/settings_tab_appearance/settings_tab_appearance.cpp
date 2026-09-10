#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/controls/input/toggle.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_appearance/settings_tab_appearance.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "desktop_shell/common/palette/matugen_external_templates.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);
extern void settings_close_mode_dropdowns(App& app);

// Dynamic-color dropdown constants.
extern const char* const kMatugenSchemeLabels[] = {"Content",      "Expressive", "Fidelity",   "Fruit salad",
                                                    "Monochrome",   "Neutral",    "Rainbow",    "Tonal spot",
                                                    "Vibrant"};
extern const char* const kMatugenSchemeValues[] = {"scheme-content",    "scheme-expressive", "scheme-fidelity",
                                                    "scheme-fruit-salad", "scheme-monochrome", "scheme-neutral",
                                                    "scheme-rainbow",     "scheme-tonal-spot", "scheme-vibrant"};
extern const int kMatugenSchemeCount = 9;
extern const char* const kMatugenModeLabels[] = {"Dark", "Light", "Auto (dark)"};
extern const char* const kMatugenModeValues[] = {"dark", "light", "auto"};
extern const int kMatugenModeCount = 3;


static constexpr int kAppearChildContentTop = kContentTop + kDockChildTabH + 12;

// Two-column card grid geometry for the overlay sliders.
// Items: 0=master opacity slider, 1=advanced toggle, 2-15=per-surface sliders
void appearance_app_geom(int contentX, int contentW, int idx,
                         int& trX, int& trY, int& trW,
                         int& cardX, int& cardY, int& cardW,
                         int contentTop) {
  constexpr int kCardH = 92;
  constexpr int kGap = 12;
  constexpr int kPad = 20;
  constexpr int kSliderY = 50;

  const int col = idx % 2;
  const int row = idx / 2;
  const int colW = (contentW * 68) / 100;
  const int colX = contentX + (contentW - colW) / 2;
  cardW = (colW - kGap) / 2;
  if (cardW < 160) cardW = 160;
  cardX = colX + col * (cardW + kGap);
  constexpr int kTopCardHLocal = 256 + 60;
  cardY = contentTop + kTopCardHLocal + kCardGap + kSpacingXL + row * (kCardH + kGap);
  trX = cardX + kPad;
  trW = cardW - kPad - kPad;
  if (trW < 40) trW = 40;
  trY = cardY + kSliderY;
}

// Top card height and templates-card positioning.
static constexpr int kTopCardH = 256 + 60;
static int top_card_bottom() { return kAppearChildContentTop + kTopCardH; }

static int appearance_templates_card_height_px(int viewportH) {
  constexpr int kRows = 15;
  const int contentH = 52 + 60 + kRows * kDockVisRowPitch + kSpacingL;
  const int fillH = viewportH - (kAppearChildContentTop - kContentTop);
  return std::max(contentH, fillH);
}

static int appearance_general_bottom_px(const Settings& s) {
  const int maxIdx = s.overlayOpacityAdvanced ? 15 : 1;
  const int maxRow = maxIdx / 2;
  constexpr int kCardH = 92;
  constexpr int kGap = 12;
  int bottom = kAppearChildContentTop + kTopCardH + kCardGap + kSpacingXL + maxRow * (kCardH + kGap) + kCardH;
  bottom += kSpacingXL + 52 + 2 * (kCardH + kGap);
  return bottom;
}

static int settings_appearance_scroll_max_px(const App& app) {
  const int viewportH = std::max(120, app.height - kContentTop - kSpacingL);
  const int bottom = (app.appearanceChildTab == kAppearanceTemplates)
    ? (kAppearChildContentTop + appearance_templates_card_height_px(viewportH) + kSpacingL)
    : appearance_general_bottom_px(app.settings);
  return std::max(0, bottom - kContentTop - viewportH);
}

void settings_clamp_appearance_scroll_px(App& app) {
  const int mx = settings_appearance_scroll_max_px(app);
  app.settingsAppearanceScrollPx = std::clamp(app.settingsAppearanceScrollPx, 0, mx);
}

int appearance_overlay_card_top() { return top_card_bottom(); }

// Dynamic-color helper functions.
int matugen_scheme_ui_index(const Settings& s) {
  for (int i = 0; i < kMatugenSchemeCount; ++i) {
    if (s.matugenScheme == kMatugenSchemeValues[i]) return i;
  }
  return 0;
}

int matugen_mode_ui_index(const Settings& s) {
  if (s.matugenMode == "light") return 1;
  if (s.matugenMode == "auto") return 2;
  return 0;
}

// M3-style row positions: row 0 = 52, row 1 = 52+60, row 2 = 52+120
void matugen_scheme_combo_geom(int contentX, int contentW, int matugenRowTop, int* cx, int* cy, int* cw,
                               int* ch) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
  *cx = cardXi + cardWi - kCardPad - *cw;
  const int bandTop = matugenRowTop + 52 + 2 * kDockVisRowPitch;
  *cy = bandTop + (kDockVisRowPitch - kSettingsComboH) / 2;
}

void matugen_mode_combo_geom(int contentX, int contentW, int matugenRowTop, int* cx, int* cy, int* cw, int* ch) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
  *cx = cardXi + cardWi - kCardPad - *cw;
  const int bandTop = matugenRowTop + 52 + 3 * kDockVisRowPitch;
  *cy = bandTop + (kDockVisRowPitch - kSettingsComboH) / 2;
}

// Dropdown component sync.
// Feed the self-contained dropdowns their labels, selection and anchor once
// per frame (paint + hit tests call these so geometry is never stale).
void matugen_scheme_dd_sync(App& app, int contentX, int contentW) {
  int scx, scy, scw, sch;
  matugen_scheme_combo_geom(contentX, contentW, kAppearChildContentTop, &scx, &scy, &scw, &sch);
  app.matugenSchemeDd.set_labels(kMatugenSchemeLabels, kMatugenSchemeCount);
  app.matugenSchemeDd.set_selected(matugen_scheme_ui_index(app.settings));
  app.matugenSchemeDd.set_row_h(kSettingsDdRowH);
  app.matugenSchemeDd.set_anchor(scx, scy, scw, sch);
}

void matugen_mode_dd_sync(App& app, int contentX, int contentW) {
  int mcx, mcy, mcw, mch;
  matugen_mode_combo_geom(contentX, contentW, kAppearChildContentTop, &mcx, &mcy, &mcw, &mch);
  app.matugenModeDd.set_labels(kMatugenModeLabels, kMatugenModeCount);
  app.matugenModeDd.set_selected(matugen_mode_ui_index(app.settings));
  app.matugenModeDd.set_row_h(kSettingsDdRowH);
  app.matugenModeDd.set_anchor(mcx, mcy, mcw, mch);
}

// Apply color slider.
bool apply_appearance_color_slider(App& app, int which, double px) {
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  const int colW = (contentW * 68) / 100;
  const int colX = contentX + (contentW - colW) / 2;
  constexpr int kColorGap = 12;
  const int col = which % 2;
  const int cw = std::max(160, (colW - kColorGap) / 2);
  const int cx = colX + col * (cw + kColorGap);
  const int trX = cx + 20;
  const int trW = cw - 40;

  static const int kMins[] = {50, 50, 0, 50};
  static const int kMaxs[] = {150, 150, 200, 200};
  static int* const kVals[] = {
    &app.settings.colorBrightnessPct,
    &app.settings.colorContrastPct,
    &app.settings.colorVibrancePct,
    &app.settings.colorGammaPct,
  };
  if (which < 0 || which > 3) return false;
  const int v = slider_value_from_x(px, trX, trW, kMins[which], kMaxs[which]);
  if (v == *kVals[which]) return false;
  *kVals[which] = v;
  return true;
}

// Apply overlay slider.
bool apply_appearance_overlay_slider(App& app, int which, double px) {
  int trX, trY, trW;
  int cardX, cardY, cardW;
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  const int gridIdx = (which == 0) ? 0 : (which + 1);
  appearance_app_geom(contentX, contentW, gridIdx, trX, trY, trW, cardX, cardY, cardW, kContentTop);
  (void)trY;
  const int v = slider_value_from_x(px, trX, trW, 0, 100);
  if (which == 0) {
    if (v == app.settings.overlayOpacityMasterPct) return false;
    app.settings.overlayOpacityMasterPct = v;
    return true;
  }
  if (which == 1) {
    if (v == app.settings.overlayOpacityCcPct) return false;
    app.settings.overlayOpacityCcPct = v;
    return true;
  }
  if (which == 2) {
    if (v == app.settings.overlayOpacityCcInnerPct) return false;
    app.settings.overlayOpacityCcInnerPct = v;
    return true;
  }
  if (which == 3) {
    if (v == app.settings.overlayOpacityAppDrawerPct) return false;
    app.settings.overlayOpacityAppDrawerPct = v;
    return true;
  }
  if (which == 4) {
    if (v == app.settings.overlayOpacityLaunchpadPct) return false;
    app.settings.overlayOpacityLaunchpadPct = v;
    return true;
  }
  if (which == 5) {
    if (v == app.settings.overlayOpacitySettingsPct) return false;
    app.settings.overlayOpacitySettingsPct = v;
    return true;
  }
  if (which == 6) {
    if (v == app.settings.overlayOpacitySettingsSidebarPct) return false;
    app.settings.overlayOpacitySettingsSidebarPct = v;
    return true;
  }
  if (which == 7) {
    if (v == app.settings.overlayOpacityDockMenuPct) return false;
    app.settings.overlayOpacityDockMenuPct = v;
    return true;
  }
  if (which == 8) {
    if (v == app.settings.overlayOpacityDesktopMenuPct) return false;
    app.settings.overlayOpacityDesktopMenuPct = v;
    return true;
  }
  if (which == 9) {
    if (v == app.settings.overlayOpacityTrayMenuPct) return false;
    app.settings.overlayOpacityTrayMenuPct = v;
    return true;
  }
  if (which == 10) {
    if (v == app.settings.overlayOpacityCalendarPct) return false;
    app.settings.overlayOpacityCalendarPct = v;
    return true;
  }
  if (which == 11) {
    if (v == app.settings.overlayOpacityWeatherPct) return false;
    app.settings.overlayOpacityWeatherPct = v;
    return true;
  }
  if (which == 12) {
    if (v == app.settings.overlayOpacityTooltipPct) return false;
    app.settings.overlayOpacityTooltipPct = v;
    return true;
  }
  if (which == 13) {
    if (v == app.settings.overlayOpacityWidgetCardPct) return false;
    app.settings.overlayOpacityWidgetCardPct = v;
    return true;
  }
  if (v == app.settings.overlayOpacityNotificationsPct) return false;
  app.settings.overlayOpacityNotificationsPct = v;
  return true;
}

// Helpers shared by paint and pointer-down.
static const char* overlay_title(int idx) {
  static const char* const kTitles[] = {
    "Master opacity",
    "Advanced",
    "Control center", "Control center tiles", "App drawer", "Launchpad",
    "Settings", "Settings sidebar", "Dock context menus", "Desktop context menus",
    "Tray menus", "Calendar", "Weather", "Dock tooltip", "Widget cards", "Notifications"
  };
  if (idx < 0 || idx > 15) return "?";
  return kTitles[idx];
}

static int overlay_value(const App& app, int idx) {
  if (idx == 0) return app.settings.overlayOpacityMasterPct;
  if (idx == 2) return app.settings.overlayOpacityCcPct;
  if (idx == 3) return app.settings.overlayOpacityCcInnerPct;
  if (idx == 4) return app.settings.overlayOpacityAppDrawerPct;
  if (idx == 5) return app.settings.overlayOpacityLaunchpadPct;
  if (idx == 6) return app.settings.overlayOpacitySettingsPct;
  if (idx == 7) return app.settings.overlayOpacitySettingsSidebarPct;
  if (idx == 8) return app.settings.overlayOpacityDockMenuPct;
  if (idx == 9) return app.settings.overlayOpacityDesktopMenuPct;
  if (idx == 10) return app.settings.overlayOpacityTrayMenuPct;
  if (idx == 11) return app.settings.overlayOpacityCalendarPct;
  if (idx == 12) return app.settings.overlayOpacityWeatherPct;
  if (idx == 13) return app.settings.overlayOpacityTooltipPct;
  if (idx == 14) return app.settings.overlayOpacityWidgetCardPct;
  if (idx == 15) return app.settings.overlayOpacityNotificationsPct;
  return 100;
}

// Draw value pill.
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
    bg.paint(cr);
  }
  cairo_set_source_rgba(cr, textR, textG, textB, 0.60f);
  cairo_move_to(cr, pillX + padX, pillY + te.height + padY - 2.0);
  cairo_show_text(cr, text);
  cairo_restore(cr);
}

// Dynamic-color templates card.
using MatTpl = eh::config::MatugenExternalTemplateToggles;

static bool matugen_tpl_get(const Settings& s, int idx) {
  const MatTpl& m = s.matugenOutputs;
  switch (idx) {
    case 0: return m.niri;
    case 1: return m.hyprland;
    case 2: return m.mango;
    case 3: return m.gtkShellCss;
    case 4: return m.gtkEventColorsLight;
    case 5: return m.gtkEventColorsDark;
    case 6: return m.kcolorscheme;
    case 7: return m.qt5ct;
    case 8: return m.qt6ct;
    case 9: return m.kittyTheme;
    case 10: return m.kittyTabs;
    case 11: return m.ghostty;
    case 12: return m.wezterm;
    case 13: return m.alacritty;
    case 14: return m.foot;
    case 15: return m.otterTerm;
    case 16: return m.btop;
    case 17: return m.neovim;
    case 18: return m.vscodeMaterial;
    case 19: return m.vscodeColorThemes;
    case 20: return m.firefox;
    case 21: return m.zenbrowser;
    case 22: return m.vesktop;
    case 23: return m.equibop;
    case 24: return m.pywalfox;
    case 25: return m.steam;
    case 26: return m.dgop;
    case 27: return m.emacs;
    case 28: return m.zed;
    case 29: return m.ptyxis;
    case 30: return m.horizonFiles;
    case 31: return m.obs;
    case 32: return m.heroic;
    case 33: return m.horizonPhoto;
    case 34: return m.horizonCalendar;
    default: return true;
  }
}

static void matugen_tpl_set(Settings& s, int idx, bool v) {
  MatTpl& m = s.matugenOutputs;
  switch (idx) {
    case 0: m.niri = v; break;
    case 1: m.hyprland = v; break;
    case 2: m.mango = v; break;
    case 3: m.gtkShellCss = v; break;
    case 4: m.gtkEventColorsLight = v; break;
    case 5: m.gtkEventColorsDark = v; break;
    case 6: m.kcolorscheme = v; break;
    case 7: m.qt5ct = v; break;
    case 8: m.qt6ct = v; break;
    case 9: m.kittyTheme = v; break;
    case 10: m.kittyTabs = v; break;
    case 11: m.ghostty = v; break;
    case 12: m.wezterm = v; break;
    case 13: m.alacritty = v; break;
    case 14: m.foot = v; break;
    case 15: m.otterTerm = v; break;
    case 16: m.btop = v; break;
    case 17: m.neovim = v; break;
    case 18: m.vscodeMaterial = v; break;
    case 19: m.vscodeColorThemes = v; break;
    case 20: m.firefox = v; break;
    case 21: m.zenbrowser = v; break;
    case 22: m.vesktop = v; break;
    case 23: m.equibop = v; break;
    case 24: m.pywalfox = v; break;
    case 25: m.steam = v; break;
    case 26: m.dgop = v; break;
    case 27: m.emacs = v; break;
    case 28: m.zed = v; break;
    case 29: m.ptyxis = v; break;
    case 30: m.horizonFiles = v; break;
    case 31: m.obs = v; break;
    case 32: m.heroic = v; break;
    case 33: m.horizonPhoto = v; break;
    case 34: m.horizonCalendar = v; break;
    default: break;
  }
}

static const char* matugen_tpl_title(int idx) {
  static const char* const kT[35] = {
      "Niri",           "Hyprland",      "Mango",           "GTK shell CSS",    "GTK colors (light)",
      "GTK colors (dark)", "KDE colors", "Qt5ct",             "Qt6ct",            "Kitty theme",
      "Kitty tabs",     "Ghostty",       "WezTerm",           "Alacritty",        "Foot",
      "Otter term",     "btop",          "Neovim",            "VS Code material", "VS Code themes",
      "Firefox",        "Zen",           "Vesktop",           "Equibop",          "PyWalFox",
      "Steam",          "dgop",          "Emacs",             "Zed",
      "Ptyxis",         "Horizon Files", "OBS Studio",        "Heroic",
      "Horizon Photo",  "Horizon Calendar",
  };
  if (idx < 0 || idx >= 35) return "?";
  return kT[static_cast<size_t>(idx)];
}

static constexpr int kMatugenTplToggleCount = 35;
static constexpr int kMatugenTplCols = 2;
static constexpr int kMatugenTplRows = (kMatugenTplToggleCount + kMatugenTplCols - 1) / kMatugenTplCols;

static void matugen_tpl_toggle_geom(int contentX, int contentW, int tplCardTop, int idx,
                                    int* rx, int* ry, int* rw, int* rh) {
  const int row = idx / kMatugenTplCols;
  const int col = idx % kMatugenTplCols;
  const int gap = 10;
  const int innerW = contentW - 2 * kCardPad;
  const int cellW = (innerW - gap) / kMatugenTplCols;
  *rx = contentX + kCardPad + col * (cellW + gap);
  // Grid rows start after the M3 row for "Use base.toml recipes"
  *ry = tplCardTop + 52 + kDockVisRowPitch + row * kDockVisRowPitch;
  *rw = cellW;
  *rh = kDockVisRowPitch;
}

static bool appearance_pointer_matugen_templates(App& app, int contentX, int contentW, double px, double ly, int tplTop) {
  // Narrow centered column like paint
  const int colW = std::max(100, (contentW * 68) / 100);
  const int colX = contentX + (contentW - colW) / 2;

  // "Use base.toml recipes" row hit box
  {
    const int bandTop = tplTop + 52;
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const int tgX = colX + colW - kCardPad - static_cast<int>(tgW);
    const int tgY = bandTop + (kDockVisRowPitch - static_cast<int>(tgH)) / 2;
    if (point_in_rect(px, ly, tgX - 6, tgY - 4, static_cast<int>(tgW) + 12, static_cast<int>(tgH) + 8)) {
      app.settings.matugenOutputs.runBundledToml = !app.settings.matugenOutputs.runBundledToml;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }
  // 30 template toggles
  for (int i = 0; i < kMatugenTplToggleCount; ++i) {
    int rx, ry, rw, rh;
    matugen_tpl_toggle_geom(colX, colW, tplTop, i, &rx, &ry, &rw, &rh);
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const int tgX = rx + rw - kCardPad - static_cast<int>(tgW);
    const int tgY = ry + (kDockVisRowPitch - static_cast<int>(tgH)) / 2;
    if (point_in_rect(px, ly, tgX - 6, tgY - 4, static_cast<int>(tgW) + 12, static_cast<int>(tgH) + 8)) {
      matugen_tpl_set(app.settings, i, !matugen_tpl_get(app.settings, i));
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }
  return false;
}

// Child tab bar.
static void paint_appearance_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                            float textR, float textG, float textB,
                                            int activeTab) {
  constexpr int kTabW = 130;
  const int barX = contentX + 8;
  const int barY = kContentTop;
  const int barW = contentW - 16;

  m3::Box bg;
  bg.setColor(textR, textG, textB, 0.04f);
  bg.setRadius(8.0f);
  bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                 static_cast<float>(barW), static_cast<float>(kDockChildTabH + 6));
  bg.paint(cr);

  const int tabY = barY + 3;
  const int tabH = kDockChildTabH;

  static const char* kChildLabels[] = {"General", "Templates"};
  for (int i = 0; i < 2; ++i) {
    const int tx = barX + 4 + i * kTabW;
    const bool sel = (i == activeTab);

    if (sel) {
      m3::Box selBg;
      selBg.setColor(textR, textG, textB, 0.10f);
      selBg.setRadius(6.0f);
      selBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY),
                        static_cast<float>(kTabW), static_cast<float>(tabH));
      selBg.paint(cr);
    }

    const char* label = kChildLabels[i];
    m3::Label lbl;
    lbl.setText(label);
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(sel ? 600 : 400);
    lbl.setColor(textR, textG, textB, sel ? 0.90f : 0.55f);
    float lw, lh;
    lbl.measureExtents(lw, lh);
    lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
  }
}

static int appearance_hit_child_tab(float px, float py, int contentX) {
  constexpr int kTabW = 130;
  const int barX = contentX + 8;
  const int barY = kContentTop + 3;
  for (int i = 0; i < 2; ++i) {
    const int tx = barX + 4 + i * kTabW;
    if (px >= tx && px < tx + kTabW && py >= barY && py < barY + kDockChildTabH)
      return i;
  }
  return -1;
}

// General child tab paint.
static void paint_appearance_general_tab(App& app, cairo_t* cr, int contentX, int contentW,
                                          double cardX, double cardW, double glassOv,
                                          double paintPointerYOffset) {
  float textR, textG, textB;
  float outR, outG, outB;
  float accR, accG, accB;
  if (app.drawChromeMatugen) {
    textR = static_cast<float>(app.drawChrome.textR);
    textG = static_cast<float>(app.drawChrome.textG);
    textB = static_cast<float>(app.drawChrome.textB);
    outR = static_cast<float>(app.drawChrome.outlineR);
    outG = static_cast<float>(app.drawChrome.outlineG);
    outB = static_cast<float>(app.drawChrome.outlineB);
    accR = static_cast<float>(app.drawChrome.accentR);
    accG = static_cast<float>(app.drawChrome.accentG);
    accB = static_cast<float>(app.drawChrome.accentB);
  } else {
    textR = static_cast<float>(Theme::TextR);
    textG = static_cast<float>(Theme::TextG);
    textB = static_cast<float>(Theme::TextB);
    outR = static_cast<float>(Theme::TextR);
    outG = static_cast<float>(Theme::TextG);
    outB = static_cast<float>(Theme::TextB);
    accR = static_cast<float>(Theme::AccR);
    accG = static_cast<float>(Theme::AccG);
    accB = static_cast<float>(Theme::AccB);
  }

  // M3-style top card: "Appearance".
  const double topCardTop = static_cast<double>(kAppearChildContentTop);
  settings_card(app, cr, cardX, topCardTop, cardW, static_cast<double>(kTopCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, topCardTop + 22, "Appearance");
  settings_show_text(cr, cardX + kCardPad, topCardTop + 44,
                     "System color and style settings", 11.f, 400, textR, textG, textB, 0.46f);

  // Row 0: Wallpaper tint toggle
  {
    const int bandTop = kAppearChildContentTop + 52;
    const int titleY = bandTop + 36;
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const float tgX = static_cast<float>(cardX + cardW - kCardPad - tgW);
    const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
    m3::Toggle matugenTg;
    matugenTg.setSize(m3::Toggle::Size::L);
    matugenTg.setGeometry(tgX, tgY, tgW, tgH);
    matugenTg.setOn(app.settings.matugenThemingEnabled);
    matugenTg.setEnabled(true);
    matugenTg.setAccentColor(accR, accG, accB);

    settings_show_text(cr, cardX + kCardPad, titleY, "Wallpaper tint", 14.f, 500,
                       textR, textG, textB, 0.93f);
    settings_show_text(cr, cardX + kCardPad, titleY + 17, "Generate Material You colors from wallpaper.", 11.f, 400,
                       textR, textG, textB, 0.46f);
    matugenTg.paint(cr, 0);
  }

  // Row 1: Native color engine toggle
  {
    const int bandTop = kAppearChildContentTop + 52 + 1 * kDockVisRowPitch;

    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cardX + kCardPad + 8, bandTop);
    cairo_line_to(cr, cardX + cardW - kCardPad - 8, bandTop);
    cairo_stroke(cr);

    const int titleY = bandTop + 36;
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const float tgX = static_cast<float>(cardX + cardW - kCardPad - tgW);
    const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
    m3::Toggle nativeTg;
    nativeTg.setSize(m3::Toggle::Size::L);
    nativeTg.setGeometry(tgX, tgY, tgW, tgH);
    nativeTg.setOn(app.settings.horizonColorsNative);
    nativeTg.setEnabled(true);
    nativeTg.setAccentColor(accR, accG, accB);

    settings_show_text(cr, cardX + kCardPad, titleY, "Color engine", 14.f, 500,
                       textR, textG, textB, 0.93f);
    settings_show_text(cr, cardX + kCardPad, titleY + 17, "Use native C++ engine instead of external matugen.", 11.f, 400,
                       textR, textG, textB, 0.46f);
    nativeTg.paint(cr, 0);
  }

  // Row 2: Color scheme combo
  {
    const int bandTop = kAppearChildContentTop + 52 + 2 * kDockVisRowPitch;

    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cardX + kCardPad + 8, bandTop);
    cairo_line_to(cr, cardX + cardW - kCardPad - 8, bandTop);
    cairo_stroke(cr);

    matugen_scheme_dd_sync(app, contentX, contentW);
    settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Color scheme", 14.f, 500,
                       textR, textG, textB, 0.93f);
    settings_show_text(cr, cardX + kCardPad, bandTop + 53, "Select the tonal palette for generated colors.", 11.f, 400,
                       textR, textG, textB, 0.46f);
    app.matugenSchemeDd.paint_trigger(app, cr, glassOv, settings_scroll_px_int(app));
  }

  // Row 3: Theme mode combo
  {
    const int bandTop = kAppearChildContentTop + 52 + 3 * kDockVisRowPitch;

    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cardX + kCardPad + 8, bandTop);
    cairo_line_to(cr, cardX + cardW - kCardPad - 8, bandTop);
    cairo_stroke(cr);

    matugen_mode_dd_sync(app, contentX, contentW);
    settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Theme mode", 14.f, 500,
                       textR, textG, textB, 0.93f);
    settings_show_text(cr, cardX + kCardPad, bandTop + 53, "Dark, light, or automatic theme switching.", 11.f, 400,
                       textR, textG, textB, 0.46f);
    app.matugenModeDd.paint_trigger(app, cr, glassOv, settings_scroll_px_int(app));
  }

  // Two-column grid for overlay items.
  const int maxIdx = app.settings.overlayOpacityAdvanced ? 15 : 1;
  for (int idx = 0; idx <= maxIdx; ++idx) {
    int trX, trY, trW, cardX2, cardY, cardW2;
    appearance_app_geom(contentX, contentW, idx, trX, trY, trW, cardX2, cardY, cardW2, kAppearChildContentTop);

    settings_card(app, cr, static_cast<double>(cardX2), static_cast<double>(cardY),
                  static_cast<double>(cardW2), 92.0, glassOv);

    settings_show_text(cr, static_cast<double>(cardX2 + 20), static_cast<double>(cardY + 28),
                       overlay_title(idx), 13.f, 500, textR, textG, textB, 0.90f);

    if (idx == 1) {
      settings_toggle(app, cr, cardX2, 0, cardW2,
                      static_cast<double>(cardY - 24), 92.0,
                      app.settings.overlayOpacityAdvanced, 0.0);
    } else {
      char valStr[16];
      std::snprintf(valStr, sizeof(valStr), "%d%%", overlay_value(app, idx));
      draw_value_pill(cr, cardX2, cardY, cardW2, valStr, textR, textG, textB);
      const double dn = (app.appearanceOverlaySliderDrag == (idx == 0 ? 0 : idx - 1)) ? app.settingsSliderDragNormT : -1.0;
      settings_slider(app, cr, trX, trY, trW, overlay_value(app, idx), 0, 100, paintPointerYOffset,
                      valStr, false, dn);
    }
  }

  // Color adjustment sliders.
  {
    const int overlayBottom = kAppearChildContentTop + kTopCardH + kCardGap + kSpacingXL
                              + (maxIdx / 2) * (92 + 12) + 92;
    const int colorSectionTop = overlayBottom + kSpacingXL;
    constexpr int kColorCardH = 92;
    constexpr int kColorGap = 12;
    const int colW = (contentW * 68) / 100;
    const int colX = contentX + (contentW - colW) / 2;

    cairo_set_source_rgba(cr, outR, outG, outB, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, static_cast<double>(colX) + kCardPad + 8, colorSectionTop);
    cairo_line_to(cr, static_cast<double>(colX + colW) - kCardPad - 8, colorSectionTop);
    cairo_stroke(cr);

    settings_show_text(cr, static_cast<double>(colX + kCardPad), static_cast<double>(colorSectionTop + 18),
                       "Color adjustment", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(colX + kCardPad), static_cast<double>(colorSectionTop + 35),
                       "Tune brightness, contrast, vibrance and gamma.", 11.f, 400,
                       textR, textG, textB, 0.46f);

    struct ColorSliderDef { const char* label; const char* sub; int* val; int min; int max; };
    ColorSliderDef defs[4] = {
      {"Brightness", "Overall brightness.", &app.settings.colorBrightnessPct, 50, 150},
      {"Contrast", "Contrast stretch.", &app.settings.colorContrastPct, 50, 150},
      {"Vibrance", "Color saturation.", &app.settings.colorVibrancePct, 0, 200},
      {"Gamma", "Midtone contrast curve.", &app.settings.colorGammaPct, 50, 200},
    };

    for (int i = 0; i < 4; ++i) {
      const int col = i % 2;
      const int row = i / 2;
      const int cw = std::max(160, (colW - kColorGap) / 2);
      const int cx = colX + col * (cw + kColorGap);
      const int cy = colorSectionTop + 52 + row * (kColorCardH + kColorGap);

      settings_card(app, cr, static_cast<double>(cx), static_cast<double>(cy),
                    static_cast<double>(cw), static_cast<double>(kColorCardH), glassOv);

      settings_show_text(cr, static_cast<double>(cx + 20), static_cast<double>(cy + 28),
                         defs[i].label, 13.f, 500, textR, textG, textB, 0.90f);

      char valStr[16];
      std::snprintf(valStr, sizeof(valStr), "%d%%", *defs[i].val);
      draw_value_pill(cr, cx, cy, cw, valStr, textR, textG, textB);

      const int trX = cx + 20;
      const int trW = cw - 40;
      const int trY = cy + 50;
      const double dn = (app.appearanceColorSliderDrag == i) ? app.settingsSliderDragNormT : -1.0;
      settings_slider(app, cr, trX, trY, trW, *defs[i].val, defs[i].min, defs[i].max,
                      paintPointerYOffset, valStr, false, dn);
    }
  }
}

// Templates child tab paint.
static void paint_appearance_templates_tab(App& app, cairo_t* cr, int contentX, int contentW,
                                            double, double, double glassOv) {
  // Narrow centered column like the monitors tab
  const int colW = std::max(100, (contentW * 68) / 100);
  const int colX = contentX + (contentW - colW) / 2;

  float textR, textG, textB;
  float outR, outG, outB;
  float accR, accG, accB;
  if (app.drawChromeMatugen) {
    textR = static_cast<float>(app.drawChrome.textR);
    textG = static_cast<float>(app.drawChrome.textG);
    textB = static_cast<float>(app.drawChrome.textB);
    outR = static_cast<float>(app.drawChrome.outlineR);
    outG = static_cast<float>(app.drawChrome.outlineG);
    outB = static_cast<float>(app.drawChrome.outlineB);
    accR = static_cast<float>(app.drawChrome.accentR);
    accG = static_cast<float>(app.drawChrome.accentG);
    accB = static_cast<float>(app.drawChrome.accentB);
  } else {
    textR = static_cast<float>(Theme::TextR);
    textG = static_cast<float>(Theme::TextG);
    textB = static_cast<float>(Theme::TextB);
    outR = static_cast<float>(Theme::TextR);
    outG = static_cast<float>(Theme::TextG);
    outB = static_cast<float>(Theme::TextB);
    accR = static_cast<float>(Theme::AccR);
    accG = static_cast<float>(Theme::AccG);
    accB = static_cast<float>(Theme::AccB);
  }

  const int tplTop = kAppearChildContentTop;
  const int viewportH = std::max(120, app.height - kContentTop - kSpacingL);
  const int tplH = appearance_templates_card_height_px(viewportH);
  settings_card(app, cr, static_cast<double>(colX), static_cast<double>(tplTop),
                static_cast<double>(colW), static_cast<double>(tplH), glassOv);
  settings_cat_label(cr, static_cast<double>(colX + kCardPad), static_cast<double>(tplTop) + 22.0, "External matugen templates");
  settings_show_text(cr, static_cast<double>(colX + kCardPad), static_cast<double>(tplTop) + 32,
                     "App outputs from wallpaper", 11.f, 400, textR, textG, textB, 0.46f);

  if (!eh::matugen::matugen_external_bundle_available()) {
    settings_show_text(cr, static_cast<double>(colX + kCardPad), tplTop + 52 + 22, "No bundle found \u2014 install matugen configs or set EH_MATUGEN_ROOT.", 11.f, 400, textR, textG, textB, 1.0f);
  }

  // M3-style row: "Use base.toml recipes"
  {
    const int bandTop = tplTop + 52;
    const int titleY = bandTop + 36;

    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, static_cast<double>(colX) + kCardPad + 8, bandTop);
    cairo_line_to(cr, static_cast<double>(colX + colW) - kCardPad - 8, bandTop);
    cairo_stroke(cr);

    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const float tgX = static_cast<float>(colX + colW - kCardPad - tgW);
    const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
    m3::Toggle tplMasterTg;
    tplMasterTg.setSize(m3::Toggle::Size::L);
    tplMasterTg.setGeometry(tgX, tgY, tgW, tgH);
    tplMasterTg.setOn(app.settings.matugenOutputs.runBundledToml);
    tplMasterTg.setEnabled(true);
    tplMasterTg.setAccentColor(accR, accG, accB);

    settings_show_text(cr, static_cast<double>(colX + kCardPad), titleY, "Use base.toml recipes", 14.f, 500,
                       textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(colX + kCardPad), titleY + 17, "Apply bundled template recipes to apps.", 11.f, 400,
                       textR, textG, textB, 0.46f);
    tplMasterTg.paint(cr, 0);
  }

  // Vertical divider between left and right columns
  {
    const int gap = 10;
    const int innerW = colW - 2 * kCardPad;
    const int cellW = (innerW - gap) / kMatugenTplCols;
    const int dividerX = colX + kCardPad + cellW + gap / 2;
    const int rowsTop = tplTop + 52 + kDockVisRowPitch;
    const int rowsBot = rowsTop + kMatugenTplRows * kDockVisRowPitch;
    cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, dividerX, rowsTop);
    cairo_line_to(cr, dividerX, rowsBot);
    cairo_stroke(cr);
  }

  // 30 template toggles in 2-column grid with M3 styling
  for (int i = 0; i < kMatugenTplToggleCount; ++i) {
    int rx, ry, rw, rh;
    matugen_tpl_toggle_geom(colX, colW, tplTop, i, &rx, &ry, &rw, &rh);
    const int bandTop = ry;
    const int titleY = bandTop + 36;

    const int row = i / kMatugenTplCols;
    if (row > 0 && i % kMatugenTplCols == 0) {
      cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, static_cast<double>(colX) + kCardPad + 8, bandTop);
      cairo_line_to(cr, static_cast<double>(colX + colW) - kCardPad - 8, bandTop);
      cairo_stroke(cr);
    }

    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const float tgX = static_cast<float>(rx + rw - kCardPad - tgW);
    const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
    m3::Toggle tg;
    tg.setSize(m3::Toggle::Size::L);
    tg.setGeometry(tgX, tgY, tgW, tgH);
    tg.setOn(matugen_tpl_get(app.settings, i));
    tg.setEnabled(true);
    tg.setAccentColor(accR, accG, accB);

    settings_show_text(cr, static_cast<double>(rx + kCardPad), static_cast<double>(titleY),
                       matugen_tpl_title(i), 14.f, 500,
                       textR, textG, textB, 0.93f);
    tg.paint(cr, 0);
  }
}

// Paint function.
void paint_appearance_tab(App& app, cairo_t* cr, int contentX, int contentW,
                          double cardX, double cardW, double glassOv,
                          double dockMatA, double paintPointerYOffset) {
  (void)dockMatA;

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

  paint_appearance_child_tab_bar(cr, contentX, contentW, textR, textG, textB, app.appearanceChildTab);

  if (app.appearanceChildTab == kAppearanceTemplates) {
    paint_appearance_templates_tab(app, cr, contentX, contentW, cardX, cardW, glassOv);
  } else {
    paint_appearance_general_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, paintPointerYOffset);
  }
}

// General child tab pointer-down.
static bool appearance_general_pointer_down(App& app, int contentX, int contentW) {
  const double lyA = app.pointerY + settings_scroll_px(app);

  // Dynamic-color toggle (row 0)
  {
    const int bandTop = kAppearChildContentTop + 52;
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const int tgX = contentX + 8 + (contentW - 16) - kCardPad - static_cast<int>(tgW);
    const int tgY = bandTop + (kDockVisRowPitch - static_cast<int>(tgH)) / 2;
    if (point_in_rect(app.pointerX, lyA, tgX - 6, tgY - 4, static_cast<int>(tgW) + 12, static_cast<int>(tgH) + 8)) {
      app.settings.matugenThemingEnabled = !app.settings.matugenThemingEnabled;
      if (app.settings.matugenThemingEnabled) app.settings.horizonColorsNative = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Native color engine toggle (row 1)
  {
    const int bandTop = kAppearChildContentTop + 52 + 1 * kDockVisRowPitch;
    constexpr float tgH = 24.0f;
    constexpr float tgW = 40.0f;
    const int tgX = contentX + 8 + (contentW - 16) - kCardPad - static_cast<int>(tgW);
    const int tgY = bandTop + (kDockVisRowPitch - static_cast<int>(tgH)) / 2;
    if (point_in_rect(app.pointerX, lyA, tgX - 6, tgY - 4, static_cast<int>(tgW) + 12, static_cast<int>(tgH) + 8)) {
      app.settings.horizonColorsNative = !app.settings.horizonColorsNative;
      if (app.settings.horizonColorsNative) app.settings.matugenThemingEnabled = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Dynamic-color scheme combo
  {
    matugen_scheme_dd_sync(app, contentX, contentW);
    if (app.matugenSchemeDd.hit_trigger(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY),
                                        settings_scroll_px_int(app))) {
      settings_close_mode_dropdowns(app);
      app.matugenSchemeDd.open_popup();
      draw(app);
      return true;
    }
  }
  // Dynamic-color mode combo
  {
    matugen_mode_dd_sync(app, contentX, contentW);
    if (app.matugenModeDd.hit_trigger(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY),
                                      settings_scroll_px_int(app))) {
      settings_close_mode_dropdowns(app);
      app.matugenModeDd.open_popup();
      draw(app);
      return true;
    }
  }

  // Overlay grid items
  const int maxIdx = app.settings.overlayOpacityAdvanced ? 15 : 1;
  for (int idx = 0; idx <= maxIdx; ++idx) {
    int trX, trY, trW, cardX2, cardY, cardW2;
    appearance_app_geom(contentX, contentW, idx, trX, trY, trW, cardX2, cardY, cardW2, kAppearChildContentTop);
    if (idx == 1) {
      constexpr int swW = 52, swH = 26;
      const int swX = cardX2 + cardW2 - swW - kSpacingXL;
      const int swY = cardY - 24 + (92 - swH) / 2;
      if (point_in_rect(app.pointerX, lyA, swX, swY, swW, swH)) {
        app.settings.overlayOpacityAdvanced = !app.settings.overlayOpacityAdvanced;
        save_settings(app.settings);
        draw(app);
        return true;
      }
    } else {
      if (point_in_rect(app.pointerX, lyA, trX - 6, trY, trW + 12, 28)) {
        const int which = (idx == 0) ? 0 : (idx - 1);
        app.appearanceOverlaySliderDrag = which;
        apply_appearance_overlay_slider(app, which, app.pointerX);
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
        draw(app);
        return true;
      }
    }
  }

  // Color adjustment sliders
  {
    const int overlayBottom = kAppearChildContentTop + kTopCardH + kCardGap + kSpacingXL
                              + (maxIdx / 2) * (92 + 12) + 92;
    const int colorSectionTop = overlayBottom + kSpacingXL;
    constexpr int kColorGap = 12;
    const int colW = (contentW * 68) / 100;
    const int colX = contentX + (contentW - colW) / 2;

    for (int i = 0; i < 4; ++i) {
      const int col = i % 2;
      const int row = i / 2;
      const int cw = std::max(160, (colW - kColorGap) / 2);
      const int cx = colX + col * (cw + kColorGap);
      const int cy = colorSectionTop + 52 + row * (92 + kColorGap);
      const int trX = cx + 20;
      const int trW = cw - 40;
      const int trY = cy + 50;
      if (point_in_rect(app.pointerX, lyA, trX - 6, trY, trW + 12, 28)) {
        app.appearanceColorSliderDrag = i;
        apply_appearance_color_slider(app, i, app.pointerX);
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
        draw(app);
        return true;
      }
    }
  }
  return false;
}

// Templates child tab pointer-down.
static bool appearance_templates_pointer_down(App& app, int contentX, int contentW) {
  const double lyA = app.pointerY + settings_scroll_px(app);
  return appearance_pointer_matugen_templates(app, contentX, contentW, app.pointerX, lyA, kAppearChildContentTop);
}

// Pointer-down handler.
bool settings_appearance_consume_pointer_down(App& app, int contentX, int contentW) {
  // Child tab switch
  const double lyA = app.pointerY + settings_scroll_px(app);
  const int tabHit = appearance_hit_child_tab(static_cast<float>(app.pointerX), static_cast<float>(lyA), contentX);
  if (tabHit >= 0 && tabHit != app.appearanceChildTab) {
    settings_close_mode_dropdowns(app);
    app.appearanceChildTab = tabHit;
    draw(app);
    return true;
  }

  if (app.appearanceChildTab == kAppearanceTemplates) {
    return appearance_templates_pointer_down(app, contentX, contentW);
  }
  return appearance_general_pointer_down(app, contentX, contentW);
}

bool appearance_is_templates_child_tab(const App& app) {
  return app.appearanceChildTab == kAppearanceTemplates;
}
