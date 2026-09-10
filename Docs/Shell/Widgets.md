# Widgets

Located in `src/desktop_shell/widgets/`. The widget system provides visual elements rendered within the dock/taskbar bars and as popup surfaces. Every file and function is documented below.

## Architecture

Widgets are small UI elements placed in the left, center, or right sections of the dock or taskbar bar. Widget layout is configured in `config.toml` via token strings parsed by `dock_widget_tokens.cpp`.

The widget bar paint function (`widget_bar_paint.cpp`) orchestrates all widget rendering:
1. Measures each widget slot
2. Computes section positions (left, center, right)
3. Renders each widget via its dedicated paint function
4. Draws separators between widget groups
5. Handles overflow (too many widgets for bar width)

The `dock_slot_hooks.hpp` file re-exports all widget functions from their implementation namespaces into a single `eh::shell::dock_slot_hooks` namespace, acting as a clean facade for the dock/taskbar code.

---

## File-by-File Detail

### `dock_slot_hooks.hpp` (79 lines)

**Purpose:** Namespace bridge. Re-exports all widget functions into `eh::shell::dock_slot_hooks` so dock/taskbar code accesses widgets through one namespace.

**What it re-exports:**
- All slot width functions: `dock_clock_slot_width()`, `dock_weather_slot_width()`, `dock_control_center_slot_width()`, `dock_media_slot_width()`, `dock_workspaces_slot_width()`, `dock_spacer_slot_width()`, `dock_battery_slot_width()`
- All paint functions: `paint_clock_slot()`, `paint_weather_slot()`, `paint_media_slot()`, `paint_workspaces_slot()`, `paint_spacer_slot()`, `paint_battery_slot()`, all `dock_control_center_*` paint functions
- `workspace_activate_entry()` / `workspaces_pick_index()` / `workspace_strip_poll()` - workspace interaction
- `battery_widget_init()` / `battery_widget_poll()` / `battery_widget_shutdown()` - battery lifecycle
- `clock_tick_signature_changed()` / `control_center_tick_signature_changed()` - change detection
- `slot_pill_style` sub-namespace: `corner_radius()`, `set_fill_for_state()`, `stroke_pill_after_fill_preserve()`
- Control center state accessors: `control_center_audio_state()`, `control_center_bluetooth_state()`, `control_center_network_state()`, `control_center_weather_state()`, `control_center_input_device_state()`, `control_center_output_device_state()`, `control_center_stream_list()`

**Why:** Single include point for dock/taskbar to access all widget functionality without knowing individual widget namespaces.

---

### `shared/shared_slot_paint.hpp` (30 lines)

**Purpose:** Defines `SlotKind` enum for widget identification.

**Enum:**
```cpp
enum SlotKind {
  App, Tray, Settings, Spotlight, AppMenu, Launchpad, AppDrawer,
  Clock, Weather, Media, ControlCenter, Workspaces, Trash,
  VolumeMixer, Separator, Battery, Vpn, Smenu
};
```

**Why:** Used by `widget_bar_paint.cpp` and `dock_pick.cpp` to identify widget types generically.

---

### `shared/slot_pill_style.hpp` (46 lines)

**Purpose:** Shared visual style constants for widget pills (rounded backgrounds behind each widget slot).

**Constants (inline globals):**
- `kPillHeightMul = 1.0` - pill-to-slot height ratio
- `g_opacityScale`, `g_pillR/G/B` - pill fill color (RGBA, globally overridable)
- `g_hoverAccentR/G/B` - accent color when slot is hovered
- `g_mediaBtnR/G/B`, `g_mediaGlyphR/G/B`, `g_mediaOnAccentR/G/B` - media control button colors

**Functions:**
- `corner_radius(double pill_h, double slot_w)` - returns `max(2, min(pill_h, slot_w) * 0.22)`
- `set_fill_for_state(cairo_t*, bool hovered)` - sets Cairo source color for pill fill (hovered uses accent, normal uses surface)
- `stroke_pill_after_fill_preserve(cairo_t*, bool hovered)` - strokes pill outline after fill

**Why:** Centralizes visual style so all widget pills have consistent look.

---

### `widget_bar/widget_bar_paint.h` (21 lines)

**Purpose:** Header for widget bar painting.

