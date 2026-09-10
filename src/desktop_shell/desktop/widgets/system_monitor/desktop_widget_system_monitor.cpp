#include "desktop_shell/desktop/widgets/system_monitor/desktop_widget_system_monitor.hpp"
#include "configuration/shell_config.hpp"

#include <dlfcn.h>

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

namespace eh::shell::desktop {

using eh::shell::shared::rounded_rect;

/* ---- NVML wrapper (dlopen-based, optional dependency) ---- */

// Minimal NVML type definitions (no system header needed)
using nvmlDevice_t = struct nvmlDevice_st*;
using nvmlReturn_t = unsigned int;
constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr unsigned int NVML_TEMPERATURE_GPU = 0;

struct nvmlMemory_st {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};

struct nvmlUtilization_st {
  unsigned int gpu;
  unsigned int memory;
};

struct DesktopSystemMonitorWidget::NvmlState {
  void* lib = nullptr;

  nvmlReturn_t (*nvmlInit)() = nullptr;
  nvmlReturn_t (*nvmlShutdown)() = nullptr;
  nvmlReturn_t (*nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetTemperature)(nvmlDevice_t, unsigned int, unsigned int*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_st*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_st*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetEnforcedPowerLimit)(nvmlDevice_t, unsigned int*) = nullptr;
  nvmlReturn_t (*nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int) = nullptr;

  nvmlDevice_t gpu{};
  char m_gpuName[64]{};
  bool ready = false;

