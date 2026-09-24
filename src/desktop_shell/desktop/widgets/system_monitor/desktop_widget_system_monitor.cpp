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
  long long memTotalKb = 0, memAvailKb = 0, swapTotalKb = 0, swapFreeKb = 0;
  char line[128];
  while (std::fgets(line, sizeof(line), f)) {
    long long kb = 0;
    if (std::sscanf(line, "MemTotal: %lld kB", &kb) == 1) memTotalKb = kb;
    else if (std::sscanf(line, "MemAvailable: %lld kB", &kb) == 1) memAvailKb = kb;
    else if (std::sscanf(line, "SwapTotal: %lld kB", &kb) == 1) swapTotalKb = kb;
    else if (std::sscanf(line, "SwapFree: %lld kB", &kb) == 1) swapFreeKb = kb;
  }
  std::fclose(f);
  d.totalMb = static_cast<int>(memTotalKb / 1024);
  d.usedMb = static_cast<int>((memTotalKb - memAvailKb) / 1024);
  if (d.totalMb > 0) d.usagePct = d.usedMb * 100 / d.totalMb;
  d.swapTotalMb = static_cast<int>(swapTotalKb / 1024);
  d.swapUsedMb = static_cast<int>((swapTotalKb - swapFreeKb) / 1024);
  if (d.swapUsedMb < 0) d.swapUsedMb = 0;
  if (d.swapTotalMb > 0) d.swapUsagePct = d.swapUsedMb * 100 / d.swapTotalMb;
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

