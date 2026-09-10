# Taskbar

Located in `src/desktop_shell/taskbar/`. The taskbar is a layer-shell panel paralleling the dock architecture but with independent state, settings, and code paths.

---

## File-by-File Detail

### `taskbar_types.hpp` (52 lines)

**Purpose:** Shared type definitions for the taskbar module.

**Enum `TaskbarPopupKind`:**
```cpp
enum class TaskbarPopupKind : uint8_t {
  None, Tray, App, Calendar, Weather, VolumeMixer,
  AppDrawer, ControlCenter, Vpn, Battery, MediaPlayer
};
```

**Type aliases:**
- `TaskbarTrayItem = eh::tray::TrayItem`
- `TaskbarRunningGroup = eh::shell::shared::RunningGroup`
- `TaskbarRunningSnapshot = eh::shell::shared::RunningSnapshot`

**Struct `TaskbarWidgetHit`:** Widget hit result: `widgetId`, `x/y/w/h` (geometry), `chosenSerial`, `isPinned`, `slotKind`.

---

### `taskbar_settings.hpp`

**Purpose:** Taskbar-specific settings (mirrors `DockSettings`).

**Fields:** Same as DockSettings but independent config section: `[taskbar]`. Includes: `iconPx`, `autoHide`, `autoHideDelayMs`, `taskbarPosition` (top/bottom), `taskbarOffset`, `taskbarMargin`, `pinnedAppIds`, `widgetLayout`, `showTray`, `showWorkspaces`, `showTrash`, `popupMargin`, `displayMode`.

---

### `taskbar.hpp` (~200 lines)

**Purpose:** `TaskbarApp` struct - central state object for the taskbar.

**Key state (parallel to DockApp):**
- Wayland objects: display, compositor, shm, seat, layer shell
- `vector<TaskbarOutputLayer> outputs` - per-output state
- `TaskbarSettings settings` - taskbar configuration
- `TaskbarPopupKind popupKind` - current popup type
- `AppDrawerState appDrawerState` - app drawer state (**uses new code path**)
- `vector<string> pinnedApps` - taskbar-specific pinned apps
- `vector<TaskbarTrayItem> trayItems` - active tray icons
- `TaskbarRunningSnapshot running` - running window snapshot
- `AnimationManager animation` - animation state
- Pointer position, hover/pressed slot tracking
- Power confirm state: `powerConfirmOpen`, `powerConfirmIdx`
- `unique_ptr<GammaService> gammaService` - nightlight control

---

### `taskbar.cpp` (~1100 lines)

**Purpose:** Main taskbar implementation.

**Initialization sequence:**
1. Connect to Wayland display
2. Bind globals (compositor, shm, layer shell, seat, etc.)
3. Read taskbar settings from config
4. Create per-output layer surfaces
5. Set up config watcher
6. Enter event loop (parallels dock_bar.cpp)

**Key functions:**
- `taskbar_render()` - renders the bar via shared `paint_widget_bar()` function
- `taskbar_handle_pointer_motion()` - slot hover tracking, tooltip updates
- `taskbar_handle_pointer_button()` - slot activation:
  - **App slot**: launch or focus window
  - **Tray slot**: open tray menu
  - **AppDrawer slot**: open app drawer popup
  - **Power button**: lock/logout/restart/shutdown with confirmation
  - **Nightlight button**: toggle via gamma service
  - **Other widgets**: open respective popup
- `taskbar_handle_pointer_axis()` - scroll (workspaces or app list)
- `taskbar_handle_key()` - keyboard navigation in popups
- `taskbar_open_popup(kind)` - creates popup surface
- `taskbar_close_popup()` - destroys popup surface
- `taskbar_auto_hide_tick()` - auto-hide animation

**Popup handling:** Routes to specific popup paint functions:
- `app_drawer_paint()` via `AppDrawerState`
- `calendar_popup_paint()`, `weather_popup_paint()`, etc.

**Power action handling:**
```cpp
if (zone == PowerButton) {
  pwr = app_drawer_pick_power_index(state, px, py);
  if (pwr == 0) { lock_screen(); popup_close(); }
  else if (pwr >= 1 && pwr <= 3) {
    state.powerConfirmOpen = true;
    state.powerConfirmIdx = pwr;
  }
}
if (zone == NightlightButton) {
  next = !gammaService_->enabled();
  gammaService_->set_enabled(next);
  gammaService_->set_temperature(next ? 4000 : 6500);
  app_drawer_set_nightlight_active(next);
}
```

---

### `taskbar_paint.cpp` / `taskbar_paint.hpp`

**Purpose:** Taskbar bar rendering.

**Function:** `taskbar_paint(app, cr)` - renders the taskbar bar by calling `paint_widget_bar()` with `WidgetBarLayout::PanelEdgeAligned`.

**Layout:** PanelEdgeAligned means left widgets anchored to left edge, right widgets to right edge, center widgets between them.

---

### `taskbar_position.cpp` / `taskbar_position.hpp`

**Purpose:** Taskbar surface positioning.

**Functions:**
- `taskbar_reposition(app)` - sets layer-shell anchor (top or bottom), margin, exclusive zone, size
- `taskbar_popup_sync_position(app)` - adjusts popup position when taskbar moves

---

## Key Differences from Dock

| Aspect | Dock | Taskbar |
|---|---|---|
| Settings section | `[dock]` | `[taskbar]` |
| Default position | Bottom | Top |
| App drawer API | `eh_app_drawer_*` (legacy) | `app_drawer_*` via `AppDrawerState` (new) |
| Layout mode | DockCentered | PanelEdgeAligned |
| Widget bar paint | Via `dock_paint_widget_bar.cpp` | Via shared `paint_widget_bar()` |
| Pinned apps | Separate list per instance | Separate list per instance |
