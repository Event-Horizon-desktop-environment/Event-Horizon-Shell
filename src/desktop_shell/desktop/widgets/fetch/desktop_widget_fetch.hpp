#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"

#include <cairo/cairo.h>

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace eh::shell::desktop {

// Parsed `fastfetch` output: a "user@host" title plus ordered key/value rows.
struct FetchSnapshot {
  std::string title;
  std::vector<std::pair<std::string, std::string>> rows;
  bool ok = false;
};

// Desktop card that mirrors `fastfetch` output: distro logo on the left,
// title + key/value rows on the right. Polls the fastfetch binary on a
// background thread and repaints in the shell's glass-card style.
struct DesktopFetchWidget : DesktopWidget {
  explicit DesktopFetchWidget(std::string widgetId);
  ~DesktopFetchWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] int intrinsicWidth() const override { return m_width; }
  [[nodiscard]] int intrinsicHeight() const override { return m_height; }

  // Throttle: fastfetch is cheap (~5ms) but a 5s cadence matches how often
  // most of these values actually change.
  static constexpr int kRefreshSeconds = 5;

 private:
  static FetchSnapshot read_fetch();
  // Measures the current snapshot and settles m_width/m_height so the host
  // reads a correct footprint *before* it clips and paints.
  // IMPORTANT: measure with the cairo_t that will paint. Pango resolves text
  // extents against the cr's transform/font options, so measuring on a
  // different context (e.g. the identity-scale scratch) can under-measure on
  // scaled outputs and every long value ends up truncated with "…".
  void relayout(cairo_t* cr = nullptr);

  std::string m_widgetId;
  int m_width = 440;
  int m_height = 240;
  double m_keyCol = 74.0;  // measured key column width
  double m_valMax = 260.0; // measured value column width
  double m_logoBox = 108.0; // logo square, grows with content height (fastfetch-style)
  bool m_layoutDirty = false;

  cairo_surface_t* m_logo = nullptr;

  std::mutex m_dataMutex;
  FetchSnapshot m_snapshot;
  std::future<void> m_readFuture;
  std::chrono::steady_clock::time_point m_lastRead{};
  bool m_haveRead = false;
};

} // namespace eh::shell::desktop
