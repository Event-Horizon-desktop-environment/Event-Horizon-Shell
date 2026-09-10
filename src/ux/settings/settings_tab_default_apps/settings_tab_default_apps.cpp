#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_default_apps/settings_tab_default_apps.hpp"
#include "ux/settings/data/default_apps/settings_default_apps.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "xdg-shell-client-protocol.h"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "ux/settings/settings_serialize.hpp"

extern void draw(App& app);

static constexpr int kDefaultAppsDdRowH = 48;

// Internal helpers.

static void fill_da_surface_card(App& app, cairo_t* cr, double cx, double cy, double cw, double ch, double glassOv) {
   
  const double s = std::clamp(static_cast<double>(app.height) * 0.0011, 0.9, 1.35);
  const double cardRad = std::min(20.0, kCardRad * std::clamp(s, 0.9, 1.35));
  {
    m3::Box box;
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
    box.setColor(r, g, b, static_cast<float>(0.72 * glassOv));
    box.setRadius(static_cast<float>(cardRad));
    box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                    static_cast<float>(cw), static_cast<float>(ch));
    box.setGlassy(true);
    box.paint(cr);
  }
}

static double default_apps_pill_icon_draw_px(double category_icon_px) {
  return std::clamp(category_icon_px * 0.72, 16.0, 24.0);
}

static void default_apps_ellipsis_fit(cairo_t* cr, std::string* text, double maxAdvance) {
   
  cairo_text_extents_t te{};
  if (text->empty()) return;
  cairo_text_extents(cr, text->c_str(), &te);
  if (te.x_advance <= maxAdvance) return;
  std::string base = *text;
  while (!base.empty()) {
    std::string trial = base;
    trial += "\u2026";
    cairo_text_extents(cr, trial.c_str(), &te);
    if (te.x_advance <= maxAdvance) {
      *text = std::move(trial);
      return;
    }
    while (!base.empty() && (static_cast<unsigned char>(base.back()) & 0xC0) == 0x80) base.pop_back();
    if (!base.empty()) base.pop_back();
  }
  *text = "\u2026";
}

// Note: originally … was used as a literal; we use the Unicode ellipsis string above.

