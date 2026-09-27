#pragma once

// Notification pipeline benchmarks + failure log (header-only so no meson
// changes are needed; each process gets its own instance).
//
// What it tracks, per process:
//   sender (mpris hosts: dock/stage/desktop)
//     - art-load kick → completion latency (avg/max), ok/fail counts
//     - pushes (mpris / art bytes) and emit-without-art reasons
//   receiver (horizon-notifications)
//     - mpris/art frames received, art attached, drops by reason
//     - compute_layout / paint_impl latency (avg/max), slow-frame warnings
//   both: a capped ring of recent failure events for "what went wrong".
//
// Durations use steady_clock (per-hop only — never compared across
// processes). Call sites do their own channel logging (mpris_dbus on the
// sender, debug_log("notifications") on the receiver); this header only
// records numbers and formats summary strings.

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>

namespace eh::shell::notifications::diag {

inline std::int64_t steady_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct PipelineStats {
  std::mutex mu;

  // Sender: album-art loads.
  std::uint64_t artLoadsOk = 0;
  std::uint64_t artLoadsFail = 0;
  std::uint64_t artLoadsStale = 0;
  std::uint64_t artLoadMsTotal = 0;
  std::uint64_t artLoadMsMax = 0;
  std::string artLoadInFlight;
  std::int64_t artLoadKickAtMs = 0;

  // Sender: pushes.
  std::uint64_t mprisPushed = 0;
  std::uint64_t artPushed = 0;
  std::uint64_t artBytesPushed = 0;
  std::uint64_t emitNoArt = 0;
  std::string lastNoArtReason;
  std::uint64_t pushFailed = 0;

  // Receiver: frames.
  std::uint64_t mprisRx = 0;
  std::uint64_t artRx = 0;
  std::uint64_t artRxBytes = 0;
  std::uint64_t artAttached = 0;
  std::uint64_t dropMalformed = 0;
  std::uint64_t dropNoEntry = 0;
  std::uint64_t dropBlob = 0;

  // Receiver: paint.
  std::uint64_t paints = 0;
  std::uint64_t paintMsTotal = 0;
  std::uint64_t paintMsMax = 0;
  std::uint64_t layouts = 0;
  std::uint64_t layoutMsTotal = 0;
  std::uint64_t layoutMsMax = 0;

  // Failure ring (newest at back, capped). Each entry carries the
  // changeSerial it was recorded at so reporters can poll for new ones
  // without losing entries to eviction.
  std::deque<std::pair<std::uint64_t, std::string>> recentFailures;
  static constexpr std::size_t kFailureCap = 16;

  // Bump on every record; reporters skip quiet periods.
  std::uint64_t changeSerial = 0;
  const std::int64_t bootMs = steady_ms_now();

  void noteFailureLocked(const std::string& msg) {
    recentFailures.emplace_back(changeSerial + 1, msg);
    while (recentFailures.size() > kFailureCap) recentFailures.pop_front();
  }

  std::string tplusLocked(std::int64_t nowMs) const {
    const double s = (nowMs - bootMs) / 1000.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "T+%.1fs", s);
    return std::string(buf);
  }

  // ---- sender -----------------------------------------------------------
  void noteArtLoadKick(const std::string& url) {
    std::lock_guard<std::mutex> lock(mu);
    artLoadInFlight = url;
    artLoadKickAtMs = steady_ms_now();
    ++changeSerial;
  }

  // Returns the load latency in ms (-1 when no kick was recorded).
  std::int64_t noteArtLoadDone(bool ok, const std::string& url) {
    std::lock_guard<std::mutex> lock(mu);
    const std::int64_t now = steady_ms_now();
    const std::int64_t ms = artLoadKickAtMs > 0 ? now - artLoadKickAtMs : -1;
    artLoadInFlight.clear();
    artLoadKickAtMs = 0;
    if (ok) {
      ++artLoadsOk;
      if (ms >= 0) {
        artLoadMsTotal += static_cast<std::uint64_t>(ms);
        artLoadMsMax = std::max(artLoadMsMax, static_cast<std::uint64_t>(ms));
      }
    } else {
      ++artLoadsFail;
      std::ostringstream os;
      os << "[" << tplusLocked(now) << "] art-load FAIL url=" << url;
      if (ms >= 0) os << " after_ms=" << ms;
      noteFailureLocked(os.str());
    }
    ++changeSerial;
    return ms;
  }

  void noteArtLoadStale(const std::string& url) {
    std::lock_guard<std::mutex> lock(mu);
    ++artLoadsStale;
    std::ostringstream os;
    os << "[" << tplusLocked(steady_ms_now()) << "] art-load STALE url=" << url;
    noteFailureLocked(os.str());
    ++changeSerial;
  }

  void notePushMpris() {
    std::lock_guard<std::mutex> lock(mu);
    ++mprisPushed;
    ++changeSerial;
  }

