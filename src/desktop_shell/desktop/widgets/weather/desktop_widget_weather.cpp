#include "desktop_shell/desktop/widgets/weather/desktop_widget_weather.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <pango/pangocairo.h>

namespace eh::shell::desktop {

DesktopWeatherWidget::DesktopWeatherWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {}

void DesktopWeatherWidget::create() {}

void DesktopWeatherWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const eh::shell::dock::control_center::ControlCenterWeatherState ws =
      eh::shell::dock::control_center::control_center_weather_state(sc, m_widgetId);

  const bool ok = ws.available;
  const double dimA = ok ? 1.0 : 0.45;

  auto make_pango = [&](const char* descStr) {
    PangoLayout* lay = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string(descStr);
    pango_layout_set_font_description(lay, desc);
    pango_font_description_free(desc);
    return lay;
  };

  std::string city;
  if (!ws.location.empty()) {
    const auto comma = ws.location.find(',');
    city = (comma == std::string::npos) ? ws.location : ws.location.substr(0, comma);
  }
  char tempBuf[16], hlBuf[32];
  if (ok) {
    std::snprintf(tempBuf, sizeof(tempBuf), "%d", ws.temp);
    std::snprintf(hlBuf, sizeof(hlBuf), "H:%d°  L:%d°", ws.hi, ws.lo);
  } else {
    std::snprintf(tempBuf, sizeof(tempBuf), "--");
    std::snprintf(hlBuf, sizeof(hlBuf), "--");
  }
  char unitBuf[8];
  std::snprintf(unitBuf, sizeof(unitBuf), "\u00b0%c", ws.fahrenheit ? 'F' : 'C');

  PangoLayout* cityL = make_pango("Inter SemiBold 13");
  pango_layout_set_text(cityL, city.c_str(), -1);
  int cityW = 0, cityH = 0;
  pango_layout_get_pixel_size(cityL, &cityW, &cityH);

  PangoLayout* tempL = make_pango("Inter Light 52");
  pango_layout_set_text(tempL, tempBuf, -1);
  int tempW = 0, tempH = 0;
  pango_layout_get_pixel_size(tempL, &tempW, &tempH);

  PangoLayout* unitL = make_pango("Inter 15");
  pango_layout_set_text(unitL, unitBuf, -1);
  int unitW = 0, unitH = 0;
  pango_layout_get_pixel_size(unitL, &unitW, &unitH);

  PangoLayout* condL = make_pango("Inter 13");
  pango_layout_set_text(condL, ok ? ws.condition.c_str() : ws.status_text.c_str(), -1);
  int condW = 0, condH = 0;
  pango_layout_get_pixel_size(condL, &condW, &condH);

  PangoLayout* hlL = make_pango("Inter 13");
  pango_layout_set_text(hlL, hlBuf, -1);
  int hlW = 0, hlH = 0;
  pango_layout_get_pixel_size(hlL, &hlW, &hlH);

  const double pad = 18.0;
  const double iconPx = 54.0;
  const double textW = static_cast<double>(std::max({tempW + unitW, condW, hlW, cityW}));
  m_width = static_cast<int>(std::ceil(pad * 2.0 + textW + 12.0 + iconPx));
  m_height = static_cast<int>(std::ceil(pad * 2.0 + (city.empty() ? 0.0 : cityH + 6.0) +
                                        tempH + 4.0 + condH + 4.0 + hlH));

  cairo_save(cr);
  eh::shell::shared::rounded_rect(cr, 0, 0, m_width, m_height, 24.0);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard * dimA);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  auto show = [&](PangoLayout* l, double x, double y, double r, double g, double b, double a) {
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, r, g, b, a * dimA);
    pango_cairo_show_layout(cr, l);
  };

  double y = pad;
  if (!city.empty()) {
    show(cityL, pad, y, 1.0, 1.0, 1.0, 0.75);
    y += cityH + 6.0;
  }
  show(tempL, pad, y, 1.0, 1.0, 1.0, 0.95);
  show(unitL, pad + tempW + 2.0, y + tempH - unitH - 2.0, 1.0, 1.0, 1.0, 0.80);
  y += tempH + 4.0;
  show(condL, pad, y, 1.0, 1.0, 1.0, 0.70);
  y += condH + 4.0;
  show(hlL, pad, y, 1.0, 1.0, 1.0, 0.70);

  const double iconCx = pad + textW + 12.0 + iconPx * 0.5;
  const double iconCy = pad + cityH + 6.0 + tempH * 0.5;
  const char* glyph = ok && !ws.icon.empty() ? ws.icon.c_str() : "cloud_off";
  cairo_new_path(cr);
  cairo_arc(cr, iconCx, iconCy, iconPx * 0.5, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.14 * dimA);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, iconCx, iconCy, iconPx, glyph,
                      mc.accentR, mc.accentG, mc.accentB, 0.95 * dimA);

  g_object_unref(cityL);
  g_object_unref(tempL);
  g_object_unref(unitL);
  g_object_unref(condL);
  g_object_unref(hlL);
  cairo_restore(cr);
}

}
