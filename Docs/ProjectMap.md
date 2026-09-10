# Project Map

A file-by-file walkthrough of the Event Horizon codebase. Source files are organized by function into top-level directories under `src/`.

---

## Project Root

The build and config files at the root set up the project:

- **`meson.build`** (1255 lines) -- Top-level Meson definition. Generates Wayland protocol code from XML at build time. Links against sdbus-c++, cairo, libxkbcommon, Pango, PipeWire, Vulkan, ffmpegthumbnailer, libjpeg, and jemalloc. Builds `libeh_shell_core.so` (the shared core library all components link against) plus the binaries below.
- **`meson_options.txt`** -- Controls optional features: PipeWire, Vulkan, systemd, X11, man pages, tests, and compiler flags.
- **`justfile`** -- Convenience recipes: build, run, install, screenshot, matugen, lint, format.
- **`PKGBUILD`** -- Arch Linux packaging.
- **`README.md`**, **`shell.md`**, **`CHANGELOG.md`** -- Project overview, shell rewrite/audit plan, and release notes.
- **`Docs/`** -- Expanded documentation: `Architecture.md`, `Build.md`, `Compositors.md`, `Configuration.md`, `DBusServices.md`, `Home.md`, `ProjectMap.md`, plus distro guides (Arch-Linux, Debian-Ubuntu, Fedora), `dock.md`, `Services/`, `Shell/`, `UI/`, `Wallpaper.md`, `Wayland.md`.
- **`.clang-format`** -- Google-inspired style with a 140-column limit and 2-space indent.
- **`.clangd`**, **`.gitignore`**, **`.circleci/config.yml`**, **`.github/`** -- LSP config, file exclusions, and CI.

### Build targets

`meson.build` produces the executable targets:

| Target | Entry source | Purpose |
|---|---|---|
| `EventHorizon` | `src/bootstrap/entry/entry_point.cpp` | The shell binary (real `main()`) |
| `eh-ipc` | `src/ipc_client/main.cpp` | CLI client for the shell IPC socket |
| `eh-startup-probe` | `src/diagnostics/startup_probe.cpp` | Startup timing probe tool |
| `horizon-live-wallpaper` | `src/ux/Live_Wallpaper/standalone_main.cpp` | Standalone live wallpaper renderer |
| `horizon-disks` | `src/ux/disks/main.cpp` | Disk utility app |
| `horizonClipboardDaemon` | `src/clipboard_daemon/main.cpp` | Clipboard helper daemon |
| `horizon-settings` | `src/ux/settings/common/settings_standalone_main.cpp` | Settings app |
| `horizon-wallpaper` | `src/wallpaper/standalone_main.cpp` | Standalone wallpaper setter |
| `horizon-notifications` | `src/notifications/standalone_main.cpp` | Notification service host |
| `horizon-desktop` | `src/desktop_shell/desktop/standalone_main.cpp` | Standalone desktop shell |
| `horizon-dock` | `src/desktop_shell/dock/standalone_main.cpp` | Standalone dock panel |
| `horizon-taskbar` | `src/desktop_shell/taskbar/standalone_main.cpp` | Standalone taskbar panel |

Plus test executables: `test_horizon_colors`, `test_contract_ipc`, `test_vram_boost`, `test_vram_boost_live`, `test_icon_cache`, `test_damage_region`.

---

## `src/` -- Source Root

No source files live directly in `src/` anymore -- they've all been moved into purpose-built subdirectories:

`assets`, `backends`, `bootstrap`, `clipboard_daemon`, `color`, `configuration`, `desktop_shell`, `diagnostics`, `dialog`, `ipc_client`, `m3`, `notifications`, `platform`, `Resources`, `services`, `ux`, `wallpaper`, `wl`.

---

## `src/bootstrap/` -- Application Layer

Everything to do with startup, the event loop, and process lifecycle. These files are created by `main()` before any shell component exists.

Organized into subdirectories by role:

| Subdirectory | Files | Purpose |
|---|---|---|
| `entry/` | `entry_point.cpp`, `app_bootstrap.cpp`/`.hpp`, `screenshot_mode.cpp`/`.hpp` | `main()`, process bootstrap, screenshot mode |
| `session/` | `unified_shell.cpp`/`.hpp`, `detail/unified_shell_session.cpp`/`.hpp` | Shell session lifecycle and run mode dispatch |
| `loop/` | `main_loop.cpp`/`.hpp`, `poll_mux.cpp`/`.hpp` | Frame callback loop and single `poll()` fd multiplexer |
| `thread/` | `thread_dispatch.cpp`/`.hpp`, `thread_pool.cpp`/`.hpp` | Thread-safe deferred call queue and worker pool |

**`entry/entry_point.cpp`** -- Real `main()`. Silences stdout, configures the allocator (jemalloc or glibc `M_ARENA_MAX`), installs a SIGUSR1 handler for jemalloc stats dumps, then constructs `Application` and calls `app.run()`.

**`pch.hpp`** (root) -- Precompiled header used by both the shell core library and the main binary. Includes common STL headers plus `common/log/mangowm_logger.hpp` so every translation unit gets logging stubs without explicit includes.

