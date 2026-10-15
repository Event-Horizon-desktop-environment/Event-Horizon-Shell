# Changelog

All notable changes to Event Horizon, newest first.

## [Unreleased]

### Overview: App grid with folders, search and drag reorder

The overview gained a full app grid mode next to the workspace cards. It replaces the separate Launchpad surface, so search, folders and launching live in one place.

- Grid mode is toggled by the bottom pill or by typing (`src/desktop_shell/Overview/overview_host.cpp`, `overview_painter.cpp`, `overview_input.cpp`). Layout caps at 1060px wide with 64px icons, paged dots and per row clipping, and icons resolve on a background thread with an accent initial fallback.
- Search filters through the app drawer query and flattens folders while active. Arrow keys, Tab, PageUp and PageDown move selection, Enter launches and Escape clears the query or exits.
- Folders group apps automatically by category with manual overrides persisted to `overview_folders.toml`. Folder tiles show 2 by 2 mini icons, open in a glass modal with rename (F2) and persist across sessions.
- Drag and drop starts after 8px, shows a lifted ghost with an insertion bar, drops app on app to create a folder and app on folder to move in, and drags out of an open folder to remove. Right click pins to dock or taskbar.

### Panel: New optional bar process (horizon-panel)

There is a new thin edge bar for setups that want indicators without a full dock or taskbar. It is off by default and enabled from Settings.

- New `horizon-panel` binary (`src/desktop_shell/panel/`, `standalone_main.cpp`, `standalone/panel_standalone.cpp`). It owns its own Wayland connection, foreign toplevel tracking, icon cache, MPRIS, tray and popups, with the parent death guard and pidfile handling shared by the other children.
- The supervisor spawns it only when `panel.enabled` is true (`src/desktop_shell/panel/spawn/panel_spawn.cpp`, `src/bootstrap/session/detail/unified_shell_session.cpp`) and reaps and respawns it on crash with the same cooldown as dock and taskbar.
- Layout is a three zone strip (left, centered clock, right indicators) with floating, edge to edge and fill width modes, auto hide, overlay layer and scroll to change volume. Popups cover tray, calendar, weather, volume mixer, control center, VPN, battery, media, Bluetooth and layout.
- New Panel settings tab (`src/ux/settings/settings_tab_panel/`) mirrors the taskbar tab with Settings, Widgets and Appearance child tabs, a shared output picker and resettable widget defaults.

### Stage: Overview, Dashboard and OSD move out of the dock

Overview and Dashboard no longer die with the dock. A new always on `horizon-stage` process owns all on demand fullscreen surfaces.

- New `horizon-stage` binary (`src/desktop_shell/stage/core/stage_app.cpp`, `standalone/stage_standalone.cpp`, `spawn/stage_spawn.cpp`). It hosts the overview, the dashboard and the OSD on its own connections, owns the Super key toggle, and subscribes to `config.applied` and `command.request` for `overview.toggle`, `dashboard.toggle` and related commands.
- The old in process overview and launchpad hooks were removed from the supervisor and dock. Killing or turning off `horizon-dock` no longer takes the overview down with it.
- Middle click on panel workspaces toggles the overview through IPC, and the stage handles open, close and toggle commands plus hover reveal and Escape close for the dashboard.

### Taskflip: New window switcher with gallery and rail

Alt Tab now opens a dedicated switcher instead of cycling silently. It keeps most recently used order and activates through the foreign toplevel protocol.

- New switcher (`src/desktop_shell/switcher/taskflip_model.cpp`, `taskflip_host.cpp`, `taskflip_gallery.cpp`, `taskflip_rail.cpp`, `taskflip_ipc.cpp`). The model rebuilds from toplevels, preserves selection by handle and promotes the activated window to the front.
- Gallery shows a cover flow of up to 9 windows with accent halo and count, rail shows a horizontal strip with icons and ellipsized titles. Both are centered overlays with no exclusive zone.
- Commands are `taskflip.toggle`, `open`, `close`, `next`, `prev`, `confirm` and `cancel` over IPC. Confirm activates via `zwlr_foreign_toplevel_handle_v1`.
- New Taskflip settings (`src/ux/settings/settings_tab_taskflip/`) with gallery style, recent order, captions, counts and icons toggles.

### Dashboard: Prototype top edge widget grid

A new top edge dashboard hosts control center style cards in a modular grid. It is revealed by hovering a thin trigger strip, so it stays out of the way until needed.

- New dashboard (`src/desktop_shell/dashboard/dashboard_types.hpp`, `dashboard_layout.cpp`, `dashboard_paint.cpp`, `dashboard_hit.cpp`, `dashboard_dispatch.cpp`, `dashboard_surface.cpp`). Cards cover clock, calendar, weather, media, volume, mic, network, Bluetooth, mixer and system, with expandable network and Bluetooth rows, slider drags throttled to 16ms and MPRIS transport actions.
- Layout is config driven (`src/configuration/shell_config.cpp`): columns, gap, margins and max width plus an ordered card list where entries are widget ids with optional column spans. Defaults pack time, media, connectivity, control and monitor rows into 4 columns.
- The trigger strip is a permanent transparent input surface while hover reveal is on, the panel surface is created per open and destroyed on close with 220ms open and 180ms close easing.
- The old `launchpad.toggle` IPC path was removed. App browsing lives in the overview grid and the Tahoe app drawer, the dashboard covers controls.

### Control Center: Pear compact layout

The control center gained a compact Pear variant for the panel, taskbar pill and small popups. It uses one layout as the source of truth for paint and hit testing.

- New Pear layout (`src/desktop_shell/controlcenter/layout/control_center_pear_layout.cpp`, `state/control_center_pear_config.hpp`). Base width is 360px scaled by UI and pear scale, with 64px pills, 64px toggles and a 90px Section B for volume, input, brightness and media.
- New Pear paint and hit testing (`src/desktop_shell/controlcenter/paint/control_center_pear_paint.cpp`, `input/control_center_pear_hit.cpp`) mirror the layout exactly, including network, Bluetooth, settings, do not disturb, slider tracks, device rows and media buttons.
- Pear is configured per widget from `[widget.<id>] pear_*` keys (scale, transparency, themes, toggle visibility, device link and command strings). Taskbar control center height is measured from the Pear layout and clamped to the output with an 800ms settle resize.

### Settings: Panel, Updates and global search plus Monitor and Bluetooth rework

Settings grew two wired tabs and a global index, and two hardware tabs were reworked. Taskflip settings exist on disk with a paint function but have no sidebar dispatch yet, and Dashboard settings are a header only stub.

- New Updates tab (tab 51, `src/ux/settings/settings_tab_updater/`): device card with version, package kind and auto check toggle on a 6 hour poll, software update card with Check now, Download, Install and armed Restart shell actions, and a release notes card showing the first 8 lines.
- New Panel tab (tab 1, `src/ux/settings/settings_tab_panel/`): Settings, Widgets and Appearance child tabs with width mode, auto hide, tooltips, three column drag widgets and slider cards, matching the taskbar tab.
- New global search (`src/ux/settings/utils/search/settings_search.hpp`): an index over every tab with AND token matching, compositor filtering and a 120 result cap, with a fixed search bar shifting tab content down.
- Shared widget defaults (`src/ux/settings/utils/widget_defaults/settings_widget_defaults.hpp`) give Reset to defaults one source of truth for dock, taskbar and panel. A shared output picker (`src/ux/settings/utils/display/settings_display_dropdown.hpp`) covers Auto, All and per output names with hot plug refresh.
- Monitors tab (tab 7) gained a drag canvas with zoom and pan, Display, Scale, Color, HDR and Luminance sections and compositor gated VRR. Bluetooth tab (tab 47) groups Connected, Remembered and Nearby with per row Pair, Connect, Disconnect and Forget plus battery and signal lines. Sound tab (tab 8) added engine quantum controls and Bluetooth profile combos.

### Updater: Self update from GitHub releases

The shell can now check, download and install its own packages. It detects the distro package kind and restarts the session after install.

- New service (`src/services/updater/updater_service.cpp`, `updater_service.hpp`). It polls `releases/latest` on libcurl, compares versions with stable above beta, picks assets by suffix for rpm, deb and pacman from `/etc/os-release` plus binary probes, and installs through `pkexec` with detached threads and an eventfd wake.
- Preferences live in `updater.toml` under `$XDG_STATE_HOME` with auto check and 6 hour due logic. Restart uses a double fork through `systemctl --user restart event-horizon` with pgrep and nohup fallbacks.
- Fixed in follow ups: version compare and asset matching plus a notification service null check on the update path.

### Widgets: Layout switcher, Fetch and media plus calendar and weather refresh

A new layout widget switches compositors layouts from the bar, and several desktop widgets were split into shared paint headers or rebuilt.

- New layout widget (`src/desktop_shell/widgets/layout/layout_paint.cpp`, `widgets/popup/layout/layout_popup.cpp`): per compositor Hyprland and Mango layout switching from dock, taskbar and panel slots with a popup picker.
- New Fetch desktop widget (`src/desktop_shell/desktop/widgets/fetch/desktop_widget_fetch.cpp`): a fastfetch style card with logo, user at host and key value rows polled every 5 seconds on a background thread.
- Calendar and fancy weather paint moved into shared headers (`calendar_card_paint.hpp`, `fancy_weather_card_paint.hpp`) so desktop and popup cards share one painter. Media player and media compact were rebuilt as liquid glass cards on `DockMpris` with art, marquee titles, CAVA strip, seek and transport controls.
- System monitor now parses swap totals and per package RAPL energy alongside `/proc` CPU, memory, net and NVML GPU with 40 sample sparklines. World clock, clock and weather widgets picked up per second ticks and output selection fixes.

### Bluetooth: Auto reconnect, pairing agent and radio block

Bluetooth moved to a dedicated discovery bus with host side auto reconnect and a proper pairing agent. Airplane and rfkill handling now goes through one module.

- `BluezService` (`src/services/bluetooth/bluez_service.cpp`) runs discovery on its own bus, pairs, trusts and connects asynchronously, persists ever connected devices and nudges them back when they return in range with cooldown and manual disconnect respect, and never blocks D-Bus on the event thread.
- `BluetoothAgent` handles NoInputNoOutput JustWorks with PIN, passkey, confirm and authorize requests. Settings and widgets map friendly errors for powered off, rfkill and scan failures.
- New `radio_block` module (`src/platform/radio_block.cpp`) aggregates `/sys/class/rfkill` and writes `/dev/rfkill` for Bluetooth and Wi-Fi with hard block reporting, replacing the old rfkill helper.
- Dock off crash fix: turning the dock off no longer crashes Settings (`f260e52`). Tooltip and pick paths handle missing layers.

### Disks: Utility overhaul with benchmark, SMART, imaging and job tracking

Horizon Disks was rebuilt around one layout module shared by paint and hit testing, with background jobs and eight modal dialogs.

- Single layout source of truth (`src/ux/disks/ui/layout.cpp`, `ui/theme.hpp`): header, sidebar, drive card, proportional volume bar with free space synthesis, GParted style partition table, details grid and job bar all share geometry with hit testing.
- New shared widgets and icons (`src/ux/disks/ui/widgets.cpp`, `ui/icons.cpp`): pill, icon and action buttons, checkboxes, filesystem pills with GParted colors, usage bars, hatch rects, spinners and bundled Eva and Lucide SVGs.
- New `JobTracker` (`src/ux/disks/jobs.cpp`): per block active and recent jobs with determinate and indeterminate progress, header pill, per row spinner and footer job bar.
- Dialogs (`src/ux/disks/dialogs/`): benchmark with 100 sample read graph, SMART with assessment banner and self tests, create and restore image with progress and overwrite warning plus image attach, drive settings for standby and write cache, mount options, resize with minimum size guard, LUKS unlock with keyring option, disk format with erase mode and GPT or MBR, volume format with encryption, and GParted style create partition defaulting to the largest free gap.

### Spotlight: Prefixed seekers for power, math, windows and emoji

Spotlight gained a small provider registry for prefixed queries. Each provider declares a prefix, global flag and scoring.

- New registry (`src/desktop_shell/spotlight/providers/seeker_registry.cpp`): `/prefix query` parsing with a staged window list for the windows provider.
- Power (`seeker_power.cpp`, prefix `power`): lock, logout, reboot and shutdown rows. Math (`seeker_math.cpp`, prefix `calc`, global): hand rolled parser for arithmetic with power, parens and unary plus. Windows (`seeker_windows.cpp`, prefix `win`): substring match on title and app id up to 8 rows. Glyphs (`seeker_glyphs.cpp`, prefix `emo`): about 50 keyword to emoji rows copied via `wl-copy`.
- Keyboard handling now moves through results consistently and query parsing is shared with the overview grid.

### Backends: Triad removal and Niri, Hyprland and Sway hardening

The unmaintained Triad backend was removed and the remaining workspace managers got JSON and id parsing hardening.

- Deleted `src/backends/triad/` (6 files, about 1052 lines) and all Triad arms in compositor IPC, power actions, session, config, docs and meson. No `triad` references remain in `src`.
- Niri (`src/backends/niri/niri_workspace_manager.cpp`): hardened JSON helpers with object, type and range checks and a whitespace collapsing title rewrite plus runtime query dispatch fixes.
- Hyprland (`src/backends/hyprland/hyprland_workspace_manager.cpp`): workspace id resolve moved from `strtol` to `sscanf`, focus split between Lua and dispatch paths with unanswered command logging. Sway got the same id hardening.
- Docs and ProjectMap updated to list only Hyprland, Sway, Niri and Mango paths.

### Notifications and OSD: Output choice, rules, pipeline stats and rework

Notifications can target an output and filter by rules, with pipeline stats for diagnosis. OSD audio and brightness widgets share a slimmer host.

- Notifications settings gained an output name (empty Auto, `all` for primary, else a Wayland output name with fallback) and a `NotificationRule` list with toast, history, sound, do not disturb bypass and urgency filters (`src/configuration/shell_config.hpp`).
- New pipeline stats (`src/desktop_shell/notifications/diag/notification_pipeline_stats.hpp`) plus toast host deduplication by summary and body, overlay opacity, stall watchdog over 250ms frame gaps and MPRIS now playing dedup (`src/desktop_shell/notifications/host/notification_toast_host.cpp`, `core/notifications.cpp`).
- IPC types (`src/desktop_shell/notifications/types/notifications_ipc.hpp`) carry the updated kinds and art blobs. OSD host, audio and brightness (`src/desktop_shell/osd/host/osd_host.cpp`, `osd/audio/osd_audio.cpp`, `osd/brightness/osd_brightness.cpp`) were reworked for the stage split.

### Services: Redeye inhibit, MPRIS track dedup and tray timeouts

Small services were added or tightened so background polling stays off the frame path.

- New `RedeyeService` (`src/services/redeye/redeye_service.cpp`): a singleton that creates and destroys a `zwp_idle_inhibitor_v1` per surface with mutex guarding, wired to `power.redeyeEnabled`.
- New `track_changed` helper (`src/services/mpris/mpris_track_change.hpp`): prefers `mpris:trackid` then title to avoid bogus notifications on partial browser metadata merges, covered by `test_mpris_track_change.cpp`.
- Tray and disks D-Bus calls are bounded to 2 seconds. New tests cover widget defaults and the settings singleton (`test_widget_defaults.cpp`, `test_settings_singleton.cpp`, `test_hit_registry.cpp`).
- Ptyxis palette and matugen template fix (`349e147`): color engine and theme sync corrected for Ptyxis.

### Polkit: Session agent split into its own process

The password dialog used to run inside the supervisor with its own Wayland connection wired into the main poll loop. A protocol error or a wedged cancel could land inside the shell process, the same class of bug every other service was already split out to avoid.

- New `horizon-polkit-agent` binary (`src/services/polkit/standalone/`, `standalone_main.cpp`). It registers as the session `org.freedesktop.PolicyKit1.AuthenticationAgent`, pumps its own GLib context through the poll loop hooks and owns the dialog and seat input on its own connection. The agent code stays in `eh_shell_core` and re registers on polkitd restarts.
- The supervisor only spawns it (`src/services/polkit/spawn/polkit_agent_spawn.cpp`) with log redirect, SIGCHLD reap, cooldown respawn, deferred retry and graceful shutdown. All in process polkit members and teardown were removed from the session.
- Dialog fixes: snapshot carries identity, icon name, details and info on separate bridge channels instead of one prompt field. The dialog renders user at host, message, action id, details and themed icon with the session request as the password label.

### Polkit: Compact dialog redesign with reference parity

The dialog was audited against mate-polkit and GNOME Shell 51 for lifecycle, cancel, retry, info and error lines, action id, details, themed icon and Escape and Enter handling. Two missing behaviors were added: identity prefers current user then root then first user, and a bare Password prompt for another user renders as Password for user.

- Compact card (`src/services/polkit/dialog/polkit_auth_dialog.cpp`): 480px wide sized to content every paint with no scrim, so the surface floats over the untouched desktop. Rows use Inter through Pango with title, accent user at host and action id meta, wrapped message, dimmed details and themed error line, plus masked field with accent caret and outlined Cancel and filled Confirm buttons.
- Colors come from the wallpaper derived chrome and reload on `config.applied` over IPC like the other children.
- Robustness from live logs: show no longer wipes a typed password on mid dialog notifications except for a new cookie or failed attempt, empty hidden prompt submits are swallowed instead of burning an attempt, registration retries 6 times then exits for respawn, and the supervisor will not stack a duplicate over a live child. Follow up fixes cover listener channels, logging (`src/services/polkit/polkit_log.hpp`) and auth service threading.

### Default Apps: File manager choice now covers FileManager1

Browser "Open containing folder" and in-shell reveal actions use the `org.freedesktop.FileManager1.ShowItems` D-Bus call, which ignores the `inode/directory` MIME handler. Whichever app owned that bus name won, no matter what Default Apps said, so choosing Horizon Files as the file manager still opened Otter Files from browsers.

