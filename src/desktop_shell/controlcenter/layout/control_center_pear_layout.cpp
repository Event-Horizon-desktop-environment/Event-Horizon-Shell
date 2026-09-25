#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"

#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "services/mpris/mpris_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace eh::shell::dock::control_center {

namespace {
bool cachedBrightAvail = false;
bool cachedBrightAvailDone = false;
bool cachedBrightWritable = false;
std::filesystem::path cachedBrightCur;
std::filesystem::path cachedBrightMax;

void ensure_backlight_scan() {
  if (cachedBrightAvailDone) return;
  cachedBrightAvailDone = true;
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root("/sys/class/backlight");
  if (!fs::exists(root, ec)) return;
  for (const auto& e : fs::directory_iterator(root, ec)) {
    if (!e.is_directory(ec)) continue;
    const fs::path cur = e.path() / "brightness";
    const fs::path mx = e.path() / "max_brightness";
    if (fs::exists(cur, ec) && fs::exists(mx, ec)) {
      cachedBrightAvail = true;
      cachedBrightCur = cur;
      cachedBrightMax = mx;
      std::ofstream probe(cur, std::ios::app);
      cachedBrightWritable = static_cast<bool>(probe);
      return;
    }
  }
}
} // namespace

bool pear_brightness_available() {
  ensure_backlight_scan();
  return cachedBrightAvail;
}

bool pear_brightness_writable() {
  ensure_backlight_scan();
  return cachedBrightAvail && cachedBrightWritable;
}

int pear_brightness_pct() {
  ensure_backlight_scan();
  if (!cachedBrightAvail) return -1;
  std::ifstream fc(cachedBrightCur), fm(cachedBrightMax);
  int cur = 0, mx = 1;
  if (!(fc >> cur)) return -1;
  if (!(fm >> mx) || mx <= 0) return -1;
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(cur) * 100.0 / mx)), 0, 100);
}

bool pear_set_brightness_pct(int pct) {
  ensure_backlight_scan();
  if (!cachedBrightAvail || !cachedBrightWritable) return false;
  std::ifstream fm(cachedBrightMax);
  int mx = 1;
  if (!(fm >> mx) || mx <= 0) return false;
  const int v = std::clamp(pct, 0, 100) * mx / 100;
  std::ofstream f(cachedBrightCur, std::ios::trunc);
  if (!f) return false;
  f << v;
  return static_cast<bool>(f);
}

