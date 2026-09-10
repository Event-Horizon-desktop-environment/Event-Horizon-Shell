#include <cairo/cairo.h>
#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_icons/settings_tab_icons.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kIconsTabId = 44;
static constexpr int kIconCardW = 320;

// Apply/Cancel backup.
static std::string s_backupIconTheme;
static bool s_backupInited = false;

static double s_applyBtnX, s_applyBtnY, s_applyBtnW, s_applyBtnH;
static double s_cancelBtnX, s_cancelBtnY, s_cancelBtnW, s_cancelBtnH;

static void icons_backup_init(const App& app) {
  if (!s_backupInited) {
    s_backupIconTheme = app.settings.iconTheme;
    s_backupInited = true;
  }
}

void icons_backup_reset() {
  s_backupInited = false;
}

static std::unordered_map<std::string, std::vector<cairo_surface_t*>> s_previewCache;

void clear_icons_preview_cache() {
    
  for (auto& [_, surfs] : s_previewCache)
    for (auto* s : surfs)
      if (s) cairo_surface_destroy(s);
  s_previewCache.clear();
}
static constexpr int kIconCardH = 140;
static constexpr int kIconPreviewW = 56;
static constexpr int kIconPreviewGap = 12;
static constexpr int kIconPreviewPadTop = 24;

static const char* kPreviewIconNames[] = {
  "folder", "text-x-generic", "computer", "application-x-executable",
  "media-optical", "input-mouse", "preferences-system", "help-browser",
  "document", "user", "system-search", "internet-web-browser"
};
static constexpr int kIconCardGridGap = 12;

struct IconsTabLayout {
  int cardW = kIconCardW;
  int cardH = kIconCardH;
  int cols = 1;
  int rows = 0;
  int gridLeft = 0;
  int gridTop = 0;
};

static IconsTabLayout icons_tab_layout(int contentX, int contentW) {
   
  IconsTabLayout lay;
  lay.cardW = kIconCardW;
  lay.cardH = kIconCardH;
  const int availableW = contentW - 32;
  lay.cols = std::max(1, availableW / (kIconCardW + kIconCardGridGap));
  const int totalW = lay.cols * kIconCardW + (lay.cols - 1) * kIconCardGridGap;
  lay.gridLeft = contentX + (contentW - totalW) / 2;
  lay.gridTop = kContentTop + kSpacingL + 60;
  return lay;
}

int icons_tab_content_bottom_px(const App& app) {
    
  const auto themes = eh::icons::list_installed_icon_themes();
  const int totalThemes = static_cast<int>(themes.size());
  IconsTabLayout lay = icons_tab_layout(16 + 240 + 16, 0);
  lay.cols = std::max(1, (app.width - 16 - 240 - 16 - 32) / (kIconCardW + kIconCardGridGap));
  if (lay.cols < 1) lay.cols = 1;
  if (totalThemes == 0) return kContentTop + kSpacingL + 60 + 60;
  const int rows = (totalThemes + lay.cols - 1) / lay.cols;
  return lay.gridTop + rows * (kIconCardH + kIconCardGridGap) + kSpacingL + 60;
}

int settings_icons_scroll_max_px(const App& app) {
   
  const int viewportH = std::max(120, app.height - kContentTop - kSpacingL);
  const int bottom = icons_tab_content_bottom_px(app);
  return std::max(0, bottom - kContentTop - viewportH);
}

void settings_clamp_icons_scroll_px(App& app) {
   
  const int mx = settings_icons_scroll_max_px(app);
  app.settingsIconsScrollPx = std::clamp(app.settingsIconsScrollPx, 0, mx);
}

