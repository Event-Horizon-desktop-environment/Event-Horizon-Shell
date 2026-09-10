# Event Horizon Desktop Environment

Event Horizon is a Wayland-native desktop shell written in C++23. It runs as a layer-shell client alongside an existing Wayland compositor (Hyprland, Niri, Sway, Mango, Labwc, or Triad) and provides a complete desktop experience: panels, widgets, notifications, wallpaper, lock screen, and settings.

## Project Map

| Directory | Purpose |
|---|---|
| `src/bootstrap/` | Application entry point, main loop, session management |
| `src/desktop_shell/` | Core shell: panels, desktop, widgets, popups |
| `src/backends/` | Compositor-specific backends (Hyprland, Niri, Sway, etc.) |
| `src/configuration/` | Configuration parsing (TOML) and runtime snapshot |
| `src/services/` | D-Bus service clients (notifications, tray, MPRIS, network, etc.) |
| `src/wl/` | Wayland protocol wrappers (layer shell, Vulkan, clipboard, etc.) |
| `src/wallpaper/` | Wallpaper rendering and thumbnail service |
| `src/ux/` | Settings UI application |
| `src/ipc_client/` | Standalone IPC client binary |
| `src/notifications/` | Notification host (`horizon-notifications`) |
| `src/clipboard_daemon/` | Clipboard helper daemon (`horizonClipboardDaemon`) |
| `src/diagnostics/` | Startup probe (`eh-startup-probe`) |
| `src/dialog/` | Layer-shell dialog widgets (file chooser, power confirm) |
| `src/m3/` | Material 3 UI components |
| `src/color/` | Color pipeline / matugen-based palette engine |
| `src/platform/` | Platform helpers (rfkill, etc.) |
| `assets/` + `Resources/` | Bundled assets, fonts, themes |
| `protocols/` | Custom Wayland protocol XML definitions |
| `subprojects/` | Meson subproject dependencies |
| `third_party/` | Vendored third-party code |

## Documentation

- [Architecture](Architecture.md) - System architecture overview
- [Build](Build.md) - Building from source
- [Configuration](Configuration.md) - Configuration system
- [Compositors](Compositors.md) - Compositor support
- [Wayland](Wayland.md) - Wayland protocol integration
- [DBus Services](DBusServices.md) - D-Bus service integrations
- [Wallpaper](Wallpaper.md) - Wallpaper system
- [Project Map](ProjectMap.md) - Detailed file-by-file directory map

### Distro Guides

- [Arch Linux](Arch-Linux.md)
- [Fedora](Fedora.md)
- [Debian / PikaOS / Ubuntu](Debian-Ubuntu.md)

### Shell Components

- [Shell Overview](Shell/Overview.md) - Modules, rendering, input, popups
- [Dock](Shell/Dock.md) - Application dock / panel
- [Taskbar](Shell/Taskbar.md) - Taskbar panel
- [App Drawer](Shell/AppDrawer.md) - Start menu / application launcher
- [Desktop](Shell/Desktop.md) - Desktop layer, icons, widgets
- [Control Center](Shell/ControlCenter.md) - Control center popup
- [Notifications](Shell/Notifications.md) - Notification system
- [Spotlight](Shell/Spotlight.md) - Spotlight search
- [Launchpad](Shell/Launchpad.md) - Full-screen app launcher
- [Lockscreen](Shell/Lockscreen.md) - Screen lock
- [OSD](Shell/OSD.md) - On-screen display
- [Power Confirm](Shell/PowerConfirm.md) - Power action confirmation
- [Widgets](Shell/Widgets.md) - Widget system (clock, weather, media, etc.)
- [Keyboard](Shell/Keyboard.md) - Keyboard layout configuration
- [Unified Infrastructure](Shell/Unified.md) - Compositor detection, output mgmt, registry

### Background Services

- [Audio](Services/Audio.md) - PipeWire audio service
- [IPC](Services/IPC.md) - Inter-process communication
- [Polkit](Services/Polkit.md) - PolicyKit authentication
- [Bing Wallpaper](Services/Bing.md) - Bing daily wallpaper downloader

### Settings UI

- [Settings Application](UI/Settings.md)
- [UI Components](UI/Components.md)

## Key Conventions

- Widget changes must be applied to both the dock and taskbar code paths
- The start menu lives in `widgets/start_menu/` (the `eh_app_drawer_*` API);
  the taskbar overlay lives in `widgets/app_drawer/` (`AppDrawerState` /
  `app_drawer_*` API in `overlay/`)
- Both share power exec logic in
  `widgets/app_drawer/power/app_drawer_power_exec.{hpp,cpp}` and power modal in
  `widgets/app_drawer/power/app_drawer_power_modal.{hpp,cpp}`
- The project uses C++23, Cairo for 2D rendering, and Vulkan for GPU-accelerated rendering