// CPU package energy via powercap RAPL (Intel + AMD Zen expose energy_uj).
// Returns false when no zone exists — caller shows "--" instead of watts.
static bool read_cpu_energy_uj(unsigned long long& out) {
  constexpr const char* kZones[] = {
    "/sys/class/powercap/intel-rapl:0/energy_uj",
    "/sys/class/powercap/intel-rapl:0:0/energy_uj",
  };
  for (const char* path : kZones) {
    FILE* f = std::fopen(path, "r");
    if (!f) continue;
    unsigned long long v = 0;
    bool ok = std::fscanf(f, "%llu", &v) == 1;
    std::fclose(f);
    if (ok) {
      out = v;
      return true;
    }
  }
  return false;
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

    // CPU package power from RAPL energy delta (needs previous sample).
    {
      unsigned long long eNow = 0;
      auto tNow = std::chrono::steady_clock::now();
      unsigned long long ePrev = 0;
      std::chrono::steady_clock::time_point tPrev{};
      bool hadPrev = false;
      {
        std::lock_guard lk(m_dataMutex);
        ePrev = m_cpuEnergyPrev;
        tPrev = m_cpuEnergyPrevT;
        hadPrev = m_cpuEnergyInit;
      }
      if (read_cpu_energy_uj(eNow)) {
        if (hadPrev && eNow >= ePrev) {
          double dt = std::chrono::duration_cast<std::chrono::milliseconds>(tNow - tPrev).count() / 1000.0;
          if (dt > 0.05)
            cpu.powerW = static_cast<double>(eNow - ePrev) / 1e6 / dt;
        }
        std::lock_guard lk(m_dataMutex);
        m_cpuEnergyPrev = eNow;
        m_cpuEnergyPrevT = tNow;
        m_cpuEnergyInit = true;
      } else {
        std::lock_guard lk(m_dataMutex);
        m_cpuEnergyInit = false;
      }
    }

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
      push(m_graph.cpuTemp, cpu.temp);
      push(m_graph.cpuClockMhz, cpu.freqMhz);
      if (cpu.powerW >= 0.0)
        push(m_graph.cpuPowerMw, static_cast<int>(std::lround(cpu.powerW * 1000.0)));
      push(m_graph.gpuUsage, gpu.usagePct);
      if (gpu.valid) {
        push(m_graph.gpuTemp, gpu.temp);
        push(m_graph.gpuPowerMw, gpu.powerMw);
        push(m_graph.gpuVramMb, gpu.vramUsedMb);
      }
      int vramPct = 0;
      if (gpu.vramTotalMb > 0)
        vramPct = std::clamp(gpu.vramUsedMb * 100 / gpu.vramTotalMb, 0, 100);
      push(m_graph.gpuVram, vramPct);
      int powerPct = 0;
      if (gpu.powerMaxMw > 0 && gpu.powerMw >= 0)
        powerPct = std::clamp(gpu.powerMw * 100 / gpu.powerMaxMw, 0, 100);
      push(m_graph.gpuPower, powerPct);
      push(m_graph.ramUsage, ram.usagePct);
      push(m_graph.ramUsedMb, ram.usedMb);
      push(m_graph.swapUsage, ram.swapUsagePct);
      push(m_graph.swapUsedMb, ram.swapUsedMb);
      auto pushNet = [&](std::deque<int>& dq, double kbps) {
        dq.push_back(static_cast<int>(std::round(kbps)));
        if (dq.size() > kGraphSamples) dq.pop_front();
      };
      pushNet(m_graph.netDown, net.downKbps);
      pushNet(m_graph.netUp, net.upKbps);
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

void DesktopSystemMonitorWidget::paint_card_background(cairo_t* cr, int x, int y, int w, int h, double radius,
                                                       double backdropAlpha, double bgR, double bgG, double bgB) {
  // Soft drop shadow (no blur dependency — Hyprland handles window blur).
  // Offset hard fill peeking below the card reads as lift.
  {
    cairo_save(cr);
    rounded_rect(cr, static_cast<double>(x), static_cast<double>(y) + 2.0,
                 static_cast<double>(w), static_cast<double>(h), radius);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
    cairo_fill(cr);
    cairo_restore(cr);
  }
  rounded_rect(cr, static_cast<double>(x), static_cast<double>(y),
               static_cast<double>(w), static_cast<double>(h), radius);
  cairo_set_source_rgba(cr, bgR * 0.55, bgG * 0.55, bgB * 0.55, backdropAlpha);
  cairo_fill_preserve(cr);

  // Monitor glass treatment (kept as-is): top sheen + bright rim + outer hairline.
  {
    cairo_save(cr);
    rounded_rect(cr, static_cast<double>(x), static_cast<double>(y),
                 static_cast<double>(w), static_cast<double>(h), radius);
    cairo_clip(cr);
    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, static_cast<double>(y),
                                                         0, static_cast<double>(y) + static_cast<double>(h) * 0.45);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1.0, 1.0, 1.0, 0.13 * backdropAlpha);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 1.0, 1.0, 1.0, 0.0);
    cairo_set_source(cr, sheen);
    cairo_rectangle(cr, static_cast<double>(x), static_cast<double>(y),
                    static_cast<double>(w), static_cast<double>(h) * 0.45);
    cairo_fill(cr);
    cairo_pattern_destroy(sheen);
    cairo_restore(cr);
  }
  {
    cairo_pattern_t* rim = cairo_pattern_create_linear(0, static_cast<double>(y),
                                                       0, static_cast<double>(y) + static_cast<double>(h));
    cairo_pattern_add_color_stop_rgba(rim, 0.0, 1.0, 1.0, 1.0, 0.38 * backdropAlpha);
    cairo_pattern_add_color_stop_rgba(rim, 0.35, 1.0, 1.0, 1.0, 0.14 * backdropAlpha);
    cairo_pattern_add_color_stop_rgba(rim, 0.7, 1.0, 1.0, 1.0, 0.05 * backdropAlpha);
    cairo_pattern_add_color_stop_rgba(rim, 1.0, 1.0, 1.0, 1.0, 0.12 * backdropAlpha);
    cairo_set_source(cr, rim);
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, static_cast<double>(x) + 1.0, static_cast<double>(y) + 1.0,
                 static_cast<double>(w) - 2.0, static_cast<double>(h) - 2.0, radius - 1.0);
    cairo_stroke(cr);
    cairo_pattern_destroy(rim);
  }
  {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5,
                 static_cast<double>(w) - 1.0, static_cast<double>(h) - 1.0, radius);
    cairo_stroke(cr);
  }
}

