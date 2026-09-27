#pragma once

#include <sys/types.h>

namespace eh::shell::stage {

void stage_write_pid_file(pid_t p);
void stage_unlink_pid_file();
pid_t stage_spawn_native_child();
void stage_kill_native_child(int pid);

}  // namespace eh::shell::stage
