# Architecture

## System Architecture

Event Horizon DE is a Wayland-native desktop shell implemented as a single-threaded, event-driven C++23 application. It connects to an existing Wayland compositor as a client, using the `wlr-layer-shell-unstable-v1` protocol to create panels, overlays, and background surfaces.

### Process Model

```
┌─────────────────────────────────────────────────────────┐
│                  EventHorizon (single process)           │
│                                                         │
│  ┌────────────┐  ┌──────────┐  ┌────────────────────┐  │
│  │  Dock App   │  │  Taskbar │  │  Desktop + Widgets  │  │
│  │  (layers)   │  │ (layers) │  │   (layers)          │  │
│  └──────┬──────┘  └────┬─────┘  └─────────┬──────────┘  │
│         │              │                  │              │
│  ┌──────┴──────────────┴──────────────────┴──────────┐  │
│  │              Shell Unified Core                    │  │
│  │  (input routing, popup management, config, IPC)    │  │
│  └──────────────────────┬─────────────────────────────┘  │
│                         │                                │
│  ┌──────────────────────┴─────────────────────────────┐  │
│  │              Wayland Layer + Protocols             │  │
│  │  (wl_display, wl_seat, layer-shell, ext-workspace, │  │
│  │   foreign-toplevel, session-lock, gamma, etc.)     │  │
│  └──────────────────────┬─────────────────────────────┘  │
│                         │                                │
└─────────────────────────┼────────────────────────────────┘
                          │ Wayland Unix socket
              ┌───────────┴───────────┐
              │   Wayland Compositor   │
              │  (Hyprland/Niri/Sway)  │
              └───────────────────────┘
```

### Event Loop

The application uses a single-threaded `poll()`-based event loop:

```
poll_mux.poll()
├── wl_display fd → dispatch Wayland events
│   ├── seat events (pointer, keyboard, touch)
│   ├── output events (geometry, scale)
│   ├── layer surface events (configure, close)
│   └── protocol events (workspace, toplevel, etc.)
├── config watcher fd → reload config, rebuild UI
├── IPC socket fd → handle external commands
├── signal fd → handle SIGINT/SIGTERM
└── timer fds → polling (battery, workspace, brightness)
    │
    └── main_loop.tick() → frame callback → render()
```

### Rendering

Each frame:
1. Poll for input and protocol events
2. Update state (animations, polling results)
3. Render surfaces (Cairo or Vulkan)
4. Commit surfaces to Wayland compositor
5. Request next frame callback

### Memory Model

All state is owned by the main process. `DockApp` and `TaskbarApp` are the top-level state structs, containing pointers to all Wayland objects, settings, and child state. The state is never shared across threads (except for read-only access to cached values under mutex).

### Thread Safety

Minimal threading:
- **Battery polling** - background thread reads UPower D-Bus, writes to mutex-guarded struct
- **PipeWire audio** - background thread monitors audio nodes, posts deferred calls to main
- **Thumbnail generation** - background thread decodes images
- **Config loading** - `inotify` triggers reload on main thread (non-blocking)

All Wayland operations must happen on the main thread. `deferred_call::post()` enables cross-thread dispatch for non-Wayland work that needs to trigger Wayland updates.

---

## Module Relationships

### Dock ↔ App Drawer

```
dock_input.cpp
  → eh_app_drawer_hit_zone() [app_drawer.cpp]
  → eh_app_drawer_paint()   [app_drawer.cpp]
  → eh_app_drawer_pick_power_index() [app_drawer.cpp]

dock_bar.cpp
  → dock_popup_paint_app_menu() [popup_paint_app_menu.cpp]
    → eh_app_drawer_paint()     [app_drawer.cpp]
```

### Taskbar ↔ App Drawer

```
taskbar.cpp
  → app_drawer_hit_zone()   [app_drawer_overlay.cpp]
  → app_drawer_paint()      [app_drawer_overlay.cpp]
  → app_drawer_pick_power_index() [app_drawer_overlay.cpp]
```

### Shared Code

```
app_drawer_power_exec.cpp ← dock_input.cpp, taskbar.cpp
app_drawer_power_modal.cpp ← app_drawer.cpp, app_drawer_overlay.cpp
desktop_list.cpp ← app_drawer.cpp, app_drawer_overlay.cpp, launchpad
widget_bar_paint.cpp ← dock_paint_widget_bar.cpp, taskbar_paint.cpp
```

### Service Dependencies

```
DockApp → PipeWireService → osd_audio.cpp
DockApp → GammaService → app_drawer (nightlight)
DockApp → OsdHost → osd_audio.cpp, osd_brightness.cpp
DockApp → NotificationManager → notification_toast_host.cpp
DockApp → NotificationManager → notification_dbus_service.cpp
DockApp → ForeignToplevelList → dock_foreign_toplevel.cpp
DockApp → TrayManager → tray.cpp, tray_context_menu.cpp
DockApp → MprisPlayer → media_paint.cpp
DockApp → IconCache → app_drawer, dock slots
```

---

## Config Flow

```
config.toml (disk)
    │ inotify watch
    ▼
shell_config_load_file() → ShellConfig snapshot
    │
    ├── derived_chrome_colors() → ChromePaintColors
    ├── dock_sync_settings() → DockApp::settings
    ├── taskbar_sync_settings() → TaskbarApp::settings
    └── appearance → all components
```

---

## Key Interfaces

### Wayland Protocols Used

| Protocol | Purpose |
|---|---|
| `wlr-layer-shell-unstable-v1` | Panel, overlay, background surfaces |
| `xdg-shell` | Standalone settings window |
| `ext-session-lock-v1` | Secure lock screen |
| `wlr-foreign-toplevel-management-v1` | Window tracking |
| `wlr-gamma-control-v1` | Nightlight |
| `ext-workspace-v1` | Workspace management |
| `wlr-screencopy-v1` | Screenshots |
| `zwp_linux_dmabuf_v1` | Vulkan buffer sharing |
| `wp_viewporter` | Surface scaling |
| `wp_fractional_scale_v1` | HiDPI fractional scaling |
| `ext-idle-notify-v1` | Idle detection |
| `zwlr_data_control_v1` | Clipboard |
| `cursor-shape-v1` | Cursor themes |
| `ext-background-effect-v1` | Blur effects |

### D-Bus Services Used

| Service | Purpose |
|---|---|
| `org.freedesktop.Notifications` | Receive/send notifications |
| `org.kde.StatusNotifierWatcher` | System tray |
| `org.mpris.MediaPlayer2.*` | Media player control |
| `org.freedesktop.NetworkManager` | Network management |
| `org.bluez` | Bluetooth |
| `org.freedesktop.UPower` | Battery status |
| `org.freedesktop.UDisks2` | Drive management |
| `org.freedesktop.SecretService` | Password storage |
| `org.freedesktop.PolicyKit1` | Authentication |
| `org.freedesktop.systemd1` | Session management |
