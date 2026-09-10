#include "ux/settings/settings_tab_desktop_widgets/settings_tab_desktop_widgets.hpp"
#include "ux/settings/settings_tab_desktop_widgets/world_clock_popup.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widgets_preferences.hpp"

#include "desktop_shell/common/log/debug_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern void draw(App& app);

static constexpr int kRowH = 56;
static constexpr int kRowGap = 8;
static constexpr int kAddBtnH = 40;
static constexpr int kAddBtnW = 168;
static constexpr int kListPadB = 8;

static constexpr double kToggleW = 52.0;
static constexpr double kToggleH = 26.0;
static constexpr double kRemoveHit = 32.0;
static constexpr double kToggleRemoveGap = 8.0;
static constexpr double kCogHit = 26.0;
static constexpr double kCogGap = 8.0;
static constexpr double kRightPad = 16.0;
static constexpr double kDragHandleW = 48.0;

static int list_content_height(int nwidgets) {
  if (nwidgets <= 0) return 0;
  return nwidgets * kRowH + (nwidgets - 1) * kRowGap;
}

struct DesktopWidgetRowGeom {
  int index = 0;
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
  double toggleX = 0;
  double toggleY = 0;
  double removeX = 0;
  double removeY = 0;
  double cogX = 0;
  double cogY = 0;
};

static std::vector<DesktopWidgetRowGeom> layout_rows(const App&, int contentX, int contentW,
                                                      int listY0, int nwidgets) {
   
  std::vector<DesktopWidgetRowGeom> out;
  const double cardX = static_cast<double>(contentX + 8);
  const double cardW = static_cast<double>(contentW - 16);
  const double rowX = cardX + 24.0;
  const double rowW = cardW - 48.0;
  double yCursor = static_cast<double>(listY0);
  for (int i = 0; i < nwidgets; ++i) {
    DesktopWidgetRowGeom g;
    g.index = i;
    g.x = rowX;
    g.y = yCursor;
    g.w = rowW;
    g.h = kRowH;
    g.removeX = rowX + rowW - kRightPad - kRemoveHit;
    g.removeY = yCursor + static_cast<double>(kRowH) * 0.5 - kRemoveHit * 0.5;
    g.toggleX = g.removeX - kToggleRemoveGap - kToggleW;
    g.toggleY = yCursor + static_cast<double>(kRowH) * 0.5 - kToggleH * 0.5;
    g.cogX = g.toggleX - kCogGap - kCogHit;
    g.cogY = yCursor + static_cast<double>(kRowH) * 0.5 - kCogHit * 0.5;
    out.push_back(g);
    yCursor += static_cast<double>(kRowH + kRowGap);
  }
  return out;
}

static void settings_draw_row_cog_button(App& app, cairo_t* cr, double x, double y, double sz,
                                         bool hover, double glassAlpha) {
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
    box.setColor(r, g, b, static_cast<float>(glassAlpha * (hover ? 0.96 : 0.90)));
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(sz), static_cast<float>(sz));
    box.setGlassy(true);
    box.paint(cr);
  }
  material_symbols_draw_glyph(cr, x + sz * 0.5, y + sz * 0.5 + 0.5, sz * 0.62, "settings",
                              Theme::TextR, Theme::TextG, Theme::TextB,
                              (hover ? 0.9 : 0.52) * glassAlpha);
}

