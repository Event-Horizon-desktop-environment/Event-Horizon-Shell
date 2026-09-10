# Event Horizon — Shell Rewrite Audit & Plan

**Status:** Audit complete 2026-08-19 — 8 deep-dive passes + full per-file split-map audit (§11) done.
Phase 0 DONE (meson test in CI; `eh_shell_core` → shared lib; baseline recorded below).
Phase 1 DONE (IPC fabric: framed protocol + pub/sub + `eh::ipc::Client` + `config.applied` broadcast + `test_contract_ipc` green).
Phase 2 DONE (Settings → `horizon-settings`; Wallpaper → `horizon-wallpaper`; Notifications → `horizon-notifications` — see §0.5; all built + `meson test` green).
Phase 3 IN PROGRESS — items 1-4 DONE; items 5-7 NOT STARTED.
- Item 1 DONE: lockscreen/polkit/gamma/idle off main connection (own connections + isolated `on_error`).
- Item 2 DONE: shell-owned `toplevel.*` service over bus + `ToplevelClient` + `ToplevelBridge` (spike proved ext list is snapshot-only on secondary connections; wlr list works on child connections).
- Item 3 DONE: desktop → `horizon-desktop` child (§0.9); parent-death guard (PDEATHSIG).
- Item 4 DONE: dock → `horizon-dock` child (§0.10) + taskbar → `horizon-taskbar` child (§0.11); both track own toplevels on own connections.
- **Item 5 NOT DONE:** extract launchpad + overview from dock child → own processes (or keep in dock; §0.10 decision = keep in dock).
- **Item 6 NOT DONE:** supervisor readiness handshake, escalation backoff, single-process compat mode deletion.
- **Item 7 NOT DONE:** live-session kill-respawn verification for all 5 children.
Phase 4 NOT STARTED (services split — tray, mpris, network, bluetooth, audio, etc.).
Phase 5 NOT STARTED (hardening & observability).
**Still in supervisor (not split):** lockscreen, polkit, gamma, idle, all D-Bus services (tray, mpris, network, bluetooth, audio, udisks2, keyring, global keyboard, global shortcuts, polkit). `--both` monolith path still exists. `desktop_list.cpp` static-init bug still present. `app_drawer_program.cpp` orphan still present.
**Goal:** Crash-isolated desktop shell. The core shell must stay up when the dock, taskbar, settings, wallpaper, or any UI component dies. Each component runs as its own process; the shell supervises and respawns them.
**Date:** 2026-08-19

---

## 0.5 DONE — `horizon-notifications` split (compiled + green)

**Status (2026-08-05): DONE, verified.** Child executable built, session rewired, mpris +
settings route over the bus, `meson test` green in debug+release under `-Werror`.
(The wallpaper split, Phase 2 item 2, was the prior completed sibling.)
Re-verified after this session: `ninja -C build-debug` + `ninja -C build-release-user`
no-op, `meson test` 2/2 OK in both. Runtime toast display still needs a live session
check (no Wayland in CI).

**Objective (achieved):** `NotificationManager` + `NotificationDbusService` +
`NotificationToastHost` now run in the `horizon-notifications` child with its own
`WaylandConnection`; the supervisor spawns it after the IPC bus is up and SIGTERMs
it on shutdown; `notify` IPC command + mpris now-playing + settings test-button
notifications route over the bus; expiry timerfd moved to the child.

**What shipped this session:**
- Child: `src/notifications/standalone/{notifications_standalone.hpp,cpp}` +
  `src/notifications/standalone_main.cpp` (jemalloc mirror). `run_notifications_standalone`
  = own `WaylandConnection` (`connect(false)`), manager + toast host + dnd/timeout from
  config, own expiry timerfd, `notify.push`/`config.applied` subscriptions, `notify.changed`
  publish (TODO: supervisor-side CC consumer still unimplemented), dbus guarded by config,
  SIGINT/SIGTERM, poll loop over [display, timerfd, ipc] with `DeferredCall::drain()`.
  `mpris-art` parsed by position (raw RGBA may contain `\x1e`); `internal`/`mpris` tokenized.
  Mpris entries keyed by track key so art can replace the base notification.
- Session: `start_notifications_services()` installs a `notify.push` bus sender +
  spawns the child (tracked in `notifications_child_pid_`); `stop_notifications_services()`
  SIGTERM-kills it and clears the sender. `arm_notification_expiry_timer`,
  `sync_notification_toast_host`, the `notification_timer_fd_` poll handler, the config-hook
  dnd/timeout/toast lines, and the in-process manager/dbus/toast/timerfd members all removed.
  `notify` IPC command → `eh::notify::push_internal(...)` (fire-and-forget "ok").
- mpris: `last_notify_id_` deleted; now-playing → `eh::notify::push_mpris(key, summary, body)`
  + `push_mpris_art(key, art)`; art-update path keyed on `last_track_notify_key_`.
- Settings: test button hint uses `canPush()`; `run_standalone` installs/clears the
  `notify.push` bus sender around its poll loop.
- meson: `notifications_spawn.cpp` + `notifications_standalone.cpp` in `eh_shell_core`;
  `horizon-notifications` executable mirrors `horizon-wallpaper` (jemalloc pin, rpath, install).
- Fixed a latent `std::string_view`→`const std::string&` mismatch in `ipc_server.cpp`
  client-event re-broadcast (`publish(std::string(topic), payload)`).
- Verify (F): `ninja -C build-debug`, `ninja -C build-release-user`, `meson test` — all green.
  Runtime toast display still needs a live session check (no Wayland in CI).

