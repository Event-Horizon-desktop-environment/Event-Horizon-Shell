#pragma once

#include "desktop_shell/common/log/debug_log.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace eh::shell_log {

namespace detail {

// Diagnostics are variadic stream-style, while debug_log() is printf-style:
// fold the arguments into a string first so both APIs stay compatible.
template <typename... Ts>
std::string diag_join(Ts&&... ts) {
  std::ostringstream os;
  (os << ... << std::forward<Ts>(ts));
  return os.str();
}

template <typename... Ts>
void diag_log(const char* tag, Ts&&... ts) {
  const std::string s = diag_join(std::forward<Ts>(ts)...);
  debug_log(tag, "%s", s.c_str());
}

}  // namespace detail

// MPRIS is a user-facing action path: every early return used to be silent,
// which made "the play button does nothing" undiagnosable. Keep these live so
// ~/EH-logs/mpris.log shows the chosen player, the target bus name and any
// D-Bus error the action runs into.
template <typename... Ts>
void mpris_dbus(Ts&&... ts) {
  detail::diag_log("mpris", std::forward<Ts>(ts)...);
}

template <typename... Ts>
void dock_mpris(Ts&&... ts) {
  detail::diag_log("dock_mpris", std::forward<Ts>(ts)...);
}

template<typename... Ts>
void dock(Ts&&...) {}
template<typename... Ts>
void panel_mpris(Ts&&...) {}
template<typename... Ts>
void dbus_tray(Ts&&...) {}
template<typename... Ts>
void notifications(Ts&&...) {}
template<typename... Ts>
void notif_verbose(Ts&&...) {}

}
