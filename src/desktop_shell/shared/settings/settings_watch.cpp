#include "../../dock/core/dock_app.h"

#include "desktop_shell/shared/core/config_watch.hpp"

#include <sys/inotify.h>
#include <unistd.h>

int dock_open_settings_inotify() {
   
  return eh::shell::shared::open_state_inotify();
}

void dock_drain_settings_inotify(DockApp& app, int inotifyFd) {
   
  eh::shell::shared::drain_inotify(inotifyFd, [&]() {
    dock_maybe_reload_settings(app, "inotify");
  });
}
