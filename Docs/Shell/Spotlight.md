# Spotlight

Located in `src/desktop_shell/spotlight/`. Provides keyboard-activated application search (similar to macOS Spotlight).

---

## File-by-File Detail

### `spotlight_search.hpp`

**Purpose:** Declares search types and functions.

**Struct `SpotlightHit`:** Search result entry shared across app drawer, spotlight, and launchpad.
- `string name`, `genericName`, `comment` - display text
- `string exec` - command to run
- `string iconKey` - icon theme key
- `string path` - .desktop file path
- `string categories` - semicolon-delimited XDG categories
- `string keywords` - semicolon-delimited search keywords
- `string mimeType` - associated MIME type

### `spotlight_query.cpp` / `spotlight_query.hpp`

**Purpose:** Search query execution.

**Functions:**
- `spotlight_query(query, max_results)` - executes search across:
  1. Desktop entries matching by name / generic name / comment / keywords / exec
  2. Running applications matching by window title / app ID
  3. Math expressions (if input looks like "2+2" or "sqrt(16)")
  4. System actions: "lock", "logout", "restart", "shutdown", "sleep", "hibernate"
- `spotlight_query_cancel()` - cancels running query (for fast retyping)

**Scoring:** Results sorted by relevance:
- Exact match: +100
- Prefix match: +50
- Word boundary match: +10
- Substring match: +5
- Description match: +2

### `popup_paint_spotlight.cpp` / `spotlight_paint.hpp`

**Purpose:** Spotlight popup rendering.

**Constants:** `kPopupW = 600`, `kPopupH = 400` (configurable)

**Rendering:**
1. Search bar at top (rounded field, search icon, text cursor)
2. Result list below search (scrollable)
3. Each result row: app icon + name + description (truncated)
4. Selected item highlight (accent background)
5. Keyboard shortcut hints next to system actions
6. Calculator result shown inline when expression detected

**Keyboard navigation:**
- Up/Down: navigate results
- Enter: launch selected app/action
- Escape: close spotlight
- Tab: jump to search field
