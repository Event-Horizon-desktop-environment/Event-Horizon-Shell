#pragma once

#include "desktop_shell/common/bench/debug_profile.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <time.h>

namespace eh::app {
inline long ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}
}

namespace eh::app::wl_loop_diag {

inline long mono_ms() noexcept {
  static const timespec t0 = []() noexcept {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
  }();
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<long>((now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L);
}

inline int heartbeat_interval_sec() noexcept { return 0; }
inline int trace_level() noexcept { return 0; }
inline bool stall_env_explicit() noexcept { return false; }
inline bool stall_diag_enabled() noexcept { return false; }
inline int slow_threshold_ms() noexcept { return 50; }
inline int ppoll_log_min_ms() noexcept { return 1000; }
inline int merged_ppoll_timeout_sec() noexcept { return 30; }

inline void log_diag_banner_once(const char*) noexcept {}
inline void maybe_heartbeat(const char*) noexcept {}
inline void log_stall_if_slow(const char*, const char*, long) noexcept {}
inline void log_stall_handler_fd(const char*, int, long) noexcept {}
inline void log_ppoll_wait(const char*, int, long) noexcept {}
inline void log_iter_verbose(const char*, int, short, long, long, long, long) noexcept {}

struct StallScope {
  explicit StallScope(const char*, const char*) noexcept {}
  StallScope(const StallScope&) = delete;
  StallScope& operator=(const StallScope&) = delete;
  StallScope(StallScope&&) = delete;
  StallScope& operator=(StallScope&&) = delete;
};

}
