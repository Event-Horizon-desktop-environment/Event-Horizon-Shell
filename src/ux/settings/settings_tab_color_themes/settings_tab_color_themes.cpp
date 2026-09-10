#include <algorithm>
#include <cstdio>
#include <vector>

#include <cairo/cairo.h>

#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_color_themes/settings_tab_color_themes.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/theme_data.hpp"
#include "ux/settings/theme_manager.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;

static constexpr int kModeCardH = 52 + 56 + kSpacingXL;
static constexpr int kModeToggleY = kBodyTop + 8;

static constexpr int kPresetCardW = 220;
static constexpr int kPresetCardH = 140;
static constexpr int kPresetSwatchH = 50;
static constexpr int kPresetGap = 10;

static constexpr int kSubHeaderH = 30;

// Variant classification.

static bool variant_is_light(std::string_view v) {
  return v == "light" || v == "latte" || v == "dawn" || v == "brand-light";
}

// Grouped preset structure.

struct ThemeGroup {
  std::string_view source;
  std::vector<const eh::settings::theme::ThemePreset*> darkPresets;
  std::vector<const eh::settings::theme::ThemePreset*> lightPresets;
};

static const std::vector<ThemeGroup>& get_theme_groups() {
  static const auto groups = [] {
    std::vector<ThemeGroup> g;
    const auto& all = eh::settings::theme::all_builtin_presets();
    for (const auto& p : all) {
      auto it = std::find_if(g.begin(), g.end(),
        [&](const ThemeGroup& grp) { return grp.source == p.source; });
      if (it == g.end()) {
        g.push_back({p.source, {}, {}});
        it = g.end() - 1;
      }
      if (variant_is_light(p.variant))
        it->lightPresets.push_back(&p);
      else
        it->darkPresets.push_back(&p);
    }
    return g;
  }();
  return groups;
}

// Mode label helper.

static void draw_mode_pill(cairo_t* cr, bool active, bool hovered, int x, int y, int w, int h,
                            const char* label, float accent_r, float accent_g, float accent_b) {
  const float rad = std::min(10.0f, static_cast<float>(h) * 0.5f);
  m3::Box box;
  if (active) {
    box.setColor(accent_r, accent_g, accent_b, hovered ? 1.0f : 0.85f);
    box.setRadius(rad);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h));
    box.paint(cr);
  } else {
    box.setColor(0.5f, 0.5f, 0.5f, hovered ? 0.26f : 0.15f);
    box.setRadius(rad);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h));
    box.paint(cr);
    cairo_round_rect(cr, static_cast<double>(x), static_cast<double>(y),
                     static_cast<double>(w), static_cast<double>(h),
                     static_cast<double>(rad));
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, hovered ? 0.55 : 0.3);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  if (hovered) {
    cairo_round_rect(cr, static_cast<double>(x), static_cast<double>(y),
                     static_cast<double>(w), static_cast<double>(h),
                     static_cast<double>(rad));
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_fill(cr);
  }

  m3::Label lbl;
  lbl.setText(label);
  lbl.setFontSize(13.0f);
  lbl.setFontWeight(600);
  if (active) {
    lbl.setColor(1.0f, 1.0f, 1.0f, hovered ? 1.0f : 0.95f);
  } else {
    lbl.setColor(0.5f, 0.5f, 0.5f, hovered ? 0.9f : 0.7f);
  }
  float tw, th;
  lbl.measureExtents(tw, th);
  // Pills are sized to their label + padding; center the text within the pill.
  const float textX = static_cast<float>(x) + (static_cast<float>(w) - tw) * 0.5f;
  lbl.paintAt(cr, textX,
    static_cast<float>(y) + static_cast<float>(h) / 2.0f - th / 2.0f);
}

// Color-source mode pill geometry (three side by side, centered).

struct ModePillGeom { int x1, x2, x3, y, w1, w2, w3, h; };

