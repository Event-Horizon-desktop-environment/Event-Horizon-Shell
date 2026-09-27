#pragma once

// Dedicated Monitors-tab log:
//
//   ~/.local/state/event-horizon/Horizon-monitors.log
//
// Always-on (never gated behind EH_BENCH / EH_SETTINGS_BENCH). Everything
// monitors-specific goes here so perf work is greppable without drowning in
// settings-bench.log or ~/EH-logs/settings.log:
//
//   - paint timings (total + per-section breakdown, every frame when tab==7)
//   - render timings (draw-level shell_snapshot / cairo_body / commit, tab==7)
//   - button presses (Refresh, Restart portals, Apply, Revert, Center view,
//     Align top, canvas select/drag/pan, pills, toggle, combos, sliders,
//     ICC Browse/Clear, dropdown picks)
//   - data timings (refresh_from_system, save_and_reload, revert_edits)
//   - slow-frame warnings (>8ms paint) with the same breakdown
//
// File is append-only with fflush per line (crash-safe). Rotated to .old
// past 8 MiB so continuous drag benchmarking can't grow it unbounded.

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

namespace eh::settings::monitors_log {

inline std::string path() {
  const char* home = std::getenv("HOME");
  const std::string base = (home && home[0]) ? home : "/tmp";
  return base + "/.local/state/event-horizon/Horizon-monitors.log";
}

inline void ensure_parent_dir() {
  const char* home = std::getenv("HOME");
  const std::string base = (home && home[0]) ? home : "/tmp";
  const std::string dir = base + "/.local/state/event-horizon";
  // mkdir -p equivalent without <filesystem> (header-only, no exceptions).
  char tmp[1024];
  std::snprintf(tmp, sizeof(tmp), "%s", dir.c_str());
  for (char* p = tmp + 1; *p; ++p) {
    if (*p == '/') {
      *p = '\0';
      ::mkdir(tmp, 0755);
      *p = '/';
    }
  }
  ::mkdir(tmp, 0755);
}

class MonitorsLog {
 public:
  static MonitorsLog& instance() {
    static MonitorsLog inst;
    return inst;
  }

