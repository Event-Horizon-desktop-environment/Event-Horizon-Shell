# Settings Application

Located in `src/ux/settings/`. The settings UI is a Cairo-rendered standalone application that can run embedded within the shell or as a separate XDG shell window.

---

## Architecture

**Two modes:**
- **Embedded** (`settings_embed_lifecycle.cpp` / `settings_embed.hpp`): popup surface within the shell process, shares Wayland connection and seat
- **Standalone** (`settings_standalone_entry_point.cpp`): separate XDG shell window with its own Wayland connection

---

## File-by-File Detail

### Core Application

#### `settings_app.cpp` / `settings_app_types.hpp` (1452 lines)

**Purpose:** Main settings window implementation. Handles layout, navigation, input, and rendering for the entire settings UI.

**State (`SettingsApp` struct):**
- `TabKind` enum: Appearance, Desktop, DesktopWidgets, Dock, Taskbar, Launcher, Workspaces, Network, Sound, Notifications, Power, Keyboard, Nightlight, Monitors, Wallpaper, Icons, Themes, Time, Bing, Hyprland, Mango, UiLayout, DefaultApps, About
- `tabOffset`, `tabTargetOffset` - scrollable tab list position
- `SettingsContent` per tab - tab-specific state widgets
- Wayland objects: `wl_display*`, `wl_seat*`, `wl_surface*`, `xdg_surface*` / `xdg_toplevel*`
- Rendering: `ShmBuffer` triple buffering, `CairoCpuBuffer`
- Input: pointer coords, pressed state, keyboard focus, text buffer
- Animation: `AnimationManager<double>` for tab slide and widget transitions

**Key functions:**
- `SettingsApp::init()` - creates XDG surface, binds protocols, shows window
- `SettingsApp::run()` - main event loop
- `SettingsApp::render()` - renders current tab content
- `SettingsApp::handle_pointer()` - button clicks, slider drag, toggle, dropdown
- `SettingsApp::handle_keyboard()` - text input, tab navigation, enter to activate

#### `settings_serialize.cpp` / `settings_serialize.hpp` (805 lines)

**Purpose:** Serializes/deserializes settings to/from TOML config files and `settings.ini`.

**Functions:**
- `settings_load(settings_app)` - loads all settings from config and settings file
- `settings_save(settings_app)` - saves all settings back to config
- `apply_matugen()` - triggers Material You palette generation and external theme template writes
- `apply_wallpaper()` - applies selected wallpaper across all outputs
- `settings_apply()` - writes TOML, triggers wallpaper/matugen/kbd apply as needed
- `get_state_dir()` - resolves state directory for `settings.ini`
- `appearance_setting_toml_key()` - maps appearance setting paths to TOML keys
- `widget_setting_simple()` - reads a single widget setting by instance ID and key
- `join_list()` / `join_set()` - serializes vectors/sets to semicolon-delimited strings
- Various `read_*()` / `write_*()` helpers for each config section

### Common Settings Infrastructure

#### `common/settings_common.hpp`

**Purpose:** Common types and utilities shared across settings tabs.

- `Setting` struct - a single settings entry (key, label, type, value, min, max, options)
- `SettingSection` - a group of related settings with a section header
- `SettingChangeCallback` - function type for value change notifications
- `settings_trace_event()` / `settings_trace_begin()` / `settings_trace_end()` - tracing helpers

#### `common/settings_embed.hpp` / `common/settings_embed_lifecycle.cpp` / `common/settings_embed_lifecycle.hpp`

**Purpose:** Embedded settings mode. Opens settings as a popup surface within the shell.

**Key functions:**
- `settings_embed_create(app)` - creates the embedded settings popup surface
- `settings_embed_destroy()` - destroys the popup
- `settings_embed_render()` - renders settings into the popup buffer
- `settings_embed_setup_input()` - routes seat events to embedded settings
- `settings_embed_lifecycle_handler()` - manages creation/destruction tied to shell state

#### `common/settings_logo.cpp` / `common/settings_logo.hpp`

**Purpose:** Renders the Event Horizon logo in the settings window header/About tab.

**Function `paint_logo(cr, x, y, size)`:**
- Draws the EH logo as a Cairo path (using Material glyph shapes)
- Used in the About tab and settings window header

#### `common/settings_trace.hpp`

**Purpose:** Performance tracing for settings operations. Macro-based instrumentation.

- `EH_SETTINGS_TRACE(name)` - scoped trace event
- `SETTINGS_TRACE_INIT` / `SETTINGS_TRACE_FINISH` - lifecycle tracing