// Width of a pill label at the pill's font settings.
static float measure_pill_label_width(const char* label) {
  m3::Label lbl;
  lbl.setText(label);
  lbl.setFontSize(13.0f);
  lbl.setFontWeight(600);
  float tw, th;
  lbl.measureExtents(tw, th);
  return tw;
}

static ModePillGeom mode_pill_geom(int cardX, int cardW) {
  constexpr float kPadX = 18.0f;
  constexpr int kMinW = 100;
  constexpr int kMaxW = 170;
  constexpr int kGap = 12;
  constexpr int kPillH = 38;
  const int pad2 = static_cast<int>(2 * kPadX);
  int w1 = std::clamp(static_cast<int>(measure_pill_label_width("Matugen")) + pad2, kMinW, kMaxW);
  int w2 = std::clamp(static_cast<int>(measure_pill_label_width("Color Engine")) + pad2, kMinW, kMaxW);
  int w3 = std::clamp(static_cast<int>(measure_pill_label_width("Custom Theme")) + pad2, kMinW, kMaxW);
  const int avail = cardW - kCardPad * 2;
  if (w1 + w2 + w3 + 2 * kGap > avail) {
    const int maxW = std::max(1, (avail - 2 * kGap) / 3);
    w1 = std::min(w1, maxW);
    w2 = std::min(w2, maxW);
    w3 = std::min(w3, maxW);
  }
  const int pillY = kModeToggleY;
  const int groupW = w1 + w2 + w3 + 2 * kGap;
  const int groupX = cardX + (cardW - groupW) / 2;
  return {groupX, groupX + w1 + kGap, groupX + w1 + kGap + w2 + kGap,
          pillY, w1, w2, w3, kPillH};
}

// Preset card helpers.

static void draw_preset_swatch(cairo_t* cr, int x, int y, int w, int h,
                                float pr, float pg, float pb,
                                float sr, float sg, float sb,
                                float tr, float tg, float tb,
                                float sur, float sug, float sub,
                                float or_, float og, float ob) {
  const int n = 5;
  const int sw = w / n;
  const float cols[5][3] = {
    {pr, pg, pb}, {sr, sg, sb}, {tr, tg, tb},
    {sur, sug, sub}, {or_, og, ob}
  };
  for (int i = 0; i < n; ++i) {
    cairo_rectangle(cr, static_cast<double>(x + i * sw), static_cast<double>(y),
                    static_cast<double>(sw), static_cast<double>(h));
    cairo_set_source_rgba(cr,
      static_cast<double>(cols[i][0]),
      static_cast<double>(cols[i][1]),
      static_cast<double>(cols[i][2]), 1.0);
    cairo_fill(cr);
  }
}

static void draw_preset_card(App& app, cairo_t* cr, int cx, int cy, int cw, int ch,
                              const eh::settings::theme::ThemePreset& preset,
                              bool selected, double glassOv) {
  const double rad = 10.0;
  if (selected) {
    float ar, ag, ab;
    settings_resolve_colors(app, ar, ag, ab, ar, ag, ab, ar, ag, ab, ar, ag, ab);
    m3::Box box;
    box.setColor(ar, ag, ab, 0.30f);
    box.setRadius(static_cast<float>(rad));
    box.setGeometry(static_cast<float>(cx - 2), static_cast<float>(cy - 2),
                    static_cast<float>(cw + 4), static_cast<float>(ch + 4));
    box.paint(cr);
  }
  settings_card(app, cr, static_cast<double>(cx), static_cast<double>(cy),
                static_cast<double>(cw), static_cast<double>(ch), glassOv);
  const auto& p = preset.palette;
  draw_preset_swatch(cr, cx, cy, cw, kPresetSwatchH,
                     p.primaryR, p.primaryG, p.primaryB,
                     p.secondaryR, p.secondaryG, p.secondaryB,
                     p.tertiaryR, p.tertiaryG, p.tertiaryB,
                     p.surfaceR, p.surfaceG, p.surfaceB,
                     p.outlineR, p.outlineG, p.outlineB);
  settings_show_text(cr, static_cast<double>(cx + 10), static_cast<double>(cy + kPresetSwatchH + 22),
                     preset.name.data(), 13, 600, 1.0, 1.0, 1.0, 0.90);
  settings_show_text(cr, static_cast<double>(cx + 10), static_cast<double>(cy + kPresetSwatchH + 40),
                     preset.source.data(), 11, 400, 1.0, 1.0, 1.0, 0.45);
  if (!preset.variant.empty()) {
    settings_show_text(cr, static_cast<double>(cx + 10), static_cast<double>(cy + kPresetSwatchH + 56),
                       preset.variant.data(), 10, 400, 1.0, 1.0, 1.0, 0.35);
  }
}

