#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>

namespace eh::shell::overview {

enum class Stage : std::uint8_t {
  AnimTick,
  UpdateScroll,
  MaybeRefreshData,
  RevealTick,
  LiveStreamTick,
  RecomputeLayout,
  HoverUpdates,
  CairoClear,
  PaintSearchBar,
  PaintAppGrid,
  PaintWorkspaceCards,
  PaintStrip,
  CairoFlush,
  VkPresent,
  RegionCommit,
  Count,
};

inline const char* stage_name(Stage p) {
  switch (p) {
    case Stage::AnimTick:             return "anim_tick";
    case Stage::UpdateScroll:         return "update_scroll";
    case Stage::MaybeRefreshData:     return "maybe_refresh_data";
    case Stage::RevealTick:           return "reveal_tick";
    case Stage::LiveStreamTick:       return "live_stream_tick";
    case Stage::RecomputeLayout:      return "recompute_layout";
    case Stage::HoverUpdates:         return "hover_updates";
    case Stage::CairoClear:           return "cairo_clear";
    case Stage::PaintSearchBar:       return "paint_search_bar";
    case Stage::PaintAppGrid:         return "paint_app_grid";
    case Stage::PaintWorkspaceCards:  return "paint_workspace_cards";
    case Stage::PaintStrip:           return "paint_strip";
    case Stage::CairoFlush:           return "cairo_flush";
    case Stage::VkPresent:            return "vk_present";
    case Stage::RegionCommit:         return "region_commit";
    default:                          return "unknown";
  }
}

struct FrameStats {
  std::array<double, static_cast<size_t>(Stage::Count)> stageUs{};
  double totalUs = 0.0;
  int totalWindows = 0;
  int totalWorkspaces = 0;
  int displayW = 0;
  int displayH = 0;
  int cardCacheHits = 0;
  int cardCacheMisses = 0;
  int winCacheHits = 0;
  int winCacheMisses = 0;
  int stripCacheHits = 0;
  int stripCacheMisses = 0;
};

class OverviewProfiler {
public:
  static OverviewProfiler& instance() {
    static OverviewProfiler s;
    return s;
  }

  void begin_frame(int nWs, int nWins, int dispW = 0, int dispH = 0) {
    if (!enabled_) return;
    frame_++;
    current_ = {};
    current_.totalWorkspaces = nWs;
    current_.totalWindows = nWins;
    current_.displayW = dispW;
    current_.displayH = dispH;
    t0_ = now_ns();
    stageStart_ = t0_;
  }

  void begin_stage(Stage /*p*/) {
    if (!enabled_) return;
    stageStart_ = now_ns();
  }

  void end_stage(Stage p) {
    if (!enabled_) return;
    const uint64_t end = now_ns();
    current_.stageUs[static_cast<size_t>(p)] += us_between(stageStart_, end);
  }

  void set_cache_stats(int cardHits, int cardMisses, int winHits, int winMisses,
                       int stripHits, int stripMisses) {
    current_.cardCacheHits = cardHits;
    current_.cardCacheMisses = cardMisses;
    current_.winCacheHits = winHits;
    current_.winCacheMisses = winMisses;
    current_.stripCacheHits = stripHits;
    current_.stripCacheMisses = stripMisses;
  }

  void end_frame() {
    if (!enabled_) return;
    current_.totalUs = us_between(t0_, now_ns());
    ring_[writeIdx_] = current_;
    writeIdx_ = (writeIdx_ + 1) % kRingSize;

    if (frame_ % kReportEvery == 0)
      flush_report();
  }

private:
  static constexpr int kRingSize = 120;
  static constexpr int kReportEvery = 60;

  OverviewProfiler() {
    enabled_ = true;
    const char* home = std::getenv("HOME");
    if (home)
      std::snprintf(path_, sizeof(path_), "%s/event-horizon-overview-profile.log", home);
    FILE* fp = std::fopen(path_, "w");
    if (fp) std::fclose(fp);
  }

