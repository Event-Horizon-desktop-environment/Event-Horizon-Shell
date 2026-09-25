# Changelog

## [Unreleased]

### Settings Keyboard + Language — Working Layout Switching, Reorder, System Locale

- **Layout switching actually switches**: clicking a source row, the layout
  dropdown, or adding a layout now jumps the running compositor straight to
  that layout on Hyprland (`hyprctl switchxkblayout` on the main keyboard,
  parsed from `hyprctl -j devices`) and Sway (`swaymsg xkb_switch_layout`),
  instead of only rewriting config and leaving the user to press the toggle
  shortcut manually.
- **Fixed the broken shortcut table**: "Super+Space" mapped to an empty XKB
  option (a live no-op) — now `grp:win_space_toggle`. Sway pushes also gained
  `xkb_numlock` and no longer go through a shell.
- **Reorder sources** with per-row up/down chevrons (shared paint/hit zones);
  removing the active layout still falls back sanely.
- **System layout list**: the add-layout list comes from
  `localectl list-x11-keymap-layouts` (cached, hardcoded fallback) with
  prettified names for unknown codes, instead of 28 hardcoded entries.
- **Repeat-slider drag skew fixed**: paint, pointer-down and pointer-motion
  share one `repeat_slider_track_geom()` (motion used a hardcoded 240px
  sidebar vs the real 260px and a 32px-narrower track).
- **Dead toggles removed**: "Show layout indicator" and "Input method" had no
  backend anywhere (zero readers) — the layout card shrinks from 5 rows to 3
  and toggle hit rects now match paint exactly. Middle-click paste is wired
  through to Hyprland's `misc.middle_click_paste`.
- **New LANGUAGE card**: installed locales (`localectl list-locales`) with
  current-locale badge, staged-apply via
  `pkexec localectl set-locale` (validated against the installed list),
  login-screen keymap readout plus one-click
  `pkexec localectl set-x11-keymap` from the enabled layouts, and a status
  line. Per-window switching scopes and IME management have no compositor
  backend and were deliberately left out.
- **Screenshot round**: repeat sliders show persistent values (the m3 popup
  only appears mid-drag) in a fixed 76px reserve that keeps the shared
  drag track stable.

### Settings Time — Real System Timezone, Manual Clock, Scroll Fixes

- **"Set as System Timezone"**: the picked override can now be written with
  `pkexec timedatectl set-timezone` (validated against the zone database);
  the card shows the real system timezone (cached, tick-refreshed) next to
  the desktop-only override, with a status line.
- **Manual clock editor** when NTP is off: date + time fields (strict
  YYYY-MM-DD / HH:MM[:SS] validation incl. leap years) with Enter-to-set via
  `pkexec timedatectl set-time`, UTF-8-safe editing and Tab switching.
- **NTP query moved off the paint path** (tick refresh + Refresh button);
  timezone picker list scrolls with the wheel; Change/Reset hovers respect
  scroll offset; custom-format Backspace is UTF-8 safe.
- **Scroll clamp for tab 30** (paint selector, wheel upper bound) so the
  taller panel can't overscroll. Weekday/date-locale display options would
  need clock-widget changes and were left out.
- **Screenshot round**: the Refresh button moved left of the NTP toggle
  (it painted on top of it); the preview card lost 28px of dead space.

### Settings Sound — Unified Combos, Over-Amplification, Speaker Test
- **Combo hit rects now equal paint rects**: closed combos are fixed
  `kSettingsComboW` wide everywhere (long names ellipsize inside, full text
  in the popup whose paint/hit already agreed) instead of paint widening by
  Pango measure while hit-testing a `strlen*8` guess.
- **Over-amplification**: "Allow volume above 100%" toggle (persisted
  `audio.allow_over_amplification`) lifts all four sliders to 150%
  (PipeWire already supports it; drag/throttle/final-commit scale the
  0..1 track norm accordingly).
- **Speaker test**: "Test Speakers" plays front-left then front-right
  (`pw-play --target=`, double-fork so the UI never blocks, with a
  test-signal fallback).
- **Card list cap 6 → 12**; engine cells hit-test the same cached values
  paint shows (no more clickable-but-dead or dead-but-painted cells);
  "No output/input device available." placeholders instead of vanishing
  sliders. Balance/meters/alert-volume/port selection have no PipeWire
  service API and were left out.
- **Screenshot round**: combos autosize to their text through one shared
  paint/hit geometry function (proven identical via a Pango determinism
  harness), so long device names widen the box instead of truncating to
  "..." while clicks still land exactly; all sliders show persistent
  percentages in their reserved track-end zone.

### Settings Startup — Working Typing, Honest Menus, Icons

- **Form typing works at all**: the key handler compared the Wayland state
  against `0` (RELEASED) while the dispatcher only forwards PRESSED/REPEATED
  — every keystroke was dropped. Now accepts both, with repeats guarded off
  one-shot keys, plus UTF-8-safe Backspace.
- **Row clicks no longer hijack the kebab menu** (hover highlight only);
  outside-popup clicks close the menu *and* reach the row beneath; the popup
  flips above the row when it would overflow the clipped list (shared
  paint/hit geometry).
- **Honest second action**: Delete (user entries), Reset to System
  (shadowed system entries), Disable/Enable (pure system entries, verified
  with `set_autostart_enabled` results and status messages); row and
  default-app toggles report failures instead of assuming success; editing
  preserves the disabled state instead of silently re-enabling.
- **Entry icons** (resolved like the app browser, glyph fallback), Exec
  shown width-capped in subtitles, delay validated with a form error
  (0–999), browser tail row reachable at every scroll offset.

### Settings Accounts — Full Multi-User Panel

The Accounts tab now manages every local user, not just the current one, and the
long-standing layout/wiring defects are fixed.

- **USERS card** (`settings_tab_accounts.cpp`, new `accounts_users.{hpp,cpp}`):
  user list enumerated with `getpwent` (UID ≥ 1000, current user first) with
  You/Admin/Locked badges, click-to-select, Administrator toggle (`usermod`/
  `gpasswd`, last-admin protected), Account Locked toggle (`passwd -l/-u`,
  self-lock refused, state queried lazily), full-name editing (`usermod -c`),
  and two-step Delete User (`userdel -r`, self-delete refused). All privileged
  calls go through pkexec with direct fork+exec (no shell) plus strict
  username/hostname/full-name validation.
- **AUTOMATIC LOGIN card**: GDM/SDDM/LightDM detection with per-user
  enable/disable (graceful "no supported login manager" state).
- **SET PASSWORD card**: now targets the selected account with a strength
  meter; create-user form gains an Administrator toggle; per-card
  success/error status lines replace the single shared message.
- **Layout**: the name/`@user`/button block is ellipsized into its own column
  and can never cover the avatar; narrow cards stack vertically; every card
  reserves a status strip; scroll is clamped to real content height (tab 46
  added to the paint-scroll selector) with scroll-to-focus on field focus.
- **Keyboard**: Enter submits the focused form, Delete works, key repeat
  works, input capped at 256 chars, Tab order follows the visual order.
- **Avatar**: JPEG and extensionless `~/.face` files decode (stb fallback),
  per-user avatar cache, install to other users' homes via
  `pkexec install -o user -g gid`; the missing-image disk probe no longer runs
  every repaint. Fields scroll horizontally instead of silently truncating at
  50 chars.

### Overview — Empty Workspaces Show the Desktop; Tiles Always Win

Empty workspace cards no longer render blank (or inherit the wrong monitor's
screenshot): they now fall back to the shell's own wallpaper image, cover-fitted
to the primary output's aspect exactly like the wallpaper layer paints it, so an
empty workspace card shows "just the desktop" (until a window opens on it). The
image is decoded once and cached (disk + memory), and the previous fallback
that matched a background workspace to its monitor's live screenshot was removed
— that could bake another workspace's windows into an empty card.

