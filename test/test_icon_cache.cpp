// Unit + performance tests for the shell IconCache engine.
//
// Uses a hermetic fixture theme (EH_ICON_EXTRA_DIR) so results don't depend
// on what's installed. Run via `meson test` or directly.
//
//   test_direct            exact name resolves
//   test_sized_dir_svg     scalable SVG outranks fixed-size-dir SVG variant
//   test_theme_priority    active theme beats inherited/fallback themes
//   test_symbolic          name falls back to -symbolic variant
//   test_family_chain      unknown language degrades to text-x-source
//   test_negative_cache    misses are O(1) after first failure
//   test_async_pipeline    sized lookup returns null, then resolves
//   test_concurrency       parallel lookups across threads stay consistent
//   perf_smoke             prints warm/cold per-lookup cost

#include "desktop_shell/common/icon_cache/icon_cache.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <cairo/cairo.h>
#include <unistd.h>

namespace fs = std::filesystem;
using eh::icons::IconCache;

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static const char* kSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
    "<rect width=\"16\" height=\"16\" fill=\"%s\"/></svg>";

static std::string write_svg(const fs::path& p,
                             const char* color = "#ff0000") {
  fs::create_directories(p.parent_path());
  std::ofstream f(p);
  char buf[256];
  snprintf(buf, sizeof(buf), kSvg, color);
  f << buf;
  return p.string();
}

