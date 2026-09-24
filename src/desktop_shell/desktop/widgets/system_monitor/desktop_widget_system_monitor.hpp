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
  double powerW = -1.0; // -1 = unavailable (no RAPL zone found)
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
  int swapUsedMb = 0;
  int swapTotalMb = 0;
  int swapUsagePct = 0;
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
  std::deque<int> cpuTemp;
  std::deque<int> cpuClockMhz;
  std::deque<int> cpuPowerMw;
  std::deque<int> gpuUsage;
  std::deque<int> gpuTemp;
  std::deque<int> gpuPowerMw;
  std::deque<int> gpuVramMb;
  std::deque<int> gpuVram;
  std::deque<int> gpuPower;
  std::deque<int> ramUsage;
  std::deque<int> ramUsedMb;
  std::deque<int> swapUsage;
  std::deque<int> swapUsedMb;
  std::deque<int> netThroughput;
  std::deque<int> netDown;
  std::deque<int> netUp;
};

struct StatRow {
  const char* label = "";
  std::string value; // big colored number (may be "--")
  std::string unit;  // dim suffix ("%", " °C", " / 300 W", ...)
  std::array<double, 3> color{1.0, 1.0, 1.0};
  const std::deque<int>* history = nullptr; // null/empty = no sparkline
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
  static int stat_card_height(cairo_t* cr, size_t rows);

 private:
  SystemMonitorSnapshot read_data();
  static void paint_card_background(cairo_t* cr, int x, int y, int w, int h, double radius,
                                    double backdropAlpha, double bgR, double bgG, double bgB);
  static void paint_graph(cairo_t* cr, double graphLeft, double graphTop, double graphW, double graphH,
                          const std::deque<int>& graphData, bool filled, const double color[3],
                          const std::deque<int>* graphData2, const double* color2,
                          const std::deque<int>* graphData3, const double* color3,
                          bool fillPrimary = true);
  void paint_stat_card(cairo_t* cr, int x, int y, int w, int h,
                       const std::string& title,
                       const std::string& titleValue, const std::string& titleUnit,
                       const double* titleColor,
                       const std::vector<StatRow>& rows,
                       double backdropAlpha,
                       double bgR, double bgG, double bgB,
                       double textR, double textG, double textB);

  void paint_card(cairo_t* cr, int x, int y, int w, int h,
                  const char* title, const std::vector<std::string>& stats,
                  const std::deque<int>& graphData, bool filled,
                  const double color[3], double backdropAlpha,
                  double bgR, double bgG, double bgB,
                  double textR, double textG, double textB,
                  double titleSize = 10.0,
                  const std::deque<int>* graphData2 = nullptr,
                  const double* color2 = nullptr,
                  const std::deque<int>* graphData3 = nullptr,
                  const double* color3 = nullptr);
  std::string m_widgetId;
  int m_width = 600;
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
  unsigned long long m_cpuEnergyPrev = 0;
  std::chrono::steady_clock::time_point m_cpuEnergyPrevT{};
  bool m_cpuEnergyInit = false;
};

} // namespace eh::shell::desktop