PearLayout cc_compute_pear_layout(double popupW, ControlCenterState& state,
                                const PearCenterConfig& cfg, double uiScale,
                                int wifiApCount) {
  PearLayout L;
  L.us = std::max(0.5, uiScale * pear_center_scale(cfg));
  const double us = L.us;
  L.W = popupW;
  L.pad = kPearLargeSpacing * us;
  L.gapM = kPearMediumSpacing * us;
  L.gapS = kPearSmallSpacing * us;

  // Section A: two half-width columns (row 1: Network + Bluetooth).

  // Section A row 1: Network + Bluetooth pills.
  L.row1Y = L.pad;
  L.row1H = 64.0 * us;
  L.netW = (popupW - L.pad * 2.0 - L.gapM) * 0.5;
  L.btW = L.netW;
  L.netX = L.pad;
  L.btX = L.pad + L.netW + L.gapM;

  // Section A row 2: Settings + Do Not Disturb pills.
  L.row2Y = L.row1Y + L.row1H + L.gapM;
  L.row2H = 56.0 * us;
  L.setW = L.netW;
  L.dndW = L.setW;
  L.setX = L.pad;
  L.dndX = L.pad + L.setW + L.gapM;

  // Toggles row (full width).
  L.showDnd = cfg.showDnd;
  L.toggles.clear();
  if (cfg.showDeviceLink && !cfg.deviceLinkCmd.empty()) L.toggles.push_back(PearToggle::DeviceLink);
  if (cfg.showNightColor) L.toggles.push_back(PearToggle::NightColor);
  if (cfg.showColorSwitcher) L.toggles.push_back(PearToggle::ColorScheme);
  if (cfg.showCamera) L.toggles.push_back(PearToggle::Camera);
  if (cfg.showCmd1) L.toggles.push_back(PearToggle::Cmd1);
  if (cfg.showCmd2) L.toggles.push_back(PearToggle::Cmd2);
  L.togX = L.pad;
  L.togY = L.row2Y + L.row2H + L.gapM;
  L.togH = L.toggles.empty() ? 0.0 : 64.0 * us;
  const size_t n = L.toggles.size();
  L.togW = (n == 0) ? 0.0 : (popupW - L.pad * 2.0 - static_cast<double>(n - 1) * L.gapS) / static_cast<double>(n);

  // Section B rows stack below Section A.
  double y = L.togY + L.togH + (L.toggles.empty() ? 0.0 : L.gapM);
  L.showVolume = cfg.showVolume;
  L.volY = y;
  L.volH = L.showVolume ? kPearSliderRowH * us : 0.0;
  if (L.showVolume) y += L.volH + L.gapM;
  // Output device switcher.
  {
    const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
    L.outDevRows = state.outputDevicesExpanded ? std::min(8, static_cast<int>(devs.size())) : 0;
    L.outDevY = y;
    L.outDevH = L.outDevRows > 0 ? (32.0 + static_cast<double>(L.outDevRows) * 34.0 + 12.0) * us : 0.0;
    if (L.outDevH > 0.0) y += L.outDevH + L.gapM;
  }
  L.showInput = cfg.showInput;
  L.inY = y;
  L.inH = L.showInput ? kPearSliderRowH * us : 0.0;
  if (L.showInput) y += L.inH + L.gapM;
  // Input device switcher.
  {
    const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
    L.inDevRows = state.inputDevicesExpanded ? std::min(8, static_cast<int>(devs.size())) : 0;
    L.inDevY = y;
    L.inDevH = L.inDevRows > 0 ? (32.0 + static_cast<double>(L.inDevRows) * 34.0 + 12.0) * us : 0.0;
    if (L.inDevH > 0.0) y += L.inDevH + L.gapM;
  }
  const bool briAvail = pear_brightness_available();
  L.showBrightness = cfg.showBrightness && briAvail;
  L.briY = y;
  L.briH = L.showBrightness ? kPearSliderRowH * us : 0.0;
  if (L.showBrightness) y += L.briH + L.gapM;
  L.showMedia = cfg.showMediaPlayer;
  L.mediaY = y;
  L.mediaH = L.showMedia ? kPearMediaRowH * us : 0.0;
  if (L.showMedia) y += L.mediaH + L.gapM;
  y += L.pad - L.gapM; // bottom pad (gap already added)

  // Networks overlay covers the whole popup when open. Skip the live scan
  // while closed so every paint/hit pass doesn't churn D-Bus snapshots.
  int rows = 0;
  if (state.networksOverlay) {
    if (wifiApCount >= 0) {
      rows = wifiApCount;
    } else {
      const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
      rows = std::min(kPearMaxWifiRows, static_cast<int>(aps.size()));
    }
  }
  L.wifiRows = rows;
  L.overlayH = L.pad + kPearNetHeaderH * us + static_cast<double>(rows) * kPearButtonH * us + L.pad;
  L.totalH = state.networksOverlay ? std::max(y, L.overlayH) : y;
  return L;
}

bool pear_spawn_screenshot_select() {
  // Prefer the EventHorizon binary next to this process (works from a build
  // dir / uninstalled tree), else rely on PATH (installed tree).
  std::string bin = "EventHorizon";
  {
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
      self[n] = '\0';
      const std::string exe = self;
      const auto slash = exe.find_last_of('/');
      const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
      if (::access((dir + "/EventHorizon").c_str(), X_OK) == 0) bin = dir + "/EventHorizon";
    }
  }
  pid_t pid = -1;
  const char* argv[] = {bin.c_str(), "--eh-screenshot", "--select", "--preview", nullptr};
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
  const int rc = posix_spawnp(&pid, bin.c_str(), nullptr, &attr,
                              const_cast<char* const*>(argv), environ);
  posix_spawnattr_destroy(&attr);
  return rc == 0 && pid > 1;
}

namespace {
// FNV-1a helpers for the popup paint signature.
void pear_sig_u64(std::uint64_t& h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}
void pear_sig_int(std::uint64_t& h, long long v) { pear_sig_u64(h, static_cast<std::uint64_t>(v)); }
void pear_sig_str(std::uint64_t& h, const std::string& s) {
  h ^= 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
}
std::uint64_t g_pear_sig = 0;
bool g_pear_sig_init = false;
} // namespace

