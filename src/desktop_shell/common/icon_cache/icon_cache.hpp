#pragma once

#include <cairo/cairo.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eh::icons {

constexpr std::size_t kMaxIconMissLogged = 256;

// Request size for app icons on scaled/HiDPI outputs or larger icon-size
// settings. app_icon(appId) (no size) is hardcoded to a 256px raster, which
// is smaller than the actual on-screen pixel size once dock/taskbar "scale"
// settings and Wayland output buffer_scale (2x/3x on HiDPI) are applied —
// the compositor then has to upscale that 256px surface, which is what
// produces the "blurry, low-res" icons. Callers that paint icons at a size
// that can exceed ~160 logical px after scaling should use
// app_icon(appId, pixelSize) instead, passing at least kHiResIconPx.
// bucket_for() rounds the request down to the nearest cache bucket and
// load_png()/load_svg() never upscale past what the theme actually ships,
// so requesting this size costs nothing when only a smaller source exists —
// it just removes the artificial 256px ceiling.
constexpr int kHiResIconPx = 384;

struct ThemeInfo {
  std::string id;
  std::string name;
  std::string path;
  bool hidden = false;
};

std::string detect_system_icon_theme();

std::vector<ThemeInfo> list_installed_icon_themes();

cairo_surface_t* load_theme_preview_icon(const std::string& themeDir, const std::string& iconName, int targetPx);

std::string theme_example_icon_name(const std::string& themeDir);

std::vector<std::string> theme_find_any_icons(const std::string& themeDir, int maxCount);

bool apply_icon_theme(const std::string& themeId);

// Logs (via debug_log tag "icons") resolve/load timing collected since the
// previous call and resets the window. ctx labels the report.
void eh_icons_perf_log_reset(const char* ctx);

// Performance/debug counters (monotonic since cache creation).
struct IconCacheStats {
  std::uint64_t cacheHits = 0;       // served from surface cache
  std::uint64_t negativeHits = 0;    // previously-missed name, O(1) rejection
  std::uint64_t indexLookups = 0;    // in-memory index probes
  std::uint64_t rasters = 0;         // svg/png decodes performed
  std::uint64_t indexBuilds = 0;     // theme directory scans performed
  std::uint64_t asyncEnqueued = 0;   // jobs handed to background loader
};

struct IconEntry {
  cairo_surface_t* surface = nullptr;
  int width = 0;
  int height = 0;
  std::size_t bytes = 0;

  std::list<std::string>::iterator lru_it{};
};

struct IconCacheData {
  std::unordered_map<std::string, IconEntry> cache{};
  std::size_t totalBytes = 0;
  std::string keyBuf;
  std::list<std::string> lru{};
  std::unordered_set<std::string> missLogged{};
  std::unordered_set<std::string> execBasenameMiss{};
  std::string themeOverride;
  // Explicitly pinned theme ("" = follow the system). Kept separate from
  // themeOverride, which always holds a CONCRETE id — otherwise auto mode
  // could never detect system switches again after the first set_icon_theme.
  std::string pinnedTheme;
  // Time-gate for ensureFresh()'s opportunistic system-theme probe
  // (auto mode only), so components without their own watchers still
  // follow system-wide icon-theme switches within seconds.
  std::int64_t lastAutoThemeProbeSec = 0;
  std::string resolvedThemeId;
  std::vector<std::string> searchDirs{};
  bool searchDirsBuilt = false;
  std::uint64_t generation = 0;

  // Performance additions.
  // Per-theme-directory index: icon name -> candidates across size dirs.
  // Built once per directory so lookups never touch the filesystem.
  struct IconCandidate {
    std::string path;
    unsigned short dirSize = 0; // nominal dir size, 0 = scalable/symbolic
    bool svg = false;
  };
  std::vector<std::unordered_map<std::string, std::vector<IconCandidate>>>
      dirIndexes;
  bool indexesBuilt = false;
  // Set while a worker scans theme dirs; paint threads must never block on
  // the index build, so the scan itself runs outside mtx.
  std::atomic<bool> indexesBeingBuilt{false};