// Group geometry helpers.

static int group_grid_cols(int cardW) {
  return std::max(1, (cardW - kCardPad * 2) / (kPresetCardW + kPresetGap));
}

static int group_grid_rows(int count, int nCols) {
  return count == 0 ? 0 : (count + nCols - 1) / nCols;
}

static int group_grid_height(int count, int nCols) {
  const int rows = group_grid_rows(count, nCols);
  return rows * kPresetCardH + std::max(0, rows - 1) * kPresetGap;
}

static int group_card_height(int darkCount, int lightCount, int nCols) {
  int h = 52 + kSpacingXL;
  if (darkCount > 0)
    h += kSubHeaderH + group_grid_height(darkCount, nCols);
  if (lightCount > 0)
    h += kSubHeaderH + group_grid_height(lightCount, nCols);
  return h;
}

// Scroll helpers.

int settings_color_themes_scroll_max_px(const App& app, int contentW) {
  const int cardW = contentW - 16;
  const int nCols = group_grid_cols(cardW);
  const auto& groups = get_theme_groups();

  int groupsH = 0;
  for (const auto& g : groups) {
    const int dc = static_cast<int>(g.darkPresets.size());
    const int lc = static_cast<int>(g.lightPresets.size());
    if (dc == 0 && lc == 0) continue;
    groupsH += group_card_height(dc, lc, nCols);
  }
  groupsH += std::max(0, static_cast<int>(groups.size()) - 1) * kCardGap;

  const int userH = app.colorThemeList.empty() ? 0 : 52 + static_cast<int>(app.colorThemeList.size()) * 44 + kSpacingXL;
  const int infoH = 52 + 40 + kSpacingXL;
  const int totalH = kModeCardH + kCardGap +
    (groups.empty() ? 0 : groupsH + kCardGap) +
    (app.colorThemeList.empty() ? 0 : userH + kCardGap) +
    infoH + kSpacingXL;
  const int viewH = app.height - kContentTop - kSpacingL;
  return std::max(0, totalH - viewH);
}

void settings_clamp_color_themes_scroll(App& app, int contentW) {
  const int mx = settings_color_themes_scroll_max_px(app, contentW);
  app.settingsColorThemesScrollPx = std::clamp(app.settingsColorThemesScrollPx, 0, mx);
}

// Paint function.

