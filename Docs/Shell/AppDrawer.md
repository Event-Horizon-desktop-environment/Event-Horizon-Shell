# App Drawer

Located in `src/desktop_shell/widgets/app_drawer/`. The app drawer (start menu / application launcher) exists in two parallel implementations that must be kept in sync for the dock and taskbar.

---

## File-by-File Detail

### `app_drawer.cpp` (1087 lines)

**Purpose:** Legacy implementation used by the dock. All functions are prefixed `eh_app_drawer_*` and operate on a `DockApp&`.

**Namespace:** Global (C-linkage-compatible for the `eh_` prefix convention).

**Anonymous namespace constants:**
- `kPopupW = 858`, `kPopupH = 680` - default popup dimensions
- `kMar = 16.0`, `kPad = 12.0`, `kPadS = 8.0`, `kPadXS = 4.0` - layout spacing
- `kLeftFrac = 0.40`, `kRightFrac = 0.60` - left/right column split ratio (dual-pane mode)
- `kSearchH = 48.0`, `kRowPitch = 64.0`, `kGridGap = 10.0`, `kGridCols = 5` - search bar / list row / grid dimensions
- `kIconSz = 46.0`, `kGridIconSz = 48.0`, `kPinIconSz = 58.0` - icon sizes for list, grid, pinned tiles
- `kPowerBtnSz = 36.0`, `kPowerBtnR = 8.0` - power button dimensions
- `kPowerBtnCount = 4` (lock, logout, restart, shutdown), `kNightlightBtnIdx = 4`
- `kCatTabH = 34.0`, `kCatTabR = 8.0`, `kCatTabGap = 6.0`, `kCatTabFont = 14.0` - category tab dimensions
- `kPopupR = 14.0`, `kFieldR = 10.0`, `kRowR = 12.0`, `kCellR = 8.0`, `kPinIconR = 8.0` - corner radii

**Anonymous namespace functions:**
- `Rect right_col(W, H)` - right column geometry (full width in smenu mode, 60% in dual-pane)
- `search_rect(W, H)` - search bar rectangle
- `search_sep_y(W)` - separator below search bar
- `list_top(W, H)` - top of scrollable app list
- `list_x(W, H)` / `list_w(W, H)` - list X position and width
- `pinned_label_y()`, `pinned_sep_y()`, `pinned_grid_top()` - pinned section Y positions
- `pinned_cell_rect(leftW, col, row)` - pinned app tile geometry
- `power_row_y(H)` - power button row Y position (bottom of popup)
- `power_sep_y(H)` - separator above power row
- `power_btn_rect(leftW, idx, H)` - individual power button position (left column, dual-pane mode)
- `smenu_footer_h()` - footer height reserved for power row in smenu mode
- `smenu_power_btn_rect(idx, totalBtns, W, H)` - centered power button position for smenu mode
- `list_bottom(H)` - bottom of scrollable list (accounts for footer in smenu mode)
- `rr(cr, x, y, w, h, rad)` - rounded rectangle path helper
- `utf8_pop_back(string)` - removes last UTF-8 character
- `truncate_to_width(cr, text, max_w, out)` - truncates text to fit max pixel width
- `cat_has_category(categories, cat)` - checks if semicolon-delimited category list contains a category

**Global state:**
- `s_smenu_mode` - static bool set at start of each API function from `app.appMenuSmenuMode`
- `kPowerBtns[5]` - power button definitions: `{ "lock", "Lock" }, { "logout", "Logout" }, { "restart_alt", "Restart" }, { "power_settings_new", "Shutdown" }, { "dark_mode", "Nightlight" }`
- `s_nightlight_active` - nightlight state
- `s_nightlight_toggle_fn` - callback to toggle system nightlight

