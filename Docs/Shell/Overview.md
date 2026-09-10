# Shell Overview

The shell is the core desktop UI, located in `src/desktop_shell/`. It provides panels, desktop, widgets, popups, lockscreen, and system services.

---

## Module Inventory

| Module | Path | Files | Purpose |
|---|---|---|---|---|
| Dock | `src/desktop_shell/dock/` | 23 | Application dock panel |
| Taskbar | `src/desktop_shell/taskbar/` | 8 | Taskbar panel |
| Desktop | `src/desktop_shell/desktop/` | 40 | Background, icons, widgets |
| Widgets | `src/desktop_shell/widgets/` | 41 | Bar widgets + popups |
| App Drawer | `src/desktop_shell/widgets/app_drawer/` | 11 | Start menu (2 implementations) |
| Control Center | `src/desktop_shell/controlcenter/` | 14 | Quick settings panel |
| Notifications | `src/desktop_shell/notifications/` | 6 | Toast + history |
| Spotlight | `src/desktop_shell/spotlight/` | 5 | App search |
| Launchpad | `src/desktop_shell/launchpad/` | 6 | Full-screen launcher |
| Lockscreen | `src/desktop_shell/lockscreen/` | 4 | Session lock |
| OSD | `src/desktop_shell/osd/` | 6 | Volume/brightness overlay |
| Power Confirm | `src/desktop_shell/power_confirm/` | 2 | Power action modal |
| Services | `src/services/` | 14 | Audio, IPC, Polkit, Bing |
| Common | `src/desktop_shell/common/` | 53 | Shared utilities |
| Shared | `src/desktop_shell/shared/` | 28 | Shared popup/layout/code |
| Unified | `src/desktop_shell/unified/` | 5 | Compositor detection, output mgmt |
| UI | `src/ux/` | 8 | Button, slider, toggle, dropdown |
| Keyboard | `src/desktop_shell/keyboard/` | 2 | Keyboard settings apply |
| Shell entry | `src/bootstrap/` | 2 | `unified_shell.cpp`, `unified_shell_session.cpp` |

---

## Entry Point

`UnifiedShellSession` in `src/bootstrap/detail/unified_shell_session.cpp` performs the entire synchronous bootstrap:

```
UnifiedShell::run()
  └─ UnifiedShellSession::run(mode)
       ├─ wl_display_connect()
       ├─ dock_init_on_display()
       ├─ wallpaper_renderer_init()
       ├─ taskbar_init_on_display()
       ├─ desktop_init_on_display()
       ├─ keyboard::apply_settings()
       ├─ launchpad_host init
       ├─ Services: Gamma, Idle, LockScreen, IPC, NetworkManager, Notifications
       └─ PollMuxLoop::run()  ← main event loop
```

The parent `UnifiedShell` class in `src/bootstrap/unified_shell.cpp` manages shell lifecycle:
- Constructs all components
- Handles `SIGINT`/`SIGTERM` for graceful shutdown
- Manages embedded settings mode
- Handles `--eh-screenshot` and `--eh-settings` CLI flags

---

## Surface Types

The shell creates these Wayland layer surfaces:

| Surface | Layer | Z-order | Purpose |
|---|---|---|---|
| Wallpaper | Background | Lowest | Wallpaper image |
| Desktop icons | Background | +1 | File/folder icons |
| Desktop widgets | Background | +2 | Clock, weather, etc. |
| Dock | Bottom | +100 | Application dock |
| Taskbar | Bottom | +101 | Taskbar panel |
| Notifications | Overlay | +200 | Toast popups |
| OSD | Overlay | +201 | Volume/brightness |
| App drawer | Overlay | +202 | Start menu |
| Control center | Overlay | +203 | Quick settings |
| Launchpad | Overlay | +204 | Full-screen launcher |
| Lockscreen | Overlay (session-lock) | Highest | Lock screen |

---

## Input Routing

Input events flow:
1. Wayland seat receives event
2. `dock_input.cpp` or `taskbar.cpp` pointer handler processes it
3. If a popup is open: route to popup-specific handler
4. If on a slot: activate slot (launch app, open popup, etc.)
5. If on background: ignore (fall through to compositor)

Keyboard input:
- Routed to focused surface (search field in app drawer, password field in lockscreen, etc.)
- Tab/Shift+Tab navigates between focusable fields
- Escape closes current popup
- Enter activates selected item

---

## Rendering Pipeline

Two rendering backends:
- **CPU path**: Cairo renders to `cairo_image_surface`, backed by `wl_shm` pool, committed via `wl_surface`
- **GPU path**: Cairo renders to CPU buffer, uploads to Vulkan image via `vkCmdBlitImage`, displayed via `zwp_linux_dmabuf_v1`

Both paths produce the same visual output. The GPU path reduces CPU copy overhead for large surfaces.

---

## Widget Bar

Both dock and taskbar use `widget_bar_paint.cpp` to render their bar content. The widget bar:
1. Divides the bar into left/center/right sections
2. Each section contains widget tokens from config
3. Widget tokens expand to their visual representation:
   - `pinned_apps` → dock/taskbar pinned app icons
   - `running_apps` → running window icons
   - `tray` → system tray icons
   - `clock` → time display
   - `weather` → weather icon + temp
   - `media` → now playing
   - `workspaces` → workspace dots
   - `battery` → battery indicator
   - `spacer` → empty space
   - `launcher` / `app_drawer` → start menu button
   - `spotlight` → search button
   - `settings` → settings button
   - `trash` → trash icon
   - `control_center` → quick settings button
   - `smenu` → start menu (centered button)

---

## Popup System

Popups are separate layer-shell overlay surfaces. Each popup:
1. Is positioned relative to its parent slot (above/below the bar)
2. Has its own input handling (routed from parent)
3. Is dismissed on click-away or Escape
4. Uses rounded rectangle chrome with consistent colors

Popup lifecycle:
```
slot click → dock_popup_open(kind)
  → create layer surface
  → position relative to bar
  → set up input handlers
  → render content
  → wait for interaction or dismiss
```

---

## Thread Safety

The shell is primarily single-threaded (main event loop). Thread-safe operations:
- Battery polling: dedicated thread reads UPower, writes to mutex-guarded cache
- Thumbnail generation: background thread for wallpaper/icon thumbnails
- PipeWire: audio service runs in its own thread, communicates via deferred calls
- Config watch: `inotify` fd is polled in the main loop, callbacks fire on main thread

`deferred_call::post()` provides thread-safe cross-thread dispatch to the main thread.

---

## Config System

See [Configuration](../Configuration.md).

The `ShellConfig` snapshot is the central configuration repository. All shell components read from it. Key sections:
- `[appearance]` - visual theming (colors, fonts, opacities, matugen)
- `[dock]` - dock settings
- `[taskbar]` - taskbar settings
- `[layout]` - UI layout configuration
- `[desktop]` - desktop settings
- `[wallpaper]` - wallpaper settings
- `[matugen]` - Material You generation settings
- `[keyboard]` - keyboard settings
