#pragma once

#include "desktop_shell/desktop/audio/cava_config.hpp"
#include "desktop_shell/desktop/audio/cava_reader.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace eh::mpris { class DockMpris; }

namespace eh::shell::desktop {

struct DesktopMediaCompactWidget : DesktopWidget {
  explicit DesktopMediaCompactWidget(eh::mpris::DockMpris* mpris);
  ~DesktopMediaCompactWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;
  bool on_click(double x, double y) override;
  void on_motion(double x, double y) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] uint32_t animIntervalMs() const override;
  [[nodiscard]] int intrinsicWidth() const override { return m_desiredWidth; }
  [[nodiscard]] int intrinsicHeight() const override { return 240; }

private:
  int hit_test_btn(double x, double y) const;
  void recompute_width();

  void tick_cava();
  void tick(double deltaMs);
  void ensure_wave_arrays();

  // MPRIS state (must precede m_cfg for init order)
  eh::mpris::DockMpris* m_mpris = nullptr;

  // Config (must precede m_cava for init order)
  CavaConfig m_cfg;

  // Cava audio visualizer — Catmull-Rom waveform
  std::unique_ptr<CavaReader> m_cava;
  std::vector<int> m_cavaValues;
  bool m_cavaActive = false;
  std::vector<double> m_waveSmoothed;
  std::vector<double> m_waveTarget;
  std::vector<double> m_peak;
  double m_idleTimer = 0.0;
  std::chrono::steady_clock::time_point m_lastTickTime{};
  bool m_active = false;
  std::string m_title;
  std::string m_artist;
  std::string m_playbackStatus;
  int64_t m_positionUs = 0;
  int64_t m_durationUs = 0;
  bool m_canPrev = false;
  bool m_canNext = false;
  bool m_canPlay = false;
  bool m_canPause = false;
  int m_hoverBtn = -1;
  bool m_hoverProg = false;
  bool m_isStream = false;
  mutable int m_desiredWidth = 390;

  // Smooth position projection
  std::string m_smoothTrackId;
  int64_t m_smoothPosUs = 0;
  int64_t m_smoothLastSnap = 0;
  std::chrono::steady_clock::time_point m_smoothPosAt;
};

} // namespace eh::shell::desktop
