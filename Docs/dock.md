# Dock — Roadmap & Setup Phases

Planned features and optimizations for the Event Horizon dock, ordered by
dependency and priority. Each phase is self-contained: the dock ships and
runs normally after completing any phase; later phases build on top.

---

## Phase 1 — Event-Loop & Rendering Optimizations

Core performance work that removes waste without changing behaviour.

### 1a. Poll-mux fd pre-allocation

`PollMuxLoop::run` allocates `std::vector<PollSlot>` and
`std::vector<FdHandler>` on every iteration (`poll_mux.cpp:98-139`).
Pool these outside the loop and clear/refill each tick to eliminate
per-iteration heap traffic.

**Files:** `src/bootstrap/loop/poll_mux.cpp`, `poll_mux.hpp`

### 1b. Wayland buffer reuse (double/triple buffer)

`present_cpu_bura` in `vulkan_wayland.cpp:1170` allocates fresh
`wl_buffer` + `VkDeviceMemory` per resize. Maintain a small ring of
buffers keyed by (width, height, format) so successive frames at the
same size reuse an existing allocation.

**Files:** `src/wl/surface/vulkan_wayland.cpp`

### 1c. Vulkan buffer cache

Across `vkAllocateMemory` / `vkFreeMemory` calls in the present path,
keep a per-thread free-list of `VkDeviceMemory` blocks. Reuse blocks
of the same size class instead of returning them to the driver every
frame.

**Files:** `src/wl/surface/vulkan_wayland.cpp`

### 1d. Multi-threaded Cairo rasterisation

Split the dock's per-widget Cairo tile rendering across a small thread
pool (4-8 workers). Each tile's `cairo_surface_t` is independent once
the font/icon atlas is resolved. Barrier before the composite blit.

**Files:** `src/desktop_shell/dock/core/dock_bar.cpp`,
`src/desktop_shell/dock/paint/dock_raster_backend.cpp`

**Verify:** `meson compile -C build-debug && top -H -p $(pgrep EventHorizon)` —
CPU usage should drop; frame time unchanged.

---

## Phase 2 — CPU-Spin Fix (Proton/RuneScape)

The dock spins at 99.9% CPU when RuneScape (Proton) is running.
The main thread lands in `ppoll()` on every GDB snapshot, meaning
ppoll returns instantly — some fd is always readable.

### Root-cause hypothesis

Proton/Wine generates a flood of `zwlr_foreign_toplevel` events
(surface create/destroy/state-change). The `visual_dirty_hook` in
`toplevel_hooks.cpp:38` fires on every event, setting
`deferDockRedraw = true` and calling `dock_schedule_frame`. The
33ms rate-limit in `dock_after_display_dispatch` gates the actual
draw but does NOT prevent the main loop from spinning — it still
processes every wayland dispatch cycle.

### Fix approach

1. **Throttle toplevel callbacks** — coalesce `visual_dirty_hook`
   invocations with a timestamp gate: skip if <16ms since last
   fire. The compositor already batches toplevel events within a
   single `wl_display_dispatch_pending`; a time-based coalesce
   catches the remaining inter-batch floods.

2. **Break the display-read spin** — in `before_ppoll`, if
   `deferDockRedraw` is true and we are within the 33ms rate-limit
   window, skip `wl_display_prepare_read` on the main display and
   use a short timeout instead. This lets ppoll actually block
   rather than returning on every toplevel event.

3. **Diagnostic instrumentation** — the temporary `[poll-diag]`
   counter in `poll_mux.cpp` identifies which fd keeps ppoll
   returning. Remove after root-cause is confirmed.

**Files:** `src/desktop_shell/shared/toplevel/toplevel_hooks.cpp`,
`src/desktop_shell/dock/core/dock_bar.cpp`,
`src/bootstrap/loop/poll_mux.cpp`

**Verify:** Start dock with RuneScape (Proton) running.
`top -p $(pgrep EventHorizon)` should show <5% CPU.
`[poll-diag]` ips should drop to <10 when idle.

---

## Phase 3 — Smooth Autohide Transitions

Replace the current binary show/hide with an easing curve.

### 3a. Slide animation framework

Use the existing `shellAnim` (Easing::EaseOutCubic) to animate
`animOffsetPx` from hidden offset to visible offset over ~200ms.
The frame callback already calls `shellAnim.tick()`; no new
timers needed.

**Files:** `src/desktop_shell/dock/core/dock_bar.cpp`

### 3b. Hover-lift animation

When the pointer enters the dock area, animate `hoverLiftPx` from
0 to the lift target (e.g. 8px) over ~165ms. Existing
`dock_anim_start_hover_lift` already wires this; verify it fires
on pointer-enter and not just on click.

**Files:** `src/desktop_shell/dock/core/dock_bar.cpp`

**Verify:** Move mouse over dock — smooth slide-up and lift.
No visual pop or frame skip.