  bool open() {
     
    lib = dlopen("libnvidia-ml.so", RTLD_LAZY);
    if (!lib) return false;

    nvmlInit = reinterpret_cast<decltype(nvmlInit)>(dlsym(lib, "nvmlInit"));
    nvmlShutdown = reinterpret_cast<decltype(nvmlShutdown)>(dlsym(lib, "nvmlShutdown"));
    nvmlDeviceGetHandleByIndex = reinterpret_cast<decltype(nvmlDeviceGetHandleByIndex)>(dlsym(lib, "nvmlDeviceGetHandleByIndex"));
    nvmlDeviceGetTemperature = reinterpret_cast<decltype(nvmlDeviceGetTemperature)>(dlsym(lib, "nvmlDeviceGetTemperature"));
    nvmlDeviceGetMemoryInfo = reinterpret_cast<decltype(nvmlDeviceGetMemoryInfo)>(dlsym(lib, "nvmlDeviceGetMemoryInfo"));
    nvmlDeviceGetUtilizationRates = reinterpret_cast<decltype(nvmlDeviceGetUtilizationRates)>(dlsym(lib, "nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetPowerUsage = reinterpret_cast<decltype(nvmlDeviceGetPowerUsage)>(dlsym(lib, "nvmlDeviceGetPowerUsage"));
    nvmlDeviceGetEnforcedPowerLimit = reinterpret_cast<decltype(nvmlDeviceGetEnforcedPowerLimit)>(dlsym(lib, "nvmlDeviceGetEnforcedPowerLimit"));
    nvmlDeviceGetName = reinterpret_cast<decltype(nvmlDeviceGetName)>(dlsym(lib, "nvmlDeviceGetName"));

    if (!nvmlInit || !nvmlShutdown || !nvmlDeviceGetHandleByIndex ||
        !nvmlDeviceGetTemperature || !nvmlDeviceGetMemoryInfo || !nvmlDeviceGetUtilizationRates) {
      dlclose(lib);
      lib = nullptr;
      return false;
    }

    if (nvmlInit() != NVML_SUCCESS) {
      dlclose(lib);
      lib = nullptr;
      return false;
    }

    if (nvmlDeviceGetHandleByIndex(0, &gpu) != NVML_SUCCESS) {
      nvmlShutdown();
      dlclose(lib);
      lib = nullptr;
      return false;
    }

    if (nvmlDeviceGetName && nvmlDeviceGetName(gpu, m_gpuName, sizeof(m_gpuName)) != NVML_SUCCESS)
      std::snprintf(m_gpuName, sizeof(m_gpuName), "GPU");

    // Strip verbose vendor prefixes
    {
      constexpr const char* kPrefixes[] = {"NVIDIA GeForce ", "AMD Radeon ", "AMD Radeon(TM) "};
      char* p = m_gpuName;
      for (auto* prefix : kPrefixes) {
        size_t plen = std::strlen(prefix);
        if (std::strncmp(p, prefix, plen) == 0) {
          std::memmove(p, p + plen, std::strlen(p + plen) + 1);
          break;
        }
      }
    }

    ready = true;
    return true;
  }

  [[nodiscard]] const char* gpu_name() const {   return ready ? m_gpuName : "GPU"; }

  void close() {
     
    if (ready) {
      ready = false;
      if (nvmlShutdown) nvmlShutdown();
    }
    if (lib) { dlclose(lib); lib = nullptr; }
  }

  [[nodiscard]] GpuData query() const {
     
    GpuData d;
    if (!ready) return d;

    unsigned int tempC = 0;
    if (nvmlDeviceGetTemperature(gpu, NVML_TEMPERATURE_GPU, &tempC) == NVML_SUCCESS)
      d.temp = static_cast<int>(tempC);

    nvmlMemory_st mem{};
    if (nvmlDeviceGetMemoryInfo(gpu, &mem) == NVML_SUCCESS) {
      d.vramUsedMb = static_cast<int>(mem.used / 1048576ULL);
      d.vramTotalMb = static_cast<int>(mem.total / 1048576ULL);
    }

    nvmlUtilization_st util{};
    if (nvmlDeviceGetUtilizationRates(gpu, &util) == NVML_SUCCESS)
      d.usagePct = static_cast<int>(util.gpu);

    unsigned int powerMw = 0;
    if (nvmlDeviceGetPowerUsage && nvmlDeviceGetPowerUsage(gpu, &powerMw) == NVML_SUCCESS)
      d.powerMw = static_cast<int>(powerMw);
    unsigned int powerLimitMw = 0;
    if (nvmlDeviceGetEnforcedPowerLimit && nvmlDeviceGetEnforcedPowerLimit(gpu, &powerLimitMw) == NVML_SUCCESS)
      d.powerMaxMw = static_cast<int>(powerLimitMw);

    d.valid = true;
    return d;
  }
};

/* ---- Data reading helpers ---- */

namespace {
int read_int_file(const char* path) {
   
  FILE* f = std::fopen(path, "r");
  if (!f) return 0;
  int val = 0;
  std::fscanf(f, "%d", &val);
  std::fclose(f);
  return val;
}

// Locate the CPU thermal sensor by hwmon name instead of a hardcoded index,
// since hwmon numbering is machine-dependent (e.g. "prom21_xhci", "nvme"
// can occupy low indices). Prefers the "Tdie" sensor over "Tctl" on AMD
// (Tctl includes a thermal boost offset and reads hotter).
static int read_cpu_temp() {
  char path[128];
  constexpr const char* kCpuSensorNames[] = {"k10temp", "coretemp", "zenpower", "cpu_thermal"};
  for (int i = 0; i < 64; ++i) {
    std::snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", i);
    FILE* f = std::fopen(path, "r");
    if (!f) continue;
    char sensor[64] = {};
    if (std::fgets(sensor, sizeof(sensor), f) == nullptr) {
      std::fclose(f);
      continue;
    }
    std::fclose(f);
    sensor[std::strcspn(sensor, "\n")] = '\0';
    bool isCpu = false;
    for (auto* cpuName : kCpuSensorNames) {
      if (std::strcmp(sensor, cpuName) == 0) { isCpu = true; break; }
    }
    if (!isCpu) continue;

    // Prefer the die temperature (AMD Tdie), fall back to temp1 (Tctl / package)
    std::snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp2_label", i);
    f = std::fopen(path, "r");
    if (f) {
      char label[32] = {};
      bool isTdie = std::fgets(label, sizeof(label), f) != nullptr &&
                    std::strstr(label, "Tdie") != nullptr;
      std::fclose(f);
      if (isTdie) {
        std::snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp2_input", i);
        return read_int_file(path);
      }
    }
    std::snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp1_input", i);
    return read_int_file(path);
  }
  return 0;
}

static CpuData read_cpu(int& prevUser, int& prevNice, int& prevSystem, int& prevIdle) {
   
  CpuData d;

  // CPU temp — locate the k10temp/coretemp sensor and prefer Tdie over Tctl
  d.temp = read_cpu_temp() / 1000;

  // CPU freq
  d.freqMhz = read_int_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq") / 1000;

  // CPU usage from /proc/stat
  FILE* f = std::fopen("/proc/stat", "r");
  if (f) {
    char buf[256];
    if (std::fgets(buf, sizeof(buf), f)) {
      int user = 0, nice = 0, sys = 0, idle = 0;
      std::sscanf(buf, "cpu %d %d %d %d", &user, &nice, &sys, &idle);
      int dUser = user - prevUser;
      int dNice = nice - prevNice;
      int dSys = sys - prevSystem;
      int dIdle = idle - prevIdle;
      int total = dUser + dNice + dSys + dIdle;
      if (prevUser > 0 && total > 0)
        d.usagePct = (dUser + dNice + dSys) * 100 / total;
      prevUser = user; prevNice = nice; prevSystem = sys; prevIdle = idle;
    }
    std::fclose(f);
  }

  return d;
}

static std::string read_cpu_name() {
   
  char name[128] = "CPU";
  FILE* f = std::fopen("/proc/cpuinfo", "r");
  if (f) {
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
      char val[128];
      if (std::sscanf(line, "model name : %127[^\n]", val) == 1) {
        std::strncpy(name, val, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        break;
      }
    }
    std::fclose(f);
  }

  // Strip known prefixes (order matters — strip "AMD " before checking for "Ryzen")
  constexpr const char* kPrefixes[] = {
    "AMD ",
    "Intel(R) Core(TM) ",
    "Intel(R) ",
  };
  char* p = name;
  for (auto* prefix : kPrefixes) {
    size_t plen = std::strlen(prefix);
    if (std::strncmp(p, prefix, plen) == 0) {
      std::memmove(p, p + plen, std::strlen(p + plen) + 1);
      break;
    }
  }

  // Normalize "Ryzen N" to "RN" (e.g. "Ryzen 9" → "R9")
  if (std::strncmp(p, "Ryzen ", 6) == 0 && p[6] >= '0' && p[6] <= '9') {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "R%c%s", p[6], p + 7);
    std::strncpy(name, buf, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }

  // Strip trailing " N-Core Processor" or " N-Core"
  {
    char* end = name + std::strlen(name);
    while (end > name && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    constexpr const char* kSuffixes[] = {"-Core Processor", "-Core"};
    for (auto* suffix : kSuffixes) {
      size_t slen = std::strlen(suffix);
      if (static_cast<size_t>(end - name) >= slen + 2 &&
          std::strncmp(end - slen, suffix, slen) == 0 &&
          end[-slen - 1] >= '0' && end[-slen - 1] <= '9') {
        // Find where the digit sequence starts
        char* cut = end - slen;
        while (cut > name && cut[-1] >= '0' && cut[-1] <= '9') --cut;
        if (cut > name && cut[-1] == ' ') --cut;
        *cut = '\0';
        break;
      }
    }
  }

  // Strip trailing " CPU @ ..." (e.g., "i7-10700K CPU @ 3.80GHz")
  {
    char* cpuAt = std::strstr(name, " CPU @ ");
    if (cpuAt) *cpuAt = '\0';
  }

  if (name[0] == '\0') std::strncpy(name, "CPU", sizeof(name));
  return name;
}

static RamData read_ram() {
   
  RamData d;
  FILE* f = std::fopen("/proc/meminfo", "r");
  if (!f) return d;
  char line[128];
  while (std::fgets(line, sizeof(line), f)) {
    int kb = 0;
    if (std::sscanf(line, "MemTotal: %d kB", &kb) == 1) d.totalMb = kb / 1024;
    if (std::sscanf(line, "MemAvailable: %d kB", &kb) == 1) {
      d.usedMb = d.totalMb - kb / 1024;
    }
  }
  std::fclose(f);
  if (d.totalMb > 0) d.usagePct = d.usedMb * 100 / d.totalMb;
  return d;
}

static NetData read_net(unsigned long long& rxPrev, unsigned long long& txPrev) {
   
  NetData d;
  FILE* f = std::fopen("/proc/net/dev", "r");
  if (!f) return d;
  char line[512];
  unsigned long long rx = 0, tx = 0;
  while (std::fgets(line, sizeof(line), f)) {
    char iface[64];
    unsigned long long r = 0, t = 0;
    if (std::sscanf(line, "%63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",
                    iface, &r, &t) >= 3) {
      if (std::strcmp(iface, "lo") == 0) continue;
      rx += r;
      tx += t;
    }
  }
  std::fclose(f);
  if (rxPrev > 0) {
    d.downKbps = static_cast<double>(rx - rxPrev) / 1024.0;
    d.upKbps = static_cast<double>(tx - txPrev) / 1024.0;
  }
  rxPrev = rx;
  txPrev = tx;
  return d;
}
} // namespace

/* ---- Widget ---- */

DesktopSystemMonitorWidget::DesktopSystemMonitorWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {
   
  m_nvml = std::make_unique<NvmlState>();
  m_nvml->open();
  m_cpuName = read_cpu_name();
  m_gpuName = m_nvml->gpu_name();
  m_cached.cpu = read_cpu(m_procStatUser, m_procStatNice, m_procStatSystem, m_procStatIdle);
  m_cached.gpu = m_nvml->query();
  m_cached.ram = read_ram();
  m_cached.net = read_net(m_netRxPrev, m_netTxPrev);
}

DesktopSystemMonitorWidget::~DesktopSystemMonitorWidget() {
   
  if (m_nvml) m_nvml->close();
}

void DesktopSystemMonitorWidget::create() {
   
  if (m_readFuture.valid()) {
    if (m_readFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      m_readFuture.get();
    } else {
      return;
    }
  }

  m_readFuture = std::async(std::launch::async, [this]() {
    int u = 0, n = 0, s = 0, id = 0;
    unsigned long long rx = 0, tx = 0;
    {
      std::lock_guard lk(m_dataMutex);
      u = m_procStatUser; n = m_procStatNice;
      s = m_procStatSystem; id = m_procStatIdle;
      rx = m_netRxPrev; tx = m_netTxPrev;
    }

    CpuData cpu = read_cpu(u, n, s, id);
    RamData ram = read_ram();
    NetData net = read_net(rx, tx);
    GpuData gpu = m_nvml ? m_nvml->query() : GpuData{};

    {
      std::lock_guard lk(m_dataMutex);
      m_cached.cpu = cpu;
      m_cached.ram = ram;
      m_cached.net = net;
      m_cached.gpu = gpu;
      m_procStatUser = u; m_procStatNice = n;
      m_procStatSystem = s; m_procStatIdle = id;
      m_netRxPrev = rx; m_netTxPrev = tx;

      auto push = [](std::deque<int>& dq, int val) {
        dq.push_back(val);
        if (dq.size() > kGraphSamples) dq.pop_front();
      };
      push(m_graph.cpuUsage, cpu.usagePct);
      push(m_graph.gpuUsage, gpu.usagePct);
      push(m_graph.ramUsage, ram.usagePct);
      double netTotal = net.downKbps + net.upKbps;
      m_graph.netThroughput.push_back(static_cast<int>(std::round(netTotal)));
      if (m_graph.netThroughput.size() > kGraphSamples) m_graph.netThroughput.pop_front();
    }
  });
}

int DesktopSystemMonitorWidget::card_content_height(cairo_t* cr, const char* title, const std::vector<std::string>& stats, double titleSize) {
   
  const double padT = 6.0, padB = 6.0;
  const double statSize = 10.0;

  PangoLayout* lay = pango_cairo_create_layout(cr);
  char descBuf[64];
  std::snprintf(descBuf, sizeof(descBuf), "Inter Bold %.0f", titleSize);
  PangoFontDescription* pfd = pango_font_description_from_string(descBuf);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, title, -1);
  int titleH = 0;
  pango_layout_get_pixel_size(lay, nullptr, &titleH);
  g_object_unref(lay);

  int statH = 14;
  for (const auto& s : stats) {
    lay = pango_cairo_create_layout(cr);
    std::snprintf(descBuf, sizeof(descBuf), "Inter %.0f", statSize);
    pfd = pango_font_description_from_string(descBuf);
    pango_layout_set_font_description(lay, pfd);
    pango_font_description_free(pfd);
    pango_layout_set_text(lay, s.c_str(), -1);
    int lh = 0;
    pango_layout_get_pixel_size(lay, nullptr, &lh);
    if (lh > statH) statH = lh;
    g_object_unref(lay);
  }

  return static_cast<int>(padT) + titleH + 4 + 30 + 4 + statH + static_cast<int>(padB);
}

void DesktopSystemMonitorWidget::paint_card(cairo_t* cr, int x, int y, int w, int h,
                                             const char* title, const std::vector<std::string>& stats,
                                             const std::deque<int>& graphData, bool filled,
                                             const double color[3], double backdropAlpha,
                                             const double bgR, const double bgG, const double bgB,
                                             const double textR, const double textG, const double textB,
                                             double titleSize) {
   
  cairo_save(cr);
  const double radius = 8.0;
  rounded_rect(cr, static_cast<double>(x), static_cast<double>(y),
               static_cast<double>(w), static_cast<double>(h), radius);
  cairo_set_source_rgba(cr, bgR * 0.55, bgG * 0.55, bgB * 0.55, backdropAlpha);
  cairo_fill_preserve(cr);

  // semi-glassy inner design using shared helper (0.8 inset tuned for these cards)
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = backdropAlpha;
    eh::widgets::slot_pill_style::paint_glass_layers(cr, static_cast<double>(x), static_cast<double>(y),
                                                     static_cast<double>(w), static_cast<double>(h), 1.0, radius);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }

  const double padL = 8.0, padT = 6.0;
  const double statSize = 10.0;

  // Title
  PangoLayout* lay = pango_cairo_create_layout(cr);
  char descBuf[64];
  std::snprintf(descBuf, sizeof(descBuf), "Inter Bold %.0f", titleSize);
  PangoFontDescription* pfd = pango_font_description_from_string(descBuf);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, title, -1);
  cairo_save(cr);
  cairo_move_to(cr, static_cast<double>(x) + padL + 1.0, static_cast<double>(y) + padT + 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
  pango_cairo_show_layout(cr, lay);
  cairo_restore(cr);

  cairo_set_source_rgba(cr, textR, textG, textB, 0.92);
  cairo_move_to(cr, static_cast<double>(x) + padL, static_cast<double>(y) + padT);
  pango_cairo_show_layout(cr, lay);
  int titleH = 0;
  pango_layout_get_pixel_size(lay, nullptr, &titleH);
  g_object_unref(lay);

  // Graph
  const double graphTop = static_cast<double>(y) + padT + static_cast<double>(titleH) + 4.0;
  const double graphH = 30.0;
  const double graphLeft = static_cast<double>(x) + padL;
  const double graphW = static_cast<double>(w) - padL * 2.0;

  if (graphW > 10.0 && graphH > 4.0 && graphData.size() >= 2) {
    cairo_save(cr);
    cairo_rectangle(cr, graphLeft, graphTop, graphW, graphH);
    cairo_clip(cr);

    int minV = 0, maxV = 100;
    if (!filled) {
      auto [mn, mx] = std::minmax_element(graphData.begin(), graphData.end());
      minV = *mn;
      maxV = std::max(*mx, minV + 1);
    }

    const double stepX = graphW / static_cast<double>(kGraphSamples - 1);
    std::vector<double> pts;
    for (size_t i = 0; i < graphData.size(); ++i) {
      double v = static_cast<double>(graphData[i] - minV) / static_cast<double>(maxV - minV);
      double px = graphLeft + static_cast<double>(i) * stepX;
      double py = graphTop + graphH - v * graphH;
      pts.push_back(px);
      pts.push_back(py);
    }

    if (pts.size() >= 4) {
      cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.2);

      cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.8);
      cairo_set_line_width(cr, 1.2);
      cairo_move_to(cr, pts[0], pts[1]);
      for (size_t i = 2; i + 1 < pts.size(); i += 2)
        cairo_line_to(cr, pts[i], pts[i + 1]);
      cairo_stroke(cr);
    }

