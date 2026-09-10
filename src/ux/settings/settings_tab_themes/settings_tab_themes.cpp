#include <cairo/cairo.h>
#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_themes/settings_tab_themes.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

void mango_commit_cfg(App& app);

// Cursor apply/cancel backup.
static std::string s_backupCursorTheme;
static int s_backupCursorSize = 24;
static bool s_backupInited = false;

static double s_applyBtnX, s_applyBtnY, s_applyBtnW, s_applyBtnH;
static double s_cancelBtnX, s_cancelBtnY, s_cancelBtnW, s_cancelBtnH;
static bool s_applyHover = false, s_cancelHover = false;

static void cursor_backup_init(const App& app) {
   
  if (!s_backupInited) {
    s_backupCursorTheme = app.mangoConfig.cursor_theme;
    s_backupCursorSize = app.mangoConfig.cursor_size;
    s_backupInited = true;
  }
}

static void cursor_backup_reset() {
  s_backupInited = false;
}

// Theme apply/cancel backup.
static std::string s_backupGtkTheme;
static std::string s_pendingGtkTheme;
static bool s_gtkBackupInited = false;

static void gtk_backup_init() {
  if (!s_gtkBackupInited) {
    s_backupGtkTheme = eh::theming::detect_system_gtk_theme();
    s_pendingGtkTheme = s_backupGtkTheme;
    s_gtkBackupInited = true;
  }
}

// Toolkit apply/cancel backup.
static std::string s_backupQtStyle;
static std::string s_pendingQtStyle;
static std::string s_backupQtColorScheme;
static std::string s_pendingQtColorScheme;
static bool s_qtBackupInited = false;

static void qt_backup_init() {
  if (!s_qtBackupInited) {
    s_backupQtStyle = eh::theming::detect_system_qt_style();
    s_pendingQtStyle = s_backupQtStyle;
    s_backupQtColorScheme = eh::theming::detect_current_qt_color_scheme();
    s_pendingQtColorScheme = s_backupQtColorScheme;
    s_qtBackupInited = true;
  }
}

// Desktop-environment apply/cancel backup.
static std::string s_backupPlasmaTheme;
static std::string s_pendingPlasmaTheme;
static bool s_plasmaBackupInited = false;

static void plasma_backup_init(const App& app) {
  if (!s_plasmaBackupInited) {
    s_backupPlasmaTheme = app.settings.plasmaTheme;
    s_pendingPlasmaTheme = s_backupPlasmaTheme;
    s_plasmaBackupInited = true;
  }
}

void themes_backup_reset() {
  s_gtkBackupInited = false;
  s_qtBackupInited = false;
  s_plasmaBackupInited = false;
  cursor_backup_reset();
}

void set_pending_qt_color_scheme(const std::string& scheme) {
  s_pendingQtColorScheme = scheme;
}

const std::string& get_pending_qt_color_scheme() {
  return s_pendingQtColorScheme;
}

void apply_cursor_config(const App& app) {
   
  const auto& cfg = app.mangoConfig;
  if (cfg.cursor_theme.empty()) return;

  // freedesktop settings DBus API for toolkit apps / compatible compositors
  {
    std::string cmd = "gsettings set org.gnome.desktop.interface cursor-theme '" + cfg.cursor_theme + "' 2>/dev/null; "
                      "gsettings set org.gnome.desktop.interface cursor-size " + std::to_string(cfg.cursor_size) + " 2>/dev/null";
    (void)std::system(cmd.c_str());
  }

  switch (detect_compositor_kind()) {
    case CompositorKind::Hyprland: {
      std::string cmd = "hyprctl setcursor '" + cfg.cursor_theme + "' " + std::to_string(cfg.cursor_size) + " 2>/dev/null";
      (void)std::system(cmd.c_str());
      break;
    }
    case CompositorKind::Sway: {
      std::string cmd = "swaymsg seat seat0 xcursor_theme '" + cfg.cursor_theme + "' " + std::to_string(cfg.cursor_size) + " 2>/dev/null";
      (void)std::system(cmd.c_str());
      break;
    }
    case CompositorKind::Niri:
    case CompositorKind::Labwc:
    case CompositorKind::Triad:
    case CompositorKind::Mango:
    default:
      break;
  }
}

static double s_subTabXs[4] = {};
static double s_subTabYs[4] = {};
static double s_subTabWs[4] = {};
static constexpr int kNTabSubs = 4;
static constexpr int kTabBarGap = 4;
static constexpr int kCardPadX = 20;
static constexpr int kThemeCardW = 480;
static constexpr int kThemeCardH = 230;
static constexpr int kThemeCardGap = 8;
static constexpr int kThemeCardRad = 10;
static constexpr int kSubTabLabelY = 38;

static constexpr int kPreviewW = 400;
static constexpr int kPreviewH = 176;
static constexpr int kPreviewTitleH = 32;

static void qt_color_scheme_combo_geom(int contentX, int contentW, int gridBottom,
                                       int& cbx, int& cby, int& cbw, int& cbh) {
   
  cbx = contentX + kCardPadX + 140;
  cbw = contentW - kCardPadX - 140 - 28;
  if (cbw < 100) cbw = 100;
  cby = gridBottom + 12;
  cbh = kQtColorSchemeComboH;
}

static int themes_content_bottom(const App& app, int contentX, int contentW) {
   
  (void)contentX;
  const int subTab = app.themesSubTab;
  int count = 0;
  if (subTab == 0) {
    count = static_cast<int>(eh::theming::list_installed_gtk_themes().size());
  } else if (subTab == 1) {
    count = static_cast<int>(eh::theming::known_qt_styles().size());
  } else if (subTab == 2) {
    count = static_cast<int>(eh::theming::list_installed_cursor_themes().size());
  } else {
    count = static_cast<int>(eh::theming::list_installed_plasma_themes().size());
  }
  const int cols = std::max(1, (contentW - 32) / kThemeCardW);
  if (count == 0) return kContentTop + kTabBarH + kSpacingL + 60 + 60;
  const int rows = (count + cols - 1) / cols;
  int bottom = kContentTop + kTabBarH + kSpacingL + 60 + rows * (kThemeCardH + kThemeCardGap);
  if (subTab == 1) {
    const int nSchemes = static_cast<int>(eh::theming::list_qt_color_schemes().size());
    bottom += 12 + kQtColorSchemeComboH;
    if (app.qtColorSchemeDropdownOpen) {
      bottom += nSchemes * kSettingsDdRowH + 4;
    }
  }
  if (subTab == 2) {
    bottom += 12 + kSliderRowH + 8 + 40 + kSpacingL;
  }
  if (subTab == 0 || subTab == 3) {
    bottom += 12 + 8 + 40 + kSpacingL;
  }
  if (subTab == 1) {
    bottom += 8 + 40 + kSpacingL;
  }
  return bottom + kSpacingL;
}

