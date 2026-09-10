# UI Components

Located in `src/ux/`. Reusable UI components used by the settings application. 6 `.cpp` / 8 `.hpp` files.

---

## File-by-File Detail

### Button

#### `button/ui_button.cpp` / `button.hpp`

**Purpose:** Clickable button widget.

**Key functions:**
- `button_layout(cr, label, icon_glyph, x, y, w, h)` - computes text/icon positions
- `button_paint(cr, state, label, icon_glyph, theme, x, y, w, h)` - renders:
  - Background fill (surface or accent color depending on state)
  - Rounded corners (configurable radius)
  - Icon glyph (Material Symbol) if provided
  - Text label centered
  - Hover state: slight background darken/lighten
  - Press state: background color shift, slight scale
  - Focus state: focus ring

### Slider

#### `slider/ui_slider.cpp` / `slider.hpp`

**Purpose:** Horizontal value slider widget.

**Key functions:**
- `slider_layout(x, y, w, h)` - computes track and thumb geometry
- `slider_paint(cr, value, min, max, theme, x, y, w, h)` - renders:
  - Track background (rounded rect, secondary color)
  - Track fill (from left to thumb position, accent color)
  - Thumb (circle at value position, surface color with shadow)
  - Value label (optional, shown above thumb or to the right)
- `slider_pick(x, y, lx, ly)` - returns normalized value from pointer position
- `slider_value_from_pos(x, min, max, step)` - converts pixel position to snapped value
- Supports: continuous drag, step snapping, keyboard arrow adjustment

### Toggle

#### `toggle/ui_toggle.cpp` / `toggle.hpp`

**Purpose:** On/off toggle switch.

**Key functions:**
- `toggle_layout(x, y, w, h)` - computes track and thumb geometry
- `toggle_paint(cr, on, anim_progress, theme, x, y, w, h)` - renders:
  - Track: off state (secondary color) / on state (accent color) with smooth transition
  - Thumb: circle that slides left (off) to right (on), animated via `anim_progress`
  - Optional label toggled on/off text
- `toggle_pick(x, y, lx, ly)` - hit test
- Animation: `toggle_animate(target)` - interpolates thumb position over 150ms

### Dropdown

#### `dropdown/dropdown.cpp` / `dropdown.hpp`

**Purpose:** Dropdown selection menu.

**Key functions:**
- `dropdown_paint_closed(cr, selected_index, items, theme, x, y, w, h)` - renders:
  - Current selection with chevron-down icon
  - Border with rounded corners
- `dropdown_paint_open(cr, selected_index, items, hover_index, scroll_offset, theme, x, y, w, h)` - renders:
  - Expanded item list with scroll
  - Each item: icon (optional), label, hover highlight
  - Scroll track if content overflows
- `dropdown_pick(x, y, lx, ly, items, scroll_offset)` - returns index or -1
- `dropdown_item_height()` / `dropdown_max_visible_items()` - sizing

### Theme

#### `theme.cpp` / `theme.hpp`

**Purpose:** Theme color definitions for the settings UI.

**`UITheme` struct:**
- `surface` - primary surface background color
- `background` - window background color
- `primary` - accent color for active elements
- `secondary` - secondary accent
- `text` - primary text color
- `textSecondary` - secondary/disabled text color
- `border` - element border color
- `shadow` - drop shadow color
- `inputBg` - input field background
- `inputBorder` - input field border
- `danger` - destructive action color (red)
- `success` - positive action color (green)
- `warning` - warning color (amber)

**Functions:**
- `ui_theme_from_shell(appearance)` - derives UI theme from `ShellAppearance` colors (respects dark/light mode)
- `ui_theme_default_dark()` / `ui_theme_default_light()` - fallback themes
