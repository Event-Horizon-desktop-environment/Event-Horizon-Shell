# Dock

Located in `src/desktop_shell/dock/`. The dock is a layer-shell panel that provides application launching, window management, system tray, and widget hosting. 23 source files.

---

## File-by-File Detail

### `dock_app.h` (524 lines)

**Purpose:** Central state object for the entire dock. `DockApp` struct is the largest single data structure in the codebase.

**Wayland objects:**
- `wl_display* display` - Wayland display connection
- `wl_compositor* compositor` - compositor global
- `wl_subcompositor* subcompositor` - sub-surface support
- `wl_shm* shm` - shared memory (for CPU rendering buffers)
- `wl_seat* seat` - input seat
- `zwlr_layer_shell_v1* layerShell` - layer shell protocol (for panel surfaces)
- `zwp_virtual_keyboard_manager_v1* vkbdManager` - virtual keyboard
- `zwlr_data_control_manager_v1* dataControl` - clipboard
- `zwp_idle_inhibit_manager_v1* idleInhibit` - idle inhibition
- `ext_idle_notifier_v1* idleNotifier` - idle notification
- `xdg_activation_v1* activation` - activation tokens
- `ext_session_lock_manager_v1* sessionLock` - session lock
- `zwlr_foreign_toplevel_manager_v1* toplevelManager` - window tracking
- `zwlr_gamma_control_manager_v1* gammaControl` - nightlight
- `zwlr_screencopy_manager_v1* screencopy` - screenshot
- `wp_viewporter* viewporter` - viewport scaling
- `wp_fractional_scale_manager_v1* fractionalScale` - fractional scaling
- `wp_tearing_control_manager_v1* tearingControl` - VRR tearing
- `zwp_linux_dmabuf_v1* linuxDmabuf` - Vulkan/drm buffer import
- `ext_background_effect_manager_v1* bgEffect` - background blur

**Output management:**
- `vector<DockOutputSlot> outputs` - per-output state
- Each `DockOutputSlot`: `wl_output*`, `zxdg_output_v1*`, `output_name`, `logical_x/y/w/h`, `ready`, `current_mode_refresh_mHz`, layer surfaces + buffers

**Rendering:**
- `ShellRendererBackend rendererBackend` - `Cpu` or `Vulkan`
- `VulkanDisplayContext* vkContext` - Vulkan context (if GPU path)
- Vulkan per-output state: `vkLayerSurface`, `vkBuffer`, `vkRaster`, `vkScheduler`, etc.
- Cairo per-output state: `cairo_surface_t*`, `cairo_t*`, `vector<ShmBuffer>` (triple buffering)

**Settings:**
- `DockSettings settings` - dock configuration
- `vector<string> startMenuPinnedApps` - apps pinned to start menu (separate from dock pins)

**Popup state:**
- `PopupKind popupKind` - current popup type (None, AppMenu, Tray, Calendar, Weather, etc.)
- `int popupW`, `popupH` - popup dimensions
- `wl_surface* popupSurface` - popup layer surface
- `zwlr_layer_surface_v1* popupLayerSurface` - popup layer surface protocol object
- `bool popupOpen` - whether popup is currently open
- `int popupOutputIdx` - which output the popup is on
- Pointer tracking for popup: `popupPointerX/Y`

**App drawer state (embedded):**
- `string appMenuQuery` - current search text
- `vector<SpotlightHit> appMenuHits` - search results
- `int appMenuSel` - selected result index
- `double appMenuScrollPx` - scroll position
- `bool appMenuSearchFocused` - keyboard focus state
- `int appMenuHoverRow` - hovered list row
- `int appMenuPowerHoverIdx` - hovered power button (-1 to 4)
- `bool appMenuSmenuMode` - full-width start menu mode
- `bool appMenuPowerConfirmOpen` - power confirm modal visible
- `int appMenuPowerConfirmIdx` - which power action to confirm
- `vector<string> appMenuCategories` - XDG categories
- `vector<double> appMenuCategoryWidths` - measured label widths
- `int appMenuSelectedCategory` / `appMenuCategoryHoverIdx` - category state
- Row context menu state: `appMenuRowCtxOpen`, `appMenuRowCtxAnchorRow`, etc.
- Pin context menu state: `appMenuPinCtxOpen`, `appMenuPinCtxAnchorIdx`, etc.
- Pin drag state: `appMenuDrawerPinHoverIdx`, `appMenuDrawerPinDragIdx`, etc.

