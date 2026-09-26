#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <cstdint>
#include <string>

namespace eh::mpris { class DockMpris; }

namespace eh::shell::desktop {

// iOS-style now-playing card (same design as the media-player popup):
// art + scrolling title/artist, seekbar with elapsed/remaining stamps,
// shuffle / prev / play / next / repeat. Fixed 400x188 card.
struct DesktopMediaCompactWidget : DesktopWidget {
  explicit DesktopMediaCompactWidget(eh::mpris::DockMpris* mpris);
  ~DesktopMediaCompactWidget() override = default;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;
  void on_motion(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] uint32_t animIntervalMs() const override;
  [[nodiscard]] int intrinsicWidth() const override { return 400; }
  [[nodiscard]] int intrinsicHeight() const override { return 188; }

 private:
  // Hit zones: 0 prev, 1 play, 2 next, 3 seek, 4 shuffle, 5 repeat, -1 none.
  int hit_test_btn(double x, double y) const;

  eh::mpris::DockMpris* m_mpris = nullptr;

  bool m_active = false;
  bool m_canPrev = false;
  bool m_canNext = false;
  int64_t m_durationUs = 0;
  bool m_isStream = false;
  int m_hoverBtn = -1;
  bool m_hoverProg = false;
  // Set during paint when the title/artist actually scrolls; drives the
  // animation interval so the marquee stays smooth.
  mutable bool m_marqueeActive = false;
};

} // namespace eh::shell::desktop