- Added a `horizon-filemanager1` proxy (`src/services/filemanager1/`). It owns `org.freedesktop.FileManager1` and forwards `ShowFolders`, `ShowItems` and `ShowItemProperties` to whichever file manager is configured. The default is looked up on every call (component TOML first, then `xdg-mime query`, with `xdg-open` or `gio open` as a last resort), and the target's Exec field codes are expanded.
- Settings now keeps a user level `org.freedesktop.FileManager1.service` override in sync every time Default Apps is applied. The override is removed when the choice is empty or when the chosen app provides the name itself (Otter, for example). Otherwise it is installed and points at the proxy. Overrides created by other software are never touched.
- The file manager MIME list now includes `inode/directory` and `inode/mount-point`.
- Added a `filemanager1_override` unit test.

### Taskbar: Prescaled icon cache (paint time from 11ms down to simple blits)

The new phase timings pinned the problem down. `paint` took 11 to 36ms while `reload`, `measure` and `present` were near zero, so the whole cost sat inside `taskbar_paint_widget_bar`. Every icon was being scaled down from its full size source (384px app icons, full size tray pixmaps) to slot size on every frame, in software. That meant about two dozen bilinear downscales per draw on the CPU raster that then gets uploaded.

- New `TaskbarScaledIconCache` maps a source and a size to a prescaled surface, so the downscale happens once and each frame is a plain 1:1 blit. It is keyed by source pointer, so a theme switch or a tray pixmap update (which recreates the source) misses the cache automatically. It is cleared on a theme or icon size change and holds at most 256 entries (first in, first out). In normal use it holds roughly two dozen entries and every lookup hits, because hover animations only touch one slot at a time.
- It uses the same bilinear filter and the same geometry apart from sub pixel rounding, so nothing looks different. The work is just no longer repeated.
- All four full resolution scaling sites now use it: pinned, running, floating pin and tray pixmaps.

### Taskbar: Shared snapshots, phase timers and region caching

A follow up to the frame rate fix below. `draw_p50_hint_ms` was still around 11ms after MPRIS and tracing left the frame path, so the remaining cost was in the paint itself.

- Snapshots are now built once per draw instead of three times. `build_running_snapshot()` (walks every toplevel and allocates the group vectors) and `DockMpris::snapshot()` (copies the player strings) used to be rebuilt in the measure pass, the layout pass and the paint pass of every frame. `taskbar_draw()` now builds both once and shares them with `taskbar_measure_sections()` and `taskbar_paint_widget_bar()`.
- `[taskbar-fps]` now reports a phase breakdown (`reload`, `measure`, `paint` and `present` averages per 5 second window), so the next slow frame names its own cause instead of needing another audit.
- Blur and input regions are only updated when the geometry changes. Every frame used to create and destroy two `wl_region` objects (ten protocol messages) for a bar that almost never resizes. Each layer now remembers the geometry its regions were set for and skips the work otherwise. Nothing looks different, since regions are purely geometric.

### Taskbar: Animation frame rate was about 50x the dock's draw cost

Taskbar animations visibly lagged behind the dock's. Runtime logs showed the taskbar drawing at a median of about 10ms per frame (p90 about 23ms, worst case 1064ms), against about 0.2ms for the dock. With a budget of 16.7ms per frame at 60Hz, the taskbar was dropping frames on most animated transitions.

- MPRIS polling ran on the frame path. `taskbar_draw()` called `DockMpris::poll_refresh()`, which dispatches up to 64 pending bus events and re-reads every player's properties, plus two `getenv` calls, two config snapshots and a string split of the blacklist and preference lists, on every single frame. The dock calls the same function once a second from `dock_handle_timer`. Battery and Bluetooth polling had the same problem. All three now run on the taskbar's 1Hz poll tick, like the dock.
- Per draw tracing was always on. Every frame wrote a `debug_log()` line (mutex, `localtime_r` and `strftime`, two `snprintf` calls, `fwrite` and `fflush`) from two call sites, plus three raw `std::cerr` lines. That is six or more formatted writes per frame on the animation path, and one log grew to 65k lines. It now only runs when `EH_TASKBAR_DRAW_TRACE=1` is set. The `reload:` lines were already throttled to 2 seconds and only fire on real changes, so they are unchanged.
- Added `[taskbar-fps]`, in the same format as the dock's `[dock-fps]` (hz, frames, avg_gap_ms, gap_min_ms and gap_max_ms, reported once per 5 second window). The taskbar had no frame rate reporting at all, which is why a regression this large stayed invisible until the two bars were compared by hand.

### Dock: Turning the dock off in Settings now actually stops it

Turning the dock off used to leave the `horizon-dock` process running. It hid its surfaces and dropped its exclusive zone to 0, but it stayed alive as a Wayland client and kept owning `org.kde.StatusNotifierWatcher`, so the "off" dock was still the session's tray host. Killing it by hand did not help either, because the supervisor's SIGCHLD handler respawned it every time.

`show_dock` now controls the process, not just the surfaces.

- The dock child asks to exit cleanly when the setting changes from true to false (`DockApp::exitRequested`), and the standalone loop unwinds. The check lives in `on_idle_flush`, not only in the display callback, so turning the dock off works even when there is no Wayland traffic.
- The SIGCHLD handler leaves the child down when `show_dock` is false, and the supervisor does not spawn it at all if the setting is already off at login.
- `sync_dock_child_with_enabled_setting()` starts the dock again when the setting changes back to true. It runs from `on_idle_flush`, so it also handles a raw config edit and a child that died while the dock was off. It uses the existing cooldown and `dockRespawnAtMs_` logic, so a crash while the dock is enabled is still respawned at the same limited rate.
- `TrayManager::start_as_client()` is replaced by `start_secondary_host(int waitMs)`. Attaching as a client only is no longer correct: with the dock off there is no watcher at all, so the taskbar's tray would have gone down with it. The taskbar now waits up to 8 seconds for the dock to claim the name, so the primary host keeps it, and then calls `start_watcher()`, which checks ownership again and only takes the name if nobody else holds it. When `show_dock` is false the wait is skipped and the taskbar hosts the registry immediately.
- `start_client()` no longer gives up after a slow first snapshot. The `RegisteredStatusNotifierItems` read is now isolated. Before, when that read threw (seen on a taskbar whose watcher call went past the 2 second limit), a single outer `try` skipped the signal subscriptions and `enterEventLoopAsync()` as well. That left a client with no items and no way to learn about new ones, so the tray stayed empty until the process restarted. Missing the first burst of items is survivable. Not listening at all is not.

### Taskbar and Dock parity: Live tray, working network panel and loop fd self heal

This came out of a parity audit of `horizon-taskbar` against `horizon-dock`, started after the taskbar showed a stale running apps list. It found three more bugs of the same kind: a per process singleton that nothing in that process drove, or a resource that was created once and never recovered.

- The taskbar's system tray was always empty. `TrayManager` is a per process singleton, and `setup_tray_bus()` only subscribed to it. The only `TrayManager::instance().start()` call in the tree was the dock's, so in the taskbar process the manager never ran, `items_` stayed empty and the tray widget had nothing to draw. No icons showed, and the `Activate`, `GetLayout` and `Event` handlers could never fire. The taskbar now starts its own manager on the worker thread.
- New `TrayManager::start_secondary_host(int waitMs)` for a bar that is not the primary tray host. This matters for ownership, not just tidiness. The dock owns `org.kde.StatusNotifierWatcher` and deliberately delays claiming it by `tray_session_defer_delay()`. A taskbar calling plain `start()` right away would win that race and take the name from the primary tray surface. Since the taskbar restarts far more often than the dock, its next exit would release the name and leave the dock attached to a watcher that no longer exists, blanking the tray in both bars until the dock restarted. So it waits for an outside owner first and only claims the name if nobody turns up (see the dock off entry above). The dock's own `start()` path is unchanged. Checked on the live bus: `org.kde.StatusNotifierWatcher` is owned by the `horizon-dock` pid, with 2 registered items (`nm_applet` and `steam`).
- The taskbar never started `NetworkManagerService`. The taskbar's control center owns the VPN popup and the Wi-Fi toggle, and this process is the only reader of that state, since the dock never touches the service. Without a start, the singleton reports an empty, unstarted state, and the only thing that ever started it was the embedded settings app when the Network tab happened to be opened. `start()` is safe to call more than once and does its D-Bus work on its own worker thread.
- Loop fd self heal now matches the dock. The dock reinstalls a lost poll timer or settings watcher every 5 seconds (`dock_install_loop_fds`). The taskbar created both once and never recovered, so losing either silently cost it its repaint timing or its config watcher for the rest of the session. Both are now created through a re-runnable `install_loop_fds()` lambda called from `on_idle_flush` on the same 5 second throttle, and the fd handlers capture by reference so a replacement fd is the one that actually gets read.
- Checked and found fine, no change needed: tray item click and menu handling exists in the taskbar and is bounded by `kTrayMethodCallTimeout`. The battery widget builds its D-Bus connection on its own poll thread rather than in a paint path, and is reference counted in both bars. The disks, live wallpaper and settings embeds all attach their `xdg_surface` and `xdg_toplevel` listeners before the first commit, so no early `configure` burst can be lost. Widget token coverage matches the dock for every token in use.

### Taskbar: Windows that were already open were missing from pinned and running apps

The taskbar only listed apps opened after it started. Every window that was already mapped when the taskbar launched was missing from the running apps widget, and pinned apps showed no running indicator for them, so the bar looked stale. The dock was fine, which made it look like a config or tracker bug instead of a startup ordering one.

- Root cause: `Connection::connect()` binds `zwlr_foreign_toplevel_manager_v1` during its own connect round trips and flushes it, but never installs a listener on that proxy. The taskbar then attached its `ForeignToplevels` listener to that already flushed proxy (`taskbar_standalone.cpp`), after `taskbar_init_on_display` had run more synchronous round trips. The compositor answers every manager bind with one `toplevel` event per already mapped window, exactly once. Those events went to a proxy with no listener and were thrown away. Only windows mapped afterwards were ever seen, because those events are sent live.
- Fix: the taskbar now binds its own `zwlr_foreign_toplevel_manager_v1` and attaches the listener inside the `wl_registry.global` callback (`bind_toplevel_manager_for_tracking`). A registry bind is only sent at the next flush, so the listener is always in place before the first batch can be dispatched. This is the same ordering the dock already uses through `dock_bind_deferred_globals()` and `dock_foreign_toplevel_bind_manager_and_hooks()`, which is why the dock was never affected.
- The same bug is fixed in the shared tracker. `toplevel_tracker_init()` bound the manager in its registry callback but only attached the listener after the round trip, which dropped the initial snapshot for the session and supervisor path. It now attaches at bind time and installs the visual dirty hook before the round trip, so the restored snapshot schedules a redraw.
- Verified against the live compositor with a standalone protocol probe. Binding and attaching after the extra round trips finds 0 existing toplevels, while attaching in the registry callback finds all of them. The taskbar boot line now reports `toplevels=<count>`.

### Dock and Taskbar freeze: Blocking StatusNotifier D-Bus call on the event loop

- Root cause of the "frozen dock": the event loop thread was stuck inside a synchronous session bus round trip. `dock_handle_timer()` and `on_idle_flush` both call `dock_try_start_deferred_tray()` and then `TrayManager::start()`, which makes a burst of blocking D-Bus calls (`NameHasOwner`, `requestName`, a `Get` of `RegisteredStatusNotifierItems`, and a `Get` against every name on the session bus while resolving item owners). With sdbus-c++'s default 25 second timeout, one unresponsive bus or tray app froze the whole dock: no repaint, no input, no config reload, 0% CPU, and file descriptors that looked perfectly healthy in the boot log.
- Tray startup now runs off the loop thread. `dock_start_tray_async()` runs `TrayManager::start()` on a joinable worker thread that is launched once and joined in `dock_cleanup`. Items still show up, because the dock already resyncs on `trayEventFd` through `dock_handle_tray`.
- All tray D-Bus calls are now limited to 2 seconds (`kTrayMethodCallTimeout`). That covers the tray manager's own bus, the dock and taskbar `trayBus` used by the remaining synchronous calls on the loop thread (`Activate` on icon click, `GetLayout` and `Event` for tray menus), and the disks manager's synchronous `GetManagedObjects`.

### Settings Keyboard and Language: Working layout switching, reordering and system locale

- Layout switching now actually switches. Clicking a source row, using the layout dropdown, or adding a layout jumps the running compositor to that layout straight away on Hyprland (`hyprctl switchxkblayout` on the main keyboard, parsed from `hyprctl -j devices`) and Sway (`swaymsg xkb_switch_layout`). Before, it only rewrote the config and left you to press the toggle shortcut yourself.
- Fixed the broken shortcut table. "Super+Space" mapped to an empty XKB option, which did nothing. It now maps to `grp:win_space_toggle`. Sway updates also gained `xkb_numlock` and no longer go through a shell.
- Sources can be reordered with up and down chevrons on each row. Removing the active layout still falls back sensibly.
- The add layout list now comes from `localectl list-x11-keymap-layouts` (cached, with a hardcoded fallback), and unknown codes get readable names, instead of a fixed list of 28 entries.
- Fixed the repeat slider drag drifting. Paint, pointer down and pointer motion now share one `repeat_slider_track_geom()`. Motion used a hardcoded 240px sidebar instead of the real 260px, and a track that was 32px narrower.
- Removed toggles that did nothing. "Show layout indicator" and "Input method" had no backend anywhere (nothing read them), so the layout card shrinks from 5 rows to 3 and toggle hit areas now match what is painted. Middle click paste is now wired through to Hyprland's `misc.middle_click_paste`.
- New Language card: installed locales (`localectl list-locales`) with a badge on the current one, staged apply through `pkexec localectl set-locale` (checked against the installed list), a readout of the login screen keymap with a one click `pkexec localectl set-x11-keymap` from the enabled layouts, and a status line. Per window switching scopes and input method management have no compositor backend and were left out on purpose.
- From the screenshot round: repeat sliders now show their value all the time (the m3 popup only appeared mid drag), in a fixed 76px area that keeps the drag track stable.

### Settings Time: Real system timezone, manual clock and scroll fixes

- New "Set as System Timezone" action. The chosen override can now be written with `pkexec timedatectl set-timezone` (checked against the zone database). The card shows the real system timezone (cached and refreshed on a tick) next to the desktop only override, with a status line.
- Manual clock editor when NTP is off: date and time fields with strict validation (YYYY-MM-DD and HH:MM[:SS], leap years included), Enter to apply through `pkexec timedatectl set-time`, UTF-8 safe editing and Tab to switch fields.
- The NTP query moved off the paint path (it refreshes on a tick and from the Refresh button). The timezone list scrolls with the mouse wheel, Change and Reset hover states respect the scroll offset, and Backspace in the custom format field is UTF-8 safe.
- Scroll clamp for tab 30 (paint selector and wheel upper bound) so the taller panel cannot overscroll. Weekday and date locale display options would need changes to the clock widget and were left out.
- From the screenshot round: the Refresh button moved to the left of the NTP toggle (it was painting on top of it), and the preview card lost 28px of dead space.

### Settings Sound: Unified combos, over amplification and speaker test

- Combo box hit areas now match what is painted. Closed combos are a fixed `kSettingsComboW` wide everywhere. Long names are cut short inside the box, with the full text in the popup, whose paint and hit areas already agreed. Before, painting widened the box by Pango measurement while hit testing used a `strlen*8` guess.
- Over amplification: a new "Allow volume above 100%" toggle (saved as `audio.allow_over_amplification`) raises all four sliders to 150%. PipeWire already supports this. Drag, throttle and final commit scale the track value to match.
- Speaker test: "Test Speakers" plays front left, then front right (`pw-play --target=`), using a double fork so the UI never blocks, with a test signal as fallback.
- The device card list limit went from 6 to 12. Engine cells hit test against the same cached values paint shows, so there are no more cells that are clickable but dead, or dead but painted. When no device exists you now see a "No output/input device available." placeholder instead of sliders vanishing. Balance, meters, alert volume and port selection have no PipeWire service API and were left out.
- From the screenshot round: combos size to their text through one shared paint and hit geometry function (proven identical with a Pango determinism harness), so long device names widen the box instead of being cut to "..." while clicks still land exactly. All sliders show their percentage in a reserved area at the end of the track.

### Settings Startup: Working typing, honest menus and icons

- Typing in forms works at all now. The key handler compared the Wayland key state against `0` (released), while the dispatcher only forwards pressed and repeated events, so every keystroke was dropped. It now accepts both, with repeats blocked on one shot keys, and Backspace is UTF-8 safe.
- Clicking a row no longer hijacks the kebab menu (the row only highlights). A click outside the popup closes the menu and also reaches the row underneath. The popup flips above the row when it would overflow the clipped list, using shared paint and hit geometry.
- The second menu action now says what it really does: Delete (user entries), Reset to System (system entries that are overridden), or Disable and Enable (plain system entries). It is checked against `set_autostart_enabled` results and shows status messages. Row and default app toggles report failures instead of assuming success, and editing keeps the disabled state instead of quietly enabling the entry again.
- Entries show icons (resolved like the app browser, with a glyph fallback). The Exec line in subtitles is width limited, the delay field is validated with a form error (0 to 999), and the last row of the browser stays reachable at every scroll offset.

### Settings Accounts: Full multi user panel

The Accounts tab now manages every local user, not just the current one, and long standing layout and wiring bugs are fixed.

