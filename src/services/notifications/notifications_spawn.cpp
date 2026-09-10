#define _GNU_SOURCE 1
#include "services/notifications/notifications_spawn.hpp"

#include "services/process/parent_death_guard.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;

namespace eh::shell::notifications {

namespace {

std::string state_base() {
  if (const char* d = std::getenv("XDG_STATE_HOME")) return std::string(d);
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

}  // namespace

pid_t notifications_spawn_native_child() {
  // Resolve the child binary: prefer a `horizon-notifications` sitting next to
  // our own executable (works from a build dir / uninstalled tree), else PATH.
  std::string childBin = "horizon-notifications";
  {
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
      self[n] = '\0';
      const std::string exe = self;
      const auto slash = exe.find_last_of('/');
      const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
      if (::access((dir + "/horizon-notifications").c_str(), X_OK) == 0)
        childBin = dir + "/horizon-notifications";
    }
  }

  const std::string d = state_base() + "/event-horizon";
  (void)mkdir(state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  const std::string logPath = d + "/horizon-notifications.log";
  const int logFd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (logFd >= 0) {
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, logFd);
  }

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

  char* argv[] = { const_cast<char*>(childBin.c_str()), nullptr };
  eh::proc::mark_child_parent_pid();
  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, childBin.c_str(), &actions, &attr, argv, environ);
  eh::proc::clear_child_parent_pid();

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0 || pid < 0) {
    std::cerr << "[notifications] spawn horizon-notifications failed: " << strerror(rc) << "\n";
    return -1;
  }
  std::cout << "[notifications] spawned horizon-notifications pid=" << static_cast<int>(pid)
            << " (log: " << logPath << ")\n";
  return pid;
}

void notifications_kill_native_child(int pid) {
  if (pid <= 1) return;
  if (kill(pid, SIGTERM) == 0) {
    for (int i = 0; i < 50; ++i) {
      if (kill(pid, 0) != 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (kill(pid, 0) == 0) (void)kill(pid, SIGKILL);
  }
}

}  // namespace eh::shell::notifications
