#pragma once

#include "desktop_shell/desktop/audio/cava_config.hpp"
#include "desktop_shell/desktop/audio/cava_reader.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vector>

namespace eh::mpris { class DockMpris; }

namespace eh::shell::desktop {

// Liquid Glass now-playing card (macOS Tahoe inspired): frosted glass shell
// with a specular top edge, large rounded album art, scrolling title/artist,
// CAVA wave strip, seekbar with elapsed/remaining stamps, and
// shuffle / prev / play / next / repeat controls. Fixed 340x440 card.
struct DesktopMediaPlayerWidget : DesktopWidget {
  explicit DesktopMediaPlayerWidget(eh::mpris::DockMpris* mpris);

  ~DesktopMediaPlayerWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;
  void on_motion(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] uint32_t animIntervalMs() const override;
  [[nodiscard]] int intrinsicWidth() const override { return 340; }
  [[nodiscard]] int intrinsicHeight() const override { return 440; }

 private:
  enum HoverPart : int {
    HoverNone = 0,
    HoverShuffle,
    HoverPrev,
    HoverPlay,
    HoverNext,
    HoverRepeat,
    HoverSeekbar
  };

  HoverPart hit_test_hover(double x, double y) const;

  eh::mpris::DockMpris* m_mpris = nullptr;

  CavaConfig m_cfg;

  std::unique_ptr<CavaReader> m_cava;
  bool m_active = false;
  std::string m_title;
  std::string m_artist;
  std::string m_playbackStatus;
  bool m_canPrev = false;
  bool m_canNext = false;
  bool m_canPlay = false;
  bool m_canPause = false;
  bool m_shuffle = false;
  std::string m_loopMode;
  HoverPart m_hoverPart = HoverNone;
  double m_cacheSeekY = 0;
  double m_cacheCtrlY = 0;
  std::vector<int> m_cavaValues;
  bool m_cavaActive = false;
  // CAVA smoothing state, reused as a linear left-to-right wave strip.
  std::array<double, 28> m_waveSmoothed{};
  std::array<double, 28> m_waveTarget{};
  bool m_waveSettled = true;
  std::chrono::steady_clock::time_point m_lastTickTime{};
  double m_idlePhase = 0.0;
  // Set during paint while the title/artist actually scrolls; drives the
  // animation interval so the marquee stays smooth.
  mutable bool m_marqueeActive = false;

  void tick(double deltaMs);
  bool tick_cava();
};

}
