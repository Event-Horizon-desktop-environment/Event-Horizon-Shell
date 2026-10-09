#pragma once

// Dedicated volume-mixer log:
//
//   ~/.local/state/event-horizon/horizon-volumemixer.log
//
// The mixer runs inside the horizon-dock process, so its diagnostics drown in
// horizon-dock.log. Everything mixer-specific — popup opens, per-frame render
// timings (ms), slow-frame breakdowns, slow input handling — goes here.
//
// Env controls:
//   EH_VOLUME_MIXER_LOG=0      fully disable this log
//   EH_VOLUME_MIXER_PERF=1     log every painted frame (default: only slow ones)
//   EH_VOLUME_MIXER_THRESH_MS  slow-frame threshold in ms (default 8.0)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace eh::shell::dock::popup::volume_mixer {

// Per-thread accumulators for costs incurred deep inside helpers
// (icon-theme cascade, text measuring) that the frame reporter reads back.
// Reset at the start of each paint.
inline thread_local long long t_mixer_icon_us = 0;
inline thread_local int t_mixer_icon_n = 0;
inline thread_local long long t_mixer_text_us = 0;
inline thread_local int t_mixer_text_n = 0;
// Last snapshot cost + whether it was served from the TTL cache.
inline thread_local long long t_mixer_last_snap_us = 0;
inline thread_local bool t_mixer_last_snap_hit = true;
// Last `pw-link -l` capture cost inside the snapshot (0 when cached).
inline thread_local long long t_mixer_last_link_us = 0;
// Slider drawing cost (paint_slider calls per frame).
inline thread_local long long t_mixer_slider_us = 0;
inline thread_local int t_mixer_slider_n = 0;

