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

**Helpers:**
- `effective_height_px(settings, globalScale)` - bar height with a guaranteed `8 × ui-scale` of breathing room per side: when `iconSize × scale` would fill the bar edge to edge, the bar grows instead (normalized into `settings.height` on every settings apply, so layer size, exclusive zone, paint, tooltips and popup clearance all agree).
- `strip_pad_px` / `section_gap_px` - inner padding at the bar's rounded ends and the gap between the left/center/right sections (shared by paint and the fill-mode bar sizing).
- `popup_bottom_clearance_px` - exact distance from the screen edge to the bar's top (edgeGap + floating inset + painted height) used to place popups above the bar.

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
  - **Toggle-close**: any click on the bar first dismisses an open popup and records its kind (`dismissedPopKind`); if that click landed on the widget owning the popup, it is treated as a toggle-close and not reopened (weather, clock/calendar, media player, control center, volume mixer, VPN, battery, Bluetooth, app drawer).
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

**Function:** `taskbar_paint_widget_bar(app, cr, x, y, w, h, leftW, centerW, rightW, ...)` - renders the three widget sections and emits their hit rects (single source of truth for pick).

**Layout (panel):** Left widgets anchored `strip_pad_px` from the bar's left end, right widgets the same from the right end, center widgets truly centered on the bar (and therefore on the screen). Fill mode sizes its pill to `center + 2 × (pad + max(left, right) + section_gap)` so the three-zone layout always fits. If the sections would collide (narrow content bar), paint falls back to a single centered strip with horizontal compression (`tb_strip_h_scale`), keeping at least the inner padding at both ends. Fill-mode sizing uses `taskbar_measure_sections()`, which shares paint's slot widths and gap rules (tray packing, `pinnedAppsTrayPill` / `runningAppsTrayPill`, widget keys) so the measured bar never disagrees with what paints.

**Press feedback:** the press overlay covers the full slot width (`slotW + 4`), and the press scale is centered on the slot - wide widgets (weather, media, clock) previously got an icon-sized overlay that only lit up half the item.

---

### `taskbar_position.cpp` / `taskbar_position.hpp`

**Purpose:** Taskbar surface positioning.

**Functions:**
- `taskbar_compute_popup_position(app, anchorX, popupW, popupH)` - popup anchor: horizontal clamp into the output, bottom clearance via `popup_bottom_clearance_px` (edge gap + floating inset + bar height), mirrored to the top margin when the bar sits at the top.
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