    cairo_restore(cr);
  }

  // Stats — first stat left, last stat right, middle stats spread evenly
  const double statY = graphTop + graphH + 4.0;
  const size_t n = stats.size();
  for (size_t i = 0; i < n; ++i) {
    lay = pango_cairo_create_layout(cr);
    std::snprintf(descBuf, sizeof(descBuf), "Inter %.0f", statSize);
    pfd = pango_font_description_from_string(descBuf);
    pango_layout_set_font_description(lay, pfd);
    pango_font_description_free(pfd);
    pango_layout_set_text(lay, stats[i].c_str(), -1);
    cairo_set_source_rgba(cr, textR, textG, textB, 0.82);
    double sx;
    if (i == 0) {
      sx = static_cast<double>(x) + padL;
    } else if (i == n - 1) {
      int tw = 0;
      pango_layout_get_pixel_size(lay, &tw, nullptr);
      sx = static_cast<double>(x) + static_cast<double>(w) - padL - static_cast<double>(tw);
    } else {
      int tw = 0;
      pango_layout_get_pixel_size(lay, &tw, nullptr);
      double centerX = static_cast<double>(x) + padL + static_cast<double>(i) * (static_cast<double>(w) - padL * 2.0) / std::max(static_cast<double>(n - 1), 1.0);
      sx = centerX - static_cast<double>(tw) / 2.0;
    }
    cairo_save(cr);
    cairo_move_to(cr, sx + 1.0, statY + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
    pango_cairo_show_layout(cr, lay);
    cairo_restore(cr);

    cairo_move_to(cr, sx, statY);
    cairo_set_source_rgba(cr, textR, textG, textB, 0.82);
    pango_cairo_show_layout(cr, lay);
    g_object_unref(lay);
  }

  cairo_restore(cr);
}

void DesktopSystemMonitorWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  SystemMonitorSnapshot snap;
  SystemMonitorGraphData graph;
  {
    std::lock_guard lk(m_dataMutex);
    snap = m_cached;
    graph = m_graph;
  }