int themes_tab_content_bottom_px(const App& app) {
  return themes_content_bottom(app, 16 + 240 + 16, app.width - 16 - 240 - 16);
}

int settings_themes_scroll_max_px(const App& app) {
   
  const int viewportH = std::max(120, app.height - kContentTop - kSpacingL);
  const int bottom = themes_tab_content_bottom_px(app);
  return std::max(0, bottom - kContentTop - viewportH);
}

void settings_clamp_themes_scroll_px(App& app) {
   
  const int mx = settings_themes_scroll_max_px(app);
  app.settingsThemesScrollPx = std::clamp(app.settingsThemesScrollPx, 0, mx);
}

static void draw_mini_preview(App& app, cairo_t* cr, double cx, double cy,
                              const std::string& themeDir, bool isGtk) {
   
  (void)app;
  const double pX = cx + (kThemeCardW - kPreviewW) * 0.5;
  const double pY = cy + 8.0;
  const double pW = kPreviewW;
  const double pH = kPreviewH;

  eh::theming::GtkThemeColors tc;
  eh::theming::GtkThemeDesign td;
  if (isGtk && !themeDir.empty()) {
    tc = eh::theming::extract_gtk_theme_colors(themeDir);
    td = eh::theming::extract_gtk_theme_design(themeDir);
  } else if (!themeDir.empty()) {
    tc = eh::theming::extract_plasma_theme_colors(themeDir);
  }

  // Shadow
  {
    m3::Box box;
    box.setColor(0, 0, 0, 0.08f);
    box.setRadius(4.0f);
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY), static_cast<float>(pW), static_cast<float>(pH));
    box.setGlassy(true);
    box.paint(cr);
  }

  // Resolve colors with fallbacks
  const double hBgR = tc.valid ? tc.headerBgR : 0.85;
  const double hBgG = tc.valid ? tc.headerBgG : 0.85;
  const double hBgB = tc.valid ? tc.headerBgB : 0.85;
  const double hFgR = tc.valid ? tc.headerFgR : 0.20;
  const double hFgG = tc.valid ? tc.headerFgG : 0.20;
  const double hFgB = tc.valid ? tc.headerFgB : 0.20;

  const double bgR = tc.valid ? tc.bgR : 0.96;
  const double bgG = tc.valid ? tc.bgG : 0.96;
  const double bgB = tc.valid ? tc.bgB : 0.96;

  const double aR = tc.valid ? tc.accentR : 0.30;
  const double aG = tc.valid ? tc.accentG : 0.50;
  const double aB = tc.valid ? tc.accentB : 0.85;

  const double fgR = tc.valid ? tc.fgR : 0.15;
  const double fgG = tc.valid ? tc.fgG : 0.15;
  const double fgB = tc.valid ? tc.fgB : 0.15;

  const double txR = tc.valid ? tc.textR : 0.40;
  const double txG = tc.valid ? tc.textG : 0.40;
  const double txB = tc.valid ? tc.textB : 0.40;

  const double inR = tc.valid ? tc.baseR : 1.0;
  const double inG = tc.valid ? tc.baseG : 1.0;
  const double inB = tc.valid ? tc.baseB : 1.0;

  const double entRad = td.valid ? td.entryRad : 2.0;
  const int borderW = td.valid ? td.borderW : 1;

  // Title bar.
  const double tH = 32.0;
  cairo_save(cr);
  cairo_round_rect(cr, pX, pY, pW, pH, 4.0);
  cairo_clip(cr);

  cairo_set_source_rgba(cr, hBgR, hBgG, hBgB, 1.0);
  cairo_rectangle(cr, pX, pY, pW, tH);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
  cairo_rectangle(cr, pX, pY + tH - 1, pW, 1);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, bgR, bgG, bgB, 1.0);
  cairo_rectangle(cr, pX, pY + tH, pW, pH - tH);
  cairo_fill(cr);

  cairo_restore(cr);

  // Title dots
  auto drawDot = [&](double bx, double by, double cr2, double cg, double cb) {
    cairo_set_source_rgba(cr, cr2, cg, cb, 0.8);
    cairo_arc(cr, bx, by, 4.0, 0, 2 * M_PI);
    cairo_fill(cr);
  };
  const double dotY = pY + tH * 0.5;
  drawDot(pX + pW - 26, dotY, 0.9, 0.3, 0.3);
  drawDot(pX + pW - 44, dotY, 0.85, 0.75, 0.2);
  drawDot(pX + pW - 62, dotY, 0.35, 0.8, 0.35);

  // Title text
  settings_show_text(cr, pX + 12, dotY + 6, "Application", 12, 600,
                     static_cast<float>(hFgR), static_cast<float>(hFgG), static_cast<float>(hFgB), 0.85f);

  // Content layout.
  const double padX = 18.0;
  const double colW = pW - padX * 2;
  double yPos = pY + tH + 12;

  // Entry
  const double entryH = 26.0;
  cairo_round_rect(cr, pX + padX, yPos, colW, entryH, entRad);
  cairo_set_source_rgba(cr, inR, inG, inB, 1.0);
  cairo_fill(cr);
  cairo_round_rect(cr, pX + padX, yPos, colW, entryH, entRad);
  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
  cairo_set_line_width(cr, std::max(0.5, borderW * 0.5));
  cairo_stroke(cr);
  settings_show_text(cr, pX + padX + 8, yPos + 18, "Search or type...", 11, 400,
                     static_cast<float>(txR), static_cast<float>(txG), static_cast<float>(txB), 0.5f);
  yPos += entryH + 12;

  // Checkbox row
  const double chH = 22.0;
  const double chSize = 16.0;
  cairo_round_rect(cr, pX + padX, yPos + (chH - chSize) * 0.5, chSize, chSize, 3);
  cairo_set_source_rgba(cr, aR, aG, aB, 0.7);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  cairo_set_line_width(cr, 2.0);
  cairo_move_to(cr, pX + padX + 4, yPos + chH * 0.5);
  cairo_line_to(cr, pX + padX + 7, yPos + chH * 0.5 + 4);
  cairo_line_to(cr, pX + padX + 12, yPos + chH * 0.5 - 3);
  cairo_stroke(cr);
  settings_show_text(cr, pX + padX + chSize + 12, yPos + 16, "Remember me", 11, 400,
                     static_cast<float>(fgR), static_cast<float>(fgG), static_cast<float>(fgB), 0.65f);
  yPos += chH + 12;

  // Filled button
  const double btnH = 28.0;
  const double btnRad = 4.0;
  cairo_round_rect(cr, pX + padX, yPos, colW, btnH, btnRad);
  cairo_set_source_rgba(cr, aR, aG, aB, 0.9);
  cairo_fill(cr);
  settings_show_text(cr, pX + padX + colW * 0.5 - 24, yPos + 19, "Continue", 12, 600,
                     1.0f, 1.0f, 1.0f, 0.95f);
  yPos += btnH + 12;

  // Divider
  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.12);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, pX + padX, yPos + 2);
  cairo_line_to(cr, pX + padX + colW, yPos + 2);
  cairo_stroke(cr);
  yPos += 10;

  // Status text
  settings_show_text(cr, pX + padX, yPos, "Ready", 9, 400,
                     static_cast<float>(txR), static_cast<float>(txG), static_cast<float>(txB), 0.35f);
}