**Enum:** `WidgetBarLayout { DockCentered, PanelEdgeAligned }`

**Function:** `paint_widget_bar(DockApp&, cairo_t*, innerW, baseH, padX, iconY, WidgetBarLayout, left_widgets, center_widgets, right_widgets)` - main bar rendering entry point.

---

### `widget_bar/widget_bar_paint.cpp` (382 lines)

**Purpose:** Complete widget bar rendering implementation.

**Anonymous namespace helpers:**
- `panel_debug_enabled()` - checks `EH_PANEL_DEBUG` env var for debug rect overlay
- `fnv1a64(string)` - FNV-1a hash for debug rect identification
- `draw_icon_surface(cr, surface, x, y, size)` - draws a cairo surface scaled to fit a square
- `draw_slot_bg(cr, x, y, w, h)` - draws semi-transparent white background for a slot
- `draw_separator(cr, x, y, h)` - draws a 1px vertical line separator
- `section_slot_count(widget_list)` - counts visual slots a widget list occupies (handles pinned_apps expansion, running_apps expansion, tray list expansion)
- `section_width(widget_list)` - computes total width for a widget section
- `paint_one_widget(...)` - renders a single widget by name:
  - `"settings"` / `"settings_button"` - draws OS logo via `os_logo.hpp`
  - `"spotlight"` / `"distro_spotlight"` / `"app_drawer"` - draws spotlight icon
  - tray token - draws each tray icon from `DockApp::trayItems`
  - `"app_menu"` - draws 3x3 dot grid (start menu button)
  - Any other string - looks up and renders app icon from pinned/running apps
- `paint_section(...)` - iterates widget list, calls `paint_one_widget()` for each, handles group expansion and separators

**Main function - `paint_widget_bar()`:**
1. Takes left/center/right widget token lists
2. In `DockCentered` mode: measures all sections, centers them as a block
3. In `PanelEdgeAligned` mode: left anchored to left edge, right to right edge, center between them
4. Calls `paint_section()` for each section with computed geometry
5. Optionally outputs debug rectangles when `EH_PANEL_DEBUG` is set

**Why:** The widget bar is the top-level container orchestrating all widget rendering in both dock and panel modes.

---

### `clock/clock_paint.cpp` / `clock_paint.hpp` (336 lines)

**Purpose:** Clock widget - time formatting, measurement, and rendering.

**Namespace:** `eh::widgets`

**Helper functions:**
- `append_weekday_short(string&)` - appends "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
- `append_date_formatted(string&, format)` - appends date in configurable format: format 0 = "Weekday, Month Day, Year", format 1 = "YYYY-MM-DD", format 2 = "Weekday DD"
- `format_clock_lines(string& line1, string& line2, settings)` - formats time as 1-2 lines based on settings: 24h/12h mode, show seconds, show date, date format, custom strftime string, timezone override
- `make_layout(cairo_t*, text, font_size)` - creates PangoLayout for text measurement
- `widget_list_contains_clock(token_list)` - checks if "clock" token is in widget config
- `clock_tick_signature_changed(settings)` - returns true when displayed time has changed (minute boundary or second boundary depending on config)
- `dock_clock_slot_width(cr, settings)` - measures required pill width for clock text
- `paint_clock_slot(cr, x, y, w, h, settings, icon_size)` - renders the clock pill

**Rendering details:**
- Uses Pango for text layout
- Multi-line mode: line 1 in semibold, line 2 in normal weight, centered vertically
- Single-line mode: medium weight, centered vertically
- Text color uses global pill style colors

---

### `weather/weather_paint.cpp` / `weather_paint.hpp` (185 lines)

**Purpose:** Weather widget - temperature display with weather icon.

**Namespace:** `eh::widgets`

**Functions:**
- `make_layout(cairo_t*, text)` - Pango layout creation
- `weather_temp_display(temp, unit)` - formats temperature: `-- degrees symbol` for no data, `25` for normal
- `weather_layout_inner(...)` - computes layout: icon size, icon-text gap, text width, unit pill dimensions
- `widget_list_contains_weather(token_list)` - checks for "weather" token
- `dock_weather_slot_width(cr, weather_state)` - measures total slot width: icon + gap + text + unit pill
- `paint_weather_slot(cr, x, y, w, h, weather_state, icon_size)` - renders the pill

