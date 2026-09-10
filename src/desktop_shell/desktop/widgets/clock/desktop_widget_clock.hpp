#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <string>

namespace eh::shell::desktop {

struct DesktopClockWidget : DesktopWidget {
  DesktopClockWidget(std::string timeFormat, bool showSeconds, bool showDate, int fontSize);

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return m_showsSeconds || m_showsDate; }
  [[nodiscard]] int intrinsicWidth() const override { return m_width; }
  [[nodiscard]] int intrinsicHeight() const override { return m_height; }

  void onSecondTick();

private:
  std::string m_format;
  bool m_showsSeconds;
  bool m_showsDate;
  int m_fontSize;
  int m_width = 0;
  int m_height = 0;
  std::string m_lastText;
};

}