void paint_icons_tab(App& app, cairo_t* cr, int contentX, int contentW,
                     double cardX, double cardW, double glassOv,
                     double dockMatA, double paintPointerYOffset) {
   
  (void)glassOv;
  (void)dockMatA;
  (void)paintPointerYOffset;

  auto themes = eh::icons::list_installed_icon_themes();
  IconsTabLayout lay = icons_tab_layout(contentX, contentW);
  const int n = static_cast<int>(themes.size());

  const int totalW = n > 0 ? (lay.cols * kIconCardW + (lay.cols - 1) * kIconCardGridGap) : 0;
  lay.gridLeft = contentX + (contentW - totalW) / 2;

  const int rows = n > 0 ? (n + lay.cols - 1) / lay.cols : 0;
  const int contentBottom = n > 0 ? (lay.gridTop + rows * (kIconCardH + kIconCardGridGap) + kSpacingL)
                                  : (kContentTop + kSpacingL + 60 + 60);
  const double cardTop = static_cast<double>(kContentTop);
  const double cardH = std::max(200.0,
                                std::max(static_cast<double>(contentBottom) - cardTop + kSpacingL,
                                         static_cast<double>(app.height) - cardTop - static_cast<double>(kSpacingL)));

  settings_card(app, cr, cardX, cardTop, cardW, cardH, 1.0);
  settings_cat_label(cr, cardX + kCardPad, cardTop + 22.0, "Icons");
  settings_label(cr, static_cast<double>(contentX + kCardPad), cardTop + kSpacingL + 46,
                 "Select an icon theme", "", false);

  if (n == 0) {
    settings_show_text(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kContentTop + 160), "No icon themes found.", 13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.7f);
    return;
  }

  for (int i = 0; i < n; ++i) {
    const auto& theme = themes[static_cast<size_t>(i)];
    if (!theme.path.empty() && s_previewCache.find(theme.id) == s_previewCache.end()) {
      std::vector<cairo_surface_t*> surfs;
      std::vector<std::string> tried;
      tried.reserve(16);
      const std::string example = eh::icons::theme_example_icon_name(theme.path);
      if (!example.empty()) tried.push_back(example);
      for (const char* pn : kPreviewIconNames) {
        if (tried.size() >= 16) break;
        bool dup = false;
        for (const auto& t : tried) { if (t == pn) { dup = true; break; } }
        if (!dup) tried.push_back(pn);
      }
      for (const auto& nm : tried) {
        auto* s = eh::icons::load_theme_preview_icon(theme.path, nm, kIconPreviewW);
        if (s) { surfs.push_back(s); if (surfs.size() >= 4) break; }
      }
      if (surfs.size() < 4) {
        for (const auto& fn : eh::icons::theme_find_any_icons(theme.path, 8)) {
          bool dup = false;
          for (const auto& t : tried) { if (t == fn) { dup = true; break; } }
          if (dup) continue;
          auto* s = eh::icons::load_theme_preview_icon(theme.path, fn, kIconPreviewW);
          if (s) { surfs.push_back(s); if (surfs.size() >= 4) break; }
        }
      }
      s_previewCache[theme.id] = std::move(surfs);
    }
  }

  const std::string& currentTheme = app.settings.iconTheme;
  double pyPaint = app.pointerY + settings_scroll_px(app);

  for (int i = 0; i < n; ++i) {
    const int col = i % lay.cols;
    const int row = i / lay.cols;
    const double cx = static_cast<double>(lay.gridLeft + col * (kIconCardW + kIconCardGridGap));
    const double cy = static_cast<double>(lay.gridTop + row * (kIconCardH + kIconCardGridGap));
    const auto& theme = themes[static_cast<size_t>(i)];
    const bool isCurrent = (theme.id == currentTheme);
    const bool hover = (app.pointerX >= cx && app.pointerX < cx + kIconCardW &&
                        pyPaint >= cy && pyPaint < cy + kIconCardH);

    {
      m3::Box box;
      if (isCurrent) {
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.accentR;
          g = app.drawChrome.accentG;
          b = app.drawChrome.accentB;
        } else {
          r = static_cast<float>(Theme::AccR);
          g = static_cast<float>(Theme::AccG);
          b = static_cast<float>(Theme::AccB);
        }
        box.setColor(r, g, b, hover ? 0.30f : 0.22f);
      } else {
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
        box.setColor(r, g, b, hover ? 0.92f : 0.85f);
      }
      box.setRadius(10.0f);
      box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                      static_cast<float>(kIconCardW), static_cast<float>(kIconCardH));
      box.setGlassy(true);
      box.paint(cr);
    }

    cairo_round_rect(cr, cx, cy, kIconCardW, kIconCardH, 10.0);
    if (isCurrent) {
      paint_src_accent(app, cr, 0.60);
      cairo_set_line_width(cr, 2.0);
    } else {
      paint_src_glass_hi(app, cr, 0.08);
      cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    if (isCurrent) {
      const double dotSize = 10.0;
      cairo_set_source_rgba(cr, 0.35, 0.85, 0.45, 1.0);
      cairo_arc(cr, cx + kIconCardW - 18.0, cy + 18.0, dotSize * 0.5, 0, 2 * M_PI);
      cairo_fill(cr);
    }

    const auto& previews = s_previewCache[theme.id];
    if (!previews.empty()) {
      const int totalPreviewW = static_cast<int>(previews.size()) * (kIconPreviewW + kIconPreviewGap) - kIconPreviewGap;
      const double px0 = cx + (kIconCardW - totalPreviewW) * 0.5;
      for (size_t pi = 0; pi < previews.size(); ++pi) {
        auto* surf = previews[pi];
        if (!surf) continue;
        const int sw = cairo_image_surface_get_width(surf);
        const int sh = cairo_image_surface_get_height(surf);
        if (sw <= 0 || sh <= 0) continue;
        const double sx = px0 + static_cast<double>(pi * (kIconPreviewW + kIconPreviewGap));
        const double sy = cy + kIconPreviewPadTop;
        const double scX = static_cast<double>(kIconPreviewW) / static_cast<double>(sw);
        const double scY = static_cast<double>(kIconPreviewW) / static_cast<double>(sh);
        const double drawW = static_cast<double>(kIconPreviewW);
        const double drawH = static_cast<double>(sh) * scY;

        cairo_save(cr);
        cairo_rectangle(cr, sx, sy + (kIconPreviewW - drawH) * 0.5, drawW, drawH);
        cairo_clip(cr);
        cairo_translate(cr, sx, sy + (kIconPreviewW - drawH) * 0.5);
        cairo_scale(cr, scX, scY);
        cairo_set_source_surface(cr, surf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
      }
    }

    std::string displayName = theme.name.empty() ? theme.id : theme.name;

    auto measureText = [&](const char* txt, int fontSize, int fontWeight, int& pw, int& ph) {
      auto* ml = pango_cairo_create_layout(cr);
      auto* md = pango_font_description_new();
      pango_font_description_set_family(md, "Inter");
      pango_font_description_set_size(md, fontSize * PANGO_SCALE);
      pango_font_description_set_weight(md, static_cast<PangoWeight>(fontWeight));
      pango_layout_set_font_description(ml, md);
      pango_layout_set_text(ml, txt, -1);
      pango_layout_get_pixel_size(ml, &pw, &ph);
      pango_font_description_free(md);
      g_object_unref(ml);
    };

    int nw, nh;
    measureText(displayName.c_str(), 13, isCurrent ? 700 : 400, nw, nh);
    const double textX = cx + (static_cast<double>(kIconCardW) - static_cast<double>(nw)) * 0.5;

    if (!theme.name.empty() && theme.name != theme.id) {
      int iw, ih;
      measureText(theme.id.c_str(), 10, 400, iw, ih);
      const double idX = cx + (static_cast<double>(kIconCardW) - static_cast<double>(iw)) * 0.5;
      settings_show_text(cr, idX, cy + kIconCardH - 42.0, theme.id.c_str(), 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);
      settings_show_text(cr, textX, cy + kIconCardH - 18.0, displayName.c_str(), 13, isCurrent ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    } else {
      settings_show_text(cr, textX, cy + kIconCardH - 20.0, displayName.c_str(), 13, isCurrent ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }
  }

  // Apply / Cancel buttons.
  icons_backup_init(app);
  {
    const int gridBottom = lay.gridTop + rows * (kIconCardH + kIconCardGridGap) - kIconCardGridGap;
    const int btnY = gridBottom + 16;
    const int btnW = 110;
    const int btnH = 36;

    s_cancelBtnX = contentX + contentW - kCardPad - btnW;
    s_cancelBtnY = btnY;
    s_cancelBtnW = btnW;
    s_cancelBtnH = btnH;
    s_applyBtnX = s_cancelBtnX - btnW - 12;
    s_applyBtnY = btnY;
    s_applyBtnW = btnW;
    s_applyBtnH = btnH;

    const double py = app.pointerY + settings_scroll_px(app);
    const bool applyHover = app.pointerX >= s_applyBtnX && app.pointerX < s_applyBtnX + btnW &&
                            py >= s_applyBtnY && py < s_applyBtnY + btnH;
    const bool cancelHover = app.pointerX >= s_cancelBtnX && app.pointerX < s_cancelBtnX + btnW &&
                             py >= s_cancelBtnY && py < s_cancelBtnY + btnH;

    const bool hasChanges = app.settings.iconTheme != s_backupIconTheme;

    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

    {
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Apply");
      btn.setGeometry(static_cast<float>(s_applyBtnX), static_cast<float>(s_applyBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setEnabled(hasChanges);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(applyHover);
      btn.paint(cr);
    }

    {
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Cancel");
      btn.setGeometry(static_cast<float>(s_cancelBtnX), static_cast<float>(s_cancelBtnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setEnabled(true);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(cancelHover);
      btn.paint(cr);
    }
  }
}

bool settings_icons_consume_pointer_down(App& app, int contentX, int contentW) {
    
  const double lyA = app.pointerY + settings_scroll_px(app);
  auto themes = eh::icons::list_installed_icon_themes();
  IconsTabLayout lay = icons_tab_layout(contentX, contentW);
  const int totalW = lay.cols * kIconCardW + (lay.cols - 1) * kIconCardGridGap;
  lay.gridLeft = contentX + (contentW - totalW) / 2;
  const int n = static_cast<int>(themes.size());

  for (int i = 0; i < n; ++i) {
    const int col = i % lay.cols;
    const int row = i / lay.cols;
    const double cx = static_cast<double>(lay.gridLeft + col * (kIconCardW + kIconCardGridGap));
    const double cy = static_cast<double>(lay.gridTop + row * (kIconCardH + kIconCardGridGap));

    if (app.pointerX >= cx && app.pointerX < cx + kIconCardW &&
        lyA >= cy && lyA < cy + kIconCardH) {
      const auto& theme = themes[static_cast<size_t>(i)];
      app.settings.iconTheme = theme.id;
      app.icons.set_icon_theme(app.settings.iconTheme);
      draw(app);
      return true;
    }
  }

  // Apply/Cancel buttons
  const bool hasChanges = app.settings.iconTheme != s_backupIconTheme;

  {
    m3::Button btn;
    btn.setGeometry(static_cast<float>(s_applyBtnX), static_cast<float>(s_applyBtnY),
                    static_cast<float>(s_applyBtnW), static_cast<float>(s_applyBtnH));
    if (btn.containsPoint(static_cast<float>(app.pointerX), static_cast<float>(lyA))) {
      if (hasChanges) {
        save_settings(app.settings);
        (void)eh::icons::apply_icon_theme(app.settings.iconTheme);
        s_backupIconTheme = app.settings.iconTheme;
      }
      draw(app);
      return true;
    }
  }

  {
    m3::Button btn;
    btn.setGeometry(static_cast<float>(s_cancelBtnX), static_cast<float>(s_cancelBtnY),
                    static_cast<float>(s_cancelBtnW), static_cast<float>(s_cancelBtnH));
    if (btn.containsPoint(static_cast<float>(app.pointerX), static_cast<float>(lyA))) {
      if (hasChanges) {
        app.settings.iconTheme = s_backupIconTheme;
        app.icons.set_icon_theme(app.settings.iconTheme);
      }
      draw(app);
      return true;
    }
  }

  return false;
}