  // Format stats
  char cpuTemp[32], cpuFreq[32], cpuPct[32];
  std::snprintf(cpuTemp, sizeof(cpuTemp), "%d\u00b0C", snap.cpu.temp);
  std::snprintf(cpuFreq, sizeof(cpuFreq), "%d.%d GHz", snap.cpu.freqMhz / 1000, (snap.cpu.freqMhz % 1000) / 100);
  std::snprintf(cpuPct, sizeof(cpuPct), "%d%%", snap.cpu.usagePct);

  char gpuTemp[32], gpuVram[32], gpuPct[32];
  if (snap.gpu.valid) {
    std::snprintf(gpuTemp, sizeof(gpuTemp), "%d\u00b0C", snap.gpu.temp);
    std::snprintf(gpuVram, sizeof(gpuVram), "%.1f/%.1f GB", static_cast<double>(snap.gpu.vramUsedMb) / 1024.0, static_cast<double>(snap.gpu.vramTotalMb) / 1024.0);
    std::snprintf(gpuPct, sizeof(gpuPct), "%d%%", snap.gpu.usagePct);
  } else {
    std::snprintf(gpuTemp, sizeof(gpuTemp), "--\u00b0");
    std::snprintf(gpuVram, sizeof(gpuVram), "--");
    std::snprintf(gpuPct, sizeof(gpuPct), "--%%");
  }

