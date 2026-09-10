#pragma once

#include <string>
#include <sys/types.h>

struct WallpaperRenderer;

namespace eh::wallpaper {

[[nodiscard]] const char* swaybg_mode_keyword(int modeIndex) noexcept;
void apply_saved(bool enabled, const std::string& imagePath, int modeIndex);
[[nodiscard]] bool pick_folder(std::string* outPath, int picker_mode = 0);
void wallpaper_set_native_renderer(WallpaperRenderer* r);
void wallpaper_redraw_native();

// PID-file helpers shared with the standalone child (self-registration).
// Both sides write the same `event-horizon/wallpaper.pid` used by the external
// fallback paths.
void write_pid_file(pid_t p);
void unlink_pid_file();

// True when the split-out `horizon-wallpaper` native child is alive (its own
// pid in the wallpaper pidfile resolves to a horizon-wallpaper exe). External
// apply callers use this to skip the external wallpaper command fallback and
// let the child re-apply via the `config.applied` broadcast instead.
[[nodiscard]] bool wallpaper_native_child_active();

// Supervisor side: spawn/kill the `horizon-wallpaper` child.
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t wallpaper_spawn_native_child();
void wallpaper_kill_native_child(int pid);

}