void DesktopSystemMonitorWidget::paint_graph(cairo_t* cr, double graphLeft, double graphTop, double graphW, double graphH,
                                             const std::deque<int>& graphData, bool filled, const double color[3],
                                             const std::deque<int>* graphData2, const double* color2,
                                             const std::deque<int>* graphData3, const double* color3,
                                             bool fillPrimary) {
  if (graphW <= 10.0 || graphH <= 4.0 || graphData.size() < 2) return;
  cairo_save(cr);
  cairo_rectangle(cr, graphLeft, graphTop, graphW, graphH);
  cairo_clip(cr);

  int minV = 0, maxV = 100;
  if (!filled) {
    auto [mn, mx] = std::minmax_element(graphData.begin(), graphData.end());
    minV = *mn;
    maxV = std::max(*mx, minV + 1);
    // Include overlay series so small series (e.g. upload) stay visible.
    auto expand = [&](const std::deque<int>* dq) {
      if (!dq || dq->size() < 2) return;
      auto [omn, omx] = std::minmax_element(dq->begin(), dq->end());
      if (*omn < minV) minV = *omn;
      if (*omx > maxV) maxV = *omx;
    };
    expand(graphData2);
    expand(graphData3);
    if (maxV <= minV) maxV = minV + 1;
  }

  const double stepX = graphW / static_cast<double>(kGraphSamples - 1);
  // 2px vertical headroom so peaks (e.g. 99-100%) don't clip on the edge.
  const double graphPad = 2.0;
  const double graphInnerH = graphH - graphPad * 2.0;
  auto value_y = [&](int val) {
    double v = static_cast<double>(val - minV) / static_cast<double>(maxV - minV);
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return graphTop + graphPad + graphInnerH - v * graphInnerH;
  };
  auto stroke_series = [&](const std::deque<int>& dq, const double c[3], double alpha) {
    cairo_set_source_rgba(cr, c[0], c[1], c[2], alpha);
    cairo_set_line_width(cr, 1.5);
    bool started = false;
    for (size_t i = 0; i < dq.size(); ++i) {
      double px = graphLeft + static_cast<double>(i) * stepX;
      double py = value_y(dq[i]);
      if (!started) {
        cairo_move_to(cr, px, py);
        started = true;
      } else {
        cairo_line_to(cr, px, py);
      }
    }
    cairo_stroke(cr);
  };

  std::vector<double> pts;
  for (size_t i = 0; i < graphData.size(); ++i) {
    double px = graphLeft + static_cast<double>(i) * stepX;
    double py = value_y(graphData[i]);
    pts.push_back(px);
    pts.push_back(py);
  }

  if (pts.size() >= 4) {
    if (fillPrimary) {
      // Area fill under the primary line.
      cairo_save(cr);
      cairo_move_to(cr, pts[0], graphTop + graphH);
      for (size_t i = 0; i + 1 < pts.size(); i += 2)
        cairo_line_to(cr, pts[i], pts[i + 1]);
      cairo_line_to(cr, pts[pts.size() - 2], graphTop + graphH);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.18);
      cairo_fill(cr);
      cairo_restore(cr);
    }

    cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.2);

    cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.9);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, pts[0], pts[1]);
    for (size_t i = 2; i + 1 < pts.size(); i += 2)
      cairo_line_to(cr, pts[i], pts[i + 1]);
    cairo_stroke(cr);
  }

  // Optional overlay lines (e.g. swap over RAM, up over down, vram/power over GPU).
  if (graphData2 && color2 && graphData2->size() >= 2)
    stroke_series(*graphData2, color2, 0.9);
  if (graphData3 && color3 && graphData3->size() >= 2)
    stroke_series(*graphData3, color3, 0.9);

  cairo_restore(cr);
}

  