void paint_color_themes_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  float accent_r, accent_g, accent_b, text_r, text_g, text_b;
  settings_resolve_colors(app, accent_r, accent_g, accent_b,
                          text_r, text_g, text_b,
                          accent_r, accent_g, accent_b,
                          accent_r, accent_g, accent_b);

  // Scan user themes if needed.
  if (app.colorThemeListNeedsRefresh) {
    app.colorThemeList.clear();
    for (const auto& ut : scan_user_themes()) {
      App::ColorThemeEntry e;
      e.name = ut.name;
      e.source = ut.source;
      e.variant = ut.variant;
      e.builtIn = false;
      e.filePath = ut.filePath;
      app.colorThemeList.push_back(std::move(e));
    }
    app.colorThemeListNeedsRefresh = false;
  }

  // Card 1: Mode toggle (Dynamic / Color Engine / Custom Theme).
  const int modeCardTop = kCardTop;
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(modeCardTop),
                static_cast<double>(cardW), static_cast<double>(kModeCardH), glassOv);
  settings_clamp_color_themes_scroll(app, contentW);

  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), modeCardTop + 22, "COLOR SOURCE");

  const ModePillGeom pg = mode_pill_geom(cardX, cardW);
  const bool isMatugen = app.settings.matugenThemingEnabled;
  const bool isColorEngine = !app.settings.colorThemeEnabled && !isMatugen;
  const bool isCustom = app.settings.colorThemeEnabled;

  draw_mode_pill(cr, isMatugen, app.colorThemesHoverPill == 0, pg.x1, pg.y, pg.w1, pg.h,
                 "Matugen", accent_r, accent_g, accent_b);
  draw_mode_pill(cr, isColorEngine, app.colorThemesHoverPill == 1, pg.x2, pg.y, pg.w2, pg.h,
                 "Color Engine", accent_r, accent_g, accent_b);
  draw_mode_pill(cr, isCustom, app.colorThemesHoverPill == 2, pg.x3, pg.y, pg.w3, pg.h,
                 "Custom Theme", accent_r, accent_g, accent_b);

  // Only the custom-theme source shows preset/theme cards below.
  if (!app.settings.colorThemeEnabled) return;

  int nextCardTop = modeCardTop + kModeCardH + kCardGap;

  // Card 2+: grouped built-in presets.
  const auto& groups = get_theme_groups();
  if (!groups.empty()) {
    const int nCols = group_grid_cols(cardW);

    for (const auto& g : groups) {
      const int darkCnt = static_cast<int>(g.darkPresets.size());
      const int lightCnt = static_cast<int>(g.lightPresets.size());
      if (darkCnt == 0 && lightCnt == 0) continue;

      const int gh = group_card_height(darkCnt, lightCnt, nCols);
      const int groupTop = nextCardTop;

      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(groupTop),
                    static_cast<double>(cardW), static_cast<double>(gh), glassOv);

      // Source name header
      {
        char header[64];
        std::snprintf(header, sizeof(header), "%.*s",
                      static_cast<int>(g.source.size()), g.source.data());
        if (header[0] >= 'a' && header[0] <= 'z')
          header[0] = static_cast<char>(header[0] - 32);
        settings_cat_label(cr, static_cast<double>(cardX + kCardPad), groupTop + 22, header);
      }

      int contentY = groupTop + 52;

      // Dark sub-section.
      if (darkCnt > 0) {
        {
          m3::Label lbl;
          lbl.setText("Dark");
          lbl.setFontSize(11.0f);
          lbl.setFontWeight(600);
          float tw, th;
          lbl.measureExtents(tw, th);
          lbl.setColor(1.0f, 1.0f, 1.0f, 0.55f);
          lbl.paintAt(cr,
            static_cast<float>(cardX + kCardPad),
            static_cast<float>(contentY) + 16.0f - th);
        }
        contentY += kSubHeaderH;

        const int totalW = nCols * kPresetCardW + (nCols - 1) * kPresetGap;
        const int gridX = cardX + (cardW - totalW) / 2;
        const int darkRows = group_grid_rows(darkCnt, nCols);

        for (int i = 0; i < darkCnt; ++i) {
          const int col = i % nCols;
          const int row = i / nCols;
          const int px = gridX + col * (kPresetCardW + kPresetGap);
          const int py = contentY + row * (kPresetCardH + kPresetGap);
          const auto& preset = *g.darkPresets[i];
          const bool selected = app.settings.colorThemeName == preset.name &&
                                app.settings.colorThemeSource == preset.source;
          draw_preset_card(app, cr, px, py, kPresetCardW, kPresetCardH,
                           preset, selected, glassOv);
        }
        contentY += darkRows * kPresetCardH + (darkRows - 1) * kPresetGap;
      }

      // Light sub-section.
      if (lightCnt > 0) {
        {
          m3::Label lbl;
          lbl.setText("Light");
          lbl.setFontSize(11.0f);
          lbl.setFontWeight(600);
          float tw, th;
          lbl.measureExtents(tw, th);
          lbl.setColor(1.0f, 1.0f, 1.0f, 0.55f);
          lbl.paintAt(cr,
            static_cast<float>(cardX + kCardPad),
            static_cast<float>(contentY) + 16.0f - th);
        }
        contentY += kSubHeaderH;

        const int totalW = nCols * kPresetCardW + (nCols - 1) * kPresetGap;
        const int gridX = cardX + (cardW - totalW) / 2;
        const int lightRows = group_grid_rows(lightCnt, nCols);

        for (int i = 0; i < lightCnt; ++i) {
          const int col = i % nCols;
          const int row = i / nCols;
          const int px = gridX + col * (kPresetCardW + kPresetGap);
          const int py = contentY + row * (kPresetCardH + kPresetGap);
          const auto& preset = *g.lightPresets[i];
          const bool selected = app.settings.colorThemeName == preset.name &&
                                app.settings.colorThemeSource == preset.source;
          draw_preset_card(app, cr, px, py, kPresetCardW, kPresetCardH,
                           preset, selected, glassOv);
        }
        contentY += lightRows * kPresetCardH + (lightRows - 1) * kPresetGap;
      }

      nextCardTop = groupTop + gh + kCardGap;
    }
  }

  // Card: user themes.
  if (!app.colorThemeList.empty()) {
    const int userCardTop = nextCardTop;
    const int userCardH = 52 + static_cast<int>(app.colorThemeList.size()) * 44 + kSpacingXL;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(userCardTop),
                  static_cast<double>(cardW), static_cast<double>(userCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), userCardTop + 22, "USER THEMES");

    int rowY = userCardTop + 56;
    for (std::size_t i = 0; i < app.colorThemeList.size(); ++i) {
      const auto& e = app.colorThemeList[i];
      const bool selected = app.settings.colorThemeName == e.name &&
                            app.settings.colorThemeSource == e.source;
      const int itemX = cardX + kCardPad;
      const int itemW = cardW - kCardPad * 2;
      if (selected) {
        m3::Box box;
        box.setColor(accent_r, accent_g, accent_b, 0.20f);
        box.setRadius(8.0f);
        box.setGeometry(static_cast<float>(itemX), static_cast<float>(rowY),
                        static_cast<float>(itemW), 38.0f);
        box.paint(cr);
      }
      settings_show_text(cr, static_cast<double>(itemX + 12), static_cast<double>(rowY + 15),
                         e.name.c_str(), 14, 500, 1.0, 1.0, 1.0, selected ? 0.95 : 0.70);
      settings_show_text(cr, static_cast<double>(itemX + itemW - 60), static_cast<double>(rowY + 15),
                         e.source.c_str(), 11, 400, 0.5, 0.5, 0.5, 0.5);
      rowY += 44;
    }
    nextCardTop = userCardTop + userCardH + kCardGap;
  }

  // Active theme info.
  {
    const int infoCardTop = nextCardTop;
    const int infoCardH = 52 + 40 + kSpacingXL;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(infoCardTop),
                  static_cast<double>(cardW), static_cast<double>(infoCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), infoCardTop + 22, "ACTIVE THEME");

    char info[128];
    std::snprintf(info, sizeof(info), "%s  (%s)",
                  app.settings.colorThemeName.c_str(),
                  app.settings.colorThemeSource.c_str());
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), infoCardTop + 56,
                       info, 15, 500, text_r, text_g, text_b, 0.90);
  }

  // Scrollbar.
  {
    const int mx = settings_color_themes_scroll_max_px(app, contentW);
    if (mx > 0) {
      cairo_save(cr);
      cairo_translate(cr, 0.0, settings_scroll_px_int(app));
      const int sbX = contentX + contentW - 10;
      const int sbY = kContentTop;
      const int sbH = app.height - kContentTop - kSpacingL;
      const float thumbH = static_cast<float>(sbH) * static_cast<float>(sbH) / static_cast<float>(sbH + mx);
      const float thumbY = static_cast<float>(sbY) + (static_cast<float>(sbH) - thumbH) *
        static_cast<float>(settings_scroll_px_int(app)) / static_cast<float>(mx);
      m3::Box box;
      box.setColor(0.5f, 0.5f, 0.5f, 0.20f);
      box.setRadius(4.0f);
      box.setGeometry(static_cast<float>(sbX), static_cast<float>(sbY), 6.0f, static_cast<float>(sbH));
      box.paint(cr);
      box.setColor(0.5f, 0.5f, 0.5f, 0.40f);
      box.setGeometry(static_cast<float>(sbX), thumbY, 6.0f, thumbH);
      box.paint(cr);
      cairo_restore(cr);
    }
  }
}