**Rendering:**
1. Weather icon (sun, cloud, rain, snow, etc. from Material Symbols)
2. Temperature text (e.g., "72")
3. Unit pill ("F" or "C") in a rounded rectangle

**Data source:** `ControlCenterWeatherState` from `control_center_paint.hpp` (populated by background weather service).

---

### `media/media_paint.cpp` / `media_paint.hpp` (585 lines)

**Purpose:** Media player widget - now playing display with album art and transport controls.

**Namespace:** `eh::widgets`

**Constants:** `kSpacingS=6`, `kSpacingXS=4`, `kBtnSmallD=16` (prev/next buttons), `kBtnPlayD=19` (play/pause button), `kMediaOuterHPad=4`, `kPrimR/G/B=0.90`, `kOnPrimR/G/B=0.10`, `kSurfR/G/B=1.0`

**State tracking:**
- `g_xfade` (crossfade state) - `MediaCrossfade` struct with `active`, `t0` (start time), `kDuration=0.28s`
- `g_last_id`, `g_last_title`, `g_last_artist`, `g_last_active`, `g_last_art` - previous track state for change detection

**Functions:**
- `make_layout(cr, text)` - Pango layout
- `layout_set_natural_single_line(layout)` - prevents text wrapping
- `layout_natural_pixel_width(layout)` - measures unwrapped text width
- `title_artist_sep_width_px()` - measures " ... " separator width
- `marquee_triangle_offset(time, speed)` - computes smooth back-and-forth scroll offset for overflow text
- `ctrl_row_width()` - computes width of prev + play + next buttons
- `fill_circle(cr, x, y, r)` - filled circle path
- `rrect(cr, x, y, w, h, r)` - rounded rectangle path
- `widget_list_contains_media(tokens)` - checks for "media" token
- `dock_media_slot_width(cr, player_snapshot)` - measures pill width = album art + gap + title/artist text + control buttons
- `paint_media_slot(cr, x, y, w, h, player, icon_size)` - renders the pill, returns true if marquee animation active

**Rendering (most complex widget):**
1. **Album art**: circular crop of album art image, or fallback dots pattern if no art
2. **Crossfade**: smooth transition between old and new tracks (0.28s duration)
3. **Title + artist**: two lines with marquee scroll for overflow text
4. **Transport controls**: prev (16px circle), play/pause (19px circle), next (16px circle)
5. **Hover states**: accent-colored rings around control buttons

**Data source:** MPRIS via `mpris_player.hpp` - `MprisPlayer` snapshot with metadata, art, playback state.

---

### `battery/battery_paint.cpp` / `battery_paint.hpp` (679 lines)

**Purpose:** Battery widget - polling, rendering, and popup.

**Namespace:** `eh::widgets`

**Constants:** D-Bus service = `org.freedesktop.UPower`, path = `/org/freedesktop/UPower`

**Enums:** `BatteryState { Unknown, Charging, Discharging, Empty, FullyCharged, PendingCharge, PendingDischarge }`

**Struct:** `UPowerState { int percentage; BatteryState state; bool isPresent; bool onBattery; }`

**Background thread:** Polls UPower every 5 seconds via a dedicated background thread. Uses `eventfd` for wake signaling. Reads battery percentage, state, and presence. Updates `g_cachedState` under mutex.

**Functions:**
- `battery_glyph_for(pct, state)` - selects Material Symbols: `battery_full`, `battery_charging_full`, `battery_alert`, `battery_unknown`
- `battery_widget_init()` - starts poll thread (refcounted, safe for multiple dock/taskbar instances)
- `battery_widget_poll()` - triggers immediate poll
- `battery_widget_shutdown()` - signals thread stop, joins thread
- `widget_list_contains_battery(tokens)` - checks for "battery" token
- `dock_battery_slot_width(cr, icon_size, show_pct)` - measures width: icon + optional percentage text
- `paint_battery_slot(cr, x, y, w, h, state, icon_size)` - renders battery pill with icon and optional percentage
- `battery_popup_height()` - returns 160px
- `dock_battery_popup_paint(cr, w, h, state)` - renders detail popup with large icon, percentage, state text, close button
- `dock_battery_popup_handle_click(app, lx, ly)` - handles close button click