**Public API functions:**
- `eh_app_drawer_set_nightlight_active(bool)` / `eh_app_drawer_get_nightlight_active()` / `eh_app_drawer_set_nightlight_toggle_fn(fn)` - nightlight state management
- `eh_app_drawer_popup_width()` / `eh_app_drawer_popup_height()` - returns `kPopupW` / `kPopupH`
- `eh_app_drawer_update_categories(app)` - scans desktop entries for XDG categories, builds category tab list, measures label widths
- `eh_app_drawer_pick_category_tab(app, lx, ly)` - hit test for category tabs, returns category index (-1 for "All")
- `eh_app_drawer_refresh_hits(app)` - re-executes search query against desktop entry database, applies category filter, clamps scroll, resets selection
- `eh_app_drawer_clamp_scroll(app)` - clamps scroll position within valid range
- `eh_app_drawer_ensure_sel_visible(app)` - scrolls to make selected item visible
- `eh_app_drawer_scroll_pixels(app, delta)` - applies scroll delta
- `eh_app_drawer_hit_zone(app, lx, ly)` - **main hit test**: returns `AppDrawerHitZone` (None, SearchField, CategoryTab, AppListRow, PinnedApp, PowerButton, NightlightButton). Checks: search bar -> category tabs (smenu) -> app list -> pinned apps (dual-pane) -> power/nightlight buttons
- `eh_app_drawer_hit_search_field(app, lx, ly)` - convenience for search field hit test
- `eh_app_drawer_pick_row_index(app, lx, ly)` - returns app list row index under cursor
- `eh_app_drawer_pick_pinned_index(app, lx, ly)` - returns pinned app index under cursor
- `eh_app_drawer_pick_power_index(app, lx, ly)` - returns power button index under cursor (dual-pane: left column, smenu: centered footer)
- `eh_app_drawer_paint(app, cr, paintBackdrop, paintContent, alpha)` - **main paint function**

**`eh_app_drawer_paint()` rendering flow:**
1. If `paintBackdrop`: draw rounded rectangle background with surface fill + accent border
2. If dual-pane mode: draw dimmed left column, vertical separator
3. If `!paintContent`: return
4. Clip to rounded rectangle
5. If `!s_smenu_mode` (dual-pane):
   - Draw "Pinned" label
   - Draw separator below label
   - For each pinned app: draw tile background (hover state), load and draw icon, draw truncated name
   - Draw separator above power row
   - Draw power buttons with hover highlighting
   - Draw vertical separator before nightlight
   - Draw nightlight button (dark_mode/light_mode glyph based on state)
6. If `s_smenu_mode` (centered footer):
   - Draw separator above power row
   - Draw centered power buttons (using `smenu_power_btn_rect` layout)
   - Draw vertical separator before nightlight
   - Draw centered nightlight button
7. Draw search bar (rounded field, search icon, text/placeholder, caret blink)
8. Draw separator below search bar
9. If smenu mode: draw category tab pills (centered, selected/hovered states)
10. Draw scrollable app list:
    - Grid mode (viewMode 0): grid of cells with icon + label, hover highlight
    - List mode (viewMode 1): rows with icon, name, description, hover highlight
11. If no results and query is non-empty: draw "No matching applications"
12. If dual-pane power confirm open: draw power confirmation modal
13. If pin context menu open: draw context menu
14. If row context menu open: draw row context menu

---

### `app_drawer.hpp` (51 lines)

**Purpose:** Header for legacy app drawer API. Declares all `eh_app_drawer_*` functions and `AppDrawerHitZone` enum.

**Enum:**
```cpp
enum class AppDrawerHitZone : uint8_t {
  None, SearchField, CategoryTab, AppListRow, PinnedApp, PowerButton, NightlightButton
};
```

---

### `app_drawer_overlay.cpp` (1113 lines)

**Purpose:** Refactored implementation used by the standalone app drawer program (and taskbar). Uses `AppDrawerState` struct instead of `DockApp`.

**Namespace:** `eh::shell::dock::app_drawer`

**Key difference from `app_drawer.cpp`:** Functions take `const AppDrawerState& s` instead of `DockApp& app`. Same constants and overall structure, but adapted for the standalone state struct. Uses `eh::icons::IconCache&` for icon resolution instead of `DockApp`'s cache.