// Pointer-move handler.

bool settings_color_themes_consume_pointer_move(App& app, int contentX, int contentW) {
  if (app.activeTab != 50) return false;

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const ModePillGeom pg = mode_pill_geom(cardX, cardW);
  const double pyC = app.pointerY + settings_scroll_px(app);

  int nh = -1;
  if (point_in_rect(app.pointerX, pyC, pg.x1, pg.y, pg.w1, pg.h)) nh = 0;
  else if (point_in_rect(app.pointerX, pyC, pg.x2, pg.y, pg.w2, pg.h)) nh = 1;
  else if (point_in_rect(app.pointerX, pyC, pg.x3, pg.y, pg.w3, pg.h)) nh = 2;

  if (nh != app.colorThemesHoverPill) {
    app.colorThemesHoverPill = nh;
    draw(app);
  }
  return true;
}

// Pointer-down handler.

bool settings_color_themes_consume_pointer_down(App& app, int contentX, int contentW) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  float accent_r, accent_g, accent_b;
  settings_resolve_colors(app, accent_r, accent_g, accent_b,
                          accent_r, accent_g, accent_b,
                          accent_r, accent_g, accent_b,
                          accent_r, accent_g, accent_b);

  const double pyC = app.pointerY + settings_scroll_px(app);

  // Mode toggle pills.
  const ModePillGeom pg = mode_pill_geom(cardX, cardW);
  const bool isMatugen = app.settings.matugenThemingEnabled;
  const bool isColorEngine = !app.settings.colorThemeEnabled && !isMatugen;

  // Dynamic-color pill
  if (point_in_rect(app.pointerX, pyC, pg.x1, pg.y, pg.w1, pg.h)) {
    if (!isMatugen) {
      app.settings.matugenThemingEnabled = true;
      app.settings.horizonColorsNative = false;
      app.settings.colorThemeEnabled = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Color Engine pill
  if (point_in_rect(app.pointerX, pyC, pg.x2, pg.y, pg.w2, pg.h)) {
    if (!isColorEngine) {
      app.settings.matugenThemingEnabled = false;
      app.settings.horizonColorsNative = true;
      app.settings.colorThemeEnabled = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Custom Theme pill
  if (point_in_rect(app.pointerX, pyC, pg.x3, pg.y, pg.w3, pg.h)) {
    if (!app.settings.colorThemeEnabled) {
      app.settings.colorThemeEnabled = true;
      app.settings.matugenThemingEnabled = false;
      app.settings.horizonColorsNative = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // If custom theme is not active, no further interactions
  if (!app.settings.colorThemeEnabled) return false;

  // Grouped preset card hits.
  const auto& groups = get_theme_groups();
  if (!groups.empty()) {
    const int nCols = group_grid_cols(cardW);
    const int modeCardH = kModeCardH;
    int nextGroupTop = kCardTop + modeCardH + kCardGap;

    for (const auto& g : groups) {
      const int darkCnt = static_cast<int>(g.darkPresets.size());
      const int lightCnt = static_cast<int>(g.lightPresets.size());
      if (darkCnt == 0 && lightCnt == 0) continue;

      const int gh = group_card_height(darkCnt, lightCnt, nCols);
      const int groupTop = nextGroupTop;

      int contentY = groupTop + 52;

      // Dark grid
      if (darkCnt > 0) {
        contentY += kSubHeaderH;
        const int totalW = nCols * kPresetCardW + (nCols - 1) * kPresetGap;
        const int gridX = cardX + (cardW - totalW) / 2;

        for (int i = 0; i < darkCnt; ++i) {
          const int col = i % nCols;
          const int row = i / nCols;
          const int px = gridX + col * (kPresetCardW + kPresetGap);
          const int py = contentY + row * (kPresetCardH + kPresetGap);
          if (point_in_rect(app.pointerX, pyC, px, py, kPresetCardW, kPresetCardH)) {
            const auto& preset = *g.darkPresets[i];
            app.settings.colorThemeName = std::string(preset.name);
            app.settings.colorThemeSource = std::string(preset.source);
            app.settings.matugenThemingEnabled = false;
            apply_theme_to_app(app, preset.palette);
            save_settings(app.settings);
            draw(app);
            return true;
          }
        }
        const int darkRows = group_grid_rows(darkCnt, nCols);
        contentY += darkRows * kPresetCardH + (darkRows - 1) * kPresetGap;
      }

      // Light grid
      if (lightCnt > 0) {
        contentY += kSubHeaderH;
        const int totalW = nCols * kPresetCardW + (nCols - 1) * kPresetGap;
        const int gridX = cardX + (cardW - totalW) / 2;

        for (int i = 0; i < lightCnt; ++i) {
          const int col = i % nCols;
          const int row = i / nCols;
          const int px = gridX + col * (kPresetCardW + kPresetGap);
          const int py = contentY + row * (kPresetCardH + kPresetGap);
          if (point_in_rect(app.pointerX, pyC, px, py, kPresetCardW, kPresetCardH)) {
            const auto& preset = *g.lightPresets[i];
            app.settings.colorThemeName = std::string(preset.name);
            app.settings.colorThemeSource = std::string(preset.source);
            app.settings.matugenThemingEnabled = false;
            apply_theme_to_app(app, preset.palette);
            save_settings(app.settings);
            draw(app);
            return true;
          }
        }
        const int lightRows = group_grid_rows(lightCnt, nCols);
        contentY += lightRows * kPresetCardH + (lightRows - 1) * kPresetGap;
      }

      nextGroupTop += gh + kCardGap;
    }
  }

  // User theme list hits.
  if (!app.colorThemeList.empty()) {
    int userCardTop = kCardTop + kModeCardH + kCardGap;
    if (!groups.empty()) {
      const int nCols = group_grid_cols(cardW);
      int groupsH = 0;
      for (const auto& g : groups) {
        const int dc = static_cast<int>(g.darkPresets.size());
        const int lc = static_cast<int>(g.lightPresets.size());
        if (dc == 0 && lc == 0) continue;
        groupsH += group_card_height(dc, lc, nCols);
        groupsH += kCardGap;
      }
      if (groupsH > 0) groupsH -= kCardGap;
      userCardTop += groupsH + kCardGap;
    }
    int rowY = userCardTop + 56;
    for (std::size_t i = 0; i < app.colorThemeList.size(); ++i) {
      const auto& e = app.colorThemeList[i];
      const int itemX = cardX + kCardPad;
      const int itemW = cardW - kCardPad * 2;
      if (point_in_rect(app.pointerX, pyC, itemX, rowY, itemW, 38)) {
        app.settings.colorThemeName = e.name;
        app.settings.colorThemeSource = e.source;
        app.settings.matugenThemingEnabled = false;
        if (!e.builtIn && !e.filePath.empty()) {
          UserTheme loaded;
          if (load_theme_from_file(e.filePath, loaded)) {
            apply_theme_to_app(app, loaded.palette);
          }
        }
        save_settings(app.settings);
        draw(app);
        return true;
      }
      rowY += 44;
    }
  }

  return false;
}