**Window tracking:**
- `ForeignToplevelList foreignToplevels` - all open windows
- Callbacks: `foreignToplevelOpenFn`, `foreignToplevelCloseFn`, `foreignToplevelStateFn`

**Tray:**
- `vector<TrayItem> trayItems` - active tray icons
- `unique_ptr<eh::tray::TrayManager> trayManager` - tray protocol manager

**Audio:**
- `unique_ptr<...> pipewireService_` - PipeWire audio service (optional)

**Gamma (nightlight):**
- `unique_ptr<GammaService> gammaService_` - gamma/color temperature control

**OSD:**
- `unique_ptr<OsdHost> osdHost` - on-screen display host

**Animation:**
- `AnimationManager animationManager` - interpolated animations
- `double hoverLiftPx` - icon lift on hover
- `int hoveredSlot` - currently hovered slot index
- Animation values: slide progress, launch bounce, etc.

**Tooltip:**
- Tooltip state: surface, text, position, timer

**Misc:**
- `unique_ptr<eh::mpris::DockMpris> mpris` - MPRIS media player tracker
- `int lockCounter` - compositor lock depth
- `bool trashFull` - whether trash has contents
- Pointer: `double pointerX/Y`, `double pointerSx/Sy` (surface-local coords)
- Keyboard: `xkb_state*`, `xkb_context*`, `xkb_keymap*`, mods, repeat rate/delay
- `bool running` - loop control flag
- `CompositorKind compositorKind` - detected compositor type

---

### `dock_bar.h` (20 lines)

**Purpose:** Public API for the dock module.

**Functions:**
- `int run_dock()` - main entry point, creates DockApp and runs event loop
- `dock_sync_settings(DockApp&)` - re-reads config and applies changes
- `dock_sync_settings_from_drag_preview(DockApp&)` - applies settings from drag preview (for inline settings in dock)
- `dock_schedule_frame(DockApp&)` - requests next frame render
- `dock_cleanup(DockApp&, bool disconnect_display)` - full cleanup

---

### [`/dock_bar.cpp`] (2407 lines)

**Purpose:** Main dock implementation - the largest single file in the project.

**Boot sequence:**
1. Allocate and zero-initialize `DockApp`
2. Connect to Wayland display
3. Bind all Wayland globals (compositor, shm, layer shell, seat, etc.)
4. Detect compositor kind
5. Initialize subsystems: gamma service, OSD host, pipewire, MPRIS, virtual keyboard, background blur, idle inhibitor
6. Read dock settings from config
7. Create per-output layer surfaces
8. Set up config file watcher
9. Enter event loop

**Event loop:**
```
while (app.running) {
  wl_display_dispatch(app.display);
  handle_config_reload();
  handle_pending_actions();
  dock_tooltip_tick(app);
  dock_launch_feedback_tick(app);
  osd_update(app);
  workspace_strip_poll(app);
  battery_widget_poll();
  animation_manager.advance(frame_time);
  dock_render(app, frame_time);
  wl_display_flush(app.display);
}
```

**Auto-hide logic:**
- Timer-based: starts when pointer leaves dock area
- Configurable delay (default 500ms)
- Inhibited when popup is open or when pointer is near dock edge
- Slide animation: slides out of screen by dock height + margin, slides in on pointer proximity

