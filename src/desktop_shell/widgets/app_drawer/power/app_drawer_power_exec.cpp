#define _GNU_SOURCE 1
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"

#include "backends/hyprland/hyprland_backends.h"

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/Error.h>

#include <cstdlib>
#include <fcntl.h>
#include <spawn.h>
#include <string_view>
#include <unistd.h>
#include <vector>

extern "C" char** environ;

namespace eh::shell::dock::app_drawer {
namespace {

void run_detached_sh(const char* script) {
   
  if (!script || !script[0]) return;

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) return;
  if (posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDWR, 0) != 0 ||
      posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_RDWR, 0) != 0 ||
      posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_RDWR, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return;
  }
  posix_spawnattr_t attr{};
  if (posix_spawnattr_init(&attr) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return;
  }
  (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

  const std::string_view sv(script);
  std::vector<char> arg_lc(sv.begin(), sv.end());
  arg_lc.push_back('\0');
  char argv0[] = "/bin/sh";
  char argv1[] = "sh";
  char argv2[] = "-c";
  char* argv[] = {argv0, argv1, argv2, arg_lc.data(), nullptr};

  pid_t pid = -1;
  const int err = posix_spawnp(&pid, "/bin/sh", &fa, &attr, argv, environ);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&fa);
  if (err != 0 || pid < 0) return;
}

void call_logind_method(const char* method) {
   
  try {
    auto bus = sdbus::createSystemBusConnection();
    auto proxy = sdbus::createProxy(*bus,
        sdbus::ServiceName{"org.freedesktop.login1"},
        sdbus::ObjectPath{"/org/freedesktop/login1"});
    proxy->callMethod(method)
        .onInterface("org.freedesktop.login1.Manager")
        .withArguments(false);
  } catch (const sdbus::Error&) {
    run_detached_sh("systemctl poweroff");
  }
}

}

void app_drawer_power_exec(CompositorKind compositor, int powerButtonIndex) {
   
  switch (powerButtonIndex) {
    case 0:
      run_detached_sh(
          "(command -v eh-ipc >/dev/null 2>&1 && eh-ipc lock) || "
          "command -v loginctl >/dev/null 2>&1 && loginctl lock-session || "
          "(command -v hyprlock >/dev/null 2>&1 && hyprlock) || "
          "(command -v swaylock >/dev/null 2>&1 && swaylock -f) || true");
      break;
    case 1:
      if (compositor == CompositorKind::Hyprland || std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
        if (wspace::hyprland::hyprland_config_is_lua()) {
          (void)hyprland_ipc_send("dispatch hl.dsp.exit()");
        } else {
          (void)hyprland_ipc_send("dispatch exit");
        }
      } else if (compositor == CompositorKind::Niri) {
        run_detached_sh(
            "(command -v niri >/dev/null 2>&1 && niri msg action quit) || "
            "loginctl terminate-session \"${XDG_SESSION_ID}\"");
      } else if (compositor == CompositorKind::Mango) {
        run_detached_sh("mmsg -q");
      } else {
        run_detached_sh("loginctl terminate-session \"${XDG_SESSION_ID}\"");
      }
      break;
    case 2:
      call_logind_method("Reboot");
      break;
    case 3:
      call_logind_method("PowerOff");
      break;
    default:
      break;
  }
}

}