- Users card (`settings_tab_accounts.cpp`, new `accounts_users.{hpp,cpp}`): the list comes from `getpwent` (UID 1000 and up, current user first) with You, Admin and Locked badges. You can select a user, toggle Administrator (`usermod` and `gpasswd`, and the last admin is protected), toggle Account Locked (`passwd -l` and `-u`, locking yourself is refused, state is queried lazily), edit the full name (`usermod -c`), and delete a user in two steps (`userdel -r`, deleting yourself is refused). All privileged calls go through pkexec with a direct fork and exec (no shell) and strict checks on usernames, hostnames and full names.
- Automatic Login card: detects GDM, SDDM and LightDM with per user enable and disable, and shows a clear "no supported login manager" state otherwise.
- Set Password card: now targets the selected account and has a strength meter. The create user form gains an Administrator toggle, and each card has its own success and error line instead of one shared message.
- Layout: the name, `@user` and button block is cut off in its own column and can no longer cover the avatar. Narrow cards stack vertically, every card reserves a status strip, and scrolling is clamped to the real content height (tab 46 added to the paint scroll selector), with scroll to focus when a field is focused.
- Keyboard: Enter submits the focused form, Delete works, key repeat works, input is capped at 256 characters, and Tab order follows the visual order.
- Avatar: JPEG and extensionless `~/.face` files now decode (stb fallback), there is a per user avatar cache, and avatars can be installed into other users' homes with `pkexec install -o user -g gid`. The disk probe for a missing image no longer runs on every repaint. Fields scroll sideways instead of silently cutting text off at 50 characters.

### Overview: Empty workspaces show the desktop, and tiles always win

Empty workspace cards used to render blank, or took the wrong monitor's screenshot. They now fall back to the shell's own wallpaper image, cover fitted to the primary output's aspect ratio exactly like the wallpaper layer paints it, so an empty workspace card shows just the desktop until a window opens on it. The image is decoded once and cached (disk and memory). The old fallback that matched a background workspace to its monitor's live screenshot is gone, because it could bake another workspace's windows into an empty card.

- Desktop fallback from the wallpaper (`overview_host.cpp` and `.hpp`): `build_wallpaper_capture()` decodes `config.wallpaperImage` through the existing wallpaper thumbnail pipeline, cover fits it to the primary output's aspect ratio (center crop and uniform scale, matching `WallpaperRenderer` mode 0) and produces a normal `WorkspaceCapture`. `capture_workspaces()` assigns that image to empty workspaces when no monitor was showing an empty workspace at capture time. Active empty workspaces still use their own monitor's real screencopy. If the wallpaper is disabled or cannot be decoded (a live or video wallpaper, for example) it falls back to the earlier transparent render.
- Tiles always paint over a capture (`overview_painter.{cpp,hpp}`): a workspace capture is only ever the backdrop. It stays behind the window tiles for workspaces that have windows and fills the card alone for empty ones. Before, a fresh capture could hide the very windows it showed, so apps looked like they had slipped behind the desktop image. The `liveMode` parameter and its per frame scan for streaming windows were removed as dead logic.

### Dock: Pinned drag reorder uses paint geometry and glides

Dragging pinned apps to reorder them in the dock felt "wonky the longer the dock gets". It now works from what paint actually drew, and slots glide to their new position instead of jumping.

- Geometry snapshot at press. The drag captures the left edge, slot pitch and icon width of the pinned run from the retained hit areas when you press (validated like `dock_pick_retained`). The computed path is only a fallback, now built on the picker's shared strip geometry (`DockStripGeom`, `dock_compute_strip_geom`, `dock_slot_layout_x`). Pitch is measured from real pinned slot positions instead of assuming icon plus gap. With `dockPinnedAppsTrayPill` the painted pitch is the icon only, so the old error grew by gap times index (about 130px at 15 pins). The fallback also uses paint's inner margin (`dock_strip_inner_margin`, 12 times the UI scale, was 8 times), the full slot width walk (Battery and WorldClock included), and handles the side by side (lr) layout.
- `dockRunningAppsTrayPill` gap rule in paint. All three of paint's `gap_after_*` walks were missing the zero gap rule the picker already had, which shifted the strip origin against pick and painted the running apps pill too narrow.
- The floating pin follows the cursor. The dragged icon was drawn inside the horizontal compression transform using surface pointer coordinates, so it lagged the cursor by `(pointerX - midX) * (1 - hScale)`. It is now drawn after the transform and clamped in surface space.
- Glide for the slot shuffle. When the insert index flips, per key fractional slot offsets are taken from the animation in flight, rebased onto the new layout, then animated to 0 over about 160ms (`EaseOutCubic` through `shellAnim`, and reduced motion settles immediately). Neighbouring pins slide instead of jumping. The pinned pill stays anchored to the target layout while cells move across it, and hit areas follow the target layout so they stay stable mid animation. The layer only draw fast path is turned off while a glide runs, so all dock layers stay in step.
- The first commit has hysteresis. `pinDragInsertIdx` starts at the pin's real filtered index instead of `-1`, so the first move cannot commit unopposed.
- No tooltip or hover churn mid drag. Hover state, the hover lift animation and tooltips are suppressed while a pin is pressed or dragged. `dock_tooltip_tick`'s `create()` does a blocking `wl_display_roundtrip` that made the drag stutter.
- Removed dead code: the unused `centers` vector in `dock_pinned_pointer_motion` and the unused `pinnedXs` pre pass in paint.

### Taskbar: Layout, popup toggle close and breathing room

Taskbar widgets no longer hug the ends of the bar, the center section is truly centered, popups close when you click their own button again, and the bar grows to keep space around its widgets.

- Padded three section layout in every width mode. Left widgets start `strip_pad_px` (16 times the UI scale) inside the bar's rounded end, right widgets end the same distance from the other end, and the center section is centered on the bar (and so on the screen). Before, the panel anchored flush to the bar edges and fill mode laid everything out as one centered strip, so the start button and settings widget sat against the edges and the center widgets drifted off center by `(left - right) / 2`. Fill mode now sizes its pill to `center + 2 * (pad + max(left, right) + section_gap)` so the three zones always fit. Sections that still collide fall back to the centered strip, which keeps the padding and compresses horizontally around it. Panel fit checks account for the padding, and hit areas follow paint as before.
- One gap rule everywhere. The render advance (`gap_local`) quietly packed pinned apps together, while the width walkers (`gap_after_slots` and `gap_after_local`) applied `iconSpacing` when `pinnedAppsTrayPill` was off. Widths and painted extents disagreed and shifted every section away from its measured position, most visibly as the center group sitting left of center. The three lambdas now share one `gap_between`, and `taskbar_measure_sections()` (which replaces `taskbar_measure_content_width`) follows the same rules plus paint's slot widths (widget keys, live MPRIS snapshot, WorldClock), so fill mode sizing cannot drift from paint.
- Toggle close for every popup widget. A click on the bar dismisses the open popup and remembers its kind, and landing on the owning widget now simply returns instead of opening it again. Before, the dismiss ran and the widget's own handler immediately created the popup again, so weather, calendar, media player, control center, volume mixer, VPN, battery and the app drawer could only be closed by clicking somewhere else. Transport buttons on the media widget (previous, play, next) still work while its popup is open.
- Control Center popup sized to the UI scale. It was created at a raw 620 by 640 while its cards, gaps and margins were laid out at `dock_ui_scale()`, which clipped content sideways and vertically on scaled setups. It now uses `kControlCenterPopupW()` and `kControlCenterPopupH()` like the dock.
- Popup clearance includes the edge margin. Clearance was `floating + height + 6` and left out `edgeGap`, so every popup sank into the bar by the bar's screen edge margin. `popup_bottom_clearance_px` now mirrors the layer margins exactly (edge gap, floating inset and painted height, mirrored for a top anchored bar).
- The press overlay covers the whole slot. It was icon sized and lit up only the left half of wide widgets (weather, media, clock). It now uses `slotW + 4`, and the press scale is centered on the slot instead of on its first icon width.
- Bar height guarantees breathing room. `effective_height_px` grows the bar to `iconSize * scale + 16 * ui` when the configured height is smaller, so widgets keep at least 8 times the UI scale in pixels of space on each side instead of touching the bar edges. The adjusted height feeds everything that uses it (layer size, exclusive zone, paint clamp, auto hide, tooltips, popup clearance), the reload compare notices a global UI scale change, and the workspaces pick path no longer scales the bar height twice against paint.

## [0.9.5 beta 1] - 2026-08-05

### Power Confirm: Glassy restyle and live countdown fixes

The power confirm dialog (dock and taskbar) got a new glass look, and its countdown and hover behavior were fixed.

- Restyled on the M3 glass pipeline. The card, the close, cancel and confirm buttons and the countdown progress bar all render through `m3::Box` glass surfaces (`glassy_box`) with rounded corners, a card outline and `m3::StateLayer` hover overlays on every button. The confirm button has a danger tint and the progress bar an accent fill. Card geometry is clamped with `std::max(0.0, ...)`, so layout degrades gracefully instead of going negative on unusually small outputs.
- The countdown froze in the dock. The frame done handler returned early for PowerConfirm, so the 60 second timer drew once and never ticked. `dock_popup_queue_followup_frame` now asks for a follow up frame while the confirm is open, and the handler redraws every 250ms (or immediately when hover motion is pending). Between ticks it rearms the frame callback and commits, so the countdown runs live and executes automatically at 0 seconds.
- The taskbar had no countdown loop. Its popup had no frame callback at all. It gained `popupFrameCb`, `popupLastDrawMs`, `popupMotionDirty` and a `taskbar_popup_frame_done` listener that follows the dock's timing. `taskbar_popup_draw` arms the callback, and the close paths destroy it.
- Hover lag and wrong hit positions at speed. The dock redrew the popup synchronously on every pointer motion event, which flooded the Vulkan present pipeline and left the compositor showing stale frames. PowerConfirm motion now only sets `popupMotionDirty` and queues a single follow up frame, and redraws are merged in the frame done handler (at most once per frame). The taskbar got the same merged motion path in `taskbar_pointer_motion`. Hover state now tracks the pointer at vsync rate, and click hit testing matches what is on screen even during fast movement.
- Stale enter coordinates. `shell_pointer_enter` stored only the surface and kept `pointerX` and `pointerY` from the last motion event, so entering the popup hit tested with old coordinates until the first motion arrived. Enter now records the surface local position and triggers an immediate redraw.
- Clicks could be swallowed. The confirm and cancel pick was checked after the app menu branches, so while the app menu was open the dialog's own buttons lost clicks. The PowerConfirm branch moved to the top of `app_drawer_handle_click`, so the dialog always wins while it is open.
- The taskbar hit test used the wrong size. `taskbar_handle_popup_click` picked against the raw surface size, but the popup is configured as a (near zero) fullscreen grab surface. It now uses `popupConfiguredW` and `popupConfiguredH` (the laid out dialog size), so clicks match the rendered buttons. The grab surface itself is now configured as 0 by 0.
- The dock popup was pushed off center. `dock_popup_sync_layer_margins_if_open` reapplies the pill position margin math to the popup layer on every draw, but the power confirm popup is a fullscreen grab surface that must keep zero margins. With a centered dock strip narrower than the output it set `marginLeft` to `(outputW - dockW) / 2` plus a pill clearance `marginBottom`, shrinking the surface from the left and bottom so the card sat right of the screen's center. The sync now skips the fullscreen grab popup kinds.
- Diagnostics: setting `EH_POWER_CONFIRM_TRACE=1` prints card geometry and pointer position from `pick()` and `paint()` to stderr.

### IPC fabric and service split (Phases 1 to 6)

The desktop shell now runs as a supervisor with separate child services that talk to each other over a persistent IPC bus.

- New framed IPC protocol (`src/services/ipc/`). Messages start with a magic byte and a length (`kFrameMagic=0xE7`, version 1, 1 MiB maximum body) and support passing file descriptors (SCM_RIGHTS), publish and subscribe topics, and request and response correlation ids. The old one shot plain text protocol used by `eh-ipc` is still served on the same socket. The server peeks at the first byte: `0xE7` means a framed client, anything else is legacy.
- `IpcClient` connects with retry and backoff and supports a blocking `request()`, `subscribe()` and `unsubscribe()` that wait for an ack, `publish()` with fd passing, and poll friendly `on_fd_ready()` with event handler dispatch.
- `IpcService` is a non blocking, poll based server with a per client out queue, subscription broadcast, fd attachment through `sendmsg` and CMSG, thread safe `publish()`, and `poll_interests()` so it plugs into the shell's main poll loop.
- `horizon-settings` child process. Settings was pulled out into a standalone binary (`settings_standalone_main.cpp`, with `run_standalone()` in `settings_embed_lifecycle`). It has a pidfile singleton guard, `spawn_settings()` through `posix_spawnp`, subscriptions to `config.applied` (reload and redraw) and `command.request` (`settings.toggle`), and a `settings_launcher` hook that replaces the old in process embed. Taskbar settings clicks now go through `request_launch_settings()`, and the `.desktop` Exec line changed to `horizon-settings`.
- `horizon-wallpaper` child process. The new `wallpaper_standalone` owns its own `WaylandConnection` and `WallpaperRenderer`, applies the wallpaper on BACKGROUND layers, reapplies it on the `config.applied` broadcast, registers a pidfile, and is spawned and reaped by the shell (`wallpaper_spawn_native_child()`, stale child cleanup, SIGTERM then SIGKILL escalation). `WallpaperRenderer::init` is simpler because it creates its own connection.
- `horizon-notifications` child process. The new `notifications_standalone` bundles `NotificationManager`, `NotificationToastHost` and the D-Bus notification service, subscribes to `config.applied` and `notify.push`, and runs a display, timer and IPC poll loop.
- `horizon-desktop` child process. The desktop icon layer, widgets and menus moved out of the supervisor into a `horizon-desktop` binary (`desktop_standalone`). It owns its own `WaylandConnection`, a 1 second tick and a 33ms animation timer, and a `DockMpris` listener with `EH_MPRIS_NOTIFY=0` (the dock stays the single now playing emitter). It subscribes to `config.applied` and is spawned and terminated by the shell. The desktop module no longer borrows the dock: output targeting, the dock reserve (`dock_effective_bar_height_px`), menu and UI scale, and catalog icons are read from the shared config snapshot, with its own `IconCache`. Pin toggling uses new settings only helpers (`dock_settings_pinned_toggle` and `dock_settings_is_app_pinned`) that save to the dock settings TOML, which the dock hot reloads through inotify. The supervisor now routes SIGCHLD through a signalfd to reap children and respawn `horizon-desktop` after a crash (limited to once every 5 seconds).
- Notification IPC contract (`notifications_ipc.hpp`). Topics are `notify.push` and `notify.changed`, the field separator is `0x1e`, and message kinds are `internal`, `mpris` and `mpris-art` (album art scaled down to 160px at most and inlined as a length prefixed image blob). MPRIS now pushes through `eh::notify::push_mpris` instead of the in process manager, and client `publish()` lets the shell broadcast client events to subscribers.
- Per service Wayland connections (Phase 5). Gamma, idle, lockscreen and polkit each got their own isolated `WaylandConnection` (`gamma_conn_`, `idle_conn_`, `lock_conn_`, `polkit_conn_`), so a protocol error only disables that one service. `LockScreen` is now self contained, with its own `wl_keyboard`, xkb state and `sync_outputs()`, and the old `lock_seat_dispatch` and `polkit_seat_dispatch` forwarding layers are gone. Polkit keys arrive already decoded as `(state, sym, utf8)`, and the auth dialog sizes its surface to a fixed card of `kCardW=460` by `kCardH=240`.
- Toplevel window service (Phase 6). The new `src/services/windows/` holds `ToplevelService`, which snapshots windows with stable ids and publishes `toplevel.created`, `toplevel.updated` and `toplevel.closed`. `ToplevelClient` mirrors the bus for split components, and `ToplevelBridge` feeds the wlr foreign toplevel tracker into the bus (token based snapshot callbacks were added to `foreign_toplevels.cpp` and `ext_foreign_toplevels.cpp`).
- Config broadcast. `config.applied` is published with the combined config modification time after every apply, and the wallpaper, settings and notifications children reload from it.
- Tests. `test/test_contract_ipc.cpp` covers protocol encode and decode, the legacy shim, request and response, publish and subscribe, SCM_RIGHTS fd passing, and the full toplevel codec, service and client round trip, including id stability when a window closes. It is registered as the meson test `ipc_fabric`. `eh_shell_core_lib` changed from static to shared, so the supervisor and children share one copy.
- Extras: the crash handler now prints a `backtrace_symbols_fd` backtrace, `EventHorizon --version` prints the version, and the night light config is reapplied through a shared `apply_nightlight_config()` helper.
- Parent death guarantee. Shell spawned children (`horizon-desktop`, `horizon-wallpaper`, `horizon-notifications`, `horizon-settings`) now set `PR_SET_PDEATHSIG` through `services/process/parent_death_guard.hpp`. The supervisor marks each spawn with an `EH_SUPERVISOR_PID` environment variable, and the child asks the kernel for SIGTERM when its parent exits, so `pkill -9 EventHorizon` reaps every child too (PDEATHSIG fires even on SIGKILL). A `getppid()` check closes the spawn race. Standalone launches, such as the settings `.desktop` file, carry no marker and are unaffected. `desktop_spawn.cpp` also checks `/proc/<pid>/stat` comm and exe before signaling, so a pidfile pid reused by an unrelated process is never killed.

### `horizon-dock` child process (Phase 3, item 4)

The dock bar moved out of the supervisor into a `horizon-dock` child, done "dock first with seams". The supervisor keeps the taskbar, toplevel tracking, gamma and Super+S. The child owns the dock surfaces, launchpad, overview, start menu, tray, settings watcher, MPRIS and the Super key menu toggle.