static void draw_qt_preview(cairo_t* cr, double cx, double cy,
                            const std::string& styleName,
                            const eh::theming::GtkThemeColors& qtColors) {
   
  const double pX = cx + (kThemeCardW - kPreviewW) * 0.5;
  const double pY = cy + 8.0;
  const double pW = kPreviewW;
  const double pH = kPreviewH;

  // Style-type detection
  enum class QtStyleType { Flat, Rounded, Windows, Breeze, Kvantum, Material };
  QtStyleType style = QtStyleType::Flat;
  std::string sname;
  for (char c : styleName) sname.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (sname.find("breeze") != std::string::npos) style = QtStyleType::Breeze;
  else if (sname.find("kvantum") != std::string::npos || sname.find("kvantom") != std::string::npos) style = QtStyleType::Kvantum;
  else if (sname.find("windows") != std::string::npos) style = QtStyleType::Windows;
  else if (sname.find("material") != std::string::npos) style = QtStyleType::Material;
  else if (sname.find("fusion") != std::string::npos) style = QtStyleType::Flat;
  else if (sname.find("oxygen") != std::string::npos) style = QtStyleType::Rounded;
  else if (sname.find("fluent") != std::string::npos) style = QtStyleType::Material;
  else if (sname.find("gtk") != std::string::npos) style = QtStyleType::Rounded;

  double entRadQt = 2.0;
  double borderW = 1.0;

  switch (style) {
    case QtStyleType::Breeze:   entRadQt = 3; borderW = 1.5; break;
    case QtStyleType::Kvantum:  entRadQt = 4; borderW = 2.0; break;
    case QtStyleType::Windows:  entRadQt = 0; borderW = 2.0; break;
    case QtStyleType::Material: entRadQt = 0; borderW = 0; break;
    case QtStyleType::Rounded:  entRadQt = 4; borderW = 1.0; break;
    default:          entRadQt = 2; borderW = 1.0; break;
  }

  // Shadow
  {
    m3::Box box;
    box.setColor(0, 0, 0, 0.08f);
    box.setRadius(4.0f);
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY), static_cast<float>(pW), static_cast<float>(pH));
    box.setGlassy(true);
    box.paint(cr);
  }

  // Title bar.
  const double tH = 32.0;
  cairo_save(cr);
  cairo_round_rect(cr, pX, pY, pW, pH, 4.0);
  cairo_clip(cr);

  cairo_set_source_rgba(cr, qtColors.headerBgR, qtColors.headerBgG, qtColors.headerBgB, 1.0);
  cairo_rectangle(cr, pX, pY, pW, tH);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
  cairo_rectangle(cr, pX, pY + tH - 1, pW, 1);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, qtColors.bgR, qtColors.bgG, qtColors.bgB, 1.0);
  cairo_rectangle(cr, pX, pY + tH, pW, pH - tH);
  cairo_fill(cr);

  cairo_restore(cr);

  // Title dots
  auto drawDot = [&](double bx, double by, double cr2, double cg, double cb) {
    cairo_set_source_rgba(cr, cr2, cg, cb, 0.8);
    cairo_arc(cr, bx, by, 4.0, 0, 2 * M_PI);
    cairo_fill(cr);
  };
  const double dotY = pY + tH * 0.5;
  drawDot(pX + pW - 26, dotY, 0.9, 0.3, 0.3);
  drawDot(pX + pW - 44, dotY, 0.85, 0.75, 0.2);
  drawDot(pX + pW - 62, dotY, 0.35, 0.8, 0.35);

  // Title text
  settings_show_text(cr, pX + 12, dotY + 6, "Application", 12, 600,
                     static_cast<float>(qtColors.headerFgR), static_cast<float>(qtColors.headerFgG), static_cast<float>(qtColors.headerFgB), 0.85f);

  // Content layout.
  const double padX = 18.0;
  const double colW = pW - padX * 2;
  double yPos = pY + tH + 12;

  // Entry
  const double entryH = 26.0;
  cairo_round_rect(cr, pX + padX, yPos, colW, entryH, entRadQt);
  cairo_set_source_rgba(cr, qtColors.baseR, qtColors.baseG, qtColors.baseB, 1.0);
  cairo_fill(cr);
  cairo_round_rect(cr, pX + padX, yPos, colW, entryH, entRadQt);
  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.3);
  cairo_set_line_width(cr, std::max(0.5, borderW * 0.5));
  cairo_stroke(cr);
  settings_show_text(cr, pX + padX + 8, yPos + 18, "Search or type...", 11, 400,
                     static_cast<float>(qtColors.textR), static_cast<float>(qtColors.textG), static_cast<float>(qtColors.textB), 0.5f);
  yPos += entryH + 12;

  // Checkbox row
  const double chH = 22.0;
  const double chSize = 16.0;
  cairo_round_rect(cr, pX + padX, yPos + (chH - chSize) * 0.5, chSize, chSize, 3);
  cairo_set_source_rgba(cr, qtColors.accentR, qtColors.accentG, qtColors.accentB, 0.7);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  cairo_set_line_width(cr, 2.0);
  cairo_move_to(cr, pX + padX + 4, yPos + chH * 0.5);
  cairo_line_to(cr, pX + padX + 7, yPos + chH * 0.5 + 4);
  cairo_line_to(cr, pX + padX + 12, yPos + chH * 0.5 - 3);
  cairo_stroke(cr);
  settings_show_text(cr, pX + padX + chSize + 12, yPos + 16, "Remember me", 11, 400,
                     static_cast<float>(qtColors.fgR), static_cast<float>(qtColors.fgG), static_cast<float>(qtColors.fgB), 0.65f);
  yPos += chH + 12;

  // Filled button
  const double btnH = 28.0;
  const double btnRad = 4.0;
  cairo_round_rect(cr, pX + padX, yPos, colW, btnH, btnRad);
  cairo_set_source_rgba(cr, qtColors.accentR, qtColors.accentG, qtColors.accentB, 0.9);
  cairo_fill(cr);
  settings_show_text(cr, pX + padX + colW * 0.5 - 24, yPos + 19, "Continue", 12, 600,
                     1.0f, 1.0f, 1.0f, 0.95f);
  yPos += btnH + 12;

  // Divider
  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.12);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, pX + padX, yPos + 2);
  cairo_line_to(cr, pX + padX + colW, yPos + 2);
  cairo_stroke(cr);
  yPos += 10;

  // Status text
  settings_show_text(cr, pX + padX, yPos, "Ready", 9, 400,
                     static_cast<float>(qtColors.textR), static_cast<float>(qtColors.textG), static_cast<float>(qtColors.textB), 0.35f);
}