  char ramUsed[32], ramPct[32];
  std::snprintf(ramUsed, sizeof(ramUsed), "%.1f/%.1f GB", static_cast<double>(snap.ram.usedMb) / 1024.0, static_cast<double>(snap.ram.totalMb) / 1024.0);
  std::snprintf(ramPct, sizeof(ramPct), "%d%%", snap.ram.usagePct);

  char netUp[32], netDown[32];
  if (snap.net.upKbps >= 1000.0)
    std::snprintf(netUp, sizeof(netUp), "\u2191%.1f MB/s", snap.net.upKbps / 1024.0);
  else
    std::snprintf(netUp, sizeof(netUp), "\u2191%.0f KB/s", snap.net.upKbps);
  if (snap.net.downKbps >= 1000.0)
    std::snprintf(netDown, sizeof(netDown), "\u2193%.1f MB/s", snap.net.downKbps / 1024.0);
  else
    std::snprintf(netDown, sizeof(netDown), "\u2193%.0f KB/s", snap.net.downKbps);

  const double cardGap = 4.0;
  const double ch = static_cast<double>(card_content_height(cr, "CPU", {cpuTemp, cpuFreq, cpuPct}));
  const double cw = (static_cast<double>(m_width) - cardGap) / 2.0;

  // Colors for graphs
  const double cpuColor[3] = {0.3, 0.7, 1.0};
  const double gpuColor[3] = {0.2, 0.9, 0.5};
  const double ramColor[3] = {1.0, 0.7, 0.2};
  const double netColor[3] = {0.8, 0.5, 1.0};