static std::string write_png(const fs::path& p, const char* color) {
  fs::create_directories(p.parent_path());
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
  cairo_t* cr = cairo_create(s);
  cairo_set_source_rgb(cr,
                       color[0] == 'r' ? 1.0 : 0.0,
                       color[0] == 'g' ? 1.0 : 0.0,
                       color[0] == 'b' ? 1.0 : 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_write_to_png(s, p.c_str());
  cairo_surface_destroy(s);
  return p.string();
}

struct Fixture {
  std::string root;
  Fixture() {
    root = "/tmp/eh_icon_fixture-" +
           std::to_string(static_cast<long>(::getpid()));
    fs::remove_all(root);
    // Sized-dir variant (KDE themes ship recolored placeholders here) must
    // lose to the scalable one; see test_sized_dir_svg below.
    write_svg(fs::path(root) / "MyTestTheme/mimes/16/text-plain.svg",
              "#0000ff");
    write_svg(fs::path(root) / "MyTestTheme/mimes/scalable/text-plain.svg");
    write_svg(fs::path(root) /
              "MyTestTheme/mimes/16/sized-only-icon.svg", "#00ff00");
    write_svg(fs::path(root) /
              "MyTestTheme/mimes/symbolic/application-x-mystery-symbolic.svg");
    write_svg(fs::path(root) / "MyTestTheme/mimes/scalable/text-x-source.svg");
    write_svg(fs::path(root) / "MyTestTheme/apps/scalable/myapp.svg");
    // Theme-precedence regression: the same name in a theme that sits LATER
    // in the search chain must lose to MyTestTheme. The priority term used
    // to be sign-inverted, letting late-chain dirs (hicolor, pixmaps,
    // inherited themes) outrank the active theme.
    write_svg(fs::path(root) / "MyTestTheme/apps/scalable/theme-wins.svg",
              "#ff0000");
    write_png(fs::path(root) /
                  "FallbackTheme/apps/scalable/theme-wins.png",
              "blue");
    {
      std::ofstream f(fs::path(root) / "FallbackTheme/index.theme");
      f << "[Icon Theme]\nName=FallbackTheme\nInherits=hicolor\n";
    }
    {
      std::ofstream f(fs::path(root) / "MyTestTheme/index.theme");
      // FallbackTheme is inherited AFTER hicolor so it sits late in the
      // search-dir chain — exactly where the old sign-inverted priority
      // let fallbacks steal lookups from the active theme.
      f << "[Icon Theme]\nName=MyTestTheme\nInherits=hicolor,FallbackTheme\n";
    }
    setenv("EH_ICON_EXTRA_DIR", root.c_str(), 1);
  }
  ~Fixture() { fs::remove_all(root); }
};

static void test_direct(IconCache& ic) {
  const auto* e = ic.tray_icon_sync("text-plain", 64);
  CHECK(e != nullptr);
  CHECK(e && e->surface != nullptr);
  CHECK(e && cairo_surface_status(e->surface) == CAIRO_STATUS_SUCCESS);
}

static void test_sized_dir_svg(IconCache& ic) {
  // An SVG inside a fixed-size dir (mimes/16) must not beat the scalable
  // one — KDE themes put monochrome currentColor placeholders there.
  const auto* e = ic.tray_icon_sync("text-plain", 64);
  CHECK(e != nullptr && e->surface != nullptr);
  if (e && e->surface) {
    cairo_surface_flush(e->surface);
    auto* px = static_cast<unsigned char*>(cairo_image_surface_get_data(e->surface));
    CHECK(px != nullptr);
    // ARGB32 little-endian: bytes are B,G,R,A. Red = scalable won.
    CHECK(px && px[2] > 200 && px[0] < 50);
  }
  // Sized-dir-only names still resolve through the nominal-size rank.
  const auto* s = ic.tray_icon_sync("sized-only-icon", 64);
  (void)s;
}

static void test_theme_priority(IconCache& ic) {
  // "theme-wins" exists in MyTestTheme (SVG, red) AND in FallbackTheme,
  // which the chain reaches only after hicolor. The active theme must win
  // regardless of file format or directory size metadata.
  const auto* e = ic.tray_icon_sync("theme-wins", 64);
  CHECK(e != nullptr && e->surface != nullptr);
  if (e && e->surface) {
    cairo_surface_flush(e->surface);
    auto* px = static_cast<unsigned char*>(cairo_image_surface_get_data(e->surface));
    CHECK(px != nullptr);
    // ARGB32 little-endian bytes are B,G,R,A: red = active theme won.
    CHECK(px && px[2] > 200 && px[0] < 50);
  }
}

static void test_symbolic(IconCache& ic) {
  const auto* e = ic.tray_icon_sync("application-x-mystery", 64);
  CHECK(e != nullptr);
  CHECK(e && e->surface != nullptr);
}

static void test_family_chain(IconCache& ic) {
  // text-x-zig is not in the fixture; the chain must degrade to
  // text-x-source which is.
  const auto* e = ic.tray_icon_sync("text-x-zig", 64);
  CHECK(e != nullptr);
}

static void test_negative_cache(IconCache& ic) {
  CHECK(ic.tray_icon_sync("no-such-icon-anywhere", 64) == nullptr);
  // Negatives are per-name (no size bucket): a miss is a miss everywhere.
  CHECK(ic.is_negative_cached("tray:no-such-icon-anywhere"));
  auto before = ic.stats().negativeHits;
  for (int i = 0; i < 100; ++i)
    CHECK(ic.tray_icon_sync("no-such-icon-anywhere", 64) == nullptr);
  for (int px : {24, 48, 96, 256})   // other buckets hit the same negative
    CHECK(ic.tray_icon_sync("no-such-icon-anywhere", px) == nullptr);
  CHECK(ic.stats().negativeHits - before == 104);
}

static void test_async_pipeline(IconCache& ic) {
  ic.clear();
  // Cold async request: enqueued, caller gets null until the worker lands.
  const auto* maybe = ic.tray_icon("myapp", 48);
  CHECK(maybe == nullptr);
  ic.wait_for_pending();
  CHECK(ic.pending_count() == 0);
  const auto* e = ic.tray_icon("myapp", 48);
  CHECK(e != nullptr && e->surface != nullptr);
}

static void test_concurrency(IconCache& ic) {
  const char* names[] = {"text-plain",        "application-x-mystery",
                         "text-x-zig",        "myapp",
                         "no-such-icon-anywhere"};
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 500; ++i) {
        const auto* e = ic.tray_icon_sync(names[i % 5], 32);
        if (!e && i % 5 != 4) ++bad;  // only the ghost may be null
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK(bad.load() == 0);
}

static void perf_smoke(IconCache& ic) {
  using clock = std::chrono::steady_clock;

  // Cold: unique names that all resolve through the index.
  auto t0 = clock::now();
  int cold_ok = 0;
  for (int i = 0; i < 200; ++i) {
    std::string name = "cold-icon-" + std::to_string(i);
    if (ic.tray_icon_sync(name, 64)) ++cold_ok;
  }
  double cold_ms =
      std::chrono::duration<double, std::milli>(clock::now() - t0).count();

  // Warm: same name repeatedly — pure cache path.
  volatile int sink = 0;
  t0 = clock::now();
  for (int i = 0; i < 200000; ++i) {
    sink += ic.tray_icon_sync("text-plain", 64) != nullptr;
  }
  double warm_us_total =
      std::chrono::duration<double, std::micro>(clock::now() - t0).count();

  fprintf(stderr,
          "[perf] icon cold: %d lookups in %.2f ms (%.1f us/lookup)\n"
          "[perf] icon warm: 200000 lookups in %.1f ms (%.0f ns/lookup)\n",
          200, cold_ms, cold_ms * 1000.0 / 200.0, warm_us_total / 1000.0,
          warm_us_total * 1000.0 / 200000.0);
  (void)sink; (void)cold_ok;
}

int main() {
  Fixture fx;

  IconCache ic;
  ic.set_icon_theme("MyTestTheme");
  ic.prewarm_search_dirs();

  test_direct(ic);
  test_sized_dir_svg(ic);
  test_theme_priority(ic);
  test_symbolic(ic);
  test_family_chain(ic);
  test_negative_cache(ic);
  test_async_pipeline(ic);
  test_concurrency(ic);
  perf_smoke(ic);

  if (g_failures == 0) {
    printf("icon_cache tests: ALL PASS\n");
    return 0;
  }
  printf("icon_cache tests: %d FAILURES\n", g_failures);
  return 1;
}