**Key functions:**
- `dock_init()` - full initialization sequence
- `dock_cleanup()` - destroys all Wayland objects, cleans up Vulkan, frees state
- `dock_render()` - chooses CPU or GPU render path, calls `dock_paint_widget_bar()`, handles popup rendering
- `dock_rebuild_widgets()` - re-reads widget layout from config, rebuilds widget list
- `dock_sync_settings()` - re-reads dock settings, updates layout
- `dock_update_auto_hide()` - computes auto-hide state and animation
- `dock_on_config_change()` - callback for config file changes
- `dock_popup_open(kind)` - opens a popup of specified kind
- `dock_popup_close(app)` - closes current popup
- `dock_popup_draw_surface(app)` - renders popup surface

**Output management:**
- Creates layer surface per output
- Syncs dock bar across all outputs (duplicated on each monitor)
- Popups appear on the output containing the cursor

---

### `dock_input.cpp` / `dock_input.hpp` (1940 lines)

**Purpose:** All pointer, keyboard, and touch input handling for the dock and its popups.

**Pointer handling:**

`dock_pointer_enter()`: Updates cursor position, starts hover tracking.

`dock_pointer_motion()`:
1. Updates `app.pointerX/Y` and `app.pointerSx/Sy`
2. `dock_pick(app, sx, sy)` - hit tests current slot
3. If hovering a new slot: cancel previous hover, start new hover animation (lift)
4. Update tooltip state
5. If popup is open: route motion to `dock_popup_handle_pointer_motion()`
6. Trigger repaint

`dock_pointer_button()` (press):
1. If power confirm modal is open: handle modal click (confirm/cancel close)
2. If context menu is open: handle context menu click or dismiss
3. Route based on `popupKind`:
   - **AppMenu**: `eh_app_drawer_hit_zone()` → launch app / power action / nightlight / search / scroll
   - **Tray**: route to tray menu handler
   - **Calendar / Weather / etc.**: route to popup-specific handler
4. If no popup: handle slot click:
   - **App slot**: launch app or focus window (if already running)
   - **Tray slot**: open tray menu popup
   - **Launcher slot**: open app drawer popup
   - **Spotlight slot**: open spotlight popup
   - **Settings slot**: launch settings
   - **Workspace slot**: switch workspace
   - **Trash slot**: open trash folder
   - **Battery / Clock / Weather / Media slots**: open respective popup
   - **Control center slot**: open control center

`dock_pointer_axis()`: If app drawer popup is open, scroll the app list. Otherwise, scroll workspace strip.

**Keyboard handling:**

`dock_keyboard_key()`:
- Tab / Shift+Tab: navigate between searchable fields
- Enter: activate selected item
- Escape: close popup / close context menu / cancel search
- Up/Down: navigate app list rows
- Modifiers: track for keyboard shortcuts

`dock_keyboard_modifiers()`: Updates modifier state for key combinations.

**Touch handling:**

`dock_touch_down()` / `dock_touch_up()` / `dock_touch_motion()`: Mirrors pointer behavior for touch input.

**Power button handling:**
```
if (zone == PowerButton) {
  pwr = eh_app_drawer_pick_power_index(app, px, py);
  if (pwr == 0) { lock_screen(); popup_close(); }
  else if (pwr >= 1 && pwr <= 3) {
    app.appMenuPowerConfirmOpen = true;
    app.appMenuPowerConfirmIdx = pwr;
    popup_draw_surface(app);
  }
}
```

**Nightlight handler:**
```
if (zone == NightlightButton) {
  next = !gammaService_->enabled();
  gammaService_->set_enabled(next);
  if (next) gammaService_->set_temperature(4000);
  set_nightlight_active(next);
  popup_draw_surface(app);
}
```

---

### `dock_paint_widget_bar.cpp` / `dock_paint_widget_bar.hpp` (1032 lines)

**Purpose:** Renders the entire dock bar - background, pill shape, slots, animations.

**Functions:**
- `dock_paint_widget_bar(app, cr, ...)` - **main paint function**: clears buffer, draws background pill, computes slot positions, paints each slot type