namespace {
// Row-card geometry (matches ram_card/stat heights below).
constexpr double kStatPadL = 8.0;
constexpr double kStatPadT = 8.0;
constexpr double kStatPadB = 8.0;
constexpr double kStatHeaderH = 18.0;
constexpr double kStatRowH = 40.0;
constexpr double kStatSparkH = 24.0;

void draw_text_with_shadow(cairo_t* cr, const char* fontDesc, const char* str, double px, double py,
                           double r, double g, double b, double a, int* outW, int* outH) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* pfd = pango_font_description_from_string(fontDesc);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, str, -1);
  cairo_save(cr);
  cairo_move_to(cr, px + 1.0, py + 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
  pango_cairo_show_layout(cr, lay);
  cairo_restore(cr);
  cairo_move_to(cr, px, py);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, lay);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(lay, &tw, &th);
  if (outW) *outW = tw;
  if (outH) *outH = th;
  g_object_unref(lay);
}

void measure_text(cairo_t* cr, const char* fontDesc, const char* str, int* outW, int* outH) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* pfd = pango_font_description_from_string(fontDesc);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, str, -1);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(lay, &tw, &th);
  if (outW) *outW = tw;
  if (outH) *outH = th;
  g_object_unref(lay);
}

// Truncate with "…" to fit maxW (UTF-8 safe).
std::string trunc_to_fit(cairo_t* cr, const char* fontDesc, const std::string& str, double maxW) {
  int tw = 0;
  measure_text(cr, fontDesc, str.c_str(), &tw, nullptr);
  if (static_cast<double>(tw) <= maxW) return str;
  std::string out = str;
  while (out.size() > 4) {
    // Pop one UTF-8 codepoint.
    do {
      out.pop_back();
    } while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);
    std::string cand = out + "…";
    measure_text(cr, fontDesc, cand.c_str(), &tw, nullptr);
    if (static_cast<double>(tw) <= maxW) return cand;
  }
  return out;
}
} // namespace

int DesktopSystemMonitorWidget::stat_card_height(cairo_t* cr, size_t rows) {
  (void)cr;
  return static_cast<int>(kStatPadT) + static_cast<int>(kStatHeaderH) + 6 +
         static_cast<int>(rows * kStatRowH) + static_cast<int>(kStatPadB);
}

