#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

inline void debug_log(const char* tag, const char* fmt, ...) {
  static FILE* log_file = []() -> FILE* {
    const char* home = std::getenv("HOME");
    if (!home) return nullptr;
    char path[4096];
    int n = std::snprintf(path, sizeof(path), "%s/event-horizon-debug.log", home);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path)) return nullptr;
    // Rotate once if a previous run let the file balloon.
    struct stat st;
    if (::stat(path, &st) == 0 && st.st_size > 64 * 1024 * 1024) {
      char old[4128];
      if (::snprintf(old, sizeof(old), "%s.old", path) > 0) ::rename(path, old);
    }
    FILE* fp = std::fopen(path, "a");
    if (fp) std::setvbuf(fp, nullptr, _IONBF, 0);
    return fp;
  }();
  if (!log_file) return;

  // Stable per-process prefix ("dock:342227") computed once: the shell runs
  // as several processes appending to this same file, and without it their
  // interleaved output is impossible to attribute.
  static const char* proc_prefix = []() -> const char* {
    static char buf[320];
    char exe[256];
    ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) n = 0;
    exe[n] = '\0';
    const char* slash = std::strrchr(exe, '/');
    const char* base = slash ? slash + 1 : exe;
    std::snprintf(buf, sizeof(buf), "%.200s:%d", base, static_cast<int>(::getpid()));
    return buf;
  }();

  static std::mutex mtx;
  std::lock_guard<std::mutex> lock(mtx);

  const std::time_t t = std::time(nullptr);
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);

  char time_buf[64];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

  std::fprintf(log_file, "[%s] [%s] [%s] ", time_buf, proc_prefix, tag);

  va_list args;
  va_start(args, fmt);
  std::vfprintf(log_file, fmt, args);
  va_end(args);

  std::fprintf(log_file, "\n");
  std::fflush(log_file);
}

inline bool debug_log_crash(const char*) { return false; }
