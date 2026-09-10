# Desktop

Located in `src/desktop_shell/desktop/`. The desktop layer provides the background surface, file/folder icons, context menus, desktop widgets, and wallpaper integration. 40 source files.

---

## File-by-File Detail

### `desktop_layer.cpp` / `desktop_layer.hpp`

**Purpose:** Creates and manages the Wayland layer-shell background surface for the desktop.

**Functions:**
- `desktop_layer_create(app, output)` - creates a `zwlr_layer_surface_v1` with `ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND` for given output
- `desktop_layer_destroy(app, output)` - destroys the desktop surface
- `desktop_layer_damage(app)` - marks the desktop surface as needing redraw

**Architecture:** One layer surface per output, at the lowest Z-order (background layer). The wallpaper and desktop widgets render into this surface.

---

### `desktop_icons.cpp` / `desktop_icons.hpp` (1461 lines)

**Purpose:** The largest file in the desktop module. Renders file and folder icons, handles selection, drag-and-drop, and file operations.

**Namespaces:** `eh::shell::desktop`, `eh::shell::desktop::file_icon_cache`

**Constants (file_icon_cache):**
- `kMaxRasterThumbsPerDock = 96` - max cached file thumbnails
- Thumbnail generation: uses `cairo_image_surface_create` with bilinear downscaling

**Key functions:**
- `desktop_icon_rebuild()` - rescans desktop directory, mounted volumes, and trash
- `desktop_icon_paint(cr, app)` - renders all desktop icons: file icon, label, selection highlight, drag ghost
- `desktop_icon_pick(lx, ly)` - hit test: returns icon index under cursor
- `desktop_icon_activate(idx)` - opens file/folder with default handler via XDG MIME types
- `desktop_icon_context_menu(idx, lx, ly)` - shows context menu for icon
- `desktop_marquee_update(x1, y1, x2, y2)` - updates selection rectangle
- `desktop_marquee_finalize()` - finalizes multi-select via marquee
- `desktop_icon_start_drag(idx)` - initiates drag operation
- `desktop_icon_drop(x, y)` - handles drop (move, copy, or link)
- `file_icon_cache::get_or_create(path, size)` - returns cached icon or generates thumbnail
- `file_icon_cache::invalidate(path)` - clears cached icon for a path
- `file_icon_cache::clear_all()` - clears entire icon cache

**Icon rendering:**
- Files: MIME-type icon from theme, or generic file icon
- Folders: folder icon from theme, plus overlay if folder is a mount point
- Trash: special trash icon with full/empty state
- Mounted volumes: drive icon with eject overlay
- Thumbnails: for image files, generates scaled raster preview

**Selection:**
- Single click: select (highlight with accent border)
- Shift click: extend selection
- Ctrl click: toggle selection
- Marquee drag: multi-select rectangle
- Double click: open with default handler

**Drag and drop:**
- Drag ghost: semi-transparent icon follows cursor
- Drop zones: desktop (move/copy), folder (move into), trash (delete)
- Visual feedback: highlight color on valid drop targets

---

### `desktop_icon_menu.cpp` / `desktop_icon_menu.hpp`

**Purpose:** Context menu for desktop icons (files, folders, trash, mounted volumes).

**Struct `IconCtxMenuRow`:** `Kind` enum with entries: Open, DesktopAction, Separator, OpenLocation, RunElevated, PinStart, PinTaskbar, Separator2, Cut, Copy, CreateShortcut, Delete, Rename, Properties, ExtractArchive, CompressSubmenu, CompressZip, CompressTarGz, CompressTar, Compress7z, Mount, Unmount, EmptyTrash, FstabDismiss, FstabYes, FstabNo

**Functions:**
- `desktop_icon_menu_show(app, icon_idx, x, y)` - builds and shows context menu for icon
- `desktop_icon_menu_paint(cr)` - renders menu
- `desktop_icon_menu_pick_item(lx, ly)` - hit test
- `desktop_icon_menu_execute(item_kind, icon_idx)` - executes selected action

**Actions by icon type:**
- **Files:** Open, Open With submenu, Cut, Copy, Delete, Rename, Properties, Compress submenu
- **Folders:** Open, Cut, Copy, Delete, Rename, Properties, Open in Terminal
- **Trash:** Empty Trash, Open
- **Mounted drives:** Unmount, Eject, Open in File Manager
- **Archives:** Extract Here, Extract To submenu, Open with Archive Manager

---

### `desktop_menu.cpp` / `desktop_menu.hpp`

**Purpose:** Right-click (background) context menu on the desktop.

**Enum `DesktopMenuPanel`:** Main, View, SortBy, AddNew

