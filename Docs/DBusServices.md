# D-Bus Services

Located in `src/services/`. Provides D-Bus client implementations for integrating with system and session services. 26 source files.

---

## File-by-File Detail

### Notifications

#### `notifications/notification_dbus_service.cpp` / `notification_dbus_service.hpp`

**Purpose:** Implements the `org.freedesktop.Notifications` D-Bus interface as a client. Receives notification events from applications and forwards them to the shell's notification manager.

**Functions:**
- `NotificationDbusService::init()` - connects to session bus, registers object path `/org/freedesktop/Notifications`
- Handles `Notify`, `CloseNotification`, `GetServerInformation`, `GetCapabilities` calls
- Manages notification ID generation (incrementing uint32)
- Forwards to `NotificationManager` for display

### System Tray

#### `tray.hpp` / `tray_item.hpp`

**Purpose:** Type definitions for the StatusNotifier protocol.

**`TrayItem` struct:** `service_name`, `object_path`, `icon_name`, `attention_icon_name`, `overlay_icon_name`, `tooltip`, `category` (Application, System, Hardware), `status` (Active, Passive, NeedsAttention), `item_is_menu` flag.

#### `tray.cpp` / `tray.hpp`

**Purpose:** StatusNotifierWatcher host implementing `org.kde.StatusNotifierWatcher`.

**`TrayHost` class:**
- Registers `/StatusNotifierHost` on session bus
- Handles item registration (`RegisterStatusNotifierItem`)
- Item deregistration, item lookup by service/object path
- Emits `StatusNotifierItemRegistered` / `StatusNotifierItemUnregistered` signals

#### `tray_manager.cpp` / `tray_manager.hpp`

**Purpose:** High-level tray management.

**Functions:**
- `tray_manager_init(app)` - initializes tray host
- `tray_manager_create_tray_surface()` - creates tray widget surface
- `tray_manager_update()` - updates tray item list display
- `tray_manager_emit_change()` - emits change signal for widget bar

#### `tray_context_menu.cpp` / `tray_context_menu.hpp`

**Purpose:** D-Bus menu interface (`com.canonical.dbusmenu`).

**Functions:**
- `tray_menu_open(service, object_path, x, y)` - opens tray context menu at position
- `tray_menu_close()` - closes open menu
- Handles `GetLayout`, `GetProperty`, `Event`, `AboutToShow` calls
- Renders tray context menus with submenus, checkboxes, radio items, separators
- Tracks menu item activation

#### `tray_env_filter.cpp` / `tray_env_filter.hpp`

**Purpose:** Environment variable sanitization for tray applications. Removes Wayland-specific variables that might confuse XWayland tray apps.

#### `tray_sni_icons.cpp` / `tray_sni_icons.hpp`

**Purpose:** Themed icon resolution for StatusNotifierItem icons.

**Functions:**
- `tray_resolve_icon(item)` - resolves tray icon from theme (hicolor/Adwaita) with proper fallback
- `tray_icon_to_pixmap(item)` - converts named icon to Cairo surface
- Handles icon name → pixmap conversion with size matching

#### `tray_stream_icon.cpp` / `tray_stream_icon.hpp`

**Purpose:** Streaming/animated icon support.

**Functions:**
- `tray_stream_icon_init(item)` - initializes streaming icon state
- `tray_stream_icon_tick(item)` - advances animation frame
- `tray_stream_icon_get_frame(item)` - returns current animation frame as Cairo surface
- Frame-based animation with configurable interval. Used for apps with live tray icons (music players, messengers).

### MPRIS

#### `mpris_player.cpp` / `mpris_player.hpp`

**Purpose:** Media Player Remote Interfacing Specification client.

**`MprisPlayerManager` class:**
- Monitors `org.mpris.MediaPlayer2.*` on session bus via name owner tracking
- Per-player state: metadata (title, artist, album, art URL), playback status (Playing, Paused, Stopped), position, volume, shuffle/repeat/loop
- Methods: `Play`, `Pause`, `PlayPause`, `Next`, `Previous`, `Seek`, `SetPosition`, `OpenUri`
- Signal-based live updates via `PropertiesChanged`