- **Desktop fallback from the wallpaper** (`desktop_shell/Overview/overview_host.cpp`, `overview_host.hpp`): `build_wallpaper_capture()` decodes `config.wallpaperImage` through the existing wallpaper thumbnail pipeline, cover-fits it to the primary output's aspect ratio (center-crop + uniform scale, mirroring `WallpaperRenderer` mode 0), and produces a normal `WorkspaceCapture`; `capture_workspaces()` now assigns that image to empty workspaces when no monitor was displaying an empty workspace at capture time. Active empty workspaces still use their own monitor's real screencopy. Falls back to the previous transparent render when the wallpaper is disabled or not decodable (e.g. a live/video wallpaper).
- **Tiles always paint over a capture** (`desktop_shell/Overview/overview_painter.{cpp,hpp}`): a workspace capture is only ever the backdrop — it stays behind the per-window tiles for workspaces with windows and fills the card alone for empty ones. Previously a fresh capture could hide the very windows it represented (apps looked like they'd slipped behind the desktop image). The `liveMode` parameter and its per-frame scan for streaming windows were removed as dead logic.

### Dock — Pinned Drag Reorder: Paint-Truth Geometry & Glide

The pinned-app drag reorder in the dock ("wonky the longer the dock gets") now derives its geometry from what paint actually drew, and slots glide to their new position instead of teleporting.

- **Paint-geometry snapshot at press** (`dock/core/dock_bar.cpp`, `dock/input/dock_slot_dispatch.cpp`, `dock/pinned/dock_pinned.cpp`): the drag captures the pinned run's left edge, slot pitch and icon width from the retained hit rects at press (validated like `dock_pick_retained`); the computed path is only a fallback, now built on the picker's shared strip geometry (`DockStripGeom` / `dock_compute_strip_geom` / `dock_slot_layout_x`). The pitch is measured from real pinned slot positions instead of assuming `icon + gap` — with `dockPinnedAppsTrayPill` the painted pitch is `icon` only, so the old error grew by `gap × index` (~130 px at 15 pins). The fallback also uses paint's inner margin (`dock_strip_inner_margin` = 12×ui scale, was 8×) and the full slot-width walk (Battery/WorldClock included), and handles the side-by-side (lr) layout.
- **`dockRunningAppsTrayPill` gap rule in paint** (`dock/paint/dock_paint_widget_bar.cpp`): all three of paint's `gap_after_*` walks were missing the zero-gap rule the picker already had, shifting the strip origin against pick and painting the running-apps pill too narrow.
- **Floating pin tracks the cursor** (`dock_paint_widget_bar.cpp`): the dragged icon was drawn inside the horizontal-compression transform using surface pointer coordinates, lagging the cursor by `(pointerX − midX) × (1 − hScale)`; it is now drawn after the transform, clamped in surface space.
- **Glide for the slot shuffle** (`dock/pinned/dock_pinned.cpp`, `dock_paint_widget_bar.cpp`): when the insert index re-flips, per-key fractional slot offsets are baked from the in-flight animation and re-based onto the new layout, then animated to 0 over ~160 ms (`EaseOutCubic` via `shellAnim`; reduced-motion settles immediately). Neighbouring pins slide instead of jumping; the pinned pill stays anchored to the target layout while cells move across it, and hit rects follow the target layout so they stay stable mid-animation. The layer-only draw fast path is disabled while a glide is running so all dock layers stay in step.
- **First commit has hysteresis** (`dock_slot_dispatch.cpp`): `pinDragInsertIdx` is seeded with the pin's real filtered index instead of `-1`, so the first move can't commit unopposed.
- **No tooltip / hover churn mid-drag** (`shared/system/input/shell_input.cpp`, `dock_pinned.cpp`, `dock/tooltip/dock_tooltip.cpp`): hover state, hover-lift animation and tooltips are suppressed while a pin is pressed or dragged — `dock_tooltip_tick`'s `create()` does a blocking `wl_display_roundtrip` that stuttered the drag.
- Dead code removed: unused `centers` vector in `dock_pinned_pointer_motion`, unused `pinnedXs` pre-pass in paint.

### Taskbar — Layout, Popup Toggle-Close & Bar Breathing Room

The taskbar's widgets no longer hug the bar ends, the center section is truly centered, popups can be closed by clicking their own button again, and the bar grows to keep breathing room around its widgets.

- **Padded three-section layout in every width mode** (`taskbar/paint/taskbar_paint.cpp`, `taskbar/core/taskbar.cpp`): left widgets start `strip_pad_px` (16×ui) inside the bar's rounded end, right widgets end the same distance from the other, and the center section is centered on the bar (and therefore the screen). Previously the panel anchored flush to `x` / `x + boxW`, and fill mode laid out as one centered strip — so the start button and the settings widget sat against the edges and the center widgets drifted off-center by `(left − right) / 2`. Fill mode now sizes its pill to `center + 2 × (pad + max(left, right) + section_gap)` so the three-zone layout always fits; sections that still collide fall back to the centered strip, which keeps the padding and compresses horizontally around it. Panel-fit checks account for the padding, and hit rects follow paint as before.
- **One gap rule everywhere** (`taskbar_paint.cpp`): the render advance (`gap_local`) silently packed pinned apps together while the width walkers (`gap_after_slots` / `gap_after_local`) applied `iconSpacing` when `pinnedAppsTrayPill` was off — widths and painted extents disagreed, shifting every section against its measured position (most visible as the center group sitting left of center). The three lambdas now share one `gap_between`, and `taskbar_measure_sections()` (replacing `taskbar_measure_content_width`) uses the same rules plus paint's slot widths (widget keys, live MPRIS snapshot, WorldClock) so fill-mode bar sizing can't drift from paint.
- **Toggle-close for every popup widget** (`taskbar/core/taskbar.cpp`): a click on the bar dismisses the open popup and records its kind; landing on the owning widget now returns instead of reopening — previously the dismiss ran and the widget's own handler immediately created the popup again, so weather, calendar, media player, control center, volume mixer, VPN, battery and the app drawer could only be closed by clicking elsewhere. Transport buttons on the media widget (prev/play/next) still act even while its popup is open.
- **Control Center popup sized to the UI scale** (`taskbar.cpp`): created as raw `620×640` while its cards, gaps and margins lay out at `dock_ui_scale()`, clipping content horizontally ("no margin") and vertically on scaled setups; it now uses `kControlCenterPopupW()/kControlCenterPopupH()` like the dock.
- **Popup clearance includes the edge margin** (`taskbar/layout/taskbar_position.cpp`, `taskbar_settings.hpp`): clearance was `floating + height + 6`, dropping `edgeGap`, so every popup sank into the bar by the bar's screen-edge margin; `popup_bottom_clearance_px` now mirrors the layer margins exactly (edge gap + floating inset + painted height, top-anchored mirrored).
- **Press overlay covers the whole slot** (`taskbar_paint.cpp`): the press rect was icon-sized, lighting up only the left half of wide widgets (weather, media, clock); it uses `slotW + 4` now, and the press scale is centered on the slot instead of its first icon-width.
- **Bar height guarantees breathing room** (`taskbar_settings.hpp`, `taskbar.cpp`): `effective_height_px` grows the bar to `iconSize × scale + 16 × ui` when the configured height is smaller — widgets keep at least 8×ui px of air per side instead of touching the bar edges. The normalized height feeds every consumer (layer size, exclusive zone, paint clamp, auto-hide, tooltips, popup clearance) and the reload compare notices a global UI-scale change; the workspaces pick path no longer double-scales the bar height against paint.

## [0.9.5 beta 1] - 2026-08-05

### Power Confirm — Glassy Restyle & Live-Countdown Fixes

The power-confirm dialog (dock + taskbar) was restyled on the M3 glass pipeline and its countdown/hover behavior fixed.

- **Glassy M3 restyle** (`power_confirm/power_confirm.cpp`): card, close/cancel/confirm buttons and the countdown progress bar now render through `m3::Box` glass surfaces (`glassy_box`) with rounded corners, a card outline, and `m3::StateLayer` hover overlays on every button; the confirm button gets a danger tint and the progress bar an accent fill. Card geometry is clamped with `std::max(0.0, …)` so the layout degrades gracefully instead of going negative on unusually small outputs.
- **Countdown froze in the dock** (`shared/popup/caret/caret.cpp`, `dock/core/dock_app.h`): the power-confirm frame-done handler early-returned for PowerConfirm, so the 60 s timer drew once and never ticked. `dock_popup_queue_followup_frame` now requests a follow-up frame while the confirm is open and the frame-done handler redraws every 250 ms (or immediately when hover motion is pending); between ticks it re-arms the frame callback and commits, so the countdown runs live and auto-executes at 0 s.
- **No countdown loop in the taskbar** (`taskbar/core/taskbar.cpp`, `taskbar/core/taskbar.hpp`): the taskbar popup had no frame callback at all. Added `popupFrameCb` / `popupLastDrawMs` / `popupMotionDirty` plus a `taskbar_popup_frame_done` listener that mirrors the dock cadence; `taskbar_popup_draw` arms the callback and the close/closed paths destroy it.
- **Hover lag / wrong hit positions at speed** (`shared/popup/dispatch/popup_dispatch.cpp`): the dock redrew the popup synchronously on every pointer-motion event (flooding the Vulkan present pipeline, so the compositor kept showing stale frames). PowerConfirm motion now only sets `popupMotionDirty` and queues a single follow-up frame; redraws are coalesced in the frame-done handler (≤ once per frame). The taskbar gained the same coalesced motion path in `taskbar_pointer_motion`. Result: hover state tracks the pointer at vsync cadence and click hit-testing matches what is on screen even during fast movement.
- **Stale enter coordinates** (`shared/system/input/shell_input.cpp`): `shell_pointer_enter` stored only the surface, leaving `pointerX/Y` from the last motion event — entering the popup hit-tested with stale coords until the first motion arrived. Enter now records the surface-local enter position and triggers an immediate power-confirm redraw.
- **Power-confirm clicks could be swallowed** (`widgets/app_drawer/input/app_drawer_dispatch.cpp`): the confirm/cancel pick was checked after the app-menu branches, so while the app menu was open the dialog's own buttons lost clicks. The PowerConfirm branch was hoisted to the top of `app_drawer_handle_click` so the dialog always wins while it is open.
- **Taskbar hit-test used the wrong size** (`taskbar/core/taskbar.cpp`): `taskbar_handle_popup_click` picked against the raw surface size while the popup is configured as a (near-zero) fullscreen grab surface; it now uses `popupConfiguredW/H` (the laid-out dialog size) so clicks match the rendered buttons. The grab surface itself is now configured as `0×0`.
- **Dock popup was pushed off-center** (`dock/input/dock_position.cpp`): `dock_popup_sync_layer_margins_if_open` re-applies the pill-position margin math to the popup layer on every draw, but the power-confirm popup is a fullscreen grab surface that must keep zero margins. With a centered dock strip narrower than the output it set `marginLeft = (outputW − dockW)/2` and a pill-clearance `marginBottom`, shrinking the surface from the left/bottom so the card sat right of the screen's center. The sync now skips the fullscreen-grab popup kinds.
- **Diagnostics:** setting `EH_POWER_CONFIRM_TRACE=1` prints card geometry + pointer position from `pick()`/`paint()` to stderr for debugging.

### IPC Fabric & Service Split (Phases 1–6)

The desktop shell now runs as a supervisor with split child services communicating over a persistent IPC bus.

- **New framed IPC protocol** (`src/services/ipc/ipc_protocol.{cpp,hpp}`): magic-prefixed, length-framed messages (`kFrameMagic=0xE7`, version 1, 1 MiB max body) with SCM_RIGHTS fd-passing, pub/sub topics, and request/response correlation ids. The legacy one-shot plain-text line protocol (used by `eh-ipc`) is still served on the same socket — the server peeks the first byte (`0xE7` → framed client, else legacy).
- **`IpcClient`** (`client.{cpp,hpp}`): connect with retry/backoff, blocking `request()`, ack-waited `subscribe()`/`unsubscribe()`, `publish()` with fd-passing, poll-friendly `on_fd_ready()` and event handler dispatch.
- **`IpcService`** (`ipc_server.cpp`): non-blocking poll-based server with per-client out-queue, subscription broadcast, fd attachment via `sendmsg`/CMSG, thread-safe `publish()`, and `poll_interests()` for integration into the shell's main poll loop.
- **`horizon-settings` child process:** settings was extracted to a standalone binary (`settings_standalone_main.cpp`, `run_standalone()` in `settings_embed_lifecycle.{cpp,hpp}`) with a pidfile singleton guard, `spawn_settings()` via `posix_spawnp`, subscription to `config.applied` (reload + redraw) and `command.request` (`settings.toggle`), and a `settings_launcher` hook replacing the old in-process embed. Taskbar settings clicks now go through `request_launch_settings()`; the `.desktop` Exec switched to `horizon-settings`.
- **`horizon-wallpaper` child process:** new `wallpaper_standalone.{cpp,hpp}` owns its own `WaylandConnection` + `WallpaperRenderer`, applies wallpaper on BACKGROUND layers, re-applies on the `config.applied` broadcast, registers a pidfile, and is spawned/reaped by the shell (`wallpaper_spawn_native_child()`, stale-child cleanup, SIGTERM→SIGKILL escalation). `WallpaperRenderer::init` simplified to create its own connection internally.
- **`horizon-notifications` child process:** new `notifications_standalone.{cpp,hpp}` bundles `NotificationManager` + `NotificationToastHost` + the DBus notification service, subscribes to `config.applied` and `notify.push`, and drives a display+timer+IPC poll loop.
- **`horizon-desktop` child process:** the desktop icon layer + widgets + menus were extracted from the supervisor into a `horizon-desktop` binary (`desktop_standalone.{cpp,hpp}`) that owns its own `WaylandConnection`, 1 s tick + 33 ms anim timerfds, and an `EH_MPRIS_NOTIFY=0` `DockMpris` listener (the dock stays the single now-playing emitter), subscribes to `config.applied`, and is spawned/SIGTERMed by the shell. The desktop module no longer borrows the dock: output targeting, dock reserve (`dock_effective_bar_height_px`), menu/UI scale, and catalog icons are read from the shared config snapshot (own `IconCache`), and pin toggling uses new settings-only `dock_settings_pinned_toggle`/`dock_settings_is_app_pinned` that persist to the dock settings toml (the dock hot-reloads via its inotify). Supervisor now routes SIGCHLD through a signalfd to reap children and respawn `horizon-desktop` on crash (5 s rate limit).
- **Notification IPC contract** (`notifications_ipc.hpp`): topics `notify.push`/`notify.changed`, field separator `0x1e`, kinds `internal` / `mpris` / `mpris-art` (album art downscaled to ≤160px and inlined as a length-prefixed image blob). MPRIS now pushes through `eh::notify::push_mpris` instead of the in-process manager; client `publish()` lets the shell broadcast client events to subscribers.
- **Per-service Wayland connections (Phase 5):** gamma, idle, lockscreen, and polkit each moved onto their own isolated `WaylandConnection` (`gamma_conn_`, `idle_conn_`, `lock_conn_`, `polkit_conn_`) so a protocol error only disables that one service. `LockScreen` became self-contained with its own `wl_keyboard` + xkb state and `sync_outputs()`; the old `lock_seat_dispatch` and `polkit_seat_dispatch` forwarding layers were deleted. Polkit keys now arrive pre-decoded `(state, sym, utf8)` and the auth dialog sizes its surface to a fixed `kCardW=460/kCardH=240` card.
- **Toplevel window service (Phase 6):** new `src/services/windows/` — `ToplevelService` snapshots windows with stable ids and publishes `toplevel.created`/`toplevel.updated`/`toplevel.closed`; `ToplevelClient` mirrors the bus for split components; `ToplevelBridge` feeds the wlr foreign-toplevel tracker into the bus (token-based snapshot callbacks added to `foreign_toplevels.cpp`/`ext_foreign_toplevels.cpp`).
- **Config broadcast:** `config.applied` is published with the aggregate config mtime after every apply; wallpaper/settings/notifications children reload from the broadcast.
- **Tests:** `test/test_contract_ipc.cpp` covers protocol encode/decode, the legacy shim, request/response, pub/sub, SCM_RIGHTS fd-passing, and the full toplevel codec/service/client round-trip with id-stability on window close. Registered as meson test `ipc_fabric`; `eh_shell_core_lib` converted static→shared (one copy shared by supervisor + children).
- **Extras:** crash handler now prints a `backtrace_symbols_fd` backtrace; `EventHorizon --version` prints the version; nightlight config is re-applied via a shared `apply_nightlight_config()` helper.
- **Parent-death guarantee:** shell-spawned children (`horizon-desktop`, `horizon-wallpaper`, `horizon-notifications`, `horizon-settings`) now install `PR_SET_PDEATHSIG` via `services/process/parent_death_guard.hpp`. The supervisor marks each spawn with an `EH_SUPERVISOR_PID` env var; the child asks the kernel for SIGTERM on parent exit, so `pkill -9 EventHorizon` reaps every child too (PDEATHSIG fires even on SIGKILL), with a `getppid()` check closing the spawn race. Standalone launches (e.g. the settings `.desktop` file) carry no marker and are unaffected. `desktop_spawn.cpp` additionally verifies `/proc/<pid>/stat` comm + exe before signaling so a pidfile pid reused by an unrelated process is never killed.

### `horizon-dock` child process (Phase 3 item 4)

The dock bar was extracted from the supervisor into a `horizon-dock` child. "Dock-first with seams": the supervisor keeps the taskbar, toplevel tracking, gamma, and Super+S; the child owns the dock surfaces, launchpad, overview, start menu, tray, settings watcher, MPRIS, and the Super-key menu toggle.

- **Child** (`src/desktop_shell/dock/standalone/` + `standalone_main.cpp`): own `WaylandConnection` + `DockApp`, own timer/tray/settings-inotify/mpris loop fds, weather engine, pidfile, parent-death guard, and the global-keyboard **Super** toggle (`dock_toggle_menu_from_keyboard`, smenu-first). No settingsFn — the supervisor keeps Super+S so the two evdev handlers never double-fire. IPC client subscribes `config.applied` (drag-preview sync → reload → schedule frame) and `command.request` (`settings.toggle`, `menu.toggle`, `launchpad.toggle`, `overview.toggle|open|close`).
- **Spawn/reap:** `dock_spawn.{hpp,cpp}` (`posix_spawnp`, sibling-binary resolution, stdio → `horizon-dock.log`, stale pidfile + `/proc/<pid>` comm/exe check, SIGTERM→SIGKILL→reap); SIGCHLD reap + 5 s rate-limited respawn in `build_handlers()`, shutdown kill. Mirrors `horizon-desktop`.
- **Session rewiring:** in-process dock, `launchpad_host_`/`overview_host_`, dock loop-fd handlers, `mpris_bus_fd_`, `dock_deferred_init_done_`, and launchpad/overview `PollMuxDisplay` registrations deleted. IPC handlers `launchpad`/`overview-*`/`menu-toggle` become `command.request` forwards. The `config.applied` hook is now taskbar redraw + embed redraw + nightlight + broadcast; the drag-preview paint tick draws the taskbar only.
- **`ToplevelTracker`** (`src/desktop_shell/shared/toplevel/`): new shared struct binding wlr foreign-toplevel v3 (+ ext-foreign-toplevel-list on Hyprland) on the session's main display. The taskbar borrows `toplevels`/`extToplevels`/`compositorKind` (its own secondary connection can't track toplevels) and `ToplevelBridge` publishes from the tracker.
- **Own settings watcher:** the supervisor opens its own inotify (`open_state_inotify()`/`drain_inotify`) to reload keyboard/nightlight/taskbar on raw file edits — no `config.applied` broadcast there, preserving the skip-matugen-on-inotify optimization (the child reloads itself independently).
- **Global keyboard:** the supervisor installs only the settings fn (Super+S → `settings.toggle`/`spawn_settings`); Super alone is handled entirely by the child. Gamma stays supervisor-owned (single gamma client — a second one in the child would fight it).
- **Taskbar Launchpad paint seam:** `Host::paint_dock_button` extracted to a shared paint-only `eh::shell::launchpad::paint_launchpad_dock_button` (pure cairo, process-local icon cache; `Host` delegates; icon members + dtor cleanup removed). The taskbar paints its Launchpad slot via the helper; `taskbar_.launchpadHost` deleted.
- **Known behavior change:** the old in-process Super handler fell back to a taskbar-only smenu; the child now toggles only the dock's smenu/app_drawer. Taskbar-smenu users use the taskbar button or `eh-ipc menu-toggle`.
- **Build:** `horizon-dock` executable + `dock_spawn.cpp`, `dock_standalone.cpp`, `toplevel_tracker.cpp` in `eh_shell_core`. Verified: debug + release build, `meson test` 2/2 green, clean no-compositor smoke, and a live-session pass with no bugs found.

### `horizon-taskbar` child process (Phase 3 item 4)

The taskbar bar was extracted from the supervisor into a `horizon-taskbar` child, completing the Phase 3 item 4 split. The supervisor keeps the toplevel tracker + `ToplevelBridge`, gamma, lockscreen/idle/polkit, and Super+S; the child owns the taskbar surfaces, app drawer + its popups, tray, weather, MPRIS, settings watcher, and its own toplevel tracking.

