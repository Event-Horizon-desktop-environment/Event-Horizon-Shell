# Notifications

Located in `src/desktop_shell/notifications/`. Complete notification system from D-Bus service to on-screen toasts.

---

## File-by-File Detail

### `notification_types.hpp` (76 lines)

**Purpose:** All notification data types.

**Enums:**
- `Urgency : uint8_t { Low=0, Normal=1, Critical=2 }` - notification priority level
- `CloseReason : uint32_t { Expired=1, Dismissed=2, ClosedByCall=3 }` - why notification was closed
- `NotificationOrigin : uint8_t { External=0, Internal=1 }` - source of notification
- `NotificationEvent { Added, Updated, Closed }` - event type for subscribers

**Type aliases:** `Clock = std::chrono::steady_clock`, `TimePoint = Clock::time_point`

**Struct `NotificationImageData`:** Image data for notification (app icon or inline image). Fields: `width`, `height`, `rowStride`, `hasAlpha`, `bitsPerSample`, `channels`, `data` (raw pixel bytes).

**Struct `Notification`:** Complete notification:
- `id` (uint32) - unique notification ID
- `origin` - External or Internal (from shell)
- `appName`, `summary`, `body` - text content
- `timeout` (int32) - display duration in ms (-1 = default)
- `urgency` - Low/Normal/Critical
- `actions` - vector of action ID/label pairs
- `icon` - optional icon name
- `imageData` - optional inline image
- `category` - optional XDG category
- `desktopEntry` - optional associated .desktop file
- `receivedTime` / `expiryTime` - timing

**Struct `NotificationHistoryEntry`:** `notification` (the data), `active` (currently displayed), `closeReason`, `eventSerial` (monotonic counter for ordering).

**Constants:** `kDefaultNotificationTimeoutMs = 6000`

---

### `notifications.cpp` / `notifications.hpp` (391 lines)

**Purpose:** Central notification state management.

**Class `NotificationManager`:**

**Private state:**
- `history_` - `deque<NotificationHistoryEntry>` (max 100 entries)
- `history_index_` - `unordered_map<uint32_t, size_t>` for O(1) lookup by ID
- `active_` - `vector<Notification>` currently visible notifications
- `change_serial_` - monotonic counter for change tracking
- `listeners_` - registered callbacks for notification events

**Public methods:**
- `upsert(notification)` - add new or update existing notification
- `close(id, reason)` - close notification, move to history with close reason
- `close_all()` - close all active notifications
- `close_action(id, action_key)` - invoke an action button
- `active_notifications()` - returns current active list
- `history()` - returns notification history
- `notify_listeners(event, notification)` - emits events to registered listeners
- `add_listener(callback)` / `remove_listener(id)` - listener management

**Private methods:**
- `rebuildHistoryIndex()` - rebuilds the history ID-to-index map
- `upsertHistory(notification, active, close_reason)` - inserts/updates history entry
- `scheduleExpiry(notification)` - starts timer for auto-dismiss (unless urgency=Critical)
- `handleExpiry(id)` - called when timeout fires

**Expiry logic:**
- timeout > 0: expires after timeout ms from receive time
- timeout = 0: never expires (requires manual dismiss)
- timeout = -1: uses `kDefaultNotificationTimeoutMs` (6s)
- Critical urgency: never auto-expires

**Duplicate detection:** If same app sends same summary+body within 1 second, it's treated as an implicit duplicate (replaces previous, resetting timer).

---

### `notification_toast_host.cpp` / `notification_toast_host.hpp` (710 lines)

**Purpose:** On-screen toast notification rendering and interaction.

**Constants:**
- `kMaxCards = 4` - maximum visible toasts at once
- Card dimensions (scaled by UI scale): `kCardW = 360`, `kCardH = 128`, `kCardGap = 8`
- Layout: `kPad = 14`, `kIconTextGap = 10`, `kIconSz = 42`, `kIconRad = 10`
- `kProgressH = 3` - progress bar height (for notifications with progress)
- `kCloseBtnSz = 20`, `kHeaderGap = 6`, `kSummaryBodyGap = 4`

**Class `NotificationToastHost`:**
- Manages a layer-shell overlay surface for toast display
- Creates surface with `ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY` (above panels but below lockscreen)
- Positions at top or bottom of screen based on config
- Renders notification cards in a vertical stack
- Handles pointer input for card interaction

**Rendering per card:**
1. App icon (from `NotificationImageData` or themed icon)
2. App name (bold, header style)
3. Summary (bold, primary text)
4. Body (normal weight, secondary text)
5. Close button (X) in top-right corner
6. Action buttons (horizontal row at bottom, if any)
7. Progress bar (if notification has progress data)
8. Urgency indicator: Critical gets a red left border

**Input handling:**
- Click on card body: opens associated app (if `desktopEntry` is set)
- Click on action button: invokes notification action via D-Bus
- Click on close button: dismisses notification
- Keyboard: Escape dismisses top notification

**Animation:**
- Slide in from top/bottom edge (120ms)
- Fade out on dismiss (80ms)
- Stack adjusts as cards are added/removed (interpolated)

---

### `notifications_notify.hpp`

**Purpose:** Desktop Notifications specification helpers. Provides:
- Server information strings (name, vendor, version, spec version)
- Capability list: `body`, `body-hyperlinks`, `body-images`, `actions`, `icon-static`, `action-icons`, `persistence`
- Default notification icon resolution order
