# PolicyKit

Located in `src/services/polkit/`. Implements the session's PolicyKit
authentication agent as its own process, `horizon-polkit-agent` — same contract
as polkit-gnome / mate-polkit: own process, own Wayland connection, own
registration lifecycle. The supervisor only spawns/respawns/kills it.

## Key Files

| File | Purpose |
|---|---|
| `polkit_auth_service.cpp` / `polkit_auth_service.hpp` | Agent service: registration, auth state machine, GLib pump hooks, `Snapshot` for the dialog |
| `polkit_auth_dialog.cpp` / `polkit_auth_dialog.hpp` | Authentication dialog rendering and interaction (layer-shell, no GTK) |
| `eh_polkit_listener.c` / `.h` | `PolkitAgentListener` subclass: picks the identity, builds the session, forwards prompt/identity/icon/details |
| `polkit_bridge_capi.h` | C ABI bridge from the listener into `PolkitAuthService` |
| `standalone/polkit_agent_standalone.cpp` / `.hpp` | `run_polkit_agent_standalone()` — the child's whole lifecycle |
| `standalone_main.cpp` | Thin `main()` + jemalloc pin for `horizon-polkit-agent` |
| `spawn/polkit_agent_spawn.cpp` / `.hpp` | Supervisor-side posix_spawn/kill helper (setsid + log redirect) |

## Architecture

1. `EventHorizon` (supervisor) spawns `horizon-polkit-agent` once the session is
   up; SIGCHLD reaps it and respawns it with the shared
   `kChildRespawnCooldownMs` cooldown (deferred respawns are retried from
   `retry_deferred_child_respawns()`), and shutdown kills it.
2. The child registers as a `org.freedesktop.PolicyKit1.AuthenticationAgent`
   on the system bus at `/org/eventhorizon/polkit1/AuthenticationAgent` for the
   session subject (`XDG_SESSION_ID`, falling back to
   `polkit_unix_session_new_for_process_sync`). libpolkit-agent re-registers by
   itself when polkitd restarts.
3. Registration happens on a thread (`POLKIT_AGENT_REGISTER_FLAGS_NONE`); the
   child's `PollMuxLoop` folds the service's GLib fds in via
   `on_gather_fds` / `on_poll_timeout` / `on_post_poll`, exactly the hooks the
   supervisor used to install for the in-process agent.
4. Requests from polkitd arrive on `EhPolkitListener`, which chooses the first
   unix-user identity, serializes the request, and pushes it over
   `polkit_bridge_capi.h`.
5. The dialog renders the request and sends the response back through
   `PolkitAuthService::submit_response()` / `cancel_request()`; retry on a wrong
   password works as before.

A Wayland failure exits the child instead of killing the shell — the
supervisor's respawn-with-cooldown brings it back when the compositor is up.

## Snapshot fields (dialog parity)

`Snapshot` carries everything the reference agents show, each in its own field
so none can clobber another (they used to share one `input_prompt` slot, which
is why identity and prompt never reached the screen):

| Field | Source | Rendered as |
|---|---|---|
| `identity` | listener's chosen `PolkitIdentity` (current-user → root → first) | meta row, `user@host · action_id` |
| `message` | polkit's `message` | wrapped body (≤3 lines) |
| `action_id` | polkit's action id | meta row with identity |
| `details` | serialized `PolkitDetails` (`key=value, ...`, capped/sanitized) | dim single line |
| `prompt` | session `request` signal (`Password for <user>:` fix-up) | field placeholder hint |
| `icon_name` | polkit's icon name | themed icon (icon cache), `lock` glyph fallback |
| `info` | `show-info` | dim note line |
| `error` | `show-error` | themed critical note line |

Reference parity: behavior matches mate-polkit and GNOME Shell's agent
(session lifecycle, cancel, retry-keep-open, Esc/Enter, empty-submit guard).
Deliberate divergences: single identity (no user switcher), no vendor row,
fast reject on concurrent requests.

## Dialog

Compact card: 480px wide, height sized to content every paint (empty rows cost
nothing), on a tightly-sized centered overlay — no scrim, so the compositor
never blurs/dims the desktop behind it. Inter
via Pango; m3 Outlined Cancel + Filled Confirm; all colors from
`derived_chrome_colors()` (matugen / horizon-colors wallpaper palette). The
child subscribes to `config.applied` over IPC and reloads + repaints, so
wallpaper/theme changes apply live. The field clears only on a new request
(cookie change) or a failed attempt — never on mid-dialog notifications.

## Seat Input

The auth dialog runs on its own Wayland connection owned by the child. A
`WaylandSeat` bound on that connection routes input to the dialog:
- Pointer motion/buttons are forwarded to the dialog only while the pointer is over the dialog
  surface.
- Keyboard keys arrive seat-decoded as `(state, sym, utf8)` and are consumed by
  `PolkitAuthDialog::consume_keyboard_key` — the dialog no longer owns an `xkb_state`.
- The dialog picks its output from its own connection's `outputs()`.

The old `polkit_seat_dispatch` forwarding layer (global dialog registry fed by the dock's
`shell_input.cpp`) was deleted when the dialog moved to its own connection.

## Logging

- `~/EH-logs/polkit.log` — `polkit_log()` trail (`svc` / `c` / `dlg` prefixes;
  cancel paths are ENTER/BEFORE/AFTER pairs).
- `~/.local/state/event-horizon/horizon-polkit-agent.log` — the child's
  stdout/stderr (spawn log redirect).
