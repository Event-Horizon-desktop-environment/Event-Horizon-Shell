#pragma once

#include "platform/common/bench/debug_profile.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <string>

namespace eh::shell::mem {

inline std::uint64_t current_rss_kb() {
  std::FILE* f = std::fopen("/proc/self/status", "rb");
  if (!f) return 0;
  std::array<char, 4096> buf;
  const size_t n = std::fread(buf.data(), 1, buf.size() - 1, f);
  std::fclose(f);
  buf[n] = 0;
  const char* marker = "VmRSS:";
  const char* p = std::strstr(buf.data(), marker);
  if (!p) return 0;
  p += std::strlen(marker);
  while (*p == ' ' || *p == '\t') ++p;
  return static_cast<std::uint64_t>(std::atoll(p));
}

inline void log_mem_breakdown() {}

}