- The child (`src/desktop_shell/dock/standalone/` and `standalone_main.cpp`) has its own `WaylandConnection` and `DockApp`, its own timer, tray, settings inotify and MPRIS loop fds, a weather engine, a pidfile, the parent death guard, and the global keyboard Super toggle (`dock_toggle_menu_from_keyboard`, start menu first). It has no settings function, because the supervisor keeps Super+S and that way the two evdev handlers never both fire. Its IPC client subscribes to `config.applied` (drag preview sync, reload, then schedule a frame) and `command.request` (`settings.toggle`, `menu.toggle`, `launchpad.toggle`, `overview.toggle`, `overview.open` and `overview.close`).
- Spawning and reaping (`dock_spawn.{hpp,cpp}`): `posix_spawnp`, sibling binary lookup, stdio sent to `horizon-dock.log`, stale pidfile check with a `/proc/<pid>` comm and exe check, and SIGTERM then SIGKILL then reap. SIGCHLD reaping and a respawn limited to once every 5 seconds live in `build_handlers()`, and the child is killed on shutdown. It mirrors `horizon-desktop`.
- Session rewiring. The in process dock, `launchpad_host_`, `overview_host_`, the dock loop fd handlers, `mpris_bus_fd_`, `dock_deferred_init_done_` and the launchpad and overview `PollMuxDisplay` registrations were deleted. The IPC handlers `launchpad`, `overview-*` and `menu-toggle` became `command.request` forwards. The `config.applied` hook is now taskbar redraw, embed redraw, night light and broadcast, and the drag preview paint tick only draws the taskbar.
- `ToplevelTracker` (`src/desktop_shell/shared/toplevel/`) is a new shared struct that binds wlr foreign toplevel v3 (plus ext foreign toplevel list on Hyprland) on the session's main display. The taskbar borrows `toplevels`, `extToplevels` and `compositorKind` from it (its own secondary connection cannot track toplevels), and `ToplevelBridge` publishes from the tracker.
- Own settings watcher. The supervisor opens its own inotify (`open_state_inotify()` and `drain_inotify`) to reload keyboard, night light and taskbar settings on raw file edits. There is no `config.applied` broadcast there, which keeps the optimization that skips matugen on inotify. The child reloads itself independently.
- Global keyboard. The supervisor installs only the settings function (Super+S sends `settings.toggle` or `spawn_settings`), and Super on its own is handled entirely by the child. Gamma stays supervisor owned, since a second gamma client in the child would fight the first.
- Taskbar Launchpad paint seam. `Host::paint_dock_button` was extracted into a shared, paint only `eh::shell::launchpad::paint_launchpad_dock_button` (pure cairo with a process local icon cache). `Host` delegates to it, its icon members and destructor cleanup were removed, the taskbar paints its Launchpad slot through the helper, and `taskbar_.launchpadHost` was deleted.
- Known behavior change: the old in process Super handler fell back to a taskbar only start menu. The child now toggles only the dock's start menu and app drawer. Taskbar start menu users should use the taskbar button or `eh-ipc menu-toggle`.
- Build: the `horizon-dock` executable, plus `dock_spawn.cpp`, `dock_standalone.cpp` and `toplevel_tracker.cpp` in `eh_shell_core`. Verified with a debug and release build, `meson test` at 2 of 2 passing, a clean smoke test without a compositor, and a live session pass that found no bugs.

### `horizon-taskbar` child process (Phase 3, item 4)

The taskbar moved out of the supervisor into a `horizon-taskbar` child, which completes the Phase 3 item 4 split. The supervisor keeps the toplevel tracker and `ToplevelBridge`, gamma, lockscreen, idle, polkit and Super+S. The child owns the taskbar surfaces, the app drawer and its popups, the tray, weather, MPRIS, the settings watcher, and its own toplevel tracking.

