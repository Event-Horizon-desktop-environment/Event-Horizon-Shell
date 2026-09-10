#include "desktop_shell/common/time/mono_time.hpp"

#include <time.h>

namespace eh::shell {

uint64_t now_mono_ms() {
   
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
}

}
