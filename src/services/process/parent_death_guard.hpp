#pragma once

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sys/prctl.h>
#include <csignal>
#include <unistd.h>

namespace eh::proc {

// Supervisor-side spawn helpers set EH_SUPERVISOR_PID=<their own pid> on the
// child's environment; child-side `install_parent_death_guard()` turns that into
// a kernel-level "die with my parent" contract that holds even when the
// supervisor is SIGKILLed (PDEATHSIG fires on parent exit, unrelated to sessions
// or process groups).
inline std::mutex& spawn_env_mutex() {
  static std::mutex m;
  return m;
}

inline void mark_child_parent_pid() {
  spawn_env_mutex().lock();
  char buf[32];
  const int n = std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(::getpid()));
  if (n > 0) ::setenv("EH_SUPERVISOR_PID", buf, 1);
}

inline void clear_child_parent_pid() {
  ::unsetenv("EH_SUPERVISOR_PID");
  spawn_env_mutex().unlock();
}

// Call at the very start of a child's entry point. If the supervisor spawned us,
// ask the kernel to SIGKILL us when it dies (fires on SIGKILL too). SIGKILL is
// used rather than SIGTERM: a graceful SIGTERM path can stall in a child's
// cleanup (e.g. the dock waiting on Vulkan/wayland teardown after its supervisor
// vanishes), and there is no follow-up kill for a PDEATHSIG delivery, so a
// child that swallows SIGTERM would keep running orphaned. SIGKILL cannot be
// caught or blocked, so `pkill -9 EventHorizon` reliably reaps every child.
// The getppid() check closes the spawn race: if the supervisor died while we
// were being exec'd, we are already reparented — exit immediately instead of
// running orphaned. Standalone launches (no marker, e.g. the settings .desktop
// file) are left untouched.
inline void install_parent_death_guard() {
  const char* p = std::getenv("EH_SUPERVISOR_PID");
  if (!p || *p == '\0') return;
  char* end = nullptr;
  const long v = std::strtol(p, &end, 10);
  if (end == p || *end != '\0' || v <= 1 || v > 4194304) return;
  const pid_t expected = static_cast<pid_t>(v);
  ::unsetenv("EH_SUPERVISOR_PID");
  if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) ::_exit(1);
  if (::getppid() != expected) ::_exit(1);
}

}  // namespace eh::proc
