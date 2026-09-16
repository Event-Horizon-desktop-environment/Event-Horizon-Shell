#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Central EventHorizon logging. Every tag gets its own file under
// $HOME/EH-logs (never one shared blob), so each subsystem's log is isolated
// and greppable. The shell runs as several processes appending to the same
// files; each line carries a stable per-process "name:pid" prefix so
// interleaved output stays attributable.

namespace eh_log {

// Root of every EventHorizon log file: $HOME/EH-logs (created on first use).
inline const char* dir() noexcept {
  static const char* d = []() noexcept {
    static char buf[4096];
    const char* home = std::getenv("HOME");
    if (home && home[0])
      std::snprintf(buf, sizeof(buf), "%s/EH-logs", home);
    else
      std::snprintf(buf, sizeof(buf), "/tmp/EH-logs");
    ::mkdir(buf, 0755);
    return buf;
  }();
  return d;
}

// Some one-shot tags belong to one logical subsystem; fold them so each
// subsystem gets exactly one file.
inline const char* fold_tag(const char* tag) noexcept {
  if (!tag) return "misc";
  if (std::strcmp(tag, "scanning") == 0 || std::strcmp(tag, "launching") == 0 ||
      std::strcmp(tag, "launch_all_autostart") == 0 || std::strcmp(tag, "found") == 0)
    return "autostart";
  return tag;
}

inline std::string filename_for(const char* tag) noexcept {
  std::string out;
  for (const char* p = tag; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c >= 'A' && c <= 'Z')
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
      out.push_back(static_cast<char>(c));
    else
      out.push_back('_');
  }
  if (out.empty()) out = "misc";
  return out + ".log";
}

inline FILE* open_rotated(const char* path) noexcept {
  struct stat st;
  if (::stat(path, &st) == 0 && st.st_size > 64 * 1024 * 1024) {
    char old[4200];
    if (std::snprintf(old, sizeof(old), "%s.old", path) > 0) ::rename(path, old);
  }
  FILE* fp = std::fopen(path, "a");
  if (fp) std::setvbuf(fp, nullptr, _IONBF, 0);
  return fp;
}

inline FILE* file_for(const char* tag) noexcept {
  static std::mutex mtx;
  static FILE* cached[64];
  static std::string cached_names[64];
  static int ncached = 0;

  const char* folded = fold_tag(tag);
  std::lock_guard<std::mutex> lock(mtx);
  for (int i = 0; i < ncached; ++i) {
    if (cached_names[i] == folded) return cached[i];
  }
  char path[4096];
  std::snprintf(path, sizeof(path), "%s/%s", dir(), filename_for(folded).c_str());
  FILE* fp = open_rotated(path);
  if (ncached < 64) {
    cached[ncached] = fp;
    cached_names[ncached] = folded;
    ++ncached;
  }
  return fp;
}

// Append a raw, already-formatted line (newline/prefix handled by the caller)
// to the given tag's file. Used by loggers with their own line format (e.g.
// the mango startup logger) that still want a home in ~/EH-logs.
inline FILE* eh_log_file(const char* tag) noexcept { return file_for(tag); }

// Full printf-style line write: [time] [name:pid] [tag] message
inline void debug_log_va(const char* tag, const char* fmt, va_list args) noexcept {
  FILE* fp = file_for(tag);
  if (!fp) return;

  static std::mutex mtx;
  std::lock_guard<std::mutex> lock(mtx);

  static const char* proc_prefix = []() noexcept -> const char* {
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

  const std::time_t t = std::time(nullptr);
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  char time_buf[64];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

  char line[8192];
  int off =
    std::snprintf(line, sizeof(line), "[%s] [%s] [%s] ", time_buf, proc_prefix, tag ? tag : "");
  if (off < 0) off = 0;
  if (static_cast<size_t>(off) >= sizeof(line)) return;

  va_list cp;
  va_copy(cp, args);
  const int n = std::vsnprintf(line + off, sizeof(line) - off, fmt, cp);
  va_end(cp);
  if (n <= 0) return;

  size_t used = off + static_cast<size_t>(n);
  if (used >= sizeof(line)) used = sizeof(line) - 1;
  if (used + 1 < sizeof(line)) {
    line[used] = '\n';
    ++used;
  }
  std::fwrite(line, 1, used, fp);
  std::fflush(fp);
}

} // namespace eh_log

inline void debug_log(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  eh_log::debug_log_va(tag, fmt, args);
  va_end(args);
}

inline bool debug_log_crash(const char*) { return false; }