#pragma once

#include <sys/types.h>

namespace eh::shell::panel {

void panel_write_pid_file(pid_t p);
void panel_unlink_pid_file();
pid_t panel_spawn_native_child();
void panel_kill_native_child(int pid);

}  // namespace eh::shell::panel