- **Child** (`src/desktop_shell/taskbar/standalone/` + `standalone_main.cpp`): own `WaylandConnection` + `TaskbarApp`, own 1 s poll timer, tray + mpris + settings-inotify loop fds, weather engine, pidfile, parent-death guard. IPC client subscribes `config.applied` (reload → `taskbar_maybe_reload_settings` → schedule frame, nightlight paint-state sync) and `command.request` (`settings.toggle`, `taskbar.menu.toggle`).
- **Own toplevel tracking:** the child binds its own `zwlr_foreign_toplevel_manager_v1` (+ Hyprland `ext_foreign_toplevel_list_v1`) on its own connection — the same pattern real-session-verified for `horizon-dock` — so the running-apps snapshot is live without the supervisor's `ToplevelClient` bus mirror. `ToplevelClient` stays available for non-bar components; if a live session ever shows stale windows (wspace handle invalidation on secondary connections), the fallback is `ToplevelClient` + `window.activate|minimize|close` command bus resolved by `ToplevelBridge`.
- **Nightlight over the bus:** the app-drawer NightlightButton (the child has no gamma client) publishes `command.request` `nightlight.toggle`; a new supervisor bus self-client (`ipc_self_client_`, since the server itself never receives publishes) flips the single supervisor-owned gamma client (fixed 4000 K on enable, same as the old in-process toggle). Paint state re-syncs from the `config.applied` broadcast. Hook: `taskbar_set_nightlight_toggle_fn` in `taskbar.{cpp,hpp}`.
- **New `taskbar.menu.toggle` command:** distinct from the dock child's `menu.toggle`, so a single Super-key press or command cannot toggle both bars' start menu/app drawer. The supervisor's `menu-toggle` IPC handler forwards `taskbar.menu.toggle` only.
- **Spawn/reap:** `taskbar_spawn.{hpp,cpp}` (`posix_spawnp`, sibling-binary resolution, stdio → `horizon-taskbar.log`, stale pidfile + `/proc/<pid>` comm/exe check, SIGTERM→SIGKILL→reap); SIGCHLD reap + 5 s rate-limited respawn in `build_handlers()`, shutdown kill. Mirrors `horizon-dock`.
- **Session rewiring:** in-process taskbar deleted — `TaskbarApp` member, `taskbar_deferred_init_done_`, `taskbar_poll_timer_fd_`, `mpris_event_fd_`, the taskbar's hyprland runtime fd, `rebuild_taskbar_layers`/`create_taskbar_layers`/`restart_taskbar`, and the taskbar `PollMuxDisplay` registration. The `config.applied` hook is now embed redraw + nightlight + broadcast; the drag-preview paint tick was removed (both halves live in the children now). `sync_widget_registry` reads the taskbar widget lists from the config snapshot instead of the in-process app.
- **Own settings watcher:** the child opens its own inotify (`open_state_inotify()`/`drain_inotify`) and reloads raw file edits with `shell_config_snapshot_skip_matugen()`, matching the dock/desktop children.
- **Known behavior change:** a taskbar wayland protocol error now terminates + respawns the child (5 s rate limit) instead of an in-process restart; `menu-toggle` no longer touches the dock's menu.
- **Build:** `horizon-taskbar` executable + `taskbar_spawn.cpp`, `taskbar_standalone.cpp` in `eh_shell_core`. Debug + release build clean, `meson test` 2/2 green. Live-session taskbar check pending (the §0.11 decision-1 toplevel-tracking risk).

### Overview — Full Rewrite (Parts 1–3)

