#pragma once

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace eh::shell {

[[nodiscard]] inline std::chrono::milliseconds tray_session_defer_delay() {
  const char* e = std::getenv("EH_TRAY_DEFER_MS");
  if (!e || !e[0]) return std::chrono::milliseconds(500);
  char* end = nullptr;
  long v = std::strtol(e, &end, 10);
  if (end == e) return std::chrono::milliseconds(500);
  if (v < 0) return std::chrono::milliseconds(500);
  if (v == 0) return std::chrono::milliseconds(0);
  if (v > 600000) v = 600000;
  return std::chrono::milliseconds(v);
}

[[nodiscard]] inline bool tray_session_defer_enabled() { return tray_session_defer_delay().count() > 0; }

}
