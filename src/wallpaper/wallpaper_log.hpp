#pragma once

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>

namespace wallpaper_log {

inline std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

inline FILE* log_file() {
  static FILE* f = []() {
    FILE* fp = fopen("/tmp/wallpaper.log", "w");
    if (fp) {
      setvbuf(fp, nullptr, _IONBF, 0);
      fprintf(fp, "=== WALLPAPER TRACE LOG ===\n");
      fprintf(fp, "ts_us        thread_id        level  message\n");
      fprintf(fp, "------------------------------------------------\n");
    }
    return fp;
  }();
  return f;
}

inline void wp_log_raw(const char* level, const char* msg) {
  FILE* f = log_file();
  if (!f) return;
  auto now = std::chrono::steady_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
  pthread_t tid = pthread_self();
  std::lock_guard<std::mutex> lk(log_mutex());
  fprintf(f, "%-14lu 0x%-11lx %-6s %s\n", us, (unsigned long)tid, level, msg);
}

inline void wp_log_fmt(const char* level, const char* fmt, ...) {
  FILE* f = log_file();
  if (!f) return;
  auto now = std::chrono::steady_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
  pthread_t tid = pthread_self();
  va_list args;
  va_start(args, fmt);
  std::lock_guard<std::mutex> lk(log_mutex());
  fprintf(f, "%-14lu 0x%-11lx %-6s ", us, (unsigned long)tid, level);
  vfprintf(f, fmt, args);
  fprintf(f, "\n");
  va_end(args);
}

class ScopeTimer {
public:
  ScopeTimer(const char* func, const char* info = nullptr)
      : func_(func), start_(std::chrono::steady_clock::now()) {
    if (info) {
      char buf[512];
      int n = snprintf(buf, sizeof(buf), ">> %s  |  %s", func_, info);
      if (n > 0) wp_log_raw("ENTER", buf);
      else wp_log_raw("ENTER", func_);
    } else {
      wp_log_raw("ENTER", func_);
    }
  }
  ~ScopeTimer() {
    auto end = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    char buf[512];
    snprintf(buf, sizeof(buf), "<< %s  duration=%ldus", func_, (long)dur);
    wp_log_raw("EXIT ", buf);
  }
private:
  const char* func_;
  std::chrono::steady_clock::time_point start_;
};

} // namespace wallpaper_log

#define WP_LOG_RAW(level, msg) wallpaper_log::wp_log_raw(level, msg)
#define WP_LOG(fmt, ...) wallpaper_log::wp_log_fmt("MSG  ", fmt, ##__VA_ARGS__)
#define WP_ENTER() wallpaper_log::wp_log_raw("ENTER", __func__)
#define WP_EXIT() wallpaper_log::wp_log_raw("EXIT ", __func__)
#define WP_SCOPE() wallpaper_log::ScopeTimer WP_SCOPE_VAR(__LINE__)(__func__, nullptr)
#define WP_SCOPE_INFO(info) wallpaper_log::ScopeTimer WP_SCOPE_VAR(__LINE__)(__func__, info)
#define WP_SCOPE_VAR(line) WP_SCOPE_NAME(line)
#define WP_SCOPE_NAME(line) wp_scope_##line
