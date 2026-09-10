#include "core/logging.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <string>

namespace hs::core {

static FILE* g_log = nullptr;
static std::mutex g_log_mutex;
static bool g_initialized = false;

static void open_log() {
  const char* home = std::getenv("HOME");
  if (!home) home = "/tmp";
  std::string path = std::string(home) + "/horizon-shot.log";
  g_log = std::fopen(path.c_str(), "w");
  if (!g_log) {
    g_log = std::fopen("/tmp/horizon-shot.log", "w");
  }
}

void init_logging() {
  if (g_initialized) return;
  g_initialized = true;
  open_log();
  if (g_log) {
    std::fprintf(g_log, "--- horizon-shot log started ---\n");
    std::fflush(g_log);
  }
}

static void format_timestamp(char* buf, size_t len) {
  std::time_t now = std::time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  std::strftime(buf, len, "%H:%M:%S", &t);
}

void hs_log(const char* file, int line, const char* fmt, ...) {
  if (!g_initialized) init_logging();
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  hs_logv(file, line, fmt, ap);
  va_end(ap);
}

void hs_logv(const char* file, int line, const char* fmt, va_list ap) {
  if (!g_log) return;
  std::lock_guard<std::mutex> lock(g_log_mutex);

  char ts[32];
  format_timestamp(ts, sizeof(ts));

  pthread_t tid = pthread_self();
  unsigned long tid_hash = 0;
  std::memcpy(&tid_hash, &tid, sizeof(tid) < sizeof(tid_hash) ? sizeof(tid) : sizeof(tid_hash));

  std::fprintf(g_log, "[%s][%06lx] %s:%d: ", ts, tid_hash & 0xfffffful, file, line);
  std::vfprintf(g_log, fmt, ap);
  std::fprintf(g_log, "\n");
  std::fflush(g_log);
}

} // namespace hs::core