void DesktopSystemMonitorWidget::paint_stat_card(cairo_t* cr, int x, int y, int w, int h,
                                                 const std::string& title,
                                                 const std::string& titleValue, const std::string& titleUnit,
                                                 const double* titleColor,
                                                 const std::vector<StatRow>& rows,
                                                 double backdropAlpha,
                                                 double bgR, double bgG, double bgB,
                                                 double textR, double textG, double textB) {
  cairo_save(cr);
  paint_card_background(cr, x, y, w, h, 15.0, backdropAlpha, bgR, bgG, bgB);

  // Header: title left, optional summary stat right.
  double rightReserve = 0.0;
  int tvW = 0, tuW = 0;
  if (titleColor && !titleValue.empty()) {
    measure_text(cr, "Inter Bold 12", titleValue.c_str(), &tvW, nullptr);
    measure_text(cr, "Inter 9", titleUnit.c_str(), &tuW, nullptr);
    rightReserve = tvW + 3 + tuW;
    draw_text_with_shadow(cr, "Inter Bold 12", titleValue.c_str(),
                          static_cast<double>(x) + w - kStatPadL - rightReserve,
                          static_cast<double>(y) + kStatPadT,
                          titleColor[0], titleColor[1], titleColor[2], 0.95, nullptr, nullptr);
    draw_text_with_shadow(cr, "Inter 9", titleUnit.c_str(),
                          static_cast<double>(x) + w - kStatPadL - tuW,
                          static_cast<double>(y) + kStatPadT + 3.0,
                          textR, textG, textB, 0.6, nullptr, nullptr);
  }
  const std::string t = trunc_to_fit(cr, "Inter Bold 10", title,
                                     w - 2 * kStatPadL - rightReserve - (rightReserve > 0 ? 8.0 : 0.0));
  draw_text_with_shadow(cr, "Inter Bold 10", t.c_str(), static_cast<double>(x) + kStatPadL,
                        static_cast<double>(y) + kStatPadT, textR, textG, textB, 0.7,
                        nullptr, nullptr);

  const double contentTop = static_cast<double>(y) + kStatPadT + kStatHeaderH + 6.0;

  // Pass 1: widest value+unit sets the sparkline start so no dead gap sits
  // between the numbers and the graphs. Spark keeps a minimum width.
  double maxTextW = 0.0;
  for (const StatRow& row : rows) {
    int vW = 0, uW = 0;
    measure_text(cr, "Inter Bold 15", row.value.c_str(), &vW, nullptr);
    measure_text(cr, "Inter 10", row.unit.c_str(), &uW, nullptr);
    maxTextW = std::max(maxTextW, static_cast<double>(vW + 3 + uW));
  }
  const double sparkRight = static_cast<double>(x) + w - kStatPadL;
  double sparkX = static_cast<double>(x) + kStatPadL + maxTextW + 10.0;
  if (sparkRight - sparkX < 80.0) sparkX = sparkRight - 80.0;
  const double sparkW = sparkRight - sparkX;
  const double textW = sparkX - 10.0 - (static_cast<double>(x) + kStatPadL);

  // Separators: under header + between rows.
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  cairo_set_line_width(cr, 1.0);
  for (size_t i = 0; i < rows.size(); ++i) {
    const double ly = contentTop + i * kStatRowH;
    cairo_move_to(cr, static_cast<double>(x) + kStatPadL, ly + 0.5);
    cairo_line_to(cr, static_cast<double>(x) + w - kStatPadL, ly + 0.5);
  }
  cairo_stroke(cr);

  for (size_t i = 0; i < rows.size(); ++i) {
    const StatRow& row = rows[i];
    const double rt = contentTop + i * kStatRowH;

    // Label.
    draw_text_with_shadow(cr, "Inter 9", row.label, static_cast<double>(x) + kStatPadL, rt + 2.0,
                          textR, textG, textB, 0.55, nullptr, nullptr);

    // Value + unit (shrink value on overflow).
    const char* bigDesc = "Inter Bold 15";
    int vW = 0, vH = 0, uW = 0, uH = 0;
    measure_text(cr, bigDesc, row.value.c_str(), &vW, &vH);
    measure_text(cr, "Inter 10", row.unit.c_str(), &uW, &uH);
    if (vW + 3 + uW > textW) {
      bigDesc = "Inter Bold 13";
      measure_text(cr, bigDesc, row.value.c_str(), &vW, &vH);
    }
    const double valueY = rt + 13.0;
    draw_text_with_shadow(cr, bigDesc, row.value.c_str(), static_cast<double>(x) + kStatPadL, valueY,
                          row.color[0], row.color[1], row.color[2], 0.95, nullptr, nullptr);
    draw_text_with_shadow(cr, "Inter 10", row.unit.c_str(),
                          static_cast<double>(x) + kStatPadL + vW + 3.0, valueY + (vH - uH),
                          textR, textG, textB, 0.6, nullptr, nullptr);

    // Sparkline.
    if (row.history && row.history->size() >= 2) {
      paint_graph(cr, sparkX, rt + (kStatRowH - kStatSparkH) / 2.0,
                  sparkW, kStatSparkH, *row.history, false, row.color.data(),
                  nullptr, nullptr, nullptr, nullptr, false);
    }
  }

  cairo_restore(cr);
}

