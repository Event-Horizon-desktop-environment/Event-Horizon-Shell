#pragma once

#include "platform/common/bench/debug_profile.hpp"

#include <iostream>
#include <time.h>

inline bool eh_startup_trace_enabled() noexcept {
  static const bool k = eh::debug_profile::env_bool("EH_STARTUP_TRACE");
  return k;
}

inline long eh_startup_elapsed_ms() noexcept {
  static const timespec t0 = []() noexcept {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
  }();
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<long>((now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L);
}

#define EH_ST_TRACE(expr) do {} while (false)
