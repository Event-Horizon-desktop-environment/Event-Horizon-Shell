#pragma once

#include "desktop_shell/common/bench/debug_profile.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace eh::bench {

inline void bench_write(const char*, const char*) {}

} // namespace eh::bench