  void write(const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    ensure_open_locked();
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
  MonitorsLog() = default;
  ~MonitorsLog() {
    if (f_) std::fclose(f_);
  }

  void ensure_open_locked() {
    if (opened_) return;
    opened_ = true;
    ensure_parent_dir();
    const std::string p = path();
    struct stat st {};
    if (::stat(p.c_str(), &st) == 0 &&
        st.st_size > 8 * 1024 * 1024) {
      std::string oldp = p + ".old";
      ::rename(p.c_str(), oldp.c_str());
    }
    f_ = std::fopen(p.c_str(), "a");
    if (f_) {
      std::setvbuf(f_, nullptr, _IONBF, 0);
      std::fprintf(f_, "--- monitors log opened pid=%d t=%s ---\n",
                   static_cast<int>(::getpid()), stamp().c_str());
      std::fflush(f_);
    }
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

inline void mon_log(const std::string& msg) {
  MonitorsLog::instance().write(msg);
}

inline void mon_logf(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  MonitorsLog::instance().write(buf);
}

using MonClock = std::chrono::steady_clock;

inline long long mon_us(MonClock::time_point a,
                        MonClock::time_point b) noexcept {
  return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

// Slider-drag session probe (monitors tab). Drag start lives in
// settings_monitors_ui.inl, motion/end in settings_event_handlers.cpp, and
// per-frame paint cost in settings_tab_monitors.cpp — this header-inline
// probe keeps the counters in one place. Always-on, same file.
struct SliderDragProbe {
  static inline bool active = false;
  static inline bool isScale = true;
  static inline int kind = -1;  // hypr slider kind, or -1 for Scale
  static inline MonClock::time_point t0{};
  static inline unsigned motions = 0;
  static inline unsigned paints = 0;
  static inline long long paintSumUs = 0;
  static inline long long sliderSumUs = 0;
  static inline char out[128] = {};
};

inline void slider_drag_begin(bool scale, int kind, const char* outName) {
  if (SliderDragProbe::active) {
    // Single pointer means the old session leaked its end; close it first.
    SliderDragProbe::active = false;
    MonitorsLog::instance().writef("slider-drag end superseded out=%s",
                                   SliderDragProbe::out);
  }
  SliderDragProbe::active = true;
  SliderDragProbe::isScale = scale;
  SliderDragProbe::kind = kind;
  SliderDragProbe::t0 = MonClock::now();
  SliderDragProbe::motions = 0;
  SliderDragProbe::paints = 0;
  SliderDragProbe::paintSumUs = 0;
  SliderDragProbe::sliderSumUs = 0;
  std::snprintf(SliderDragProbe::out, sizeof(SliderDragProbe::out), "%s",
                outName ? outName : "-");
}

inline void slider_drag_motion() {
  if (SliderDragProbe::active) ++SliderDragProbe::motions;
}

inline void slider_drag_paint(long long paintUs, long long sliderUs) {
  if (!SliderDragProbe::active) return;
  ++SliderDragProbe::paints;
  SliderDragProbe::paintSumUs += paintUs;
  SliderDragProbe::sliderSumUs += sliderUs;
}

inline void slider_drag_end(const char* why) {  if (!SliderDragProbe::active) return;
  SliderDragProbe::active = false;
  const long long dur =
      std::chrono::duration_cast<std::chrono::microseconds>(MonClock::now() -
                                                            SliderDragProbe::t0)
          .count();
  const unsigned n = SliderDragProbe::paints;
  char what[32];
  if (SliderDragProbe::isScale)
    std::snprintf(what, sizeof(what), "Scale");
  else
    std::snprintf(what, sizeof(what), "kind%d", SliderDragProbe::kind);
  MonitorsLog::instance().writef(
      "slider-drag end %s why=%s out=%s motions=%u paints=%u dur=%lldus avgpaint=%lldus avgsliders=%lldus",
      what, why, SliderDragProbe::out, SliderDragProbe::motions, n, dur,
      n ? SliderDragProbe::paintSumUs / n : 0, n ? SliderDragProbe::sliderSumUs / n : 0);
}

// Scoped section timer: logs only when the section exceeds threshUs, so
// normal frames stay to the single paint-summary line while slow sections
// name themselves with zero per-frame cost when fast.
class MonTimer {
 public:
  MonTimer(const char* label, long long threshUs = 2000)
      : label_(label), thresh_(threshUs), t0_(MonClock::now()) {}
  ~MonTimer() { finish(); }
  void arm(const char* label, long long threshUs = 2000) {
    finish();
    label_ = label;
    thresh_ = threshUs;
    t0_ = MonClock::now();
    done_ = false;
  }

 private:
  void finish() {
    if (done_) return;
    done_ = true;
    const long long us =
        std::chrono::duration_cast<std::chrono::microseconds>(MonClock::now() -
                                                              t0_)
            .count();
    if (us >= thresh_) {
      MonitorsLog::instance().write(std::string("slow ") + label_ + " " +
                                    std::to_string(us) + "us");
    }
  }
  const char* label_;
  long long thresh_;
  MonClock::time_point t0_;
  bool done_ = false;
};

// Paint-cause tag: call sites record WHY the repaint was requested
// ("press:Apply", "drag", "release", ...). paint_monitors_tab takes it into
// the paint line; the draw-level render line reads the last taken value.
// Last-writer-wins across coalesced draws; defaults to "other".
inline const char*& mon_cause_slot() {
  static thread_local const char* cause = "other";
  return cause;
}
inline const char*& mon_cause_last_slot() {
  static thread_local const char* last = "other";
  return last;
}
inline void mon_cause_set(const char* cause) { mon_cause_slot() = cause ? cause : "other"; }
inline const char* mon_cause_take() {
  const char* c = mon_cause_slot();
  mon_cause_slot() = "other";
  mon_cause_last_slot() = c;
  return c;
}
inline const char* mon_cause_last() { return mon_cause_last_slot(); }

}  // namespace eh::settings::monitors_log