---

## Phase 4 — Focus Memory

Remember last-focused app per workspace; restore on workspace switch.

### 4a. Track focus changes

Subscribe to `zwlr_foreign_toplevel_handle_v1.state` (activated
flag). On state-change, record (workspace_id, app_id) in a small
`std::unordered_map`.

### 4b. Restore on workspace switch

When the compositor signals a workspace switch (Hyprland IPC
`workspace >>`), focus the remembered app for that workspace via
`hyprctl dispatch focuswindow`.

**Files:** `src/desktop_shell/shared/toplevel/toplevel_hooks.cpp`,
`src/desktop_shell/dock/core/dock_bar.cpp`

**Verify:** Switch to workspace, focus app A, switch away, switch
back — app A should regain focus.

---

## Phase 5 — Dock Hide on Fullscreen

Auto-hide the dock when any window enters fullscreen.

### 5a. Listen for fullscreen state

`zwlr_foreign_toplevel_handle_v1.state` includes a `fullscreen`
bit. Set `app.settings.dockShowDock = false` when any tracked
toplevel sets it; restore when the last fullscreen clears.

### 5b. Animate hide

Feed the state change through the Phase 3 slide animation so the
dock slides away rather than popping.

**Files:** `src/desktop_shell/shared/toplevel/toplevel_hooks.cpp`,
`src/desktop_shell/dock/core/dock_bar.cpp`

**Verify:** Open a game in fullscreen — dock slides away.
Exit fullscreen — dock slides back.

---

## Phase 6 — Notification Badges on Dock Icons

Show unread-notification counts on dock app icons.

### 6a. D-Bus notification monitor

Subscribe to `org.freedesktop.Notifications` signals
(`NotificationClosed`, `ActionInvoked`) and count open
notifications per `app_name`. Maintain a
`std::unordered_map<std::string, int>` badge counter.

### 6b. Badge overlay rendering

In the dock icon paint path, composite a small red circle with
count text in the top-right corner of the icon's allocated tile
when the badge count > 0.

**Files:** `src/desktop_shell/dock/slots/` (new file or existing
widget slot), `src/desktop_shell/dock/paint/`

**Verify:** Receive a notification — badge appears on the app icon.
Dismiss it — badge clears.

---

## Phase 7 — Per-App GPU Badge

Detect Vulkan/OpenGL apps and show a small badge.

### 7a. Detect GPU usage

At toplevel discovery time, scan `/proc/<pid>/maps` for
`libvulkan.so`, `libGL.so`, `d3dadapter12.so` (Proton). Cache
the result per app_id.

### 7b. Badge rendering

Small "GPU" or lightning-bolt icon in the bottom-right of the
dock icon tile. Only shown for apps that use GPU.

**Files:** `src/desktop_shell/shared/toplevel/toplevel_hooks.cpp`,
`src/desktop_shell/dock/paint/`

**Verify:** Launch a Vulkan game — GPU badge appears on its dock
icon. Launch a terminal — no badge.

---

## Phase 8 — Per-Output Dock Instances

Each monitor gets its own dock with independent settings.

### 8a. Multi-output layer surfaces

Create a separate `zwlr_layer_surface_v1` per output. Each
surface gets its own `DockOutputLayer` entry (already partially
supported; needs full lifecycle isolation).

### 8b. Per-output settings

Extend `DockSettings` to accept per-output overrides in the
config JSON. Fall back to global values when an output-specific
key is absent.

**Files:** `src/desktop_shell/dock/core/dock_bar.cpp`,
`src/desktop_shell/dock/standalone/dock_standalone.cpp`

**Verify:** Connect a second monitor — each monitor shows its own
dock with the correct app set and sizing.

---

## Phase 9 — Quick Settings Popover

Tap a gear icon on the dock for brightness/volume/bluetooth
without opening full settings.

### 9a. Popover shell

New layer-surface popup anchored to the gear icon position.
Contains sliders for brightness and volume, plus a bluetooth
toggle row.

### 9b. Backend integration

Brightness: read/write `/sys/class/backlight/*/brightness`.
Volume: PulseAudio or PipeWire control via
`org.freedesktop.DBus.Properties`.
Bluetooth: BlueZ5 D-Bus `org.bluez.Adapter1.Powered`.

**Files:** `src/desktop_shell/dock/popup/` (new),
`src/desktop_shell/dock/slots/`

**Verify:** Click gear icon — popover slides in with live
brightness/volume sliders. Adjust — system values change.

---

## Ordering

```
Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4
                                   ├──► Phase 5
                                   ├──► Phase 6
                                   └──► Phase 7
Phase 8 (independent, can start anytime after Phase 1)
Phase 9 (independent, can start anytime after Phase 3)
```

Phase 1 and 2 are the priority — they fix real CPU waste. Phase 3
is the UX foundation for 4-7. Phases 8-9 are feature additions
that don't depend on the others.
