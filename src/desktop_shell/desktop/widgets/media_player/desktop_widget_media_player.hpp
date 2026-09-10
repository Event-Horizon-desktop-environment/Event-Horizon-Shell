#pragma once

#include "desktop_shell/desktop/audio/cava_config.hpp"
#include "desktop_shell/desktop/audio/cava_reader.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace eh::mpris { class DockMpris; }

namespace eh::shell::desktop {

struct DesktopMediaPlayerWidget : DesktopWidget {
  explicit DesktopMediaPlayerWidget(eh::mpris::DockMpris* mpris);

  ~DesktopMediaPlayerWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;
  void on_motion(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] int intrinsicWidth() const override { return 340; }
  [[nodiscard]] int intrinsicHeight() const override { return 420; }

private:
  enum HoverPart : int { HoverNone = 0, HoverClose, HoverPrev, HoverPlay, HoverNext, HoverSeekbar };

  HoverPart hit_test_hover(double x, double y) const;

  eh::mpris::DockMpris* m_mpris = nullptr;

  CavaConfig m_cfg;

  std::unique_ptr<CavaReader> m_cava;
  bool m_active = false;
  std::string m_title;
  std::string m_artist;
  std::string m_album;
  std::string m_playbackStatus;
  bool m_canPrev = false;
  bool m_canNext = false;
  bool m_canPlay = false;
  bool m_canPause = false;
  HoverPart m_hoverPart = HoverNone;
  double m_cacheSeekY = 0;
  double m_cacheCtrlY = 0;
  std::vector<int> m_cavaValues;
  bool m_cavaActive = false;
  std::array<double, 28> m_blobSmoothed{};
  std::array<double, 28> m_blobTarget{};
  bool m_blobConverged = true;
  std::chrono::steady_clock::time_point m_lastTickTime{};

  void tick(double deltaMs);
  bool tick_cava();
};

}