- **New scrollable workspace-card overview** replacing the stale one-shot thumbnail grid. `overview.md` documents the design research (client-side clone of the hyprland-scroll-overview plugin); plugin-port and hybrid approaches were explicitly rejected.
- **New data model:** `OverviewWindow` (addr, appId, title, monitor-relative geometry, floating/focused/special), `OverviewWorkspace` (id, label, monitor, capture), `OverviewLayout` (axis, `scale=0.5`, cardW/H, pitch, close button, app grid), `OverviewColors`, `OverviewCardRect` — plus `overview_host.hpp`/`overview_painter.hpp` reorganized around them.
- **Rendering:** zoom-out open effect (cards shrink from full-monitor), per-appId FNV-1a hash → stable hue tint for placeholder tiles, radial-gradient + grid-line procedural backgrounds, window tiles with icon + title pill, focused accent border, hover lift, drop-target ring, close button, workspace label pill, topmost drag ghost at 0.9 alpha.
- **Interaction:** wheel/trackpad scroll with per-frame exponential chase (`kScrollEaseRatePerSec=32`, settle 0.25px, no overshoot), click-to-switch workspace, click-window-to-activate, close buttons, and real drag & drop (8px threshold) via `dispatch movetoworkspace <id>,address:<addr>`; non-Hyprland falls back to activate-target-then-activate-window.
- **Hyprland IPC data refresh** (`refresh_from_hyprland()`): `j/workspaces` + `j/clients` + `j/activeworkspace`, with a dock fallback using workspace strip poll + foreign toplevels.
- **Part 2 — screencopy workspaces:** new `capture_workspaces()` one-shot batched screencopy of every ready output before the layer surface is created (so the capture never contains the overview), matched to workspaces by monitor name; real BGRA covers the card in "Screencopy" mode (window tiles skipped); high-depth integer formats now clamp directly to 8-bit instead of the HDR tone-map path. Captures survive the 2s periodic refresh.
- **Live view (Part 3):** new `protocols/hyprland-toplevel-export-v1.xml` (Hyprland's window-export protocol, v2) + `ToplevelStream` (`src/wl/capture/toplevel_stream.{cpp,hpp}`) which streams live window frames (one in flight per window, chained on `ready`, downscaled to 1024px, BGRA, with `ignore_damage`). When "Live updates" is enabled the overview shows live window contents instead of static tiles, aspect-fit with the tinted placeholder filling letterbox space.
- **Drag improvements:** grab offset preserved at drag start, edge auto-scroll (64px zone, 1.6 pitches/s), and deferred activation that runs *after* the layer surface is destroyed so Hyprland can't undo focus on unmap.
- **Launcher settings tab M3 rewrite** (`launcher_tab_m3.hpp`): three child tabs — Launcher / Workspaces / Overview — with sliders for launchpad scale/icon fill/cell gap/folder size/grid columns/rows/slot pill opacity/DPI, workspaces max slots (0–16)/max icons, and overview card scale/card gap/scroll delay/close button size/search width plus Capture mode (Snapshot/Screencopy) and Scroll axis (Vertical/Horizontal) chips. "Workspaces" was removed as its own sidebar entry (absorbed into Launcher).
- **New config fields:** `overview_axis`, `overview_capture_mode`, `overview_card_scale_pct`, `overview_card_gap_px`, `overview_scroll_delay_ms`, `overview_close_btn_size_px`, `overview_search_width_px`, `overview_live_updates` (all persisted in `settings.toml`).
- **Build:** vendored material-color-utilities static lib now builds against absl flat-hash-map with a forced-include PCH (`pch_material_colors.hpp`) fixing the missing `<optional>` include.

### Horizon Colors — Native In-Process Color Engine

- **New native M3 color pipeline** (`src/color/horizon_colors.{cpp,hpp}`): decode wallpaper image → quantize (Celebi: Wu → WSmeans) → score → CAM16/HCT → full M3 scheme, entirely in C++, replacing the external matugen subprocess. Vendors Google's `material-color-utilities` as a git submodule (built into static lib `eh_material_colors`, ~30 upstream `.cc` files), with optional lcms2 and libjpeg support.
- **Palette caching** (`horizon_colors_palette.cpp`): three-tier — in-memory cache keyed by path+mtime+scheme+mode, v3 disk cache under `$XDG_STATE_HOME`, then full generation; publishes directly into `ShellAppearance` `hc*` fields; `[settings-bench]` timing logs.
- **Native templates** (`horizon_colors_templates.cpp`, 1129 lines): in-process mirror of matugen's template output gated on `horizonColorsNative && horizonColorsPaletteOk` — 47 M3 role mappings, btop theme rewrite + `pkill -SIGUSR2 btop`, otter-terminal color sync, and VS Code `settings.json` merge setting "Dynamic Base16 EventShell Dark/Light".
- **External template sync:** new matugen configs/templates for `horizon-files` and `horizon-photo` (`.conf` → TOML float-RGB patching), gated on `matugen_templates.horizon_files/photo` toggles.
- **matugen-parity work:** ICC transform disabled (matugen ignores ICC profiles); `SchemeVibrant` built inline with neutral-variant chroma 10 (bundled MCU uses 12); fixed **112×112 bit-exact Triangle resize** (Rust `image` crate filter reproduction with `volatile` FMA guard) replacing stbir; near-grayscale filter drops colors with chroma < 5 before scoring.
- **zune-jpeg port** (`third_party/zune-jpeg/zune_jpeg.hpp`): header-only C++ port of the Rust zune-jpeg 0.5.12 scalar decoder (baseline SOF0/1/2, Huffman, fixed-point IDCT, YCbCr→RGB) for byte-identical seed extraction vs matugen; staged with `jpeg_decoder.hpp` wrapper for a later commit.
- **Text color rule change:** text is now always forced on/off-white by theme mode, never palette-derived. Matugen and the native engine are mutually exclusive; "Color Engine" is now an explicit third source (see Color Themes below).
- **Tests:** `test_horizon_colors.cpp` (340 lines, decode→quantize→score→scheme coverage) as meson test `horizon_colors_engine`; `stb_impl.cpp` single-TU.
- **Research docs:** `Horizon-colors.md` (1285 lines, full matugen/material-color-utilities internals reference) and `matugen.md` (porting notes).

### System Theming Module

- **Extracted all OS theming out of `icon_cache.cpp`** (−1459 lines) into a dedicated `src/desktop_shell/common/system_theming/` module (`eh::theming`): GTK (`gtk_theming.cpp`, 558 lines — gsettings + settings.ini apply, `@define-color` extraction with libadwaita/legacy names, CSS selector-scan fallback for colors and design metrics, memoized caches), Qt (`qt_theming.cpp`, 318 lines — qt6ct/qt5ct style + color-scheme apply to conf and kdeglobals, style plugin scanning, preview palettes), Plasma (`plasma_theming.cpp`, 407 lines — plasmarc/kdeglobals theme apply with `[Colors:*]` section copying), and Cursor (`cursor_theming.cpp`, 187 lines — Xcursor themes via dynamic `dlopen("libXcursor.so.1")` with alias mapping).
- `icon_cache.cpp` now handles icons only, with shared `run_cmd_capture`, `read_ini_kv`/`update_ini_key`, and `icon_base_dirs()` helpers moved to `system_theming_utils.cpp`.
- **Desktop entry resolution fallback index** (`desktop_entries.cpp`): lazy-built index over all XDG `applications/` dirs for `X-Flatpak-RenamedFrom` legacy ids and reverse-DNS trailing segments (e.g. `resolve` → `com.blackmagicdesign.resolve`), consulted as a final fallback in `find_desktop_file_for_appid()`.
- **Pins persist canonical ids:** dock and taskbar pin toggles now resolve to the canonical `.desktop` stem (e.g. `sh.cider.Cider`) instead of compositor-dependent WM_CLASS ids (deduplicated helper `dock_pin_store_id`).

### Control Center — Rebuild & Per-Widget Modules

- **Monolith split:** `control_center_paint.cpp` cut from ~2,400 to ~200 lines; per-domain widget modules extracted into `controlcenter/widgets/`: audio (+269), bluetooth (+69), mixer (+161), network (+153), and a 1054-line **weather widget** with a full async Open-Meteo engine (curl-multi driven by an eventfd, JSON parsed by hand, geocoding via geocoding-api.open-meteo.com, tri-provider IP auto-location with 24h/5min caching, 32-entry LRU cache with 10-min TTL, background 10-min service thread).
- **Audio device persistence:** `control_center_persist.{cpp,hpp}` saves the selected sink/source to `$XDG_STATE_HOME/ControlCenter/controlcenter.toml` (atomic temp-file rename); the saved defaults are re-applied as the "pending" selection each time the control center opens (with a 900ms ignore window so the pipewire default-change doesn't clobber the highlight).
- **Wi-Fi connect flow:** `control_center_wifi_connect()` drains `pending_secret_request()`, queues the secret, and calls `activate_ap()` with real error surfacing ("Password required"/"Network not found"/"Failed to start connection").
- **Shared paint utils:** `control_center_paint_utils.hpp` with glass-card and status-pill painters, UTF-8-aware truncation, and `kSurfR/G/B`, glass fill/stroke constants.
- **Panel/popup geometry:** output+input device panels are now **stacked** full-width rows below the cards (not overlapped via `max`), icon badges switched to 28×28 rounded squares, mixer collapsed pill gets a dashed accent affordance, media card uses an accent→dim gradient with a filled play/pause button.
- **Modal-state refactor:** control-center geometry/dispatch/hit-testing rebuilt around a `ControlCenterActiveModal` state instead of per-panel booleans (`control_center_anim.hpp` slimmed to just `cc_ease01`; `popup_open_control_center` 10-arg → 3-arg); dock/taskbar control-center integration rebuilt on the shared helpers.

### Hyprland Settings — UX Redo

- **Trackwise layout engine** (`hyprland_m3_layout.hpp`): `HyprlandCardLayout` lays out section cards in a centered 68% column with toggle rows (`kHyprToggleRowH=60`) before slider rows (`kHyprSliderRowH=68`), header zone `kHyprSectionHeaderH=52`, card gap 12; `beginGroup/endGroup/skipSection/rowTop/sectionCardH` keep paint and hit geometry in lockstep (a `skipSection` hit path). Shared `hyprland_paint_slider_card` / `hyprland_paint_toggle_card` / `hyprland_paint_anim_card` (with 3-dot animation menu) helpers replaced ~650 lines of per-file duplicated lambdas across all 13 M3 files.
- **Every sub-tab restructured** with titles + subtitles, e.g. decoration (8 cards: Rounding && Opacity, Blur, Blur Advanced, Dim, Shadow, Glow, Motion Blur, Misc), input (Keyboard, Mouse, Touchpad, Gestures, Window Behavior), general (Window && Gaps, Snap, Tearing, Border Behavior, Layout), animations (master toggle + one card per category), cursor, dwindle, master, misc, binds, group, quirks, scrolling, xwayland.
- **Child tab bar:** `kTabW` 108, gap 8, glassy with accent-selected tabs; layout dropdown now a real card row (`hyprlandLayoutComboX/Y`, width 140) with its own click-to-open/select handler.
- **Lua parser rewrite** (`settings_hyprland_data.cpp`): section-stack based dotted-key parsing replacing the brace-depth state machine; recursive `write_nested_entries` for arbitrary nesting; `escape_lua` returns bare true/false/numbers and quotes everything else.
- **Fix commit (`e84cea7`):** `accel_profile` corrected from int to string (`"adaptive"`/`"flat"`/`"custom"`, validated on read, written via `escape_lua`, shown as a named value-pill slider); `initial_workspace_token_timeout` default 10 and `render_unfocused_fps` default 15 with `>=1` clamps (hyprland rejects 0) — both exposed as new sliders in the "VRR && Wallpaper && Timing" card (1–240 and 1–120 ranges).
- **Earlier expansion (from `6ababb6`):** four new child tabs — **Group** (8 toggles), **XWayland**, **OpenGL**, **Quirks** (15 total) — plus a scrollable child-tab bar with auto-center-on-click, and expanded M3 controls (snap monitor gap, border grab area, resize corner, cursor zoom/warp, ecosystem toggles, glow/motion-blur decoration, touchpad tap-to-click/flip, etc.).

### Hyprland Layout Settings — 0.56 Alignment

The Scrolling, Master, and Dwindle layout tabs were re-audited against the official Hyprland 0.56.x wiki and the v0.56.2 source (`src/config/values/ConfigValues.cpp` + `src/layout/algorithm/tiled/{scrolling,master,dwindle}/*`). Any option absent from the 0.56.2 source (confirmed via a full-tree grep) was treated as removed and dropped from the data structs, the Lua read/write paths, and the UI; missing options were added as clean toggle cards or labeled value sliders consistent with the rest of the tabs.

- **Scrolling** (`hyprland_scrolling_m3.hpp`): added `follow_min_visible` slider (0–100%, default 0.4) for how much of a column must stay visible while scrolling; added `wrap_focus` and `wrap_swapcol` toggles (both default true) for focus/swap-column wrap-around; added `direction` as a labeled value slider (left/right/down/up); removed the invalid `no_gaps_when_only` (verified absent from the 0.56.2 scrolling source) from the struct, `read_scrolling()`/`write_scrolling()`, and UI.
- **Master** (`hyprland_master_m3.hpp`): removed invalid `lock_fullscreen` and `no_gaps_when_only` (both absent from 0.56.2 source); added toggles `smart_resizing` (resize direction follows cursor), `drop_at_cursor` (drop dragged windows at cursor position), and `always_keep_position` (keep master position when there are no slaves); added labeled sliders `special_scale_factor` (10–100%), `new_status` (master/slave/inherit), `new_on_active` (before/after/none), `orientation` (left/right/top/bottom/center), and `center_master_fallback` (left/right/top/bottom) — `new_status` and `orientation` were previously in the data struct but never exposed in the UI. **`new_on_active` was retyped from bool to string** (0.56 changed it from a bool to a `before`/`after`/`none` enum) and `new_on_top` default corrected to `false` to match the source.
- **Dwindle** (`hyprland_dwindle_m3.hpp`): removed invalid `no_gaps_when_only`, `no_tiling_border`, and `use_monoscope` (the latter was a dead widget that existed in state but was never painted); added toggles `permanent_direction_override` (persist the preselect direction until changed) and `precise_mouse_move` (drop moved windows at the exact cursor point); added `split_bias` labeled slider (directional/current); `force_split` now renders named value labels (follow mouse/left/right); `split_width_multiplier` range widened to 10–300% and `default_split_ratio` to 10–190% to match the wiki ranges (0.1–3 and 0.1–1.9). **Fixed a latent slider-index bug:** the pointer-handler slider arrays were ordered differently from the paint order, so dragging "Special scale factor" would have committed a split-width change — all slider arrays now match the paint order.
- Build verified with `ninja -C build-release-verify horizon-settings` (clean). The invalid master/dwindle options (`lock_fullscreen`, `no_gaps_when_only`, `no_tiling_border`, `use_monoscope`) are now never read or written.

### Hyprland Settings — 0.56 Alignment (remaining tabs)

The General, Misc, Cursor, Render, OpenGL, Quirks, Group, Decoration, Input, and Animations tabs were re-audited against the Hyprland v0.56.2 source (`src/config/values/ConfigValues.cpp`). Defaults, option presence, types, and value ranges were corrected to match; several UI cards exposed options that were previously only in the data struct or the Lua write path.

- **General:** `extend_border_grab_area` slider range widened to 0–100.
- **Misc:** `force_default_wallpaper` retyped bool→int (default `-1` for "default", plus none/wallpaper 1/wallpaper 2), `initial_workspace_tracking` retyped bool→int (disable/workspace/monitor+workspace), new `middle_click_paste` toggle, `mouse_move_enables_dpms`/`allow_session_lock_restore`/`session_lock_blur`/`screencopy_force_8b` default corrections, `name_vk_after_proc`/`always_follow_on_dnd`/`layers_hog_keyboard_focus`/`enable_anr_dialog` default corrections, `on_focus_under_fullscreen` default 2, `lockdead_screen_delay` default 1000, `anr_missed_pings` default 5.
- **Cursor:** `no_hardware_cursors`, `no_break_fs_vrr`, and `use_cpu_buffer` retyped bool→3-state sliders (disable/enable/auto, all default 2); `zoom_detached_camera`/`hide_on_touch`/`hide_on_tablet` exposed as toggles (previously unexposed); write path emits the int-valued cursor options instead of bools.
- **Render:** `fp16_sdr_tf` default 2→0 and range narrowed to monitor/linear; `use_fp16` default 0→2 (disable/enable/auto slider); new `use_shader_blur_blend` toggle.
- **OpenGL:** new `nvidia_anti_flicker` toggle (default true), emitted on the Lua write path.
- **Quirks:** `prefer_hdr` retyped bool→int slider (disable/prefer hdr/always hdr, default 0) and now emitted via `std::to_string`; `skip_non_kms_dmabuf_formats` exposed as a toggle (verified present in 0.56.2 source).
- **Group:** `drag_into_group` retyped bool→int slider (disabled/enabled/groupbar only); `focus_removed_window` default corrected to true.
- **Decoration:** corrected source ranges — `rounding` 0–100, `dim_special` 0–100%, `blur:size` 0–100, `blur:passes` 0–10, `shadow:range` 0–100, `glow:range` 0–100, `shadow:scale` 0–100%, `motion_blur:samples` 1–64; new `motion_blur:enabled` toggle card and `dim_inactive` relocated from the Motion Blur card into the Dim card; new `vibrancy_darkness` and `popups_ignorealpha` sliders (both previously read/written but not exposed); `screen_shader` now only written when enabled and non-empty.
- **Input:** new **Touch Device** card (`transform` slider, `enabled` toggle), **Virtual Keyboard** card (`share_states` 0–2 labeled slider, `release_pressed_on_close` toggle), **Tablet** card (`transform` slider, `absolute_region_position`/`relative_input`/`left_handed` toggles), and **Tablet Tool** card (`eraser_button_mode`, `eraser_button_override`, `pressure_range_min`/`pressure_range_max` sliders) — all backed by the new structs; `scroll_button`/`follow_mouse_shrink` sliders and `scroll_button_lock` toggle were dead (in state + flush switch but never painted) and are now exposed.
- **Animations:** new `workspace_wraparound` toggle in the Animation Master card.
- **Touchpad:** `tap_to_click`/`tap_and_drag` keys verified correct on the Lua write path — `luaConfigValueName` in 0.56 maps `-`→`_` and `:`→`.`, so `touchpad.tap_to_click`/`touchpad.tap_and_drag` resolve to Hyprland's `input:touchpad:tap-to-click`/`tap-and-drag`.
- Lua roundtrip verified for the new nested device tables (`touchdevice.*`, `virtualkeyboard.*`, `tablet.*`, `tablettool.*`) and empty-string `tap_button_map` via a standalone write→read test. Build verified with `ninja -C build-release-verify horizon-settings` (clean).

### Settings Cleanup & UX Polish

- **`ScrollController`** (`settings_scroll_controller.hpp`): the single place scroll animation/clamping lives — frame-rate-independent exponential easing (`kTauMs=40`), clamp-on-shrink, snap/scroll-by, `animating()`. Wired through `settings_scroll.{cpp,hpp}` (`settings_scroll_px()`), the per-tab clamp paths, event handlers, and the widget picker (scrollable, clipped grid with scrollbar).
- **`SettingsDropdown` component** (`settings_dropdown.{cpp,hpp}`): self-contained combobox owning labels/selection/hover/open state with `hit_trigger`, `hit_row`, `popup_geom` (auto-flips above the anchor on overflow), `paint_trigger`, `paint_popup`. Adopted first by the appearance tab's Matugen scheme/mode combos.
- **Widget picker rewrite** (+618): M3 glass panel (radius 16, alpha 0.96), measured-from-text panel width, uniform card height from the longest wrapped description (clamped 1–3 lines), scrollable clipped grid, **frame-synced search caret** (530ms blink via `wl_surface.frame`), clear-filter chip, accent hover cards with "add" chip, "No matching widgets" empty state, scrollbar thumb, footer hint.
- **Header removal:** the old 56px "Settings" title strip is gone (`kContentTop` 76→16); window buttons paint last on top.
- **New opacity knobs:** per-surface "Notifications" overlay slider (new `overlayOpacityNotifications`, `OverlaySurfaceAlphaKind::Notifications`) and a dedicated **"Settings sidebar"** slider (`overlayOpacitySettingsSidebar`, TOML `overlay_opacity_settings_sidebar`) scaling just the sidebar backdrop; `overlay_title`/`overlay_value` grid extended to 16 entries.
- **Notification toast fixes:** duplicate-card suppression via a `summary+0x1e+body` seen-set; toast opacity tied to the new slider; `tick_animations()` stall watchdog (frame callbacks >250ms) and `maybe_emit_now_playing_notify_assume_locked` dedup.
- **Media compact widget:** card/art/buttons now glassy boxes with the backdrop tied to the "Widget cards" overlay slider.
- **Scroll migration everywhere:** all hyprland/mango/dock/taskbar tabs use `settings_scroll_px(app)` instead of raw per-tab ints; autostart tab hit-tests and overlays now scroll-aware.

### Glass Card Restyle

- **New shared `glass_card_style.hpp`** (`paint_glass_card()`): frosted recipe — chrome color × 0.35 at 0.78 alpha through a glassy `m3::Box` + 1px white border at alpha 0.12 inset 0.5px.
- **Applied DE-wide** (be6e946, 42 files): control center popup, dock bar, taskbar, tooltips, notification toasts, context menus, spotlight, desktop icons menus, polkit auth dialog, calendar/media/weather/world-clock/system-monitor widgets, battery/bluetooth/VPN/weather/media/volume popups, app drawer, start menu, settings cards and sidebar, and the hyprland child-tab bar.
- **Dock pills unified** on `slot_pill_style::paint_pill`; dock/taskbar widget-pill colors changed to `dockFill·0.6 + accent·0.15`.

### VPN — Import & UX Overhaul

- **Real import, fixed:** importing `.conf`/`.ovpn` files now actually creates NetworkManager profiles (was: local parse that only filled fields). WireGuard files with non-interface-name basenames (e.g. `AirVPN_NL-Alblasserdam_…-Entry3.conf`) now import via a sanitized temp name + rename-back; imported profiles get `connection.autoconnect no`. All add/import runs synchronously (~100–300ms) with real nmcli errors surfaced through `std::string& err`; shell-quoting (`sh_quote`), private 0600 temp configs under `$XDG_RUNTIME_DIR`, and "Install the \"<pkg>\" package" hints on missing plugins.
- **Add-VPN dialog rewrite** (`vpn_add_dialog.cpp`, 1236 lines): declarative per-type forms for **WireGuard, OpenVPN, IKEv2, L2TP/IPsec, PPTP, AnyConnect** with masked secret fields (• bullets), plugin-availability detection, per-type import buttons, auto-sizing dialog that hugs the active form and re-centers, Tab-to-cycle-fields, Enter-to-submit, and status/error messaging.
- **In-process X25519 keygen** (TweetNaCl-style field arithmetic) replacing the `wg genkey`/`openssl rand` subprocess.
- **UUID-based reconciliation:** the VPN list previously matched rows **by index**, which crossed identities when a connection toggled active state (the refresh sorts by active). Rows now match by UUID with optimistic toggle state kept until confirmed or 8s elapsed; disconnecting also clears autoconnect so NM won't re-dial. Applied to both the network tab and the dock VPN popup.
- **Auto-connect toggle:** `VpnConnectionInfo.autoconnect` parsed from `nmcli -f AUTOCONNECT`; new `setVpnAutoconnect()` service method; per-row auto-connect `m3::Toggle` in the network tab; `deactivateVpnConnection()` now disables autoconnect after a manual disconnect.
- **Name truncation:** UTF-8-safe "…" ellipsis fitting for long imported profile names.

### Dock & Taskbar

- **Exclusive-zone gaps:** new `dockExclusiveZoneGap` and `taskbarExclusiveZoneGap` (TOML `exclusive_zone_gap`, clamped 0–100) — extra space above/below the bars so tiled/maximized windows rest clear of them; taskbar also gets `taskbarEdgeGap` (`edge_gap`, 0–25) applied in *every* width mode (previously 0 in full mode). Exposed as new sliders in the dock (10→12 items) and taskbar (8→10) appearance tabs.
- **Running-apps tray pill:** new `dockRunningAppsTrayPill` / `taskbarRunningAppsTrayPill` settings paint one pill-shaped background behind consecutive running app icons (mirroring the pinned tray pill), with matching gap/hit-test logic.
- **Liquid glass rim:** dock bar gains a 7-layer liquid-glass perimeter rim (frosted base → vertical shading → white specular top edge → inner bevel → side highlights → tinted bottom lip → faint outer glow), tinted by accent when `dockColoredGlass`; border switches color when any palette source is active (`anyPaletteActive()`).
- **Shell input renamed/relocated:** `dock_input.cpp` → `src/desktop_shell/shared/system/input/shell_input.cpp` (`g_shell_seat_listener`), documenting itself as the shell-wide seat listener.
- Chrome-color `debug_log` traces for dock/taskbar paint.

### World Clock Widget (new)

- **Dock slot** (`world_clock_paint.{cpp,hpp}`, 571 lines): analog day/night faces (light dial 7–19h, dark otherwise), 60 ticks, hour numerals, orange `#ff9f0a` second hand; cities from `city_N_tz/_label/_color` widget settings (Local/UTC fallback); time from a background `timeapi.io` fetch (30-min refresh) with `TZ`+`localtime_r` fallback; offset labels ("Same Time"/"+16HRS") and day labels ("Today/Tomorrow/Yesterday/+N Days"); second-level tick signature redraw.
- **Desktop widget variant** (`desktop_widget_world_clock.{cpp,hpp}`, 365 lines): reads `$XDG_STATE_HOME/event-horizon/WorldClock` TOML (defaults LA/Tokyo/Sydney), analog faces + day/offset labels.
- Registered as `DesktopWidgetType::WorldClock`, `SlotKind::WorldClock`, picker entry, dock/taskbar layout, and dock timer redraw.

#### Settings popups (city editor)

- **Settings city editor** (`world_clock_popup.{cpp,hpp}`): searchable timezone picker popup for the World Clock widget cities — text filter with 530 ms frame-synced caret, scrollable list (1 row per notch, opens at scroll 0, dropdown content dismisses on blank-inside click only), picked tz derives the city label from the last path component (`_` → space), Save persists `city_N_tz`/`city_N_label` to `~/.local/state/event-horizon/WorldClock`. Instant apply: `horizon-desktop` reloads the widget's cities on every paint (30 Hz anim timer) → edits land within ~33 ms.
- **Desktop widget duplicate** (`desktop_world_clock_settings.{cpp,hpp}`): the same editor mirrored into `horizon-desktop`, subscribing an IPC `worldclock.settings` topic (no publisher exists yet, so the desktop-side entry point is currently unreachable).
- **Interaction fixes:** negative control-id guard (previously killed the Save/X/search/clear buttons — buttons were dead), dropdown no longer closes on press (phantom close when clicking the search field — the dialog re-centers as the list collapses), open-at-top instead of mid-list, button renamed Done → Save with centered text + footer gap.

### Color Themes

- **Three-way color source picker:** the mode pills become Matugen / **Color Engine** / Custom Theme, sized to label width (100–170px) with hover highlighting; the pills drive `matugenThemingEnabled` / `horizonColorsNative` / `colorThemeEnabled` exclusivity; preset cards only show for Custom Theme. Real-time hover tracking via new `settings_color_themes_consume_pointer_move()`.

### Themes, Icons & Vendored GTK Theme

- **adw-gtk3 vendored** under `third_party/adw-gtk3/` (light + dark metathemes, GTK3/GTK4 css incl. a 3055-line `libadwaita.css`, ~64 symbolic/slider/text-select assets) and installed to `${datadir}/themes` via `install_subdir`; custom `#settingsDialog` tab styling appended to match the shell's settings dialog.
- **Settings label alignment fixes:** card captions moved `+15` → `+22` across time/bluetooth/keyboard/themes/appearance/desktop/icons/notifications tabs.
- `NewDesign.html`: an "Orbit Control" control-center design prototype committed as reference.

### Build, CI & Docs

- **build.sh rewritten** (358 → 73 lines): distro detection limited to apt/dnf/pacman with pinned dep lists, then `exec sudo just install` — all meson/ninja orchestration delegated to the justfile (new `test` recipe added).
- `Docs/Debian-Ubuntu.md` deps synced (`libaudit-dev`, `libpng-dev`, `liblz4-dev`, `zlib1g-dev`, `libdrm-dev`, `cmake`); `Docs/Fedora.md` updated.
- CI runs `meson test -C build-debug --print-errorlogs`.
- **no-RSVG build fixes:** unused-parameter suppressions in `asset_loader.cpp` and `settings_tab_wifi.cpp` (RSVG-guarded icon path).
- `matugen/configs` + templates for horizon-files/horizon-photo; `third_party/material-color-utilities` submodule.

## [0.9.2] - 2026-07-01

### Settings Coordinate & Hit-Testing Fixes

- **Double-scroll in M3 dock dispatch:** Removed redundant `+ scroll` from `handlePointerDown`/`handlePointerUp`/`handlePointerMove` in `settings_tab_dock.cpp`. Callers already pass document-space Y via `settings_logical_content_y_tab01()` — adding scroll again corrupted all hit-testing when scrolled.
- **Double-scroll in M3 taskbar dispatch:** Same fix applied to `settings_tab_taskbar.cpp` for the taskbar's three dispatch functions.
- **Qt Color Scheme hover coordinate mismatch:** `on_pointer_motion()` was comparing `pyL` (document-space Y) against `ly` (screen-space Y) for the Qt scheme dropdown hover row, causing highlights to track the wrong row. Changed to use `app.pointerY` (screen-space Y), matching the click handler.
- **M3 widget drag ghost Y coordinate:** Drag ghost `widgetDragGrabDy` was computed from `app.pointerY` (screen space) but painted inside a cairo context already translated by `-scroll`. Changed to use `py` (document-space Y, already computed) in `dock_tab_m3.hpp` and `taskbar_tab_m3.hpp`.
- **Scroll wheel missing content-area bounds check:** Tabs 0, 1, and 11 processed scroll wheel events regardless of pointer position, causing content to scroll when hovering outside the content column (e.g. over nav buttons or padding). Added the same `app.pointerX < tcx || app.pointerY < kContentTop` guard used by all other tabs.
- **Matugen scheme/mode dropdown hit position:** Click handler in `settings_event_handlers.cpp` used wrong `matugenRowTop` value (`kContentTop + kSpacingL + 46` instead of `kContentTop + kDockChildTabH + 12`), offsetting hit detection by ~16px.

### Settings Additional Fixes

 - **Font installation:** Added `install_data` for Inter and Fira Code fonts (8 files) to `meson.build` so bundled fonts are deployed to `$datadir/event-horizon/assets/fonts/` at install time.

### Audio Visualizer — Smooth Catmull-Rom Waveform

- **Flat bars → organic waveform:** Replaced static rendering with a 48-point Catmull-Rom spline (24 cava bands mirrored left/right) producing smooth, continuous wave curves with spiky transient response
- **Asymmetric smoothing:** Fast attack (20ms τ) snaps to beat transients instantly; slower decay (60ms τ) produces graceful fall — each band smoothed independently
- **Visual layers:** Vertical gradient fill below wave (0.20 → 0.05 → 0.0 opacity), 4-pass glow on wave crest (0.06 per pass), peak marker dots that linger and decay at 0.5/s
- **Idle breathing:** Gentle sine pulse when playback is paused — keeps the widget visually alive
- **Wave height:** Scaled from 22% → 30% → 48% → 58% of card height for maximum visual impact
- **Hann window removed:** Spikes remain visible edge-to-edge across the full waveform width

### Cava Subprocess — Inline Config, Higher Resolution

- **Config pipe bug fixed:** Cava's inline config was written to a closed pipe fd (write end closed before writing). Config now written from the parent process after fork — cava actually receives noise_reduction, monstercat, gravity settings
- **Bars:** 6 → 12 → 24 bands for maximum frequency resolution
- **Smoothing set aggressively:** `noise_reduction=0`, `integral=50`, `gravity=100`, `monstercat=2.0`, `sensitivity=50`, `framerate=60`

### Desktop Widget Animation Layer

- **30fps paint timer:** Added a second timerfd (33ms interval) alongside the existing 1s timer. The 1s timer handles `on_second_tick()` (MPRIS polling, create calls); the 33ms timer calls `paint_all_layers()` + `wl_display_flush()` for smooth animation
- `tick_cava()` only called during playback; idle state generates breathing animation

### Configurable Cava — Real-time TOML Reload

- **Config file:** `$XDG_STATE_HOME/event-horizon/cava/cava.toml` exposes all cava parameters (bars, framerate, sensitivity, cutoffs, smoothing) and visualizer parameters (wave height, attack/decay τ, fill/glow/peak opacities, idle amplitude, blob τ/opacity)
- **Real-time reload:** Config file mtime checked every ~1s via the existing timer. Cava-relevant field changes restart the cava subprocess; visualizer-only changes apply instantly
- **Default config file** created at the path above with all fields documented

### Build

- Added `cava_config.cpp` to meson source list
- Removed stale `todo/` directory

### Taskbar Pinned-App Drag Reorder Fixes

- **Reorder silently no-op in panel layout mode:** `taskbar_pin_drag_motion()` used `firstLeft == 0.0` as an "uninitialized geometry" sentinel, but `startX` (and thus `firstLeft`) is legitimately `0.0` whenever the taskbar renders in panel layout mode (`taskbar_paint.cpp:601`). This silently aborted every motion event, so `pinDragInsertIdx` never left `-1` and nothing ever committed. Replaced with an explicit `pinDragGeometryValid` flag computed once at drag-init from the actual pinned-hit count, in `taskbar_pin_drag.cpp`/`taskbar.hpp`.
- **New order reverted before it reached disk:** `taskbar_draw()` unconditionally opens with `taskbar_maybe_reload_settings()`, which does `app.settings = next` whenever `app.settings.pinnedApps` differs from the global config snapshot. The release handler drew before saving, so the freshly committed reorder sat in `app.settings` for one draw call while the global snapshot was still stale — the reload logic stomped it back to the old order before `taskbar_save_settings()` ever ran. Reordered `taskbar_pointer_button()` in `taskbar.cpp` to save (refreshing the global snapshot) before drawing.
- **Intermittent frozen drag preview:** `taskbar_schedule_frame()` requested a `wl_surface_frame` callback without checking that the target surface was actually configured, unlike the dock's equivalent (`dock_pick_frame_surface_ready`). On some compositors this meant `wl_callback::done` never fired, permanently stalling all further redraws for the rest of the drag even though the reorder math kept computing correctly underneath. Added `taskbar_pick_frame_surface_ready()` plus a 100ms stall watchdog (`frameCallbackRequestedMs`) that abandons a stuck callback so scheduling can recover, in `taskbar.cpp`/`taskbar.hpp`.
- **Slow drag performance:** Every pointer-motion event during a pin drag called `taskbar_draw()` synchronously, forcing an unthrottled full repaint per event. The dock already vsync-throttles its own pin-drag redraws via `dock_schedule_frame`; the taskbar had the equivalent (`taskbar_schedule_frame`/`frameRedrawPending`) but nothing ever used it. Wired `taskbar_pin_drag_motion()` in `taskbar_pin_drag.cpp` to schedule a coalesced frame instead of drawing synchronously.

## [alpha 3.2] - 2026-07-01

### Settings Coordinate & Hit-Testing Fixes

- **Double-scroll in M3 dock dispatch:** Removed redundant `+ scroll` from all three dispatch functions in `settings_tab_dock.cpp`.
- **Double-scroll in M3 taskbar dispatch:** Same fix applied to `settings_tab_taskbar.cpp`.
- **Qt Color Scheme hover coordinate mismatch:** Changed `pyL` to `app.pointerY` in `settings_event_handlers.cpp` so hover row detection matches screen-space coordinates.
- **M3 widget drag ghost Y coordinate:** Fixed drag ghost tracking in `dock_tab_m3.hpp` and `taskbar_tab_m3.hpp` by using `py` (document-space Y) instead of `app.pointerY`.
- **Scroll wheel content-area bounds check:** Added content-column bounds guard for tabs 0/1/11 in `settings_scroll.cpp`.
- **Matugen scheme/mode dropdown hit position:** Corrected `matugenRowTop` offset in `settings_event_handlers.cpp`.

### Settings Additional Fixes

- **Font installation:** Bundled Inter and Fira Code fonts now properly installed via `meson.build`.

## [0.9.1] - 2026-06-17

### Color Themes System

- **Built-in preset library:** 279 curated + seed-generated + mass-produced presets covering Catppuccin (Latte, Frappé, Macchiato, Mocha + accent variants), Google Material (Light/Dark), Gruvbox (Dark/Light), Rosé Pine (Pine/Moon/Dawn), Google Brand (Dark/Light), Dracula (Dark/Alucard), Nord (Dark/Light), Tokyo Night (Dark/Storm), Monokai (Dark/Soda/Vivid/Pro/Night), Solarized (Dark/Light), Everforest, Kanagawa, Ayu (Mirage), Atom, Night Owl, Sonokai, Andromeda, and 266 Ptyxis terminal-to-M3 converted palettes
- **Category grouping:** Presets organized by source family (catppuccin, google, gruvbox, rose-pine, dracula, etc.) with dark/light sub-sections within each group
- **Theme application:** `apply_theme_to_app()` — applies M3 palette (primary, secondary, tertiary, error, surface, outline) from any built-in or user preset
- **User themes:** JSON-based user theme files scanned from `~/.config/event-horizon/themes/` with `scan_user_themes()` / `load_theme_from_file()`
- **Theme persistence:** `colorThemeEnabled`, `colorThemeName`, `colorThemeSource`, and all 16 palette floats serialized to settings JSON
- **Matugen integration:** Color themes live alongside matugen theming — toggling "Matugen" vs "Custom Theme" via mode pills

### Settings "Opens Blank" Bug Fixes

Four independent root causes identified and fixed:

1. **Deferred draw with no retry:** When both SHM buffers were busy and no animation was active, `pendingRedraw` was set but no `wl_surface.frame` callback was scheduled, leaving the window blank up to 30 seconds. Fix: always schedule a frame callback in the deferred path (`settings_app.cpp:709`).
2. **Pending redraw skipped after toggle:** `embed_after_display_dispatch()` had an early `return` after processing `pendingToggle`, skipping the `pendingRedraw` check (`settings_embed_lifecycle.cpp:711`).
3. **Stale chrome config on reopen:** `destroy_settings_surfaces()` didn't reset `settings_paint_shell_snapshot_valid`, causing the first draw after reopen to reuse a stale `ShellConfig` (`settings_embed_lifecycle.cpp:138`).
4. **Vulkan transient failure:** `settings_present_vk_raster()` transient failure caused `draw()` to return without committing any buffer. Fix: copy rendered content from staging buffer to SHM and commit immediately (`settings_app.cpp:1597`).

### Additional Fixes

- **DE crash with Rs3:** Fixed entry-point crash when Rs3 (a Rust-based compositor) is detected — added null-safety for compositor detection path and session initialization guard
- **Settings scroll improvements:** Smooth scroll convergence, per-tab scroll state clamping, fixed scroll-px overflow on tab switch
- **Vulkan swapchain:** Fixed surface loss handling in `vulkan_wayland.cpp` — proper swapchain recreation on output hotplug
- **Dock bar:** Improved pinned app slot calculation for edge-to-edge dock mode
- **Taskbar:** Added window state diffing optimization — only repaint dirty regions
- **Session lock:** Fixed lock surface activation race — defer surface commit until frame callback
- **Shell config:** Added color theme config fields (`colorThemeEnabled`, `colorThemeName`, `colorThemeSource`, 16 palette hex values)
- **Settings common:** Added `settings_group_header()` for categorized section headers in settings tabs

### Version

- Bumped from 0.9.0 to 0.9.1

## [0.9.0] - 2026-06-13

### Horizon File Manager — Wayland-Native File Browser

- **Full file browser with:** Grid/list view toggle, path bar with clickable segments, sidebar with favorites/drives/bookmarks, drag-and-drop file operations, recursive search with worker thread, thumbnail previews for images, video (ffmpegthumbnailer via cmake build), PDF (poppler-glib), SVG (nanosvg), and EPUB
- **Navigation system:** Directory tree with collapse/expand, breadcrumb path bar, forward/back history, bookmark management, favorite folders
- **Computer view:** Mounted drives, filesystem roots, network locations displayed in collapsible sidebar sections
- **File operations:** Copy, move, rename, delete with confirmation dialogs; compression/extraction via libarchive
- **Progress dialog:** Operation progress with cancel support for long-running file operations
- **Drive mounting:** UDisks2 DBus service integration for automatic mount/unmount of removable media
- **Media preview:** Video frame extraction via ffmpegthumbnailer built from source, image gallery preview with zoom
- **Search:** Recursive filename search running in background worker thread with result streaming
- **Context menu:** Right-click actions (open, copy, cut, paste, rename, delete, properties, compress/extract)
- **Rename UI:** Inline rename overlay with validation
- **Settings persistence:** Show hidden files, default sort column/direction, view mode saved across sessions via JSON config
- **Embedded terminal:** Launch terminal in current directory
- Packaged as standalone `horizon-files` Meson project with archive viewer subdirectory

### Horizon Shot — Wayland Screenshot & Annotation Tool

- **Capture modes:** Region selection, active window, full screen (via wlr-screencopy + ext-data-control-v1 protocols)
- **DRM/KMS capture:** Direct display capture through DRM for KMS-available outputs
- **Image formats:** PNG, JPEG, JXL (HDR), 16-bit PNG with HDR/SDR tone mapping
- **Annotation overlay:** Draw, highlight, arrow, rectangle, ellipse, text annotation tools after capture
- **Quick editing:** Crop, resize, color adjustments (brightness, contrast, saturation) before save
- **Clipboard integration:** Auto-copy captured image to Wayland clipboard
- **Standalone desktop entry:** `horizon-shot.desktop` with application launcher integration
- Packaged as standalone `horizon-shot` Meson project

### Sub-Module Packaging & CI

- Added both projects as proper git submodules under `packages/` tracking the `Stable` branch
- **GitHub Actions:** Full pipeline — build debug+release, install to staging (staging-shot-*, staging-files-*), version extraction from each sub-project's own meson.build, packaging in Arch (pacman/zstd), Debian (deb), and RPM formats with separate debug symbol packages
- **CircleCI:** Identical multi-stage pipeline with workspace persistence, version files, and per-format packaging jobs
- **Root config.yml:** Mirrored CircleCI pipeline for redundancy
- **PKGBUILD:** Converted to Arch split-package producing `event-horizon-de`, `horizon-shot`, and `horizon-files` from a single PKGBUILD
- **Auto-update:** Each CI build fetches latest `Stable` branch tip via `git fetch origin Stable + git checkout origin/Stable`
- **Nested submodules:** Horizon-files' own submodules (tomlplusplus, nanosvg, stb, ffmpegthumbnailer) initialized recursively during CI
- **Dependencies:** Added `cmake` to build containers for ffmpegthumbnailer compilation; resolved include path issues for nested third_party

### Settings Toggle Accent Color Fix

- Replaced all hardcoded emerald-green toggle accent colors (`r=0.396, g=0.792, b=0.412`) with matugen-resolved accent colors across 22 files:
  - **Power tab:** `power_toggle_emerald()` now uses matugen `a_r/a_g/a_b` instead of constants
  - **Appearance tab:** Removed file-scope `eaR_/eaG_/eaB_` hardcodes; both paint functions now read `accR/accG/accB` from `settings_resolve_colors()`
  - **Hyprland tab — 13 m3 files:** All toggle/switch components use `accentR_/accentG_/accentB_` member variables, populated via `syncColours(app)` at paint time
  - **Network tab — 5 m3 files:** Wired IPv4/IPv6/Ethernet/Security/Advanced toggles switched to resolved accent
  - **Taskbar tab — `taskbar_tab_m3.hpp`:** 7 toggle rows updated
  - **Dock tab — `dock_tab_m3.hpp`:** 5 toggle rows updated

### Live Wallpaper

- **Gallery system:** New `gallery.cpp`/`gallery.hpp` module with `LiveWallpaperVerticalMetrics`, `LiveWallpaperTabLayout`, `live_wallpaper_vertical_metrics()`, `live_wallpaper_tab_layout()`, `live_wallpaper_destroy_thumbs()`, `live_wallpaper_clamp_page()`, `live_wallpaper_ensure_wallpaper_gallery()` with mtime-based stat debouncing (500ms) to avoid redundant folder rescans
- **Thumbnail management:** LRU eviction, separate blurred-thumb cache for 18+ marked content, disk-backed blurred thumbnail cache (`lw_load_cached_blurred_thumb`/`lw_save_blurred_thumb_cache`), video frame extraction for hover preview
- **Performance:** Folder rescan only triggers on mtime change, async thumbnail pre-caching via `WallpaperThumbnailService`, lazy hero surface invalidation
- **Sort modes:** Name, Oldest, Newest with sort state tracking (`liveWallpaperGallerySortMode`)
- **Settings serialization:** UI preferences (opacity, scale, columns, rows, thumb radius, sort mode) persisted and restored; `lw_save_settings()`/`lw_save_ui_config()`
- **Adult content marking:** Right-click context menu toggles 18+ flag, blurred preview with box-blur radius 100, disk cache for blurred thumbnails
- **Gallery hit testing:** Per-thumb click detection matching the grid math from `live_wallpaper_tab_layout()` — both left-click selection and right-click context menu now work (was previously stubbed, always selected first item)
- **Gallery navigation:** Prev/Next page button hit testing with page clamping (was previously stubbed)
- **Hover effects:** Scale animation (1.0→1.10→1.0) with easing curves, accent highlight overlay, video hover preview frame advancement with timing
- **Video wallpaper:** `live_wallpaper_apply_video_with_matugen()`, `live_wallpaper_start_hover_video_preview()`, frame-by-frame playback in hover popup
- **Scroll support:** `liveWallpaperScrollPx` for settings tab content scrolling
- **Layout engine:** `compute_lw_layout()` with dynamic scaling (0.4x–2.5x based on window size), hero/controls/grid proportional layout, `LiveWallpaperLayout` struct with margin/tab/content/hero/ctrl/gallery dimensions
- **Theme colors:** `lw_resolve_colors()` supporting both matugen chrome paints and fallback dark theme (`BgR=0.102, BgG=0.075, BgB=0.188, AccR=0.769, AccG=0.659, AccB=0.941`)
- **M3 UI rendering:** Cards with 14px radius, combo boxes, dropdown popups with hover tracking, pill-style sort buttons, outlined accent buttons, glass highlight surfaces
- Cleaned up file browser code separation — removed `src/ux/file_browser/` and `src/ux/screenshot/` directories from main tree, ported gallery helpers into live wallpaper codebase

### Control Center — Major Rework

- **New dispatch system:** `control_center_dispatch.cpp`/`.hpp` — centralized event routing for pointer move, click, scroll, and keyboard input
- **Popup dispatch framework:** `popup_dispatch.cpp`/`.hpp`/`popup_items.cpp`/`.hpp` — reusable popup item system with icon+label rows, toggle items, slider items, button items
- **Keyboard navigation:** Spotlight search keyboard handler (`spotlight_keyboard.cpp`/`.hpp`), app drawer keyboard handler (`app_drawer_keyboard.cpp`/`.hpp`), tab/enter/escape navigation
- **App drawer dispatch:** `app_drawer_dispatch.cpp`/`.hpp` — separate input module for app drawer interactions
- **Dock slot dispatch:** `dock_slot_dispatch.cpp`/`.hpp` — per-slot click/hover handling extracted from monolithic dock input
- **Animation system:** Improved `control_center_anim.hpp` with smoother panel transitions
- **Panel geometry:** Refined `control_center_panel_geometry.cpp` for better popup positioning
- **Paint improvements:** Bluetooth widget paint (`bluetooth_paint.cpp`/`.hpp`), control center popup paint refinements
- **Margins system:** New `margins.cpp`/`.hpp` for consistent popup margin calculations
- **Toplevel hooks:** Refactored toplevel window hooks into shared module
- **Widget bar:** Slot hooks for widget-level dispatch

### Settings Application Improvements

- **Wallpaper tab:** Major performance refactor — config serialization rewritten (`settings_serialize.cpp` — 521 lines changed), wallpaper path resolution streamlined, async thumbnail loading, removed redundant stat calls
- **Live wallpaper tab:** Adapted for standalone live wallpaper separation, UI config persistence
- **Dock tab:** Updated widget bar slot references, toggle accent colors fixed
- **Taskbar tab:** Updated widget bar slot references, toggle accent colors fixed
- **Desktop widgets tab:** Updated for config refactoring
- **Icons tab:** Minor include fix
- **Accounts tab:** Fixed null-pointer crash when `getenv("HOME")` returns null during avatar `cp` command; moved user name, `@username`, and "Choose Image" button to far right edge of card (right-aligned via Pango text measurement) — no more overlap with avatar placeholder
- **GPU settings:** Updated for new config structure
- **Widget picker:** Major cleanup (447 lines removed) — simplified widget catalog
- **Event handlers:** Updated scroll targets, active field management, input routing for new dispatch system

### Desktop Shell

- **Dock:**
  - New `dock_slot_dispatch.cpp`/`.hpp` — extract slot click/hover from monolithic `dock_input.cpp` (1675 lines removed from dock_input)
  - Dock pinned app storage and reordering improvements
  - Tooltip positioning fixes
  - Widget bar paint and dispatch updates
  - Trash state module moved to shared `fs/trash_state.cpp`/`.hpp`
- **Taskbar:** Core taskbar logic refinements, paint updates for group popups
- **App drawer:** New `app_drawer_dispatch.cpp`/`.hpp` + `app_drawer_keyboard.cpp`/`.hpp` — extracted input handling and keyboard navigation
- **Spotlight search:** `spotlight_keyboard.cpp`/`.hpp` — keyboard-driven app search with arrow navigation
- **Popup system:** New `popup_dispatch.cpp`/`.hpp`/`popup_items.cpp`/`.hpp` — generic popup item rendering and event dispatch, session management improvements
- **Bluetooth widget:** Paint and layout improvements
- **Config watch:** Debounced file-watching for config changes
- **Namespaces:** Centralized namespace declarations in `namespaces.hpp`
- **Vulkan/Wayland:** `vulkan_wayland.cpp` improved swapchain handling (159 lines added)
- **Debug logging:** Enhanced `debug_log.hpp` with structured logging support

### Configuration & Serialization

- `shell_config.cpp`/`shell_config.hpp` — major rewrite (521 lines changed): streamlined path resolution, removed redundant fields, improved settings group handling
- `settings_serialize.cpp` — rewritten JSON serialization (521 lines changed) for new settings structure
- `settings_app_types.hpp` — added fields for new dispatch/input systems
- `settings_embed_lifecycle.cpp` — updated embed lifecycle for config changes
- Removed `settings_serialize.hpp` obsolete declarations

### Build System

- **Meson:** Removed file_browser and screenshot source entries (moved to submodules), removed tomlplusplus subproject wrap dependency, updated for new directory structure
- **build.sh:** Updated for submodule-based builds, added horizon-files/horizon-shot build stages
- **PKGBUILD:** Converted to split-package: `event-horizon-de`, `horizon-shot`, `horizon-files` each with own version and dependencies
- **justfile:** Removed (replaced by build.sh)
- **Submodules:** Removed inline `subprojects/tomlplusplus-3.4.0/` and `subprojects/tomlplusplus.wrap`, switched to git submodule; added nlohmann/json cirrus CI config; registered horizon-files and horizon-shot submodules

### CI/CD

- **GitHub Actions:** Full multi-distribution pipeline — Arch (.pkg.tar.zst), Debian (.deb), RPM (.rpm) with debug symbol variants; version extraction from sub-project meson.build files; shared staging artifacts across packaging jobs; submodule auto-update to latest Stable branch
- **CircleCI:** Parallel pipeline with workspace persistence, per-format packaging jobs, nested submodule init for horizon-files
- **Root config.yml:** Mirrored CircleCI configuration for multi-CI redundancy
- **Submodule handling:** Force-populate fallback for incomplete gitlink registrations; `--depth 1` shallow clones for speed; nested `git submodule update --init` for horizon-files dependencies; manual `git fetch origin Stable` + checkout instead of `--remote` to avoid shallow-clone tracking-ref issues
- **Dependencies:** Added `cmake`, `openssl-devel`, `libjpeg-turbo-devel`, `poppler-glib-devel`, `libarchive-devel` to build containers
- **Deps fix:** Removed duplicate deps from meson.build, fixed ffmpegthumbnailer subproject handling

### Documentation

- **Arch Linux build guide:** `Docs/Arch-Linux.md` with full dependency list and build instructions
- **Debian/Ubuntu build guide:** `Docs/Debian-Ubuntu.md`
- **Fedora build guide:** `Docs/Fedora.md`
- **README:** Updated with sub-project references
- **Removed:** `files.md` (447 lines of stale file listings)

### Other Changes

- **Notification service:** Fixed DBus service include path
- **files.md:** Removed stale file listing document
- **Changelog:** Updated for 0.9.0 with detailed commit history

### Full commit history

38 commits since 17899fa (Alpha 2 0.8.0), spanning File Manager and Screenshot Tool integration, CI/CD pipeline overhaul, toggle color fixes, control center dispatch system, dock slot dispatch, keyboard navigation, wallpaper performance, and build system restructuring.

## [0.8.0] - 2026-06-05

### License

- Relicensed to MIT license (added LICENSE file)

### Dock Tab — M3 Rewrite with 3 Child Tabs

- **Settings child tab:** "Visibility & behavior" card with 5 toggle rows: Show dock, Auto-hide dock, Group running windows, Dock tooltips, Pinned apps tray pill — each with title (bandTop+36), description (bandTop+53), and right-aligned toggle
- **Widgets child tab:** Interactive widget reordering with 3-column layout (LEFT/CENTER/RIGHT), drag-and-drop with ghost visualization, per-widget toggle and remove button, "Add widget" button per column
- **Appearance child tab** (new `settings_tab_dock_appearance.cpp`): 8 configurable items in 2-column grid — Corner Radius (0-50), Dock Scale (50-150%), Icon Size (0-50), Icon Spacing (-50-50), Dock Opacity (0-100%), Dock Border (toggle), Border Size (0-12px), Border Opacity (0-100%)

### Taskbar Tab — M3 Rewrite with 3 Child Tabs

- **Settings child tab:** 7 toggle rows (Show taskbar, Position at top, Group running windows, Auto-hide, Embedded widgets, Tooltips, Pinned apps tray pill) + Width mode combo (Floating/Edge-to-edge/Fill)
- **Widgets child tab:** Same 3-column drag-and-drop widget reordering as dock tab
- **Appearance child tab:** 8 M3 sliders — Height (24-120px), Radius (0-50px), Opacity (0-100%), Icon size (0-96px), Icon spacing (0-50px), Floating gap (0-50px), Scale (50-150%), Border size (1-12px) + Taskbar border toggle

### Sound Tab — M3 Rewrite with Per-App Audio

- Per-app audio streams now display resolved app icons with volume sliders and mute buttons
- Default Output (sink) and Default Input (source) visually separate with icon + label + combo dropdown
- Engine section redesigned: 2-column grid for sample rate, force graph rate, allowed rates preset, default output PCM
- Visual overhaul: card radius 14, header icons 24px, headings 15px/700, section gap 32, label column 200, form row pitch 56
- Default audio settings persisted and reapplied via `apply_saved_defaults()`

### Night Light Tab — M3 Redesign

- Unified slider drag state (code range 210-215) replacing separate `nightlightSliderDrag`
- New `nightlight_consume_pointer_move()` handler removes inline slider logic from event handler
- M3 card pattern with color temperature, schedule, and brightness controls

### Notifications Tab — M3 Rewrite

- Unified `notifSliderDrag` replacing separate `notifTimeoutSliderDrag` and `notifToastGeomSliderDrag`
- New geometry helpers: `notif_app_geom()`, `apply_notifications_slider_x()`, `notifications_primary_test_button_geom()`
- Added `notificationsScalePct` setting (percentage-based notification scaling)

### Themes Tab — M3 Redesign

- Simplified mini-preview: window controls (red/yellow/green dots), "Application" title, search field, "Remember me" checkbox, "Continue" filled button, divider line
- Preview uses actual theme colors from `GtkThemeColors` (headerBg, headerFg, bg, accent, fg, text, base)
- Fixed GTK/Plasma cursor theme application command quoting

### Launcher Tab — M3 Update

- Geometry refactored: `launcher_slider_row_geom()` delegates to `launcher_app_geom()` instead of inline constants
- View mode combo geometry accepts pointer output instead of hardcoded Y offset

### Workspaces Tab — M3 Update

- Geometry refactored: `workspace_slider_row_geom()` delegates to `workspace_app_geom()`
- Layout constants moved out of event handlers into shared functions

### Wired Tab — Complete M3 Rewrite with 6 Child Tabs

- **Status:** Interface name, connection status, speed, duplex, auto-negotiate, MAC/IPv4/IPv6 addresses, gateway, DNS, driver info, vendor, product, MTU, device state
- **IPv4:** DNS priority, DHCP timeout, route metric, required timeout sliders + send hostname, ignore auto DNS, never default, may fail toggles (emerald-500 accent)
- **IPv6:** Same structure with router advertisement timeout
- **Ethernet:** MTU slider, auto-negotiate toggle, accept all MACs toggle
- **Security:** 802.1x enable toggle, system CA certs toggle
- **Advanced:** Autoconnect priority slider, autoconnect toggle, browser-only toggle
- Network service enhanced: reads 22 new Ethernet properties including speed, carrier, duplex, driver version, firmware, vendor

### Hyprland Tab — Full M3 Rewrite (11 sections)

- Replaced all `.inl` inline files with M3-styled `.hpp` components (total ~3107 lines):
  - **General:** 5 sliders + 3 toggles (borders, gaps, tearing, snap)
  - **Decoration:** 18 sliders + 7 toggles (rounding, opacity, blur, shadow, dimming)
  - **Input:** 7 sliders + 8 toggles (repeat, sensitivity, touchpad, follow mouse)
  - **Binds:** 2 sliders + 2 toggles (scroll delay, focus)
  - **Dwindle:** 4 sliders + 4 toggles (split ratio, force split, smart resizing)
  - **Master:** 2 sliders + 2 toggles (master factor, slave count, new on top)
  - **Scrolling:** 2 sliders + 2 toggles (column width, scroll focus)
  - **Animations:** Master toggle + 31 per-entry toggles across 5 categories
  - **Misc:** 2 sliders + 6 toggles (VRR, wallpaper, autoreload, logo)
  - **Render:** 3 sliders + 4 toggles (FP16, shader blur, direct scanout, color management)
  - **Cursor:** 3 toggles (hardware cursors, hyprcursor, gsettings sync)
- Added animation popup editor with speed control, curve selector (13 presets), style selector per entry
- Added cubic bezier editor with 13 preset curves
- Sidebar collapsed from 11 sub-tabs to single entry

### Power Tab — Full Layout Redesign

- **Sleep & Power child tab:** Display Sleep card with emerald-500 toggle + timeout slider (1-120 min), Idle Suspend card with toggle + timeout slider (5-240 min), Power Button + Lid Close side-by-side grid with action combo dropdowns, header-only Battery percentage toggle card
- **Performance child tab:** CPU Performance card with right-aligned Scaling Governor combo (Performance/Powersave) and Energy Performance Preference combo (5 levels), System Tuning Profile card with 8 TuneD profiles applied via async `tuned-adm` in detached thread
- All toggles use emerald-500 accent (`eaR=0.396, eaG=0.792, eaB=0.412`)

### Settings Infrastructure

- Rewrote `settings_event_handlers.cpp` — removed dozens of duplicate layout constants, consolidated geometry into shared functions (`dock_app_slider_geom`, `notif_app_geom`, `workspace_app_geom`, `launcher_app_geom`, `appearance_app_geom`)
- Added animated smooth scrolling with `settingsScrollPxTarget` + per-frame convergence
- Added opaque panel support for dropdown popups (`opaquePanel` parameter skips glass highlight)
- Fixed scroll paint/click mismatch for power tab (now maps `activeTab == 32` to `app.settingsPowerScrollPx`)
- Fixed power button dropdown popup not rendering (geometry was overwritten by lid close)
- Fixed all 5 power dropdown hover tracking in `on_pointer_motion` handler
- Enhanced widget drag-and-drop with ghost visualization, grab offset calculation, section/index tracking
- Hyprland event routing: single entry (tab 33) instead of 11 sub-tabs (33-43)

### Bug Fixes

- **Matugen stability:** Fixed header includes for backend restructure, added locked border colors in matugen output, added group/groupbar section config for Hyprland v0.48+, added `ptyxis` terminal theme template, fixed keyword fallback syntax (tries `key:value` and `key/value`)
- **Drive mounting:** Improved mount/unmount operations with proper path handling and polkit integration
- **General:** Power actions backend fixes, launcher tab fixes, network tab fixes, wired settings tab fixes, icon tab updates, UI fixes across multiple tabs

## [0.6.0] - 2026-06-04

### Animation System

- Implemented a complete Material 3 (M3) motion and animation framework with core animation engine providing easing curves, duration presets, and keyframe support
- Defined motion design tokens following the M3 specification (duration, easing, delay constants)
- Integrated the animation system into widget rendering pipelines across all shell components
- Rewrote the animation manager with improved interpolation, completion callbacks, and shared animation state
- Applied animated transitions to: dock tooltips, OSD overlays, taskbar state changes, app drawer popups, power modals, calendar popups, media player popups, volume mixer popups, VPN popups, weather popups, battery/clock/media/weather/workspaces/Bluetooth widget paints, settings panels, wallpaper transitions, and button/slider/toggle interactions
- Added layout caching and measurement scratch utilities for efficient animation frame rendering
- Added animated pill-style slot rendering support
- Replaced all instantaneous visibility toggles with animated transitions (fade, slide, scale)

### Compositor Backend Rewrite (Multi-Compositor Architecture)

- **MangoWM Window Manager:**
  - Full workspace management overhaul with complete rewrite
  - Rewrote keyboard and output backend for the new architecture
  - Added runtime with IPC integration, event loop, and compositor lifecycle management
  - Added comprehensive MangoWM documentation detailing architecture, IPC protocol, and configuration
  - Implemented compositor-level power management (shutdown, reboot, logout, suspend via logind)

- **Hyprland Backend:**
  - Added runtime with socket-based IPC, event handling, and window management
  - Added event handler for Hyprland IPC signals (workspace change, window open/close, urgent, fullscreen)
  - Added keyboard layout management via Hyprland
  - Added output/monitor management
  - Added workspace tracking and manipulation
  - Added toplevel window mapping protocol for window-to-workspace association
  - Added window identification and matching
  - Added new Wayland protocol for focus grabbing

- **Sway Backend:**
  - Added IPC runtime with socket communication
  - Added keyboard layout management
  - Added output management
  - Added workspace tracking

- **Niri Backend:**
  - Added compositor runtime
  - Added keyboard support
  - Added output support
  - Added workspace management

- **Triad Backend:**
  - Added compositor runtime
  - Added keyboard support
  - Added output support
  - Added workspace management

- **Ext-Workspace Protocol Backend:**
  - Added support for the wlr-ext-workspace-unstable-v1 protocol
  - Implemented workspace creation, removal, reordering, and naming

- **Compositor Abstraction Layer:**
  - Added IPC abstraction over compositor communication channels
  - Added virtual interface for keyboard management
  - Added virtual interface for workspace management
  - Added runtime compositor detection (Hyprland, Sway, Niri, MangoWM, Triad)
  - Restructured Wayland registry for dynamic compositor feature registration

### DBus Services Restructuring

- Relocated all DBus service implementations to a dedicated DBus layer for clean separation of concerns:
  - Bluetooth (service and agent)
  - Keyring/secret service
  - MPRIS (player and album art)
  - NetworkManager (service, secret agent, type definitions)
  - Notifications service
  - System tray (host, manager, SNI icons, stream icons, environment filter, context menu)
  - UDisks2 drive service
- Added GNOME/KScreen global shortcuts DBus service
- Added Bluetooth pairing agent implementation
- Significantly rewrote NetworkManager service to fix VPN connection state tracking, secret agent integration, and connection activation

### File Structure Refactoring

- Renamed and reorganized numerous core files across the codebase to improve naming consistency and logical grouping
- Reorganized widget files into dedicated subdirectories by component type (battery, clock, media, weather, spacer, widget bar, shared utilities, app drawer, start menu)
- Removed all dock-specific component files that were refactored into shared components (dock calendar popup, control center, spotlight, tray, weather popup, volume mixer, workspace strip)

### Desktop Widgets System

- **Full Desktop Widget System with:**
  - Widget host container with dynamic placement, resize, and configuration
  - Interactive calendar widget with month navigation and event indicators
  - Analog and digital clock display
  - Full media player widget with album art, playback controls, and progress
  - System monitor widget showing CPU, memory, disk, and network usage graphs with real-time data
  - Current conditions and forecast weather display
  - Enhanced weather visualization with animated backgrounds
  - Compositing layer for widget rendering
  - Widget positioning, size, and visibility preferences
  - Audio visualization data reader (CAVA integration) for visualizers
- Merged desktop widgets with the desktop experience layer
- Updated settings UI for widget configuration

### Settings Application

- **Hyprland Settings Tab** - Comprehensive Hyprland configuration covering:
  - General settings (mod key, sensitivity, gaps, borders, cursor behavior, gestures, tearing)
  - Input configuration (keyboard layout, touchpad, mouse, scroll behavior)
  - Key binding management with modmask, keycode, dispatcher, and argument editing
  - Decoration settings (rounding, opacity, blur, shadows, dimming, colormatrix)
  - Animation configuration with per-style settings (fade, slide, windows, workspaces, border)
  - Layout settings (dwindle/master/top, orientation, gap offsets, split options)
  - Dwindle layout specifics (pseudo mode, no_gaps_when_only, force_split)
  - Master layout specifics (new window position, master factor, smart resizing)
  - Rendering settings (explicit sync, direct scanout, vsync, cursor, power optimization)
  - Miscellaneous (disable_autoreload, XWayland, render ahead, NVIDIA, VRR)
  - Scrolling configuration (scroll method, fingers, edge, axis)

- **Power Settings Tab** - Power management with suspend, hibernate, power button action, and idle timeout
- **Time Settings Tab** - Timezone selection, clock format (12h/24h), date format, NTP sync, manual time/date
- **Keyboard Settings Tab** - Layout/variant selection with preview, repeat rate/delay sliders, compose key config
- **Accounts Tab** - User account management, avatar change, password change
- **Bluetooth Tab** - Device scanning, pairing, connection management, device removal
- **Themes Tab** (rewritten) - Complete theme management with preview thumbnails, import/export, color scheme selection
- **Desktop Tab** - Desktop icon layout, show/hide icons, snap-to-grid, icon size
- **Dock Tab** - M3-styled dock configuration (position, size, autohide, behavior)
- **Icons Tab** - Icon theme selection with preview
- **Bing Wallpaper Tab** (rewritten) - Daily wallpaper with region selection, resolution, download management
- **Monitors Tab** (updated) - DRM probing, GPU detection, layout editor improvements
- **Night Light Tab** - Blue light filter with schedule, temperature slider, brightness
- **UI Layout Tab** (rewritten) - Panel/dock layout configuration
- **Wallpaper Tab** (rewritten) - Selection with thumbnails, solid colors, gradients
- **Workspaces Tab** - Workspace layout and behavior configuration
- **Notification Tab** (updated) - Per-app notification blocking, do-not-disturb, popup position
- **Default Apps Tab** (updated) - Application association per MIME type
- **Launcher Tab** (updated) - App drawer and launcher preferences
- **Network Tab** (updated) - WiFi, wired, and VPN connection management with VPN add dialog rewritten
- **Sound Tab** (rewritten) - Per-app audio volume, input/output device selection, mute
- **Taskbar Tab** - Taskbar behavior, grouping, pinning, appearance
- Settings serialization rewritten with JSON-based persistence for all new tabs
- Reactive event-driven settings application with debouncing

### M3 UI Framework

- Complete Material 3 UI component library including:
  - **Core framework:** Animation engine, flexbox-style layout, focus ring, glyph rendering, typography label, separator, spacer, state layer (hover/focus/pressed)
  - **Controls:** Banner, bottom app bar, bottom sheet, full button system (filled, tonal, outlined, text, elevated, icon), card (elevated/filled/outlined), carousel, checkbox with indeterminate state, chip (input/choice/filter/action/assist), date picker, dialog, floating action button, text input, navigation bar/drawer/rail, radio button group, search bar, dropdown select, slider (continuous/discrete/range), snackbar, tab bar (fixed/scrollable), time picker, toggle switch, top app bar
  - **Design tokens:** Color (primary, secondary, tertiary, error, neutral, variant surfaces), elevation levels 0-5, motion duration/easing, shape corner radius, spacing scale, typography scale (display, headline, title, body, label)
  - Builder pattern for constructing component trees
  - Dynamic color palette generation implementing material_color_utilities in C++
- Integrated M3 theming across all settings panels (60+ tabs migrated to M3-styled rendering)
- Added color scheme fallback palette for when theme resolution fails
- Updated theme system with M3 color scheme integration and dynamic palette switching

### App Drawer and Start Menu

- Split the monolithic app drawer into separate app drawer and start menu components
- **App Drawer:**
  - Grid layout with alphabetical sorting and category filtering
  - Overlay rendering with performance optimizations (cached icon surfaces, dirty region tracking)
  - Per-app entry rendering with icon caching, hover states, and launch handling
  - Power action execution (shutdown, restart, logout, suspend)
  - Power confirmation modal dialog
  - Usage tracing for most-frequently-used app sorting
  - Desktop entry enumeration with parsing and caching
  - Gesture zone definitions for drawer activation
  - Popup menu rendering
- **Start Menu:**
  - Grouped app categories with search filtering and pinned/frequent apps
  - Activation zone definitions
- **Launchpad:**
  - Complete rewrite with folder creation/management, drag-to-folder, pin-to-dock support
  - Search bar with real-time filtering
  - Folder maker icon for quick folder creation
  - Persistent folder state across sessions
  - Layer surface management with dirty-region tracking for performance
  - Real-time app search with relevance scoring
  - Fixed launchpad not persisting user-created folders between sessions
  - Fixed launchpad bad performance (throttled re-rendering, icon cache optimization)
  - Added right-click context menu for folder creation and app pinning
- **Overview:**
  - Complete workspace overview with window preview thumbnails via screencopy
  - Overview rendering with animated workspace transitions
- **Window Picker** - Window selection interface for task switching

### Taskbar

- Full taskbar rewrite with:
  - Window grouping by application ID with expandable group popups
  - Visual state indicators (active, urgent, minimized, focused)
  - Progress bar overlay for window progress (file transfers, downloads)
  - Drag-and-drop reordering of pinned and running applications
  - Right-click context menu (close all, minimize all, pin, unpin)
  - Animated hover and active state transitions
  - Startup notification integration (launch feedback)
  - Configurable position (top, bottom), size, and behavior
  - Mini panel mode - compact taskbar variant for vertical panels
  - Fixed icons not setting on configuration reload
  - Settings tab with grouping, pinning, and appearance options

### Dock

- Core dock implementation with persistent pinned app storage, drag-to-pin, running app indicators
- Rewritten input handling with gesture support (swipe up for app drawer, swipe left/right for workspace switch)
- Widget bar for rendering battery, clock, Bluetooth, media, VPN, workspaces, and weather slots
- Hover/tooltip popup picking with directional awareness and edge cases
- Persistent pinned application storage with serialization
- Animated tooltip rendering with position adjustment
- Dynamic dock positioning (horizontal/vertical, edge snapping)
- Fixed dock exclusive zone calculation for proper window avoidance
- Fixed desktop right-click menu activation
- Added trash/discard drop target on dock
- Removed monolithic dock app drawer and control center implementations (11,000+ lines deleted, refactored into shared components)

### Control Center

- M3-styled control center with quick settings toggles
- Comprehensive popup with notifications, volume, brightness, media player, network, Bluetooth, and settings shortcuts
- Hit testing and layout calculation with direction-aware expansion
- Panel geometry management
- State management for toggles, sliders, and active panels
- Added VPN section with connection status display

### Power Menu

- Full power menu implementation with animated dialog:
  - Power actions: Shutdown, Reboot, Suspend, Hibernate, Logout, Lock Screen
  - Confirmation dialog with customizable timeout
  - Keyboard shortcut integration
  - DBus logind integration for system-level power actions
- Modal confirmation overlay with M3 styling:
  - Action type icons and labels
  - Cancel and confirm buttons with focus management
  - Escape key to dismiss
- Fixed power buttons now actually execute system power actions via logind
- Fixed power menu UI input blocking issue

### Lock Screen

- Wayland session lock protocol implementation:
  - Full-surface lock screen rendering with blurred background
  - Password input field with visibility toggle
  - Date/time display
  - Media playback controls on lock screen
  - Keyboard shortcut for instant lock
- Pointer/keyboard event routing for lock screen
- Integrated ext-session-lock-v1 Wayland protocol

### System Tray

- Relocated tray implementation from dock into standalone module:
  - Tray host surface management
  - Tray item registration, icon updates, menu management
  - StatusNotifierItem icon rendering with attention icon support
  - XDG environment filter for tray
  - Tray item context menu rendering
  - Animated icon streaming support
- Separated tray functionality from dock into standalone module

### Keyboard

- Keyboard layout management infrastructure:
  - Applies keyboard layout, variant, and options via libxkbcommon
  - Integrated with compositor backends through abstraction interface
  - Multi-layout support with per-window layout tracking
  - Compose key configuration
  - Typing delay and speed configuration
- Virtual keyboard support via input method protocol

### IPC Service

- Unix domain socket IPC server:
  - Command/response protocol for external tools to interact with Event Horizon
  - Commands: workspace management, window focus, power actions, settings query/modify
  - JSON-based message format
- Command-line IPC client utility

### Night Light

- Gamma-based blue light filter:
  - Configurable color temperature (3500K-6500K)
  - Schedule-based auto-activation (sunset to sunrise)
  - Smooth transition animation on enable/disable
  - Settings tab integration
- Added wlr-gamma-control-unstable-v1 protocol support

### Wallpaper

- Wallpaper rendering engine with:
  - Dynamic wallpaper switching with crossfade animation
  - Solid color, gradient, and image wallpaper support
  - Bing daily wallpaper download and caching
  - Thumbnail generation and caching for fast preview
- Configuration application with color scheme extraction from wallpaper
- Optimized image decoding with JPEG (libjpeg-turbo), PNG, WebP support
- Async thumbnail generation service
- Removed legacy desktop widgets overlay (widgets now rendered in the desktop layer directly)

### Vulkan/Rendering

- Major Vulkan rendering overhaul:
  - Proper swapchain recreation on output resize
  - Image layout transition management
  - Synchronization with timeline semaphores
  - Custom Vulkan pipeline for M3 UI rendering
  - Optimal buffer allocation with dedicated GPU memory priority
  - Vulkan destruction queue with deferred resource cleanup
- CPU-side Cairo fallback buffer for non-GPU rendering
- Shared memory buffer pool optimization
- Wayland surface extension protocol support

### Icon Cache

- Complete icon cache overhaul:
  - Multi-theme lookup following XDG icon theme specification
  - SVG rendering with cairo and librsvg
  - PNG/WebP direct loading
  - Lazily-populated LRU cache with configurable size limits
  - Fallback icon chain with parent theme traversal
  - Hard-coded fallback for common application icons
  - Memory-efficient surface sharing

### Performance Optimizations

- **Startup Time:** Reduced startup time by approximately 98% through lazy initialization, deferred DBus connections, parallel service loading, and postponed UI rendering until after first frame
- **Memory/RAM:** Significant RAM reduction (~40%) via:
  - Icon cache size limits and LRU eviction
  - Shared memory pool reuse for Vulkan buffers
  - Bundled asset lazy-loading (fonts, icons, images)
  - Removing unused bundled assets
  - Efficient surface damage tracking to avoid full redraws
  - Reduced texture atlas fragmentation
- **App Drawer Performance:** Cached app icon surfaces with dirty-region-only redraws, reduced layout calculations
- **Launchpad Performance:** Throttled search with debouncing, lazy folder loading, cached folder previews
- **Taskbar Speed:** Optimized window state diffing, reduced paint operations with clip regions
- **Toggle Instant Response:** Synchronous state change with deferred visual animation
- **Media Player:** Reduced album art polling frequency, cached art surfaces
- **General Optimization:** Added performance benchmarking framework, optimized memory usage across all widget types, reduced string allocations, pool allocator for small objects

### Crash Fixes and Stability

- **Popup Surface Crash:** Fixed crash when destroying popup surfaces with active damage regions; added null-buffer commit pattern before destroy to prevent compositor from retaining a reference to the destroyed surface
- **Workspace Switch Crash:** Fixed null pointer dereference in workspace backend when switching workspaces rapidly; added surface existence validation before focus operations
- **General Crashes:**
  - Fixed segfault in desktop layer when toplevel is destroyed during animation
  - Fixed use-after-free in notification toast timer
  - Fixed crash on empty workspace list
  - Fixed crash in overview when window closed during rendering
  - Fixed crash in OSD when output removed
  - Fixed crash in taskbar when pinned app executable missing
  - Fixed crash in dock when foreign toplevel removed
  - Fixed crash in wallpaper renderer when image fails to decode
- **UI Fallback Colors:** Fixed theme color fallback chain - now properly cascades from M3 palette to system theme to hardcoded fallback palette
- **Icon Reload:** Fixed icons not updating when theme or color scheme changes at runtime
- **Polkit Typing Issue:** Fixed text input handling in PolicyKit authentication dialog (key event propagation and focus)
- **Power Menu Input Block:** Fixed input region calculation preventing click-through on power menu backdrop
- **VPN Connection Stability:** Fixed VPN connection state tracking with proper phase transitions

### Build System

- **Meson Build:**
  - Integrated all new compositor backends as optional build targets
  - Added build options: embed_assets, jpeg, polkit_agent, native_march, release_omit_frame_pointer
  - Added M3 framework compilation units
  - Added DBus service source files in dedicated directories
  - Added tomlplusplus as a subproject dependency
- **New Build Options:**
  - `embed_assets` - Embed asset files into binary (default: false)
  - `jpeg` - JPEG decoding support via libjpeg-turbo (default: false)
  - `polkit_agent` - PolicyKit authentication agent (default: false)
  - `native_march` - Architecture-specific optimizations (default: false)
  - `release_omit_frame_pointer` - Frame pointer omission in release builds (default: true)
- **PKGBUILD:**
  - Version bumped from 0.2.0 to 0.6.0
  - Added dependencies: libjpeg-turbo, librsvg, libdrm, polkit
  - Updated build dependencies with new requirements
  - Added install targets for desktop files and icons

### CI/CD

- **GitHub Actions:**
  - Complete CI pipeline with build, test, and artifact publishing
  - Multi-distribution builds (Arch Linux via pacman, Ubuntu/Debian via apt)
  - Dependency caching for faster builds
  - Automated release artifact upload with versioned filenames
  - Added liblz4-devel dependency for both apt and pacman package managers
  - Added binutils installation step
  - Upgraded action versions for upload and download artifacts
  - Removed redundant release steps and consolidated package upload process
- **CircleCI:** Added alternative CI pipeline with multi-architecture build matrix

### Portal Removal

- Removed the entire xdg-desktop-portal implementation including portal service, app chooser, dialog, and file chooser
- Removed portal systemd service files and D-Bus service registrations
- Portal disabled via configuration flag

### Wayland Protocols

- Added new protocol specifications: hyprland-focus-grab, hyprland-toplevel-mapping, wlr-gamma-control
- Added protocol implementations: ext-foreign-toplevel-list, ext-session-lock, wlr-screencopy, wlr-gamma-control, virtual-keyboard, ext-idle-notify
- Implemented protocol auto-detection and dynamic registration

### Documentation

- Added comprehensive documentation directory covering:
  - Architecture overview, build instructions, and configuration guide
  - Compositor backend documentation for all supported compositors
  - DBus service interface documentation
  - Complete project file mapping reference
  - Detailed shell component docs: App Drawer, Control Center, Desktop, Dock, Keyboard, Launchpad, Lock Screen, Notifications, OSD, Overview, Power Confirm, Spotlight, Taskbar, Unified Shell, Widgets
  - Service docs: Audio (PipeWire), Bing Wallpaper, IPC, PolicyKit
  - UI component library and settings application documentation
  - Wallpaper system and Wayland protocol integration docs
  - UX design documentation
  - MangoWM reference (3947 lines)
- Updated project README with build badges and feature list
- Added configuration reference document
- Updated example configuration file with new options

### Miscellaneous

- **Clipboard:** Rewrote clipboard implementation with proper Wayland data device protocol, MIME type filtering, and offer caching
- **Notifications:** Toast notification system rewritten with action buttons, urgency levels, timeout management, and position configuration
- **Audio Visualizer:** PipeWire audio data reader for waveform visualization
- **Bundled Fonts:** Embedding Material Symbols font and other icon fonts directly
- **Memory Monitor:** Accurate memory usage reporting from system status
- **Desktop Archive:** Desktop file archive and compression for backup
- **Open With Dialog:** "Open with" dialog for file association management
- **RF Kill Switch:** Monitoring for Bluetooth and WiFi radio state
- **Thread Pool:** Reusable thread pool for asynchronous operations
- **Battery Widget:** Battery status widget with icon, percentage, and charging state, integrating with UPower via D-Bus
- **Bluetooth Widget:** Bluetooth status widget with device list, connection state, and toggle
- **VPN Popup:** VPN connection management popup with status, connect/disconnect, and configuration
- **Weather Widget:** Enhanced with animated conditions, multi-day forecast, and location detection
- **Workspace Widget:** Rewritten with animated transitions, improved multi-monitor support, and touchpad gesture integration
- **Media Widget:** Rewritten with album art, playback position slider, and per-app media control
- **Calendar Popup:** Updated with event indicators and month navigation
- **Volume Mixer Popup:** Updated with per-app audio stream control and output device selection
- **Media Player Popup:** Full media player popup with playback controls
- **Settings Widget Drag:** Drag-and-drop widget reordering in settings
- **Widget Picker:** Desktop widget type selection dialog
- **Matugen Integration:** Enhanced Material You color generation with external template support
- **Screen Recording:** Screencopy PNG capture for screenshot functionality
- **Idle Inhibitor:** Prevent idle/suspend during fullscreen video playback
- **File Chooser Dialog:** Native file chooser dialog implementation
- **Dialog Base:** Reusable dialog infrastructure for all modal dialogs
- **Debug/Diagnostics:** Structured diagnostic logging, performance benchmarking framework, Wayland event loop diagnostics
- **Translations:** Internationalization framework setup

### File Statistics

This release encompasses approximately 260+ modified commits spanning development from 2026-05-19 to 2026-06-04, touching over 500 unique files with approximately 70,000 lines added and 35,000 lines removed across the entire codebase.
