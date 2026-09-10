#pragma once

#include "platform/common/bench/debug_profile.hpp"

#include <iostream>

namespace eh::bench {

[[nodiscard]] inline bool enabled() noexcept {
  static int s_cached = -1;
  if (s_cached < 0) {
    s_cached = eh::debug_profile::env_bool("EH_BENCH") ? 1 : 0;
    if (s_cached) {
      std::cerr << "[eh-bench] EH_BENCH=1 — unified diagnostics on stderr:\n"
                << "  [settings-bench] / load_uncached / matugen_palette\n"
                << "  [dock-bench] / unified dock milestones\n"
                << "  [dock-bench] cc_open / [control-center] open\n"
                << "  [eh-bench] curl_fetch (weather APIs)\n"
                << "  [eh-bench] wallpaper_thumb_decode (gallery thumbs)\n";
    }
  }
  return s_cached != 0;
}

}