static void default_apps_compute_pill_geometry(App& app, cairo_t* cr,
                                               const eh::settings::default_apps::DefaultAppsLayout& daLay, int r,
                                               double winH, int* out_ddx, int* out_ddy, int* out_ddw, int* out_ddh,
                                               std::string* out_show) {
   
  const double s = daLay.ui_scale;
  const int innerLeft = daLay.apps_x + daLay.inner_pad;
  const int innerRight = daLay.apps_x + daLay.apps_w - daLay.inner_pad;
  const int marginS = std::max(6, static_cast<int>(std::lround(8.0 * s)));
  const int rowTop = daLay.first_row_y + r * daLay.row_h;

  const double catIconPx = std::clamp(winH * 0.04, 18.0, 40.0);
  const double kCatIconSlotW = std::max(catIconPx + 10.0, 28.0 * s);
  const double kCatIconLabelGap = std::max(8.0, 10.0 * s);
  const double kPillIconTextGap = std::max(5.0, 7.0 * s);
  const double kPillPadL = std::max(6.0, 9.0 * s);
  const double kPillChevronInset = std::max(9.0, 12.0 * s);
  const double rowFont = std::clamp(13.0 * std::min(s, 1.5), 11.0, 22.0);

  const std::string& saved = *default_apps_saved_field(app, r);
  std::string show =
      saved.empty() ? std::string("System default") : eh::settings::default_apps::friendly_line_for_desktop_id(saved);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, rowFont);
  cairo_font_extents_t fe_pill{};
  cairo_font_extents(cr, &fe_pill);
  const double textEmH = fe_pill.ascent + fe_pill.descent;
  const double coreH = textEmH;
  const int padVY = std::max(3, static_cast<int>(std::lround(4.0 * s)));
  int pill_h = static_cast<int>(std::ceil(coreH)) + 2 * padVY;
  pill_h = std::clamp(pill_h, 22, std::max(22, daLay.row_h - 2));
  *out_ddy = rowTop + (daLay.row_h - pill_h) / 2;
  *out_ddh = pill_h;

  cairo_text_extents_t te_cat{};
  const char* catStr = eh::settings::default_apps::category_label(r);
  cairo_text_extents(cr, catStr, &te_cat);

  const double labelStart = static_cast<double>(innerLeft) + kCatIconSlotW + kCatIconLabelGap;
  const double catEnd = labelStart + te_cat.x_advance;
  const double minGap = std::max(10.0, 8.0 * s);
  const double pillRight = static_cast<double>(innerRight - marginS);
  const double maxPillLeft = catEnd + minGap;
  const double maxW = std::max(0.0, pillRight - maxPillLeft);

  const double pillIconDrawPxGeo = default_apps_pill_icon_draw_px(catIconPx);
  const double leftChrome = kPillPadL + pillIconDrawPxGeo + kPillIconTextGap;
  const double rightChrome = kPillChevronInset + 20.0;

  m3::Label measPillLbl;
  measPillLbl.setFontSize(static_cast<float>(rowFont));
  measPillLbl.setText(show.c_str());
  float measPw, measPh;
  measPillLbl.measureExtents(measPw, measPh);
  const double naturalW = leftChrome + static_cast<double>(measPw) + rightChrome;

  double pill_w;
  if (naturalW <= maxW) {
    pill_w = naturalW;
  } else {
    double textMax = maxW - leftChrome - rightChrome;
    if (textMax < 3.0) textMax = 3.0;
    while (!show.empty()) {
      std::string trial = show;
      trial += "\u2026";
      measPillLbl.setText(trial.c_str());
      float tw, th;
      measPillLbl.measureExtents(tw, th);
      if (static_cast<double>(tw) <= textMax) {
        show = std::move(trial);
        break;
      }
      while (!show.empty() && (static_cast<unsigned char>(show.back()) & 0xC0) == 0x80) show.pop_back();
      if (!show.empty()) show.pop_back();
    }
    if (show.empty()) show = "\u2026";
    measPillLbl.setText(show.c_str());
    measPillLbl.measureExtents(measPw, measPh);
    pill_w = leftChrome + static_cast<double>(measPw) + rightChrome;
    if (pill_w > maxW) pill_w = maxW;
  }

  double ddx_d = pillRight - pill_w;
  if (ddx_d < maxPillLeft) ddx_d = maxPillLeft;

  *out_ddx = static_cast<int>(std::floor(ddx_d));
  *out_ddw = std::max(1, static_cast<int>(std::ceil(pillRight - ddx_d)));
  *out_show = std::move(show);
}

static int default_app_picker_selected_idx(const App& app) {
   
  const std::string* const saved = default_apps_saved_field(app, app.defaultAppPickerCategory);
  if (!saved || saved->empty()) return 0;
  for (size_t i = 0; i < app.defaultAppPickerEntries.size(); ++i) {
    std::string base = app.defaultAppPickerEntries[i].path;
    if (const size_t sl = base.rfind('/'); sl != std::string::npos) base = base.substr(sl + 1);
    if (base == *saved) return static_cast<int>(i + 1);
  }
  return -1;
}

