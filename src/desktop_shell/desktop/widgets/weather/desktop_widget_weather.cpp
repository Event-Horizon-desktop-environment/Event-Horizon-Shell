#include "desktop_shell/desktop/widgets/weather/desktop_widget_weather.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <cmath>
#include <pango/pangocairo.h>

namespace eh::shell::desktop {

DesktopWeatherWidget::DesktopWeatherWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {}

void DesktopWeatherWidget::create() {
   
  auto& sc = eh::config::shell_config_snapshot();
  (void)sc;
}

void DesktopWeatherWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const eh::shell::dock::control_center::ControlCenterWeatherState ws =
      eh::shell::dock::control_center::control_center_weather_state(sc, m_widgetId);

  cairo_save(cr);

  const bool ok = ws.available;
  const double textAlpha = ok ? 0.92 : 0.45;

  const double iconPx = 32.0;
  const double tempFontSize = 28.0;

  auto make_pango = [&](const char* descStr) {
    PangoLayout* lay = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string(descStr);
    pango_layout_set_font_description(lay, desc);
    pango_font_description_free(desc);
    return lay;
  };

  auto make_pango_size = [&](const char* desc, double size) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.0f", desc, size);
    return make_pango(buf);
  };

  std::string tempText;
  char unitCh = 'C';
  if (ok) {
    unitCh = ws.fahrenheit ? 'F' : 'C';
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", ws.temp);
    tempText = buf;
  } else {
    tempText = "--";
  }

  const double unitFontSize = 14.0;
  char unitStr[8];
  std::snprintf(unitStr, sizeof(unitStr), "\u00b0%c", unitCh);

  PangoLayout* tempLayout = make_pango_size("Inter Bold", tempFontSize);
  pango_layout_set_text(tempLayout, tempText.c_str(), -1);
  int tempW = 0, tempH = 0;
  pango_layout_get_pixel_size(tempLayout, &tempW, &tempH);

  PangoLayout* unitLayout = make_pango_size("Inter Bold", unitFontSize);
  pango_layout_set_text(unitLayout, unitStr, -1);
  int unitW = 0, unitH = 0;
  pango_layout_get_pixel_size(unitLayout, &unitW, &unitH);

  const double padX = 10.0;
  const double padY = 10.0;
  m_width = static_cast<int>(std::ceil(iconPx + 6.0 + static_cast<double>(tempW) + static_cast<double>(unitW) + padX * 2.0));
  m_height = static_cast<int>(std::ceil(static_cast<double>(tempH) + padY * 2.0));

  const double ox = padX;
  const double oy = padY;
  const double col1W = iconPx + 6.0;

  const double iconY = oy + (static_cast<double>(tempH) - iconPx) * 0.5;
  const char* glyph = ok ? ws.icon.c_str() : "cloud";
  draw_material_glyph(cr, ox + iconPx * 0.5 + 1.0, iconY + iconPx * 0.5 + 1.0, iconPx, glyph, 0.0, 0.0, 0.0, textAlpha * 0.35);
  draw_material_glyph(cr, ox + iconPx * 0.5, iconY + iconPx * 0.5, iconPx, glyph, mc.accentR, mc.accentG, mc.accentB, textAlpha);

  auto draw_with_shadow = [&](PangoLayout* lay, double x, double y, double r, double g, double b) {
    cairo_save(cr);
    cairo_move_to(cr, x + 1.0, y + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, textAlpha * 0.35);
    pango_cairo_show_layout(cr, lay);
    cairo_restore(cr);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, r, g, b, textAlpha);
    pango_cairo_show_layout(cr, lay);
  };

  draw_with_shadow(tempLayout, ox + col1W, oy, mc.accentR, mc.accentG, mc.accentB);
  g_object_unref(tempLayout);

  draw_with_shadow(unitLayout, ox + col1W + static_cast<double>(tempW), oy + static_cast<double>(tempH) - static_cast<double>(unitH) - 2.0, mc.accentR, mc.accentG, mc.accentB);
  g_object_unref(unitLayout);

  cairo_restore(cr);
}

}

