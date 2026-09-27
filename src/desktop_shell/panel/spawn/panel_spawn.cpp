// Panel child spawn — brand-new, mirrors the dock/taskbar spawn pattern
// (pidfile guard against PID reuse, next-to-exe resolution, log redirect,
// setsid) but with horizon-panel names. No code is copied; the structure
// follows the same process contract so the supervisor can manage it.

#define _GNU_SOURCE 1
#include "desktop_shell/panel/spawn/panel_spawn.hpp"

#include "services/process/parent_death_guard.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace eh::shell::panel {

namespace {

std::string panel_state_base() {
  if (const char* d = std::getenv("XDG_STATE_HOME")) return std::string(d);
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

std::string panel_pid_file_path() {
  const std::string d = panel_state_base() + "/event-horizon";
  (void)mkdir(panel_state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  return d + "/panel.pid";
}

bool panel_read_pid_file(pid_t* outPid) {
  *outPid = -1;
  FILE* f = fopen(panel_pid_file_path().c_str(), "rb");
  if (!f) return false;
  char buf[64]{};
  if (!fgets(buf, sizeof(buf), f)) {
    fclose(f);
    return false;
  }
  fclose(f);
  *outPid = static_cast<pid_t>(std::strtol(buf, nullptr, 10));
  return *outPid > 1;
}

bool panel_pid_is_ours(pid_t pid) {
  if (pid <= 1) return false;
  char proc[64];
  std::snprintf(proc, sizeof(proc), "/proc/%d/stat", static_cast<int>(pid));
  if (FILE* f = fopen(proc, "rb")) {
    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
      const char* openP = std::strchr(buf, '(');
      const char* closeP = std::strrchr(buf, ')');
      if (openP && closeP && closeP > openP)
        if (std::string_view(openP + 1, static_cast<std::size_t>(closeP - openP - 1)) == "horizon-panel") {
          fclose(f);
          return true;
        }
    }
    fclose(f);
  }
  std::snprintf(proc, sizeof(proc), "/proc/%d/exe", static_cast<int>(pid));
  char exe[4096];
  const ssize_t n = ::readlink(proc, exe, sizeof(exe) - 1);
  if (n <= 0) return false;
  exe[n] = '\0';
  return std::string_view(exe, static_cast<std::size_t>(n)).find("horizon-panel") != std::string_view::npos;
}

bool panel_native_child_active() {
  pid_t p = -1;
  if (!panel_read_pid_file(&p) || p <= 1) return false;
  if (!panel_pid_is_ours(p)) {
    panel_unlink_pid_file();
    return false;
  }
  return true;
}

}  // namespace

void panel_write_pid_file(pid_t p) {
  FILE* f = fopen(panel_pid_file_path().c_str(), "wb");
  if (!f) return;
  fprintf(f, "%d\n", static_cast<int>(p));
  fclose(f);
}

void panel_unlink_pid_file() { (void)unlink(panel_pid_file_path().c_str()); }

pid_t panel_spawn_native_child() {
  if (panel_native_child_active()) {
    pid_t stale = -1;
    if (panel_read_pid_file(&stale) && stale > 1) panel_kill_native_child(stale);
  }
  std::string childBin = "horizon-panel";
  {
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
      self[n] = '\0';
      const std::string exe = self;
      const auto slash = exe.find_last_of('/');
      const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
      if (::access((dir + "/horizon-panel").c_str(), X_OK) == 0) childBin = dir + "/horizon-panel";
    }
  }
  const std::string d = panel_state_base() + "/event-horizon";
  (void)mkdir(panel_state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  const std::string logPath = d + "/horizon-panel.log";
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
  char* argv[] = {const_cast<char*>(childBin.c_str()), nullptr};
  eh::proc::mark_child_parent_pid();
  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, childBin.c_str(), &actions, &attr, argv, environ);
  eh::proc::clear_child_parent_pid();
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0 || pid < 0) {
    std::cerr << "[panel] spawn horizon-panel failed: " << strerror(rc) << "\n";
    return -1;
  }
  std::cout << "[panel] spawned horizon-panel pid=" << static_cast<int>(pid) << " (log: " << logPath << ")\n";
  return pid;
}

void panel_kill_native_child(int pid) {
  if (pid <= 1) {
    panel_unlink_pid_file();
    return;
  }
  if (!panel_pid_is_ours(pid)) {
    panel_unlink_pid_file();
    return;
  }
  const bool alive = kill(pid, SIGTERM) == 0 || errno == ESRCH;
  if (alive) {
    for (int i = 0; i < 50; ++i) {
      if (waitpid(pid, nullptr, WNOHANG) == pid) break;
      if (kill(pid, 0) != 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (kill(pid, 0) == 0) (void)kill(pid, SIGKILL);
    (void)waitpid(pid, nullptr, WNOHANG);
  }
  panel_unlink_pid_file();
}

}  // namespace eh::shell::panel
