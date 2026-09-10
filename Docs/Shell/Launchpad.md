# Launchpad

Located in `src/desktop_shell/launchpad/`. A full-screen application launcher overlay.

---

## File-by-File Detail

### `launchpad_host.cpp` / `launchpad_host.hpp`

**Purpose:** Launchpad state management and lifecycle.

**Constants:**
- Grid: configurable columns (default 15) and rows (default 6) from `ShellAppearance`
- Cell gap: configurable from settings
- Icon fill percentage: configurable (default 58%)
- Layout scale percentage: configurable (default 100%)

**Key functions:**
- `launchpad_init(app)` - creates launchpad state, reads config
- `launchpad_show(app)` - opens launchpad overlay
- `launchpad_hide(app)` - closes launchpad overlay
- `launchpad_toggle(app)` - toggle visibility
- `launchpad_visible(app)` - returns visibility state

**State:** `LaunchpadState` struct: `visible`, `query` (search text), `hits` (search results), `sel` (selected index), `scrollPx`, `categories`, `selectedCategory`

### `launchpad_layer.cpp` / `launchpad_layer.hpp`

**Purpose:** Layer-surface creation and rendering.

**Functions:**
- `launchpad_layer_create(app, output)` - creates full-screen overlay surface per output
- `launchpad_layer_destroy(app, output)` - destroys overlay surface
- `launchpad_render(app)` - renders the launchpad:
  1. Background: semi-transparent dark overlay (full screen)
  2. Search bar at top (centered, rounded field)
  3. Category pills below search bar
  4. App grid: icons in configurable grid layout with labels below
  5. Hover highlight on grid items
  6. Selection highlight on focused item

**Layout:**
- Search bar: 50% width, centered at top
- Category pills: centered below search
- App grid: centered, `launchpadGridColumns` x `launchpadGridRows` cells
- Each cell: icon (configurable size via `launchpadIconFillPct`) + label (truncated to fit)

### `launchpad_search.cpp` / `launchpad_search.hpp`

**Purpose:** Search within the launchpad.

**Functions:**
- `launchpad_search_update(app)` - executes search against desktop entry catalog
- `launchpad_search_clear(app)` - clears search and resets to default grid
- `launchpad_search_handle_key(app, key)` - incremental search on keypress

**Search:** Same fuzzy-matching algorithm as the app drawer (`desktop_list.cpp`). Searches by name, generic name, keywords. Category filtering preserves the selected category.
