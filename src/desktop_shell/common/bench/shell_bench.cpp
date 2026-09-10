#include "desktop_shell/common/bench/shell_bench.hpp"

#include "desktop_shell/common/bench/bench_trace.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"

#include <cstdlib>

namespace {

ShellBenchClock::time_point g_init_t0{};
bool g_have_init_t0 = false;
bool g_layer_detail_logged = false;
bool g_first_draw_detail_logged = false;

}

bool eh_dock_bench() {
   
  if (eh::bench::enabled()) return true;
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_DOCK_BENCH") ? 1 : 0;
  return cached != 0;
}

bool eh_cc_open_bench() {
   
  if (eh::bench::enabled()) return true;
  return eh::debug_profile::env_bool("EH_CC_OPEN_BENCH");
}

bool eh_menu_bench() {
   
  if (eh::bench::enabled()) return true;
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_MENU_BENCHMARK") ? 1 : 0;
  return cached != 0;
}

double shell_bench_ms_since(ShellBenchClock::time_point t0) {
   
  return std::chrono::duration<double, std::milli>(ShellBenchClock::now() - t0).count();
}

double shell_bench_ms_between(ShellBenchClock::time_point a, ShellBenchClock::time_point b) {
   
  return std::chrono::duration<double, std::milli>(b - a).count();
}

void shell_bench_set_init_t0(ShellBenchClock::time_point t) {
   
  g_init_t0 = t;
  g_have_init_t0 = true;
}

bool shell_bench_have_init_t0() { return g_have_init_t0; }

ShellBenchClock::time_point shell_bench_init_t0() { return g_init_t0; }

bool shell_bench_should_log_layer_create_detail() {   return eh_dock_bench() && !g_layer_detail_logged; }

void shell_bench_mark_layer_create_detail_logged() { g_layer_detail_logged = true; }

bool shell_bench_should_log_first_draw_detail() {   return eh_dock_bench() && !g_first_draw_detail_logged; }

void shell_bench_mark_first_draw_detail_logged() { g_first_draw_detail_logged = true; }
