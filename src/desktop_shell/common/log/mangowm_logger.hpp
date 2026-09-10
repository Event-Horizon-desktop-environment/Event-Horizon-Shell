#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <pthread.h>

#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>

inline bool mangowm_log_enabled() noexcept {
  static const bool k = [] {
    const char* e = std::getenv("EH_STARTUP_TRACE");
    return e && e[0] && e[0] != '0';
  }();
  return k;
}

inline void mangowm_log_line(char level, const char* fmt, ...) {
  if (!mangowm_log_enabled()) return;
  const auto now = std::chrono::system_clock::now();
  std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&tt, &tm);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  flockfile(stderr);
  std::fprintf(stderr, "[mango %c %02d:%02d:%02d.%03d pid=%ld] ", level, tm.tm_hour, tm.tm_min, tm.tm_sec,
               static_cast<int>(ms), static_cast<long>(getpid()));
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
  funlockfile(stderr);
}

#define MANGOWM_LOG(level, ...)     do {} while (false)
#define MANGOWM_TRACE(...)          do {} while (false)
#define MANGOWM_DEBUG(...)          do {} while (false)
#define MANGOWM_INFO(...)           mangowm_log_line('I', __VA_ARGS__)
#define MANGOWM_WARN(...)           mangowm_log_line('W', __VA_ARGS__)
#define MANGOWM_ERROR(...)          mangowm_log_line('E', __VA_ARGS__)
#define MANGOWM_CRITICAL(...)       mangowm_log_line('C', __VA_ARGS__)
#define MANGOWM_SCOPE(name)         do {} while (false)
#define MANGOWM_FN()                do {} while (false)
