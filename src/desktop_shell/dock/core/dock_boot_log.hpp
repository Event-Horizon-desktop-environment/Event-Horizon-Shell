#pragma once

// Unconditional boot tracer for the split-out dock child (horizon-dock).
//
// Every step of dock startup is printed to stderr — the supervisor redirects
// the child's stderr into horizon-dock.log — and mirrored into ~/EH-logs/dock.log
// via debug_log(). The most recent step is kept in a small static buffer so the
// wl_log protocol-error handler installed by run_dock_standalone() can report
// exactly what the dock was doing when the compositor killed the connection
// (e.g. Hyprland layer-shell violations like "y == 0 but anchor doesn't have
// top and bottom").
//
// The tracer is verbose-mode-independent: unlike EH_ST_TRACE it always runs,
// so the last lines before a crash never depend on an env flag being set.

#include "desktop_shell/common/log/debug_log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace eh::shell::dock {

namespace boot_detail {
inline std::mutex& mtx() {
  static std::mutex m;
  return m;
}
inline int& seq() {
  static int s = 0;
  return s;
}
inline char* step_buf() {
  static char buf[512];
  return buf;
}
inline constexpr std::size_t kStepBufSize = 512;
}  // namespace boot_detail

// Index of the last completed step (0 = none yet).
inline int dock_boot_step_index() {
  std::lock_guard<std::mutex> lock(boot_detail::mtx());
  return boot_detail::seq();
}

// Human-readable current step ("" when none yet).
inline const char* dock_boot_current_step() {
  std::lock_guard<std::mutex> lock(boot_detail::mtx());
  return boot_detail::step_buf();
}

// Mark a completed startup step. Always printed; sequence-numbered so a crash
// log shows the exact step that never completed.
inline void dock_boot_step(const char* fmt, ...) {
  char msg[512];
  {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
  }

  int idx = 0;
  {
    std::lock_guard<std::mutex> lock(boot_detail::mtx());
    idx = ++boot_detail::seq();
    (void)std::snprintf(boot_detail::step_buf(), boot_detail::kStepBufSize, "%s", msg);
  }

  std::fprintf(stderr, "[horizon-dock:boot] %03d %s\n", idx, msg);
  std::fflush(stderr);
  debug_log("dock", "boot %03d %s", idx, msg);
}

// One-shot trace of a layer surface being created. Printed unconditionally so
// the creator can be matched to a following protocol error by anchor/size.
inline void dock_boot_surface(const char* what, const char* ns, std::uint32_t anchor, std::uint32_t w,
                              std::uint32_t h, int exclusiveZone, int marginTop, int marginRight,
                              int marginBottom, int marginLeft, const char* outName) {
  std::fprintf(stderr,
               "[horizon-dock:boot] surface %s ns=\"%s\" anchor=0x%x size=%ux%u excl=%d margin=%d,%d,%d,%d"
               " output=%s\n",
               what ? what : "?", ns ? ns : "", static_cast<unsigned>(anchor), static_cast<unsigned>(w),
               static_cast<unsigned>(h), exclusiveZone, marginTop, marginRight, marginBottom, marginLeft,
               outName ? outName : "<null>");
  std::fflush(stderr);
}

}  // namespace eh::shell::dock