**Functions (parallel to `app_drawer.cpp`):**
- `app_drawer_paint(s, cr, icons, ...)` replaces `eh_app_drawer_paint(app, cr, ...)`
- `app_drawer_hit_zone(s, lx, ly)` replaces `eh_app_drawer_hit_zone(app, lx, ly)`
- etc.

**Additional features vs. legacy:**
- Uses `IconCache` directly (not embedded in DockApp)
- `AppDrawerChromeColors` struct for flexible color overrides
- `smenu_power_btn_rect()` and `smenu_footer_h()` - centered footer layout (inlined in legacy version)

---

### `app_drawer_overlay.hpp` (115 lines)

**Purpose:** Header for refactored app drawer API.

**Structs:**
- `AppDrawerChromeColors` - color override struct with dockFill, drawerDim, accent, outline RGB components
- `AppDrawerState` - self-contained state struct: `popupW/H`, `pinnedApps`, `startMenuPinnedApps`, `query`, `hits`, `sel`, `scrollPx`, `searchFieldFocused`, `pointerX/Y`, `hoverListRow`, row/pin context menu state, `hoverPowerIdx`, `smenuMode`, `categories`, `categoryWidths`, `selectedCategory`, `hoverCategoryIdx`, `viewMode`

**Functions:** Same as legacy API but with `AppDrawerState&` parameter.

---

### `app_drawer_power_exec.cpp` / `app_drawer_power_exec.hpp`

**Purpose:** Shared power action execution. Called by both dock and taskbar on power button click.

**Function:** `app_drawer_power_exec(CompositorKind compositor, int powerButtonIndex)`

**Actions by index:**
| Index | Action | Implementation |
|---|---|---|
| 0 | Lock | Tries `loginctl lock-session`, falls back to `hyprlock` or `swaylock` |
| 1 | Logout | Compositor-specific: Hyprland (`dispatch exit`), Niri (`niri msg action quit`), Mango (`mmsg -q`), fallback `loginctl terminate-session` |
| 2 | Reboot | `systemctl reboot` |
| 3 | Shutdown | `systemctl poweroff` |

**Execution method:** Uses `posix_spawnp` to run `/bin/sh -c <command>` as a detached process (setsid, /dev/null for stdio). No waiting for child process.

---

### `app_drawer_power_modal.cpp` / `app_drawer_power_modal.hpp`

**Purpose:** Power confirmation dialog for logout, restart, and shutdown.

**Enum:** `PowerConfirmPick { Outside=-1, Cancel=0, Confirm=1, CloseX=2 }`

**Functions:**
- `layout_modal(W, H, pending_idx)` - computes geometry: card rect (max 400px wide, centered), close button, cancel button, confirm button
- `title_for(pending_idx)` - returns "Log out?", "Restart?", "Shut down?"
- `message_for(pending_idx)` - returns descriptive subtext: "End this session...", "The system will restart.", "The system will power off."
- `confirm_label_for(pending_idx)` - returns "Log out", "Restart", "Shut down"
- `paint_power_confirm_modal(cr, W, H, pending_idx, px, py, chrome)` - renders modal: shadow, rounded card, close (X) button, title, message, cancel button, confirm button (red-tinted)
- `pick_power_confirm_modal(W, H, pending_idx, lx, ly)` - hit tests modal elements

**Colors:** Confirm button uses error colors (`kErrR=0.93, kErrG=0.38, kErrB=0.42`).

---

### `app_drawer_program.cpp` (1200 lines)

**Purpose:** **Standalone executable** for the app drawer overlay. Compiled as a separate binary that communicates with the dock process via filesystem-based IPC.

**Namespaces:** Anonymous (local helpers), `eh::shell::dock::app_drawer` (state/query calls)

**Key structs:**
- `Rect` - xywh geometry
- `Request` - IPC position: `anchor` (Top/Bottom), `margin`, `x`, `toggle`
- `App` - monolithic state struct: `wl_display`, `wl_compositor`, `wl_shm`, `wl_seat`, `wl_pointer`, `wl_keyboard`, `zwlr_layer_shell_v1`, surfaces, buffers, `IconCache`, `AppDrawerState`, XKB state, context menu state, caret blink state

