#pragma once

// Dedicated control-center log:
//
//   ~/.local/state/event-horizon/horizon-controlcenter.log
//
// The control center runs inside the horizon-dock process, so its diagnostics
// drown in horizon-dock.log. Everything CC-specific (geometry, toggles,
// presses, paint/size mismatches) goes here instead, timestamped.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace eh::shell::dock::control_center {

class CcLog {
 public:
  static CcLog& instance() {
    static CcLog inst;
    return inst;
  }

  static std::string path() {
    const char* home = std::getenv("HOME");
    const std::string base = home ? home : "/tmp";
    return base + "/.local/state/event-horizon/horizon-controlcenter.log";
  }

  void write(const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!opened_) {
      const std::string p = path();
      f_ = std::fopen(p.c_str(), "a");
      opened_ = true;
      if (f_) {
        std::fprintf(f_, "--- cc log opened pid=%d t=%s ---\n", static_cast<int>(::getpid()),
                     stamp().c_str());
        std::fflush(f_);
      }
    }
    if (!f_) return;
    std::fprintf(f_, "[%s] %s\n", stamp().c_str(), msg.c_str());
    std::fflush(f_);
  }

 private:
  CcLog() = default;
  ~CcLog() {
    if (f_) std::fclose(f_);
  }

  static std::string stamp() {
    char buf[32];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld", tmv.tm_hour, tmv.tm_min,
                  tmv.tm_sec, ts.tv_nsec / 1000000);
    return buf;
  }

  std::mutex mu_;
  FILE* f_ = nullptr;
  bool opened_ = false;
};

inline void cc_log(const std::string& msg) { CcLog::instance().write(msg); }

// Scoped timing: logs only when the section exceeds the threshold, so normal
// frames stay silent and slow ones name themselves.
class CcTimer {
 public:
  CcTimer(const char* label, long long threshUs = 2000)
      : label_(label), thresh_(threshUs), t0_(std::chrono::steady_clock::now()) {}
  ~CcTimer() { finish(); }
  // Close the current section (logging if slow) and start a new one.
  void arm(const char* label, long long threshUs = 2000) {
    finish();
    label_ = label;
    thresh_ = threshUs;
    t0_ = std::chrono::steady_clock::now();
    done_ = false;
  }

 private:
  void finish() {
    if (done_) return;
    done_ = true;
    const long long us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0_)
            .count();
    if (us >= thresh_) {
      CcLog::instance().write(std::string("slow ") + label_ + " " + std::to_string(us) + "us");
    }
  }
  const char* label_;
  long long thresh_;
  std::chrono::steady_clock::time_point t0_;
  bool done_ = false;
};

} // namespace eh::shell::dock::control_center