**Functions:**
- `desktop_menu_show(app, x, y)` - shows main desktop context menu
- `desktop_menu_paint(cr, app)` - renders current menu panel
- `desktop_menu_pick_item(lx, ly)` - hit test
- `desktop_menu_execute(item)` - executes selected action

**Main menu items:**
- New Folder, New File, Arrange Icons, Paste, Display Settings, Change Wallpaper

**View submenu:**
- Icon size slider, Grid spacing slider, Sort By submenu, Show Hidden Files toggle

**Sort By submenu:**
- Name, Size, Type, Date Modified (ascending/descending)

**Add New submenu:**
- Widget placement: Clock, Calendar, Weather, Media Player, System Monitor

---

### `desktop_pointer.cpp` / `desktop_pointer.hpp`

**Purpose:** Pointer/mouse input handling for desktop surfaces.

**Functions:**
- `desktop_pointer_enter(app, surface, sx, sy)` - handle pointer enter event
- `desktop_pointer_motion(app, sx, sy)` - handle pointer motion: icon hover, marquee drag, drag ghost position
- `desktop_pointer_button(app, button, state)` - handle press/release: icon select, context menu, drag start, drop
- `desktop_pointer_axis(app, dx, dy)` - handle scroll (currently unused on desktop)
- `desktop_pointer_leave(app)` - handle pointer leave

**State:** Tracks hovered icon, selected icons, drag state, marquee state, and context menu state.

---

### `desktop_app.cpp` / `desktop_app.hpp`

**Purpose:** Desktop application management. Handles launching applications from desktop files.

**Functions:**
- `desktop_app_launch(app, desktop_path)` - parses .desktop file, resolves Exec with field codes, runs command
- `desktop_app_launch_exec(app, exec_cmd)` - executes command via `/bin/sh -c`
- `desktop_app_launch_terminal(app, exec_cmd)` - launches in terminal (wraps command with terminal emulator)

**Field code substitution:** Replaces `%f`, `%F`, `%u`, `%U`, `%i`, `%c`, `%k` in Exec line with actual values.

---

### `desktop_entries.cpp` / `desktop_entries.hpp` (489 lines)

**Purpose:** Desktop entry resolution and matching. Used to find .desktop files from running app IDs.

**Functions:**
- `desktop_id_candidate_bases(app_id)` - generates candidate stem names from app ID (strips .desktop, .exe, .bin, handles lowercase variants)
- `desktop_paths_for_app_id(app_id)` - searches XDG data directories for matching .desktop files
- `resolve_desktop_path(app_id)` - returns first matching .desktop path
- `parse_desktop_file_extended(path)` - enhanced .desktop parsing with field code extraction

**Why:** Maps window app_ids (from `foreign_toplevel_handle`) to .desktop files for icon lookup and pinning.

### `desktop_entry_types.hpp`

**Purpose:** Shared data types for desktop entries and application metadata.

**Types:**
- `DesktopEntry` struct: `name`, `genericName`, `comment`, `icon`, `exec`, `path`, `categories`, `keywords`, `mimeType`, `noDisplay`, `terminal`, `startupNotify`, `startupWMClass`, `dbusActiveService`
- `DesktopEntryAction` struct: `name`, `exec`, `icon` (for additional actions like "New Window")
- `EntryMatchResult` struct: `entry`, `score`, `matched_field`

---

### `desktop_archive.cpp` / `desktop_archive.hpp`

**Purpose:** Archive file mounting and extraction.

**Functions:**
- `desktop_archive_mount(path)` - mounts archive using `archivemount` if available, or extracts to temp directory
- `desktop_archive_unmount(path)` - unmounts archive
- `desktop_archive_extract(path, dest)` - extracts archive to destination using `unzip`, `tar`, etc.
- `desktop_archive_can_handle(path)` - checks if file extension is a supported archive format
- `desktop_archive_get_mime_type(path)` - returns MIME type for archive (zip, tar.gz, 7z, rar, etc.)

---

### `desktop_preferences.cpp` / `desktop_preferences.hpp`

**Purpose:** Desktop icon preferences dialog.

**Functions:**
- `desktop_preferences_show(app)` - opens preferences popup
- `desktop_preferences_hide(app)` - closes preferences popup
- `desktop_preferences_paint(cr, app)` - renders preferences dialog
- `desktop_preferences_handle_click(app, lx, ly)` - handles click on preference controls

**Settings:** Icon size, grid spacing, label font size, label color, sort order, show hidden files.

---

### `desktop_xdg_ops.cpp` / `desktop_xdg_ops.hpp`

**Purpose:** XDG desktop operations (Open With, file associations).

**Functions:**
- `xdg_open_file(path)` - opens file with default handler via `xdg-open`
- `xdg_open_with(app, path, desktop_id)` - opens file with specified application
- `xdg_get_default_app_for_mime(mime_type)` - queries default application via .desktop files
- `xdg_get_apps_for_mime(mime_type)` - lists all applications for MIME type