static void settings_paint_default_apps_pill(App& app, cairo_t* cr, int bx, int by, int bw, int bh, double glassOv,
                                             const char* displayText, bool expanded,
                                             const eh::icons::IconEntry* iconEnt, double textFontPx, double iconDrawPx,
                                             double padL, double iconTextGap, double chevronInset) {
   
  const bool hovered =
      app.pointerX >= bx && app.pointerY >= by && app.pointerX < bx + bw && app.pointerY < by + bh;
  const double rad = std::clamp(static_cast<double>(bh) * 0.28, 5.0, 9.0);
  {
    m3::Box box;
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
    box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
    box.setRadius(static_cast<float>(rad));
    box.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(bw), static_cast<float>(bh));
    box.paint(cr);
  }
  cairo_round_rect(cr, bx, by, bw, bh, rad);
  paint_src_glass_hi(app, cr, 0.11 * glassOv);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double midY = static_cast<double>(by) + static_cast<double>(bh) * 0.5;

  double xCur = static_cast<double>(bx) + padL;
  if (iconEnt && iconEnt->surface) {
    const double iw = static_cast<double>(iconEnt->width);
    const double ih = static_cast<double>(iconEnt->height);
    const double sc = iconDrawPx / std::max(1.0, std::max(iw, ih));
    const double ix = xCur;
    const double iy = midY - iconDrawPx * 0.5;
    cairo_save(cr);
    cairo_translate(cr, ix, iy);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, iconEnt->surface, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    xCur += iconDrawPx + iconTextGap;
  }

  m3::Label pillTxt;
  pillTxt.setFontSize(static_cast<float>(textFontPx));
  pillTxt.setFontWeight(400);
  pillTxt.setColor(Theme::TextR, Theme::TextG, Theme::TextB, static_cast<float>(1.0 * glassOv));
  pillTxt.setText(displayText);
  float pTw, pTh;
  pillTxt.measureExtents(pTw, pTh);
  pillTxt.paintAt(cr, static_cast<float>(xCur), static_cast<float>(midY) - pTh * 0.5f);

  const double chevPx = std::clamp(iconDrawPx * 0.92, 14.0, 20.0);
  const double chevAlpha = hovered ? 0.95 * glassOv : 0.55 * glassOv;
  material_symbols_draw_glyph(cr, static_cast<double>(bx + bw) - chevronInset, midY, chevPx,
                              expanded ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB,
                              chevAlpha);
}

static void settings_paint_default_app_picker_list(App& app, cairo_t* cr, int x, int y, int w, int viewH,
                                                   int rowCount, double glassOv, int scrollPx) {
  (void)glassOv;
   
  const int fullH = rowCount * kDefaultAppsDdRowH;
  const int listH = (viewH > 0) ? viewH : fullH;
  {
    m3::Box box;
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
    box.setColor(r, g, b, 1.0f);
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(listH));
    box.paint(cr);
  }
  cairo_round_rect(cr, x, y, w, listH, 8.0);
  paint_src_glass_hi(app, cr, 0.28);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const int selIdx = default_app_picker_selected_idx(app);
  const int hoverIdx = app.defaultAppPickerPopupHoverIdx;
  constexpr double kPadL = 16.0;
  constexpr double kIconColW = 24.0;
  constexpr double kIconGap = 8.0;
  constexpr double kTextSize = 13.0;
  constexpr double kGlyphPx = 20.0;
  constexpr double kRightPad = 16.0;
  const double textStartX = static_cast<double>(x) + kPadL + kIconColW + kIconGap;
  const double textMaxW = static_cast<double>(w) - kPadL - kIconColW - kIconGap - kRightPad;

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kTextSize);

  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, listH);
  cairo_clip(cr);
  for (int i = 0; i < rowCount; ++i) {
    const int ry = y + i * kDefaultAppsDdRowH - scrollPx;
    if (ry + kDefaultAppsDdRowH <= y || ry >= y + listH) continue;
    if (i == hoverIdx) {
      cairo_rectangle(cr, x + kPadL, ry, w - kPadL - 1, kDefaultAppsDdRowH);
      paint_src_glass_hi(app, cr, 0.16);
      cairo_fill(cr);
    }
    const bool sel = i == selIdx;
    const double midY = static_cast<double>(ry) + static_cast<double>(kDefaultAppsDdRowH) * 0.5;
    const double rowAlpha = sel ? 1.0 : (i == hoverIdx ? 0.92 : 0.82);

    if (i == 0) {
      material_symbols_draw_glyph(cr, static_cast<double>(x) + kPadL + kIconColW * 0.5, midY, kGlyphPx, "restart_alt",
                                  Theme::TextR, Theme::TextG, Theme::TextB, rowAlpha);
    } else {
      const eh::app_drawer::DesktopEntry& e = app.defaultAppPickerEntries[static_cast<size_t>(i - 1)];
      std::string base = e.path;
      if (const size_t sl = base.rfind('/'); sl != std::string::npos) base = base.substr(sl + 1);
      const eh::icons::IconEntry* ie = app.icons.app_icon(base);
      if (ie && ie->surface) {
        const double isz = kGlyphPx;
        const double iw = static_cast<double>(ie->width);
        const double ih = static_cast<double>(ie->height);
        const double sc = isz / std::max(1.0, std::max(iw, ih));
        cairo_save(cr);
        cairo_translate(cr, static_cast<double>(x) + kPadL + (kIconColW - isz) * 0.5, midY - isz * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, ie->surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
      } else {
        material_symbols_draw_glyph(cr, static_cast<double>(x) + kPadL + kIconColW * 0.5, midY, kGlyphPx, "apps",
                                    Theme::TextR, Theme::TextG, Theme::TextB, rowAlpha);
      }
    }

    m3::Label txtLbl;
    txtLbl.setFontSize(static_cast<float>(kTextSize));
    txtLbl.setFontWeight(sel ? 700 : 400);
    txtLbl.setColor(Theme::TextR, Theme::TextG, Theme::TextB, static_cast<float>(rowAlpha));
    float txtTw, txtTh;
    const std::string& labelStr = app.defaultAppPickerComboLabels[static_cast<size_t>(i)];
    txtLbl.setText(labelStr.c_str());
    txtLbl.measureExtents(txtTw, txtTh);
    if (textMaxW > 0.0 && txtTw > static_cast<float>(textMaxW)) {
      std::string truncated = labelStr;
      default_apps_ellipsis_fit(cr, &truncated, textMaxW);
      txtLbl.setText(truncated.c_str());
      txtLbl.measureExtents(txtTw, txtTh);
    }
    txtLbl.paintAt(cr, static_cast<float>(textStartX), static_cast<float>(midY) - txtTh * 0.5f);
  }
  cairo_restore(cr);
}