**`entry/app_bootstrap.cpp` / `.hpp`** sets up the process. `Application::run()` installs signal handlers (SIGINT, SIGTERM), connects to the Wayland display, detects the compositor (Hyprland, Niri, Sway, Mango, etc.), creates the `PollMux` for fd multiplexing, prepares `MainLoop` for frame callbacks, and hands control to `UnifiedShell::run()`. Also exposes `Application::quit()` for graceful teardown and `Application::wayland_display()` for the `wl_display*` pointer.

**`session/unified_shell.cpp` / `.hpp`** owns everything. `UnifiedShell::run()` checks for `--eh-screenshot` and `--eh-settings` flags, parses the `ShellRunMode` (dock, taskbar, both, desktop, etc.), and creates a `UnifiedShellSession` that boots all components -- dock, taskbar, desktop, wallpaper renderer, lock screen, notification toast host, OSD, launchpad, and the embedded settings popup.

**`loop/main_loop.cpp` / `.hpp`** frames the render loop. `MainLoop::tick()` fires on each `wl_callback::done` event, and `request_frame()` schedules the next callback. Add tick listeners via `add_tick_callback()`.

**`loop/poll_mux.cpp` / `.hpp`** is the single `poll()` call that drives everything. Register fds (Wayland display, IPC socket, config watcher, timers) with callbacks, and `poll_mux.poll()` dispatches them. Single-threaded, event-driven.

**`thread/thread_dispatch.cpp` / `.hpp`** bridges the gap for thread safety. Wayland objects are not thread-safe, so `deferred_call::post()` queues lambdas from worker threads and `deferred_call::flush()` runs them on the main loop.

**`entry/screenshot_mode.cpp` / `.hpp`** implements `--eh-screenshot`. Uses `zwlr_screencopy_manager_v1` to capture full outputs or regions, saves as PNG.

**`session/detail/unified_shell_session.cpp` / `.hpp`** parses CLI flags (`--dock`, `--taskbar`, `--desktop`, `--full`, `--standalone`) and launches the right components. The `ShellRunMode` enum controls which subset of the shell starts.

**`wayland_integrated_poll.hpp`** and **`wl_loop_diag.hpp`** (root) are utility headers -- the former wraps `wl_display` dispatch into `poll_mux`, the latter prints event timing for debugging.

---

## `src/backends/` -- Compositor Backends

An abstraction layer so the shell works across Hyprland, Niri, Sway, Mango, Labwc, and Triad. Each compositor gets its own directory with keyboard, output, workspace, and runtime backends.

The base interfaces live in `interfaces/`:

- **`interfaces/compositor_ipc.cpp` / `.h`** -- Abstract `CompositorRuntime` base class with virtual methods for init, poll_fd, dispatch, shutdown, and event callbacks.
- **`interfaces/power_actions.cpp` / `.h`** -- Platform power operations via logind D-Bus: lock, logout, reboot, shutdown.
- **`interfaces/keyboard_backend.h`** -- `KeyboardBackend` base: `apply_keymap()` and `query_keyboard_info()`.
- **`interfaces/workspace_backend.h`** -- `WorkspaceBackend` base: `workspaces()`, `activate_workspace()`, `on_change()`.
- **`interfaces/ext_workspace_backend.cpp` / `.h`** -- Generic workspace backend using the `ext-workspace-v1` protocol, works with any compositor that supports it.

**`ext_workspace/`** -- `ext_workspace_manager.cpp` / `.h` -- client-side helper for binding and tracking `ext-workspace-v1` objects.

Root level: `compositor_kind.hpp` -- `enum class CompositorKind` with values `Unknown, Niri, Hyprland, Sway, Mango, Labwc, Triad`. Free functions like `detect_compositor_kind()`, `compositor_kind_cstr()`, `is_niri()`, etc.

### Hyprland (`hyprland/`)

| File | What it does |
|---|---|
| `hyprland_runtime.cpp` / `.h` | Connects to the Hyprland Unix socket, manages JSON-based IPC event loop |
| `hyprland_event_handler.cpp` / `.h` | Dispatches events: workspace, focusedmon, activewindow, open/close/movewindow, urgent, fullscreen, monitor changes |
| `hyprland_keyboard_backend.cpp` / `.h` | Queries layout via `hyprctl devices -j`, parses JSON |
| `hyprland_output_backend.cpp` / `.h` | Monitor config via `hyprctl monitors -j` |
| `hyprland_workspace_backend.cpp` / `.h` | Tracks workspace state through IPC events |

### Niri (`niri/`)

| File | What it does |
|---|---|
| `niri_runtime.cpp` / `.h` | Connects via `$NIRI_SOCKET`, JSON request/response protocol |
| `niri_keyboard_backend.cpp` / `.h` | Queries keyboard layout from Niri state |
| `niri_output_backend.cpp` / `.h` | Output geometry and fractional scaling |
| `niri_workspace_backend.cpp` / `.h` | Uses `ext-workspace-v1` (Niri supports this natively) |

### Sway (`sway/`)

| File | What it does |
|---|---|
| `sway_runtime.cpp` / `.h` | Connects via `$SWAYSOCK`, uses `swaymsg` JSON protocol |
| `sway_keyboard_backend.cpp` / `.h` | Layout via `swaymsg -t get_inputs` |
| `sway_output_backend.cpp` / `.h` | Outputs via `swaymsg -t get_outputs` |
| `sway_workspace_backend.cpp` / `.h` | Workspaces via `swaymsg -t get_workspaces` + subscribe |