  const double backdropAlpha = static_cast<double>(sc.appearance.overlayOpacityWidgetCard);

  const auto mc = eh::config::derived_chrome_colors(sc.appearance);
  const double bgR = mc.drawerDimR, bgG = mc.drawerDimG, bgB = mc.drawerDimB;
  const double txR = mc.textR, txG = mc.textG, txB = mc.textB;

  eh::shell::shared::paint_glass_card(cr, 0, 0, static_cast<double>(m_width),
                                      static_cast<double>(m_height), 14.0, mc,
                                      sc.appearance.overlayOpacityWidgetCard);

  paint_card(cr, 0, 0, static_cast<int>(cw), static_cast<int>(ch),
             m_cpuName.c_str(), {cpuTemp, cpuFreq, cpuPct}, graph.cpuUsage, true, cpuColor, backdropAlpha,
             bgR, bgG, bgB, txR, txG, txB, 7.5);
  int gpuCardX = static_cast<int>(cw + cardGap);
  int gpuCardW = static_cast<int>(cw);
  paint_card(cr, gpuCardX, 0, gpuCardW, static_cast<int>(ch),
             m_gpuName.c_str(), {gpuTemp, gpuVram, gpuPct}, graph.gpuUsage, true, gpuColor, backdropAlpha,
             bgR, bgG, bgB, txR, txG, txB, 7.5);