void paint_desktop_widgets_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                               double /*paintPointerYOffset*/) {
   
  auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  const int listH = list_content_height(static_cast<int>(configs.size()));
  const int cardH = 12 + 20 + 8 + listH + 18 + kAddBtnH + kListPadB + 16;

  cairo_save(cr);

  const int cy = kContentTop + 12;

  settings_card(app, cr, cardX, cy, cardW, cardH, glassOv);

  settings_show_text(cr, cardX + kCardPad, cy + 24, "Desktop widgets", 14.f, 700, Theme::TextR, Theme::TextG, Theme::TextB, 0.96f);

  const int listY0 = cy + 12 + 20 + 8;

  auto geoms = layout_rows(app, contentX, contentW, listY0, static_cast<int>(configs.size()));

  for (int i = 0; i < static_cast<int>(geoms.size()); ++i) {
    const auto& g = geoms[i];
    const auto& cfg = configs[i];

    {
      m3::Box box;
      float cr_, cg_, cb_;
      if (app.drawChromeMatugen) {
        cr_ = app.drawChrome.panelFillR;
        cg_ = app.drawChrome.panelFillG;
        cb_ = app.drawChrome.panelFillB;
      } else {
        cr_ = static_cast<float>(Theme::BgR);
        cg_ = static_cast<float>(Theme::BgG);
        cb_ = static_cast<float>(Theme::BgB);
      }
      box.setColor(cr_, cg_, cb_, 0.9f);
      box.setRadius(10.0f);
      box.setGeometry(static_cast<float>(g.x), static_cast<float>(g.y),
                      static_cast<float>(g.w), static_cast<float>(g.h));
      box.paint(cr);
    }
    cairo_round_rect(cr, g.x, g.y, g.w, g.h, 10.0);
    paint_src_glass_hi(app, cr, 0.11);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    settings_draw_drag_handle_row(app, cr, g.x + 8.0, g.y + g.h * 0.5, 1.0);

    const double iw = 32.0;
    const double ix = g.x + 32.0;
    const double iy = g.y + (g.h - iw) * 0.5;
    {
      m3::Box box;
      float cr_, cg_, cb_;
      if (app.drawChromeMatugen) {
        cr_ = app.drawChrome.panelFillR;
        cg_ = app.drawChrome.panelFillG;
        cb_ = app.drawChrome.panelFillB;
      } else {
        cr_ = static_cast<float>(Theme::BgR);
        cg_ = static_cast<float>(Theme::BgG);
        cb_ = static_cast<float>(Theme::BgB);
      }
      box.setColor(cr_, cg_, cb_, 0.05f);
      box.setRadius(8.0f);
      box.setGeometry(static_cast<float>(ix), static_cast<float>(iy),
                      static_cast<float>(iw), static_cast<float>(iw));
      box.paint(cr);
    }
    cairo_round_rect(cr, ix, iy, iw, iw, 8.0);
    paint_src_glass_hi(app, cr, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    double gR = 0.88, gG = 0.93, gB = 0.96;
    if (app.drawChromeMatugen) {
      gR = 0.5 * app.drawChrome.accentR + 0.5 * 0.92;
      gG = 0.5 * app.drawChrome.accentG + 0.5 * 0.95;
      gB = 0.5 * app.drawChrome.accentB + 0.5 * 0.98;
    }

    const std::string wid = cfg.widgetId.empty()
                                ? std::string(desktop_widget_type_to_widget_id(cfg.type))
                                : cfg.widgetId;
    material_symbols_draw_glyph(cr, ix + iw * 0.5, iy + iw * 0.5 + 0.5, 23.0,
                                dock_widget_material_ligature(wid), gR, gG, gB, 0.95);

    const double txtX = ix + iw + 8.0;
    const std::string title = cfg.widgetId.empty()
                                  ? widget_display_title(wid)
                                  : widget_display_title(cfg.widgetId);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13);
    settings_draw_trimmed_text_line(cr, title, txtX, g.y + 32.5, 44, 1.0, 13.f, 700);

    const bool slotOn = app.settings.desktopWidgetSlotsDisabled.find(wid) ==
                        app.settings.desktopWidgetSlotsDisabled.end();
    const double hoverPY = app.pointerY + settings_scroll_px(app);
    const bool hoverToggle = app.pointerX >= g.toggleX && app.pointerX < g.toggleX + kToggleW &&
                              hoverPY >= g.toggleY && hoverPY < g.toggleY + kToggleH;
    settings_draw_widget_accent_toggle(app, cr, g.toggleX, g.toggleY, hoverToggle, 1.0, slotOn);

    if (wid == "world_clock") {
      const bool hoverCog = app.pointerX >= g.cogX && app.pointerX < g.cogX + kCogHit &&
                            hoverPY >= g.cogY && hoverPY < g.cogY + kCogHit;
      settings_draw_row_cog_button(app, cr, g.cogX, g.cogY, kCogHit, hoverCog, 1.0);
    }

    const bool hoverRemove = app.pointerX >= g.removeX && app.pointerX < g.removeX + kRemoveHit &&
                               app.pointerY >= g.removeY && app.pointerY < g.removeY + kRemoveHit;
    settings_draw_row_remove_button(app, cr, g.removeX, g.removeY, kRemoveHit, hoverRemove, 1.0);
  }

  // Add widget button.
  const int addBtnY = cy + cardH - 16 - kAddBtnH - kListPadB;
  const double abx = cardX + 24.0;
  const double aby = static_cast<double>(addBtnY);
  const double hoverPY = app.pointerY + settings_scroll_px(app);
  const bool hoverAdd = app.pointerX >= abx && app.pointerX < abx + kAddBtnW &&
                         hoverPY >= aby && hoverPY < aby + kAddBtnH;

  {
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button addWidgetBtn;
    addWidgetBtn.setMinSize(0, 0);
    addWidgetBtn.setGlyph("add");
    addWidgetBtn.setLabel("Add widget");
    addWidgetBtn.setGeometry(static_cast<float>(abx), static_cast<float>(aby),
                              static_cast<float>(kAddBtnW), static_cast<float>(kAddBtnH));
    addWidgetBtn.setSize(m3::Button::Size::M);
    addWidgetBtn.setStyle(m3::Button::Style::Outlined);
    addWidgetBtn.setAccentColor(a_r, a_g, a_b);
    addWidgetBtn.setOutlineColor(o_r, o_g, o_b);
    addWidgetBtn.setHovered(hoverAdd);
    addWidgetBtn.paint(cr);
  }

  cairo_restore(cr);
}