**Enums:** `Anchor { Top, Bottom }`

**Constants:** `kWheelStepPx = 48.0`, SIGUSR1/SIGUSR2 for IPC signaling

**Anonymous namespace functions:**
- `on_sigusr1()` / `on_sigusr2()` - signal handlers for close/reposition IPC
- `runtime_dir()`, `state_dir()`, `settings_path()` - XDG directory helpers
- `enable_debug_log_to_file_if_needed()` - redirects stderr to log file when `EH_APP_DRAWER_DEBUG` is set
- `load_drawer_pinned_apps()` / `save_drawer_pinned_apps()` - persists pinned apps to `settings.ini`
- `request_path()` / `lock_path()` - IPC file paths
- `write_request_file()` / `read_request_file_or_default()` - IPC position protocol
- `try_take_singleton_lock_or_signal_primary_and_exit()` - singleton enforcement via `flock()`. If another instance is running, sends SIGUSR1 (show/hide) or SIGUSR2 (reposition) to the existing process
- `launch_exec_command()` - double-fork + `execvp` to launch an app
- `draw()` - main render: selects Vulkan or SHM+Cairo path, clears buffer, calls `app_drawer_paint()`, draws context menu overlay, commits surface
- Wayland listeners: `registry_global`, pointer events (enter/leave/motion/button/axis), keyboard events (keymap/key/modifiers/repeat_info), seat capabilities, layer configure/closed
- `drawer_scroll()` - handles scroll deltas
- Vulkan helpers: `appdrawer_ensure_vk_raster()`, `appdrawer_present_vk_raster()`, `appdrawer_abort_vk()`
- `apply_layer_geometry()` - sets layer-shell anchor/margin/size from `Request`
- Caret blink: `appdrawer_destroy_caret_frame()`, `appdrawer_queue_caret_frame()`, `appdrawer_caret_frame_done()` - 530ms half-cycle blink

**`main()` flow:**
1. Parse CLI args (`--anchor=`, `--margin=`, `--x=`, `--toggle`, `--reposition`)
2. Write request file for position IPC
3. Take singleton lock (signal existing instance if found)
4. Init signal handlers
5. Init FontConfig, Material Symbols font
6. Connect Wayland, bind globals
7. Create zwlr_layer_shell_v1 overlay surface
8. Enter event loop: poll for signals, icon theme changes, Wayland events (50ms poll interval)

---

### `desktop_list.cpp` / `desktop_list.hpp`

**Purpose:** Desktop entry (.desktop file) catalog and search.

**Namespace:** `eh::shell::dock::app_drawer`

**Struct `DesktopEntry`:** `path`, `name`, `genericName`, `comment`, `keywords`, `categories`, `mimeTypesLower`, `exec`, `icon`, `noDisplay`, `hidden`, `terminal`

**Functions:**
- `eh_app_drawer_desktop_stem_from_path(path)` - extracts stem (filename without .desktop)
- `eh_app_drawer_menu_query(query, pinned_apps, out_hits)` - main search: filters all entries against query using fuzzy matching, merges pinned apps first
- `get_cached_entries()` / `copy_desktop_entries()` - access cached entry list
- `entry_score(entry, query)` - computes relevance score
- `is_standard_category(cat)` - checks if category is in XDG standard list
- `dock_app_drawer_entry_for_pin_in_catalog(pin, catalog)` - resolves pin string to DesktopEntry

**Anonymous namespace (desktop_list.cpp):**
- `fuzzyScore(text, pattern)` - character-by-character fuzzy matching with bonuses: word boundary +10, consecutive match +4, prefix +8, case match +1
- `scoreList(text, pattern)` - best score across semicolon-delimited list
- `xdg_application_dirs()` - discovers XDG data directories
- `parse_desktop_file(path)` - parses .desktop INI file, extracts fields, filters non-Application types
- `scan_desktop_entries()` - walks XDG directories recursively, parses .desktop files, deduplicates by stem, sorts by name
- `entries_cache_mut()` - lazy-initialized static cache
- `g_desktop_list_mutex` - mutex protecting cache for thread safety