  // GPU power at top-right edge
  if (snap.gpu.valid) {
    char gpuPower[32];
    int pw = snap.gpu.powerMw / 1000;
    int pm = snap.gpu.powerMaxMw / 1000;
    if (pm > 0)
      std::snprintf(gpuPower, sizeof(gpuPower), "%d/%d W", pw, pm);
    else
      std::snprintf(gpuPower, sizeof(gpuPower), "%d W", pw);

    PangoLayout* lay = pango_cairo_create_layout(cr);
    char descBuf[64];
    std::snprintf(descBuf, sizeof(descBuf), "Inter 9");
    PangoFontDescription* pfd = pango_font_description_from_string(descBuf);
    pango_layout_set_font_description(lay, pfd);
    pango_font_description_free(pfd);
    pango_layout_set_text(lay, gpuPower, -1);
    int tw = 0;
    pango_layout_get_pixel_size(lay, &tw, nullptr);

    const double padR = 8.0, padT2 = 6.0;
    double px = static_cast<double>(gpuCardX + gpuCardW) - padR - static_cast<double>(tw);
    double py = static_cast<double>(0) + padT2;

    cairo_save(cr);
    cairo_move_to(cr, px + 1.0, py + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
    pango_cairo_show_layout(cr, lay);

    cairo_move_to(cr, px, py);
    cairo_set_source_rgba(cr, txR, txG, txB, 0.82);
    pango_cairo_show_layout(cr, lay);
    cairo_restore(cr);

    g_object_unref(lay);
  }

  paint_card(cr, 0, static_cast<int>(ch + cardGap), static_cast<int>(cw), static_cast<int>(ch),
             "RAM", {ramUsed, ramPct}, graph.ramUsage, true, ramColor, backdropAlpha,
             bgR, bgG, bgB, txR, txG, txB);
  paint_card(cr, static_cast<int>(cw + cardGap), static_cast<int>(ch + cardGap),
             static_cast<int>(cw), static_cast<int>(ch),
             "NET", {netUp, netDown}, graph.netThroughput, false, netColor, backdropAlpha,
             bgR, bgG, bgB, txR, txG, txB);
}

} // namespace eh::shell::desktop