**Debug:** `EH_BATTERY_DEBUG` env var overrides state for testing (charging, discharging, low, full, absent).

---

### `workspaces/workspaces_model.hpp` (21 lines)

**Purpose:** Workspace data model. Defines the `WorkspaceEntry` struct used by the workspace widget and shared workspace strip painting.

**Struct `WorkspaceEntry`:** `int id`, `string label`, `bool active`, `bool occupied`, `bool urgent`, `vector<string> app_keys`

**Enum `WorkspaceDisplayMode`:** `Id` (show workspace number), `Name` (show workspace name), `None` (hide)

### `workspaces/workspaces_paint.cpp` / `workspaces_paint.hpp` (704 lines)

**Purpose:** Workspace widget - visual workspace switching with animations.

**Namespace:** `eh::widgets`

**Constants:** `kPrimR/G/B = 0.90`, `kSurfR/G/B = 0.96/0.98/0.99`

**Functions:**
- `widget_setting(config, key, default)` - reads workspace-specific config
- `parse_display_mode(string)` - parses "name" (show workspace names) or "none" (show dots only)
- `parse_show_workspace_apps(config)` - whether to show app icons on workspace cells
- `parse_max_workspace_icons()` - maximum app icons per cell (1-8, default 4)
- `workspace_max_slots_from_settings(config)` - max workspaces to display (0-32, default 5)
- `workspace_strip_apply_max_slots(entries, max)` - trims/pads workspace list
- `fetch_mmsg()` - runs `mmsg -g -t` to query Mango workspace state via PipeWire
- `mangowm_parse_workspaces(string)` - parses Mango's JSON workspace output
- `layout_cells(entries, total_w, gap)` - computes per-workspace cell geometry with proportional sizing
- `draw_scaled_icon_with_alpha(cr, surface, x, y, size, alpha)` - renders app icon at specified alpha
- `draw_steam_tile(cr, x, y, size)` - special rendering for Steam app tiles
- `widget_list_contains_workspaces(tokens)` - checks for "workspaces" token
- `WsStripAnimState` - animation helper:
  - `updateTargets(entries, total_w)` - starts smooth transition to new cell positions
  - `getX(idx)`, `getW(idx)` - interpolated position/size with cubic easing
  - `progress()` - 0..1 animation progress (200ms duration)
- `workspace_strip_poll(app)` - polls workspace state from either Wayland (`global_wayland_workspaces()`) or Mango (`mmsg`), applies max slots, triggers animation
- `dock_workspaces_slot_width(cr, entries, config)` - measures total strip width
- `paint_workspaces_slot(cr, x, y, w, h, entries, config, icon_cache, mouse_x)` - renders the strip
- `workspaces_pick_index(lx, entries, total_w, gap)` - hit tests which workspace cell was clicked
- `workspace_activate_entry(app, entry)` - switches to workspace via `mmsg -t N` or Wayland workspace API

**Rendering:**
1. Pill background spanning all cells
2. Per-workspace cells: active cell highlighted with accent, occupied cells muted, empty cells dimmed
3. Optional workspace labels (name or number)
4. Optional app icons within each cell
5. Smooth position/size animations between workspace changes

**Dual data source:** Supports both `ext-workspace-v1` Wayland protocol and Mango's `mmsg` IPC.

---

### `spacer/spacer_paint.cpp` / `spacer_paint.hpp` (24 lines)

**Purpose:** Invisible spacer widget for layout.

**Functions:**
- `dock_spacer_slot_width(icon_ref_px)` - returns `icon_ref_px` (minimum 1)
- `paint_spacer_slot(...)` - no-op (all params void-casted)

**Why:** Allows adding empty space in the widget bar for alignment/grouping.

---

## Popup Widgets

These open as separate popup surfaces when their inline widget is clicked. Each popup follows the same pattern:
- `.hpp` - Constants and template paint function declaration (templated on app type for dock/taskbar compatibility)
- `.cpp` - Click handler implementation (non-template, uses `DockApp` directly)
- `.tpp` - Template paint function implementation (included from `.hpp`)

### `popup/calendar/calendar_popup.hpp` (40 lines) + `calendar_popup.cpp` (12 lines)

**Constants:** `kCalendarPopupW = 320`, `kCalendarPopupH = 370`