- The child (`src/desktop_shell/taskbar/standalone/` and `standalone_main.cpp`) has its own `WaylandConnection` and `TaskbarApp`, its own 1 second poll timer, tray, MPRIS and settings inotify loop fds, a weather engine, a pidfile and the parent death guard. Its IPC client subscribes to `config.applied` (reload, `taskbar_maybe_reload_settings`, schedule a frame, sync the night light paint state) and `command.request` (`settings.toggle` and `taskbar.menu.toggle`).
- Own toplevel tracking. The child binds its own `zwlr_foreign_toplevel_manager_v1` (plus Hyprland's `ext_foreign_toplevel_list_v1`) on its own connection, the same pattern already verified in a real session for `horizon-dock`. The running apps snapshot is therefore live without the supervisor's `ToplevelClient` bus mirror. `ToplevelClient` stays available for non bar components. If a live session ever shows stale windows (workspace handle invalidation on secondary connections), the fallback is `ToplevelClient` plus a `window.activate`, `window.minimize` and `window.close` command bus resolved by `ToplevelBridge`.
- Night light over the bus. The app drawer's night light button publishes `command.request` `nightlight.toggle`, because the child has no gamma client. A new supervisor bus self client (`ipc_self_client_`, needed since the server never receives its own publishes) flips the single supervisor owned gamma client (a fixed 4000K on enable, same as the old in process toggle). Paint state resyncs from the `config.applied` broadcast. The hook is `taskbar_set_nightlight_toggle_fn` in `taskbar.{cpp,hpp}`.
- New `taskbar.menu.toggle` command. It is separate from the dock child's `menu.toggle`, so a single Super key press or command cannot toggle both bars' start menus. The supervisor's `menu-toggle` IPC handler forwards `taskbar.menu.toggle` only.
- Spawning and reaping (`taskbar_spawn.{hpp,cpp}`) work like the dock's: `posix_spawnp`, sibling binary lookup, stdio sent to `horizon-taskbar.log`, stale pidfile and `/proc/<pid>` checks, SIGTERM then SIGKILL then reap, plus SIGCHLD reaping with a 5 second limited respawn in `build_handlers()` and a kill on shutdown.
- Session rewiring. The in process taskbar is gone: the `TaskbarApp` member, `taskbar_deferred_init_done_`, `taskbar_poll_timer_fd_`, `mpris_event_fd_`, the taskbar's Hyprland runtime fd, `rebuild_taskbar_layers`, `create_taskbar_layers`, `restart_taskbar` and the taskbar `PollMuxDisplay` registration were all removed. The `config.applied` hook is now embed redraw, night light and broadcast, and the drag preview paint tick was removed because both halves now live in the children. `sync_widget_registry` reads the taskbar widget lists from the config snapshot instead of the in process app.
- Own settings watcher. The child opens its own inotify (`open_state_inotify()` and `drain_inotify`) and reloads raw file edits with `shell_config_snapshot_skip_matugen()`, matching the dock and desktop children.
- Known behavior changes: a taskbar Wayland protocol error now terminates and respawns the child (limited to once every 5 seconds) instead of restarting in process, and `menu-toggle` no longer touches the dock's menu.
- Build: the `horizon-taskbar` executable, plus `taskbar_spawn.cpp` and `taskbar_standalone.cpp` in `eh_shell_core`. Debug and release builds are clean and `meson test` passes 2 of 2. The live session taskbar check is still pending (the toplevel tracking risk from §0.11 decision 1).

### Overview: Full rewrite (Parts 1 to 3)

- A new scrollable workspace card overview replaces the stale one shot thumbnail grid. `overview.md` documents the design research, a client side clone of the hyprland-scroll-overview plugin. A plugin port and a hybrid approach were explicitly rejected.
- New data model: `OverviewWindow` (address, appId, title, monitor relative geometry, floating, focused and special flags), `OverviewWorkspace` (id, label, monitor, capture), `OverviewLayout` (axis, `scale=0.5`, card width and height, pitch, close button, app grid), `OverviewColors` and `OverviewCardRect`. `overview_host.hpp` and `overview_painter.hpp` were reorganized around them.
- Rendering: a zoom out open effect (cards shrink from full monitor size), a stable tint per appId for placeholder tiles (FNV-1a hash to hue), radial gradient and grid line procedural backgrounds, window tiles with an icon and title pill, a focused accent border, hover lift, a drop target ring, a close button, a workspace label pill, and a topmost drag ghost at 0.9 alpha.
- Interaction: wheel and trackpad scrolling with a per frame exponential chase (`kScrollEaseRatePerSec=32`, settles at 0.25px, no overshoot), click to switch workspace, click a window to activate it, close buttons, and real drag and drop (8px threshold) through `dispatch movetoworkspace <id>,address:<addr>`. Compositors other than Hyprland fall back to activating the target workspace and then the window.
- Hyprland IPC data refresh (`refresh_from_hyprland()`) uses `j/workspaces`, `j/clients` and `j/activeworkspace`, with a dock fallback that uses the workspace strip poll and foreign toplevels.
- Part 2, screencopy workspaces. The new `capture_workspaces()` does a one shot batched screencopy of every ready output before the layer surface is created (so the capture never contains the overview), matched to workspaces by monitor name. Real BGRA covers the card in "Screencopy" mode (window tiles are skipped). High depth integer formats now clamp directly to 8 bit instead of going through the HDR tone map path. Captures survive the 2 second periodic refresh.
- Part 3, live view. The new `protocols/hyprland-toplevel-export-v1.xml` (Hyprland's window export protocol, v2) and `ToplevelStream` (`src/wl/capture/toplevel_stream.{cpp,hpp}`) stream live window frames: one in flight per window, chained on `ready`, downscaled to 1024px, BGRA, with `ignore_damage`. When "Live updates" is on, the overview shows live window contents instead of static tiles, aspect fitted with the tinted placeholder filling the letterbox space.
- Drag improvements: the grab offset is preserved at drag start, there is edge auto scroll (64px zone, 1.6 pitches per second), and activation is deferred until after the layer surface is destroyed, so Hyprland cannot undo focus on unmap.
- Launcher settings tab M3 rewrite (`launcher_tab_m3.hpp`) with three child tabs: Launcher, Workspaces and Overview. Launcher has sliders for launchpad scale, icon fill, cell gap, folder size, grid columns, rows, slot pill opacity and DPI. Workspaces has max slots (0 to 16) and max icons. Overview has card scale, card gap, scroll delay, close button size and search width, plus Capture mode (Snapshot or Screencopy) and Scroll axis (Vertical or Horizontal) chips. "Workspaces" no longer has its own sidebar entry, since it was folded into Launcher.
- New config fields, all saved in `settings.toml`: `overview_axis`, `overview_capture_mode`, `overview_card_scale_pct`, `overview_card_gap_px`, `overview_scroll_delay_ms`, `overview_close_btn_size_px`, `overview_search_width_px` and `overview_live_updates`.
- Build: the vendored material-color-utilities static library now builds against the absl flat hash map with a forced include PCH (`pch_material_colors.hpp`), which fixes a missing `<optional>` include.

### Horizon Colors: Native in process color engine

- New native M3 color pipeline (`src/color/horizon_colors.{cpp,hpp}`). It decodes the wallpaper image, quantizes it (Celebi: Wu then WSmeans), scores the colors, goes through CAM16 and HCT, and builds a full M3 scheme, all in C++, replacing the external matugen subprocess. It vendors Google's `material-color-utilities` as a git submodule (built into the static library `eh_material_colors`, about 30 upstream `.cc` files), with optional lcms2 and libjpeg support.
- Palette caching (`horizon_colors_palette.cpp`) has three tiers: an in memory cache keyed by path, mtime, scheme and mode, then a v3 disk cache under `$XDG_STATE_HOME`, then full generation. Results are published directly into the `hc*` fields of `ShellAppearance`, with `[settings-bench]` timing logs.
- Native templates (`horizon_colors_templates.cpp`) mirror matugen's template output in process, gated on `horizonColorsNative && horizonColorsPaletteOk`. They cover 47 M3 role mappings, a btop theme rewrite plus `pkill -SIGUSR2 btop`, otter terminal color sync, and a VS Code `settings.json` merge that sets "Dynamic Base16 EventShell Dark/Light".
- External template sync. New matugen configs and templates for `horizon-files` and `horizon-photo` (`.conf` to TOML float RGB patching), gated on the `matugen_templates.horizon_files` and `matugen_templates.horizon_photo` toggles.
- Matugen parity work. The ICC transform is disabled (matugen ignores ICC profiles). `SchemeVibrant` is built inline with a neutral variant chroma of 10 (the bundled MCU uses 12). A bit exact 112 by 112 Triangle resize (a reproduction of the Rust `image` crate filter, with a `volatile` FMA guard) replaces stbir. A near grayscale filter drops colors with chroma below 5 before scoring.
- zune-jpeg port (`third_party/zune-jpeg/zune_jpeg.hpp`). A header only C++ port of the Rust zune-jpeg 0.5.12 scalar decoder (baseline SOF0, SOF1 and SOF2, Huffman, fixed point IDCT, YCbCr to RGB) for byte identical seed extraction compared with matugen. It is staged together with the `jpeg_decoder.hpp` wrapper for a later commit.
- Text color rule change: text is now always forced to on white or off white by theme mode and is never derived from the palette. Matugen and the native engine are mutually exclusive, and "Color Engine" is now an explicit third color source (see Color Themes below).
- Tests: `test_horizon_colors.cpp` covers decode, quantize, score and scheme, and runs as the meson test `horizon_colors_engine`. `stb_impl.cpp` is a single translation unit.
- Research docs: `Horizon-colors.md` (a full reference on matugen and material-color-utilities internals) and `matugen.md` (porting notes).

### System Theming module

- All OS theming moved out of `icon_cache.cpp` (1459 lines fewer) into a dedicated `src/desktop_shell/common/system_theming/` module (`eh::theming`). It has four parts:
  - GTK (`gtk_theming.cpp`): applies settings through gsettings and `settings.ini`, extracts `@define-color` values with libadwaita and legacy names, falls back to a CSS selector scan for colors and design metrics, and memoizes results.
  - Qt (`qt_theming.cpp`): applies the qt6ct and qt5ct style and color scheme to the conf files and kdeglobals, scans style plugins, and builds preview palettes.
  - Plasma (`plasma_theming.cpp`): applies the theme to plasmarc and kdeglobals, copying `[Colors:*]` sections.
  - Cursor (`cursor_theming.cpp`): handles Xcursor themes through a dynamic `dlopen("libXcursor.so.1")` with alias mapping.
- `icon_cache.cpp` now only handles icons. The shared `run_cmd_capture`, `read_ini_kv`, `update_ini_key` and `icon_base_dirs()` helpers moved to `system_theming_utils.cpp`.
- Desktop entry resolution fallback index (`desktop_entries.cpp`). A lazily built index over all XDG `applications/` directories covers `X-Flatpak-RenamedFrom` legacy ids and the trailing segment of reverse DNS names (for example `resolve` finds `com.blackmagicdesign.resolve`). It is consulted as a final fallback in `find_desktop_file_for_appid()`.
- Pins now store canonical ids. Dock and taskbar pin toggles resolve to the canonical `.desktop` name (for example `sh.cider.Cider`) instead of compositor dependent WM_CLASS ids, through a shared `dock_pin_store_id` helper.

### Control Center: Rebuild and per widget modules

- The monolith was split. `control_center_paint.cpp` shrank from about 2,400 lines to about 200, and per domain widget modules now live in `controlcenter/widgets/`: audio, Bluetooth, mixer, network, and a large weather widget. The weather widget has a full async Open-Meteo engine (curl-multi driven by an eventfd, hand parsed JSON, geocoding through geocoding-api.open-meteo.com, IP based auto location using three providers with 24 hour and 5 minute caching, a 32 entry LRU cache with a 10 minute TTL, and a background service thread that runs every 10 minutes).
- Audio device persistence. `control_center_persist.{cpp,hpp}` saves the selected sink and source to `$XDG_STATE_HOME/ControlCenter/controlcenter.toml` (written through an atomic temp file rename). The saved defaults are reapplied as the pending selection every time the control center opens, with a 900ms ignore window so the PipeWire default change does not overwrite the highlight.
- Wi-Fi connect flow. `control_center_wifi_connect()` drains `pending_secret_request()`, queues the secret, and calls `activate_ap()` with real error messages ("Password required", "Network not found", "Failed to start connection").
- Shared paint utilities in `control_center_paint_utils.hpp`: glass card and status pill painters, UTF-8 aware truncation, and the `kSurfR`, `kSurfG` and `kSurfB` glass fill and stroke constants.
- Panel and popup geometry. Output and input device panels are now full width rows stacked below the cards (not overlapped through `max`). Icon badges are 28 by 28 rounded squares, the mixer's collapsed pill gets a dashed accent affordance, and the media card uses an accent to dim gradient with a filled play and pause button.
- Modal state refactor. Control center geometry, dispatch and hit testing were rebuilt around a `ControlCenterActiveModal` state instead of per panel booleans. `control_center_anim.hpp` is slimmed down to just `cc_ease01`, `popup_open_control_center` went from 10 arguments to 3, and the dock and taskbar control center integration was rebuilt on the shared helpers.

### Hyprland Settings: UX redo

- New track based layout engine (`hyprland_m3_layout.hpp`). `HyprlandCardLayout` lays section cards out in a centered column 68% wide, with toggle rows (`kHyprToggleRowH=60`) before slider rows (`kHyprSliderRowH=68`), a section header zone of `kHyprSectionHeaderH=52` and a card gap of 12. Helpers such as `beginGroup`, `endGroup`, `skipSection`, `rowTop` and `sectionCardH` keep paint and hit geometry in lockstep. Shared `hyprland_paint_slider_card`, `hyprland_paint_toggle_card` and `hyprland_paint_anim_card` (with a three dot animation menu) replaced about 650 lines of duplicated per file lambdas across all 13 M3 files.
- Every sub tab was restructured with titles and subtitles:
  - Decoration has 8 cards: Rounding and Opacity, Blur, Blur Advanced, Dim, Shadow, Glow, Motion Blur and Misc.
  - Input has Keyboard, Mouse, Touchpad, Gestures and Window Behavior.
  - General has Window and Gaps, Snap, Tearing, Border Behavior and Layout.
  - Animations has a master toggle and one card per category.
  - Cursor, dwindle, master, misc, binds, group, quirks, scrolling and xwayland were restructured the same way.
- Child tab bar: tabs are 108px wide with an 8px gap, glassy, with the selected tab in the accent color. The layout dropdown is now a real card row (`hyprlandLayoutComboX` and `hyprlandLayoutComboY`, 140px wide) with its own click to open and select handler.
- Lua parser rewrite (`settings_hyprland_data.cpp`). Dotted key parsing now uses a section stack instead of the brace depth state machine, `write_nested_entries` is recursive for arbitrary nesting, and `escape_lua` returns bare true, false and numbers while quoting everything else.
- Fix commit `e84cea7`: `accel_profile` is now a string (`"adaptive"`, `"flat"` or `"custom"`, checked on read, written through `escape_lua`, and shown as a named value pill slider) instead of an int. `initial_workspace_token_timeout` defaults to 10 and `render_unfocused_fps` to 15, both clamped to at least 1 because Hyprland rejects 0. Both are exposed as new sliders in the "VRR and Wallpaper and Timing" card (ranges 1 to 240 and 1 to 120).
- Earlier expansion (from `6ababb6`): four new child tabs, Group (8 toggles), XWayland, OpenGL and Quirks (15 in total), plus a scrollable child tab bar that centers on click. Expanded M3 controls include snap monitor gap, border grab area, resize corner, cursor zoom and warp, ecosystem toggles, glow and motion blur decoration, touchpad tap to click and flip, and more.

### Hyprland Layout Settings: 0.56 alignment

The Scrolling, Master and Dwindle layout tabs were audited again against the official Hyprland 0.56.x wiki and the v0.56.2 source (`src/config/values/ConfigValues.cpp` and `src/layout/algorithm/tiled/{scrolling,master,dwindle}/*`). Any option missing from the 0.56.2 source (confirmed with a full tree grep) was treated as removed and dropped from the data structs, the Lua read and write paths, and the UI. Missing options were added as clean toggle cards or labeled value sliders, consistent with the other tabs.

- Scrolling (`hyprland_scrolling_m3.hpp`):
  - Added a `follow_min_visible` slider (0 to 100%, default 0.4) for how much of a column must stay visible while scrolling.
  - Added `wrap_focus` and `wrap_swapcol` toggles (both default true) for focus and column swap wraparound.
  - Added `direction` as a labeled value slider (left, right, down, up).
  - Removed the invalid `no_gaps_when_only` (confirmed absent from the 0.56.2 scrolling source) from the struct, `read_scrolling()`, `write_scrolling()` and the UI.
- Master (`hyprland_master_m3.hpp`):
  - Removed the invalid `lock_fullscreen` and `no_gaps_when_only` (both absent from the 0.56.2 source).
  - Added toggles `smart_resizing` (resize direction follows the cursor), `drop_at_cursor` (drop dragged windows at the cursor position) and `always_keep_position` (keep the master position when there are no slaves).
  - Added labeled sliders `special_scale_factor` (10 to 100%), `new_status` (master, slave or inherit), `new_on_active` (before, after or none), `orientation` (left, right, top, bottom or center) and `center_master_fallback` (left, right, top or bottom). `new_status` and `orientation` already existed in the data struct but were never shown in the UI.
  - `new_on_active` changed from a bool to a string, because 0.56 turned it into a `before`, `after` or `none` enum. The `new_on_top` default was corrected to `false` to match the source.
- Dwindle (`hyprland_dwindle_m3.hpp`):
  - Removed the invalid `no_gaps_when_only`, `no_tiling_border` and `use_monoscope`. The last one was a dead widget that existed in state but was never painted.
  - Added toggles `permanent_direction_override` (keep the preselect direction until changed) and `precise_mouse_move` (drop moved windows at the exact cursor point).
  - Added a `split_bias` labeled slider (directional or current). `force_split` now shows named values (follow mouse, left, right). The `split_width_multiplier` range widened to 10 to 300% and `default_split_ratio` to 10 to 190%, matching the wiki ranges (0.1 to 3 and 0.1 to 1.9).
  - Fixed a hidden slider index bug. The pointer handler's slider arrays were ordered differently from the paint order, so dragging "Special scale factor" would have changed the split width instead. All slider arrays now match the paint order.
- Verified with a clean `ninja -C build-release-verify horizon-settings` build. The invalid master and dwindle options (`lock_fullscreen`, `no_gaps_when_only`, `no_tiling_border` and `use_monoscope`) are never read or written anymore.

### Hyprland Settings: 0.56 alignment (remaining tabs)

The General, Misc, Cursor, Render, OpenGL, Quirks, Group, Decoration, Input and Animations tabs were audited again against the Hyprland v0.56.2 source (`src/config/values/ConfigValues.cpp`). Defaults, option presence, types and value ranges were corrected to match, and several UI cards now expose options that previously only existed in the data struct or the Lua write path.

- General: the `extend_border_grab_area` slider range widened to 0 to 100.
- Misc:
  - `force_default_wallpaper` changed from bool to int (default `-1` for the default, plus none, wallpaper 1 and wallpaper 2).
  - `initial_workspace_tracking` changed from bool to int (disable, workspace, or monitor and workspace).
  - New `middle_click_paste` toggle.
  - Default corrections for `mouse_move_enables_dpms`, `allow_session_lock_restore`, `session_lock_blur`, `screencopy_force_8b`, `name_vk_after_proc`, `always_follow_on_dnd`, `layers_hog_keyboard_focus` and `enable_anr_dialog`. `on_focus_under_fullscreen` now defaults to 2, `lockdead_screen_delay` to 1000 and `anr_missed_pings` to 5.
- Cursor: `no_hardware_cursors`, `no_break_fs_vrr` and `use_cpu_buffer` changed from bool to three state sliders (disable, enable, auto, all defaulting to 2). `zoom_detached_camera`, `hide_on_touch` and `hide_on_tablet` are now toggles in the UI. The write path emits the int valued cursor options instead of bools.
- Render: `fp16_sdr_tf` default changed from 2 to 0 and its range was narrowed to monitor or linear. `use_fp16` default changed from 0 to 2 (a disable, enable or auto slider). New `use_shader_blur_blend` toggle.
- OpenGL: new `nvidia_anti_flicker` toggle (default true), written on the Lua write path.
- Quirks: `prefer_hdr` changed from bool to an int slider (disable, prefer HDR, always HDR, default 0) and is now written with `std::to_string`. `skip_non_kms_dmabuf_formats` is exposed as a toggle (verified present in the 0.56.2 source).
- Group: `drag_into_group` changed from bool to an int slider (disabled, enabled, groupbar only), and the `focus_removed_window` default was corrected to true.
- Decoration:
  - Corrected ranges from the source: `rounding` 0 to 100, `dim_special` 0 to 100%, `blur:size` 0 to 100, `blur:passes` 0 to 10, `shadow:range` 0 to 100, `glow:range` 0 to 100, `shadow:scale` 0 to 100% and `motion_blur:samples` 1 to 64.
  - New `motion_blur:enabled` toggle card, and `dim_inactive` moved from the Motion Blur card to the Dim card.
  - New `vibrancy_darkness` and `popups_ignorealpha` sliders (both were read and written before but never shown).
  - `screen_shader` is now only written when enabled and not empty.
- Input:
  - New Touch Device card (`transform` slider and `enabled` toggle).
  - New Virtual Keyboard card (`share_states` labeled slider from 0 to 2, and a `release_pressed_on_close` toggle).
  - New Tablet card (`transform` slider, plus `absolute_region_position`, `relative_input` and `left_handed` toggles).
  - New Tablet Tool card (`eraser_button_mode`, `eraser_button_override`, `pressure_range_min` and `pressure_range_max` sliders).
  - All of these are backed by new structs. The `scroll_button` and `follow_mouse_shrink` sliders and the `scroll_button_lock` toggle were dead (present in state and in the flush switch but never painted) and are now shown.
- Animations: new `workspace_wraparound` toggle in the Animation Master card.
- Touchpad: the `tap_to_click` and `tap_and_drag` keys were verified correct on the Lua write path. `luaConfigValueName` in 0.56 maps `-` to `_` and `:` to `.`, so `touchpad.tap_to_click` and `touchpad.tap_and_drag` resolve to Hyprland's `input:touchpad:tap-to-click` and `tap-and-drag`.
- The Lua roundtrip was verified for the new nested device tables (`touchdevice.*`, `virtualkeyboard.*`, `tablet.*` and `tablettool.*`) and for an empty `tap_button_map` string, with a standalone write then read test. A clean `ninja -C build-release-verify horizon-settings` build confirmed it.

### Settings cleanup and UX polish

- `ScrollController` (`settings_scroll_controller.hpp`) is now the single place where scroll animation and clamping live: frame rate independent exponential easing (`kTauMs=40`), clamping when content shrinks, snap and scroll by, and `animating()`. It is wired through `settings_scroll.{cpp,hpp}` (`settings_scroll_px()`), the per tab clamp paths, the event handlers and the widget picker (a scrollable, clipped grid with a scrollbar).
- New `SettingsDropdown` component (`settings_dropdown.{cpp,hpp}`). It is a self contained combobox that owns its labels, selection, hover and open state, with `hit_trigger`, `hit_row`, `popup_geom` (flips above the anchor on overflow), `paint_trigger` and `paint_popup`. It was adopted first by the appearance tab's Matugen scheme and mode combos.
- Widget picker rewrite. It is now an M3 glass panel (radius 16, alpha 0.96) with a panel width measured from the text, a uniform card height from the longest wrapped description (clamped to 1 to 3 lines), a scrollable clipped grid, a search caret synced to frames (530ms blink through `wl_surface.frame`), a clear filter chip, accent hover cards with an "add" chip, a "No matching widgets" empty state, a scrollbar thumb and a footer hint.
- Header removal. The old 56px "Settings" title strip is gone (`kContentTop` went from 76 to 16), and window buttons now paint last, on top.
- New opacity controls: a per surface "Notifications" overlay slider (new `overlayOpacityNotifications` and `OverlaySurfaceAlphaKind::Notifications`) and a dedicated "Settings sidebar" slider (`overlayOpacitySettingsSidebar`, TOML key `overlay_opacity_settings_sidebar`) that scales only the sidebar backdrop. The `overlay_title` and `overlay_value` grid grew to 16 entries.
- Notification toast fixes: duplicate cards are suppressed through a seen set keyed on summary, `0x1e` and body, toast opacity follows the new slider, `tick_animations()` has a stall watchdog (frame callbacks slower than 250ms), and `maybe_emit_now_playing_notify_assume_locked` is deduplicated.
- Media compact widget. The card, art and buttons are now glassy boxes with the backdrop tied to the "Widget cards" overlay slider.
- Scroll migration everywhere. All hyprland, mango, dock and taskbar tabs use `settings_scroll_px(app)` instead of raw per tab ints, and the autostart tab's hit tests and overlays are now scroll aware.

### Glass Card restyle

- New shared `glass_card_style.hpp` with `paint_glass_card()`, a frosted recipe: chrome color times 0.35 at 0.78 alpha through a glassy `m3::Box`, plus a 1px white border at alpha 0.12, inset by 0.5px.
- Applied across the whole desktop environment (commit `be6e946`, 42 files): control center popup, dock bar, taskbar, tooltips, notification toasts, context menus, spotlight, desktop icon menus, polkit auth dialog, the calendar, media, weather, world clock and system monitor widgets, the battery, Bluetooth, VPN, weather, media and volume popups, the app drawer, the start menu, settings cards and sidebar, and the hyprland child tab bar.
- Dock pills are unified on `slot_pill_style::paint_pill`, and dock and taskbar widget pill colors changed to `dockFill * 0.6 + accent * 0.15`.

### VPN: Import and UX overhaul

- Import now really works. Importing `.conf` and `.ovpn` files creates actual NetworkManager profiles (before, it only parsed locally and filled in fields). WireGuard files whose names are not valid interface names (for example `AirVPN_NL-Alblasserdam_…-Entry3.conf`) import through a sanitized temporary name and are renamed back afterwards. Imported profiles get `connection.autoconnect no`. All add and import work runs synchronously (about 100 to 300ms) and real nmcli errors come back through `std::string& err`. Shell quoting (`sh_quote`) is handled, temporary configs are private (mode 0600 under `$XDG_RUNTIME_DIR`), and missing plugins show an "Install the "<pkg>" package" hint.
- The Add VPN dialog was rewritten (`vpn_add_dialog.cpp`) with declarative forms per type for WireGuard, OpenVPN, IKEv2, L2TP/IPsec, PPTP and AnyConnect. It has masked secret fields (bullets), plugin availability detection, per type import buttons, a dialog that sizes itself to the active form and recenters, Tab to cycle fields, Enter to submit, and status and error messages.
- In process X25519 key generation (TweetNaCl style field arithmetic) replaces the `wg genkey` and `openssl rand` subprocesses.
- UUID based matching. The VPN list used to match rows by index, which mixed up identities when a connection changed its active state (the refresh sorts by active). Rows now match by UUID, with the optimistic toggle state kept until confirmed or 8 seconds pass. Disconnecting also clears autoconnect, so NetworkManager will not dial again. This applies to both the network tab and the dock VPN popup.
- Auto connect toggle. `VpnConnectionInfo.autoconnect` is parsed from `nmcli -f AUTOCONNECT`, there is a new `setVpnAutoconnect()` service method and a per row auto connect `m3::Toggle` in the network tab, and `deactivateVpnConnection()` now turns autoconnect off after a manual disconnect.
- Long imported profile names are shortened with a UTF-8 safe "…" ellipsis.

### Dock and Taskbar

- Exclusive zone gaps. New `dockExclusiveZoneGap` and `taskbarExclusiveZoneGap` (TOML key `exclusive_zone_gap`, clamped to 0 to 100) add extra space above or below the bars so tiled and maximized windows sit clear of them. The taskbar also gets `taskbarEdgeGap` (`edge_gap`, 0 to 25), applied in every width mode (it used to be 0 in full mode). They appear as new sliders in the dock (10 to 12 items) and taskbar (8 to 10) appearance tabs.
- Running apps tray pill. New `dockRunningAppsTrayPill` and `taskbarRunningAppsTrayPill` settings paint one pill shaped background behind consecutive running app icons, mirroring the pinned tray pill, with matching gap and hit test logic.
- Liquid glass rim. The dock bar gains a seven layer liquid glass edge (frosted base, vertical shading, a white specular top edge, an inner bevel, side highlights, a tinted bottom lip and a faint outer glow). It is tinted by the accent color when `dockColoredGlass` is on, and the border changes color whenever any palette source is active (`anyPaletteActive()`).
- Shell input renamed and moved. `dock_input.cpp` became `src/desktop_shell/shared/system/input/shell_input.cpp` (`g_shell_seat_listener`), documenting itself as the shell wide seat listener.
- Added chrome color `debug_log` traces for dock and taskbar paint.

### World Clock widget (new)

- Dock slot (`world_clock_paint.{cpp,hpp}`). Analog day and night faces (a light dial from 7 to 19 hours, dark otherwise) with 60 ticks, hour numerals and an orange `#ff9f0a` second hand. Cities come from the widget settings `city_N_tz`, `city_N_label` and `city_N_color` (with a Local or UTC fallback). Time comes from a background `timeapi.io` fetch (refreshed every 30 minutes) with a `TZ` plus `localtime_r` fallback. It shows offset labels ("Same Time", "+16HRS") and day labels ("Today", "Tomorrow", "Yesterday", "+N Days"), and redraws on a per second tick signature.
- Desktop widget variant (`desktop_widget_world_clock.{cpp,hpp}`). It reads the TOML in `$XDG_STATE_HOME/event-horizon/WorldClock` (defaults: LA, Tokyo and Sydney) and shows analog faces with day and offset labels.
- Registered as `DesktopWidgetType::WorldClock` and `SlotKind::WorldClock`, with a picker entry, dock and taskbar layout support, and a dock timer redraw.

#### Settings popups (city editor)

- Settings city editor (`world_clock_popup.{cpp,hpp}`). A searchable timezone picker popup for the World Clock cities. It has a text filter with a 530ms frame synced caret and a scrollable list (one row per wheel notch, opens at scroll 0, dropdown content only dismisses on a click on blank space inside it). The picked timezone gives the city label from the last path component (`_` becomes a space), and Save writes `city_N_tz` and `city_N_label` to `~/.local/state/event-horizon/WorldClock`. Changes apply instantly: `horizon-desktop` reloads the widget's cities on every paint (30Hz animation timer), so edits land within about 33ms.
- Desktop widget duplicate (`desktop_world_clock_settings.{cpp,hpp}`). The same editor is mirrored into `horizon-desktop`, subscribing to an IPC topic `worldclock.settings`. Nothing publishes to it yet, so the desktop side entry point is currently unreachable.
- Interaction fixes:
  - A negative control id guard (before, it silently killed the Save, X, search and clear buttons, so they did nothing).
  - The dropdown no longer closes on press (clicking the search field caused a phantom close, because the dialog recenters as the list collapses).
  - The list opens at the top instead of mid list.
  - The Done button was renamed to Save, with centered text and a footer gap.

### Color Themes

- Three way color source picker. The mode pills are now Matugen, Color Engine and Custom Theme, sized to their label width (100 to 170px) with hover highlighting. They drive the exclusive `matugenThemingEnabled`, `horizonColorsNative` and `colorThemeEnabled` flags, and preset cards only show for Custom Theme. Hover is tracked in real time through the new `settings_color_themes_consume_pointer_move()`.

### Themes, Icons and vendored GTK theme

- adw-gtk3 is vendored under `third_party/adw-gtk3/` (light and dark metathemes, GTK3 and GTK4 CSS including a 3055 line `libadwaita.css`, and about 64 symbolic, slider and text select assets) and installed to `${datadir}/themes` through `install_subdir`. Custom `#settingsDialog` tab styling was added to match the shell's settings dialog.
- Settings label alignment fixes: card captions moved from `+15` to `+22` across the time, Bluetooth, keyboard, themes, appearance, desktop, icons and notifications tabs.
- `NewDesign.html`, an "Orbit Control" control center design prototype, was committed as a reference.

### Build, CI and Docs

- `build.sh` was rewritten (358 lines down to 73). Distro detection is limited to apt, dnf and pacman with pinned dependency lists, and it then runs `exec sudo just install`, leaving all meson and ninja work to the justfile (a new `test` recipe was added).
- `Docs/Debian-Ubuntu.md` dependencies were synced (`libaudit-dev`, `libpng-dev`, `liblz4-dev`, `zlib1g-dev`, `libdrm-dev` and `cmake`), and `Docs/Fedora.md` was updated.
- CI now runs `meson test -C build-debug --print-errorlogs`.
- Fixes for builds without RSVG: unused parameter suppressions in `asset_loader.cpp` and `settings_tab_wifi.cpp` (the RSVG guarded icon path).
- Added `matugen/configs` and templates for horizon-files and horizon-photo, and the `third_party/material-color-utilities` submodule.

## [0.9.2] - 2026-07-01

### Settings: Coordinate and hit testing fixes

- Double scroll in the M3 dock dispatch. Removed a redundant `+ scroll` from `handlePointerDown`, `handlePointerUp` and `handlePointerMove` in `settings_tab_dock.cpp`. Callers already pass a document space Y through `settings_logical_content_y_tab01()`, so adding the scroll again corrupted all hit testing while the page was scrolled.
- Double scroll in the M3 taskbar dispatch. The same fix was applied to the taskbar's three dispatch functions in `settings_tab_taskbar.cpp`.
- Qt Color Scheme hover mismatch. `on_pointer_motion()` compared `pyL` (document space Y) against `ly` (screen space Y) for the Qt scheme dropdown hover row, so highlights followed the wrong row. It now uses `app.pointerY` (screen space Y), matching the click handler.
- M3 widget drag ghost position. The drag ghost's `widgetDragGrabDy` was computed from `app.pointerY` (screen space) but painted inside a cairo context already translated by `-scroll`. It now uses `py` (document space Y, already computed) in `dock_tab_m3.hpp` and `taskbar_tab_m3.hpp`.
- Scroll wheel ignored the content area. Tabs 0, 1 and 11 handled wheel events wherever the pointer was, so content scrolled when you hovered over the nav buttons or padding. They now use the same `app.pointerX < tcx || app.pointerY < kContentTop` guard as every other tab.
- Matugen scheme and mode dropdown hit position. The click handler in `settings_event_handlers.cpp` used the wrong `matugenRowTop` (`kContentTop + kSpacingL + 46` instead of `kContentTop + kDockChildTabH + 12`), which shifted hit detection by about 16px.

### Settings: Additional fixes

- Font installation. Added `install_data` for the Inter and Fira Code fonts (8 files) to `meson.build`, so the bundled fonts are installed to `$datadir/event-horizon/assets/fonts/`.

### Audio Visualizer: Smooth Catmull-Rom waveform

- From flat bars to an organic waveform. The static rendering was replaced with a 48 point Catmull-Rom spline (24 cava bands mirrored left and right), which gives smooth, continuous wave curves with a spiky response to transients.
- Asymmetric smoothing. A fast attack (20ms tau) snaps to beats instantly, and a slower decay (60ms tau) falls away gracefully. Each band is smoothed on its own.
- Visual layers: a vertical gradient fill below the wave (opacity 0.20, then 0.05, then 0.0), a four pass glow on the wave crest (0.06 per pass), and peak marker dots that linger and decay at 0.5 per second.
- Idle breathing. A gentle sine pulse plays while playback is paused, which keeps the widget feeling alive.
- Wave height was raised from 22% to 30% to 48% to 58% of the card height for more impact.
- The Hann window was removed, so spikes stay visible from edge to edge across the whole waveform.

### Cava subprocess: Inline config and higher resolution

- Fixed a config pipe bug. Cava's inline config was written to a pipe fd that was already closed (the write end was closed before writing). The config is now written from the parent process after the fork, so cava really receives its `noise_reduction`, `monstercat` and `gravity` settings.
- Bars went from 6 to 12 to 24 bands, for the most frequency resolution.
- Smoothing is set aggressively: `noise_reduction=0`, `integral=50`, `gravity=100`, `monstercat=2.0`, `sensitivity=50` and `framerate=60`.

### Desktop widget animation layer

- A 30fps paint timer. A second timerfd (33ms interval) now runs next to the existing 1 second timer. The 1 second timer handles `on_second_tick()` (MPRIS polling and create calls), while the 33ms timer calls `paint_all_layers()` and `wl_display_flush()` for smooth animation.
- `tick_cava()` only runs during playback, and the idle state produces the breathing animation.

### Configurable Cava: Live TOML reload

- Config file. `$XDG_STATE_HOME/event-horizon/cava/cava.toml` exposes every cava parameter (bars, framerate, sensitivity, cutoffs, smoothing) and every visualizer parameter (wave height, attack and decay tau, fill, glow and peak opacities, idle amplitude, blob tau and opacity).
- Live reload. The config file's modification time is checked about once a second on the existing timer. Changes to cava related fields restart the cava subprocess, while visualizer only changes apply instantly.
- A default config file with every field documented is created at that path.

### Build

- Added `cava_config.cpp` to the meson source list.
- Removed the stale `todo/` directory.

### Taskbar: Pinned app drag reorder fixes

- Reorder silently did nothing in panel layout mode. `taskbar_pin_drag_motion()` used `firstLeft == 0.0` as an "uninitialized geometry" marker, but `startX` (and so `firstLeft`) is legitimately `0.0` whenever the taskbar renders in panel layout mode (`taskbar_paint.cpp:601`). That silently aborted every motion event, so `pinDragInsertIdx` never left `-1` and nothing was ever committed. It was replaced with an explicit `pinDragGeometryValid` flag, computed once at drag start from the actual pinned hit count (in `taskbar_pin_drag.cpp` and `taskbar.hpp`).
- The new order was reverted before it reached disk. `taskbar_draw()` always starts with `taskbar_maybe_reload_settings()`, which does `app.settings = next` whenever `app.settings.pinnedApps` differs from the global config snapshot. The release handler drew before saving, so the freshly committed order sat in `app.settings` for one draw while the global snapshot was still old, and the reload logic overwrote it with the old order before `taskbar_save_settings()` ever ran. `taskbar_pointer_button()` in `taskbar.cpp` now saves first (which refreshes the global snapshot) and draws afterwards.
- Intermittent frozen drag preview. `taskbar_schedule_frame()` asked for a `wl_surface_frame` callback without checking that the target surface was actually configured, unlike the dock's equivalent (`dock_pick_frame_surface_ready`). On some compositors `wl_callback::done` then never fired, which permanently stalled all further redraws for the rest of the drag even though the reorder math kept running underneath. Added `taskbar_pick_frame_surface_ready()` and a 100ms stall watchdog (`frameCallbackRequestedMs`) that drops a stuck callback so scheduling can recover (in `taskbar.cpp` and `taskbar.hpp`).
- Slow drag performance. Every pointer motion event during a pin drag called `taskbar_draw()` synchronously, forcing an unthrottled full repaint per event. The dock already throttles its pin drag redraws to vsync through `dock_schedule_frame`. The taskbar had the equivalent (`taskbar_schedule_frame` and `frameRedrawPending`), but nothing used it. `taskbar_pin_drag_motion()` in `taskbar_pin_drag.cpp` now schedules a merged frame instead of drawing synchronously.

## [alpha 3.2] - 2026-07-01

### Settings: Coordinate and hit testing fixes

- Double scroll in the M3 dock dispatch: removed the redundant `+ scroll` from all three dispatch functions in `settings_tab_dock.cpp`.
- Double scroll in the M3 taskbar dispatch: the same fix in `settings_tab_taskbar.cpp`.
- Qt Color Scheme hover mismatch: changed `pyL` to `app.pointerY` in `settings_event_handlers.cpp`, so hover row detection matches screen space coordinates.
- M3 widget drag ghost position: fixed drag ghost tracking in `dock_tab_m3.hpp` and `taskbar_tab_m3.hpp` by using `py` (document space Y) instead of `app.pointerY`.
- Scroll wheel content area check: added a content column bounds guard for tabs 0, 1 and 11 in `settings_scroll.cpp`.
- Matugen scheme and mode dropdown hit position: corrected the `matugenRowTop` offset in `settings_event_handlers.cpp`.

### Settings: Additional fixes

- Font installation. The bundled Inter and Fira Code fonts are now properly installed through `meson.build`.

## [0.9.1] - 2026-06-17

### Color Themes system

- Built in preset library. 279 presets, a mix of hand curated, seed generated and mass produced. They cover Catppuccin (Latte, Frappé, Macchiato and Mocha plus accent variants), Google Material (Light and Dark), Gruvbox (Dark and Light), Rosé Pine (Pine, Moon and Dawn), Google Brand (Dark and Light), Dracula (Dark and Alucard), Nord (Dark and Light), Tokyo Night (Dark and Storm), Monokai (Dark, Soda, Vivid, Pro and Night), Solarized (Dark and Light), Everforest, Kanagawa, Ayu (Mirage), Atom, Night Owl, Sonokai, Andromeda, and 266 Ptyxis terminal palettes converted to M3.
- Category grouping. Presets are organized by source family (catppuccin, google, gruvbox, rose-pine, dracula and so on), with dark and light sub sections inside each group.
- Theme application. `apply_theme_to_app()` applies the M3 palette (primary, secondary, tertiary, error, surface and outline) from any built in or user preset.
- User themes. JSON theme files are scanned from `~/.config/event-horizon/themes/` through `scan_user_themes()` and `load_theme_from_file()`.
- Theme persistence. `colorThemeEnabled`, `colorThemeName`, `colorThemeSource` and all 16 palette floats are saved in the settings JSON.
- Matugen integration. Color themes live next to matugen theming, and you switch between "Matugen" and "Custom Theme" with the mode pills.

### Settings "opens blank" bug fixes

Four separate root causes were found and fixed:

1. Deferred draw with no retry. When both SHM buffers were busy and no animation was running, `pendingRedraw` was set but no `wl_surface.frame` callback was scheduled, which left the window blank for up to 30 seconds. The deferred path now always schedules a frame callback (`settings_app.cpp:709`).
2. Pending redraw skipped after a toggle. `embed_after_display_dispatch()` returned early after handling `pendingToggle`, so it skipped the `pendingRedraw` check (`settings_embed_lifecycle.cpp:711`).
3. Stale chrome config on reopen. `destroy_settings_surfaces()` did not reset `settings_paint_shell_snapshot_valid`, so the first draw after reopening reused an old `ShellConfig` (`settings_embed_lifecycle.cpp:138`).
4. Transient Vulkan failure. When `settings_present_vk_raster()` failed once, `draw()` returned without committing any buffer. It now copies the rendered content from the staging buffer to SHM and commits right away (`settings_app.cpp:1597`).

### Additional fixes

- Desktop crash with Rs3. Fixed an entry point crash when Rs3 (a Rust based compositor) is detected, by adding null safety to the compositor detection path and a guard in session initialization.
- Settings scroll improvements: smooth scroll convergence, per tab scroll state clamping, and a fix for scroll pixel overflow when switching tabs.
- Vulkan swapchain. Fixed surface loss handling in `vulkan_wayland.cpp`, with proper swapchain recreation on output hotplug.
- Dock bar. Improved the pinned app slot calculation for edge to edge dock mode.
- Taskbar. Added window state diffing, so only dirty regions are repainted.
- Session lock. Fixed a lock surface activation race by deferring the surface commit until the frame callback.
- Shell config. Added the color theme config fields (`colorThemeEnabled`, `colorThemeName`, `colorThemeSource` and 16 palette hex values).
- Settings common. Added `settings_group_header()` for categorized section headers in settings tabs.

### Version

- Bumped from 0.9.0 to 0.9.1.

## [0.9.0] - 2026-06-13

### Horizon File Manager: Wayland native file browser

- A full file browser with these features:
  - Grid and list views, a path bar with clickable segments, and a sidebar with favorites, drives and bookmarks.
  - Drag and drop file operations, and recursive search that runs in a worker thread.
  - Thumbnail previews for images, video (ffmpegthumbnailer, built through cmake), PDF (poppler-glib), SVG (nanosvg) and EPUB.
- Navigation: a directory tree that collapses and expands, a breadcrumb path bar, back and forward history, bookmark management and favorite folders.
- Computer view: mounted drives, filesystem roots and network locations in collapsible sidebar sections.
- File operations: copy, move, rename and delete with confirmation dialogs, and compression and extraction through libarchive.
- A progress dialog with cancel support for long running operations.
- Drive mounting through the UDisks2 D-Bus service, for automatic mounting and unmounting of removable media.
- Media preview: video frame extraction through ffmpegthumbnailer built from source, and an image gallery preview with zoom.
- Search: recursive filename search in a background worker thread, with results streaming in.
- Context menu: open, copy, cut, paste, rename, delete, properties, and compress and extract.
- Inline rename overlay with validation.
- Settings persistence: show hidden files, default sort column and direction, and view mode are saved across sessions in a JSON config.
- Embedded terminal: opens a terminal in the current directory.
- Packaged as a standalone `horizon-files` Meson project, with an archive viewer subdirectory.

### Horizon Shot: Wayland screenshot and annotation tool

- Capture modes: region selection, active window and full screen (through the wlr-screencopy and ext-data-control-v1 protocols).
- DRM and KMS capture: direct display capture through DRM for outputs where KMS is available.
- Image formats: PNG, JPEG, JXL (HDR) and 16 bit PNG, with HDR and SDR tone mapping.
- Annotation overlay after capture: draw, highlight, arrow, rectangle, ellipse and text tools.
- Quick editing before saving: crop, resize and color adjustments (brightness, contrast and saturation).
- Clipboard integration: the captured image is copied to the Wayland clipboard automatically.
- A standalone desktop entry (`horizon-shot.desktop`) with launcher integration.
- Packaged as a standalone `horizon-shot` Meson project.

### Sub module packaging and CI

- Both projects were added as proper git submodules under `packages/`, tracking the `Stable` branch.
- GitHub Actions: a full pipeline that builds debug and release, installs to staging (`staging-shot-*` and `staging-files-*`), extracts the version from each sub project's own `meson.build`, and packages for Arch (pacman and zstd), Debian (deb) and RPM, each with separate debug symbol packages.
- CircleCI: the same multi stage pipeline, with workspace persistence, version files and per format packaging jobs.
- The root `config.yml` mirrors the CircleCI pipeline for redundancy.
- `PKGBUILD` was converted to an Arch split package that produces `event-horizon-de`, `horizon-shot` and `horizon-files` from a single PKGBUILD.
- Auto update: every CI build fetches the tip of the `Stable` branch with `git fetch origin Stable` and `git checkout origin/Stable`.
- Nested submodules: horizon-files' own submodules (tomlplusplus, nanosvg, stb and ffmpegthumbnailer) are initialized recursively during CI.
- Dependencies: added `cmake` to the build containers for the ffmpegthumbnailer build, and resolved include path issues for the nested third party code.

### Settings toggle accent color fix

- Replaced every hardcoded emerald green toggle accent (`r=0.396, g=0.792, b=0.412`) with the matugen resolved accent color, across 22 files:
  - Power tab: `power_toggle_emerald()` now uses matugen `a_r`, `a_g` and `a_b` instead of constants.
  - Appearance tab: removed the file scope `eaR_`, `eaG_` and `eaB_` hardcodes. Both paint functions now read `accR`, `accG` and `accB` from `settings_resolve_colors()`.
  - Hyprland tab (13 m3 files): all toggle and switch components use the `accentR_`, `accentG_` and `accentB_` member variables, filled in through `syncColours(app)` at paint time.
  - Network tab (5 m3 files): the wired IPv4, IPv6, Ethernet, Security and Advanced toggles now use the resolved accent.
  - Taskbar tab (`taskbar_tab_m3.hpp`): 7 toggle rows updated.
  - Dock tab (`dock_tab_m3.hpp`): 5 toggle rows updated.

### Live Wallpaper

- Gallery system. A new `gallery.cpp` and `gallery.hpp` module provides `LiveWallpaperVerticalMetrics`, `LiveWallpaperTabLayout`, `live_wallpaper_vertical_metrics()`, `live_wallpaper_tab_layout()`, `live_wallpaper_destroy_thumbs()`, `live_wallpaper_clamp_page()` and `live_wallpaper_ensure_wallpaper_gallery()`. Folder stats are debounced by modification time (500ms) to avoid pointless rescans.
- Thumbnail management. LRU eviction, a separate blurred thumbnail cache for 18+ marked content, a disk backed blurred thumbnail cache (`lw_load_cached_blurred_thumb` and `lw_save_blurred_thumb_cache`), and video frame extraction for the hover preview.
- Performance. The folder is only rescanned when its modification time changes, thumbnails are cached ahead of time through `WallpaperThumbnailService`, and the hero surface is invalidated lazily.
- Sort modes: Name, Oldest and Newest, with the sort state tracked in `liveWallpaperGallerySortMode`.
- Settings serialization. UI preferences (opacity, scale, columns, rows, thumbnail radius and sort mode) are saved and restored through `lw_save_settings()` and `lw_save_ui_config()`.
- Adult content marking. A right click context menu toggles an 18+ flag, with a blurred preview (box blur radius 100) and a disk cache for the blurred thumbnails.
- Gallery hit testing. Per thumbnail click detection matches the grid math from `live_wallpaper_tab_layout()`, so both left click selection and the right click context menu work (before, this was stubbed and always selected the first item).
- Gallery navigation. Previous and Next page buttons now have hit testing with page clamping (also stubbed before).
- Hover effects: a scale animation (1.0 to 1.10 and back to 1.0) with easing curves, an accent highlight overlay, and video hover preview frames that advance with timing.
- Video wallpaper: `live_wallpaper_apply_video_with_matugen()`, `live_wallpaper_start_hover_video_preview()` and frame by frame playback in the hover popup.
- Scroll support through `liveWallpaperScrollPx` for the settings tab content.
- Layout engine. `compute_lw_layout()` scales dynamically (0.4x to 2.5x depending on window size) and lays out the hero, controls and grid proportionally, using a `LiveWallpaperLayout` struct for margin, tab, content, hero, controls and gallery dimensions.
- Theme colors. `lw_resolve_colors()` supports both matugen chrome paints and a fallback dark theme (`BgR=0.102, BgG=0.075, BgB=0.188, AccR=0.769, AccG=0.659, AccB=0.941`).
- M3 UI rendering: cards with a 14px radius, combo boxes, dropdown popups with hover tracking, pill style sort buttons, outlined accent buttons and glass highlight surfaces.
- Cleanup: the `src/ux/file_browser/` and `src/ux/screenshot/` directories were removed from the main tree, and the gallery helpers were ported into the live wallpaper codebase.

### Control Center: Major rework

- New dispatch system. `control_center_dispatch.cpp` and `.hpp` route events centrally for pointer move, click, scroll and keyboard input.
- Popup dispatch framework. `popup_dispatch.cpp` and `.hpp` plus `popup_items.cpp` and `.hpp` form a reusable popup item system with icon and label rows, toggle items, slider items and button items.
- Keyboard navigation: a spotlight search keyboard handler (`spotlight_keyboard.cpp` and `.hpp`), an app drawer keyboard handler (`app_drawer_keyboard.cpp` and `.hpp`), and tab, enter and escape navigation.
- App drawer dispatch. `app_drawer_dispatch.cpp` and `.hpp` are a separate input module for app drawer interactions.
- Dock slot dispatch. `dock_slot_dispatch.cpp` and `.hpp` handle per slot click and hover, pulled out of the monolithic dock input.
- Animation system. `control_center_anim.hpp` has smoother panel transitions.
- Panel geometry. `control_center_panel_geometry.cpp` was refined for better popup positioning.
- Paint improvements to the Bluetooth widget (`bluetooth_paint.cpp` and `.hpp`) and the control center popup.
- Margins system. New `margins.cpp` and `.hpp` give consistent popup margin calculations.
- Toplevel hooks were refactored into a shared module, and the widget bar gained slot hooks for widget level dispatch.

### Settings application improvements

- Wallpaper tab: a major performance refactor. Config serialization was rewritten (`settings_serialize.cpp`, 521 lines changed), wallpaper path resolution was streamlined, thumbnails load asynchronously, and redundant stat calls were removed.
- Live wallpaper tab: adapted for the standalone live wallpaper split, with UI config persistence.
- Dock and Taskbar tabs: updated widget bar slot references, and toggle accent colors fixed.
- Desktop widgets tab: updated for the config refactoring.
- Icons tab: a small include fix.
- Accounts tab: fixed a null pointer crash when `getenv("HOME")` returned null during the avatar `cp` command. The user name, `@username` and "Choose Image" button moved to the far right of the card (right aligned through Pango text measurement), so they no longer overlap the avatar placeholder.
- GPU settings: updated for the new config structure.
- Widget picker: a major cleanup (447 lines removed) that simplifies the widget catalog.
- Event handlers: updated scroll targets, active field management and input routing for the new dispatch system.

### Desktop shell

- Dock:
  - New `dock_slot_dispatch.cpp` and `.hpp` extract slot click and hover handling from the monolithic `dock_input.cpp` (1675 lines removed from it).
  - Improvements to pinned app storage and reordering.
  - Tooltip positioning fixes.
  - Widget bar paint and dispatch updates.
  - The trash state module moved to the shared `fs/trash_state.cpp` and `.hpp`.
- Taskbar: core logic refinements and paint updates for group popups.
- App drawer: new `app_drawer_dispatch` and `app_drawer_keyboard` modules for extracted input handling and keyboard navigation.
- Spotlight search: `spotlight_keyboard.cpp` and `.hpp` add keyboard driven app search with arrow navigation.
- Popup system: new `popup_dispatch` and `popup_items` modules for generic popup item rendering and event dispatch, plus session management improvements.
- Bluetooth widget: paint and layout improvements.
- Config watch: file watching for config changes is now debounced.
- Namespaces: namespace declarations are centralized in `namespaces.hpp`.
- Vulkan and Wayland: `vulkan_wayland.cpp` handles swapchains better (159 lines added).
- Debug logging: `debug_log.hpp` gained structured logging support.

### Configuration and serialization

- `shell_config.cpp` and `shell_config.hpp` were largely rewritten (521 lines changed): path resolution is streamlined, redundant fields are gone, and settings group handling is better.
- `settings_serialize.cpp` has rewritten JSON serialization (521 lines changed) for the new settings structure.
- `settings_app_types.hpp` gained fields for the new dispatch and input systems.
- `settings_embed_lifecycle.cpp` was updated for config changes.
- Removed the obsolete declarations from `settings_serialize.hpp`.

### Build system

- Meson: removed the file_browser and screenshot source entries (they moved to submodules), removed the tomlplusplus subproject wrap dependency, and updated for the new directory structure.
- `build.sh`: updated for submodule based builds, with horizon-files and horizon-shot build stages.
- `PKGBUILD`: converted to a split package (`event-horizon-de`, `horizon-shot` and `horizon-files`), each with its own version and dependencies.
- `justfile`: removed (replaced by `build.sh`).
- Submodules: removed the inline `subprojects/tomlplusplus-3.4.0/` and `subprojects/tomlplusplus.wrap` in favor of a git submodule, added the nlohmann/json cirrus CI config, and registered the horizon-files and horizon-shot submodules.

### CI/CD

- GitHub Actions: a full multi distribution pipeline for Arch (`.pkg.tar.zst`), Debian (`.deb`) and RPM (`.rpm`) with debug symbol variants, version extraction from sub project `meson.build` files, shared staging artifacts across packaging jobs, and submodule auto update to the latest Stable branch.
- CircleCI: a parallel pipeline with workspace persistence, per format packaging jobs and nested submodule init for horizon-files.
- The root `config.yml` mirrors the CircleCI configuration for multi CI redundancy.
- Submodule handling: a force populate fallback for incomplete gitlink registrations, `--depth 1` shallow clones for speed, a nested `git submodule update --init` for horizon-files dependencies, and a manual `git fetch origin Stable` plus checkout instead of `--remote` to avoid shallow clone tracking ref problems.
- Dependencies: added `cmake`, `openssl-devel`, `libjpeg-turbo-devel`, `poppler-glib-devel` and `libarchive-devel` to the build containers.
- Dependency fix: removed duplicate dependencies from `meson.build` and fixed the ffmpegthumbnailer subproject handling.

### Documentation

- Added build guides: `Docs/Arch-Linux.md` (with the full dependency list and build instructions), `Docs/Debian-Ubuntu.md` and `Docs/Fedora.md`.
- The README was updated with sub project references.
- Removed `files.md`, a stale 447 line file listing.

### Other changes

- Notification service: fixed the D-Bus service include path.
- Changelog: updated for 0.9.0 with a detailed commit history.

### Full commit history

38 commits since 17899fa (Alpha 2, 0.8.0), covering the File Manager and Screenshot Tool integration, the CI/CD pipeline overhaul, toggle color fixes, the control center dispatch system, dock slot dispatch, keyboard navigation, wallpaper performance and the build system restructuring.

## [0.8.0] - 2026-06-05

### License

- Relicensed under the MIT license (added a LICENSE file).

### Dock tab: M3 rewrite with 3 child tabs

- Settings child tab. A "Visibility and behavior" card with 5 toggle rows: Show dock, Auto hide dock, Group running windows, Dock tooltips and Pinned apps tray pill. Each has a title, a description and a right aligned toggle.
- Widgets child tab. Interactive widget reordering in a 3 column layout (Left, Center, Right), with drag and drop and a ghost preview, a toggle and remove button for each widget, and an "Add widget" button per column.
- Appearance child tab (new `settings_tab_dock_appearance.cpp`). 8 settings in a 2 column grid: Corner Radius (0 to 50), Dock Scale (50 to 150%), Icon Size (0 to 50), Icon Spacing (-50 to 50), Dock Opacity (0 to 100%), Dock Border (toggle), Border Size (0 to 12px) and Border Opacity (0 to 100%).

### Taskbar tab: M3 rewrite with 3 child tabs

- Settings child tab. 7 toggle rows (Show taskbar, Position at top, Group running windows, Auto hide, Embedded widgets, Tooltips and Pinned apps tray pill) plus a Width mode combo (Floating, Edge to edge or Fill).
- Widgets child tab. The same 3 column drag and drop widget reordering as the dock tab.
- Appearance child tab. 8 M3 sliders: Height (24 to 120px), Radius (0 to 50px), Opacity (0 to 100%), Icon size (0 to 96px), Icon spacing (0 to 50px), Floating gap (0 to 50px), Scale (50 to 150%) and Border size (1 to 12px), plus a Taskbar border toggle.

### Sound tab: M3 rewrite with per app audio

- Per app audio streams now show their resolved app icons with volume sliders and mute buttons.
- Default Output (sink) and Default Input (source) are visually separate, each with an icon, a label and a combo dropdown.
- The Engine section was redesigned as a 2 column grid for sample rate, force graph rate, allowed rates preset and default output PCM.
- Visual overhaul: card radius 14, header icons 24px, headings at 15px and weight 700, section gap 32, label column 200 and form row pitch 56.
- Default audio settings are saved and reapplied through `apply_saved_defaults()`.

### Night Light tab: M3 redesign

- Slider drag state is unified (code range 210 to 215), replacing the separate `nightlightSliderDrag`.
- A new `nightlight_consume_pointer_move()` handler removes the inline slider logic from the event handler.
- M3 card pattern for color temperature, schedule and brightness controls.

### Notifications tab: M3 rewrite

- A single `notifSliderDrag` replaces the separate `notifTimeoutSliderDrag` and `notifToastGeomSliderDrag`.
- New geometry helpers: `notif_app_geom()`, `apply_notifications_slider_x()` and `notifications_primary_test_button_geom()`.
- Added a `notificationsScalePct` setting for percentage based notification scaling.

### Themes tab: M3 redesign

- A simplified mini preview with window controls (red, yellow and green dots), an "Application" title, a search field, a "Remember me" checkbox, a "Continue" filled button and a divider line.
- The preview uses the actual theme colors from `GtkThemeColors` (headerBg, headerFg, bg, accent, fg, text and base).
- Fixed command quoting when applying GTK and Plasma cursor themes.

### Launcher tab: M3 update

- Geometry refactor. `launcher_slider_row_geom()` now delegates to `launcher_app_geom()` instead of using inline constants.
- The view mode combo geometry takes a pointer output instead of a hardcoded Y offset.

### Workspaces tab: M3 update

- Geometry refactor. `workspace_slider_row_geom()` now delegates to `workspace_app_geom()`.
- Layout constants moved out of event handlers into shared functions.

### Wired tab: Complete M3 rewrite with 6 child tabs

- Status: interface name, connection status, speed, duplex, auto negotiate, MAC, IPv4 and IPv6 addresses, gateway, DNS, driver info, vendor, product, MTU and device state.
- IPv4: sliders for DNS priority, DHCP timeout, route metric and required timeout, plus toggles for send hostname, ignore auto DNS, never default and may fail (emerald-500 accent).
- IPv6: the same structure, with a router advertisement timeout.
- Ethernet: an MTU slider, an auto negotiate toggle and an accept all MACs toggle.
- Security: toggles for 802.1x and system CA certificates.
- Advanced: an autoconnect priority slider, an autoconnect toggle and a browser only toggle.
- The network service now reads 22 new Ethernet properties, including speed, carrier, duplex, driver version, firmware and vendor.

### Hyprland tab: Full M3 rewrite (11 sections)

- Replaced all `.inl` inline files with M3 styled `.hpp` components (about 3107 lines in total):
  - General: 5 sliders and 3 toggles (borders, gaps, tearing, snap).
  - Decoration: 18 sliders and 7 toggles (rounding, opacity, blur, shadow, dimming).
  - Input: 7 sliders and 8 toggles (repeat, sensitivity, touchpad, follow mouse).
  - Binds: 2 sliders and 2 toggles (scroll delay, focus).
  - Dwindle: 4 sliders and 4 toggles (split ratio, force split, smart resizing).
  - Master: 2 sliders and 2 toggles (master factor, slave count, new on top).
  - Scrolling: 2 sliders and 2 toggles (column width, scroll focus).
  - Animations: a master toggle and 31 per entry toggles across 5 categories.
  - Misc: 2 sliders and 6 toggles (VRR, wallpaper, autoreload, logo).
  - Render: 3 sliders and 4 toggles (FP16, shader blur, direct scanout, color management).
  - Cursor: 3 toggles (hardware cursors, hyprcursor, gsettings sync).
- Added an animation popup editor with speed control, a curve selector (13 presets) and a style selector per entry.
- Added a cubic bezier editor with 13 preset curves.
- The sidebar went from 11 sub tabs down to a single entry.

### Power tab: Full layout redesign

- Sleep and Power child tab. A Display Sleep card with an emerald-500 toggle and a timeout slider (1 to 120 minutes), an Idle Suspend card with a toggle and a timeout slider (5 to 240 minutes), a Power Button and Lid Close pair side by side with action combo dropdowns, and a header only Battery percentage toggle card.
- Performance child tab. A CPU Performance card with right aligned combos for Scaling Governor (Performance or Powersave) and Energy Performance Preference (5 levels), and a System Tuning Profile card with 8 TuneD profiles applied through `tuned-adm` asynchronously in a detached thread.
- All toggles use the emerald-500 accent (`eaR=0.396, eaG=0.792, eaB=0.412`).

### Settings infrastructure

- Rewrote `settings_event_handlers.cpp`. Dozens of duplicate layout constants were removed, and geometry was consolidated into shared functions (`dock_app_slider_geom`, `notif_app_geom`, `workspace_app_geom`, `launcher_app_geom` and `appearance_app_geom`).
- Added animated smooth scrolling with `settingsScrollPxTarget` and a per frame convergence.
- Added opaque panel support for dropdown popups (the `opaquePanel` parameter skips the glass highlight).
- Fixed a scroll paint and click mismatch in the power tab (`activeTab == 32` now maps to `app.settingsPowerScrollPx`).
- Fixed the power button dropdown popup not rendering (its geometry was being overwritten by lid close).
- Fixed hover tracking for all 5 power dropdowns in the `on_pointer_motion` handler.
- Improved widget drag and drop with a ghost preview, grab offset calculation and section and index tracking.
- Hyprland event routing now uses a single entry (tab 33) instead of 11 sub tabs (33 to 43).

### Bug fixes

- Matugen stability. Fixed header includes after the backend restructure, added locked border colors to the matugen output, added group and groupbar section config for Hyprland v0.48 and later, added a `ptyxis` terminal theme template, and fixed the keyword fallback syntax (it now tries both `key:value` and `key/value`).
- Drive mounting. Improved mount and unmount operations with proper path handling and polkit integration.
- General: fixes to the power actions backend, launcher tab, network tab, wired settings tab and icon tab, plus UI fixes across several tabs.

## [0.6.0] - 2026-06-04

### Animation system

- Implemented a complete Material 3 (M3) motion and animation framework. The core animation engine provides easing curves, duration presets and keyframe support.
- Defined motion design tokens that follow the M3 specification (duration, easing and delay constants).
- Integrated the animation system into the widget rendering pipelines of all shell components.
- Rewrote the animation manager with better interpolation, completion callbacks and shared animation state.
- Added animated transitions to dock tooltips, OSD overlays, taskbar state changes, app drawer popups, power modals, calendar popups, media player popups, volume mixer popups, VPN popups, weather popups, the battery, clock, media, weather, workspaces and Bluetooth widget paints, settings panels, wallpaper transitions, and button, slider and toggle interactions.
- Added layout caching and measurement scratch utilities for efficient animation frame rendering.
- Added support for animated pill style slot rendering.
- Replaced every instant visibility toggle with an animated transition (fade, slide or scale).

### Compositor backend rewrite (multi compositor architecture)

- MangoWM:
  - Full workspace management overhaul, rewritten from scratch.
  - Rewrote the keyboard and output backend for the new architecture.
  - Added a runtime with IPC integration, an event loop and compositor lifecycle management.
  - Added detailed MangoWM documentation covering the architecture, IPC protocol and configuration.
  - Implemented compositor level power management (shutdown, reboot, logout and suspend through logind).
- Hyprland:
  - Added a runtime with socket based IPC, event handling and window management.
  - Added an event handler for Hyprland IPC signals (workspace change, window open and close, urgent and fullscreen).
  - Added keyboard layout management, output and monitor management, and workspace tracking and manipulation.
  - Added a toplevel window mapping protocol that associates windows with workspaces, plus window identification and matching.
  - Added a new Wayland protocol for focus grabbing.
- Sway: added an IPC runtime with socket communication, keyboard layout management, output management and workspace tracking.
- Niri: added the compositor runtime, keyboard support, output support and workspace management.
- Triad: added the compositor runtime, keyboard support, output support and workspace management.
- Ext-workspace protocol backend: added support for the wlr-ext-workspace-unstable-v1 protocol, with workspace creation, removal, reordering and naming.
- Compositor abstraction layer:
  - An IPC abstraction over compositor communication channels.
  - Virtual interfaces for keyboard and workspace management.
  - Runtime compositor detection (Hyprland, Sway, Niri, MangoWM and Triad).
  - A restructured Wayland registry for dynamic compositor feature registration.

### D-Bus services restructuring

- All D-Bus service implementations moved to a dedicated D-Bus layer, for a cleaner separation of concerns. This covers Bluetooth (service and agent), the keyring and secret service, MPRIS (player and album art), NetworkManager (service, secret agent and type definitions), the notifications service, the system tray (host, manager, SNI icons, stream icons, environment filter and context menu), and the UDisks2 drive service.
- Added a GNOME and KScreen global shortcuts D-Bus service.
- Added a Bluetooth pairing agent.
- Significantly rewrote the NetworkManager service to fix VPN connection state tracking, secret agent integration and connection activation.

### File structure refactoring

- Renamed and reorganized many core files for more consistent naming and logical grouping.
- Reorganized widget files into subdirectories by component type (battery, clock, media, weather, spacer, widget bar, shared utilities, app drawer and start menu).
- Removed all dock specific component files that had been refactored into shared components (the dock calendar popup, control center, spotlight, tray, weather popup, volume mixer and workspace strip).

### Desktop widgets system

- A full desktop widget system with:
  - A widget host container with dynamic placement, resizing and configuration.
  - An interactive calendar widget with month navigation and event indicators.
  - Analog and digital clock displays.
  - A full media player widget with album art, playback controls and progress.
  - A system monitor widget with real time CPU, memory, disk and network graphs.
  - Current conditions and forecast weather, with animated backgrounds.
  - A compositing layer for widget rendering.
  - Preferences for widget position, size and visibility.
  - An audio visualization data reader (CAVA integration) for visualizers.
- Merged desktop widgets into the desktop experience layer.
- Updated the settings UI for widget configuration.

### Settings application

- Hyprland tab. Comprehensive Hyprland configuration covering:
  - General settings (mod key, sensitivity, gaps, borders, cursor behavior, gestures and tearing).
  - Input (keyboard layout, touchpad, mouse and scroll behavior).
  - Key binding management with modmask, keycode, dispatcher and argument editing.
  - Decoration (rounding, opacity, blur, shadows, dimming and colormatrix).
  - Animations with per style settings (fade, slide, windows, workspaces and border).
  - Layout settings (dwindle, master and top, orientation, gap offsets and split options).
  - Dwindle specifics (pseudo mode, `no_gaps_when_only` and `force_split`).
  - Master specifics (new window position, master factor and smart resizing).
  - Rendering (explicit sync, direct scanout, vsync, cursor and power optimization).
  - Miscellaneous settings (`disable_autoreload`, XWayland, render ahead, NVIDIA and VRR).
  - Scrolling configuration (scroll method, fingers, edge and axis).
- Power tab: power management with suspend, hibernate, power button action and idle timeout.
- Time tab: timezone selection, clock format (12 or 24 hour), date format, NTP sync and manual time and date.
- Keyboard tab: layout and variant selection with a preview, repeat rate and delay sliders, and compose key configuration.
- Accounts tab: user account management, avatar change and password change.
- Bluetooth tab: device scanning, pairing, connection management and device removal.
- Themes tab (rewritten): complete theme management with preview thumbnails, import and export, and color scheme selection.
- Desktop tab: desktop icon layout, show and hide icons, snap to grid and icon size.
- Dock tab: M3 styled dock configuration (position, size, autohide and behavior).
- Icons tab: icon theme selection with a preview.
- Bing Wallpaper tab (rewritten): a daily wallpaper with region selection, resolution and download management.
- Monitors tab (updated): DRM probing, GPU detection and layout editor improvements.
- Night Light tab: a blue light filter with a schedule, a temperature slider and brightness.
- UI Layout tab (rewritten): panel and dock layout configuration.
- Wallpaper tab (rewritten): selection with thumbnails, solid colors and gradients.
- Workspaces tab: workspace layout and behavior configuration.
- Notification tab (updated): per app notification blocking, do not disturb and popup position.
- Default Apps tab (updated): application association per MIME type.
- Launcher tab (updated): app drawer and launcher preferences.
- Network tab (updated): Wi-Fi, wired and VPN connection management, with a rewritten VPN add dialog.
- Sound tab (rewritten): per app audio volume, input and output device selection, and mute.
- Taskbar tab: taskbar behavior, grouping, pinning and appearance.
- Settings serialization was rewritten with JSON based persistence for all the new tabs.
- Settings are now applied reactively, driven by events and debounced.

### M3 UI framework

- A complete Material 3 UI component library:
  - Core framework: animation engine, flexbox style layout, focus ring, glyph rendering, typography label, separator, spacer and state layer (hover, focus and pressed).
  - Controls: banner, bottom app bar, bottom sheet, the full button system (filled, tonal, outlined, text, elevated and icon), card (elevated, filled and outlined), carousel, checkbox with an indeterminate state, chips (input, choice, filter, action and assist), date picker, dialog, floating action button, text input, navigation bar, drawer and rail, radio button group, search bar, dropdown select, slider (continuous, discrete and range), snackbar, tab bar (fixed and scrollable), time picker, toggle switch and top app bar.
  - Design tokens: color (primary, secondary, tertiary, error, neutral and variant surfaces), elevation levels 0 to 5, motion duration and easing, shape corner radius, spacing scale, and typography scale (display, headline, title, body and label).
  - A builder pattern for constructing component trees.
  - Dynamic color palette generation, implementing material_color_utilities in C++.
- Integrated M3 theming across all settings panels (60+ tabs migrated to M3 styled rendering).
- Added a fallback color scheme palette for when theme resolution fails.
- Updated the theme system with M3 color scheme integration and dynamic palette switching.

### App Drawer and Start Menu

- Split the monolithic app drawer into separate app drawer and start menu components.
- App Drawer:
  - A grid layout with alphabetical sorting and category filtering.
  - Overlay rendering with performance optimizations (cached icon surfaces and dirty region tracking).
  - Per app entry rendering with icon caching, hover states and launch handling.
  - Power action execution (shutdown, restart, logout and suspend), with a power confirmation modal dialog.
  - Usage tracking, for sorting by most frequently used apps.
  - Desktop entry enumeration with parsing and caching.
  - Gesture zone definitions for opening the drawer.
  - Popup menu rendering.
- Start Menu: grouped app categories with search filtering and pinned and frequent apps, plus activation zone definitions.
- Launchpad:
  - A complete rewrite with folder creation and management, drag to folder and pin to dock support.
  - A search bar with real time filtering and relevance scoring.
  - A folder maker icon for quick folder creation.
  - Folder state that persists across sessions.
  - Layer surface management with dirty region tracking for performance.
  - A right click context menu for folder creation and app pinning.
  - Fixed: Launchpad did not keep user created folders between sessions.
  - Fixed: Launchpad performed badly (re-rendering is now throttled and the icon cache was optimized).
- Overview: a complete workspace overview with window preview thumbnails through screencopy, and animated workspace transitions.
- Window Picker: a window selection interface for task switching.

### Taskbar

- A full taskbar rewrite with:
  - Window grouping by application ID, with expandable group popups.
  - Visual state indicators (active, urgent, minimized and focused).
  - A progress bar overlay for window progress (file transfers and downloads).
  - Drag and drop reordering of pinned and running applications.
  - A right click context menu (close all, minimize all, pin and unpin).
  - Animated hover and active state transitions.
  - Startup notification integration (launch feedback).
  - Configurable position (top or bottom), size and behavior.
  - A mini panel mode, a compact taskbar variant for vertical panels.
  - A settings tab with grouping, pinning and appearance options.
  - Fixed: icons were not set on configuration reload.

### Dock

- Core dock implementation with persistent pinned app storage (with serialization), drag to pin and running app indicators.
- Rewritten input handling with gesture support (swipe up for the app drawer, swipe left or right to switch workspace).
- A widget bar for rendering battery, clock, Bluetooth, media, VPN, workspaces and weather slots.
- Hover and tooltip popup picking with directional awareness and edge case handling.
- Animated tooltip rendering with position adjustment.
- Dynamic dock positioning (horizontal or vertical, with edge snapping).
- Fixed the dock exclusive zone calculation, so windows avoid the dock properly.
- Fixed activation of the desktop right click menu.
- Added a trash and discard drop target on the dock.
- Removed the monolithic dock app drawer and control center implementations (more than 11,000 lines deleted, refactored into shared components).

### Control Center

- An M3 styled control center with quick settings toggles.
- A comprehensive popup with notifications, volume, brightness, media player, network, Bluetooth and settings shortcuts.
- Hit testing and layout calculation with direction aware expansion.
- Panel geometry management.
- State management for toggles, sliders and active panels.
- Added a VPN section that shows connection status.

### Power Menu

- A full power menu with an animated dialog:
  - Power actions: Shutdown, Reboot, Suspend, Hibernate, Logout and Lock Screen.
  - A confirmation dialog with a customizable timeout.
  - Keyboard shortcut integration.
  - D-Bus logind integration for system level power actions.
- A modal confirmation overlay in M3 styling, with action type icons and labels, Cancel and Confirm buttons with focus management, and Escape to dismiss.
- Fixed: the power buttons now really run system power actions through logind.
- Fixed: the power menu UI blocked input.

### Lock Screen

- Wayland session lock protocol implementation:
  - A full surface lock screen with a blurred background.
  - A password field with a visibility toggle.
  - Date and time display.
  - Media playback controls on the lock screen.
  - A keyboard shortcut for instant lock.
- Pointer and keyboard event routing for the lock screen.
- Integrated the ext-session-lock-v1 Wayland protocol.

### System Tray

- Moved the tray implementation out of the dock into a standalone module:
  - Tray host surface management.
  - Tray item registration, icon updates and menu management.
  - StatusNotifierItem icon rendering, with attention icon support.
  - An XDG environment filter for the tray.
  - Tray item context menu rendering.
  - Support for animated icon streaming.

### Keyboard

- Keyboard layout management infrastructure:
  - Applies the keyboard layout, variant and options through libxkbcommon.
  - Integrated with the compositor backends through the abstraction interface.
  - Multi layout support with per window layout tracking.
  - Compose key configuration.
  - Typing delay and speed configuration.
- Virtual keyboard support through the input method protocol.

### IPC service

- A Unix domain socket IPC server:
  - A command and response protocol that lets external tools talk to Event Horizon.
  - Commands for workspace management, window focus, power actions and settings query and modification.
  - A JSON based message format.
- A command line IPC client utility.

### Night Light

- A gamma based blue light filter:
  - Configurable color temperature (3500K to 6500K).
  - Schedule based activation (sunset to sunrise).
  - A smooth transition animation on enable and disable.
  - Settings tab integration.
- Added wlr-gamma-control-unstable-v1 protocol support.

### Wallpaper

- A wallpaper rendering engine with:
  - Dynamic wallpaper switching with a crossfade animation.
  - Solid color, gradient and image wallpapers.
  - Bing daily wallpaper download and caching.
  - Thumbnail generation and caching for fast previews.
- Config application with color scheme extraction from the wallpaper.
- Optimized image decoding for JPEG (libjpeg-turbo), PNG and WebP.
- An async thumbnail generation service.
- Removed the legacy desktop widgets overlay (widgets are now rendered directly in the desktop layer).

### Vulkan and rendering

- A major Vulkan rendering overhaul:
  - Proper swapchain recreation on output resize.
  - Image layout transition management.
  - Synchronization with timeline semaphores.
  - A custom Vulkan pipeline for M3 UI rendering.
  - Optimal buffer allocation that prioritizes dedicated GPU memory.
  - A Vulkan destruction queue with deferred resource cleanup.
- A CPU side Cairo fallback buffer for rendering without a GPU.
- Shared memory buffer pool optimization.
- Wayland surface extension protocol support.

### Icon cache

- A complete icon cache overhaul:
  - Multi theme lookup that follows the XDG icon theme specification.
  - SVG rendering with cairo and librsvg.
  - Direct PNG and WebP loading.
  - A lazily populated LRU cache with configurable size limits.
  - A fallback icon chain with parent theme traversal.
  - A hardcoded fallback for common application icons.
  - Memory efficient surface sharing.

### Performance optimizations

- Startup time: reduced by about 98% through lazy initialization, deferred D-Bus connections, parallel service loading, and postponing UI rendering until after the first frame.
- Memory: about 40% less RAM through icon cache size limits and LRU eviction, shared memory pool reuse for Vulkan buffers, lazy loading of bundled assets (fonts, icons and images), removal of unused bundled assets, efficient surface damage tracking that avoids full redraws, and less texture atlas fragmentation.
- App drawer: cached app icon surfaces with dirty region only redraws, and fewer layout calculations.
- Launchpad: throttled and debounced search, lazy folder loading and cached folder previews.
- Taskbar: faster window state diffing, and fewer paint operations through clip regions.
- Toggles respond instantly: the state changes synchronously and the visual animation follows.
- Media player: less frequent album art polling, and cached art surfaces.
- General: added a performance benchmarking framework, optimized memory usage across all widget types, reduced string allocations, and added a pool allocator for small objects.

### Crash fixes and stability

- Popup surface crash. Fixed a crash when destroying popup surfaces with active damage regions. A null buffer commit now happens before destroy, so the compositor does not hold on to a destroyed surface.
- Workspace switch crash. Fixed a null pointer dereference in the workspace backend when switching workspaces rapidly, by validating that the surface exists before focus operations.
- Other crashes fixed:
  - A segfault in the desktop layer when a toplevel is destroyed during an animation.
  - A use after free in the notification toast timer.
  - A crash on an empty workspace list.
  - A crash in the overview when a window closed during rendering.
  - A crash in the OSD when an output was removed.
  - A crash in the taskbar when a pinned app's executable was missing.
  - A crash in the dock when a foreign toplevel was removed.
  - A crash in the wallpaper renderer when an image failed to decode.
- UI fallback colors. Fixed the theme color fallback chain, which now cascades properly from the M3 palette to the system theme and then to the hardcoded fallback palette.
- Icon reload. Fixed icons not updating when the theme or color scheme changes at runtime.
- Polkit typing. Fixed text input in the PolicyKit authentication dialog (key event propagation and focus).
- Power menu input block. Fixed the input region calculation that prevented click through on the power menu backdrop.
- VPN connection stability. Fixed VPN connection state tracking with proper phase transitions.

### Build system

- Meson build:
  - Integrated all the new compositor backends as optional build targets.
  - Added the build options `embed_assets`, `jpeg`, `polkit_agent`, `native_march` and `release_omit_frame_pointer`.
  - Added the M3 framework compilation units.
  - Added the D-Bus service source files in dedicated directories.
  - Added tomlplusplus as a subproject dependency.
- New build options and their defaults:
  - `embed_assets` embeds asset files into the binary (default false).
  - `jpeg` enables JPEG decoding through libjpeg-turbo (default false).
  - `polkit_agent` enables the PolicyKit authentication agent (default false).
  - `native_march` enables architecture specific optimizations (default false).
  - `release_omit_frame_pointer` omits frame pointers in release builds (default true).
- PKGBUILD: bumped the version from 0.2.0 to 0.6.0, added the dependencies libjpeg-turbo, librsvg, libdrm and polkit, updated the build dependencies, and added install targets for desktop files and icons.

### CI/CD

- GitHub Actions:
  - A complete CI pipeline with build, test and artifact publishing.
  - Multi distribution builds (Arch Linux through pacman, Ubuntu and Debian through apt).
  - Dependency caching for faster builds.
  - Automatic release artifact upload with versioned filenames.
  - Added the liblz4-devel dependency for both apt and pacman, and a binutils install step.
  - Upgraded the upload and download artifact action versions.
  - Removed redundant release steps and consolidated the package upload process.
- CircleCI: added an alternative CI pipeline with a multi architecture build matrix.

### Portal removal

- Removed the entire xdg-desktop-portal implementation, including the portal service, app chooser, dialog and file chooser.
- Removed the portal systemd service files and D-Bus service registrations.
- The portal is disabled through a configuration flag.

### Wayland protocols

- Added the protocol specifications hyprland-focus-grab, hyprland-toplevel-mapping and wlr-gamma-control.
- Added implementations for ext-foreign-toplevel-list, ext-session-lock, wlr-screencopy, wlr-gamma-control, virtual-keyboard and ext-idle-notify.
- Implemented protocol auto detection and dynamic registration.

### Documentation

- Added a comprehensive documentation directory covering:
  - The architecture overview, build instructions and configuration guide.
  - Compositor backend docs for every supported compositor.
  - D-Bus service interface docs.
  - A complete project file mapping reference.
  - Detailed shell component docs: App Drawer, Control Center, Desktop, Dock, Keyboard, Launchpad, Lock Screen, Notifications, OSD, Overview, Power Confirm, Spotlight, Taskbar, Unified Shell and Widgets.
  - Service docs: Audio (PipeWire), Bing Wallpaper, IPC and PolicyKit.
  - The UI component library and settings application docs.
  - The wallpaper system and Wayland protocol integration docs.
  - UX design docs.
  - A MangoWM reference (3947 lines).
- Updated the project README with build badges and a feature list.
- Added a configuration reference document.
- Updated the example configuration file with the new options.

### Miscellaneous

- Clipboard: rewritten with the proper Wayland data device protocol, MIME type filtering and offer caching.
- Notifications: the toast system was rewritten with action buttons, urgency levels, timeout management and position configuration.
- Audio visualizer: a PipeWire audio data reader for waveform visualization.
- Bundled fonts: the Material Symbols font and other icon fonts are now embedded directly.
- Memory monitor: accurate memory usage reporting from the system status.
- Desktop archive: desktop file archiving and compression for backups.
- Open With dialog: an "Open with" dialog for managing file associations.
- RF kill switch: monitoring of the Bluetooth and Wi-Fi radio state.
- Thread pool: a reusable thread pool for asynchronous operations.
- Battery widget: shows the icon, percentage and charging state, using UPower through D-Bus.
- Bluetooth widget: shows a device list, connection state and a toggle.
- VPN popup: VPN connection management with status, connect and disconnect, and configuration.
- Weather widget: improved with animated conditions, a multi day forecast and location detection.
- Workspace widget: rewritten with animated transitions, better multi monitor support and touchpad gesture integration.
- Media widget: rewritten with album art, a playback position slider and per app media control.
- Calendar popup: updated with event indicators and month navigation.
- Volume mixer popup: updated with per app audio stream control and output device selection.
- Media player popup: a full media player popup with playback controls.
- Settings widget drag: drag and drop widget reordering in settings.
- Widget picker: a desktop widget type selection dialog.
- Matugen integration: improved Material You color generation with external template support.
- Screen recording: screencopy PNG capture for the screenshot function.
- Idle inhibitor: prevents idle and suspend during fullscreen video playback.
- File chooser dialog: a native file chooser dialog.
- Dialog base: reusable dialog infrastructure for all modal dialogs.
- Debug and diagnostics: structured diagnostic logging, a performance benchmarking framework and Wayland event loop diagnostics.
- Translations: set up the internationalization framework.

### File statistics

This release covers about 260 or more commits, from 2026-05-19 to 2026-06-04, touching more than 500 unique files with roughly 70,000 lines added and 35,000 removed across the codebase.
