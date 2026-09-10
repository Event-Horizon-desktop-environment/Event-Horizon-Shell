#pragma once

#include <wayland-client.h>

// The desktop shell's Wayland seat listener. Owns the pointer and keyboard
// listeners and dispatches input to the dock, popups, control center,
// spotlight, app drawer, lockscreen, polkit agent and settings embeds.
extern const wl_seat_listener g_shell_seat_listener;
