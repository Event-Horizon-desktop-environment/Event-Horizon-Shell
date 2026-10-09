#pragma once

// Dedicated Sound-tab log:
//
//   ~/.local/state/event-horizon/Horizon-sound.log
//
// Dropdown open/pick tracing for the Sound tab (Default output/input,
// per-app routing, card profiles, engine lists): every press with an open
// dropdown logs the list geometry, the computed row, and the applied action
// with post-apply verification, so a "clicking does nothing" report is
// pinpointed in a single interaction. Append-only with fflush per line.

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace eh::settings::sound_log {

inline std::string path() {
  const char* home = std::getenv("HOME");
  const std::string base = (home && home[0]) ? home : "/tmp";
  return base + "/.local/state/event-horizon/Horizon-sound.log";
}

class SoundLog {
 public:
  static SoundLog& instance() {
    static SoundLog inst;
    return inst;
  }

  void write(const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!opened_) {
      opened_ = true;
      const std::string p = path();
      struct stat st {};
      if (::stat(p.c_str(), &st) == 0 && st.st_size > 8 * 1024 * 1024) {
        ::rename(p.c_str(), (p + ".old").c_str());
      }
      f_ = std::fopen(p.c_str(), "a");
      if (f_) {
        std::setvbuf(f_, nullptr, _IONBF, 0);
        std::fprintf(f_, "--- sound log opened pid=%d t=%s ---\n",
                     static_cast<int>(::getpid()), stamp().c_str());
        std::fflush(f_);
      }
    }
    if (!f_) return;
    std::fprintf(f_, "[%s pid=%d] %s\n", stamp().c_str(),
                 static_cast<int>(::getpid()), msg.c_str());
    std::fflush(f_);
  }

  void writef(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(buf);
  }

 private:
  SoundLog() = default;
  ~SoundLog() {
    if (f_) std::fclose(f_);
  }

  static std::string stamp() {
    char buf[48];
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv {};
    localtime_r(&ts.tv_sec, &tmv);
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                  tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000);
    return buf;
  }

  std::mutex mu_;
  FILE* f_ = nullptr;
  bool opened_ = false;
};

inline void snd_log(const std::string& msg) {
  SoundLog::instance().write(msg);
}

inline void snd_logf(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  SoundLog::instance().write(buf);
}

}  // namespace eh::settings::sound_log