#### `common/settings_standalone_entry_point.cpp`

**Purpose:** Entry point for standalone settings mode. Creates its own Wayland display connection and runs the settings app as an independent XDG window. Used by `eh-settings` CLI.

### Data Layer

#### `data/settings_monitors.cpp` / `data/settings_monitors.hpp`

**Purpose:** Monitor configuration data. Reads and validates monitor settings from config: position, scale, transform, mode (resolution + refresh), enabled state. Used by the Monitors settings tab.

#### `data/settings_mango_data.cpp` / `data/settings_mango_data.hpp`

**Purpose:** Mango compositor-specific data. Stores Mango display configuration (output layout, tag assignments, scratchpad settings).

#### `data/settings_hyprland_data.cpp` / `data/settings_hyprland_data.hpp`

**Purpose:** Hyprland-specific settings data: window rules, binds, monitor config, animations, blur settings. Read/written from/to `~/.config/hypr/hyprland.conf`.

#### `data/settings_default_apps.cpp` / `data/settings_default_apps.hpp`

**Purpose:** Default application associations. Maps MIME types to desktop entry IDs. Provides lookup: `default_app_for_mime(mime)` and `set_default_app(mime, desktop_id)`.

#### `data/settings_desktop_widgets_data.hpp`

**Purpose:** Desktop widget configuration data. Per-widget-instance settings: position (row, column), size, visibility, widget-specific settings (e.g., weather city, clock format).

### Utility Layer

Located in `settings/utils/`:

- `settings_scroll.cpp` / `settings_scroll.hpp` - Scrollable list region. `SettingsScroll` struct: `content_height`, `viewport_height`, `scroll_offset`, `scroll_target`. Functions: `scroll_begin()`, `scroll_update()`, `scroll_render_track()`, `scroll_pick()`.
- `settings_slider_appliers.cpp` / `settings_slider_appliers.hpp` - Maps slider values to settings keys. `apply_slider_int(setting, value)`, `apply_slider_float(setting, value)`, `read_slider_value(setting)`.
- `settings_widget_drag.cpp` / `settings_widget_drag.hpp` - Drag-and-drop widget reordering in the dock/taskbar layout editor. Tracks drag source, target position, visual indicator.
- `settings_wallpaper_thumbs.cpp` / `settings_wallpaper_thumbs.hpp` - Wallpaper thumbnail grid rendering. `render_wallpaper_grid(cr, thumbnails, selected)`, `pick_wallpaper_thumb(x, y, grid_layout)`.
- `settings_sound_cache.cpp` / `settings_sound_cache.hpp` - Caches sound theme files for the Sound settings tab. Lists available sounds, plays previews.
- `settings_event_handlers.cpp` / `settings_event_handlers.hpp` - Common event handler registration. `register_tab_event_handlers(app, tab)` - sets up pointer/keyboard dispatch per tab.
- `settings_gpu.cpp` / `settings_gpu.hpp` - GPU information queries (vendor, renderer, VRAM). Used for rendering backend selection display.
- `material_glyphs.cpp` / `settings/utils/material_glyphs.hpp` - Material Symbols glyph helper for settings UI. `draw_settings_glyph(cr, name, x, y, size, color)`.
- `settings_monitors_tab.cpp` / `settings_monitors_tab.hpp` - Monitor configuration UI helpers: output rectangle drag, arrangement preview, mode list.
- `settings_monitors_drm_probe.cpp` / `settings_monitors_drm_probe.hpp` - Direct DRM probe for monitor EDID data, used when Wayland doesn't provide enough monitor info.
- `widget_picker.cpp` / `widget_picker.hpp` - Widget picker component. Shows available widgets in a grid, allows adding to bar layout. `WidgetPicker` struct with `available_widgets`, `selected`, `on_add_callback`.

### Tab Implementations

Each tab is a `.cpp`/`.hpp` pair in its own directory implementing the `SettingsContent` interface:

| File | Content Rendered |
|---|---|
| `settings_tab_appearance.cpp` / `settings_tab_appearance.hpp` | Theme mode (light/dark), accent color picker, font selection, background effects toggles, matugen controls |
| `settings_tab_desktop.cpp` / `settings_tab_desktop.hpp` | Desktop icon grid settings, icon size, label font, show/hide home/trash/mounts |
| `settings_tab_desktop_widgets.cpp` / `settings_tab_desktop_widgets.hpp` | Per-widget enable/disable, position, and settings (weather location, clock format, calendar first day) |
| `settings_tab_dock_bar.cpp` / `settings_tab_dock_bar.hpp` | Dock position, auto-hide, icon size, widget bar layout editor, pinned apps |
| `settings_tab_taskbar.cpp` / `settings_tab_taskbar.hpp` | Taskbar position, auto-hide, icon size, widget bar layout editor |
| `settings_tab_launcher.cpp` / `settings_tab_launcher.hpp` | App drawer grid/list mode, search behavior, pinned apps management |
| `settings_tab_workspaces.cpp` / `settings_tab_workspaces.hpp` | Workspace display mode (names/numbers/hidden), workspace strip location |
| `settings_tab_network.cpp` / `settings_tab_network.hpp` | Wi-Fi scan/connect, Ethernet status, VPN management |
| `settings_tab_wifi.cpp` / `settings_tab_wired.cpp` | Sub-tabs within Network tab: Wi-Fi list (signal bars, SSID, security icons), wired connection status |
| `settings_tab_sound.cpp` / `settings_tab_sound.hpp` | Output device selector, volume slider, input device selector, sound theme picker, alert sound preview |
| `settings_tab_notifications.cpp` / `settings_tab_notifications.hpp` | Do-not-disturb toggle, per-app notification overrides, notification history |
| `settings_tab_power.cpp` / `settings_tab_power.hpp` | Power button behavior, idle/suspend timeout, lid close action (laptop) |
| `settings_tab_keyboard.cpp` / `settings_tab_keyboard.hpp` | Keyboard layout selector, switch shortcut, caps lock behavior, compose key, repeat rate/delay |
| `settings_tab_nightlight.cpp` / `settings_tab_nightlight.hpp` | Nightlight enable/disable, schedule (sunset-to-sunrise/manual times), color temperature slider |
| `settings_tab_monitors.cpp` / `settings_tab_monitors.hpp` | Output arrangement (drag to reorder), resolution/refresh rate per output, scale, transform |
| `settings_tab_wallpaper.cpp` / `settings_tab_wallpaper.hpp` | Wallpaper image picker grid (with thumbnails), scaling mode selector |
| `settings_tab_icons.cpp` / `settings_tab_icons.hpp` | Icon theme selector, icon size preferences |
| `settings_tab_themes.cpp` / `settings_tab_themes.hpp` | Theme import/export, installed themes list, active theme display |
| `settings_tab_time.cpp` / `settings_tab_time.hpp` | Time format (12h/24h), date format, timezone selector, NTP toggle |
| `settings_tab_bing.cpp` / `settings_tab_bing.hpp` | Bing daily wallpaper enable, download directory, resolution selector, market selector, auto-update interval |
| `settings_tab_hyprland.cpp` / `settings_tab_hyprland.hpp` | Hyprland-specific: window rules editor, animation settings, blur settings, input config |
| `settings_tab_mango.cpp` / `settings_tab_mango.hpp` | Mango-specific: display tags, scratchpad, per-output layout |
| `settings_tab_layout.cpp` / `settings_tab_layout.hpp` | Bar layout configuration through interactive widget drag-drop |
| `settings_tab_default_apps.cpp` / `settings_tab_default_apps.hpp` | MIME type → application association table |

### Network Sub-Dialogs

- `vpn_add_dialog.cpp` / `vpn_add_dialog.hpp` - Modal dialog for adding/managing VPN connections. Fields: connection name, gateway, username, password, protocol.
- `wifi_password_prompt.cpp` / `wifi_password_prompt.hpp` - Password prompt dialog for secured Wi-Fi networks. Shows SSID, password field with show/hide, connect button.
- `keyring_password_prompt.cpp` / `keyring_password_prompt.hpp` - Keyring unlock prompt dialog. Shows keyring name, password field, unlock button.

## UI Framework (separate from settings tabs)

The settings app uses the component library in `src/ux/`:

- `button/ui_button.cpp` / `button.hpp` - clickable button with label, icon, hover/press states
- `slider/ui_slider.cpp` / `slider.hpp` - horizontal value slider with configurable min/max/step
- `toggle/ui_toggle.cpp` / `toggle.hpp` - animated on/off switch
- `dropdown/dropdown.cpp` / `dropdown.hpp` - dropdown selection menu with scroll
- `ui/theme.cpp` / `ui/theme.hpp` - `UITheme` struct with material colors for the settings UI: `surface`, `background`, `primary`, `secondary`, `text`, `textSecondary`, `border`, `shadow`, `inputBg`, `inputBorder`, `danger`, `success`