bool settings_desktop_widgets_handle_remove_click(App& app, int contentX, int contentW) {
   
  auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
  const int cy = kContentTop + 12;
  const int listY0 = cy + 12 + 20 + 8;
  const double py = app.pointerY + settings_scroll_px(app);

  auto geoms = layout_rows(app, contentX, contentW, listY0, static_cast<int>(configs.size()));
  for (const auto& g : geoms) {
    if (app.pointerX >= g.removeX && app.pointerX < g.removeX + kRemoveHit &&
        py >= g.removeY && py < g.removeY + kRemoveHit) {
      configs.erase(configs.begin() + static_cast<ptrdiff_t>(g.index));
      eh::shell::desktop::desktop_widgets_prefs_save(configs);
      app.settings.desktopWidgets = configs;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }
  return false;
}

bool settings_desktop_widgets_handle_toggle_click(App& app, int contentX, int contentW) {
   
  auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
  const int cy = kContentTop + 12;
  const int listY0 = cy + 12 + 20 + 8;
  const double py = app.pointerY + settings_scroll_px(app);

  auto geoms = layout_rows(app, contentX, contentW, listY0, static_cast<int>(configs.size()));
  for (const auto& g : geoms) {
    if (app.pointerX >= g.toggleX && app.pointerX < g.toggleX + kToggleW &&
        py >= g.toggleY && py < g.toggleY + kToggleH) {
      if (g.index < static_cast<int>(configs.size())) {
        const std::string wid = configs[g.index].widgetId.empty()
                                    ? desktop_widget_type_to_widget_id(configs[g.index].type)
                                    : configs[g.index].widgetId;
        if (app.settings.desktopWidgetSlotsDisabled.find(wid) !=
            app.settings.desktopWidgetSlotsDisabled.end()) {
          app.settings.desktopWidgetSlotsDisabled.erase(wid);
        } else {
          app.settings.desktopWidgetSlotsDisabled.insert(wid);
        }
        save_settings(app.settings);
        draw(app);
        return true;
      }
    }
  }
  return false;
}

bool settings_desktop_widgets_handle_settings_click(App& app, int contentX, int contentW) {
  auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
  const int cy = kContentTop + 12;
  const int listY0 = cy + 12 + 20 + 8;
  const double py = app.pointerY + settings_scroll_px(app);

  auto geoms = layout_rows(app, contentX, contentW, listY0, static_cast<int>(configs.size()));
  for (const auto& g : geoms) {
    if (app.pointerX >= g.cogX && app.pointerX < g.cogX + kCogHit &&
        py >= g.cogY && py < g.cogY + kCogHit) {
      if (g.index < static_cast<int>(configs.size())) {
        const std::string wid = configs[g.index].widgetId.empty()
                                    ? desktop_widget_type_to_widget_id(configs[g.index].type)
                                    : configs[g.index].widgetId;
        if (wid == "world_clock") world_clock_popup_open(app);
      }
      return true;
    }
  }
  return false;
}

bool settings_desktop_widgets_handle_add_click(App& app, int contentX, int) {   
  auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
  const int cy = kContentTop + 12;
  const int listH = list_content_height(static_cast<int>(configs.size()));
  const int cardH = 12 + 20 + 8 + listH + 18 + kAddBtnH + kListPadB + 16;

  const int addBtnY = cy + cardH - 16 - kAddBtnH - kListPadB;
  const double abx = static_cast<double>(contentX + 8 + 24);
  const double aby = static_cast<double>(addBtnY);
  const double py = app.pointerY + settings_scroll_px(app);
  if (app.pointerX >= abx && app.pointerX < abx + kAddBtnW &&
      py >= aby && py < aby + kAddBtnH) {
    debug_log("settings", "desktop_widgets: opening widget picker");
  app.widgetPickerOpen = true;
    app.widgetPickerForDesktop = true;
    app.widgetPickerSection = "";
    app.widgetPickerFilter.clear();
    app.widgetPickerHoverSlot = -1;
    draw(app);
    return true;
  }
  return false;
}