// Exported functions (called from settings_app.cpp).

const std::string* default_apps_saved_field(const App& app, int r) {
   
  switch (r) {
    case 0: return &app.settings.defaultApps.web;
    case 1: return &app.settings.defaultApps.mail;
    case 2: return &app.settings.defaultApps.calendar;
    case 3: return &app.settings.defaultApps.fileManager;
    case 4: return &app.settings.defaultApps.terminal;
    case 5: return &app.settings.defaultApps.music;
    case 6: return &app.settings.defaultApps.video;
    case 7: return &app.settings.defaultApps.images;
    case 8: return &app.settings.defaultApps.pdf;
    default: return &app.settings.defaultApps.web;
  }
}

void settings_fill_default_app_picker(App& app) {
   
  app.defaultAppPickerEntries = eh::app_drawer::copy_desktop_entries();
  auto& v = app.defaultAppPickerEntries;
  v.erase(std::remove_if(v.begin(), v.end(),
                         [](const eh::app_drawer::DesktopEntry& e) { return e.noDisplay || e.hidden; }),
          v.end());
  eh::settings::default_apps::filter_picker_entries_for_category(app.defaultAppPickerCategory, &v);
  std::sort(v.begin(), v.end(), [](const eh::app_drawer::DesktopEntry& a, const eh::app_drawer::DesktopEntry& b) {
    return a.name < b.name;
  });
  const int nPick = static_cast<int>(app.defaultAppPickerEntries.size());
  app.defaultAppPickerComboLabels.clear();
  app.defaultAppPickerComboLabels.reserve(static_cast<size_t>(nPick) + 1);
  app.defaultAppPickerComboLabels.emplace_back("Use system default (clear stored choice)");
  for (const auto& e : v) {
    std::string base = e.path;
    if (const size_t sl = base.rfind('/'); sl != std::string::npos) base = base.substr(sl + 1);
    std::string line =
        !e.name.empty() ? e.name : eh::settings::default_apps::friendly_line_for_desktop_id(base);
    if (line.size() > 120) line.resize(117), line += "\u2026";
    app.defaultAppPickerComboLabels.push_back(std::move(line));
  }
  app.defaultAppPickerScrollPx = 0;
}