**Rendering steps:**
1. **Background**: Full-width rounded rectangle with surface fill color + accent border
2. **Pill shape**: If configured, narrower pill-shaped background containing only center section
3. **Per-slot rendering**: For each slot (app, tray, widget):
   - App slot: icon (from pinned or running app), running indicator dot, hover lift animation (Y offset + scaled up)
   - Tray slot: tray icon from StatusNotifierItem
   - Widget slots: delegated to widget-specific paint functions (clock, weather, media, battery, workspaces, spacer)
   - Launcher slot: 3x3 dot grid icon
   - Settings slot: OS logo
   - Spotlight slot: search icon
   - Trash slot: trash icon (full/empty state)
4. **Separators**: Between widget groups
5. **Launch bounce**: Spring animation on newly launched apps
6. **Hover lift**: Slot scales up and lifts on hover

---

### `dock_pick.cpp` / `dock_pick.hpp`

**Purpose:** Hit testing for dock bar slots.

**Functions:**
- `dock_pick(app, sx, sy)` - main hit test. Returns `DockPickResult` with `slot_index`, `slot_kind` (App, Tray, Settings, etc.), and hit position
- `dock_pick_workspace_index(app, result, idx, px)` - hit test within workspace strip
- `dock_strip_slot_xw(app, result, idx)` - returns slot X position and width

**Pick logic:**
1. If pointer is outside bar bounds: return miss
2. Iterate through all slots (left section, center section, right section)
3. For each slot, check if pointer is within slot bounds
4. Special handling for pinned_apps expansion (each pinned app is a sub-slot)
5. Special handling for running_apps expansion and tray items
6. Return slot index + kind

---

### `dock_pinned.cpp` / `dock_pinned.h`

**Purpose:** Pinned app management for both dock bar and start menu.

**Functions:**
- `dock_pinned_apps_source_for_layout(app)` - returns the list of app IDs to show in the bar (pinned + running windows)
- `dock_pin_drag_rebuild_paint_order(app)` - rebuilds paint order after user drags to reorder
- `dock_pinned_serialize(app)` - writes pinned apps to config
- `dock_pinned_add(app, desktop_path)` - pins an app
- `dock_pinned_remove(app, idx)` - un-pins an app
- `dock_pinned_move(app, from, to)` - reorder pinned app
- `dock_pinned_is_pinned(app, app_id)` - checks if app is pinned

**Pin sources:**
- Dock bar pinned apps: from `settings.pinnedAppIds`
- Start menu pinned apps: from `startMenuPinnedApps` (separate list)

---

### `dock_position.cpp` / `dock_position.hpp`

**Purpose:** Dock surface positioning on screen.

**Functions:**
- `dock_reposition(app)` - sets layer-shell anchor (bottom/left/right), margin, exclusive zone, and size based on settings
- `dock_popup_sync_layer_margins_if_open(app)` - adjusts popup position when dock moves (e.g., on output layout change)
- `dock_output_position_changed(app, output)` - handles output geometry changes
- `dock_calculate_exclusive_zone(app)` - computes exclusive zone to reserve space

**Position options:**
- Bottom (default): anchored to bottom edge, full width
- Left: anchored to left edge, full height
- Right: anchored to right edge, full height
- Offset: configurable pixel offset from edge
- Margin: gap between dock and screen edge

---

### `dock_settings.hpp`

**Purpose:** `DockSettings` struct with all dock configuration.

**Fields:**
- `iconPx` - icon size in pixels (32..200, default 48)
- `autoHide` - whether dock auto-hides
- `autoHideDelayMs` - delay before auto-hide (default 500)
- `dockPosition` - panel position (Bottom, Left, Right)
- `dockOffset` - offset from screen edge
- `dockMargin` - gap from screen edge
- `pinnedAppIds` - list of pinned app IDs (string vector)
- `widgetLayout` - `WidgetLayout { left, center, right }` - widget token lists
- `showTray` - whether system tray is visible
- `showWorkspaces` - whether workspace strip is visible
- `showTrash` - whether trash icon is visible
- `popupMargin` - gap between dock and popup surfaces
- `windowFocusHoverDelayMs` - delay before focusing window on hover
- `displayMode` - `PinnedOnly`, `VisibleWindows`, `All`
- `startMenuPinnedAppIds` - separate pinned apps for start menu
- `startMenuIconSize` - icon size within start menu