**Standard categories list:** AudioVideo, Audio, Video, Development, Education, Game, Graphics, Network, Office, Settings, System, Utility

---

### `app_drawer_trace.cpp` / `app_drawer_trace.hpp`

**Purpose:** Debug tracing infrastructure.

**Functions:**
- `eh_app_drawer_debug_level()` - reads `EH_APP_DRAWER_DEBUG` env var (0-9), cached on first call
- `trace_line(level, component, message)` - writes `[appdrawer][component] message` to stderr if debug level >= min_level

---

### `appdrawer_zone.hpp` (16 lines)

**Purpose:** Debug string conversion for hit zones.

**Function:** `dock_appdrawer_zone_cstr(AppDrawerHitZone)` - returns C-string name of each zone.

---

### `popup_paint_app_menu.cpp` (23 lines)

**Purpose:** Bridge between dock popup system and app drawer paint function.

**Function:** `dock_popup_paint_app_menu(app, cr, config, gpu_path)`:
1. Reads overlay alpha from config
2. Calls `eh_app_drawer_paint(app, cr, true, true, alpha)`

**Why:** The dock's [`/dock_bar.cpp`] calls `dock_popup_paint_*` functions for each popup type. This file translates that call into the `eh_app_drawer_paint()` call.

---

## Two Code Paths

| Aspect | Dock (`app_drawer.cpp`) | Taskbar (`app_drawer_overlay.cpp`) |
|---|---|---|
| State type | Embedded in `DockApp` fields | `AppDrawerState` struct |
| API prefix | `eh_app_drawer_*` | `app_drawer_*` |
| Namespace | Global | `eh::shell::dock::app_drawer` |
| Smenu mode field | `app.appMenuSmenuMode` | `s.smenuMode` |
| Icon resolution | Via `DockApp::iconCache` | Via external `eh::icons::IconCache&` |
| Chrome colors | From config via `mc` struct | Via `AppDrawerChromeColors` parameter |
| Used by | `dock_input.cpp`, `popup_paint_app_menu.cpp` | `taskbar.cpp`, `app_drawer_program.cpp` |

**AGENTS.md rule:** Any change to widget rendering in one file must be replicated in the other.

---

## Layout Modes

### Dual-pane mode
- Left 40%: pinned apps (grid) + power buttons + nightlight
- Right 60%: search bar + app list (grid or list view)
- Power buttons aligned to bottom-left

### Start menu (smenu) mode
- Full width (100%)
- Search bar at top
- Category tabs centered below search
- App list (grid or list) in middle
- Power buttons + nightlight **centered at bottom**
- Footer separator line above power row

---

## Power Buttons

| Index | Icon glyph | Action | Confirmation |
|---|---|---|---|
| 0 | `lock` | Lock screen | No |
| 1 | `logout` | Logout | Yes (modal) |
| 2 | `restart_alt` | Restart | Yes (modal) |
| 3 | `power_settings_new` | Shutdown | Yes (modal) |
| 4 | `dark_mode` / `light_mode` | Toggle nightlight | No |

Nightlight glyph switches between `dark_mode` (active, blue tint) and `light_mode` (inactive, white).

---

## Hit Zone Priority Order

`eh_app_drawer_hit_zone()` checks in order:
1. Search field (highest priority)
2. Category tabs (smenu mode only)
3. App list rows
4. Pinned apps (dual-pane only, left column)
5. Power buttons (dual-pane: left column; smenu: centered footer)
6. Nightlight button (same zones as power buttons)
7. None (miss)

---

## View Modes

| Mode | Value | Description | Row height | Grid cols |
|---|---|---|---|---|
| Grid | 0 | App icons in a grid with labels below | `kGridIconSz + kGridLabelGap + kGridLabelFontPx + kGridGap` | 5 |
| List | 1 | App rows with icon, name, description | `kRowPitch` (64px) | 1 |
