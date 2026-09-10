# Control Center

Located in `src/desktop_shell/controlcenter/`. The control center is a popup panel providing quick settings toggles, audio control, network/Bluetooth management, media player, and notification display.

---

## File-by-File Detail

### `control_center_state.hpp`

**Purpose:** State structures for the control center UI.

**Structs:**
- `ControlCenterAudioState` - volume (0-1), mute state, audio service availability
- `ControlCenterNetworkState` - connectivity state, active connection (SSID), wifi scanning state, access point list
- `ControlCenterBluetoothState` - adapter powered, device list (connected/paired)
- `ControlCenterWeatherState` - temperature, conditions, icon name, location
- `ControlCenterNightlightState` - enabled state, temperature
- `ControlCenterInputDevice` - device name, id, muted, volume
- `ControlCenterOutputDevice` - device name, id, muted, volume, default flag
- `MixerStream` - per-application stream: app_name, icon_name, volume, muted, node_id
- `MixerUiState` - UI state: show output devices, show input devices, show mixer
- `ControlCenterPanelState` - all above plus expanded/visible sections, panel geometry

---

### `control_center_paint.cpp` / `control_center_paint.hpp`

**Purpose:** Control center panel rendering.

**Functions:**
- `control_center_paint_header(cr, state)` - renders header with close button
- `control_center_paint_quick_settings(cr, state)` - renders toggle buttons row: Wi-Fi, Bluetooth, Nightlight, Do Not Disturb, VPN
- `control_center_paint_sliders(cr, state)` - renders volume (output/input) and brightness sliders
- `control_center_paint_media(cr, state)` - renders current media player card
- `control_center_paint_network(cr, state)` - renders network section (active connection, available networks)
- `control_center_paint_bluetooth(cr, state)` - renders Bluetooth section (devices list)
- `control_center_paint_weather(cr, state)` - renders weather section
- `control_center_paint(cr, W, H, state)` - **main paint function**: layers all sections vertically

**Section layout (top to bottom):**
1. Header (close button, title "Quick Settings")
2. Quick settings toggles (2xN grid)
3. Audio sliders (output volume, input volume, output device selector)
4. Media player
5. Network toggle + active connection info + available networks list
6. Bluetooth toggle + paired devices list
7. Weather summary
8. Notification strip (recent notifications from `NotificationManager`)

**Toggle buttons:** Each shows icon + label, hover highlight, and active state (filled accent when on, dim when off).

**Sliders:** Audio and brightness use custom slider metrics (track, thumb with drag handle).

---

### `control_center_popup_paint.cpp` / `control_center_popup_paint.hpp`

**Purpose:** Control center popup surface rendering wrapper.

**Functions:**
- `dock_popup_paint_control_center(app, cr, config)` - main popup paint entry point. Creates rounded layer-shell surface, calls `control_center_paint()`, renders backdrop and chrome.

---

### `control_center_hit.cpp` / `control_center_hit.hpp`

**Purpose:** Hit testing for control center elements.

**Functions:**
- `control_center_hit_query(x, y, state)` - returns which element is at (x, y):
  - `CloseButton` / `SettingsButton` in header
  - `Toggle(idx)` in quick settings grid
  - `Slider(Sink|Source|Brightness)` in sliders section
  - `MediaPrev` / `MediaPlayPause` / `MediaNext` / `MediaSeekbar` in media section
  - `NetworkToggle` / `NetworkItem(idx)` in network section
  - `BluetoothToggle` / `BluetoothDevice(idx)` in Bluetooth section
  - `NotificationItem(idx)` in notification strip
  - `None` (miss)

---

### `control_center_panel_geometry.cpp` / `control_center_panel_geometry.hpp`

**Purpose:** Computes control center panel dimensions.

**Functions:**
- `control_center_panel_width()` - returns panel width (configurable, default ~400px)
- `control_center_panel_height(state)` - computes total height based on visible sections
- `control_center_section_geometry(state)` - returns Y positions for each section

---

### `control_center_notifications_strip.cpp` / `control_center_notifications_strip.hpp` / `control_center_notifications_layout.hpp`

**Purpose:** Notification strip within the control center.

**Functions:**
- `control_center_notifications_paint(cr, x, y, w, state)` - renders notification list
- `control_center_notifications_height(count)` - computes strip height based on notification count

**Rendering:** Each notification shows: app icon, app name, summary, body (truncated), timestamp. Critical notifications highlighted with red accent. Empty state shows "No notifications" text.

---

### `control_center_anim.hpp`

**Purpose:** Animation timing constants for control center.

**Constants:**
- Panel show/hide duration: 150ms
- Section expand/collapse: 200ms
- Slider drag: immediate (no animation)
- Toggle switch: 100ms crossfade

---

### `control_center_slider_metrics.hpp`

**Purpose:** Slider dimensions and positioning.

**Constants:**
- Track width: 4px
- Thumb diameter: 20px
- Track length: section width - 2*margin
- Slider height (with labels): 48px

---

### `mixer_stream_icon_resolve.cpp` / `mixer_stream_icon_resolve.hpp`

**Purpose:** Resolves audio stream/application icons for the volume mixer UI.

**Functions:**
- `mixer_stream_icon_for(stream, icons)` - returns Cairo surface for stream icon (from app icon, themed icon, or default audio icon)
- `mixer_stream_icon_name(app_name, icon_name)` - resolves icon theme name for stream
- `mixer_stream_icon_fallback()` - returns default audio glyph for streams without icons
