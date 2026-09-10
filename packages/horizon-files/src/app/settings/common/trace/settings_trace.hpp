#pragma once

#include "platform/common/bench/bench_trace.hpp"
#include "platform/common/bench/debug_profile.hpp"

namespace eh::settings::trace {

[[nodiscard]] inline bool bench() noexcept {
  static int cached = -1;
  if (cached < 0) {
    cached = (eh::bench::enabled() || eh::debug_profile::env_bool("EH_SETTINGS_BENCH")) ? 1 : 0;
  }
  return cached != 0;
}

[[nodiscard]] inline bool debug() noexcept {
  static int cached = -1;
  if (cached < 0) {
    cached = eh::debug_profile::env_bool("EH_SETTINGS_DEBUG") ? 1 : 0;
  }
  return cached != 0;
}

}