  static uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }

  static double us_between(uint64_t a, uint64_t b) {
    return static_cast<double>(b - a) / 1000.0;
  }

  void flush_report() {
    FILE* fp = std::fopen(path_, "a");
    if (!fp) return;

    const int n = std::min(frame_, kReportEvery);
    int idx = (writeIdx_ - n + kRingSize) % kRingSize;

    double totalMin = 1e18, totalMax = 0, totalSum = 0;
    std::array<double, static_cast<size_t>(Stage::Count)> pMin{}, pMax{}, pSum{};
    pMin.fill(1e18);
    pMax.fill(0);
    pSum.fill(0);
    int sumWins = 0, cardH = 0, cardM = 0, winH = 0, winM = 0, strpH = 0, strpM = 0;

    for (int i = 0; i < n; ++i) {
      const auto& f = ring_[(idx + i) % kRingSize];
      totalMin = std::min(totalMin, f.totalUs);
      totalMax = std::max(totalMax, f.totalUs);
      totalSum += f.totalUs;
      sumWins += f.totalWindows;
      cardH += f.cardCacheHits;
      cardM += f.cardCacheMisses;
      winH += f.winCacheHits;
      winM += f.winCacheMisses;
      strpH += f.stripCacheHits;
      strpM += f.stripCacheMisses;
      for (size_t p = 0; p < static_cast<size_t>(Stage::Count); ++p) {
        pMin[p] = std::min(pMin[p], f.stageUs[p]);
        pMax[p] = std::max(pMax[p], f.stageUs[p]);
        pSum[p] += f.stageUs[p];
      }
    }

    const double d = static_cast<double>(n);
    const double tAvg = totalSum / d;

    std::fprintf(fp, "\n=== Overview Profile (frames %d-%d) ===\n",
                 frame_ - n, frame_);
    std::fprintf(fp, "  resolution: %dx%d  workspaces: %d  avg_windows: %d\n",
                 current_.displayW, current_.displayH, current_.totalWorkspaces,
                 n > 0 ? sumWins / n : 0);
    std::fprintf(fp, "  card_cache: hits=%d misses=%d  win_cache: hits=%d misses=%d\n",
                 cardH, cardM, winH, winM);
    std::fprintf(fp, "  strip_cache: hits=%d misses=%d\n", strpH, strpM);
    std::fprintf(fp, "  total_ms: min=%.2f  avg=%.2f  max=%.2f\n",
                 totalMin / 1000.0, tAvg / 1000.0, totalMax / 1000.0);
    std::fprintf(fp, "  %-26s %10s %10s %10s  %6s\n",
                 "stage", "avg_us", "min_us", "max_us", "pct%");

    for (size_t p = 0; p < static_cast<size_t>(Stage::Count); ++p) {
      if (pSum[p] <= 0.0 && pMax[p] <= 0.0) continue;
      const double avg = pSum[p] / d;
      const double pct = tAvg > 0.0 ? (avg / tAvg) * 100.0 : 0.0;
      std::fprintf(fp, "  %-26s %10.0f %10.0f %10.0f  %5.1f%%\n",
                   stage_name(static_cast<Stage>(p)), avg, pMin[p], pMax[p], pct);
    }

    std::fprintf(fp, "=========================================\n");
    std::fflush(fp);
    std::fclose(fp);
  }

  uint64_t t0_ = 0;
  uint64_t stageStart_ = 0;
  int frame_ = 0;
  int writeIdx_ = 0;
  bool enabled_ = false;
  FrameStats current_{};
  std::array<FrameStats, kRingSize> ring_{};
  char path_[512] = {};
};

struct StageGuard {
  explicit StageGuard(Stage p) : stage_(p) {
    OverviewProfiler::instance().begin_stage(stage_);
  }
  ~StageGuard() { OverviewProfiler::instance().end_stage(stage_); }
private:
  Stage stage_;
};

} // namespace eh::shell::overview
