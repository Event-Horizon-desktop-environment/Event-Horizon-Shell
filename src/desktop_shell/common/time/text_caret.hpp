#pragma once

#include <cstdint>
#include <time.h>

namespace eh::shell {

inline uint64_t monotonic_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
}

inline bool text_caret_blink_on(uint64_t mono_ms, bool field_focused) {
  if (!field_focused) return false;
  constexpr uint64_t kHalfPeriodMs = 530;
  return ((mono_ms / kHalfPeriodMs) & 1ULL) == 0ULL;
}

}