### Triad (`triad/`)

| File | What it does |
|---|---|
| `triad_runtime.cpp` / `.h` | Custom `dwl-ipc-unstable-v2` protocol connection |
| `triad_keyboard_backend.cpp` / `.h` | Uses Wayland virtual keyboard protocol directly |
| `triad_output_backend.cpp` / `.h` | Output geometry from `dwl-ipc-unstable-v2` events |
| `triad_workspace_backend.cpp` / `.h` | Tag/workspace tracking via `dwl-ipc-unstable-v2` |

### Mango (`mango/`)

Stub implementations: `mango_keyboard_backend.h`, `mango_output_backend.h`, and a partial `mango_workspace_backend.cpp` / `.h` that uses Mango's `mmsg` IPC tool for workspace info.

---

## `src/configuration/` -- Configuration

Three files handle the entire config system:

- **`shell_config.cpp`** -- Parses `~/.config/event-horizon/config.toml`. Reads all sections: `[appearance]`, `[dock]`, `[taskbar]`, `[layout]`, `[desktop]`, `[wallpaper]`, `[matugen]`, `[keyboard]`. Produces a `ShellConfig` snapshot.
- **`shell_config.hpp`** (497 lines) -- The `ShellConfig` struct and all its sub-structs: `ShellAppearance` (overlay opacities, launchpad grid, matugen scheme/fonts/colors, day/night schedule), `MatugenExternalTemplateToggles` (toggles for 20+ apps), `ChromePaintColors` (runtime-computed matugen palette). Key functions: `shell_config_load_file()`, `shell_config_snapshot()` for thread-safe reads, `derived_chrome_colors()`.
- **`shell_renderer_backend.hpp`** -- Tiny enum: `Cpu` or `Vulkan`. Selected at startup.

---

## `src/services/` -- D-Bus Service Clients

Each subdirectory wraps a system or session D-Bus service.

**`bluetooth/`** -- `bluez_service.cpp` / `.hpp`: connects to `org.bluez`, monitors adapter state (powered, discoverable, pairable) and device list (connected, paired, name, address, battery). Used by the control center.

**`keyring/`** -- `secret_service_daemon.cpp` / `.hpp`: connects to `org.freedesktop.SecretService`. Stores and retrieves passwords (Wi-Fi credentials, network secrets) with keyring unlock state tracking.

**`mpris/`** -- Two files. `mpris_player.cpp` / `.hpp` monitors `org.mpris.MediaPlayer2.*` on the session bus, tracks all active players with metadata (title, artist, album, art URL), playback status, position, volume, shuffle/repeat. Methods for Play, Pause, Next, Previous, Seek. `mpris_album_art.cpp` / `.hpp` downloads and caches album art from `mpris:artUrl` with LRU eviction.

**`network/`** -- Organized into `core/`, `types/`. `core/network_manager_service.cpp` / `.hpp` connects to `org.freedesktop.NetworkManager` and monitors connections, Wi-Fi access points, device states, and global connectivity. Methods for connect, disconnect, scan. `types/network_secret_agent.cpp` / `.hpp` implements the SecretAgent interface for Wi-Fi password requests. `types/network_types.hpp` defines the data types: `NetworkDevice`, `ActiveConnection`, `AccessPoint`, `SecurityType`, `ConnectivityState`.

**`notifications/`** -- `notification_dbus_service.cpp` / `.hpp` implements `org.freedesktop.Notifications`, forwarding Notify/CloseNotification to the shell's `NotificationManager`; `notifications_spawn.cpp` / `.hpp` spawns and manages the notification host binary.

**`tray/`** -- Organized into subdirectories: `manager/tray.cpp` / `.hpp` implements the StatusNotifierWatcher host; `manager/tray_manager.cpp` / `.hpp` handles high-level tray lifecycle; `dbus/tray_context_menu.cpp` / `.hpp` implements the D-Bus menu interface; `dbus/tray_sni_icons.cpp` / `.hpp` resolves themed icons; `filter/tray_env_filter.cpp` / `.hpp` sanitizes environment for XWayland tray apps; `icon/tray_stream_icon.cpp` / `.hpp` handles animated tray icons; `icon/tray_item.hpp` defines the `TrayItem` struct.

**`polkit/`** -- Organized into `listener/`, `service/`, `dialog/`. `listener/eh_polkit_listener.cpp` / `.h` implements polkit agent authentication; `listener/polkit_bridge_capi.h` is the C ABI bridge; `service/polkit_auth_service.cpp` handles authentication service logic; `dialog/polkit_auth_dialog.cpp` manages the auth dialog UI on its own Wayland connection (seat input via `WaylandSeat`). (The old `dispatch/polkit_seat_dispatch` forwarding layer was removed when the dialog moved off the main connection.)

**`udisks2/`** -- `udisks2_drive_service.cpp` / `.hpp` connects to `org.freedesktop.UDisks2`, monitors drive add/remove and mounting state. Methods: mount, unmount, eject. Used by the desktop for removable drive icons.

Additional service clients:

- **`audio/`** -- `pipewire_service.cpp` / `.hpp`: PipeWire audio session + stream management for playback control and mixer.
- **`autostart/`** -- `autostart_service.cpp` / `.hpp`: autostart entry CRUD (XDG autostart + hidden entries), used by the settings Autostart tab.
- **`bing/`** -- `bing_wallpaper.cpp` / `.hpp`: Bing daily wallpaper fetcher.
- **`global_keyboard/`** -- `global_keyboard_handler.cpp` / `.hpp`: global shortcut capture via WLR foreign keyboard grabs.
- **`global_shortcuts/`** -- `global_shortcuts_service.cpp` / `.hpp`: `org.freedesktop.portal.*` / xdg-desktop-portal global shortcuts.
- **`ipc/`** -- `ipc_server.cpp` / `.hpp` (the shell's Unix-socket IPC server), `ipc_protocol.cpp` / `.hpp` (framed message protocol), `client.cpp` / `.hpp` (the `eh-ipc` client side).
- **`process/`** -- `parent_death_guard.hpp`: PR_SET_PDEATHSIG helper so children die with their parent.
- **`vram_boost/`** -- `vram_boost_manager.cpp` / `.hpp` + `vram_boost_detail.cpp` / `.hpp`: NVIDIA dmem cgroup VRAM boosting.
- **`windows/`** -- `toplevel_bridge.cpp`, `toplevel_client.cpp`, `toplevel_service.cpp`, `toplevel_types.cpp` (+ `.hpp`): toplevel window tracking bridge/service used by the taskbar.

---

## `src/wl/` -- Wayland Protocol Layer

Thin C++ wrappers around Wayland protocols, organized by function into subdirectories.

| Subdirectory | Files | Purpose |
|---|---|---|
| `core/` | `connection.cpp`/`.hpp`, `protocols.hpp`, `memfd.cpp`/`.hpp`, `seat.cpp`/`.hpp`, `gpu_page_trim.cpp`/`.hpp`, `icd_select.cpp`/`.hpp` | Display connection, generated protocol headers, SHM memory, seat management, GPU memory trimming, Vulkan ICD selection |
| `buffer/` | `shm_buffer.cpp`/`.hpp`, `shm_buffer_pool.hpp`, `cairo_cpu_buffer.cpp`/`.hpp`, `single_pixel_buffer.cpp`/`.hpp`, `damage_region.cpp`/`.hpp` | CPU and GPU buffer allocation and management |
| `surface/` | `layer_surface.hpp`, `overlay_premul.hpp`, `surface_extensions.cpp`/`.hpp`, `vulkan_wayland.cpp`/`.hpp`, `vulkan_destruction_queue.cpp`/`.hpp`, `tearing_control.cpp`/`.hpp` | Layer shell surfaces, viewport/cropping/tearing, Vulkan WSI integration |
| `input/` | `pointer_constraints.cpp`/`.hpp`, `clipboard.cpp`/`.hpp`, `virtual_keyboard.cpp`/`.hpp`, `keyboard_shortcuts_inhibit.cpp`/`.hpp` | Pointer constraints, clipboard, virtual keyboard, shortcut inhibit |
| `session/` | `idle_service.cpp`/`.hpp`, `idle_inhibitor.cpp`/`.hpp`, `session_lock_service.cpp`/`.hpp` | Idle detection, idle inhibit, session lock |
| `color/` | `nightlight.cpp`/`.hpp`, `color_management.cpp`/`.hpp` | Gamma control, color management |
| `capture/` | `screencopy_png.cpp`/`.hpp`, `image_copy_capture.cpp`/`.hpp`, `toplevel_stream.cpp`/`.hpp` | Screenshots, image copy capture, live toplevel streaming |
| `toplevel/` | `foreign_toplevels.cpp`/`.hpp`, `ext_foreign_toplevels.cpp`/`.hpp`, `workspaces.cpp`/`.h`, `activation_service.cpp`/`.hpp` | Window tracking, workspaces, XDG activation tokens |

---

## `src/color/` -- Color Pipeline

- **`horizon_colors.cpp`** / `.hpp` -- The matugen color engine: color-space math, contrast, gamut mapping, and palette generation used by theming.
- **`jpeg_decoder.hpp`** -- Header-only JPEG decode helper (stb_image-backed) for wallpaper/thumbnail loading.

---

## `src/dialog/` -- Dialog Widgets

- **`dialog_base.cpp`** / `.hpp` -- Shared dialog window/surface scaffolding.
- **`file_chooser_dialog.cpp`** / `.hpp` -- File/directory picker used by wallpaper and folder settings.

---

## `src/wallpaper/` -- Wallpaper System

Organized into subdirectories by function:

- **`core/wallpaper.cpp` / `.hpp`** -- The `WallpaperRenderer`. Creates a layer-shell background surface per output, renders the wallpaper image with configurable scale mode (fill, fit, stretch, center, tile), and supports crossfade transitions.
- **`apply/wallpaper_apply.cpp` / `.hpp`** -- Reads the `[wallpaper]` config section, resolves image paths (absolute, relative to config, or bundled asset), and triggers rendering on all outputs.
- **`thumbnail/wallpaper_thumbnail.cpp` / `.hpp`** -- Generates scaled-down thumbnails for the settings wallpaper picker. Caches to `~/.cache/event-horizon/wallpaper-thumbs/`.
- **`thumbnail/wallpaper_thumbnail_service.cpp` / `.hpp`** -- Background thumbnail generation via `poll_mux` idle callbacks. Returns shared Cairo surfaces.
- **`raster/wallpaper_raster_decode.cpp` / `.hpp`** -- Decodes PNG, JPEG, BMP, GIF, TIFF, and SVG from disk or memory into Cairo surfaces.
- **`standalone_main.cpp`** -- Entry point for the `horizon-wallpaper` binary; `standalone/` holds its backing state helpers.

---

## `src/ipc_client/` -- IPC Client

**`main.cpp`** -- Standalone binary (`eh-ipc`) for communicating with the running shell over a Unix socket. Usage: `eh-ipc toggle nightlight`, `eh-ipc get volume`.

---

## `src/notifications/` -- Notification Host

- **`standalone_main.cpp`** -- Entry point for the `horizon-notifications` binary.
- **`standalone/`** -- `notifications_standalone.cpp` / `.hpp`: standalone notification host bootstrap (owns the `NotificationManager` and toast surfaces).

(The D-Bus `org.freedesktop.Notifications` service lives in `src/services/notifications/`; the in-shell toast host lives in `src/desktop_shell/notifications/host/`.)

---

## `src/clipboard_daemon/` -- Clipboard Daemon

**`main.cpp`** -- Tiny Wayland clipboard helper (`horizonClipboardDaemon`) that keeps the selection owned for headless/edge cases.

---

## `src/diagnostics/` -- Diagnostics

**`startup_probe.cpp`** -- `eh-startup-probe`: prints startup timing and component stats for debugging perf regressions.

---

## `src/platform/` -- Platform Helpers

**`rfkill_helper.cpp`** / `.h` -- rfkill interface for enabling/disabling radios (used by the network/Bluetooth settings).

---

## `src/assets/` and `src/Resources/` -- Static Assets

Non-source assets embedded into the binaries or shipped alongside them:

- Branding: `Event-Horizon-logo.*`, launchpad artwork (`Dark_Launchpad.*`, `Light_Launchpad.*`).
- Wallpaper: `Default-Wallpaper.jpg`.
- Icons: `Horizon-settings.svg`, `Horizon-wallpaper.svg`, `Horizon-Filesv2.svg`, etc.
- Trash artwork: `MacOS-Trash-Empty.*`, `MacOS-Trash-Full.*`.
- Fonts in `assets/fonts/` (Material Symbols icon font, Noto Sans).

---

## `src/desktop_shell/` -- Core Shell

The main desktop shell. Split into modules by component, each in its own subdirectory.

### `common/` -- Shared Utilities

Files organized into subdirectories by concern:

| Subdirectory | Contents | Purpose |
|---|---|---|
| `animation/` | `animations.cpp`/`.hpp`, `pointer_velocity.cpp`/`.hpp` | Templated `AnimationManager<T>`, cubic bezier easing, spring physics, pointer velocity tracking |
| `asset/` | `asset_loader.cpp`/`.hpp`, `bundled_assets.cpp`/`.hpp` | Compile-time embedded assets via xxd/objcopy, SVG loading from asset search paths |
| `bench/` | `startup_trace.hpp`, `debug_profile.hpp` | Startup timing and frame profiling |
| `font/` | `bundled_fonts.cpp`/`.hpp`, `font_config.hpp` | Material Symbols icon font, Noto Sans, fontconfig wrapper |
| `fs/` | `file_util.hpp`, `shell_file_util.hpp`, `shell_paths.cpp`/`.hpp`, `string_util.cpp`/`.hpp`, `trash_state.cpp`/`.hpp` | Filesystem helpers, path resolution, trash monitoring |
| `geom/` | `rect_overlap.hpp` | Rectangle overlap/math helpers |
| `glyph/` | `bundled_assets.cpp`/`.hpp`, `material_glyph.cpp`/`.hpp` | Material Symbols glyph rendering by name or codepoint |
| `icon_cache/` | `icon_cache.cpp`/`.hpp` | XDG icon theme resolution and raster cache |
| `log/` | `debug_log.hpp`, `mangowm_logger.hpp`, `verbose_log.hpp`, `log_file.hpp` | Stub logging macros, crash logging, file logger |
| `mem/` | `periodic_trim.cpp`/`.hpp` | Periodic heap trim (jemalloc/malloc_trim) + Pango font cache clearing |
| `monitor/` | `output_assign.hpp` | Output assignment helpers |
| `ns/` | `namespaces.hpp` | C++ namespace aliases and using declarations |
| `os_logo/` | `os_logo.hpp` | Distro logo detection and Cairo surface loading |
| `palette/` | `horizon_colors_palette.cpp`/`.hpp`, `horizon_colors_templates.cpp`/`.hpp`, `matugen_palette.cpp`/`.hpp`, `matugen_external_templates.cpp`/`.hpp`, `hyprland_border_matugen.cpp`/`.hpp`, `color_hsv.hpp` | Material You color generation, theme file templates, Hyprland border colors |
| `registry/` | `widget_registry.cpp`/`.hpp` | Desktop widget registry |
| `system_theming/` | `system_theming.hpp`, `gtk_theming.cpp`, `qt_theming.cpp`, `plasma_theming.cpp`, `cursor_theming.cpp`, `system_theming_utils.cpp`/`.hpp` | GTK/Qt/Plasma/cursor theme application |
| `time/` | `mono_time.cpp`/`.hpp`, `text_caret.hpp` | Monotonic timing, caret blink |
| `tray/` | `tray_session_defer.hpp` | Tray session deferral helpers |
| `workspace/` | `workspace_strip_types.hpp` | Workspace strip type definitions |

### `shared/` -- Shared Infrastructure

Code shared between the dock and taskbar panels.

**`shared/core/`** -- `app_launch.hpp` (fork+exec helpers), `cairo_helpers.hpp` (rounded rects, hex colors, centered text), `config_watch.hpp` (inotify-based config reloading), `running_snapshot.hpp` (window grouping by app_id).

**`shared/popup/`** -- Popup infrastructure organized into subdirectories:
- `buffer/buffer.cpp` -- Buffer management for popup surfaces
- `caret/caret.cpp` -- Text cursor rendering
- `chrome/chrome.hpp` -- Chrome color derivation for popup surfaces
- `geometry/layout.cpp`, `margins.cpp` -- Text layout via Pango, margin computation
- `paint/finish.cpp`, `paint_context_menu.cpp` -- Surface teardown, context menu painting
- `session/session.cpp` -- Session tracking and popup lifecycle
- `dispatch/popup_items.cpp` -- Shared popup menu item dispatch
- `popup_position.cpp` -- Position computation relative to anchor

**`shared/layout/`** -- `strip_geometry.hpp` computes slot positions in horizontal strips.

**`shared/paint/`** -- `glass_card_style.hpp` -- shared glassy card painting.

**`shared/pins/`** -- `pin_identity.cpp` / `.hpp` -- App ID normalization and pin matching.

**`shared/settings/`** -- `settings_io.cpp` reads/writes `settings.ini` for mutable per-user state (pinned apps, window positions). `settings_watch.cpp` monitors it for changes.

**`shared/system/`** -- `wayland_state.hpp` (registry state cache) plus `input/`.

**`shared/toplevel/`** -- `toplevel_hooks.cpp` / `.hpp`, `toplevel_tracker.cpp` / `.hpp` -- shared toplevel hooking and tracking for pin state.

**`shared/widgets/`** -- `workspace_strip.cpp` renders workspace dots/bars with active, occupied, and urgent states.

### `dock/` -- Dock Panel

Files organized into subdirectories by function. The central state object is `DockApp` in `core/dock_app.h` (396 lines). It holds everything: Wayland objects, per-monitor output slots, settings, popup state, app drawer state, pin state, tray items, foreign toplevel list, gamma service, OSD host, animation manager, tooltip state, and more.

| Subdirectory | Key files | Purpose |
|---|---|---|
| `core/` | `dock_bar.cpp` (2793 lines) + `dock_bar.h`, `dock_app.h`, `dock_settings.hpp` | Main dock implementation, frame scheduling, auto-hide, render loop, settings |
| `input/` | `dock_pick.cpp`/`.hpp`, `dock_position.cpp`/`.hpp`, `dock_input_region.hpp`, `dock_slot_dispatch.cpp`/`.hpp` | Pointer/keyboard/touch input, layer-shell positioning, hit testing, slot click dispatch |
| `paint/` | `dock_paint_widget_bar.cpp`/`.hpp`, `dock_raster_backend.cpp`/`.hpp`, `dock_anim.hpp`, `dock_strip_geometry.hpp` | Bar rendering, Vulkan/CPU backend selection, dock animation, strip geometry |
| `pinned/` | `dock_pinned.cpp`/`.h`, `dock_pin_identity.cpp` | Pinned app CRUD, app ID normalization |
| `widgets/` | `dock_widget_tokens.cpp` | Widget token parsing and management |
| `tooltip/` | `dock_tooltip.cpp`/`.hpp` | Tooltip layer surfaces with configurable delay |
| `launch/` | `dock_launch_feedback.cpp`/`.hpp` | Launch bounce animation |
| `layout/` | `dock_layout_shared.hpp` | Layout shared helpers |
| `output/` | `dock_layer_outputs.hpp` | Per-output layer assignment |
| `spawn/` | `dock_spawn.cpp`/`.hpp` | Particle spawn effects (start dock animation) |
| `standalone/` | `dock_standalone.cpp` + `standalone_main.cpp` | `horizon-dock` binary entry |

### `taskbar/` -- Taskbar Panel

Organized into subdirectories. Architecturally mirrors the dock but with independent state and settings.

| Subdirectory | Key files | Purpose |
|---|---|---|
| `core/` | `taskbar.cpp` (3786 lines), `taskbar.hpp`, `taskbar_settings.hpp` | Main implementation, `TaskbarApp` struct, settings |
| `Features/` | `taskbar_pin_drag.cpp`/`.h`, `taskbar_pinned.cpp`/`.h` | Pin drag & drop, pinned-app sessions |
| `paint/` | `taskbar_paint.cpp`/`.hpp` | Bar rendering |
| `layout/` | `taskbar_position.cpp`/`.hpp`, `taskbar_types.hpp` | Surface positioning, type definitions |
| `spawn/` | `taskbar_spawn.cpp`/`.hpp` | App launch/animation |
| `standalone/` | `taskbar_standalone.cpp` + `standalone_main.cpp` | `horizon-taskbar` binary entry |

### `notifications/` -- Notification Management

- `core/notifications.cpp` / `.hpp` -- The `NotificationManager` (in-shell notification state and lifecycle).
- `host/notification_toast_host.cpp` / `.hpp` -- On-screen toast layer surfaces.
- `types/` -- `notification_types.hpp`, `notifications_notify.hpp`, `notifications_ipc.hpp` (IPC framing for the notification host).

### `desktop/` -- Desktop Surface & Widgets

- `core/` -- background desktop surface (color/image/offscreen).
- `icons/` -- desktop file/folder icons, context menus, drag handling.
- `entries/` -- `.desktop` entry parsing, XDG ops (URI/clipboard), open-with.
- `audio/` -- desktop audio widgets (cava config helpers).
- `spawn/` -- desktop spawn/animation helpers.
- `standalone/` + `standalone_main.cpp` -- `horizon-desktop` binary entry.
- `widgets/` -- the desktop widget host (`desktop_widget.hpp`, `desktop_widget_host.cpp`, `desktop_widget_layer.cpp`, `desktop_widgets_preferences.cpp`) and widget implementations: `calendar/`, `clock/`, `media_compact/`, `media_player/`, `shared/`, `system_monitor/`, `weather/`, `weather_fancy/`, `world_clock/`.

### `widgets/` -- Bar Widgets & Popups

| Subdirectory | Purpose |
|---|---|
| `clock/` | Clock widget (module root) + popup calendar |
| `weather/` | Weather widget |
| `media/` | Media (MPRIS) widget |
| `battery/` | Battery widget (UPower) + popup |
| `bluetooth/` | Bluetooth widget + popup |
| `world_clock/` | World clock widget + analog face painting |
| `workspaces/` | Workspace strip widget |
| `start_menu/` | Start menu overlay (`start_menu.cpp`, `popup_paint_start_menu.cpp`, `start_menu_zone.hpp`) |
| `app_drawer/` | App drawer overlay (`overlay/`, `list/`, `input/`, `power/`, `program/`, `trace/`) |
| `popup/` | Shared popup surfaces: `calendar/`, `media_player/`, `volume_mixer/`, `vpn/`, `weather/` |
| `shared/` | Shared widget paint helpers (`slot_pill_style.hpp`, `glass_card_style.hpp`, etc.) |

### `controlcenter/` -- Quick Settings

- `paint/` -- `control_center_panel_geometry.cpp` (layout), popup painting: `control_center_popup_paint.cpp` (997 lines), `control_center_paint.cpp`, `control_center_paint_utils.hpp`, `control_center_notifications_strip.cpp`.
- `input/` -- `control_center_dispatch.cpp` (567 lines), `control_center_hit.cpp`.
- `layout/` -- animation + geometry helpers (`control_center_anim.hpp`, `control_center_notifications_layout.hpp`, `control_center_slider_metrics.hpp`).
- `mixer/` -- `mixer_stream_icon_resolve.cpp`.
- `persist/` -- `control_center_persist.cpp` / `.hpp` -- persisted CC state.
- `state/` -- `control_center_state.hpp` -- the control-center `AppState`.
- `widgets/` -- per-section widgets: `audio/`, `bluetooth/`, `media/`, `mixer/`, `network/`, `notifications/`, `weather/`.

### `Overview/` -- Workspace Overview

`overview_host.cpp` / `.hpp`, `overview_painter.cpp` / `.hpp`, `overview_input.cpp` / `.hpp`, `overview_capture.cpp` / `.hpp`, `overview_actions.cpp` / `.hpp`, `overview_reveal.cpp` / `.hpp`, `overview_types.hpp`, `overview_profiler.hpp` -- multi-monitor workspace overview/scale mode: window thumbnails, drag-to-workspace, add-workspace slots.

### `ui/` -- Shell UI Toolkit

A small internal UI toolkit used by shell popups: `button/`, `slider/`, `toggle/`, `dropdown/` (each with theme-able paint), `theme.cpp` / `.hpp` (shell `UITheme`), and `fallback_palette.hpp`.

### `launchpad/` -- Launchpad

Host (`host/launchpad_host.cpp` -- layer surface lifecycle, folder drag/create, TOML state, painting), layer (`layer/launchpad_layer.cpp` -- grid layout, folder overlay paint), and search (`search/launchpad_search.cpp`).

### `lockscreen/` -- Session Lock

Flat: `session_lock.cpp` (830 lines) / `.hpp` -- Wayland session-lock screen with PAM auth, output management, and input forwarding.

### `osd/` -- OSD Overlays

`host/osd_host.cpp`, `audio/osd_audio.cpp`, `brightness/osd_brightness.cpp` -- volume and brightness overlays.

### `spotlight/` -- App Search

`search/spotlight_keyboard.cpp`, `spotlight_query.cpp` (+ `.hpp`), `paint/popup_paint_spotlight.cpp` -- keyboard-activated application search overlay.

### `power_confirm/` -- Power Modal

Flat: `power_confirm.cpp` / `.hpp` -- power action confirmation modal.

### `keyboard/` -- Keyboard Apply

Flat: `keyboard_settings_apply.cpp` / `.hpp` -- applies the keyboard layout from config at startup.

### `unified/` -- Unified Shell Glue

`compositor_kind.cpp` / `.hpp` (compositor detection), `unified_layer_outputs.cpp`, `unified_wayland_registry.cpp` / `.hpp` (protocol registry binding).

---

## `src/ux/` -- User Apps (Settings, Disks, Live Wallpaper)

The user-facing applications live here. Each is a standalone binary and can also be embedded into the shell.

### `ux/settings/` -- Settings Application

Root files: `settings_app.cpp` (1742 lines, the main app), `settings_app_types.hpp`, `settings_serialize.cpp` / `.hpp`, `theme_data.hpp`, `theme_manager.cpp` / `.hpp`.

Organized into subdirectories by role:

| Subdirectory | Purpose |
|---|---|
| `settings_tab_accounts/`, `settings_tab_appearance/`, `settings_tab_autostart/`, `settings_tab_bing/`, `settings_tab_bluetooth/`, `settings_tab_color_themes/`, `settings_tab_default_apps/`, `settings_tab_desktop/`, `settings_tab_desktop_widgets/`, `settings_tab_dock/`, `settings_tab_dock_appearance/`, `settings_tab_hyprland/`, `settings_tab_icons/`, `settings_tab_keyboard/`, `settings_tab_launcher/`, `settings_tab_live_wallpaper/`, `settings_tab_mango/`, `settings_tab_monitors/`, `settings_tab_network/`, `settings_tab_nightlight/`, `settings_tab_notifications/`, `settings_tab_power/`, `settings_tab_sound/`, `settings_tab_taskbar/`, `settings_tab_themes/`, `settings_tab_time/`, `settings_tab_ui_layout/`, `settings_tab_wallpaper/`, `settings_tab_workspaces/` | One subdir per settings tab |
| `data/` → `default_apps/`, `hyprland/`, `mango/`, `monitors/`, plus `settings_desktop_widgets_data.hpp` | Settings data models |
| `utils/` → `dropdown/`, `events/`, `gpu/`, `helpers/`, `monitors/`, `scroll/`, `sound/`, `wallpaper/`, `widget_picker/` | Shared utility implementations |
| `common/` → `embed/`, `logo/`, `trace/`, plus `settings_common.hpp`, `settings_standalone_main.cpp` | Shared common components and the `horizon-settings` entry |

Each tab directory may contain further subdirs: `m3/` (Material 3 widget bindings), `tabs/`, `dialogs/`, `widgets/`, `common/`.

### `ux/disks/` -- Horizon Disk Utility

`horizon-disks` binary. Files: `main.cpp`, `app.cpp`, `app_types.hpp`, `block.cpp`, `drive.cpp`, `manager.cpp`, `item.hpp`, `feature.h`, subdirectories `dialogs/` (create partition, format disk, format volume), `embed/embed.cpp` (embedded-in-settings mode), `input/events.cpp`, `ui/draw.cpp`.

### `ux/Live_Wallpaper/` -- Standalone Live Wallpaper

`horizon-live-wallpaper` binary. Files: `standalone_main.cpp`, `app.cpp`, `app_types.hpp`, `settings.cpp`, `gallery.cpp` / `.hpp` (ported gallery helpers), subdirectories `input/events.cpp`, `ui/draw.hpp`, `embed/embed.cpp`.

### `ux/Powermenu/` -- Power Menu Overlay

Flat: `power_menu.cpp` / `.hpp` -- power menu overlay UI.

---

## `src/m3/` -- Material 3 UI Components

Reusable Material Design 3 widget library, organized by component category:

| Subdirectory | Components |
|---|---|
| `core/` | `animation.hpp`, `focus_ring.hpp`, `glyph.hpp`, `label.hpp`, `primitives/` (`box.hpp`, `flex.hpp`, `separator.hpp`, `spacer.hpp`, `state_layer.hpp`) |
| `controls/containers/` | `button.hpp`, `card.hpp`, `fab.hpp` |
| `controls/display/` | `banner.hpp`, `bottom_sheet.hpp`, `carousel.hpp`, `dialog.hpp`, `snackbar.hpp` |
| `controls/input/` | `checkbox.hpp`, `chip.hpp`, `date_picker.hpp`, `input.hpp`, `radio_button.hpp`, `select.hpp`, `slider.hpp`, `time_picker.hpp`, `toggle.hpp` |
| `controls/navigation/` | `bottom_app_bar.hpp`, `nav_bar.hpp`, `nav_drawer.hpp`, `nav_rail.hpp`, `search_bar.hpp`, `tabs.hpp`, `top_app_bar.hpp` |
| `tokens/` | `color.hpp`, `elevation.hpp`, `motion.hpp`, `shape.hpp`, `spacing.hpp`, `type_scale.hpp` -- the M3 design tokens |
| `styles/` | `theme.cpp`/`.hpp` (`UITheme` struct with material colors from `ShellAppearance`), `color_tokens.hpp` |
| `builder.hpp` | Tree-builder DSL for composing M3 widget hierarchies |