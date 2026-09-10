#pragma once

#include <chrono>
#include <cstdint>

#if defined(EH_USE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#else
#include <malloc.h>
#endif

namespace eh::shell::shared {

// Periodic allocator / font-cache maintenance for long-lived shell processes.
//
// Decode/paint spikes (wallpaper decompression, icon pixbufs, popup layouts)
// leave dirty heap pages behind after the objects are freed; without a trim
// the process keeps that RSS forever. Call tick() from the main loop — it is
// cheap (two timestamp compares) between the trim/fontmap intervals.
class PeriodicTrim {
public:
  explicit PeriodicTrim(uint64_t trimIntervalMs = 60000,
                        uint64_t fontClearIntervalMs = 120000)
      : trimEveryMs_(trimIntervalMs), fontEveryMs_(fontClearIntervalMs) {}

  void tick() {
    const uint64_t now = now_ms();
    if (now - lastTrim_ >= trimEveryMs_) {
      lastTrim_ = now;
      purge_heap();
    }
    if (fontEveryMs_ != 0 && now - lastFontClear_ >= fontEveryMs_) {
      lastFontClear_ = now;
      clear_font_cache();
    }
  }

  static void purge_heap() {
#if defined(EH_USE_JEMALLOC)
    mallctl("arenas.purge", nullptr, nullptr, nullptr, 0);
#else
    malloc_trim(0);
#endif
  }

  // Kept separate so callers that already manage their own font map can schedule
  // it independently; the Pango implementation lives in periodic_trim.cpp to
  // avoid dragging Pango headers into every TU.
  static void clear_font_cache();

private:
  static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }

  uint64_t trimEveryMs_;
  uint64_t fontEveryMs_;
  uint64_t lastTrim_ = 0;
  uint64_t lastFontClear_ = 0;
};

}  // namespace eh::shell::shared