---

### `dock_foreign_toplevel.cpp` / `dock_foreign_toplevel.hpp`

**Purpose:** Window tracking via `zwlr_foreign_toplevel_manager_v1`.

**Functions:**
- `dock_foreign_toplevel_bind_manager_and_hooks(app)` - binds the toplevel manager, registers event handlers

**Window state tracking:**
- On open: add to `foreignToplevels` list, emit signal
- On close: remove from list, emit signal
- On change: update title, app_id, state (minimized, maximized, fullscreen, activated)

**Window-to-pin matching:** Uses `dock_pin_identity_norm_matches_anchor()` to match running windows to pinned apps for the running indicator.

---

### `dock_launch_feedback.cpp` / `dock_launch_feedback.hpp`

**Purpose:** Launch bounce animation.

**Functions:**
- `dock_start_launch_bounce(app, path)` - starts bounce animation for launched app
- `dock_launch_bounce_extra_lift_y(app, is_app_slot, slot_key)` - returns Y offset for bounce animation

**Animation:**
- Spring-based: decays over ~500ms
- Max bounce height: 20px above normal position
- Applied as extra Y offset on the icon

---

### `dock_anim.hpp`

**Purpose:** Animation types.

**Functions:**
- `dock_anim_start_slide(app, from, to, duration)` - starts a position slide animation (used for dock show/hide)
- `dock_anim_start_hover_lift(app, slot_idx)` - starts hover lift animation for a slot

---

### `dock_tooltip.cpp` / `dock_tooltip.hpp`

**Purpose:** Tooltip display on hover.

**Functions:**
- `dock_tooltip_show(app, text, x, y)` - creates a layer-surface tooltip with text at position
- `dock_tooltip_hide(app)` - destroys tooltip surface
- `dock_tooltip_tick(app)` - tooltip lifecycle: shows after delay, follows pointer
- `dock_tooltip_cleanup(app)` - cleanup on shutdown

**Timing:**
- Show delay: 500ms (configurable)
- Tooltip follows pointer while hovering the same slot

---

### `dock_raster_backend.cpp` / `dock_raster_backend.hpp`

**Purpose:** Rendering backend selection and buffer management.

**Functions:**
- `dock_cpu_render(app, frame_time)` - CPU path: `cairo_image_surface_create` + `wl_shm` buffer, commit via `wl_surface`
- `dock_vulkan_render(app, frame_time)` - GPU path: Vulkan image allocation, Cairo renders to CPU, upload via `vkCmdBlitImage`, display via `zwp_linux_dmabuf_v1`
- `dock_render(app, frame_time)` - selects backend, handles frame lifecycle

---

### `dock_trash.cpp`

**Purpose:** Trash state monitoring.

**Functions:**
- `dock_trash_update(app)` - reads trash directory `~/.local/share/Trash/files/`
- `dock_trash_full()` - returns true if trash has any files
- `dock_trash_open(app)` - opens trash in file manager

---

### `dock_widget_tokens.cpp` / `dock_widget_tokens.hpp`

**Purpose:** Widget token parsing from dock settings.

**Functions:**
- `dock_first_control_center_widget_id(settings)` - scans widget layout for first token that sounds like control center

---

### Remaining Files

| File | Purpose |
|---|---|
| `dock_input_region.hpp` | Computes and sets input region for dock layer surfaces (excludes gaps/transparent areas) |
| `dock_layer_outputs.hpp` | Per-output surface management, output enumeration for layer creation |
| `dock_layout_shared.hpp` | `dock_pointer_on_any_dock_layer()` - checks if pointer is over any dock surface (for auto-hide) |
| `dock_pin_identity.cpp` / `dock_pin_identity.hpp` | Matches running window app_ids to pinned app identities (normalization) |
| `dock_strip_geometry.hpp` | Computes pill geometry: position and dimension of the widget bar pill |
