# Keyboard

Located in `src/desktop_shell/keyboard/`. Applies keyboard layout settings from configuration at shell startup.

---

## File-by-File Detail

### `keyboard_settings_apply.cpp` / `keyboard_settings_apply.hpp` (56 lines)

**Purpose:** Applies `[keyboard]` config section settings to the system via `setxkbmap` and `kbdrate`.

**Namespace:** `eh::shell::keyboard`

**Function `apply_settings(const KeyboardSettings& ks)`:**
1. Reads `ks.switchShortcut` (0-6) and maps to `grp:` xkbmap option:
   - 0/4: `alt_shift_toggle`
   - 1/5: `ctrl_shift_toggle`
   - 2: `win_space_toggle`
   - 3: `caps_toggle`
   - 6: no switch shortcut
2. Reads `ks.capsLockBehavior` (0-3) and maps:
   - 1: `ctrl:nocaps`
   - 2: `caps:swapescape`
   - 3: `caps:none`
3. Reads `ks.composeKey` (0-4) and maps:
   - 1: `compose:ralt`
   - 2: `compose:rctrl`
   - 3: `compose:menu`
   - 4: `compose:rwin`
4. Builds and runs `setxkbmap -layout "<layout>" -option "<options>"`
5. If `ks.repeatRate > 0`, runs `kbdrate -r <rate> -d <delay>`
6. Called from `unified_shell_session.cpp` during initialization