bool pear_popup_signature_changed(ControlCenterState& state,
                                  const eh::config::ShellConfig& sc,
                                  const std::string& widgetId,
                                  eh::mpris::DockMpris* mpris, double popupW) {
  const PearCenterConfig cfg = pear_center_config(sc, widgetId);
  const double uiScale = dock_ui_scale(sc.dock);
  const PearLayout L = cc_compute_pear_layout(popupW, state, cfg, uiScale);
  std::uint64_t h = 1469598103934665603ULL;
  pear_sig_int(h, static_cast<long long>(L.totalH * 1000.0));
  pear_sig_int(h, cfg.showDnd ? 1 : 0);
  pear_sig_int(h, static_cast<long long>(L.toggles.size()));
  for (auto t : L.toggles) pear_sig_int(h, static_cast<int>(t));
  pear_sig_int(h, state.networksOverlay ? 1 : 0);
  pear_sig_int(h, L.wifiRows);
  pear_sig_str(h, state.wifiLastError);
  pear_sig_int(h, state.wifiPasswordPrompt ? 1 : 0);
  pear_sig_str(h, state.wifiPendingSsid);

  const auto ns = eh::shell::dock_slot_hooks::control_center_network_state();
  pear_sig_int(h, ns.connected ? 1 : 0);
  pear_sig_int(h, ns.wifi ? 1 : 0);
  pear_sig_int(h, ns.ethernet ? 1 : 0);
  pear_sig_str(h, ns.ssid);
  pear_sig_str(h, ns.iface);
  const auto bs = eh::shell::dock_slot_hooks::control_center_bluetooth_state();
  pear_sig_int(h, bs.powered ? 1 : 0);
  {
    std::string sub;
    if (bs.powered) {
      const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
      for (const auto& d : devs) {
        if (d.connected) {
          sub = d.alias.empty() ? d.address : d.alias;
          break;
        }
      }
    }
    pear_sig_str(h, sub);
  }
  pear_sig_int(h, sc.notifications.doNotDisturb ? 1 : 0);
  pear_sig_int(h, sc.nightLight.enabled ? 1 : 0);
  pear_sig_int(h, cfg.isDarkTheme);
  const auto ao = eh::shell::dock_slot_hooks::control_center_audio_output_state();
  pear_sig_int(h, ao.volume_pct);
  pear_sig_int(h, ao.muted ? 1 : 0);
  pear_sig_str(h, ao.device_name);
  const auto ai = eh::shell::dock_slot_hooks::control_center_audio_input_state();
  pear_sig_int(h, ai.volume_pct);
  pear_sig_int(h, ai.muted ? 1 : 0);
  pear_sig_str(h, ai.device_name);
  pear_sig_int(h, pear_brightness_pct());
  {
    const auto outs = eh::shell::dock_slot_hooks::control_center_output_devices();
    pear_sig_int(h, state.outputDevicesExpanded ? 1 : 0);
    pear_sig_int(h, static_cast<long long>(outs.size()));
    for (const auto& d : outs) {
      pear_sig_str(h, d.sink_name + "|" + d.display_name);
      pear_sig_int(h, d.is_default ? 1 : 0);
    }
    pear_sig_str(h, state.outputDevicesPendingSink);
    const auto ins = eh::shell::dock_slot_hooks::control_center_input_devices();
    pear_sig_int(h, state.inputDevicesExpanded ? 1 : 0);
    pear_sig_int(h, static_cast<long long>(ins.size()));
    for (const auto& d : ins) {
      pear_sig_str(h, d.source_name + "|" + d.display_name);
      pear_sig_int(h, d.is_default ? 1 : 0);
    }
    pear_sig_str(h, state.inputDevicesPendingSource);
  }
  if (L.showMedia && mpris) {
    const auto ms = mpris->snapshot();
    pear_sig_int(h, ms.active ? 1 : 0);
    pear_sig_str(h, ms.title);
    pear_sig_str(h, ms.artist);
    pear_sig_str(h, ms.playback_status);
    pear_sig_int(h, ms.art ? 1 : 0);
    pear_sig_int(h, (ms.can_go_previous ? 1 : 0) * 1 + (ms.can_play || ms.can_pause ? 2 : 0) +
                        (ms.can_go_next ? 4 : 0));
  } else {
    pear_sig_int(h, L.showMedia ? 1 : 0);
  }
  if (!g_pear_sig_init || h != g_pear_sig) {
    g_pear_sig = h;
    g_pear_sig_init = true;
    return true;
  }
  return false;
}

} // namespace eh::shell::dock::control_center
