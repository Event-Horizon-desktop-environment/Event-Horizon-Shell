#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"

#include <string>

namespace eh::shell::desktop {

struct DesktopWeatherFancyWidget : DesktopWidget {
  explicit DesktopWeatherFancyWidget(std::string widgetId);
  ~DesktopWeatherFancyWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] int intrinsicWidth() const override { return 340; }
  [[nodiscard]] int intrinsicHeight() const override { return 340; }

private:
  std::string m_widgetId;
};

}

