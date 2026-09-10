#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <string>

namespace eh::shell::desktop {

struct DesktopWorldClockWidget : DesktopWidget {
  explicit DesktopWorldClockWidget(std::string widgetId);

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;
  void on_motion(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] int intrinsicWidth() const override { return m_width; }
  [[nodiscard]] int intrinsicHeight() const override { return m_height; }

private:
  std::string m_widgetId;
  int m_width = 320;
  int m_height = 200;
};

}
