# Lockscreen

Located in `src/desktop_shell/lockscreen/`. Implements a Wayland session-lock screen.

---

## File-by-File Detail

### `session_lock.cpp` / `session_lock.hpp`

**Purpose:** Lock screen surfaces, rendering, and unlock flow.

**Constants:**
- Lock surface layer: `ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY` (locked by compositor via session-lock protocol)
- Clock font size: 48px (time), 16px (date)

**Key functions:**
- `lock_screen_init(app)` - binds `ext_session_lock_manager_v1`, creates lock surfaces per output
- `lock_screen_lock()` - calls `ext_session_lock_v1_lock()` to activate lock
- `lock_screen_unlock()` - calls `ext_session_lock_v1_unlock()` to deactivate lock
- `lock_screen_create_surfaces(app)` - creates `ext_session_lock_surface_v1` per output
- `lock_screen_paint(cr, app, output)` - renders lock screen content

**Rendering:**
1. Wallpaper background (blurred version via `ext_background_effect_v1`)
2. Dark semi-transparent overlay (dim effect)
3. Clock display (large time in center, smaller date below)
4. Media player controls (if media is playing - now playing info + transport buttons)
5. Password input field (rounded rectangle, centered below clock)
6. Hint text ("Click to type" when idle, dots/asterisks when typing)
7. Caps lock / Num lock indicators
8. Unlock button (or Enter key accepts password)

**Authentication flow:**
1. User types password in the input field
2. Enter key submits password
3. If PAM authentication succeeds: `lock_screen_unlock()` is called
4. If PAM authentication fails: error message shown, field cleared, user can retry
5. Rate limiting after N failed attempts (with cooldown timer)

**Input handling (own connection):**
- The lock screen runs on its own Wayland connection (`lock_conn_`) and owns its full seat input there:
  its own `wl_pointer` (for confinement) and its own `wl_keyboard` + `xkb_context`/`keymap`/`state`.
  While locked, the compositor routes pointer/keyboard events to the lock surfaces on that
  connection, so the password field gets keys directly — no forwarding through the dock's seat.
- Pointer: confine to lock surface, wake display
- Keyboard: password entry, modifier tracking, Enter to submit, Escape to clear,
  Ctrl+Alt+Delete emergency unlock
- Outputs come from the lock connection's globals and are re-synced on every connection dispatch
  (`LockScreen::sync_outputs`), so a monitor plugged in while locked still gets a lock surface.

---

## Dependencies

- `ext_session_lock_v1` Wayland protocol for secure locking
- PAM (Pluggable Authentication Modules) for password verification
- `ext_background_effect_v1` for background blur
