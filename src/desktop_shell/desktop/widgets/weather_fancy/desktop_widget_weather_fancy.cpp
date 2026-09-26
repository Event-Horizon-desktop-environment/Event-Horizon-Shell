#include "desktop_shell/desktop/widgets/weather_fancy/desktop_widget_weather_fancy.hpp"
#include "desktop_shell/desktop/widgets/weather_fancy/fancy_weather_card_paint.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <utility>

namespace eh::shell::desktop {

DesktopWeatherFancyWidget::DesktopWeatherFancyWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {}

DesktopWeatherFancyWidget::~DesktopWeatherFancyWidget() = default;

void DesktopWeatherFancyWidget::create() {}

void DesktopWeatherFancyWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double W = static_cast<double>(intrinsicWidth());
  m_height = static_cast<int>(paint_fancy_weather_card(cr, W, sc, m_widgetId));
}

}