struct CursorPreviewItem {
  std::string name;
  cairo_surface_t* surface;
};
static std::unordered_map<std::string, std::vector<CursorPreviewItem>> s_cursorPreviewCache;
static constexpr std::size_t kMaxCursorPreviewEntries = 64;

static void cursor_preview_cache_trim() {
   
  if (s_cursorPreviewCache.size() <= kMaxCursorPreviewEntries) return;
  auto oldest = s_cursorPreviewCache.begin();
  auto it = s_cursorPreviewCache.begin();
  ++it;
  for (; it != s_cursorPreviewCache.end(); ++it) {
    if (it->first < oldest->first) oldest = it;
  }
  for (auto& item : oldest->second)
    if (item.surface) cairo_surface_destroy(item.surface);
  s_cursorPreviewCache.erase(oldest);
}

static void draw_cursor_preview(cairo_t* cr, double cx, double cy,
                                const std::string& cursorThemePath) {
   
  const double pX = cx + (kThemeCardW - kPreviewW) * 0.5;
  const double pY = cy + 8.0;

  {
    m3::Box box;
    box.setColor(0, 0, 0, 0.08f);
    box.setRadius(4.0f);
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY), static_cast<float>(kPreviewW), static_cast<float>(kPreviewH));
    box.setGlassy(true);
    box.paint(cr);
  }

  {
    m3::Box box;
    box.setColor(0.96f, 0.96f, 0.96f, 1.0f);
    box.setRadius(4.0f);
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY), static_cast<float>(kPreviewW), static_cast<float>(kPreviewH));
    box.setGlassy(true);
    box.paint(cr);
  }

  // Load multi-cursor grid if not cached
  if (!cursorThemePath.empty()) {
    auto it = s_cursorPreviewCache.find(cursorThemePath);
    if (it == s_cursorPreviewCache.end()) {
      static const char* kShapeNames[] = {"left_ptr", "hand2", "xterm", "crosshair", "watch", "fleur"};
      std::vector<CursorPreviewItem> items;
      for (const char* sn : kShapeNames) {
        cairo_surface_t* s = eh::theming::load_cursor_shape_surface(cursorThemePath, sn, 32);
        if (s) items.push_back({sn, s});
      }
      if (!items.empty()) {
        s_cursorPreviewCache[cursorThemePath] = std::move(items);
        cursor_preview_cache_trim();
      }
    }
  }

  auto cacheIt = s_cursorPreviewCache.find(cursorThemePath);
  const bool hasCursors = (cacheIt != s_cursorPreviewCache.end() && !cacheIt->second.empty());

  if (hasCursors) {
    const auto& items = cacheIt->second;
    const int nCols = 3;
    const int nRows = 2;
    const double cellW = kPreviewW / static_cast<double>(nCols);
    const double cellH = kPreviewH / static_cast<double>(nRows);
    const double maxCursorPx = 56.0;
    const double labelH = 14.0;

    for (std::size_t i = 0; i < items.size() && static_cast<int>(i) < nCols * nRows; ++i) {
      const int col = static_cast<int>(i % nCols);
      const int row = static_cast<int>(i / nCols);
      const double cellX = pX + col * cellW;
      const double cellY = pY + row * cellH;

      cairo_surface_t* surf = items[i].surface;
      int iw = cairo_image_surface_get_width(surf);
      int ih = cairo_image_surface_get_height(surf);
      double scale = std::min(maxCursorPx / static_cast<double>(iw),
                              (cellH - labelH - 4.0) / static_cast<double>(ih));
      if (scale > 3.0) scale = 3.0;
      if (scale < 0.3) scale = 0.3;
      double dw = iw * scale;
      double dh = ih * scale;
      double dx = cellX + (cellW - dw) * 0.5;
      double dy = cellY + (cellH - labelH - dh) * 0.5;

      cairo_save(cr);
      cairo_translate(cr, dx, dy);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, surf, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);

      cairo_text_extents_t te;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 7);
      cairo_text_extents(cr, items[i].name.c_str(), &te);
      settings_show_text(cr, cellX + (cellW - te.x_advance) * 0.5, cellY + cellH - 3, items[i].name.c_str(), 7, 400,
                         0.5f, 0.5f, 0.5f, 0.6f);
    }
  } else {
    settings_show_text(cr, pX + 10, pY + kPreviewH * 0.5 + 3, "No preview", 9, 400, 0.6f, 0.6f, 0.6f, 0.5f);
  }
}