void default_app_picker_teardown_layer(App& app) {
   
  app.defaultAppPickerBuf[0].set_release_hook(nullptr, nullptr);
  app.defaultAppPickerBuf[1].set_release_hook(nullptr, nullptr);
  app.defaultAppPickerBuf[0].destroy();
  app.defaultAppPickerBuf[1].destroy();
  app.defaultAppPickerSurfExt.destroy();
  if (app.defaultAppPickerXdgPopup) {
    xdg_popup_destroy(app.defaultAppPickerXdgPopup);
    app.defaultAppPickerXdgPopup = nullptr;
  }
  if (app.defaultAppPickerXdgChildSurface) {
    xdg_surface_destroy(app.defaultAppPickerXdgChildSurface);
    app.defaultAppPickerXdgChildSurface = nullptr;
  }
  if (app.defaultAppPickerXdgWlSurface) {
    wl_surface_destroy(app.defaultAppPickerXdgWlSurface);
    app.defaultAppPickerXdgWlSurface = nullptr;
  }
  if (app.defaultAppPickerLayer) {
    zwlr_layer_surface_v1_destroy(app.defaultAppPickerLayer);
    app.defaultAppPickerLayer = nullptr;
  }
  if (app.defaultAppPickerLayerWlSurface) {
    wl_surface_destroy(app.defaultAppPickerLayerWlSurface);
    app.defaultAppPickerLayerWlSurface = nullptr;
  }
  app.defaultAppPickerLayerW = 0;
  app.defaultAppPickerLayerH = 0;
  app.defaultAppPickerXdgGeomValid = false;
  app.defaultAppPickerPopupRelToParentX = 0;
  app.defaultAppPickerPopupRelToParentY = 0;
}

bool default_app_picker_popup_geom(const App& app, int contentX, int contentW, int* out_lx, int* out_ly,
                                   int* out_lw, int* out_view_h, int* out_row_h, int* out_n_rows,
                                   int* out_max_scroll) {
   
  if (!app.defaultAppPickerOpen || app.activeTab != 10) return false;
  eh::settings::default_apps::DefaultAppsLayout daLay{};
  eh::settings::default_apps::compute_default_apps_layout(contentX, contentW, kContentTop,
                                                          settings_content_viewport_h(app), &daLay);
  int ax = 0;
  int ay = 0;
  int aw = 0;
  int ah = 0;
  if (app.defaultAppsPillRectValid) {
    const int c = app.defaultAppPickerCategory;
    ax = app.defaultAppsPillRect[c][0];
    ay = app.defaultAppsPillRect[c][1];
    aw = app.defaultAppsPillRect[c][2];
    ah = app.defaultAppsPillRect[c][3];
  } else {
    eh::settings::default_apps::default_apps_dropdown_rect(daLay, app.defaultAppPickerCategory, &ax, &ay, &aw, &ah);
  }
  const int nApps = static_cast<int>(app.defaultAppPickerEntries.size());
  const int nRows = nApps + 1;
  const int pickerRowH = kDefaultAppsDdRowH;
  const int minListW = 236;
  int listW = std::max(aw, minListW);
  const int lx = std::clamp(ax, 8, std::max(8, app.width - listW - 8));
  listW = std::min(listW, std::max(minListW, app.width - lx - 8));

  const int ly = ay + ah + 2;
  constexpr int kBottomMargin = 8;
  const int spaceBelow = std::max(0, app.height - kBottomMargin - ly);
  int maxRowsFit = (pickerRowH > 0) ? (spaceBelow / pickerRowH) : 10;
  maxRowsFit = std::max(1, maxRowsFit);
  constexpr int kDefaultAppPickerMaxVisRows = 10;
  int visRows = std::min({nRows, kDefaultAppPickerMaxVisRows, maxRowsFit});
  visRows = std::max(1, visRows);
  int viewH = visRows * pickerRowH;
  const int totalH = nRows * pickerRowH;
  const int maxScroll = std::max(0, totalH - viewH);
  *out_lx = lx;
  *out_ly = ly;
  *out_lw = listW;
  *out_view_h = viewH;
  *out_row_h = pickerRowH;
  *out_n_rows = nRows;
  *out_max_scroll = maxScroll;
  return true;
}