### Architecture (decided, do NOT redesign)
- **`horizon-notifications` child** owns: `NotificationManager`, `NotificationDbusService`
  (org.freedesktop.Notifications, sdbus), `NotificationToastHost` (own layer-shell surface on
  the child's own connection). No pidfile — the supervisor tracks the pid from `posix_spawn`.
- **Bus contract** (`notify.push` / `notify.changed`), see `notifications_ipc.hpp` below.
- **Producers** (supervisor `notify` cmd, mpris, settings test button) just publish
  `notify.push`; they have NO manager instance anymore.
- **Child publishes `notify.changed`** (payload = `changeSerial()` string) so a future
  dock/control-center strip can subscribe. There is NO supervisor-side consumer yet (CC
  notifications widget is unimplemented — the strip paint file is empty), so no
  supervisor self-subscription is needed this pass. The server now re-broadcasts client
  events, which is what makes child→bus publishing work.

### DONE this session (files written, NOT yet compiled)
1. **`src/services/ipc/client.hpp` + `.cpp`** — added
   `bool publish(const std::string& topic, std::string_view payload, const std::vector<int>& fds = {});`
   Sends a `kTypeEvent` frame via `write_all`. **IMPORTANT: `fds` are NOT actually sent**
   client→server (server `recv` reads plain bytes; no SCM_RIGHTS receive path). Senders must
   inline bytes in the payload (documented in the code comment).
2. **`src/services/ipc/ipc_server.cpp` `handle_frame` default case** — re-broadcasts
   `kTypeEvent` from framed clients to subscribers (parses `topic\0payload`, calls `publish`).
   This is what lets the child publish `notify.changed`.
3. **NEW `src/desktop_shell/notifications/types/notifications_ipc.hpp`** (header-only, inline fns):
   - `eh::notify::kPushTopic = "notify.push"`, `kChangedTopic = "notify.changed"`,
     `kFieldSep = '\x1e'`, kinds `kKindInternal="internal"`, `kKindMpris="mpris"`,
     `kKindMprisArt="mpris-art"`, `kArtMaxDim = 160`.
   - Payload formats:
     - `internal \x1e app \x1e summary \x1e body \x1e urgency` (urgency = int 0/1/2)
     - `mpris \x1e key \x1e summary \x1e body`
     - `mpris-art \x1e key \x1e <image blob>` — blob is binary! See GOTCHA below.
   - `downscale_art()` (box-average to ≤160px so payload « 1 MiB `kMaxFrameBody`),
     `serialize_image_blob()` / `deserialize_image_blob()` (`ImageBlobHeader` POD:
     w,h,rowStride,hasAlpha,bps,channels,dataLen + raw RGBA).
   - Process-wide sender: `notifySenderSlot()` (function-local static),
     `setNotifySender(PushSender)`, `canPush()`, `push_raw(payload, fds={})`.
   - `encode_internal`, `encode_mpris`, `push_internal`, `push_mpris`, `push_mpris_art(key,img)`.
4. **`src/desktop_shell/notifications/types/notifications_notify.hpp`** — `info`/`error` now
   fall back to `push_internal(...)` when `instance()` is null (so supervisor + settings child
   route over the bus instead of needing an in-process manager).
5. **NEW `src/services/notifications/notifications_spawn.hpp` + `.cpp`** —
   `notifications_spawn_native_child()` → pid (sibling-exe resolution via `/proc/self/exe`,
   `posix_spawnp` + `POSIX_SPAWN_SETSID`, stdio→`$XDG_STATE_HOME/event-horizon/horizon-notifications.log`,
   local `state_base()` helper duplicated from wallpaper_apply.cpp) and
   `notifications_kill_native_child(int)` (SIGTERM→50×20ms→SIGKILL). Mirrors
   `wallpaper_spawn_native_child` / `wallpaper_kill_native_child`.

### TODO (in order) — resume here
**A. Child executable — create these 3 files:**
- `src/notifications/standalone/notifications_standalone.hpp`:
  `namespace eh::shell::notifications { int run_notifications_standalone(); }`
- `src/notifications/standalone/notifications_standalone.cpp` — full run loop, details below.
- `src/notifications/standalone_main.cpp` — jemalloc mirror of
  `src/wallpaper/standalone_main.cpp` (same `malloc_conf` + `mallctl("narenas")` pin +
  `kEhNotificationsJePinned`), calls `run_notifications_standalone()`. MANDATORY per the
  `desktop_list.cpp` static-init heap bug (every child links core).

`run_notifications_standalone()` sketch (mirror `wallpaper_standalone.cpp` structure):
1. `WaylandConnection conn; conn.connect(false);` require `compositor() && shm() && layer_shell()`.
2. `NotificationManager mgr; NotificationToastHost toastHost(mgr); eh::notify::setInstance(&mgr);`
3. `shell_config_reload_from_disk_now()`; from `shell_config_snapshot().notifications` set
   `setServerDefaultTimeoutMs` + `setDoNotDisturb`.
4. Output pick: `conn.outputs()` (non-empty) else bail on toasts; if
   `ncfg.toast.layerShellEnabled` → `toastHost.initialize(conn.compositor(), conn.shm(),
   conn.layer_shell(), conn.seat(), output)` + `apply_config(ncfg.toast)`.
   (`conn.seat()` may be null — host tolerates null seat.)
5. `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC)` for expiry. Copy the
   `arm_notification_expiry_timer` logic from the session (uses `mgr.nextExpiryTimeoutMs()`,
   clamp ≥1ms, `timerfd_settime`).
6. `mgr.addEventCallback([&](const Notification&, NotificationEvent){ arm(); DeferredCall::callLater([&]{ toastHost.on_notifications_changed(); if (ipcOk) ipc.publish(kChangedTopic, std::to_string(mgr.changeSerial())); }); })`
   — DeferredCall is a thread-safe static; the dbus thread fires events, so always defer the
   toast redraw + publish via `callLater` + `DeferredCall::drain()` in the poll loop.
7. `mgr.setStateCallback` same as session: if `doNotDisturb()` close all `Dismissed`, arm,
   callLater → changed.
8. DBus: if `ncfg.dbusEnabled` → `try { dbus = make_unique<NotificationDbusService>(mgr); } catch (...) {}`
   (ctor calls `bus_->enterEventLoopAsync()` → own thread; can throw if no session bus / name taken).
9. IPC: `IpcClient ipc`; handler:
   - `config.applied` → `shell_config_reload_from_disk_now()`; re-apply dnd/timeout; re-run the
     toast sync (output may have appeared).
   - `notify.push` → parse + dispatch (GOTCHA below); then arm().
   Subscribe `config.applied` + `kPushTopic`; `ipcFd = ipc.fd()`.
10. SIGINT/SIGTERM handler sets `g_notif_signal`.
11. Poll loop over [display fd, timerfd, ipc fd], timeout 30'000:
    EINTR→signal check; timerfd POLLIN→read u64 + `dbus?dbus->processExpired():mgr.processExpired()` + arm;
    display POLLIN→`wl_display_dispatch`, else `wl_display_flush`; ipc POLLIN→`ipc.on_fd_ready`;
    after each iteration `DeferredCall::drain()`.
12. Cleanup: `toastHost.shutdown(); dbus.reset(); setInstance(nullptr);`

**GOTCHA — `mpris-art` binary tail:** the art blob is raw RGBA and CAN contain `\x1e` bytes,
so do NOT split it with a `\x1e` tokenizer. Parse `mpris-art` by position: payload =
`"mpris-art" \x1e key \x1e blob` → find the FIRST `\x1e` (past kind), then the SECOND `\x1e`
(past key); `blob = payload.substr(second+1)`. `deserialize_image_blob(blob)`. Only `internal`
and `mpris` kinds are safe to tokenize (text fields). Child keeps
`std::unordered_map<std::string, MprisEntry{uint32_t id; std::string summary, body;}>` keyed by
the mpris key: on `mpris` → store summary/body, `id = mgr.addOrReplace(id, "Media", summary,
body, Urgency::Normal, kDefaultNotificationTimeoutMs, NotificationOrigin::Internal)`;
on `mpris-art` → if `id==0` ignore (art arrived before the notification), else
`mgr.addOrReplace(id, "Media", e.summary, e.body, ..., image)`.

**B. Session (`src/bootstrap/session/detail/unified_shell_session.{hpp,cpp}`):**
- hpp: delete includes line 14 `notification_dbus_service.hpp` + line 15
  `notification_toast_host.hpp`; delete methods `arm_notification_expiry_timer`,
  `start_notifications_services`, `stop_notifications_services`, `sync_notification_toast_host`;
  delete members `notification_manager_`, `notification_dbus_`, `notification_toast_host_`,
  `notification_timer_fd_`; ADD `int notifications_child_pid_ = -1;`.
- cpp: add `#include "services/notifications/notifications_spawn.hpp"`; KEEP
  `#include "desktop_shell/notifications/types/notifications_notify.hpp"` (gives `eh::notify::`).
- Rewrite `start_notifications_services()` (was ~:172-222): install sender
  `eh::notify::setNotifySender([this](const std::string& p, const std::vector<int>&){ if (!ipc_service_ || !ipc_service_->running()) return false; ipc_service_->publish(eh::notify::kPushTopic, p); return true; });`
  then `if (ipc_service_ && ipc_service_->running()) { int pid = eh::shell::notifications::notifications_spawn_native_child(); if (pid > 1) notifications_child_pid_ = pid; }`.
  (It's called at :1009, AFTER the IPC init block — correct ordering.)
- Rewrite `stop_notifications_services()` (~:224): `if (notifications_child_pid_ > 1) { notifications_kill_native_child(notifications_child_pid_); notifications_child_pid_ = -1; } eh::notify::setNotifySender(nullptr);`. Drop timerfd/manager/dbus/toast cleanup.
- Delete `arm_notification_expiry_timer` + `sync_notification_toast_host` definitions.
- `build_handlers()`: DELETE the `notification_timer_fd_` handler block (~:254-266).
- config-applied hook (~:358-360): DELETE `notification_manager_.setDoNotDisturb(...)`,
  `setServerDefaultTimeoutMs(...)`, `sync_notification_toast_host(sc.notifications)` — the child
  re-syncs itself on the `config.applied` broadcast.
- Startup (~:1011-1014): DELETE the `{ const auto& sc = shell_config_snapshot(); sync_notification_toast_host(sc.notifications); }` block.
- `notify` IPC handler (~:924-929): keep name `"notify"`, replace body with
  `eh::notify::push_internal("IPC Command", args[0], args[1], 1); return "ok";` (drop the
  `using Urgency` + `notification_manager_` refs). Fire-and-forget is fine.

**C. mpris now-playing (`src/services/mpris/mpris_player.{hpp,cpp}`):**
- hpp: delete member `std::uint32_t last_notify_id_ = 0;` (line ~103). KEEP `last_track_notify_key_`.
- cpp `update_now_playing` (~:350-413): keep config guard (:360), Stopped→clear-key branch
  (:365-367), key dedup (:388-391). DELETE `NotificationManager* mgr = eh::notify::instance(); if (!mgr) return;`
  (:370-373) and the `for (existing : mgr->all())` duplicate check (:396-402). After building
  summary/body: `eh::notify::push_mpris(key, summary, body);` and
  `if (s.art) { if (auto art = cairo_surface_to_notification_image(s.art.get())) eh::notify::push_mpris_art(key, *art); }`.
  DELETE the `last_notify_id_ = mgr->addInternal(...)` (:411-412).
- cpp `update_now_playing_art_assume_locked` (~:764-787): guard becomes
  `if (last_track_notify_key_.empty()) return;`; replace `mgr`/`addOrReplace` with
  `eh::notify::push_mpris_art(last_track_notify_key_, *art)` when `album_art` has_value.
  Fix the log at `poll_art_completions` (:812-813) that prints `last_notify_id_` (use the key).
- Keep `#include "desktop_shell/notifications/types/notifications_notify.hpp"`.

**D. Settings:**
- `src/ux/settings/settings_tab_notifications/settings_tab_notifications.cpp:311` — change
  `if (!eh::notify::instance())` hint to `if (!eh::notify::canPush())` and reword the hint text.
  The `eh::notify::info(...)` onClick now routes over the bus (keep as-is).
- `src/ux/settings/common/embed/settings_embed_lifecycle.cpp` `run_standalone` — after the ipc
  connect+subscribe block (~:505-508), add
  `eh::notify::setNotifySender([&ipc](const std::string& p, const std::vector<int>&){ return ipc.publish(eh::notify::kPushTopic, p); });`
  and `eh::notify::setNotifySender(nullptr);` before the function returns. Verify
  `notifications_ipc.hpp` is reachable (notifications_notify.hpp includes it).

**E. meson.build:**
- Add to `libeh_shell_core` sources: `'src/services/notifications/notifications_spawn.cpp'`,
  `'src/notifications/standalone/notifications_standalone.cpp'`.
- Add executable mirroring `horizon-wallpaper`:
  `executable('horizon-notifications', sources: 'src/notifications/standalone_main.cpp',
  dependencies: eh_shell_core_dep, cpp_args: shell_cpp_args + jemalloc_cpp_args,
  cpp_pch: 'src/bootstrap/pch.hpp', install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir'), install_mode: 'rwxr-xr-x')`.

**F. Verify:** `ninja -C build-debug`, `ninja -C build-release-user`, `meson test -C build-debug`
(ipc_fabric + horizon_colors_engine must stay green — ipc_fabric exercises the client). Fix any
`-Werror` issues (esp. unused-parameter in lambdas, `_GNU_SOURCE` for `readlink`, `strerror`
include `<cstring>`).

### Logged notes (pre-existing, unrelated to this pass)
- Observed zombie `[horizon-setting] <defunct>` processes (settings child not reaped) — harmless,
  follow-up later.
- Steam crash the user hit was confirmed unrelated (its own LD_PRELOAD + no-DISPLAY issue).

---

## 0.6 DONE — Phase 3 item 1: lockscreen/polkit/gamma/idle off the main connection

**Status (2026-08-05): DONE, compiled + verified.** Each of the four system services now runs on
its own `WaylandConnection` (registered in the mux `extra_displays` set), so a protocol error in
any one of them disables just that connection instead of killing the dock's connection. Builds +
`meson test` green in debug+release under `-Werror`. Live behavior still needs a real session check
(no Wayland in CI).

**Objective (achieved):** the dock is no longer the process that owns gamma control, the idle
notifier, the session-lock surfaces, or the polkit auth dialog. All four were decoupled from
`dock_.gammaControlMgr` / `dock_.idleNotifier` / `dock_.sessionLockMgr` / `dock_.xkbState` and moved
onto their own connections with their own seat input.

**What shipped this session:**
- **Gamma** (`session:700`): `gamma_conn_` own connection; `GammaService` built from
  `gamma_conn_->gamma_control_manager()`, outputs from `gamma_conn_->outputs()`. Transition fd still
  polled via `build_handlers`. Nightlight config hook unchanged (`apply_nightlight_config`).
- **Idle** (`session:715`): `idle_conn_` own connection; `IdleService(notifier, seat)` from
  `idle_conn_`. Behaviors/config wiring unchanged.
- **Lockscreen** (`session:757`): `lock_conn_` own connection. `LockScreen` now owns its full seat
  input on that connection — its own `wl_keyboard` + own `xkb_context`/`keymap`/`state` (previously
  it borrowed `dock_.xkbState` and got keys forwarded from `shell_input.cpp`), plus the existing own
  `wl_pointer`. Ctor's `xkb_state*` param is now unused (pass `nullptr`). Outputs come from
  `lock_conn_->outputs()` and are re-synced on every connection dispatch (`sync_outputs`) so a
  monitor plugged in while locked still gets a lock surface. The dock registry no longer forwards
  `lock_add_output`/`lock_remove_output`; `shell_input.cpp` no longer forwards lock keys. Emergency
  Ctrl+Alt+Delete / Esc / test-mode all decode with the lock connection's own keymap now.
- **Polkit dialog** (`session:981`): `polkit_conn_` own connection + `polkit_seat_`
  (`eh::wayland::WaylandSeat`) wired to the dialog. The dialog ctor dropped its `xkb_state*` param;
  `consume_keyboard_key` now takes the seat-decoded `(state, sym, utf8, len)` so it needs no xkb of
  its own. Pointer events are forwarded only while the pointer is over the dialog surface.
  `show()` picks its output from `polkit_conn_->outputs()` (never a dock-connection proxy).
- **Removed dead forwarding layer:** `lock_seat_dispatch.{hpp,cpp}` and
  `polkit_seat_dispatch.{hpp,cpp}` (global screen/dialog registries used by `shell_input.cpp`) are
  deleted from the tree + `meson.build`; the two source dirs are gone.
- **Isolated error handling:** all four connections registered via a shared `registerOwnDisplay`
  helper (`session:1127`) — `on_error` logs the tag and disables that display slot; the main loop
  keeps running.
- **Shutdown ordering:** each service is reset before its own connection is disconnected
  (`session:1208`), and the polkit seat is unbound before `polkit_conn_` is reset.

**Not done (still in-process; needs a live session or later phases):** the eventual process split
(`horizon-lockscreen` / `horizon-services`) and the supervisor SIGCHLD restart path — item 1 only
moved these services off the main connection, it did not fork them. Item 2 (toplevel spike) is next
and requires a live Hyprland session.

---

## 0.7 DONE — Phase 3 item 2 spike: `ext-foreign-toplevel-list-v1` on a secondary connection

**Status (2026-08-05): DONE.** Ran on the live Hyprland 0.56 session (wayland-1). Verdict: the
protocol is **snapshot-only on secondary connections** → the per-component tracking path is dead;
the toplevel service must be shell-owned and broadcast over the bus.

**How it was tested:** a standalone probe (`/tmp/opencode/toplevel_spike.cpp`, kept out of the repo)
connected an independent `wl_display` (same as a child process would), bound
`ext_foreign_toplevel_list_v1`, and listened for 90 s while the user worked.

**Observed:**
- Initial sync delivered: all 5 existing toplevels arrived with `title`/`app_id`/`identifier`/`done`.
- **`finished` never arrived** (Hyprland's ext list impl doesn't signal sync completion).
- **Zero incremental events**: opening + closing an otter-term window produced no `toplevel`/`closed`;
  multiple live title changes (Edge tab, Discord channel) produced no `title`/`done`.
- Same result across 3 runs (one quiet 20 s + two with a window cycle).

**Consequence for the plan:** §7 item 2 takes the fallback branch — supervisor/`horizon-windows`
owns `zwlr_foreign_toplevel_manager_v1` + `ext_foreign_toplevel_list_v1` on the main connection and
broadcasts `toplevel.*` / `toplevel.list` over the IPC bus; taskbar/overview/dock app-menu subscribe.
`xdg_activation_v1` cross-connection activation still to be spiked.

---

## 0.8 DONE — Phase 3 item 2: shell-owned toplevel service over the bus

**Status (2026-08-05): DONE.** Implemented the fallback path the §0.7 spike proved mandatory. The
shell tracks foreign toplevels on its main connection (as before) and broadcasts deltas over the IPC
bus; `test_contract_ipc` extended and green in debug+release. No live-session validation yet (passive
observation only, no windows spawned).

**What was built** (`src/services/windows/`, added to `eh_shell_core` + `test_contract_ipc`):
- `toplevel_types.hpp/.cpp` — id-stable wire codec. Length-prefixed fields (`id;flags;appLen;app;titleLen;title`),
  robust to tabs/newlines/NULs in titles; `toplevel_encode/decode_record`, `_list`, plus the topics
  `toplevel.created` / `toplevel.updated` / `toplevel.closed` and the `toplevel.list` command.
- `toplevel_service.hpp/.cpp` — server: `register_handlers()` answers `toplevel.list` (full snapshot);
  `set_snapshot(records)` diffs against the previous snapshot and publishes created/updated/closed,
  assigning each record a stable `id` on first appearance (keyed by the bridge-provided `key`).
- `toplevel_bridge.hpp/.cpp` — session-side: subscribes to `ForeignToplevels` snapshot callbacks and
  pushes the wlr list (handle-keyed records; `closed`/null-handle entries dropped, which is what
  produces the closed events). Only the wlr list is published (the taskbar's source); the ext list
  stays in-process for overview/dock.
- `toplevel_client.hpp/.cpp` — client mirror: connects, subscribes to the three topics, fetches the
  initial snapshot via `toplevel.list`, maintains `list()` + `find(id)`, fires a change handler.
- `ForeignToplevels::add/remove_snapshot_cb` (fires alongside `notify_visual_dirty`, plus on prune)
  and `ExtForeignToplevels::add/remove_changed_cb` (multi-observer; `set_changed_cb` now clears + adds
  one). Hooks are now multi-observer so the dock and the bridge can coexist.
- Session wiring: `toplevel_bridge_` created after the IPC bus (`session:~951`) and torn down before
  `dock_cleanup`/bus stop on shutdown (`session:~1202`).

**Behavioral delta:** none in-process — the taskbar still consumes the dock's tracker via the existing
borrow. The service is live on the bus (publishes + answers `toplevel.list`) so a split component can
adopt `ToplevelClient` at any time.

**Test coverage:** `test_toplevel_codec()` (round trip incl. tabs/NUL titles, malformed input, empty
list) and `test_toplevel_service_roundtrip()` (created → updated → closed → clear over the real
server/client with the poll loop, verifying stable ids and change notifications).

---

## 0.9 DONE — Phase 3 item 3: desktop → `horizon-desktop` child (compiled + green)

**Status (2026-08-05): DONE (compiled + built in debug; live respawn checkpoint pending a real
session — no Wayland compositor in CI).** The in-process `DesktopApp` moved out of the supervisor into a
`horizon-desktop` child, decoupled from every `DockApp*` touchpoint (grep for
`app.dock|app->dock|.dock->|DockApp` under `src/desktop_shell/desktop/` is clean).

**Decoupling** (replaces the §6.1 `DockApp*` borrows):
- Output targeting (`desktop_layer.cpp:439-440`, was `dock_.settings.outputName`): reads
  `shell_config_snapshot().dock.outputName`, falls back to `app.outputName` (`desktopOutputName`).
  All-displays / auto (top-left) / name-match preserved.
- Dock reserve (4 sites): `dock_effective_bar_height_px(dockCfg)` from the config snapshot — the dock's
  own settings inotify (`settings_watch.cpp`) still hot-reloads settings-only writes, so toggles land.
- Catalog icons (3 sites, was `eh_app_drawer_resolve_catalog_icon(*app.dock,…)`): `DesktopApp` owns an
  `IconCache` (`iconCache`, theme = `sc.dock.iconTheme`); `desktop_resolve_catalog_icon` mirrors the
  dock's stem/tray/app lookup.
- Pin toggle/is-pinned (3 sites, was `dock_pinned_toggle`/`dock_is_app_pinned`): new settings-only
  `dock_settings_pinned_toggle`/`dock_settings_is_app_pinned` (`dock/pinned/dock_pinned.{h,cpp}`) mutate
  `sc.dock.pinnedApps` + persist via `dock_save_dock_settings` (no toplevel fallback — the child has no
  toplevel tracking).
- Menu/UI scale (7 sites): `dock_ui_scale(config_snapshot().dock)`.
- Settings launch (row 10): `eh::settings::request_launch_settings()`.

**Child** (`src/desktop_shell/desktop/standalone/`): own `WaylandConnection` + `DesktopApp`
(`desktop_init_on_display(DesktopApp&)`), config reloaded from disk before init; own 1 s tick + 33 ms
anim timerfds (was `session:266,276`); deferred icon rescan + mount-dialog action after each display
dispatch; own `DockMpris` listener (`EH_MPRIS_NOTIFY=0` so the supervisor's dock stays the single
now-playing emitter); `IpcClient` subscribes `config.applied` → reload + re-apply output/theme/widget
configs + `desktop_create_layers`; widget configs from prefs with settings-store fallback (parity with
old `session:640-649`); SIGINT/SIGTERM → clean exit.

**Supervisor** (`unified_shell_session.{hpp,cpp}`): spawns the child after the IPC bus is up
(`desktop_spawn_native_child()`, `desktop_spawn.{hpp,cpp}` — sibling-binary resolution for build-dir
runs, stdio→`$XDG_STATE_HOME/event-horizon/horizon-desktop.log`, stale pidfile + `/proc/<pid>/exe`
reaping); SIGCHLD routed through a signalfd in `build_handlers()` reaps the three split children and
respawns the desktop child on unexpected exit (5 s rate limit). Shutdown SIGTERMs it
(SIGTERM→50×20ms→SIGKILL). In-process desktop members, timerfds, `extra_displays` registration,
`restart_desktop`, and the inotify-handler layer/widget reload were deleted.

**Build:** `horizon-desktop` executable (mirrors `horizon-notifications`: `eh_shell_core_dep` +
jemalloc `malloc_conf`/`mallctl` pin per the `desktop_list.cpp` static-init bug) +
`desktop_standalone.cpp` + `desktop_spawn.cpp` in `eh_shell_core`.

**Parent-death (hard-kill) guarantee:** every shell-spawned child (`horizon-desktop`,
`horizon-wallpaper`, `horizon-notifications`, `horizon-settings`) calls
`eh::proc::install_parent_death_guard()` (`services/process/parent_death_guard.hpp`) at the top of its
entry point. The spawn helpers mark the child's env with `EH_SUPERVISOR_PID=<spawner pid>`; the child
then sets `PR_SET_PDEATHSIG(SIGKILL)` and double-checks `getppid()` so a supervisor that died mid-spawn
cannot leave an orphan running. Because PDEATHSIG fires on *parent exit* — not on session teardown —
`pkill -9 EventHorizon` now reaps wallpaper/notifications/desktop/settings alongside the supervisor.
Standalone launches (no marker, e.g. the settings `.desktop` file) are untouched. `desktop_kill_native_child`
now verifies `/proc/<pid>/stat` comm + `/proc/<pid>/exe` before signaling (`desktop_pid_is_ours`), so a
pidfile pid reused by an unrelated process is never SIGTERM/SIGKILLed.

---

## 0.10 DONE — Phase 3 item 4: dock → `horizon-dock` child (compiled + green)

**Status (2026-08-05): DONE — built in debug + release, `meson test` green, live session smoke
clean (no Wayland compositor in CI); verified on a real session with no bugs found.** The in-process
`DockApp` (bar surfaces, start menu, launchpad, overview, tray, settings watcher, MPRIS, dock
poll/tray timerfds) moved out of the supervisor into a `horizon-dock` child. The supervisor keeps the
taskbar + toplevel tracking + gamma + Super+S; the child owns the Super-key menu toggle.

**Child** (`src/desktop_shell/dock/standalone/` + `standalone_main.cpp`): own `WaylandConnection` +
`DockApp` (`dock_init_on_display` + deferred startup), own timer/tray/settings-inotify/mpris loop fds,
weather engine, pidfile, parent-death guard. Owns the global-keyboard **Super** key:
`dock_toggle_menu_from_keyboard` (smenu-first, matches the old in-process handler; app_drawer
fallback). No settingsFn — the supervisor keeps Super+S to avoid double-fire. IPC client subscribes
`config.applied` (drag-preview sync → reload → `dock_maybe_reload_settings` → schedule frame) and
`command.request` (`settings.toggle` / `menu.toggle` / `launchpad.toggle` / `overview.toggle|open|close`).

**Supervisor** (`unified_shell_session.{hpp,cpp}`, "dock-first with seams"):
- In-process dock, `launchpad_host_`, `overview_host_`, dock loop-fd handlers, `mpris_bus_fd_`,
  `dock_deferred_init_done_`, and the launchpad/overview `PollMuxDisplay` registrations deleted.
- **New shared `ToplevelTracker`** (`src/desktop_shell/shared/toplevel/toplevel_tracker.{hpp,cpp}`)
  binds a minimal registry (wlr foreign-toplevel v3 + ext-foreign-toplevel-list on Hyprland) on the
  session's **main** display — the taskbar borrows `toplevels`/`extToplevels`/`compositorKind` from it
  (its own secondary connection still can't track toplevels), and `ToplevelBridge` publishes from it.
- Spawns `horizon-dock` after the IPC bus is up (`dock_spawn_native_child()`, sibling-binary
  resolution, stdio → `$XDG_STATE_HOME/event-horizon/horizon-dock.log`, stale pidfile +
  comm/exe reaping, SIGTERM→50×20ms→SIGKILL→reap). SIGCHLD reap + 5 s rate-limited respawn on crash,
  shutdown kill, all mirroring `horizon-desktop`.
- **Own settings watcher** (`open_state_inotify()` + `drain_inotify`, shared config_watch.hpp): reloads
  keyboard/nightlight/taskbar from raw file edits. No `config.applied` broadcast on raw edits (keeps the
  matugen-skip-on-inotify optimization); the child watches inotify independently.
- IPC handlers `launchpad` / `overview-toggle|open|close` / (taskbar) `menu-toggle` → bus forwards;
  the `config.applied` hook = taskbar redraw + embed redraw + nightlight + broadcast; drag-preview paint
  tick draws the taskbar only (dock half runs in the child on the broadcast).
- Global keyboard: supervisor keeps **Super+S** only (`settings.toggle` forward / `spawn_settings`).
  Gamma stays supervisor-owned (a single gamma client — a second one in the child would fight it).
- `sync_widget_registry()` now covers taskbar roles only (dock widget registry lives in the child).

**Taskbar Launchpad paint seam:** `Host::paint_dock_button` extracted to a shared paint-only helper
`eh::shell::launchpad::paint_launchpad_dock_button` (pure cairo, process-local icon cache; `Host`
delegates; its icon members + dtor cleanup removed). The taskbar paints its Launchpad slot through the
helper (clicks already forwarded `launchpad.toggle`); `taskbar_.launchpadHost` deleted.

**Build:** `horizon-dock` executable (mirrors `horizon-desktop`: `eh_shell_core_dep` + jemalloc pin) +
`dock_spawn.cpp` + `dock_standalone.cpp` + `toplevel_tracker.cpp` in `eh_shell_core`.

**Known behavior change:** the old in-process Super handler fell back to a taskbar-only smenu; the child
now toggles only the dock's smenu/app_drawer (the supervisor isn't a bus client, so there's no forward
path). Taskbar-smenu users toggle via the taskbar button or `eh-ipc menu-toggle` instead.

---

## 0.11 DONE — Phase 3 item 4 (taskbar half): taskbar → `horizon-taskbar` (2026-08-12)

**Status (2026-08-12): DONE — implementation complete, builds green, crash fixed.** The taskbar
bar + app drawer + popups now run in a `horizon-taskbar` child with its own `WaylandConnection` and
its own toplevel tracking. Debug + release builds clean, `meson test` 2/2 green (ipc_fabric +
horizon_colors_engine). The taskbar respawn machinery is live (dock respawn verified on a real
session 2026-08-12; the taskbar's own live-session toplevel-tracking check — decision-1 risk — is
still pending a real session).

### Design decisions (locked 2026-08-05, user-confirmed) — IMPLEMENTED as locked
1. **Toplevel tracking: the taskbar child binds its OWN `zwlr_foreign_toplevel_manager_v1` + (Hyprland)
   `ext_foreign_toplevel_list_v1` on its own connection** — mirroring `horizon-dock` — NOT the
   `ToplevelClient` bus mirror the §0.7/§0.8 fallback implied. Rationale: `horizon-dock` already binds the
   wlr manager on its own connection (`unified_wayland_registry.cpp:211`, `toplevel_hooks.cpp:29`,
   `dock_foreign_toplevel_bind_manager_and_hooks`) and its overview lists *live* windows (real-session
   verified). That empirically contradicts the §3 item 2 / §5.2 "wlr handles die on secondary connections"
   rationale that created `ToplevelTracker` + the bus service. `ToplevelClient` stays available for non-bar
   components. **RISK (needs live-session check):** if wspace really does invalidate handles on secondary
   connections, the taskbar would show stale/no running apps; fall back to `ToplevelClient` + a new
   window-command bus (`window.activate|minimize|close <id>`, resolved by `ToplevelBridge` against the
   session tracker's handle map — `ToplevelRecord.id` is already stable per handle).
2. **Gamma stays supervisor-owned** (single gamma client). The taskbar's app-drawer NightlightButton
   (`taskbar.cpp:1183`, calls `gammaService_->set_enabled` directly) instead publishes
   `command.request` `nightlight.toggle`; the supervisor toggles gamma + replies nothing (fire-and-forget).
   The child keeps the existing paint-state fallback (`eh::appdrawer::set_nightlight_active`).
3. **`menu-toggle` IPC stays taskbar-scoped:** after the split the supervisor forwards it as
   `command.request` `taskbar.menu.toggle` (a NEW command, distinct from the dock's `menu.toggle`) so only
   the taskbar child toggles. The dock's `menu.toggle` handling is untouched.

### What shipped this session (2026-08-12)
- **Child** (`src/desktop_shell/taskbar/standalone/taskbar_standalone.{hpp,cpp}` +
  `standalone_main.cpp`, jemalloc mirror of `dock/standalone_main.cpp`):
  `run_taskbar_standalone()` = parent-death guard → `shell_config_reload_from_disk_now()` →
  `taskbar_init_on_display` on its own `WaylandConnection` → attach own `ForeignToplevels` /
  `ExtForeignToplevels` (taskbar tracks its own toplevels; `taskbar.toplevels = &mine`) →
  `compositorKind = detect_compositor_kind()` → own 1 s poll timerfd + tray fd + mpris
  poll/event fds + settings-inotify fd → IPC client (subscribes `config.applied` +
  `command.request`) → pidfile → `PollMuxLoop` (display fd + extra fds) → cleanup + pidfile
  unlink. `taskbar_init_deferred_startup` (tray bus + MPRIS + bluetooth) runs on the child's
  loop. `config.applied` → `taskbar_maybe_reload_settings` → schedule frame + nightlight
  paint-state sync; `command.request` handles `settings.toggle` + `taskbar.menu.toggle`.
- **Nightlight over the bus (decision 2):** app-drawer NightlightButton (child has no gamma
  client) flips its paint state via `taskbar_set_nightlight_toggle_fn` hook (`taskbar.{cpp,hpp}`)
  and publishes `command.request` `nightlight.toggle`. Supervisor bus **self-client**
  (`ipc_self_client_` — the server does not receive its own publishes) flips the single
  supervisor-owned gamma client (fixed 4000 K on enable, same as the old in-process toggle).
  Paint state re-syncs from the `config.applied` broadcast.
- **`taskbar.menu.toggle` command (decision 3):** NEW command, distinct from the dock child's
  `menu.toggle`. The supervisor's `menu-toggle` IPC handler now forwards `taskbar.menu.toggle`
  only, so one Super-key press / `eh-ipc menu-toggle` cannot toggle both bars.
- **Spawn/reap:** `taskbar_spawn.{hpp,cpp}` mirrors `dock_spawn` verbatim (`posix_spawnp` +
  `POSIX_SPAWN_SETSID`, sibling-binary resolution, stdio →
  `$XDG_STATE_HOME/event-horizon/horizon-taskbar.log`, stale pidfile + `/proc/<pid>` comm/exe
  `pid_is_ours` guard, SIGTERM→50×20ms→SIGKILL→reap). SIGCHLD signalfd handler in
  `build_handlers()` reaps + respawns `horizon-taskbar` on unexpected exit (5 s rate limit,
  mirroring dock/desktop); shutdown kill.
- **Session rewiring:** in-process taskbar deleted — `TaskbarApp` member,
  `taskbar_deferred_init_done_`, `taskbar_poll_timer_fd_`, `mpris_event_fd_`, the taskbar's
  hyprland-runtime fd, `rebuild_taskbar_layers`/`create_taskbar_layers`/`restart_taskbar`, the
  taskbar `PollMuxDisplay`/`extra_displays` registration, and `DragPreviewUser.taskbar`. The
  `config.applied` hook is now embed redraw + nightlight + broadcast only (the drag-preview paint
  tick died with the in-process taskbar — the child re-applies the merged preview on the
  broadcast). `sync_widget_registry` reads the taskbar widget lists from the config snapshot.
  Spawn happens once the IPC service is up; gamma/`ToplevelTracker`/Super+S stay supervisor-owned.
- **Own settings watcher:** the child opens its own inotify (`open_state_inotify()` /
  `drain_inotify`) and reloads raw file edits with `shell_config_snapshot_skip_matugen()`,
  matching the dock/desktop children (keeps the matugen-skip-on-inotify optimization).
- **meson.build:** `taskbar_spawn.cpp` + `taskbar_standalone.cpp` added to `eh_shell_core`;
  `horizon-taskbar` executable mirrors `horizon-dock` (`eh_shell_core_dep` + jemalloc
  `malloc_conf`/`mallctl` pin per the `desktop_list.cpp` static-init heap bug).
- **Crash found + fixed during this session's testing:** `drag_preview_user_` was created
  *after* `shell_config_set_applied_hook(...)` registered it, so the hook fired with a null user
  pointer on the first config apply (matugen-deferred) → SIGSEGV in
  `unified_shell_config_applied_hook`. Reordered `make_unique` before the registration
  (`unified_shell_session.cpp`). Symptom: "EventHorizon won't stay open / crashes at startup"
  — this was the startup crash, now fixed.

### Known behavior change
- A taskbar wayland protocol error now **terminates + respawns the child** (5 s rate limit)
  instead of an in-process restart; `menu-toggle` no longer touches the dock's menu.

### Research findings (this session)
- **Taskbar surface** (`TaskbarApp`, taskbar.hpp): owns its `WaylandConnection` (`taskbar_init_on_display`,
  `connect(true)`), xkb, pointer/keyboard listeners, icon cache, animation manager, `DockMpris`, tray
  bus + `trayEventFd`, app-drawer state + popups, tooltips, frame-callback watchdog, fast-start VK. All
  self-contained — the child can call `taskbar_init_on_display` / `taskbar_init_deferred_startup` /
  `taskbar_cleanup` as-is.
- **Borrows to eliminate:** `toplevels`/`extToplevels` (pointers → child owns `ForeignToplevels` +
  `ExtForeignToplevels` on its own `app.wl`, then `taskbar.toplevels = &mine`), `compositorKind` (→
  `detect_compositor_kind()`, cached global), `gammaService_` (→ bus, see decision 2).
- **Toplevel use is shallow + handle-based:** 16 sites in taskbar.cpp (app context menu: activate/minimize/
  close via `tl->handle`, pin-store fallback iterating appIds) + taskbar_paint.cpp via
  `build_running_snapshot(*app.toplevels, …)`. All keep working once the child tracks its own connection —
  NO taskbar rewrite needed (this is why decision 1 is so cheap). `extToplevels` is NOT actually referenced
  by taskbar code (grep clean) — only the dock uses its ext list.
- **`taskbar_maybe_reload_settings(app, sc)` is self-contained** (sets `enabled`, `pendingOutputRebind`,
  layer rebuild on output change). The child reloads on its own inotify AND on `config.applied`
  (drag-preview merge sync first, mirroring `dock_sync_settings_from_drag_preview`).
- **Config propagation:** children get config changes via their OWN inotify (`open_state_inotify()` +
  `drain_inotify`, shared `config_watch.hpp`) — the dock child does exactly this; `config.applied` is a
  secondary broadcast (settings saves, taskbar pin saves, matugen). Taskbar child needs BOTH: own inotify
  (raw file edits, no broadcast) + `config.applied` subscription (preview merge + matugen reloads).
- **Session touchpoints to delete/rewire** (`unified_shell_session.{hpp,cpp}`): `TaskbarApp` member +
  `taskbar_*` calls in `build_handlers` (tray fd, 1 s poll timer `taskbar_poll_timer_fd_`, hyprland
  runtime fd, `mpris_event_fd_`), `rebuild_taskbar_layers`/`create_taskbar_layers`, `on_idle_flush`
  (deferred startup, matugen blocks, taskbar flush/deferred-redraw), init block (`init_on_display`,
  layer creation, poll timer, `DragPreviewUser.taskbar`, `taskbar_.gammaService_`), `menu-toggle` handler,
  the taskbar `extra_displays` registration + `restart_taskbar`, cleanup (`taskbar_cleanup`,
  poll-timer close), `sync_widget_registry` (reads `taskbar_.settings.*` → switch to
  `shell_config_snapshot().taskbar.*`).
- **Spawn/kill/pidfile:** mirror `dock_spawn.{hpp,cpp}` verbatim (state_base dirs, pidfile,
  comm+exe `pid_is_ours` guard, sibling-binary resolution, `posix_spawnp` + `POSIX_SPAWN_SETSID`,
  stdio → `$XDG_STATE_HOME/event-horizon/horizon-taskbar.log`, SIGTERM→50×20ms→SIGKILL→reap).
- **meson:** add `taskbar_spawn.cpp` + `taskbar_standalone.cpp` to `eh_shell_core`; `horizon-taskbar`
  executable mirroring `horizon-dock` (`eh_shell_core_dep` + jemalloc `malloc_conf`/`mallctl` pin per the
  `desktop_list.cpp` static-init heap bug).

### Files created (all done)
- `src/desktop_shell/taskbar/spawn/taskbar_spawn.{hpp,cpp}` — supervisor spawn/kill + child pidfile.
- `src/desktop_shell/taskbar/standalone/taskbar_standalone.{hpp,cpp}` — `run_taskbar_standalone()`.
- `src/desktop_shell/taskbar/standalone_main.cpp` — jemalloc mirror of `dock/standalone_main.cpp`.

### Implementation order followed (this session)
1. `taskbar_spawn.{hpp,cpp}` (mirror dock) ✅
2. `taskbar_standalone` + `standalone_main.cpp` ✅
3. Nightlight/menu bus commands: child publishes `nightlight.toggle` / handles
   `taskbar.menu.toggle`; supervisor registers `nightlight.toggle` (gamma via self-client) +
   forwards `menu-toggle` → `taskbar.menu.toggle` ✅
4. Session rewiring: in-process taskbar deleted; spawn/kill/respawn `horizon-taskbar` in SIGCHLD
   handler (5 s rate limit, mirroring dock/desktop); `sync_widget_registry` from config snapshot ✅
5. meson.build (sources + executable) ✅
6. Build debug + release, `meson test` (ipc_fabric + horizon_colors_engine both green) ✅
7. Live-session check — ESPECIALLY the decision-1 risk (toplevel-on-secondary-connection):
   **PENDING** (dock half verified live 2026-08-12; taskbar session check still to do). Then docs
   (CHANGELOG updated; this section).

### Thoughts / notes
- Decision 1 was implemented (own toplevel tracking in the child, mirroring `horizon-dock`); it
  still carries the §8 contradiction. The dock child's working overview strongly suggested it
  would hold. If the live check shows the taskbar's running-app list is stale on the user's
  wspace, we flip to `ToplevelClient` + window-command bus (option A was fully specced: add
  `serial` to `ToplevelRecord` OR keep an id→handle map in `ToplevelBridge`; ~19 taskbar call
  sites become id lookups + bus sends).
- The `taskbar_.gammaService_` field is dead in the child (removed); the null-fallback paint
  toggle (`eh::appdrawer::set_nightlight_active`) stays so a no-session (`horizon-taskbar`
  launched bare) run still flips the icon.
- `menu.toggle` collision (both bars subscribed) is the reason for the new `taskbar.menu.toggle` command;
  do NOT reuse `menu.toggle` for the taskbar or a single publish would toggle both bars.
- `sync_widget_registry`'s launchpad/app_drawer/smenu rows already forward `launchpad.toggle`; after the
  taskbar split the session's registry rows are config-derived and the activations still target the dock
  child — no behavior change, just a config-snapshot source swap.
- The session's drag-preview paint tick + `DragPreviewUser.taskbar` die with the in-process taskbar; the
  child re-applies the merged preview on `config.applied` (its `taskbar_maybe_reload_settings` reads
  `shell_config_snapshot()`, which carries the overlay while a settings drag is active).

---

**Scope:** `src/` (764 files, ~168,600 lines: 321 `.cpp`, 443 `.hpp/.h`), `meson.build`, 5 executables.

---

## 1. Current state (today)

### 1.1 Process model — supervisor + 5 child processes, ~14 Wayland connections

Entry: `src/bootstrap/entry/entry_point.cpp` → `Application::run` (`app_bootstrap.cpp`) → `UnifiedShell::run` (`unified_shell.cpp`) → `UnifiedShellSession::run` (`src/bootstrap/session/detail/unified_shell_session.cpp:545`).

`UnifiedShellSession` owns one event loop (`PollMuxLoop`, `src/bootstrap/loop/poll_mux.hpp`) and spawns 5 children after the IPC bus is up. The supervisor itself retains lockscreen/polkit/gamma/idle + the IPC bus + config watcher + matugen. SIGCHLD signalfd respawns `horizon-desktop`, `horizon-dock`, `horizon-taskbar` on crash (5 s rate limit per child). Wallpaper and notifications are reaped but not auto-respawned (restart on next startup only).

**Supervisor-owned components (in-process, not yet split):**

| Component | Wayland connection | Own loop fds? | Restart on protocol error? |
|---|---|---|---|
| IPC server | — (Unix socket) | `session:293` | ✅ rebindable |
| Lockscreen | own connection (`session:757`) | own seat keyboard+pointer+xkb; outputs re-synced on dispatch | ⚠️ connection disabled on protocol error (no restart yet) |
| Polkit dialog (opt) | own connection (`session:981`) | via `on_gather_fds` + own-connection seat | ⚠️ connection disabled on protocol error (no restart yet) |
| Gamma | own connection (`session:700`) | gamma fd | ⚠️ connection disabled on protocol error (no restart yet) |
| Idle | own connection (`session:715`) | — | ⚠️ connection disabled on protocol error (no restart yet) |

**Child processes (separate processes, SIGCHLD respawn):**

| Child | Executable | Wayland connection | Spawned after | Restart on crash? |
|---|---|---|---|---|
| `horizon-wallpaper` | `horizon-wallpaper` | own connection (`connect(false)`, no workspaces) | IPC bus up | ❌ reaped only (Phase 4 respawn) |
| `horizon-notifications` | `horizon-notifications` | own connection (`connect(false)`) | IPC bus up | ❌ reaped only (Phase 4 respawn) |
| `horizon-desktop` | `horizon-desktop` | own connection (`connect(true)`) | IPC bus up | ✅ SIGCHLD respawn, 5 s rate limit |
| `horizon-dock` | `horizon-dock` | own connection (`connect(true)`); launchpad + overview inside child on their own connections | IPC bus up | ✅ SIGCHLD respawn, 5 s rate limit |
| `horizon-taskbar` | `horizon-taskbar` | own connection (`connect(true)`) + own toplevel tracking (`zwlr_foreign_toplevel_manager_v1` + `ext_foreign_toplevel_list_v1`) | IPC bus up | ✅ SIGCHLD respawn, 5 s rate limit |

`--both`/default → `Both` (always spawns children; `--mode` param in `run()` is now unused). Other modes: `--eh-screenshot`, `--eh-settings`, `--ipc <cmd>`. The `--both` monolith path still exists but is dead code (children always spawn).

### 1.2 Build topology

- `eh_shell_core` **shared lib** (`shared_library`, meson.build:871): **~295 `.cpp` + 62 generated protocol files**, compiled once, dynamically linked by every executable. `pic: true`; `install_rpath: '$ORIGIN/../' + libdir`. Code+assets shared in physical memory across all processes.
- Executables (11):
  - `EventHorizon` — the supervisor (shell + lockscreen/polkit/gamma/idle + IPC bus + config watcher + matugen). Still accepts `--both` but always spawns children (dead code path).
  - `eh-ipc` — socket client, links **nothing** (95-line standalone, `src/ipc_client/main.cpp`).
  - `horizon-settings` — settings UI (singleton, spawned on demand, bus toggle).
  - `horizon-wallpaper` — background layers + palette decode (own connection, pidfile supervision).
  - `horizon-notifications` — org.freedesktop.Notifications D-Bus + toast host (own connection).
  - `horizon-desktop` — desktop icons + widgets (own connection, SIGCHLD respawn).
  - `horizon-dock` — dock bar + app menu + popups + control center + spotlight + launchpad + overview + tray + start menu (own connection, SIGCHLD respawn).
  - `horizon-taskbar` — taskbar + app drawer + popups + own toplevel tracking (own connection, SIGCHLD respawn).
  - `horizon-live-wallpaper` — links whole core (only needs ~6 files).
  - `horizon-disks` — links whole core.
  - `horizonClipboardDaemon` — links only `wayland-client` + clipboard.cpp.
- Orphans/dead code: `src/ux/settings/common/settings_standalone_main.cpp` (real `main()`, superseded by `horizon-settings` but still exists); `src/desktop_shell/widgets/app_drawer/program/app_drawer_program.cpp` (1384-line standalone with `main()`, not in meson.build, unreferenced).
- `desktop_list.cpp` static-init bug still present: `g_entries_cache = scan_desktop_entries()` at line 322 runs at library load time (`dlopen` of `libeh_shell_core.so`); every child triggers it; masked by jemalloc interposition in all children (all have `malloc_conf` + `mallctl("narenas")` pin).

### 1.3 Config flow (dual: in-process hook + IPC broadcast)

`config.toml` → inotify watch (`src/desktop_shell/shared/core/config_watch.hpp`, polled at `session:315`) → `shell_config_reload_from_disk_now()` → `shell_config_apply_from_memory()` (`shell_config.cpp:1920`) runs an **in-process applied hook** (`unified_shell_config_applied_hook`) that:
1. Mutates lockscreen/gamma/idle (still in-process; dock/taskbar/desktop removed — they are children).
2. Publishes `config.applied` on the IPC bus (payload = aggregate config mtime `sec.nsec`) so all child processes re-apply locally.

Each child also watches its own inotify (`open_state_inotify()` + `drain_inotify`, shared `config_watch.hpp`) for raw file edits — the IPC broadcast covers settings saves and matugen-derived changes; inotify covers raw file edits (keeps the matugen-skip-on-inotify optimization). Matugen palette authority stays single-owner (supervisor) via `async_matugen_update`'s generation check.

### 1.4 IPC protocol (framed binary + legacy text shim)

`src/services/ipc/ipc_server.hpp/.cpp` + `src/services/ipc/ipc_protocol.hpp/.cpp`. **Framed protocol:** `[0xE7 magic][u32le length][packed header u8 version, u8 type, u32 flags, u64 id = 14 B][body]`; flag `FD` → trailing `SCM_RIGHTS` fds. Types: `request`, `response`, `event`, `ack`; `subscribe`/`unsubscribe` are their own frame types.

**Legacy text protocol** kept as a compat shim on the same socket — the server peeks the first byte and routes ASCII-text connections through the old one-shot path. `eh-ipc` + `--ipc` inline client keep working unchanged.

Pub/sub: `subscribe(topic)`/`unsubscribe(topic)` frames with acks; broker keeps per-topic subscriber fds; `publish(topic, payload, fds=[])` broadcasts framed events. Server has a poll-loop contract: `poll_interests()` → `on_fd_ready(fd, revents)`.

Client library: `eh::ipc::Client` (`src/services/ipc/client.hpp/.cpp`) — blocking `connect` (retry), framed `request()` round-trip, `subscribe()`/`unsubscribe()`, nonblocking `poll_fds()`/`on_fd_ready()` + event handler, SCM_RIGHTS receive. Reused by every child; `eh-ipc` stays the dependency-light legacy client.

Active bus topics: `config.applied`, `toplevel.*` / `toplevel.list`, `notify.push` / `notify.changed`, `command.request` (settings.toggle, menu.toggle, launchpad.toggle, overview.toggle|open|close, nightlight.toggle, taskbar.menu.toggle). Remaining topics (`output.*`, `lock.state`, `wallpaper.changed`, `mpris.*`, `tray.changed`, `weather.updated`, `network.changed`) arrive with their owning components in Phase 4.

---

## 2. Crash-isolation audit (current state 2026-08-19)

| Crash scenario | Result today |
|---|---|
| Dock segfault | **Child respawned by supervisor** (5 s rate limit). Other children unaffected. |
| Taskbar segfault | **Child respawned by supervisor** (5 s rate limit). Other children unaffected. |
| Desktop segfault | **Child respawned by supervisor** (5 s rate limit). Other children unaffected. |
| Wallpaper decode crash | **Child reaped but NOT auto-respawned.** Supervisor stays up; restarts on next startup. |
| Notification crash | **Child reaped but NOT auto-respawned.** Supervisor stays up; restarts on next startup. |
| Lockscreen/PAM auth crash | **Entire shell dies.** Still in-process on supervisor connection. |
| Settings crash | **Child dies, no impact on supervisor.** `horizon-settings` is an external child (singleton, spawned on demand). Supervisor stays up. |
| Polkit dialog crash | **Connection disabled, in-process.** No restart path yet. |
| Gamma service crash | **Connection disabled, in-process.** No restart path yet. |
| D-Bus service crash (tray/mpris/network/bluetooth/audio) | **Entire shell dies.** All D-Bus services still run in the supervisor process. |

**Verdict:** the dock/taskbar/desktop isolation is now real. The supervisor survives their crashes and respawns them. Wallpaper/notifications are isolated but not auto-respawned. The remaining crash risks are lockscreen, polkit, gamma, and all D-Bus services — these are Phase 4 candidates.

---

## 3. Refactor blockers (what keeps components coupled) — audit 2026-08-19

1. **Shared `VulkanDisplayContext` across displays.** **RESOLVED by process split.** Dock/taskbar/desktop each have their own VkDevice/own display. Launchpad + overview still share the dock child's `dockVk` (same process). Settings embed path is dead.
2. **Toplevel tracking is single-connection by protocol design.** **RESOLVED.** Dock and taskbar children each bind their own `zwlr_foreign_toplevel_manager_v1` on their own connections (the wlr protocol works on secondary connections — real-session verified for dock 2026-08-12). `ToplevelClient` exists as fallback for non-bar components. The ext list remains snapshot-only on secondary connections (spike confirmed); the wlr list provides live updates.
3. **Config live-apply is an in-process function hook.** **RESOLVED.** `config.applied` broadcast over IPC bus now covers all children. In-process hook still mutates lockscreen/gamma/idle (supervisor-owned); dock/taskbar/desktop/notifications/wallpaper apply locally on the broadcast.
4. **Global singletons / borrowed pointers.**
   - `eh::notify::setInstance` — **RESOLVED**: `NotificationManager` owned by `horizon-notifications` child; `eh::notify::push_*` route over bus.
   - `wallpaper_set_native_renderer` / `g_native_renderer` — **RESOLVED**: native renderer inside `horizon-wallpaper` child.
   - `NetworkManagerService::instance()` — **NOT RESOLVED**: singleton in supervisor. Phase 4 candidate.
   - Desktop `DockApp* dock` — **RESOLVED**: decoupled in §0.9 (config reads from snapshot, pin helpers via settings-only functions).
   - Widget registry cross-links — **NOT RESOLVED**: still cross-links dock/taskbar/launchpad activations in supervisor.
   - `TaskbarApp` borrows dock toplevels — **RESOLVED**: taskbar child tracks its own toplevels on its own connection.
5. **Monolithic static lib.** **RESOLVED:** `eh_shell_core` is now a `shared_library` (meson.build:871). Code+assets shared in physical memory across all processes. `desktop_list.cpp` static-init bug persists but is masked by jemalloc in all children.

---

## 4. Existing seams (audit 2026-08-19 — many now done)

- ✅ Per-component Wayland connections — **DONE**: dock, taskbar, desktop, wallpaper, notifications each have their own `WaylandConnection` in their own process. Lockscreen/polkit/gamma/idle have own connections in the supervisor.
- ✅ `PollMuxLoop::extra_displays` — per-connection error isolation (demonstrated; used by supervisor for lockscreen/polkit/gamma/idle).
- ✅ Settings standalone path — **DONE**: `horizon-settings` child (singleton, bus toggle, `config.applied`).
- ✅ Independent executables — **DONE**: `eh-ipc`, `horizonClipboardDaemon`, `horizon-disks`, `horizon-live-wallpaper`.
- ✅ IPC command server — **DONE**: framed binary protocol + pub/sub + `eh::ipc::Client` + legacy text shim.
- ✅ `ext-foreign-toplevel-list-v1` — bound (snapshot-only on secondary connections); `zwlr_foreign_toplevel_manager_v1` works on child connections (real-session verified).
- ✅ `eh_shell_core` → `shared_library` — **DONE** (PIC, shared across processes).
- ✅ Config split per-component on disk — **DONE**: each component has its own `<state>/<component>/<component>.toml`; `shell_config_snapshot_skip_matugen()` for cheap reads.
- ✅ Wallpaper external process path — **DONE**: `horizon-wallpaper` child with pidfile supervision.
- ✅ TrayManager eventfd broadcast — existing pub/sub pattern; tray is still in supervisor but the eventfd pattern is proven.
- ✅ `WaylandConnection` — reusable per-process connection helper (used by all children).
- ✅ `PollMuxLoop` — reusable event loop (used by all children).
- ✅ Launcher/overview Hosts — own `WaylandConnection`; live inside `horizon-dock` child (satisfied by child's own `DockApp`).

---

## 5. Deep-dive: per-subsystem contracts (findings from the 8 passes)

This section is the reference for §7. Every line is a fact from the current code — the split must preserve these behaviors, fds, and ordering.

### 5.1 Dock (`src/desktop_shell/dock/`) — becomes `horizon-dock`

**Structs** — `DockApp : WaylandState` (`dock/core/dock_app.h:52`), `DockOutputLayer` (`dock_app.h:36`, holds `wl_output`, surface, layer, `bgEffect`, `surfExt`, cairo `glRaster` + `shmRaster` + optional `vkLayer`), `WaylandState` base (`shared/system/wayland_state.hpp:58`: display, registry, compositor, xdgOutputManager, shm, seat, pointer, keyboard, xkb, xdgBase, layerShell, viewporter, fractionalScaleMgr, toplevelManager, extToplevelList, gammaControlMgr, idleNotifier, sessionLockMgr, bgEffectMgr, pointerConstraints, `deferredBinds`, `ForeignToplevels toplevels`, AnimationManager, `DockMpris mpris`, IconCache, `outputSlots`, tray fields, `dockVk`/`deferredVkDrop`/`deferredVkLayers`, settingsInotifyFd). 14 popup kinds (`dock_app.h:180-205`).

**Init** — `dock_init_on_display` (`dock_bar.cpp:2139`):
1. `wl_display_connect`, registry listener `g_registry_listener` (`unified_wayland_registry.cpp:260`).
2. Non-blocking poll loop until `compositor+shm+layerShell` bound (`dock_bar.cpp:2158-2166`) — uses `wl_display_prepare_read`/`read_events`/`dispatch_pending`, NOT `roundtrip` (startup bench).
3. xkb context (`:2169`); `dock_bind_deferred_globals` (`:2172`, impl `unified_wayland_registry.cpp:206`), `dock_bind_xdg_all_slots` (`:2173`), foreign-toplevel manager + hooks (`:2174`), ext-toplevels changed cb (`:2175`); flush+dispatch (`:2178-2179`).
4. `detect_compositor_kind()` (`:2182`; `unified/compositor_kind.cpp:63`).
5. `dock_create_main_layer_surfaces` (`:2193`); `dock_maybe_reload_settings("startup")` (`:2203`); icon theme prewarm (`:2206`); tray session defer logic (`:2211-2225`).

**Registry** — essential globals bound in listener (`unified_wayland_registry.cpp:151-184`): `wl_compositor` (v4), `wl_shm` (v1), `zwlr_layer_shell_v1` (v4), `wl_output` (v4, each output → `DockOutputSlot` + `lock_add_output`). Deferred after roundtrip (`:206-258`): `wl_seat` (v5), `zwlr_foreign_toplevel_manager_v1` (v3), `ext_foreign_toplevel_list_v1` (**Hyprland only**, `:219`), `xdg_wm_base` (v2), `zxdg_output_manager_v1` (v3), fractional-scale, viewporter, gamma control, idle notifier, session lock manager, background-effect manager, pointer constraints. Output removal → slot teardown + `pendingDockOutputRebind` (`:186-204`).

**Loop fds** (`dock_install_loop_fds`, `dock_bar.cpp:2279`): `pollTimerFd` (timerfd, interval via `dock_apply_poll_timer_interval`), `settingsInotifyFd` (inotify on config dir). `trayEventFd` comes from `TrayManager::subscribe()`.

**Handlers** (all in `dock_bar.cpp`): `dock_handle_timer` (`:2297` — osd brightness/audio polls, `dock_try_start_deferred_tray`, read timerfd, settings reload every 400 ms, clock/world-clock/media/control-center tick-signature checks, battery/bluetooth polls, tooltip tick, popup redraw), `dock_handle_inotify` (`:2376` — drain inotify, auto icon-theme refresh), `dock_handle_tray` (`:2386` — drain eventfd, `dock_tray_sync_items`, `sizeDirty`, `trayStripNeedsLayerAck`, `dock_draw`), `dock_after_display_dispatch` (`:2399` — `drain_deferred_vk_drop`, bluetooth immediate draw, `deferDockRedraw` capped at 33 ms/frame).

**Cleanup** (`dock_cleanup`, `dock_bar.cpp:2428`): **two paths.** If `wl_display_get_error` → skip ALL Wayland proxy destroys (would segfault), still tear down threads/timers/mpris/popup/VK (`:2434-2463`). Normal path: osd shutdown, tooltip, tray shutdown, close pollTimerFd+settingsInotifyFd, popup_close, destroy app-menu host surfaces, destroy xdg outputs, `dock_clear_dock_layers`, then **critically**: `deferredVkLayers.clear(); deferredVkDrop.reset(); popupVkLayer.reset();` **before** `wl_display_disconnect` — the NVIDIA driver calls Wayland during swapchain teardown, so VK must die first (`:2499-2506`). Then toplevels.shutdown, destroy keyboard/xkb/pointer/seat/layerShell/registry, `wl_display_disconnect`.

**Raster** (`dock/paint/dock_raster_backend.cpp`): `VulkanDisplayContext` lazily created on the dock's display (`:178-195`; own `VkInstance`+`VkDevice`, `vulkan_wayland.cpp:251-510`); failure → `abort_vk_backend` (`:111`) → cairo/shm fallback (`use_vulkan_backend`, `:56`). Deferred destruction via `VulkanDestructionQueue` (`:120-156`). **The 30 fps redraw cap, the VK-before-disconnect rule, and the fallback path must be preserved in the child process.**

**Input** — `dock_slot_dispatch.cpp:27` (`dock_handle_slot_press`): popup-open/close logic, per-kind dismiss rules, calendar/weather/CC/launchpad slots. Requires toplevel list (app menus), output geometry, serials from seat.

### 5.2 Taskbar (`src/desktop_shell/taskbar/`) — becomes `horizon-taskbar`

- Own `WaylandConnection` (`taskbar.cpp:2598-2648`): connect(true) → own compositor/shm/layerShell/xdg-base/seat + xkb; `taskbar_bind_deferred_globals`; **init_workspaces on its own display**.
- Borrows from dock: `taskbar_.toplevels = &dock_.toplevels; taskbar_.extToplevels = &dock_.extToplevels;` (`session:639-640`), `compositorKind` (`:641`), `gammaService_` (`:746`), `launchpadHost` (`:682`).
- Sends IPC itself: `ipc_send_command` (`taskbar.cpp:169-186`) — a fire-and-forget socket client (used for menu-toggle).
- Own fds: `trayEventFd`, 1 s poll timer (`session:699-707` → handler `:256-265` marks `frameRedrawPending` + schedule_frame), frame-callback watchdog (`taskbar.cpp:2787`).
- Restart path exists in-process for protocol errors (`restart_taskbar`, `session:1179-1252`: re-init, re-wire borrowed toplevels, rebuild layers, swap `extra_displays` slot, 5 s rate limit).
- `taskbar_toggle_menu`, `taskbar_draw`, `taskbar_schedule_frame`, `taskbar_handle_tray`, `taskbar_init_deferred_startup` (own mpris, `session:449`).

### 5.3 Desktop (`src/desktop_shell/desktop/`) — becomes `horizon-desktop`

- Own `WaylandConnection` (`desktop_app.cpp:87-114`); pointer listener bound directly on its own `wl_seat` (`:28-108`).
- Layers per output (`desktop_create_layers`/`create_layers`, `desktop/core/desktop_layer.cpp`), icons (`desktop_icons.cpp`), marquee selection (`desktop_marquee.cpp`), icon context menu + desktop right-click menu (`desktop_menu.cpp`, `desktop_icon_menu.cpp`), mount dialog with **async std::future** mount/unmount (`desktop_app.hpp:166`, `desktop_mount_dialog_process_deferred_action` at `session:361`).
- Widget host (`DesktopWidgetHost`, `desktop/widgets/shared/desktop_widget_host.hpp`): configs from prefs file + settings.toml (`session:688-697`); **MPRIS injected from dock** (`desktop_.widgetHost.set_mpris(dock_.mpris.get())`, `session:452`).
- Fds: `desktop_widget_timer_fd` (1 s, `session:266`), `desktop_widget_anim_fd` (33 ms, `session:276`). Redraw: `paint_all_layers` + `wl_display_flush(desktop_.display)`.
- `DockApp* dock` uses are narrow: config reads + launch actions (see §6.7) — `desktop_layer.cpp:440`, `desktop_icons.cpp:271,817`, `desktop_icon_menu.cpp:496,835,1327`.

### 5.4 Wallpaper (`src/wallpaper/`) — becomes `horizon-wallpaper`

- `WallpaperRenderer` (`wallpaper/core/wallpaper.hpp`): display, compositor, shm, layerShell, viewporter, `layers` (per output `WallpaperOutputLayer`), `cachedImage`/`cacheMmapPtr`/`compressedImage`, `currentImage`/`currentMode`/`enabled`, `pendingDecode`, `overlayPainter` (set nowhere — dead seam).
- Layer config (`wallpaper.cpp:370-402`): namespace `event-horizon-wallpaper`, layer `BACKGROUND`, all anchors, `exclusiveZone=-1`, no keyboard. **Needs only compositor/shm/layerShell/viewporter + outputs** → trivially its own connection.
- Decode pipeline (`wallpaper.cpp:88-226`): disk cache `wallpaper.rgb24` + `wallpaper.meta` (mmap + LZ4), downscale cap 4096 (`:452`). `wallpaper_renderer_poll_decode` (`:423`) runs on idle (`session:415`). Redraw on config.
- Global native renderer (`wallpaper_apply.cpp:41-50`) vs external process paths: `EH_WALLPAPER_CMD`, swaybg, gsettings; PID-file supervision (`wallpaper.pid`, kill-previous + spawn + alive-check, `:201-216, 339-362`). This is the reference supervision pattern.
- Settings (in-process) calls `apply_saved` → native renderer when `g_native_renderer` set.

### 5.5 Lockscreen + polkit + gamma + idle — becomes `horizon-lockscreen` / supervisor-owned

- `LockScreen` (`src/desktop_shell/lockscreen/`, ctor at `session:775-783`): built from dock's display/compositor/shm/sessionLockMgr/seat/xkbState/pointerConstraints; outputs registered via `lock_add_output` (from dock registry listener, `unified_wayland_registry.cpp:179`).
- Polkit dialog (`session:1002-1019`): own layer surfaces on the main connection; `PolkitAuthService::gather_fds`/`poll_timeout`/`dispatch_glib` wired into the poll loop (`session:1127-1136`).
- Gamma service (`session:738-749`): `GammaService` on dock's gamma manager + output slots; transition fd polled (`session:287`); shared with taskbar (`taskbar_.gammaService_`).
- Idle service (`session:751-772`): `IdleService` on dock's idle notifier + seat; behaviors run `system()` on ThreadPool.

### 5.6 Settings (`src/ux/settings/`) — becomes `horizon-settings`

- **Standalone path already works**: `run_standalone()` (`settings_embed_lifecycle.cpp:333-429`) — own `WaylandConnection`, own seat bind (unless embed), thumbnail wake fd, network-manager wake fd, 30 s poll, SIGINT/SIGTERM handler, clean teardown. This is the split template.
- **Embed path** (today's default): `embed_init(display_, dock_.seat)` on the dock display (`session:471`), shared `dockVk` via `embed_register_shell_vk` (`session:478-483`), `embed_request_redraw()` fired from the config applied hook (`session:112`), `embed_after_display_dispatch` polled after every display dispatch (`session:365`, `:413`).
- Settings is the **config writer**: `write_state_settings_toml` / per-component toml writes (`shell_config.cpp:952-973` atomic tmp+rename), plus the **drag-preview overlay** (`shell_config_set_settings_drag_preview` + `shell_config_set_drag_preview_paint_tick`, `session:731-733`) which paints dock+taskbar live.
- Consumes: `WallpaperThumbnailService` (`thumbnail_wake_fd`), `NetworkManagerService`, wallpaper apply (`apply_saved`), bluetooth/audio (PipeWire), bing service, 30+ tabs.

### 5.7 Launchpad + Overview — already own connections

- `Host(DockApp&, WaylandConnection)` (`launchpad_host.hpp:35`, `overview_host.hpp:42`). Own wl conn, own seat/xkb, own layer surfaces, own `VulkanLayerSurface` (uses shared `dockVk`). `detach_vk()` on protocol error; no restart.
- `DockApp&` uses (grep): overview — `dock_.seat` (activation serials, `overview_host.cpp:293,342`), `dock_.toplevels.list()` (`:384`); launchpad — `dock_.settings.startMenuPinnedApps` (`launchpad_host.cpp:1630,1632`), `dock_.outputSlots` (`:2099`), `dock_.display` roundtrip (`:2233`), plus `dock_ui_scale(dock_.settings)` in the `Overview/` variant.
- Activate windows via `zwlr_foreign_toplevel_handle_v1_activate(handle, dock_.seat)` — needs a seat serial source, which in the split becomes the toplevel/window service (the shell owns seat serials and broadcasts them, or children use `xdg_activation_v1` on their own connection).

### 5.8 Services (D-Bus) — become `horizon-services`

| Service | Type | Fd/trigger | Notes |
|---|---|---|---|
| Tray / StatusNotifier | D-Bus (`services/tray/manager/tray_manager.hpp`) | eventfd subscribers (`trayEventFd` on dock+taskbar) | Singleton; watcher on name-owner changes |
| MPRIS | D-Bus (`services/mpris/mpris_player.hpp`, `DockMpris`) | `poll_fd` + `poll_event_fd` (`session:305-314`) | **One instance per bar** (dock + taskbar each construct their own) |
| Notifications | D-Bus (`services/notifications/notification_dbus_service.hpp`) | `notification_timer_fd` expiry (`session:241`) | `NotificationManager` + `NotificationDbusService` + `NotificationToastHost` (toasts on dock connection) + control-center widget (in dock process) |
| Weather (CC async) | — (`desktop_shell/widgets/`) | `weather_wake_fd` (`session:237`), curl_multi driven on idle (`:540`) | Feeds control-center widget + dock weather slot |
| Network | D-Bus (`services/network/core/network_manager_service.hpp`) | `wake_fd` (`session:342`), pre-started (`:980`) | Singleton; consumed by settings + CC widget |
| Polkit | D-Bus (`services/polkit/`) | `gather_fds`/`poll_timeout`/`dispatch_glib` (`session:1127-1136`) | Dialog needs layer-shell (main conn today) |
| Global keyboard | D-Bus (`services/global_keyboard/global_keyboard_handler.hpp`) | init callback dispatches to dock/taskbar menus (`session:788-847`) | `eh-service` key events → menu-toggle/settings |
| Global shortcuts | D-Bus (`services/global_shortcuts/global_shortcuts_service.hpp`) | — | `notifyKeyEvent(key_sym, state)` |
| Audio (PipeWire), Bluetooth (bluez), Keyring (secret service), UDisks2, Bing | D-Bus | — | Non-Wayland; crash-prone → prime `horizon-services` candidates |

### 5.9 Cross-cutting internals

- **`WaylandConnection`** (`wl/core/connection.cpp:245` connect): binds compositor, subcompositor, shm, seat, xdg_wm_base, layer_shell, xdg_output_manager, wl_output (per-slot logical geometry), xdg_activation, foreign_toplevel, ext_foreign_toplevel_list, session_lock, idle_notifier, idle_inhibit, background_effect, hyprland toplevel mapping/export, fractional_scale, viewporter, virtual_keyboard, ext_workspace, hyprland IPC (zdwl), data_control, gamma, screencopy, tearing, data_device, single_pixel_buffer, xdg_wm_dialog, pointer_constraints, relative_pointer, keyboard_shortcuts_inhibit, linux_dmabuf, ext image capture, wp_color_manager. Output snapshot: `logical_output_bounds()`, `output_by_name()`. `runtime_registry()` exposes the compositor backend (hyprland poll handle, `session:296-304`).
- **`PollMuxLoop`** (`bootstrap/loop/poll_mux.cpp:70-259`): ppoll with SIGTERM/SIGINT blocked outside ppoll; `extra_displays` (per-display error isolation); `get_handlers` (fd list rebuilt per iteration); `on_display` (return false = exit); `on_idle_flush`. Reusable as the child event loop.
- **`ThreadPool`** (`bootstrap/thread/thread_pool.cpp`), **`DeferredCall::callLater`** (`bootstrap/thread/thread_dispatch.cpp`) — used by config applied hook and notifications; each child needs its own.
- **`shell_config`** (`configuration/shell_config.cpp`): `g_mu`, `g_load_gen`, `g_cache`, `g_cache_no_matugen`, `g_applied_hook`, `g_drag_preview_*`. `load_uncached(skip_matugen)` (`:847`) = defaults → merge declarative dir + settings.toml + per-component tomls → normalize widgets → optional matugen. `async_matugen_update` (`:814`) runs palette derivation on a background thread, generation-checks, swaps cache, fires applied hook via `DeferredCall`. Palette derivation = `refresh_wallpaper_derived_palette(appearance, wallpaper)` + external templates + native templates + hyprland border sync.
- **IPC** (`services/ipc/ipc_server.cpp`): nonblocking AF_UNIX listener; `on_accept` reads ≤4096 B, dispatches one line, writes one response. No framing.

---

## 6. Target architecture (multi-process)

```
 ┌───────────────────────────────────────────────────────────────┐
 │  horizon-shell   (SUPERVISOR — must never die)                │
 │  · spawns + SIGCHLD-monitors every component                  │
 │  · respawn with rate-limit + backoff, escalation              │
 │  · owns: IPC command bus (broker), config watcher + broadcast │
 │  · owns: session-lock coordination, autostart, global         │
 │    shortcuts (D-Bus), idle service, gamma service             │
 │  · owns: toplevel/window event service (or horizon-windows)   │
 │  · exposes the same /tmp event-horizon-ipc.sock so `eh-ipc`   │
 │    keeps working                                               │
 ├────────────────────────────┬───────────────────────────────────┤
 │  each child = own process, own Wayland connection, own event  │
 │  loop, own VkDevice (cairo fallback available)                │
 │                                                               │
 │  horizon-dock            dock bar, app menu, popups,          │
 │                          control center, spotlight            │
 │  horizon-taskbar         taskbar + its popups                 │
 │  horizon-desktop         desktop icons + widgets              │
 │  horizon-wallpaper       background layers + palette decode   │
 │  horizon-settings        settings UI (spawned on demand)      │
 │  horizon-lockscreen      ext-session-lock + PAM               │
 │  horizon-notifications   org.freedesktop.Notifications D-Bus  │
 │                          + toast host                         │
 │  horizon-services        tray, network, bluetooth, audio,     │
 │                          mpris, udisks2 (D-Bus services)      │
 └────────────────────────────┬───────────────────────────────────┘
                              │ Wayland
                  ┌───────────┴───────────┐
                  │   Wayland Compositor   │
                  │  (Hyprland/Niri/Sway)  │
                  └───────────────────────┘
```

**Guiding rules**
1. Components may only talk to the shell over the IPC bus; never via direct `struct` pointers or globals.
2. Only the supervisor writes "component state" (running/restarting/backoff); children are stateless supervisors.
3. Config is file-driven; the shell broadcasts `config.applied` after the watcher fires; each component re-applies locally (they already watch the same files via inotify — keep both, IPC is the source of truth for UI drag-preview overrides).
4. Session-lock lives in a child with its own Wayland connection + restart path — it must not be in the dock process.
5. Keep a single-process `EventHorizon` compat mode behind a config flag during the transition; delete once all components ship.

### 6.1 Narrow component interfaces (what replaces `DockApp*`/borrowed pointers)

| Dependency today | Replaced by |
|---|---|
| `dock_.toplevels` (taskbar, overview, dock app-menu) | `toplevel.*` events + `toplevel.list` request over bus; children bind their own `wlr-foreign-toplevel-management`/`ext-foreign-toplevel-list` where possible |
| `dock_.seat` activation serial (overview, dock) | `xdg_activation_v1` on the child's own connection; else shell broadcasts serials in `toplevel.activate` |
| `dock_.settings` reads (launchpad, desktop, widgets) | per-component toml (already exists) + `config.applied` event — **DONE for desktop (item 3):** output/reserve/pin/menu-scale read from `shell_config_snapshot().dock` + `dock_settings_*` settings writes; dock hot-reloads via its own inotify |
| `dock_.outputSlots` / output geometry (launchpad, toasts, popups) | `output.*` events broadcast by supervisor (owns output tracking) + each child's own `WaylandConnection::logical_output_bounds()` |
| `dock_.mpris` (desktop widget host, control-center) | MPRIS events over bus (`mpris.*`); each child keeps its own `DockMpris` for art/metadata — **DONE for desktop (item 3):** child owns a listener `DockMpris` with `EH_MPRIS_NOTIFY=0` (dock stays the single emitter) |
| `desktop_.widgetHost.set_mpris(dock_.mpris.get())` | **DONE (item 3):** the `horizon-desktop` child owns its own `DockMpris` and calls `set_mpris` on itself; no bus event needed for the widgets |
| `eh::notify::setInstance` | NotificationManager owned by `horizon-notifications`; `notify.*` events over bus |
| `wallpaper_set_native_renderer`/`g_native_renderer` | native renderer inside `horizon-wallpaper` only; settings talks via IPC `wallpaper.apply` |
| widget registry cross-links (`session:1331`) | bus commands (`widget-activate` stays; registry rebuilt per component) |
| in-process drag preview painting both bars (`session:97`) | drag-preview overlay owned by the shell over the bus (`preview.patch`/`preview.paint`), or preview confined to the settings process |

---

## 7. The plan — fine-grained, nothing-breaks

### Phase 0 — Baseline & testability (foundation, low risk)

1. Wire `meson test` into CI — **DONE**: CircleCI `config.yml` (`build` job, right after `build-debug`); CI is root-level `config.yml` (no `.github/workflows/` exists). `just test` added for local runs. The only test (`test_horizon_colors`) now runs and passes (`horizon_colors_engine` OK).
2. Add unit tests for pure logic, core-independent (pattern: `test_horizon_colors` links only `eh_material_colors`):
   - `shell_config` parse/serialize round-trips, per-component toml precedence (declarative < settings.toml < per-component).
   - IPC framing (Phase 1 protocol) round-trip + fd-passing.
   - `output_assign` parsing, palette derivation, `dock_settings_equal`, widget-token normalization.
3. Record baseline: per-binary sizes, RSS, startup-to-ready ms (existing bench infra `eh::bench`, `src/desktop_shell/common/bench/`). Target numbers for per-process overhead. **Recorded (2026-08-05):**
   - **Before (static):** debug `EventHorizon` 177,377,824 B; release 27,790,032 B.
   - **After (shared):** debug `libeh_shell_core.so` 215,048,368 B + 165,512 B stub; release `.so` 30,647,200 B + 10,584 B stub. PIC overhead ~10%; the win is cross-process sharing of one code+asset copy (see item 5).
   - RSS/startup-to-ready not yet measured (needs a dedicated session, not the live one).
4. Add `--version`, structured per-component log tags (pid+tag), and a crash `SIGSEGV/SIGABRT` handler per process that prints component name + backtrace (`execinfo`) before re-raising (pattern already in `session:549-569`). **DONE (partial):** `--version` added (unified_shell.cpp:110, prints `EH_SHELL_VERSION`=0.9.5); crash handler now dumps a `backtrace()` before re-raising (session.cpp:549-569). Per-component log tags deferred to Phase 2-3 where they arrive naturally with process separation.
5. Convert `eh_shell_core` → `shared_library()` — **DONE**: `eh_bundled` got `pic:true`; three executables linking core got `install_rpath: '$ORIGIN/../' + libdir`; the two standalone apps that bind protocol globals directly (`horizon-disks`, `horizon-live-wallpaper`) now compile `protocol_headers`+`protocol_sources` themselves because wayland-scanner marks `*_interface` globals `WL_PRIVATE`/hidden and they cannot cross a .so boundary (previously static linking absorbed them). Verified: debug+release builds green under `-Werror`, `meson test` green, `DESTDIR` staging resolves the .so via RUNPATH, staged binary launched (loaded .so, ran config pipeline). `horizonClipboardDaemon` + `eh-ipc` unaffected (don't link core). **Checkpoint:** single-process shell fully works, `meson test` green.

### Phase 1 — IPC fabric (the foundation for everything)

**Status (2026-08-05):** framing + pub/sub + client lib + `config.applied` broadcast DONE; `test_contract_ipc` green in debug+release under `-Werror`. `toplevel.*` is live (shell-owned service, §0.8). Remaining data-feed topics (`output.*`, `notify.changed`, `lock.state`, `mpris.*`, …) are wired as the components that own them split out in Phase 2–3 — the broker machinery is already generic and tested.

1. Upgrade `ipc_server`/client to a **framed protocol** — **DONE** (`src/services/ipc/ipc_protocol.hpp/.cpp`):
   - Wire layout: `[0xE7 magic][u32le length][packed header u8 version, u8 type, u32 flags, u64 id = 14 B][body]`; flag `FD` → trailing `SCM_RIGHTS` fds.
   - Types: `request`, `response`, `event`, `ack`; `subscribe`/`unsubscribe` are their own frame types.
   - **Legacy newline protocol kept as a compat shim on the same socket** — the server peeks the first byte and routes ASCII-text connections through the old one-shot path. `eh-ipc` + `--ipc` inline client keep working unchanged.
   - All existing 16 commands keep working unchanged (server dispatch path shared by both dialects).
   - fd-passing implemented end-to-end via `sendmsg`/`recvmsg` + `SCM_RIGHTS` (server closes its copy post-delivery; client passes received fds to the event handler, which must `dup()` what it keeps).
2. Pub/sub in `IpcService` — **DONE** (`ipc_server.cpp`): `subscribe(topic)`/`unsubscribe(topic)` frames with acks; broker keeps per-topic subscriber fds; `publish(topic, payload, fds=[])` broadcasts framed events (with optional fd attachments). Server now has a poll-loop contract: `poll_interests()` → `on_fd_ready(fd, revents)`, wired into `build_handlers()` (`session:293-303`) replacing the old single listen-fd handler; connections are parked in a `pending_` set until their first byte resolves the dialect (avoids closing a connect-then-write client).
   - Topics (initial, per earlier plan): `config.applied` **live now**; `toplevel.*`, `output.*`, `notify.changed`, `lock.state`, `wallpaper.changed`, `mpris.*`, `tray.changed`, `weather.updated`, `network.changed`, `command.request` arrive with their owning components.
3. New `eh::ipc::Client` library — **DONE** (`src/services/ipc/client.hpp/.cpp`, added to `eh_shell_core`): blocking `connect` (retry), framed `request()` round-trip, `subscribe()`/`unsubscribe()`, nonblocking `poll_fds()`/`on_fd_ready()` + event handler, SCM_RIGHTS receive. Reused by every child; `eh-ipc` stays the dependency-light legacy client until Phase 3.
4. **Route the config applied hook through IPC** — **PARTIAL**: `shell_config_set_applied_hook` remains the local shim (`session:107-125`); the hook now ALSO broadcasts `config.applied` (payload = aggregate config mtime `sec.nsec`) so children can re-apply locally. The in-process dock/taskbar/settings mutation stays for now (they still live here); it is removed when those components split out in Phase 3. Drag-preview over the bus comes with the split.
5. **Checkpoint:** single process still works end-to-end, `meson test` green (debug + release), `test_contract_ipc` covers framing round-trip, legacy shim, pub/sub, unsubscribe, and SCM_RIGHTS fd-passing. No visible change.

### Phase 2 — Split the easy, self-contained components

**Status (2026-08-05): ALL DONE.** Items 1-3 shipped + verified (`meson test` green in debug+release). Item 4 (live respawn checkpoint) still requires a real Wayland session — no compositor in CI.

1. **Settings → `horizon-settings` binary.**
   - **Status (2026-08-05): DONE.** `horizon-settings` executable built (`settings_standalone_main.cpp`), links `eh_shell_core`, `install_rpath` set, .desktop `Exec=horizon-settings`.
   - `launch_settings_override` (`session:747`, `dock_app.h:164`) now routes through `eh::settings::request_launch_settings()` → installed launcher hook (toggle via bus `command.request:settings.toggle` when running, else `posix_spawnp` setsid + stdio→log, sibling-binary resolution for build-dir runs). All entry points (taskbar slot, dock slot, desktop menu, global-keyboard shortcut) go through it.
   - In-process embed path removed from the supervisor: deferred `embed_init`/`embed_register_shell_vk` block deleted, `launch_embedded_settings_thunk`/`launch_settings_embedded`/`display_for_settings_`/`seat_for_settings_` deleted. `embed_*` library functions remain (no-op when `g_embed` null) and the old `EventHorizon --eh-settings` alias still works.
   - Child (in `run_standalone`): own `WaylandConnection` + window, singleton pidfile guard (`$XDG_RUNTIME_DIR/event-horizon-settings.pid`), connects to the IPC bus, subscribes `config.applied` (→ `shell_config_reload_from_disk_now()` + deferred redraw) and `command.request` (→ `settings.toggle` closes cleanly).
   - **jemalloc note:** `settings_standalone_main.cpp` mirrors `entry_point.cpp` (`malloc_conf` + a `mallctl` pin) so the child's allocator interposes like the main binary. Without this, a **pre-existing latent heap bug** in the `desktop_list.cpp` static init (`g_entries_cache = scan_desktop_entries()`, line 322 — runs at lib load in every child) crashes under glibc malloc (`free(): invalid size` while closing a `.desktop` ifstream) but is masked by jemalloc. The other children survive only because they transitively inherit jemalloc interposition (`horizon-disks` links it directly; `horizon-live-wallpaper` by load order). TODO (hardening): root-cause with ASAN — a real OOB/UA in the desktop-entry scan path.
   - **Guardrail:** settings renders identically (standalone path unchanged from `--eh-settings`); startup benchmark vs embedded deferred to Phase 5.
 2. **Wallpaper → own connection, then `horizon-wallpaper`.**
    - **Status (2026-08-05): DONE.** Renderer owns its `WaylandConnection` (`connect(false)`, no workspaces; `wallpaper_renderer_init(r)` single-arg); `WallpaperRenderer` got a `conn` member and out-of-line dtor. New `WaylandConnection::outputs()` returns all bound outputs for layer creation.
    - `horizon-wallpaper` child (`src/wallpaper/standalone_main.cpp` + `standalone/wallpaper_standalone.cpp`): own connection, one BACKGROUND layer per output, ported idle-poll decode folded into its own poll loop (30 s idle), cairo/LZ4 disk-cache pipeline untouched. Pidfile `$XDG_STATE_HOME/event-horizon/wallpaper.pid`; log `event-horizon/horizon-wallpaper.log`; spawn/kill supervision via `wallpaper_spawn_native_child()` / `wallpaper_kill_native_child()` (SIGTERM→50×20ms→SIGKILL); `/proc/<pid>/exe` sanity check rejects stale/non-native pidfiles.
    - Supervisor spawns it right after the IPC service comes up; in-process `WallpaperRenderer` member, init block, and idle decode poll removed from `unified_shell_session.{hpp,cpp}`; shutdown SIGTERMs the child. Settings applies via `config.applied` (broadcast → child reloads + re-applies when digest differs); `wallpaper_apply_if_digest_changed` defers to the child via `shell_config_invalidate()` when the child is alive.
    - **Palette (matugen) source of truth:** the **supervisor** runs `async_matugen_update` (it already owns config) and broadcasts `config.applied` with the derived palette; wallpaper broadcast also triggers it. Children just consume. This keeps single-palette-authority and avoids N matugen runs. (Child's `wallpaper_apply_from_snapshot` uses the skip-matugen snapshot so it never re-derives the palette.)
    - **jemalloc gate:** `standalone_main.cpp` mirrors the settings child (`malloc_conf` + `mallctl` pin) — mandatory per the `desktop_list.cpp` static-init bug note above.
  3. **Notification toasts → own connection inside `horizon-notifications`.**
     - **Status (2026-08-05): DONE.** See §0.5. `horizon-notifications` child owns
       `NotificationManager` + `NotificationDbusService` + `NotificationToastHost` on its own
       connection; expiry timerfd in the child; supervisor spawns/SIGTERMs it; `notify` IPC
       command, mpris now-playing, and the settings test button publish `notify.push` over the bus.
     - `NotificationManager` + `NotificationDbusService` (org.freedesktop.Notifications) + `NotificationToastHost` move to one child process. Port `sync_notification_toast_host` to give the toast host its own layer-shell connection (child does this itself: `conn.outputs().front()` + `conn.seat()`).
    - Expiry timerfd moves with it (child owns its own `timerfd_create`).
    - Child publishes `notify.changed` (payload = `changeSerial()`); server re-broadcasts client events. Dock/control-center strip (unimplemented) subscribes in Phase 3.
    - **Guardrail:** `notify` IPC command now publishes `notify.push` (fire-and-forget "ok"); mpris now-playing + settings test button route through `eh::notify::push_*` helpers.
4. **Checkpoint:** kill `horizon-settings`/`horizon-wallpaper`/`horizon-notifications` → dock/taskbar/shell keep running; each respawns (settings on demand, wallpaper+notifications auto). **PENDING — needs a live session (no Wayland in CI).**

### Phase 3 — Split the main shells (the heavy lift)

**Status (2026-08-19): items 1-4 DONE.** Phase 3 remaining work (items 5-7) is the live-session
verification and supervisor hardening. The core child extraction is complete.

1. **Move lockscreen, polkit dialog, gamma, idle off the main connection.** **DONE (2026-08-05) — see §0.6.** Still in-process; the fork step remains for a future phase. Lockscreen/polkit/gamma/idle each have their own Wayland connections with isolated `on_error`.
2. **Toplevel/window service.** **DONE (2026-08-05) — see §0.8.** `ToplevelService` over IPC bus, `ToplevelClient` for consumers, `ToplevelBridge` wired to session-side tracker. Updated 2026-08-12: taskbar/dock children track their own toplevels on their own connections; `ToplevelClient` available for non-bar components and as fallback.
3. **Extract desktop → `horizon-desktop`.** **DONE (2026-08-05) — see §0.9.**
4. **Extract dock → `horizon-dock` and taskbar → `horizon-taskbar`.** **DONE (2026-08-12) — see §0.10, §0.11.**
5. **Extract launchpad + overview → own processes.** **DONE-via-dock (2026-08-05) — see §0.10.** They live inside `horizon-dock` on their own connections; the `Host(dock_, wl)` ctor deps are satisfied by the child's own `DockApp`. Extracting them to separate processes is possible but not currently planned (the dock child is their natural owner).
6. **Supervisor hardening.** **NOT DONE.** Missing: exponential backoff (5 s → 10 s → 20 s → 40 s → cap 2 min) and escalation (after N rapid crashes, restart whole session). Readiness handshake (child sends `hello {component, pid}` / supervisor waits for `ready`) not implemented — the spawn just returns the pid and SIGCHLD tracks exit. Single-process `--both` compat path still exists but is dead code (children always spawn).
7. **Checkpoint.** **PARTIAL.** Dock + taskbar live-session verified (2026-08-12); taskbar toplevel tracking (decision-1 risk) still pending live verification; desktop/wallpaper/notifications live respawn still pending.

### Phase 4 — Services split & cleanup

**Status (2026-08-19): NOT STARTED.** All D-Bus services remain in the supervisor process.

1. Move D-Bus services (tray/StatusNotifier, network, bluetooth, audio/PipeWire, MPRIS, UDisks2) into `horizon-services` or per-service children. These are D-Bus driven; the crash-prone ones (PipeWire, bluez) are non-Wayland-critical.
2. Move autostart into the supervisor (`session:496-505`).
3. Wire the orphan `app_drawer_program.cpp` in or delete it; delete `settings_standalone_main.cpp` orphan if superseded by `horizon-settings`.
4. Update `Docs/Architecture.md`, `Docs/ProjectMap.md`, README, packaging (`PKGBUILD`, CI package jobs), and the `.desktop` files to the new binary set.
5. Delete the single-process `--both` path and the legacy IPC shim.
6. Auto-respawn `horizon-wallpaper` and `horizon-notifications` on crash (currently reaped but not respawned).

### Phase 5 — Hardening & observability

1. Per-process log files (`~/.cache/event-horizon/log/<component>.log`) with pid/tag; crash coredumps per component.
2. Crash-injection test harness: a build flag that randomly `abort()`s a component to prove the supervisor respawns it and the shell stays up.
3. Restart metrics: count, last crash reason, escalation events — exposed over IPC (`eh-ipc stats`).
4. Optionally: systemd unit per component (or a `horizon-session` unit that starts the supervisor).

---

## 8. Risks & open questions (audit 2026-08-19)

- **Toplevel handles on secondary connections.** **RESOLVED.** `zwlr_foreign_toplevel_manager_v1` works on child connections (real-session verified for dock 2026-08-12, taskbar pending live check). `ext-foreign-toplevel-list-v1` remains snapshot-only on secondary connections. `ToplevelClient` over bus available as fallback. If taskbar's live check shows stale windows → flip to `ToplevelClient` + window-command bus.
- **Config live-apply + drag preview.** **RESOLVED.** `config.applied` IPC broadcast covers all children; in-process hook remains for lockscreen/gamma/idle only. Drag preview paint tick is gone from the supervisor; each child re-applies locally on broadcast.
- **Vulkan per process** = a VkDevice/queue per component (more VRAM). Acceptable; cairo/shm fallback exists for most surfaces. Dock + taskbar + desktop children each have their own VkDevice.
- **Matugen palette authority** stays single-owner (supervisor) via `async_matugen_update`'s generation check (`shell_config.cpp:831-844`). **Single-owner confirmed.**
- **Startup latency** with 5 sequential spawns. Currently sequential; parallel spawn + readiness handshake not yet implemented (Phase 3 item 6).
- **IPC protocol upgrade** — **DONE** (framed binary + legacy text shim on same socket). The legacy shim stays for `eh-ipc` + `--ipc` compat; eventual deletion is Phase 4.
- **Popup/overlay positioning** — children use their own connection's `logical_output_bounds()`. Dock child owns all popups. No cross-process popup positioning needed currently.
- **Drag-and-drop across components** (pin drag dock↔taskbar) — **NOT RESOLVED.** Crosses process boundaries. Currently works via settings writes (both bars read the same toml). A shell-owned preview layer or bus-based drop-support remains an open question.
- **Single-process compat mode** — `--both` still accepted but dead code (children always spawn). Should be deleted in Phase 4.
- **PAM/uid handling** — stays in supervisor (lockscreen is in-process). If lockscreen splits to its own process, PAM moves with it.
- **NVIDIA teardown rule** — replicated in all children (dock/taskbar/desktop/notifications/wallpaper all have VK-before-disconnect ordering in their standalone cleanup paths).

---

## 9. Definition of done (audit 2026-08-19)

Phase 0 done = `meson test` runs in CI and passes ✓; shared lib refactor is behavior-neutral ✓; per-component log tags — **outstanding** (deferred to Phase 2-3 where they arrive naturally; `--version` + crash backtrace are in).
Phase 1 done = framed IPC + pub/sub live (✓ framing, ✓ pub/sub machinery, ✓ `config.applied` over the bus); toplevel/notify signals flow over the bus (arrive with Phase 2-3 splits); legacy commands all still work (✓).
Phase 2 done = killing `horizon-settings`/`horizon-wallpaper`/`horizon-notifications` does not affect dock/taskbar/shell. — Settings child split ✓; Wallpaper split ✓; Notifications split ✓. Builds + `meson test` green.
Phase 3 done = kill any component (`kill -9 horizon-dock`) → supervisor respawns it in ≤ ~1 s, all other components untouched, `eh-ipc lock` still works throughout.
  - Phase 3 item 1 ✓ 2026-08-05 — lockscreen/polkit/gamma/idle off main connection.
  - Phase 3 item 2 ✓ 2026-08-05 — toplevel service over bus.
  - Phase 3 item 3 ✓ 2026-08-05 — desktop → `horizon-desktop` child.
  - Phase 3 item 4 ✓ 2026-08-12 — dock → `horizon-dock` child; taskbar → `horizon-taskbar` child.
  - Phase 3 item 5 ✓ 2026-08-05 — launchpad + overview inside dock child (not separate processes).
  - Phase 3 item 6 **NOT DONE** — supervisor exponential backoff + escalation + readiness handshake.
  - Phase 3 item 7 **PARTIAL** — dock/taskbar live-session verified; taskbar toplevel tracking + desktop/wallpaper/notifications live respawn still pending.
Phase 4 done = `EventHorizon --both` monolith path deleted; D-Bus services split out; binary set = supervisor + children only. **NOT STARTED.**
Phase 5 done = crash-injection test suite passes in CI. **NOT STARTED.**

---

## 10. Key files to touch first (audit 2026-08-19)

**Done (Phase 0-3 items 1-4):**
- `src/services/ipc/ipc_server.cpp/hpp` + `src/services/ipc/client.hpp/.cpp` — framed IPC + pub/sub ✓
- `src/services/ipc/ipc_protocol.hpp/.cpp` — wire format ✓
- `src/configuration/shell_config.cpp/hpp` — `config.applied` broadcast ✓
- `src/services/windows/` — toplevel service + bridge + client ✓
- `src/desktop_shell/dock/standalone/` — `horizon-dock` child ✓
- `src/desktop_shell/taskbar/standalone/` — `horizon-taskbar` child ✓
- `src/desktop_shell/desktop/standalone/` — `horizon-desktop` child ✓
- `src/wallpaper/standalone/` — `horizon-wallpaper` child ✓
- `src/notifications/standalone/` — `horizon-notifications` child ✓
- `src/desktop_shell/shared/toplevel/toplevel_tracker.{hpp,cpp}` — shared tracker ✓
- `meson.build` — shared lib + 11 executables ✓

**Still to touch (Phase 3 item 6 + Phase 4):**
- `src/bootstrap/session/detail/unified_shell_session.cpp` — supervisor hardening: exponential backoff, escalation, readiness handshake, delete `--both` dead code path.
- `src/desktop_shell/shared/system/wayland_state.hpp` — still carries dock-centric members; if lockscreen/polkit split, carve further.
- `src/services/tray/` — extract to `horizon-tray` or `horizon-services` (Phase 4).
- `src/services/mpris/` — extract to `horizon-mpris` (Phase 4).
- `src/services/network/` — extract to `horizon-network` (Phase 4).
- `src/services/bluetooth/` — extract to `horizon-bluetooth` (Phase 4).
- `src/services/audio/` — extract to `horizon-audio` (Phase 4).
- `src/desktop_shell/widgets/app_drawer/program/app_drawer_program.cpp` — orphan, delete or adopt.
- `src/ux/settings/common/settings_standalone_main.cpp` — orphan, delete (superseded by `horizon-settings`).
- `Docs/Architecture.md`, `Docs/ProjectMap.md`, README, packaging — update for new binary set.

---

## 11. Per-file split map (audit 2026-08-05)

**Method.** Include-graph analysis of all 321 `.cpp` files in `src/`, plus manual review of every cross-component include. 131/321 files have **zero** cross-component reach (they are either leaf widgets, pure helpers, or self-contained). Every file with cross-component includes was classified with an action (codes below).

**Action codes.**
- `KEEP` — stays in the shared lib, used by supervisor + any child (leaf helper / reusable infra). No change.
- `MOVE` — the whole file moves to a child process as-is (already clean or only self-referential).
- `EXTRACT` — coupling exists; extract an interface first, then move. Callout lists what to cut.
- `SPLIT` — file is itself a hub; dissolve/disperse across processes.
- `SUPER` — belongs in the supervisor process (config authority, power, global input, session).
- `ORPHAN` — real `main()` exists but is not wired into `meson.build` (adopt or delete).
- `CUT` — dead file / unreferenced.

### 11.1 Headline findings

1. **The dock's process boundary is "everything that renders into dock-owned layer surfaces"** — not just the bar. The popup session (`shared/popup/session/session.hpp`) opens every popup (spotlight, control-center, power-confirm, app-menu, trash, calendar, weather, volume-mixer, media-player, vpn, battery, bluetooth, tray) via `DockApp&` on dock-owned surfaces. Same for OSD, start-menu, app-drawer, all bar widgets. So the `horizon-dock` child = bar + widgets + all popups. What splits OUT of the dock is the **data feeds**: tray/mpris/audio/network/bluetooth/weather/notifications/global-shortcuts become IPC/DBus consumers.
2. **`shell_input.cpp` (`shared/system/input/`) is the hub-of-hubs** — one router that calls into DOCK(12), WIDGETS(4), SPOTLIGHT(3), CONTROLCENTER, LAUNCHPAD, DESKTOP, SETTINGS, LOCKSCREEN, SERVICES. In the split it **dissolves**: each child owns its surfaces → its own seat events → its own input dispatch; only global-shortcut/gesture dispatch stays centralized (supervisor or global-keyboard service).
3. **Taskbar piggybacks the dock's toplevel tracking via raw pointers** (`taskbar_.toplevels = &dock_.toplevels`, session.cpp:645/1213) — an acknowledged dangling-pointer hazard (taskbar.hpp:219). This is the taskbar's hardest boundary: either independent tracking per process or a shared toplevel-service (see §8 decision b).
4. **`wl/` (26 files) is a clean, reusable per-process Wayland stack.** Zero coupling to desktop components except `connection.cpp` (→ BACKENDS/UNIFIED for `runtime_registry()`/compositor-kind — minor). Every child can own a full connection/Vulkan/session-lock/idle stack via the shared lib. This is the foundation the split is built on.
5. **`backends/` (26 files) all clean** — per-compositor IPC adapters. Only `power_actions.cpp` reaches out (WL/UNIFIED) and belongs in the supervisor.
6. **`services/` are mostly leaf-clean** and each is a natural child-process candidate. The exceptions that cross boundaries are the ones that render into shell surfaces: `tray_stream_icon.cpp` (draws into dock/CC/desktop surfaces — split: service owns DBus/icon data, dock-side draws), `tray.cpp`, `polkit_auth_dialog.cpp` (renders its own window, WL:3 — fine, becomes polkit child), `autostart_service.cpp` (→DESKTOP:2 — launches desktop entries; extract the launch dep), `mpris_player.cpp` (→NOTIFICATIONS:1 for album-art toasts).
7. **Two orphan `main()`s confirmed:** `settings_standalone_main.cpp` (→ `horizon-settings`, Phase 2) and `app_drawer_program.cpp` (self-contained dock-owned program, currently not in `meson.build`; either add to dock build or delete — `CUT`).
8. **`configuration/shell_config.cpp` is single-owner today and must stay single-owner** (the supervisor). 20+ files read it directly; those reads become IPC `config.applied` consumers (or keep a read-only local snapshot broadcast by the supervisor).

### 11.2 Process map (file → target process)

| Component (dir) | #files | Action | Target process |
|---|---|---|---|
| `wl/` | 26 | `KEEP` | shared lib — per-process stack |
| `backends/` | 26 | `KEEP` (power_actions `SUPER`) | shared lib |
| `desktop_shell/common/` | 24 | `KEEP` (palette: EXTRACT CONFIG/SETTINGS reads) | shared lib |
| `desktop_shell/ui/` | 5 | `KEEP` | shared lib |
| `desktop_shell/desktop/` | 28 | `MOVE` (DockApp deps → EXTRACT) | horizon-desktop |
| `desktop_shell/dock/` | 11 | `MOVE` (service links → EXTRACT) | horizon-dock |
| `desktop_shell/widgets/` | 22 | `MOVE` | horizon-dock |
| `desktop_shell/controlcenter/` | 13 | `MOVE` | horizon-dock |
| `desktop_shell/osd/` | 3 | `MOVE` | horizon-dock |
| `desktop_shell/spotlight/` | 3 | `MOVE` | horizon-dock |
| `desktop_shell/power_confirm/` | 1 | `MOVE` | horizon-dock |
| `desktop_shell/shared/popup/*` | 11 | `MOVE` | horizon-dock |
| `desktop_shell/shared/pins/` | 1 | `KEEP` | shared lib (dock+taskbar) |
| `desktop_shell/shared/settings/` | 2 | `SUPER` | supervisor (config watch/io) |
| `desktop_shell/shared/toplevel/` | 1 | `EXTRACT` | toplevel-service (dock+taskbar) |
| `desktop_shell/shared/system/input/` | 1 | `SPLIT` | dissolves per-child |
| `desktop_shell/shared/widgets/` | 1 | `MOVE` | horizon-dock |
| `desktop_shell/taskbar/` | 5 | `MOVE` — DONE 2026-08-12 (child tracks its own toplevels; toplevel → EXTRACT NOT needed) | horizon-taskbar |
| `desktop_shell/launchpad/` | 3 | `MOVE` (DockApp deps → EXTRACT) | horizon-launchpad |
| `desktop_shell/Overview/` + `overview/` | 5 | `MOVE` (DockApp deps → EXTRACT) | horizon-overview |
| `desktop_shell/lockscreen/` | 2 | `MOVE` | horizon-lockscreen |
| `desktop_shell/notifications/` | 2 | `MOVE` | horizon-notifications |
| `desktop_shell/keyboard/` | 1 | `SUPER` | supervisor |
| `desktop_shell/unified/` | 3 | `SPLIT` | registry refactor; compositor_kind → shared lib |
| `wallpaper/` | 5 | `MOVE` (apply → EXTRACT CONFIG/DIALOG/SETTINGS) | horizon-wallpaper |
| `services/*` | 24 | `MOVE` | per-service children (see 11.4) |
| `ux/settings/` | 58 | `MOVE` (drag-preview → EXTRACT via IPC) | horizon-settings |
| `ux/disks/` | 11 | `KEEP` | already `horizon-disks` |
| `ux/Live_Wallpaper/` | 6 | `KEEP` | already `horizon-live-wallpaper` |
| `ux/Powermenu/` | 1 | `MOVE` | horizon-dock |
| `bootstrap/` | 10 | `SPLIT` | supervisor + shared lib (loop/thread) |
| `configuration/` | 1 | `SUPER` | supervisor |
| `color/` | 1 | `KEEP` | shared lib |
| `dialog/` | 2 | `KEEP` | shared lib (or settings child) |
| `platform/` | 1 | `KEEP` | shared lib |
| `ipc_client/` | 1 | `KEEP` | eh-ipc |
| `clipboard_daemon/` | 1 | `KEEP` | already separate exe |

### 11.3 The dock in detail (the biggest issue)

**Stays in `horizon-dock` (surface-bound, cannot move):**
- All 11 `dock/` files (core bar, input, launch feedback, paint/VK backend, pinned, tooltip, widget tokens).
- All 22 `widgets/` (bar widgets + popups + start-menu + app-drawer).
- All 13 `controlcenter/`, 3 `osd/`, 3 `spotlight/`, 1 `power_confirm/`.
- All 11 `shared/popup/*` (session, buffer, caret, chrome, dispatch, geometry, layout, margins, paint, popup_position) + `shared/toplevel/toplevel_hooks.cpp` + `shared/widgets/workspace_strip.cpp` + `ux/Powermenu/power_menu.cpp`.

**Splits OUT of the dock (become IPC/DBus feeds), per file:**
- `services/tray/manager/tray.cpp` (DOCK:1), `tray_stream_icon.cpp` (draws into dock/CC/desktop — keep drawing dock-side, stream icon DATA over bus).
- `services/mpris/*` — media widgets consume state via IPC `mpris.*` events instead of `dock_.mpris`.
- `services/audio/pipewire_service.cpp` — volume/mixer + OSD read volumes via IPC `audio.changed`.
- `services/network/*`, `services/bluetooth/*` — CC + widget paints read via IPC.
- Weather fetch (`control_center_weather_drive_curl_multi` + `weather_paint`) — data via IPC `weather.updated`.
- Notifications (CC strip) — via IPC `notify.changed`.
- `configuration/shell_config.hpp` reads inside dock files → local snapshot broadcast via IPC `config.applied`.

**Interface extraction required (dock):**
- `dock_bar.cpp` reaches into WIDGETS:12, SHARED:12, SERVICES:3, SPOTLIGHT:3, CONTROLCENTER:3, OSD:2, POWER_CONFIRM:1, SETTINGS:1, DESKTOP:1, LAUNCHPAD:1. The popup/widget/CC/OSD/spotlight reach is **internal** to the dock process (fine). The SERVICES + SETTINGS + DESKTOP + LAUNCHPAD reach is the boundary to cut:
  - `desktop_shell/settings/...` — the settings launch (`launch_settings_override`) and drag-preview → IPC.
  - `desktop_shell/desktop/...` (e.g. `dock_pinned.cpp`→DESKTOP:1, `dock_tooltip.cpp`→DESKTOP:1) — desktop-launch/entry lookups → shared `desktop_entries` helper stays in shared lib; direct `DockApp`-style calls → IPC.
  - `services/` direct object access → IPC consumers.
- `shell_input.cpp` (dock+everything input router) — see 11.1.2.
- `unified_wayland_registry.cpp` — binds globals and hands them to dock + lock outputs; becomes "registry owner" in the supervisor (or each child binds its own; the lock-output coupling moves with the lockscreen child).

### 11.4 Service split map (each → child process)

| File | Cross deps | Action | Child |
|---|---|---|---|
| `audio/pipewire_service.cpp` | none | `MOVE` | horizon-audio |
| `autostart/autostart_service.cpp` | DESKTOP:2 | `EXTRACT` (launch dep) | horizon-autostart |
| `bing/bing_wallpaper.cpp` | COMMON, BOOTSTRAP | `MOVE` | horizon-wallpaper |
| `bluetooth/bluez_service.cpp` + `bluetooth_agent.cpp` | PLATFORM | `MOVE` | horizon-bluetooth |
| `global_keyboard/global_keyboard_handler.cpp` | BOOTSTRAP | `MOVE` | horizon-input (or supervisor) |
| `global_shortcuts/global_shortcuts_service.cpp` | none | `MOVE` | horizon-input (or supervisor) |
| `ipc/ipc_server.cpp` | none | `KEEP` | supervisor (becomes the bus) |
| `keyring/secret_service_daemon.cpp` | COMMON | `MOVE` | horizon-keyring |
| `mpris/*` | CONFIG, NOTIFICATIONS | `MOVE` | horizon-mpris |
| `network/*` | COMMON | `MOVE` | horizon-network |
| `notifications/notification_dbus_service.cpp` | COMMON | `MOVE` | horizon-notifications |
| `polkit/*` | dialog→WL:3 | `MOVE` | horizon-polkit |
| `tray/*` | DOCK, CONTROLCENTER, DESKTOP | `EXTRACT` (icon draw) | horizon-tray |
| `udisks2/udisks2_drive_service.cpp` | none | `MOVE` | horizon-udisks2 |

### 11.5 Bootstrap/session split map

| File | Action | Notes |
|---|---|---|
| `entry/app_bootstrap.cpp`, `entry_point.cpp` | `SUPER` | thin supervisor entry |
| `entry/screenshot_mode.cpp`, `region_select.cpp` | `SUPER` | screenshot runs in supervisor |
| `session/unified_shell.cpp` | `SUPER` | `--version`/`--ipc` arg parsing stays |
| `session/detail/unified_shell_session.cpp` | `SPLIT` | the 1368-line orchestrator → supervisor core; per-component init blocks become `posix_spawn` of children |
| `loop/main_loop.cpp`, `poll_mux.cpp` | `KEEP` | shared lib (every child's loop) |
| `thread/thread_dispatch.cpp`, `thread_pool.cpp` | `KEEP` | shared lib |
| `common/registry/widget_registry.cpp` | `KEEP` | shared lib |

### 11.6 Dead / adoption candidates
- `ux/settings/common/settings_standalone_main.cpp` — `ORPHAN` → `horizon-settings` (Phase 2).
- `widgets/app_drawer/program/app_drawer_program.cpp` — `ORPHAN`/`CUT` (referenced by nothing, not in meson.build; if it's a dock-owned program mode, add to dock child build; else delete).
- `desktop/widgets/desktop_widget_host.{cpp,hpp}`, `desktop_widget_layer.{cpp,hpp}`, `desktop_widgets_preferences.{cpp,hpp}` (the TOP-LEVEL trio, not the `shared/` ones) — **dead**: not built (absent from meson.build) and included by nothing. The live implementations are `desktop/widgets/shared/*`. `CUT` candidates.
