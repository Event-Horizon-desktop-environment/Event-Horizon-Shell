#include "desktop_shell/osd/brightness/osd_brightness.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/osd/host/osd_host.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace eh::shell::osd {
namespace {

std::optional<std::pair<std::filesystem::path, std::filesystem::path>> discover_backlight() {
   
  namespace fs = std::filesystem;
  const fs::path root("/sys/class/backlight");
  std::error_code ec;
  if (!fs::exists(root, ec)) return std::nullopt;
  for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
    if (!e.is_directory(ec)) continue;
    const fs::path cur = e.path() / "brightness";
    const fs::path mx = e.path() / "max_brightness";
    if (fs::exists(cur, ec) && fs::exists(mx, ec)) return std::pair{cur, mx};
  }
  return std::nullopt;
}

bool read_int(const std::filesystem::path& p, int& out) {
   
  std::ifstream f(p);
  if (!f) return false;
  f >> out;
  return true;
}

int g_last_pct = -1;
bool g_brightness_primed = false;

const char* brightness_glyph(float norm) {
   
  if (norm < 0.4f) return "brightness_low";
  return "brightness_high";
}

}

void osd_brightness_poll(DockApp& app) {
   
  if (!app.osdHost) return;

  static std::optional<std::pair<std::filesystem::path, std::filesystem::path>> paths = discover_backlight();
  if (!paths) return;

  using clock = std::chrono::steady_clock;
  static clock::time_point s_next = {};
  const clock::time_point now = clock::now();
  if (now < s_next) return;
  s_next = now + std::chrono::milliseconds(350);

  int cur = 0;
  int mx = 1;
  if (!read_int(paths->first, cur) || !read_int(paths->second, mx) || mx <= 0) return;

  const int pct = std::clamp(static_cast<int>(std::lround(static_cast<double>(cur) * 100.0 / static_cast<double>(mx))), 0, 100);
  if (!g_brightness_primed) {
    g_brightness_primed = true;
    g_last_pct = pct;
    return;
  }
  if (pct == g_last_pct) return;
  g_last_pct = pct;

  const float norm = std::clamp(static_cast<float>(cur) / static_cast<float>(mx), 0.f, 1.f);
  OsdContent c{};
  c.icon_ligature = brightness_glyph(norm);
  c.value_text = std::to_string(pct) + "%";
  c.progress = norm;
  app.osdHost->show(c);
}

}