void paint_themes_tab(App& app, cairo_t* cr, int contentX, int contentW,
                      double cardX, double cardW, double glassOv,
                      double dockMatA, double paintPointerYOffset) {
   
  (void)glassOv;
  (void)dockMatA;
  (void)paintPointerYOffset;

  auto gtkThemes = eh::theming::list_installed_gtk_themes();
  const auto qtStyles = eh::theming::known_qt_styles();
  auto cursorThemes = eh::theming::list_installed_cursor_themes();
  auto plasmaThemes = eh::theming::list_installed_plasma_themes();

  const int cols = std::max(1, (contentW - 32) / kThemeCardW);
  const int subTab = app.themesSubTab;
  int itemCount = 0;
  if (subTab == 0) itemCount = static_cast<int>(gtkThemes.size());
  else if (subTab == 1) itemCount = static_cast<int>(qtStyles.size());
  else if (subTab == 2) itemCount = static_cast<int>(cursorThemes.size());
  else itemCount = static_cast<int>(plasmaThemes.size());

  const int rows = itemCount > 0 ? (itemCount + cols - 1) / cols : 0;
  const int contentBottom = itemCount > 0
    ? (kContentTop + kTabBarH + kSpacingL + 60 + rows * (kThemeCardH + kThemeCardGap) + kSpacingL)
    : (kContentTop + kTabBarH + kSpacingL + 60 + 60);
  const double cardTop = static_cast<double>(kContentTop);
  double extraH = 0;
  if (subTab == 1) extraH = 12 + kQtColorSchemeComboH;
  else if (subTab == 2) extraH = 12 + kSliderRowH + kSpacingL;
  const double cardH = std::max(200.0,
    std::max(static_cast<double>(contentBottom) - cardTop + kSpacingL + extraH,
             static_cast<double>(app.height) - cardTop - static_cast<double>(kSpacingL)));

  settings_card(app, cr, cardX, cardTop, cardW, cardH, 1.0);
  settings_cat_label(cr, cardX + kCardPad, cardTop + 22.0, "Themes");

  // sub-tab bar
  static const char* kSubTabs[] = {"GTK", "QT", "Cursor", "Plasma"};
  double tabX = static_cast<double>(contentX + kCardPadX);
  const double tabY = cardTop + kSubTabLabelY;
  for (int t = 0; t < kNTabSubs; ++t) {
    auto* pangoLayout = pango_cairo_create_layout(cr);
    auto* pangoDesc = pango_font_description_new();
    pango_font_description_set_family(pangoDesc, "Inter");
    pango_font_description_set_size(pangoDesc, 13 * PANGO_SCALE);
    pango_font_description_set_weight(pangoDesc,
      static_cast<PangoWeight>(t == subTab ? 700 : 400));
    pango_layout_set_font_description(pangoLayout, pangoDesc);
    pango_layout_set_text(pangoLayout, kSubTabs[t], -1);
    int pw, ph;
    pango_layout_get_pixel_size(pangoLayout, &pw, &ph);
    const double tw = static_cast<double>(pw) + 24.0;
    s_subTabXs[t] = tabX;
    s_subTabYs[t] = tabY;
    s_subTabWs[t] = tw;
    {
      float r, g, b;
      float a = (t == subTab) ? 0.35f : 0.15f;
      if (t == subTab) {
        if (app.drawChromeMatugen) {
          r = app.drawChrome.accentR; g = app.drawChrome.accentG; b = app.drawChrome.accentB;
        } else {
          r = static_cast<float>(Theme::AccR); g = static_cast<float>(Theme::AccG); b = static_cast<float>(Theme::AccB);
        }
      } else {
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR; g = app.drawChrome.panelFillG; b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR); g = static_cast<float>(Theme::BgG); b = static_cast<float>(Theme::BgB);
        }
      }
      m3::Box box;
      box.setColor(r, g, b, a);
      box.setRadius(6.0f);
      box.setGeometry(static_cast<float>(tabX), static_cast<float>(tabY), static_cast<float>(tw), static_cast<float>(kTabBarH));
      box.setGlassy(true);
      box.paint(cr);
    }
    const double textX = tabX + (tw - static_cast<double>(pw)) * 0.5;
    const double textY = tabY + kTabBarH * 0.5 + static_cast<double>(ph) * 0.5 - 2.0;
    settings_show_text(cr, textX, textY, kSubTabs[t], 13,
                       t == subTab ? 700 : 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB),
                       t == subTab ? 1.0f : 0.7f);
    pango_font_description_free(pangoDesc);
    g_object_unref(pangoLayout);
    tabX += tw + kTabBarGap;
  }

  if (itemCount == 0) {
    if (subTab == 0) settings_show_text(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kContentTop + kTabBarH + 80), "No GTK themes found.", 13, 400, static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 0.7f);
    else if (subTab == 1) settings_show_text(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kContentTop + kTabBarH + 80), "No QT styles available.", 13, 400, static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 0.7f);
    else if (subTab == 2) settings_show_text(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kContentTop + kTabBarH + 80), "No cursor themes found.", 13, 400, static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 0.7f);
    else settings_show_text(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kContentTop + kTabBarH + 80), "No Plasma themes found.", 13, 400, static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 0.7f);
    if (subTab == 1) {
      int gridBottom = kContentTop + kTabBarH + kSpacingL + 60 + 60;
      int cbx, cby, cbw, cbh;
      qt_color_scheme_combo_geom(contentX, contentW, gridBottom, cbx, cby, cbw, cbh);
      settings_label(cr, static_cast<double>(contentX + kCardPadX), static_cast<double>(cby) + 10.0, "Color scheme", "");
      const char* displayText = s_pendingQtColorScheme.empty() ? "None" : s_pendingQtColorScheme.c_str();
      settings_paint_combo_closed(app, cr, cbx, static_cast<int>(cby - settings_scroll_px_int(app)), cbw, cbh, 1.0,
                                  displayText, app.qtColorSchemeDropdownOpen, settings_scroll_px_int(app));
    }
    return;
  }

  const double gridTop = static_cast<double>(kContentTop) + kTabBarH + kSpacingL + 60;
  const int totalGridW = cols * kThemeCardW + (cols - 1) * kThemeCardGap;
  const double gridLeft = static_cast<double>(contentX) + (static_cast<double>(contentW) - static_cast<double>(totalGridW)) * 0.5;

  double pyPaint = app.pointerY + settings_scroll_px(app);

  for (int i = 0; i < itemCount; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const double cx = gridLeft + static_cast<double>(col * (kThemeCardW + kThemeCardGap));
    const double cy = gridTop + static_cast<double>(row * (kThemeCardH + kThemeCardGap));

    std::string id, name, themeDir;
    eh::theming::GtkThemeColors qtColors;
    if (subTab == 0) {
      const auto& th = gtkThemes[static_cast<size_t>(i)];
      id = th.id;
      name = th.name;
      themeDir = th.path;
    } else if (subTab == 1) {
      id = qtStyles[static_cast<size_t>(i)];
      name = id;
      qtColors = eh::theming::qt_style_preview_colors(id);
      // Override colors with pending color scheme for the current style
      if (id == s_pendingQtStyle && s_pendingQtColorScheme != s_backupQtColorScheme && !s_pendingQtColorScheme.empty()) {
        eh::theming::GtkThemeColors sc = eh::theming::qt_color_scheme_preview_colors(s_pendingQtColorScheme);
        if (sc.valid) {
          qtColors.accentR = sc.accentR; qtColors.accentG = sc.accentG; qtColors.accentB = sc.accentB;
          qtColors.bgR = sc.bgR; qtColors.bgG = sc.bgG; qtColors.bgB = sc.bgB;
          qtColors.fgR = sc.fgR; qtColors.fgG = sc.fgG; qtColors.fgB = sc.fgB;
          qtColors.baseR = sc.baseR; qtColors.baseG = sc.baseG; qtColors.baseB = sc.baseB;
          qtColors.textR = sc.textR; qtColors.textG = sc.textG; qtColors.textB = sc.textB;
          qtColors.headerBgR = sc.headerBgR; qtColors.headerBgG = sc.headerBgG; qtColors.headerBgB = sc.headerBgB;
          qtColors.headerFgR = sc.headerFgR; qtColors.headerFgG = sc.headerFgG; qtColors.headerFgB = sc.headerFgB;
        }
      }
    } else if (subTab == 2) {
      const auto& ct = cursorThemes[static_cast<size_t>(i)];
      id = ct.id;
      name = ct.name;
      themeDir = ct.path;
    } else {
      const auto& pt = plasmaThemes[static_cast<size_t>(i)];
      id = pt.id;
      name = pt.name;
      themeDir = pt.path;
    }

    const bool isCurrent = (subTab == 0) ? (id == s_pendingGtkTheme)
                          : (subTab == 1) ? (id == s_pendingQtStyle)
                          : (subTab == 2) ? (id == app.mangoConfig.cursor_theme)
                          : (id == s_pendingPlasmaTheme);
    const bool hover = (app.pointerX >= cx && app.pointerX < cx + kThemeCardW &&
                        pyPaint >= cy && pyPaint < cy + kThemeCardH);

    {
      float r, g, b;
      float a;
      if (isCurrent) {
        a = hover ? 0.30f : 0.22f;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.accentR; g = app.drawChrome.accentG; b = app.drawChrome.accentB;
        } else {
          r = static_cast<float>(Theme::AccR); g = static_cast<float>(Theme::AccG); b = static_cast<float>(Theme::AccB);
        }
      } else {
        a = hover ? 0.92f : 0.85f;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR; g = app.drawChrome.panelFillG; b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR); g = static_cast<float>(Theme::BgG); b = static_cast<float>(Theme::BgB);
        }
      }
      m3::Box box;
      box.setColor(r, g, b, a);
      box.setRadius(static_cast<float>(kThemeCardRad));
      box.setGeometry(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(kThemeCardW), static_cast<float>(kThemeCardH));
      box.setGlassy(true);
      box.paint(cr);
    }

    cairo_round_rect(cr, cx, cy, kThemeCardW, kThemeCardH, kThemeCardRad);
    if (isCurrent) {
      paint_src_accent(app, cr, 0.60);
      cairo_set_line_width(cr, 2.0);
    } else {
      paint_src_glass_hi(app, cr, 0.08);
      cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    if (isCurrent) {
      cairo_set_source_rgba(cr, 0.35, 0.85, 0.45, 1.0);
      cairo_arc(cr, cx + kThemeCardW - 14.0, cy + 16.0, 5.0, 0, 2 * M_PI);
      cairo_fill(cr);
    }

    // Preview
    if (subTab == 2) {
      draw_cursor_preview(cr, cx, cy, themeDir);
    } else if (subTab == 1 && qtColors.valid) {
      draw_qt_preview(cr, cx, cy, name, qtColors);
    } else if (subTab == 3) {
      draw_mini_preview(app, cr, cx, cy, themeDir, false);
    } else {
      draw_mini_preview(app, cr, cx, cy, themeDir, subTab == 0);
    }

    // theme name
    cairo_text_extents_t te;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           isCurrent ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_text_extents(cr, name.c_str(), &te);
    const double textX = cx + (kThemeCardW - te.x_advance) * 0.5;
    settings_show_text(cr, textX, cy + kPreviewH + 8.0 + 20.0 + 5.0, name.c_str(), 13,
                       isCurrent ? 700 : 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 1.0f);

    if (subTab != 2 && !name.empty() && name != id) {
      cairo_text_extents(cr, id.c_str(), &te);
      const double idX = cx + (kThemeCardW - te.x_advance) * 0.5;
      settings_show_text(cr, idX, cy + kPreviewH + 8.0 + 20.0 + 22.0, id.c_str(), 10, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB), 0.55f);
    }
  }

  // Qt color scheme dropdown (only for QT sub-tab)
  if (subTab == 1) {
    const int gridBottom = static_cast<int>(gridTop) + rows * (kThemeCardH + kThemeCardGap) - kThemeCardGap;
    int cbx, cby, cbw, cbh;
    qt_color_scheme_combo_geom(contentX, contentW, gridBottom, cbx, cby, cbw, cbh);
    settings_label(cr, static_cast<double>(contentX + kCardPadX), static_cast<double>(cby) + 10.0, "Color scheme", "");
    const char* displayText = s_pendingQtColorScheme.empty() ? "None" : s_pendingQtColorScheme.c_str();
    const double scrD = static_cast<double>(settings_scroll_px_int(app));
    settings_paint_combo_closed(app, cr, cbx, static_cast<int>(cby - scrD), cbw, cbh, 1.0,
                                displayText, app.qtColorSchemeDropdownOpen, settings_scroll_px_int(app));
  }

  // Cursor size slider and apply/cancel buttons (only for Cursor sub-tab)
  if (subTab == 2) {
    cursor_backup_init(app);
    const int gridBottom = static_cast<int>(gridTop) + rows * (kThemeCardH + kThemeCardGap) - kThemeCardGap;
    int slY = gridBottom + 12;
    settings_label(cr, static_cast<double>(contentX + kCardPadX), static_cast<double>(slY) + 10.0, "Cursor size", "");
    int trX = contentX + kCardPadX;
    int trW = contentW - kCardPadX - kCardPadX - 52;
    if (trW < 40) trW = 40;
    settings_slider(app, cr, trX, slY + 20, trW, app.mangoConfig.cursor_size, 16, 64, paintPointerYOffset, nullptr, false, (app.themesSliderDrag == 0) ? app.settingsSliderDragNormT : -1.0);

    // Apply/Cancel buttons — bottom-right
    const int btnY = slY + 20 + 28 + 8;
    const int btnW = 110;
    const int btnH = 36;

    s_cancelBtnX = contentX + contentW - kCardPadX - btnW;
    s_cancelBtnY = btnY;
    s_cancelBtnW = btnW;
    s_cancelBtnH = btnH;
    s_applyBtnX = s_cancelBtnX - btnW - 12;
    s_applyBtnY = btnY;
    s_applyBtnW = btnW;
    s_applyBtnH = btnH;

    // Track hover via pointer position (adjusted for scroll)
    const double py = app.pointerY + settings_scroll_px(app);
    s_applyHover = app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + btnW &&
                   py >= s_applyBtnY && py < s_applyBtnY + btnH;
    s_cancelHover = app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + btnW &&
                    py >= s_cancelBtnY && py < s_cancelBtnY + btnH;

    // Check if there are unsaved changes
    const bool hasChanges = (app.mangoConfig.cursor_theme != s_backupCursorTheme) ||
                            (app.mangoConfig.cursor_size != s_backupCursorSize);

    // Apply button (M3 Filled)
    {
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Apply");
      btn.setGeometry(static_cast<float>(s_applyBtnX), static_cast<float>(s_applyBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Filled);
      btn.setSize(m3::Button::Size::S);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setHovered(s_applyHover);
      btn.setEnabled(hasChanges);
      btn.paint(cr);
    }

    // Cancel button (M3 Outlined)
    {
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Cancel");
      btn.setGeometry(static_cast<float>(s_cancelBtnX), static_cast<float>(s_cancelBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::S);
      btn.setAccentColor(t_r, t_g, t_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(s_cancelHover);
      btn.setEnabled(hasChanges);
      btn.paint(cr);
    }
  }

  // Apply/Cancel buttons for the toolkit theme sub-tabs
  if (subTab == 0 || subTab == 1 || subTab == 3) {
    if (subTab == 0) gtk_backup_init();
    else if (subTab == 1) qt_backup_init();
    else if (subTab == 3) plasma_backup_init(app);

    const int gridBottom = static_cast<int>(gridTop) + rows * (kThemeCardH + kThemeCardGap) - kThemeCardGap;
    int btnYBase = gridBottom + 12;
    if (subTab == 1) {
      btnYBase += 12 + kQtColorSchemeComboH;
    }
    const int btnY = btnYBase + 8;
    const int btnW = 110;
    const int btnH = 36;

    s_cancelBtnX = contentX + contentW - kCardPadX - btnW;
    s_cancelBtnY = btnY;
    s_cancelBtnW = btnW;
    s_cancelBtnH = btnH;
    s_applyBtnX = s_cancelBtnX - btnW - 12;
    s_applyBtnY = btnY;
    s_applyBtnW = btnW;
    s_applyBtnH = btnH;

    const double py = app.pointerY + settings_scroll_px(app);
    s_applyHover = app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + btnW &&
                   py >= s_applyBtnY && py < s_applyBtnY + btnH;
    s_cancelHover = app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + btnW &&
                    py >= s_cancelBtnY && py < s_cancelBtnY + btnH;

    const std::string& pendVal = (subTab == 0) ? s_pendingGtkTheme
                               : (subTab == 1) ? s_pendingQtStyle
                               : s_pendingPlasmaTheme;
    const std::string& backVal = (subTab == 0) ? s_backupGtkTheme
                               : (subTab == 1) ? s_backupQtStyle
                               : s_backupPlasmaTheme;
    bool hasChanges = pendVal != backVal;
    if (subTab == 1) hasChanges = hasChanges || (s_pendingQtColorScheme != s_backupQtColorScheme);

    {
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Apply");
      btn.setGeometry(static_cast<float>(s_applyBtnX), static_cast<float>(s_applyBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Filled);
      btn.setSize(m3::Button::Size::S);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setHovered(s_applyHover);
      btn.setEnabled(hasChanges);
      btn.paint(cr);
    }

    {
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Cancel");
      btn.setGeometry(static_cast<float>(s_cancelBtnX), static_cast<float>(s_cancelBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::S);
      btn.setAccentColor(t_r, t_g, t_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(s_cancelHover);
      btn.setEnabled(hasChanges);
      btn.paint(cr);
    }
  }
}

bool settings_themes_consume_pointer_down(App& app, int contentX, int contentW) {
   
  const double lyA = app.pointerY + settings_scroll_px(app);
  const int subTab = app.themesSubTab;
  auto gtkThemes = eh::theming::list_installed_gtk_themes();
  const auto qtStyles = eh::theming::known_qt_styles();
  auto cursorThemes = eh::theming::list_installed_cursor_themes();
  auto plasmaThemes = eh::theming::list_installed_plasma_themes();
  int itemCount = 0;
  if (subTab == 0) itemCount = static_cast<int>(gtkThemes.size());
  else if (subTab == 1) itemCount = static_cast<int>(qtStyles.size());
  else if (subTab == 2) itemCount = static_cast<int>(cursorThemes.size());
  else itemCount = static_cast<int>(plasmaThemes.size());

  const int cols = std::max(1, (contentW - 32) / kThemeCardW);
  const int totalGridW = cols * kThemeCardW + (cols - 1) * kThemeCardGap;
  const double gridLeft = static_cast<double>(contentX) + (static_cast<double>(contentW) - static_cast<double>(totalGridW)) * 0.5;
  const double gridTop = static_cast<double>(kContentTop) + kTabBarH + kSpacingL + 60;

  // sub-tab clicks
  for (int t = 0; t < kNTabSubs; ++t) {
    const double tw = s_subTabWs[t];
    if (lyA >= s_subTabYs[t] && lyA < s_subTabYs[t] + kTabBarH &&
        app.pointerX >= s_subTabXs[t] && app.pointerX < s_subTabXs[t] + tw) {
      if (app.themesSubTab == 2 && t != 2) cursor_backup_reset();
      if (app.themesSubTab != 2 && t == 2) { s_gtkBackupInited = false; s_qtBackupInited = false; s_plasmaBackupInited = false; }
      app.themesSubTab = t;
      app.settingsThemesScrollPx = 0;
      draw(app);
      return true;
    }
  }

  for (int i = 0; i < itemCount; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const double cx = gridLeft + static_cast<double>(col * (kThemeCardW + kThemeCardGap));
    const double cy = gridTop + static_cast<double>(row * (kThemeCardH + kThemeCardGap));

    if (app.pointerX >= cx && app.pointerX < cx + kThemeCardW &&
        lyA >= cy && lyA < cy + kThemeCardH) {
      if (subTab == 0) {
        const auto& theme = gtkThemes[static_cast<size_t>(i)];
        s_pendingGtkTheme = theme.id;
      } else if (subTab == 1) {
        const auto& style = qtStyles[static_cast<size_t>(i)];
        s_pendingQtStyle = style;
      } else if (subTab == 2) {
        const auto& ct = cursorThemes[static_cast<size_t>(i)];
        app.mangoConfig.cursor_theme = ct.id;
      } else {
        const auto& pt = plasmaThemes[static_cast<size_t>(i)];
        s_pendingPlasmaTheme = pt.id;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }
  }

  // Qt color scheme dropdown trigger
  if (subTab == 1) {
    const int gridBottom = static_cast<int>(gridTop) + (itemCount > 0 ? ((itemCount + cols - 1) / cols) : 0) * (kThemeCardH + kThemeCardGap) - kThemeCardGap;
    int cbx, cby, cbw, cbh;
    qt_color_scheme_combo_geom(contentX, contentW, gridBottom, cbx, cby, cbw, cbh);
    const double ly = static_cast<double>(cby);
    if (app.pointerX >= cbx && app.pointerX < cbx + cbw &&
        lyA >= ly && lyA < ly + kQtColorSchemeComboH) {
      settings_close_non_default_app_dropdowns(app);
      app.qtColorSchemeDropdownOpen = true;
      draw(app);
      return true;
    }
  }

  // Cursor size slider trigger
  if (subTab == 2) {
    const int gridBottom = static_cast<int>(gridTop) + ((itemCount + cols - 1) / cols) * (kThemeCardH + kThemeCardGap) - kThemeCardGap;
    int slY = gridBottom + 12;
    int trX = contentX + kCardPadX;
    int trW = contentW - kCardPadX - kCardPadX - 52;
    if (trW < 40) trW = 40;
    if (app.pointerX >= trX - 6 && app.pointerX < trX + trW + 12 &&
        lyA >= slY + 20 && lyA < slY + 20 + 28) {
      app.themesSliderDrag = 0;
      int v = slider_value_from_x(app.pointerX, trX, trW, 16, 64);
      if (v != app.mangoConfig.cursor_size) {
        app.mangoConfig.cursor_size = v;
      }
      draw(app);
      return true;
    }
  }

  // Apply/Cancel buttons for the toolkit theme tab
  if (subTab == 0) {
    const bool hasChanges = s_pendingGtkTheme != s_backupGtkTheme;

    if (app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + s_applyBtnW &&
        lyA >= s_applyBtnY && lyA < s_applyBtnY + s_applyBtnH) {
      if (hasChanges) {
        (void)eh::theming::apply_gtk_theme(s_pendingGtkTheme);
        s_backupGtkTheme = s_pendingGtkTheme;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }

    if (app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + s_cancelBtnW &&
        lyA >= s_cancelBtnY && lyA < s_cancelBtnY + s_cancelBtnH) {
      if (hasChanges) s_pendingGtkTheme = s_backupGtkTheme;
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }
  }

  // Apply/Cancel buttons for QT tab
  if (subTab == 1) {
    const bool hasChanges = s_pendingQtStyle != s_backupQtStyle || s_pendingQtColorScheme != s_backupQtColorScheme;

    if (app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + s_applyBtnW &&
        lyA >= s_applyBtnY && lyA < s_applyBtnY + s_applyBtnH) {
      if (hasChanges) {
        (void)eh::theming::apply_qt_style(s_pendingQtStyle, true);
        (void)eh::theming::apply_qt_color_scheme(s_pendingQtColorScheme, true);
        s_backupQtStyle = s_pendingQtStyle;
        s_backupQtColorScheme = s_pendingQtColorScheme;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }

    if (app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + s_cancelBtnW &&
        lyA >= s_cancelBtnY && lyA < s_cancelBtnY + s_cancelBtnH) {
      if (hasChanges) {
        s_pendingQtStyle = s_backupQtStyle;
        s_pendingQtColorScheme = s_backupQtColorScheme;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }
  }

  // Apply/Cancel buttons for the desktop-environment tab
  if (subTab == 3) {
    const bool hasChanges = s_pendingPlasmaTheme != s_backupPlasmaTheme;

    if (app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + s_applyBtnW &&
        lyA >= s_applyBtnY && lyA < s_applyBtnY + s_applyBtnH) {
      if (hasChanges) {
        app.settings.plasmaTheme = s_pendingPlasmaTheme;
        (void)eh::theming::apply_plasma_theme(s_pendingPlasmaTheme);
        save_settings(app.settings);
        s_backupPlasmaTheme = s_pendingPlasmaTheme;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }

    if (app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + s_cancelBtnW &&
        lyA >= s_cancelBtnY && lyA < s_cancelBtnY + s_cancelBtnH) {
      if (hasChanges) s_pendingPlasmaTheme = s_backupPlasmaTheme;
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }
  }

  // Apply/Cancel buttons for cursor tab
  if (subTab == 2) {
    const bool hasChanges = (app.mangoConfig.cursor_theme != s_backupCursorTheme) ||
                            (app.mangoConfig.cursor_size != s_backupCursorSize);

    // Apply button
    if (app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + s_applyBtnW &&
        lyA >= s_applyBtnY && lyA < s_applyBtnY + s_applyBtnH) {
      if (hasChanges) {
        mango_commit_cfg(app);
        apply_cursor_config(app);
        save_settings(app.settings);
        s_backupCursorTheme = app.mangoConfig.cursor_theme;
        s_backupCursorSize = app.mangoConfig.cursor_size;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }

    // Cancel button
    if (app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + s_cancelBtnW &&
        lyA >= s_cancelBtnY && lyA < s_cancelBtnY + s_cancelBtnH) {
      if (hasChanges) {
        app.mangoConfig.cursor_theme = s_backupCursorTheme;
        app.mangoConfig.cursor_size = s_backupCursorSize;
        mango_commit_cfg(app);
        apply_cursor_config(app);
        save_settings(app.settings);
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return true;
    }
  }

  return false;
}

