# Compositors

Event Horizon supports multiple Wayland compositors through an abstraction layer in `src/backends/`. 32 source files.

---

## Detection

Compositor detection happens at startup in `src/desktop_shell/unified/compositor_kind.cpp`. Uses environment variables:

| Compositor | Detection |
|---|---|
| Hyprland | `HYPRLAND_INSTANCE_SIGNATURE` |
| Niri | `NIRI_SOCKET` or socket path |
| Sway | `SWAYSOCK` |
| Mango | `MANGO_SOCKET` |
| Labwc | `LABWC_PID` |
| Triad | `TRIAD_SOCKET` |

Fallback: `XDG_CURRENT_DESKTOP`, `XDG_SESSION_DESKTOP`, `DESKTOP_SESSION` env vars.

## Compositor Kind

Defined in `src/desktop_shell/unified/compositor_kind.hpp`:

```cpp
enum class CompositorKind : uint8_t {
  Unknown, Niri, Hyprland, Sway, Mango, Labwc, Triad
};
```

Free functions: `detect_compositor_kind()`, `compositor_kind_cstr()`, `compositor_env_hint()`, `is_niri()`, `is_hyprland()`, `is_sway()`, `is_mango()`, `is_labwc()`, `is_triad()`.

---

## File-by-File Detail

### `power_actions.cpp` / `power_actions.h`

**Purpose:** Platform-level power operations. Uses `logind` D-Bus for session management.

**Functions:**
- `platform_lock()` - locks session via `loginctl lock-session`
- `platform_logout()` - logs out via `loginctl terminate-user`
- `platform_reboot()` - reboots via `systemctl reboot`
- `platform_shutdown()` - shuts down via `systemctl poweroff`

### `compositor_ipc.cpp` / `compositor_ipc.h`

**Purpose:** Abstract `CompositorRuntime` base class.

**Virtual methods:**
- `init(wl_display*)` - initializes compositor-specific IPC connection
- `poll_fd()` - returns IPC file descriptor for `poll_mux`
- `dispatch()` - dispatches pending IPC events
- `shutdown()` - closes IPC connection
- `on_event(callback)` - registers event handler

### `keyboard_backend.h`

**Purpose:** Abstract `KeyboardBackend` base class.

**Virtual methods:**
- `apply_keymap(layout, variant, options)` - applies keyboard layout
- `query_keyboard_info()` - returns current layout info (name, variant, shortNames)

### `workspace_backend.h`

**Purpose:** Abstract `WorkspaceBackend` base class.

**Virtual methods:**
- `workspaces()` - returns list of workspace entries
- `activate_workspace(id)` - switches to workspace
- `on_change(callback)` - registers workspace change listener

### `ext_workspace_backend.cpp` / `ext_workspace_backend.h`

**Purpose:** Generic workspace backend using `ext-workspace-v1` Wayland protocol. Works with any compositor that supports this protocol (Niri, KWin, some wlroots).

### Hyprland Backend (`hyprland/`)

| File | Description |
|---|---|
| `hyprland_runtime.cpp` / `hyprland_runtime.h` | Connects to `$HYPRLAND_INSTANCE_SIGNATURE` Unix socket (`/tmp/hypr/$INSTANCE/.socket.sock`). Manages JSON-based IPC event loop. `HyprlandRuntime` class implementing `CompositorRuntime`. |
| `hyprland_event_handler.cpp` / `hyprland_event_handler.h` | Event dispatch for Hyprland IPC. Parses event types: `workspace`, `focusedmon`, `activewindow`, `openwindow`, `closewindow`, `movewindow`, `urgent`, `fullscreen`, `monitor`, `createworkspace`, `destroyworkspace`, `moveworkspace`, `activelayout`, `openlayer`, `closelayer`. Maps to shell callbacks. |
| `hyprland_keyboard_backend.cpp` / `hyprland_keyboard_backend.h` | Queries keyboard layout via `hyprctl devices -j`. Parses JSON output to extract active keymap. |
| `hyprland_output_backend.cpp` / `hyprland_output_backend.h` | Queries monitor config via `hyprctl monitors -j`. Provides output geometry, scale, transform, make/model, description. |
| `hyprland_workspace_backend.cpp` / `hyprland_workspace_backend.h` | Tracks workspace state via Hyprland IPC events. Maintains workspace list with active/occupied/urgent states, window lists per workspace. |

### Niri Backend (`niri/`)

| File | Description |
|---|---|
| `niri_runtime.cpp` / `niri_runtime.h` | Connects to Niri IPC socket (`$NIRI_SOCKET`). Manages JSON-based request/response protocol. `NiriRuntime` class. |
| `niri_keyboard_backend.cpp` / `niri_keyboard_backend.h` | Queries keyboard layout from Niri. Resolves group and variant from compositor state. |
| `niri_output_backend.cpp` / `niri_output_backend.h` | Output geometry and scale from Niri. Handles fractional scaling. |
| `niri_workspace_backend.cpp` / `niri_workspace_backend.h` | Workspace tracking via `ext-workspace-v1` protocol (Niri supports this natively). |

### Sway Backend (`sway/`)

| File | Description |
|---|---|
| `sway_runtime.cpp` / `sway_runtime.h` | Connects to Sway IPC socket (`$SWAYSOCK`). Uses `swaymsg` JSON protocol. `SwayRuntime` class. |
| `sway_keyboard_backend.cpp` / `sway_keyboard_backend.h` | Keyboard layout via `swaymsg -t get_inputs`. |
| `sway_output_backend.cpp` / `sway_output_backend.h` | Output configuration via `swaymsg -t get_outputs`. |
| `sway_workspace_backend.cpp` / `sway_workspace_backend.h` | Workspace tracking via `swaymsg -t get_workspaces` + subscribe events. |

### Triad Backend (`triad/`)

| File | Description |
|---|---|
| `triad_runtime.cpp` / `triad_runtime.h` | Custom `dwl-ipc-unstable-v2` protocol connection for Triad compositor. `TriadRuntime` class. |
| `triad_keyboard_backend.cpp` / `triad_keyboard_backend.h` | Uses Wayland virtual keyboard protocol directly. |
| `triad_output_backend.cpp` / `triad_output_backend.h` | Output geometry from `dwl-ipc-unstable-v2` events. |
| `triad_workspace_backend.cpp` / `triad_workspace_backend.h` | Workspace tracking via `dwl-ipc-unstable-v2` tags/workspace protocol. |

### Mango Backend (`mango/`)

| File | Description |
|---|---|
| `mango_keyboard_backend.h` | Stub/skeleton for Mango keyboard backend. |
| `mango_output_backend.h` | Stub/skeleton for Mango output backend. |
| `mango_workspace_backend.cpp` / `mango_workspace_backend.h` | Uses Mango's `mmsg` IPC tool (`mmsg -g -t`) for workspace info. Parses tag-based workspace model. |

---

## Power Actions

Power actions (`lock`, `logout`, `restart`, `shutdown`) in `app_drawer_power_exec.cpp` are compositor-aware:
- Lock: `loginctl lock-session`, or compositor-specific (`hyprlock`, `swaylock`)
- Logout: compositor-specific IPC to exit the session
- Reboot: `systemctl reboot`
- Shutdown: `systemctl poweroff`