int default_apps_hit_pill_row(const App& app, double px, double py,
                              const eh::settings::default_apps::DefaultAppsLayout& daLay) {
   
  if (app.defaultAppsPillRectValid) {
    for (int r = 0; r < eh::settings::default_apps::kNumCategories; ++r) {
      if (point_in_rect(px, py, app.defaultAppsPillRect[r][0], app.defaultAppsPillRect[r][1],
                        app.defaultAppsPillRect[r][2], app.defaultAppsPillRect[r][3]))
        return r;
    }
    return -1;
  }
  int row = -1;
  eh::settings::default_apps::hit_row_control(px, py, daLay, &row, nullptr);
  return row;
}

// Paint.

void paint_default_apps_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  eh::settings::default_apps::DefaultAppsLayout daLay{};
  eh::settings::default_apps::compute_default_apps_layout(contentX, contentW, kContentTop,
                                                          settings_content_viewport_h(app), &daLay);
  app.defaultAppsPillRectValid = false;

  const double s = daLay.ui_scale;
  const double winH = static_cast<double>(app.height);

  fill_da_surface_card(app, cr, static_cast<double>(daLay.header_x), static_cast<double>(daLay.header_y),
                       static_cast<double>(daLay.header_w), static_cast<double>(daLay.header_h), glassOv);
  {
    const double hx = static_cast<double>(daLay.header_x);
    const double hy = static_cast<double>(daLay.header_y);
    const double hdrIcon = std::clamp(winH * 0.035, 20.0, 44.0);
    const double iconCx = hx + std::max(20.0, 22.0 * s);
    material_symbols_draw_glyph(cr, iconCx, hy + static_cast<double>(daLay.header_h) * 0.5, hdrIcon, "apps",
                                Theme::AccR, Theme::AccG, Theme::AccB, 1.0 * glassOv);
    const double titleLeft = iconCx + hdrIcon * 0.55 + 8.0 * s;
    const double titleFs = std::clamp(18.0 * std::min(s, 1.45), 14.0, 30.0);
    const double subFs = std::clamp(11.0 * std::min(s, 1.45), 10.0, 18.0);
    settings_show_text(cr, titleLeft, hy + static_cast<double>(daLay.header_h) * 0.38, "Default Applications", titleFs, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    settings_show_text(cr, titleLeft, hy + static_cast<double>(daLay.header_h) * 0.72,
                    "Configure default applications for file types and actions. Changes apply when you pick an app.", subFs, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
  }

  fill_da_surface_card(app, cr, static_cast<double>(daLay.apps_x), static_cast<double>(daLay.apps_y),
                       static_cast<double>(daLay.apps_w), static_cast<double>(daLay.apps_h), glassOv);

  const int innerLeft = daLay.apps_x + daLay.inner_pad;
  const double catIconPx = std::clamp(winH * 0.04, 18.0, 40.0);
  const double kCatIconSlotW = std::max(catIconPx + 10.0, 28.0 * s);
  const double kCatIconLabelGap = std::max(8.0, 10.0 * s);
  const double rowFont = std::clamp(13.0 * std::min(s, 1.5), 11.0, 22.0);
  const double kPillPadLPaint = std::max(6.0, 9.0 * s);
  const double kPillIconTextGapPaint = std::max(5.0, 7.0 * s);
  const double kPillChevronInsetPaint = std::max(9.0, 12.0 * s);
  const double pillIconDrawPx = default_apps_pill_icon_draw_px(catIconPx);

  for (int r = 0; r < eh::settings::default_apps::kNumCategories; ++r) {
    const int rowTop = daLay.first_row_y + r * daLay.row_h;
    if (r < eh::settings::default_apps::kNumCategories - 1) {
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
      cairo_rectangle(cr, static_cast<double>(innerLeft), static_cast<double>(rowTop + daLay.row_h - 1),
                      static_cast<double>(daLay.apps_w - 2 * daLay.inner_pad), 1.0);
      cairo_fill(cr);
    }

    const double mid_y = static_cast<double>(rowTop) + static_cast<double>(daLay.row_h) * 0.5;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, rowFont);
    cairo_font_extents_t fe_row{};
    cairo_font_extents(cr, &fe_row);

    const double row_text_baseline = mid_y - (fe_row.descent - fe_row.ascent) * 0.5;
    const double shared_text_vc_y = row_text_baseline + (fe_row.descent - fe_row.ascent) * 0.5;

    material_symbols_draw_glyph(cr, static_cast<double>(innerLeft) + kCatIconSlotW * 0.5, shared_text_vc_y,
                                catIconPx, eh::settings::default_apps::category_material_icon(r), Theme::AccR,
                                Theme::AccG, Theme::AccB, 0.92 * glassOv);

    settings_show_text(cr, static_cast<double>(innerLeft) + kCatIconSlotW + kCatIconLabelGap, row_text_baseline, eh::settings::default_apps::category_label(r), rowFont, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);

    int ddx = 0;
    int ddy = 0;
    int ddw = 0;
    int ddh = 0;
    std::string show;
    default_apps_compute_pill_geometry(app, cr, daLay, r, winH, &ddx, &ddy, &ddw, &ddh, &show);
    app.defaultAppsPillRect[r][0] = ddx;
    app.defaultAppsPillRect[r][1] = ddy;
    app.defaultAppsPillRect[r][2] = ddw;
    app.defaultAppsPillRect[r][3] = ddh;

    const bool expanded = app.defaultAppPickerOpen && app.defaultAppPickerCategory == r;
    const std::string& savedPill = *default_apps_saved_field(app, r);
    const eh::icons::IconEntry* pillIconEnt = savedPill.empty() ? nullptr : app.icons.app_icon(savedPill);
    settings_paint_default_apps_pill(app, cr, ddx, ddy, ddw, ddh, glassOv, show.c_str(), expanded, pillIconEnt,
                                     rowFont, pillIconDrawPx, kPillPadLPaint, kPillIconTextGapPaint,
                                     kPillChevronInsetPaint);
  }
  app.defaultAppsPillRectValid = true;
}

