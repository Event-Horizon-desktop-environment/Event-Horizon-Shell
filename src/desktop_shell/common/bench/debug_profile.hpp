#pragma once

#include <cstdlib>

namespace eh::debug_profile {

inline bool eh_debug_enabled() noexcept {
  static const bool enabled = []() noexcept {
    const char* e = std::getenv("EH_DEBUG");
    if (!e) return false;
    char* end = nullptr;
    const long v = std::strtol(e, &end, 10);
    return end != e && *end == '\0' && v != 0;
  }();
  return enabled;
}

inline int env_int(const char* name, int default_val) noexcept {
  const char* e = std::getenv(name);
  if (!e || !e[0]) return default_val;
  if (e[0] == '0' && e[1] == '\0') return 0;
  char* end = nullptr;
  const long v = std::strtol(e, &end, 10);
  if (end == e || *end != '\0') return default_val;
  return static_cast<int>(v);
}

inline bool env_bool(const char* name, bool /*default_on*/ = false) noexcept {
  // Master EH_DEBUG enables all debug flags
  if (eh_debug_enabled()) return true;
  // Otherwise check the specific env var
  return env_int(name, 0) != 0;
}

} // namespace eh::debug_profile