**Functions:**
- `dock_calendar_popup_paint(app, cr, config)` - renders month calendar view with highlighted current date
- `dock_calendar_popup_handle_click(app, lx, ly)` - always closes popup (no interactive elements yet)

### `popup/media_player/media_player_popup.hpp` (37 lines) + `media_player_popup.cpp` (147 lines)

**Constants:** `kMediaPlayerPopupW = 340`, `kMediaPlayerPopupH = 420`

**Layout constants:** outer margin 14, inner width 312, close button 32px, album art at 52% of column width, transport buttons with 1.24x size boost

**Functions:**
- `dock_media_player_popup_handle_click(app, lx, ly)`:
  1. Tests close button hit (circular hit test)
  2. Retrieves MPRIS snapshot
  3. Measures layout to compute button positions
  4. Handles play/pause, previous, next button hits
  5. Handles seekbar click (sets position fractionally)

**Renders:** Full album art, track title, artist, album, seekbar with progress, time display, volume slider, play/pause/prev/next buttons, shuffle/repeat toggles.

### `popup/volume_mixer/volume_mixer_popup.hpp` (34 lines) + `volume_mixer_popup.cpp` (210 lines)

**Constants:** `kVolumeMixerPopupW = 600`

**Functions:**
- `reset_ui()` - resets `MixerUiState`
- `dock_volume_mixer_popup_handle_click(app, lx, ly)`:
  1. Gets PipeWire service instance
  2. Tests header pill hits (Output/Input toggle)
  3. Tests sink/source device list hits (click to set default)
  4. Tests mixer stream list hits (per-app volume click)
  5. Falls through to close popup

**Renders:** Output/Input header pills, device list with volume sliders, per-stream mixer strips with icons, labels, volume sliders, and mute buttons.

### `popup/vpn/vpn_popup.hpp` (36 lines) + `vpn_popup.cpp` (121 lines)

**Constants:** `kVpnPopupW = 320`

**Struct:** `PopupVpnEntry { path, name, activePath, active, toggle() }` - calls NetworkManager D-Bus to connect/disconnect

**Global state:** `s_entries` - vector of VPN connection entries

**Functions:**
- `vpn_popup_sync_entries(nm_service)` - syncs popup entries from NetworkManager
- `vpn_popup_entry_count()` - returns entry count
- `vpn_popup_entry_name(idx)` - returns connection name
- `vpn_popup_entry_active(idx)` - returns active state
- `vpn_popup_toggle_entry(idx)` - toggles connection
- `vpn_popup_remove_entry(idx)` - removes connection via NetworkManager
- `dock_vpn_popup_handle_click(app, lx, ly)` - handles close, remove, connect/disconnect buttons

### `popup/weather/weather_popup.hpp` (39 lines) + `weather_popup.cpp` (12 lines)

**Constants:** `kWeatherPopupW = 360`, `kWeatherPopupH = 360`

**Functions:**
- `dock_weather_popup_paint(app, cr, config)` - renders detailed forecast: current conditions, hourly forecast, 7-day forecast
- `dock_weather_popup_handle_click(app, lx, ly)` - always closes popup

---

## Widget Summary

| Widget | Width fn | Paint fn | Popup | Data source |
|---|---|---|---|---|
| **Clock** | `dock_clock_slot_width` | `paint_clock_slot` | `calendar_popup` | `std::time` |
| **Weather** | `dock_weather_slot_width` | `paint_weather_slot` | `weather_popup` | `ControlCenterWeatherState` |
| **Media** | `dock_media_slot_width` | `paint_media_slot` | `media_player_popup` | MPRIS via `mpris_player.hpp` |
| **Workspaces** | `dock_workspaces_slot_width` | `paint_workspaces_slot` | -- | Wayland/Mango via `workspaces.h` |
| **Battery** | `dock_battery_slot_width` | `paint_battery_slot` | `dock_battery_popup_paint` | UPower via `sdbus-c++` |
| **Spacer** | `dock_spacer_slot_width` | `paint_spacer_slot` | -- | No data |
| **Volume Mixer** | (control center) | (control center) | `volume_mixer_popup` | PipeWire via `pipewire_service.hpp` |
| **VPN** | (control center) | (control center) | `vpn_popup` | NetworkManager via `network_manager_service.hpp` |
