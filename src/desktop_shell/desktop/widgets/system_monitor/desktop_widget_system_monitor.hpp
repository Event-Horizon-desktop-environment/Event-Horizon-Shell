#pragma once

#include "desktop_shell/desktop/widgets/shared/desktop_widget.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eh::shell::desktop {

struct CpuData {
  int temp = 0;
  int freqMhz = 0;
  int usagePct = 0;
};

struct GpuData {
  int temp = 0;
  int vramUsedMb = 0;
  int vramTotalMb = 0;
  int usagePct = 0;
  int powerMw = 0;
  int powerMaxMw = 0;
  bool valid = false;
};

struct RamData {
  int usedMb = 0;
  int totalMb = 0;
  int usagePct = 0;
};

struct NetData {
  double upKbps = 0;
  double downKbps = 0;
};

struct SystemMonitorSnapshot {
  CpuData cpu{};
  GpuData gpu{};
  RamData ram{};
  NetData net{};
};

constexpr std::size_t kGraphSamples = 40;

struct SystemMonitorGraphData {
  std::deque<int> cpuUsage;
  std::deque<int> gpuUsage;
  std::deque<int> ramUsage;
  std::deque<int> netThroughput;
};

struct DesktopSystemMonitorWidget : DesktopWidget {
  explicit DesktopSystemMonitorWidget(std::string widgetId);
  ~DesktopSystemMonitorWidget() override;

  void create() override;
  void paint(cairo_t* cr, const eh::config::ShellConfig& sc) override;

  [[nodiscard]] bool wantsSecondTicks() const override { return true; }
  [[nodiscard]] int intrinsicWidth() const override { return m_width; }
  [[nodiscard]] int intrinsicHeight() const override { return m_height; }

  // Compute height needed for a card row given content
  static int card_content_height(cairo_t* cr, const char* title, const std::vector<std::string>& stats, double titleSize = 10.0);

private:
  SystemMonitorSnapshot read_data();
  void paint_card(cairo_t* cr, int x, int y, int w, int h,
                  const char* title, const std::vector<std::string>& stats,
                  const std::deque<int>& graphData, bool filled,
                  const double color[3], double backdropAlpha,
                  double bgR, double bgG, double bgB,
                  double textR, double textG, double textB,
                  double titleSize = 10.0);
  std::string m_widgetId;
  int m_width = 340;
  int m_height = 180;
  SystemMonitorSnapshot m_cached{};

  SystemMonitorGraphData m_graph{};

  struct NvmlState;
  std::unique_ptr<NvmlState> m_nvml;
  std::string m_cpuName = "CPU";
  std::string m_gpuName = "GPU";

  std::mutex m_dataMutex;
  std::future<void> m_readFuture;

  int m_procStatUser = 0, m_procStatNice = 0, m_procStatSystem = 0, m_procStatIdle = 0;
  unsigned long long m_netRxPrev = 0, m_netTxPrev = 0;
};

} // namespace eh::shell::desktop