  void notePushArt(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mu);
    ++artPushed;
    artBytesPushed += bytes;
    ++changeSerial;
  }

  void noteEmitNoArt(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mu);
    ++emitNoArt;
    lastNoArtReason = reason;
    ++changeSerial;
  }

  // A push_*() call that went nowhere (no sender installed or bus down).
  // This is the silent toast-killer: log-loud by recording a failure entry.
  void notePushFailed(const std::string& what) {
    std::lock_guard<std::mutex> lock(mu);
    ++pushFailed;
    std::ostringstream os;
    os << "[" << tplusLocked(steady_ms_now()) << "] push FAILED kind=" << what
       << " (no IPC sender or bus down — toast never left this process)";
    noteFailureLocked(os.str());
    ++changeSerial;
  }

  std::uint64_t artAvgMs() const {
    const std::uint64_t n = artLoadsOk;
    return n ? artLoadMsTotal / n : 0;
  }

  std::string senderSummary() {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream os;
    os << "art loads ok=" << artLoadsOk << " fail=" << artLoadsFail << " stale=" << artLoadsStale
       << " avg_ms=" << artAvgMs() << " max_ms=" << artLoadMsMax << " pushed mpris=" << mprisPushed
       << " art=" << artPushed << " (" << artBytesPushed << "B) no_art=" << emitNoArt
       << " push_failed=" << pushFailed;
    if (!lastNoArtReason.empty()) os << " last_no_art=" << lastNoArtReason;
    return os.str();
  }

  // ---- receiver ---------------------------------------------------------
  void noteMprisRx() {
    std::lock_guard<std::mutex> lock(mu);
    ++mprisRx;
    ++changeSerial;
  }

  void noteArtRx(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mu);
    ++artRx;
    artRxBytes += bytes;
    ++changeSerial;
  }

  void noteArtAttached() {
    std::lock_guard<std::mutex> lock(mu);
    ++artAttached;
    ++changeSerial;
  }

  void noteDropMalformed() {
    std::lock_guard<std::mutex> lock(mu);
    ++dropMalformed;
    noteFailureLocked("[" + tplusLocked(steady_ms_now()) + "] drop MALFORMED art frame");
    ++changeSerial;
  }

  void noteDropNoEntry(const std::string& key, int w, int h) {
    std::lock_guard<std::mutex> lock(mu);
    ++dropNoEntry;
    std::ostringstream os;
    os << "[" << tplusLocked(steady_ms_now()) << "] drop NO-ENTRY key=" << key << " (" << w << "x"
       << h << ")";
    noteFailureLocked(os.str());
    ++changeSerial;
  }

  void noteDropBlob(std::uint64_t bytes, const std::string& key) {
    std::lock_guard<std::mutex> lock(mu);
    ++dropBlob;
    std::ostringstream os;
    os << "[" << tplusLocked(steady_ms_now()) << "] drop BLOB-DECODE key=" << key
       << " bytes=" << bytes;
    noteFailureLocked(os.str());
    ++changeSerial;
  }

  void notePaint(std::uint64_t ms) {
    std::lock_guard<std::mutex> lock(mu);
    ++paints;
    paintMsTotal += ms;
    paintMsMax = std::max(paintMsMax, ms);
    ++changeSerial;
  }

  void noteLayout(std::uint64_t ms) {
    std::lock_guard<std::mutex> lock(mu);
    ++layouts;
    layoutMsTotal += ms;
    layoutMsMax = std::max(layoutMsMax, ms);
    ++changeSerial;
  }

  std::uint64_t paintAvgMs() const {
    return paints ? paintMsTotal / paints : 0;
  }

  std::uint64_t layoutAvgMs() const {
    return layouts ? layoutMsTotal / layouts : 0;
  }

  std::string receiverSummary() {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream os;
    os << "rx mpris=" << mprisRx << " art=" << artRx << " (" << artRxBytes
       << "B) attached=" << artAttached << " drops malformed=" << dropMalformed
       << " no_entry=" << dropNoEntry << " blob=" << dropBlob << " paint n=" << paints
       << " avg_ms=" << paintAvgMs() << " max_ms=" << paintMsMax << " layout n=" << layouts
       << " avg_ms=" << layoutAvgMs() << " max_ms=" << layoutMsMax;
    return os.str();
  }

  // Failures recorded after |seenSerial| (call with a cursor you keep,
  // initially 0); updates the cursor to the current serial.
  std::string failuresSince(std::uint64_t& seenSerial) {
    std::lock_guard<std::mutex> lock(mu);
    std::ostringstream os;
    for (const auto& [serial, f] : recentFailures) {
      if (serial <= seenSerial) continue;
      os << "  " << f << "\n";
    }
    seenSerial = changeSerial;
    return os.str();
  }

  std::uint64_t serial() {
    std::lock_guard<std::mutex> lock(mu);
    return changeSerial;
  }
};

inline PipelineStats& pipeline_stats() {
  static PipelineStats s;
  return s;
}

// RAII wall-timer: records into PipelineStats on destruction so early
// returns are measured too.
template <void (PipelineStats::*Note)(std::uint64_t)>
class ScopedMs {
 public:
  ScopedMs() : t0_(steady_ms_now()) {}
  ~ScopedMs() {
    const std::int64_t ms = steady_ms_now() - t0_;
    (pipeline_stats().*Note)(static_cast<std::uint64_t>(ms > 0 ? ms : 0));
  }
  std::int64_t elapsedMs() const { return steady_ms_now() - t0_; }

 private:
  std::int64_t t0_;
};

using ScopedPaint = ScopedMs<&PipelineStats::notePaint>;
using ScopedLayout = ScopedMs<&PipelineStats::noteLayout>;

}  // namespace eh::shell::notifications::diag