#### `mpris_album_art.cpp` / `mpris_album_art.hpp`

**Purpose:** Downloads and caches album art from MPRIS `mpris:artUrl`.

**`MprisAlbumArtCache` class:**
- `get_art(player, callback)` - downloads/retrieves album art
- Supports local file paths and HTTP URLs
- Caches decoded images as Cairo surfaces with LRU eviction
- Used by media player widget and popup

### Network

#### `network_manager_service.cpp` / `network_manager_service.hpp`

**Purpose:** NetworkManager D-Bus client.

**`NetworkManagerService` class:**
- Monitors: active connections, Wi-Fi access points (SSID, strength, security, frequency band), device state (ethernet, wifi, modem), global connectivity state (full, limited, none)
- Methods: `connect(ssid, password)`, `disconnect(ssid)`, `forget_network(ssid)`, `scan_wifi()`, `connect_ethernet()` / `disconnect_ethernet()`
- Emits signals on state changes via `PropertiesChanged`
- Auto-reconnects to known networks

#### `network_secret_agent.cpp` / `network_secret_agent.hpp`

**Purpose:** Implements `org.freedesktop.NetworkManager.SecretAgent`.

**Functions:**
- Responds to `GetSecrets` calls from NetworkManager
- Shows Polkit auth dialog or uses stored keyring passwords
- Falls back to manual password prompt for unknown networks

#### `network_types.hpp`

**Purpose:** Data types for network management.

**Types:**
- `NetworkDevice` - device info (name, type, state, interface, hw_address)
- `ActiveConnection` - connection info (id, uuid, type, device, state, vpn)
- `AccessPoint` - Wi-Fi access point (ssid, bssid, strength, security, frequency, channel)
- `SecurityType` enum: Open, WEP, WPA, WPA2, WPA3, Enterprise
- `ConnectivityState` enum: Full, Limited, None, Portal

### Bluetooth

#### `bluetooth/bluez_service.cpp` / `bluez_service.hpp`

**Purpose:** BlueZ D-Bus client.

**`BluezService` class:**
- Monitors adapter state: powered, discoverable, pairable, discovering
- Monitors device list: connected, paired, trusted, name, address, battery level, icon
- Methods: `set_powered(bool)`, `start_discovery()`, `stop_discovery()`, `pair(address)`, `unpair(address)`, `connect(address)`, `disconnect(address)`
- Signal-based: `DeviceFound`, `DeviceRemoved`, `DevicePropertyChanged`, `AdapterPropertyChanged`
- Used by control center for Bluetooth UI

### UDisks2

#### `udisks2/udisks2_drive_service.cpp` / `udisks2_drive_service.hpp`

**Purpose:** UDisks2 D-Bus client for storage drive management.

**`UDisks2Service` class:**
- Monitors drive addition/removal, mounting state
- Per-drive: device path, model, vendor, size, mount points, filesystem type, removable flag
- Methods: `mount(drive)`, `unmount(drive)`, `eject(drive)`, `unlock_encrypted(drive, password)`
- Used by desktop for removable drive icons and archive mounting

### Keyring

#### `keyring/secret_service_daemon.cpp` / `secret_service_daemon.hpp`

**Purpose:** Secret Service (org.freedesktop.SecretService) client.

**`SecretServiceDaemon` class:**
- `store_password(service, key, password)` - stores a password in the default keyring
- `lookup_password(service, key)` - retrieves a stored password
- `delete_password(service, key)` - removes a stored password
- `unlock_keyring()` - prompts for keyring unlock via auth dialog
- `keyring_locked()` / `keyring_unlocked()` - state tracking
- Used for persisting Wi-Fi passwords, network secrets, and other sensitive data