  // Keys that fully failed resolution — rejected in O(1) afterwards.
  std::unordered_set<std::string> negativeKeys{};

  // Async loading state (guarded by mtx unless noted).
  std::mutex mtx;
  struct PendingLoad {
    std::string key;
    std::string name;
    int px = 0;
    std::uint64_t gen = 0;   // data generation at enqueue; stale results drop
  };
  std::deque<PendingLoad> queue{};
  std::unordered_set<std::string> queuedKeys{};
  // Jobs popped and currently resolving on the worker (lock-free reads).
  std::atomic<int> inFlight{0};
  std::unique_ptr<std::thread> worker{};
  std::condition_variable cv{};
  std::atomic<bool> quit{false};
  std::atomic<bool> workerRunning{false};

  ~IconCacheData();

  // Stats are atomic so bench/tests can read them lock-free.
  std::atomic<std::uint64_t> stCacheHits{0};
  std::atomic<std::uint64_t> stNegativeHits{0};
  std::atomic<std::uint64_t> stIndexLookups{0};
  std::atomic<std::uint64_t> stRasters{0};
  std::atomic<std::uint64_t> stIndexBuilds{0};
  std::atomic<std::uint64_t> stAsyncEnqueued{0};
};

class IconCache {
public:
  IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;
  IconCache(IconCache&&) = delete;
  IconCache& operator=(IconCache&&) = delete;
  ~IconCache();

  static cairo_surface_t* load_settings_logo_surface();

  const IconEntry* app_icon(const std::string& appId);

  // Size-aware variant — see kHiResIconPx above. Unlike tray_icon(name, px),
  // this resolves synchronously (same as app_icon(appId)) to avoid changing
  // the placeholder/retry behavior existing app-icon callers rely on.
  const IconEntry* app_icon(const std::string& appId, int pixelSize);

  const IconEntry* tray_icon(const std::string& iconName);

  // Size-aware variant used by hot paths (app grid, file listings). Returns
  // nullptr while the icon rasterizes on the background thread; the caller
  // draws its placeholder and simply retries next frame.
  const IconEntry* tray_icon(const std::string& iconName, int pixelSize);

  const IconEntry* app_icon_from_exec_basename(const std::string& execBasename);

  void set_icon_theme(std::string themeId);
  const std::string& icon_theme_override() const { return d_->themeOverride; }

  bool refresh_auto_theme_if_needed();
  void prewarm_search_dirs();

  // Monotonic counter bumped on every icon theme switch; lets renderers
  // detect "icons changed" cheaply and race-free (e.g. baked-page caches).
  std::uint64_t icon_theme_generation() const;

  // Explicitly pinned theme ("" = auto/follow system) — mirrors what config
  // stores, so callers can diff against settings without normalizing.
  std::string icon_theme() const {
    std::lock_guard<std::mutex> lk(d_->mtx);
    return d_->pinnedTheme;
  }

  // Test/bench hooks.
  IconCacheStats stats() const;
  bool is_negative_cached(const std::string& key) const;
  std::size_t pending_count() const;
  // Block until the background queue drains (tests/bench only).
  void wait_for_pending();
  // Resolve synchronously bypassing the background queue.
  const IconEntry* tray_icon_sync(const std::string& iconName, int pixelSize);
  // Drop cached rasters/negatives; indexes rebuild lazily on next lookup.
  void clear();

private:
  std::shared_ptr<IconCacheData> d_;
  // Synchronous resolve used by the legacy full-size entry points. Returns
  // the cached entry (or nullptr after a negative resolution).
  const IconEntry* resolve_and_cache(const std::string& key, const std::string& iconName,
                                     bool isTray, int px);
  void rebuild_search_dirs_if_needed();
  void build_indexes_if_needed();
  void enqueue_async(const std::string& key, const std::string& name, int px);
  void ensure_worker_started();
  void stop_worker();
  void clear_locked();
  void ensureFresh();
};

}