void settings_paint_default_app_picker_popup(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  int lx = 0;
  int ly = 0;
  int lw = 0;
  int vh = 0;
  int rh = 0;
  int nRows = 0;
  int maxScroll = 0;
  if (!default_app_picker_popup_geom(app, contentX, contentW, &lx, &ly, &lw, &vh, &rh, &nRows, &maxScroll)) return;
  const int nLab = static_cast<int>(app.defaultAppPickerComboLabels.size());
  if (nLab != nRows || nRows <= 0) return;
  settings_paint_default_app_picker_list(app, cr, lx, ly, lw, vh, nRows, glassOv,
                                         app.defaultAppPickerScrollPx);
}

// Pointer down.

bool settings_default_apps_consume_pointer_down(App& app, int contentX, int contentW) {
   
  eh::settings::default_apps::DefaultAppsLayout daLay{};
  eh::settings::default_apps::compute_default_apps_layout(contentX, contentW, kContentTop,
                                                          settings_content_viewport_h(app), &daLay);
  const int row = default_apps_hit_pill_row(app, app.pointerX, app.pointerY, daLay);
  if (row >= 0) {
    if (app.defaultAppPickerOpen && app.defaultAppPickerCategory == row) {
      app.defaultAppPickerOpen = false;
      app.defaultAppPickerPopupHoverIdx = -1;
      draw(app);
      return true;
    }
    app.defaultAppPickerCategory = row;
    settings_fill_default_app_picker(app);
    app.defaultAppsDropdownHoverRow = -1;
    app.defaultAppPickerPopupHoverIdx = -1;
    app.defaultAppPickerOpen = true;
    draw(app);
    return true;
  }
  return false;
}
