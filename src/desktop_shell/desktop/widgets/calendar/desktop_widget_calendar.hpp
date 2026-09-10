#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <ctime>
#include <string>

namespace eh::shell::desktop {

struct DesktopCalendarWidget : DesktopWidget {
  DesktopCalendarWidget();
  ~DesktopCalendarWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return false; }
  [[nodiscard]] int intrinsicWidth() const override { return 320; }
  [[nodiscard]] int intrinsicHeight() const override { return 370; }

private:
  void prev_month();
  void next_month();

  std::tm m_displayDate{};
  std::tm m_selectedDate{};
};

}