void DesktopSystemMonitorWidget::paint_card(cairo_t* cr, int x, int y, int w, int h,
                                             const char* title, const std::vector<std::string>& stats,
                                             const std::deque<int>& graphData, bool filled,
                                             const double color[3], double backdropAlpha,
                                             const double bgR, const double bgG, const double bgB,
                                             const double textR, const double textG, const double textB,
                                             double titleSize,
                                             const std::deque<int>* graphData2,
                                             const double* color2,
                                             const std::deque<int>* graphData3,
                                             const double* color3) {
   
  cairo_save(cr);
  const double radius = 15.0;
  paint_card_background(cr, x, y, w, h, radius, backdropAlpha, bgR, bgG, bgB);

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
  paint_graph(cr, graphLeft, graphTop, graphW, graphH, graphData, filled, color,
              graphData2, color2, graphData3, color3);

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

  const double cardGap = 8.0;
  const double cw = (static_cast<double>(m_width) - cardGap) / 2.0;

  auto hist = [](const std::deque<int>& dq) -> const std::deque<int>* {
    return dq.size() >= 2 ? &dq : nullptr;
  };
  auto S = [](const char* fmt, auto v) -> std::string {
    char b[48];
    std::snprintf(b, sizeof(b), fmt, v);
    return std::string(b);
  };

  // Value colors match the graph line colors; card chrome stays matugen.
  const std::array<double, 3> cpuBlue{0.3, 0.7, 1.0};
  const std::array<double, 3> gpuGreen{0.2, 0.9, 0.5};
  const std::array<double, 3> tempRed{1.0, 0.38, 0.35};
  const std::array<double, 3> powerOrange{1.0, 0.45, 0.25};
  const std::array<double, 3> clockCyan{0.3, 0.9, 0.9};
  const std::array<double, 3> vramPurple{0.8, 0.5, 1.0};
  const std::array<double, 3> ramOrange{1.0, 0.7, 0.2};
  const std::array<double, 3> swapPink{1.0, 0.35, 0.55};

  char clockBuf[32];
  std::snprintf(clockBuf, sizeof(clockBuf), "%d.%d", snap.cpu.freqMhz / 1000, (snap.cpu.freqMhz % 1000) / 100);

  std::vector<StatRow> cpuRows;
  cpuRows.push_back({"USAGE", S("%d", snap.cpu.usagePct), " %", cpuBlue, hist(graph.cpuUsage)});
  cpuRows.push_back({"TEMP", S("%d", snap.cpu.temp), " \u00b0C", tempRed, hist(graph.cpuTemp)});
  if (snap.cpu.powerW >= 0.0)
    cpuRows.push_back({"POWER", S("%.0f", snap.cpu.powerW), " W", powerOrange, hist(graph.cpuPowerMw)});
  else
    cpuRows.push_back({"POWER", "--", " W", powerOrange, nullptr});
  cpuRows.push_back({"CLOCK", clockBuf, " GHz", clockCyan, hist(graph.cpuClockMhz)});

  const bool gv = snap.gpu.valid;
  const double vramUsedGb = static_cast<double>(snap.gpu.vramUsedMb) / 1024.0;
  const double vramTotalGb = static_cast<double>(snap.gpu.vramTotalMb) / 1024.0;
  const std::string gpuPowerUnit =
      (gv && snap.gpu.powerMaxMw > 0) ? "/ " + S("%d", snap.gpu.powerMaxMw / 1000) + " W" : " W";
  const std::string vramUnit =
      (gv && snap.gpu.vramTotalMb > 0) ? "/ " + S("%.1f", vramTotalGb) + " GB" : "GB";
  std::vector<StatRow> gpuRows;
  gpuRows.push_back({"USAGE", gv ? S("%d", snap.gpu.usagePct) : "--", " %", gpuGreen, hist(graph.gpuUsage)});
  gpuRows.push_back({"TEMP", gv ? S("%d", snap.gpu.temp) : "--", " \u00b0C", tempRed, hist(graph.gpuTemp)});
  gpuRows.push_back({"POWER", gv ? S("%d", snap.gpu.powerMw / 1000) : "--", gpuPowerUnit, powerOrange,
                     hist(graph.gpuPowerMw)});
  gpuRows.push_back({"VRAM", gv ? S("%.1f", vramUsedGb) : "--", vramUnit, vramPurple, hist(graph.gpuVramMb)});

  const double ramUsedGb = static_cast<double>(snap.ram.usedMb) / 1024.0;
  const double ramTotalGb = static_cast<double>(snap.ram.totalMb) / 1024.0;
  const double swapUsedGb = static_cast<double>(snap.ram.swapUsedMb) / 1024.0;
  std::vector<StatRow> memRows;
  memRows.push_back({"RAM", S("%.1f", ramUsedGb), "/ " + S("%.1f", ramTotalGb) + " GB", ramOrange,
                     hist(graph.ramUsedMb)});
  memRows.push_back({"SWAP", S("%.1f", swapUsedGb), " GB used", swapPink, hist(graph.swapUsedMb)});
  const std::string ramPctV = S("%d", snap.ram.usagePct);

  auto fmtRate = [](double kbps) -> std::pair<std::string, std::string> {
    char v[32], u[16];
    if (kbps >= 1000.0) {
      std::snprintf(v, sizeof(v), "%.1f", kbps / 1024.0);
      std::snprintf(u, sizeof(u), " MB/s");
    } else {
      std::snprintf(v, sizeof(v), "%.0f", kbps);
      std::snprintf(u, sizeof(u), " KB/s");
    }
    return {v, u};
  };
  const auto [downV, downU] = fmtRate(snap.net.downKbps);
  const auto [upV, upU] = fmtRate(snap.net.upKbps);
  std::vector<StatRow> netRows;
  netRows.push_back({"DOWN", downV, downU, clockCyan, hist(graph.netDown)});
  netRows.push_back({"UP", upV, upU, vramPurple, hist(graph.netUp)});

  const int topH = std::max(stat_card_height(cr, cpuRows.size()), stat_card_height(cr, gpuRows.size()));
  const int botH = std::max(stat_card_height(cr, memRows.size()), stat_card_height(cr, netRows.size()));
  const int gap = static_cast<int>(cardGap);
  m_height = topH + gap + botH;

  const double backdropAlpha = static_cast<double>(sc.appearance.overlayOpacityWidgetCard);

  const auto mc = eh::config::derived_chrome_colors(sc.appearance);
  const double bgR = mc.drawerDimR, bgG = mc.drawerDimG, bgB = mc.drawerDimB;
  const double txR = mc.textR, txG = mc.textG, txB = mc.textB;

  paint_stat_card(cr, 0, 0, static_cast<int>(cw), topH,
                  "CPU \u00b7 " + m_cpuName, clockBuf, " GHz", clockCyan.data(),
                  cpuRows, backdropAlpha, bgR, bgG, bgB, txR, txG, txB);
  paint_stat_card(cr, static_cast<int>(cw + cardGap), 0, static_cast<int>(cw), topH,
                  "GPU \u00b7 " + m_gpuName, "", "", nullptr,
                  gpuRows, backdropAlpha, bgR, bgG, bgB, txR, txG, txB);
  paint_stat_card(cr, 0, topH + gap, static_cast<int>(cw), botH,
                  "MEMORY", ramPctV, " %", ramOrange.data(),
                  memRows, backdropAlpha, bgR, bgG, bgB, txR, txG, txB);
  paint_stat_card(cr, static_cast<int>(cw + cardGap), topH + gap,
                  static_cast<int>(cw), botH,
                  "NET", "", "", nullptr,
                  netRows, backdropAlpha, bgR, bgG, bgB, txR, txG, txB);
}

} // namespace eh::shell::desktop