inline long long mixer_us_between(
    const std::chrono::steady_clock::time_point& a,
    const std::chrono::steady_clock::time_point& b) {
  return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

inline long long mixer_steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

class MixerLog {
 public:
  static MixerLog& instance() {
    static MixerLog inst;
    return inst;
  }

  static std::string path() {
    const char* home = std::getenv("HOME");
    const std::string base = home ? home : "/tmp";
    return base + "/.local/state/event-horizon/horizon-volumemixer.log";
  }

  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] bool perf_all() const { return perf_all_; }
  [[nodiscard]] double thresh_ms() const { return thresh_; }

  void write(const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!enabled_) return;
    if (!opened_) {
      const std::string p = path();
      f_ = std::fopen(p.c_str(), "a");
      opened_ = true;
      if (f_) {
        std::fprintf(f_, "--- mixer log opened pid=%d t=%s ---\n", static_cast<int>(::getpid()),
                     stamp().c_str());
        std::fflush(f_);
      }
    }
    if (!f_) return;
    std::fprintf(f_, "[%s] %s\n", stamp().c_str(), msg.c_str());
    std::fflush(f_);
  }

  void report_paint(double total_ms, double snap_ms, bool snap_hit, double link_ms, double layout_ms,
                    double chrome_ms, double header_ms, double master_ms, double devices_ms,
                    double streams_ms, double router_ms, double footer_ms, int devs, int streams) {
    const long long icon_us = t_mixer_icon_us;
    const int icon_n = t_mixer_icon_n;
    const long long text_us = t_mixer_text_us;
    const int text_n = t_mixer_text_n;
    const long long slider_us = t_mixer_slider_us;
    const int slider_n = t_mixer_slider_n;
    // Repaint tracking: ms since the previous paint + per-second rate. A tiny
    // dt (1-2ms) next to a slow frame means a repaint storm (e.g. every
    // pointer-motion event repainting), not an expensive frame.
    const long long now_ms = mixer_steady_ms();
    double dt_ms = 0.0;
    if (have_last_) {
      dt_ms = static_cast<double>(now_ms - last_paint_ms_);
      if (dt_ms < min_dt_ms_) min_dt_ms_ = dt_ms;
    }
    have_last_ = true;
    last_paint_ms_ = now_ms;
    const long long sec = now_ms / 1000;
    if (sec != cur_sec_) {
      if (sec_count_ > max_per_sec_) max_per_sec_ = sec_count_;
      cur_sec_ = sec;
      sec_count_ = 0;
    }
    sec_count_++;
    frames_++;
    total_all_ms_ += total_ms;
    if (total_ms > max_ms_) max_ms_ = total_ms;
    const bool slow = total_ms >= thresh_;
    if (slow) slow_++;
    if (!perf_all_ && !slow) {
      // Still emit a periodic summary so idle health is visible.
      if (frames_ % 300 == 0) summarize("paint");
      return;
    }
    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "paint total=%.2fms dt=%.1f snap=%.2f%s link=%.2f layout=%.2f chrome=%.2f "
                  "header=%.2f master=%.2f devs=%.2f streams=%.2f router=%.2f footer=%.2f "
                  "sliders=%.2f(n=%d) icons=%.2f(n=%d) text=%.2f(n=%d) dev=%d streams=%d",
                  total_ms, dt_ms, snap_ms, snap_hit ? "(hit)" : "(miss)", link_ms, layout_ms,
                  chrome_ms, header_ms, master_ms, devices_ms, streams_ms, router_ms, footer_ms,
                  slider_us / 1000.0, slider_n, icon_us / 1000.0, icon_n, text_us / 1000.0, text_n,
                  devs, streams);
    write(slow ? std::string("SLOW ") + buf : buf);
    if (frames_ % 300 == 0) summarize("paint");
  }

  void report_open(int devs, int streams, double snap_ms) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "open devs=%d streams=%d snap=%.2fms", devs, streams, snap_ms);
    write(buf);
  }

  void report_click(const char* action, double ms) {
    if (ms < 4.0 && !perf_all_) return;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "click action=%s %.2fms", action ? action : "?", ms);
    write(buf);
  }

  // Drag-path accounting: called per pointer-motion with the cost of the
  // synchronous PipeWire set_node_volume round-trip. Reported in the summary;
  // a high pwset_avg next to a repaint storm means the drag itself (not the
  // drawing) is the hitch.
  void note_motion_pw(long long us) {
    std::lock_guard<std::mutex> lk(mu_);
    motions_++;
    motion_pw_us_ += us;
  }

 private:
  MixerLog() {
    if (const char* e = std::getenv("EH_VOLUME_MIXER_LOG"))
      if (std::strcmp(e, "0") == 0) enabled_ = false;
    if (const char* e = std::getenv("EH_VOLUME_MIXER_PERF"))
      if (std::strcmp(e, "1") == 0) perf_all_ = true;
    if (const char* e = std::getenv("EH_VOLUME_MIXER_THRESH_MS")) {
      char* end = nullptr;
      const double v = std::strtod(e, &end);
      if (end != e && v > 0.0 && v < 10000.0) thresh_ = v;
    }
  }
  ~MixerLog() {
    if (f_) std::fclose(f_);
  }

  void summarize(const char* ctx) {
    if (frames_ == 0) return;
    const double pw_avg = motions_ > 0 ? static_cast<double>(motion_pw_us_) / 1000.0 / motions_ : 0.0;
    if (sec_count_ > max_per_sec_) max_per_sec_ = sec_count_;
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "summary[%s] frames=%llu slow=%llu avg=%.2fms max=%.2fms mindt=%.1fms "
                  "rate_max/s=%llu motions=%llu pwset_avg=%.2fms",
                  ctx, frames_, slow_, total_all_ms_ / static_cast<double>(frames_), max_ms_,
                  min_dt_ms_ > 1e8 ? 0.0 : min_dt_ms_, max_per_sec_, motions_, pw_avg);
    write(buf);
  }

  static std::string stamp() {
    char buf[32];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  ts.tv_nsec / 1000000);
    return buf;
  }

  std::mutex mu_;
  FILE* f_ = nullptr;
  bool opened_ = false;
  bool enabled_ = true;
  bool perf_all_ = false;
  double thresh_ = 8.0;
  unsigned long long frames_ = 0;
  unsigned long long slow_ = 0;
  double total_all_ms_ = 0.0;
  double max_ms_ = 0.0;
  // Repaint-rate tracking.
  bool have_last_ = false;
  long long last_paint_ms_ = 0;
  double min_dt_ms_ = 1e9;
  long long cur_sec_ = -1;
  unsigned long long sec_count_ = 0;
  unsigned long long max_per_sec_ = 0;
  // Drag-motion accounting.
  unsigned long long motions_ = 0;
  long long motion_pw_us_ = 0;
};

inline void mixer_log(const std::string& msg) {
  if (MixerLog::instance().enabled()) MixerLog::instance().write(msg);
}

}  // namespace eh::shell::dock::popup::volume_mixer