---

### `desktop_marquee.cpp` / `desktop_marquee.hpp`

**Purpose:** Selection rectangle (marquee) for multi-select.

**Functions:**
- `desktop_marquee_start(x, y)` - begins marquee at anchor point
- `desktop_marquee_update(x, y)` - updates selection rectangle to current point
- `desktop_marquee_finalize(icons)` - computes which icons are within the selection rectangle
- `desktop_marquee_cancel()` - cancels marquee without selection
- `desktop_marquee_paint(cr)` - renders the dashed selection rectangle with accent-colored border and semi-transparent fill

---

### `cava_reader.cpp` / `cava_reader.hpp`

**Purpose:** Audio visualization data reader. Reads FFT output from the CAVA audio visualizer (via named pipe or shared memory).

**Functions:**
- `cava_reader_init()` - connects to CAVA's output pipe
- `cava_reader_poll()` - reads latest FFT data
- `cava_reader_get_bars()` - returns current bar amplitudes as float array
- `cava_reader_shutdown()` - disconnects from CAVA

**Why:** Provides audio visualization data for the desktop system monitor widget.

---

### `session.cpp` / `session.hpp`

**Purpose:** Desktop session types and utilities.

**Enum:** `DesktopMenuPanel { Main, View, SortBy, AddNew }`

**Struct `IconCtxMenuRow`:** Context menu row type definitions for file/folder operations.

---

### `background_seat.cpp` / `background_seat.hpp`

**Purpose:** Secondary `wl_seat` management for background operations.

**Functions:**
- `background_seat_init(app)` - registers interest in the background seat
- `background_seat_pointer_motion(app, sx, sy)` - routes background seat pointer events
- `background_seat_pointer_button(app, button, state)` - routes background seat button events

**Why:** Some compositors provide a separate seat for desktop interaction. This handles routing events from that seat.

---

## Desktop Widget System

### `desktop_widget_host.cpp` / `desktop_widget_host.hpp` (242 lines)

**Purpose:** Hosts and manages desktop widget instances.

**Struct `DesktopWidgetHost::WidgetInstance`:** `DesktopWidgetConfig config`, `unique_ptr<DesktopWidget> widget`

**Functions:**
- `make_widget(config, mpris)` - factory: creates widget by type (Clock, Weather, SystemMonitor, WeatherFancy, Calendar, MediaPlayer)
- `DesktopWidgetHost::load_from_config(config)` - reads `[desktop]` section, creates widget instances for each configured widget
- `DesktopWidgetHost::paint_all(cr)` - renders all widgets at their configured positions
- `DesktopWidgetHost::handle_click(app, lx, ly)` - routes clicks to widgets
- `DesktopWidgetHost::set_mpris(mpris)` - provides MPRIS reference for media player widget

### `desktop_widget.hpp` (base class)

**Purpose:** Abstract base class for desktop widgets.

**Virtual methods:** `paint(cr)`, `handle_click(app, lx, ly)`, `get_config()`, `set_config(config)`, `get_position()` / `set_position(x, y)`, `get_size()`

### Individual Widget Implementations

| File(s) | Widget type | Description |
|---|---|---|
| `desktop_widget_clock.cpp` / `desktop_widget_clock.hpp` | Clock | Digital clock display with configurable format, font size, date/seconds visibility |
| `desktop_widget_weather.cpp` / `desktop_widget_weather.hpp` | Weather | Current weather icon + temperature + conditions, updates from `ControlCenterWeatherState` |
| `desktop_widget_weather_fancy.cpp` / `desktop_widget_weather_fancy.hpp` | Weather Fancy | Enhanced weather display with animated icons, hourly forecast, gradient background |
| `desktop_widget_calendar.cpp` / `desktop_widget_calendar.hpp` | Calendar | Month calendar view with date highlighting, navigation controls |
| `desktop_widget_media_player.cpp` / `desktop_widget_media_player.hpp` | Media Player | MPRIS media player with album art, track info, transport controls, seekbar |
| `desktop_widget_system_monitor.cpp` / `desktop_widget_system_monitor.hpp` | System Monitor | CPU, memory, disk, network usage graphs (line charts). Optional audio visualization bars (via CAVA). |

### `desktop_widget_layer.cpp` / `desktop_widget_layer.hpp`

**Purpose:** Manages the Wayland overlay surface for desktop widgets. Rendered on top of the wallpaper but below icons.

### `desktop_widgets_preferences.cpp` / `desktop_widgets_preferences.hpp`

**Purpose:** Per-widget configuration dialog. Allows per-widget settings: position, size, visual options.

