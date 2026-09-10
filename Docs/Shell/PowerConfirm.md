# Power Confirm

Located in `src/desktop_shell/power_confirm/`. A standalone power action confirmation modal dialog used outside the app drawer context.

---

## File-by-File Detail

### `power_confirm.hpp`

**Purpose:** Public API for the power confirmation modal.

**Struct `CardGeom`:**
- `double cardX, cardY, cardW, cardH` - main card rectangle
- `double closeX, closeY, closeW, closeH` - close button (X)
- `double cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH` - Cancel button
- `double confirmBtnX, confirmBtnY, confirmBtnW, confirmBtnH` - Confirm button

**Enums (returned by pick):** `Pick { Outside=-1, Cancel=0, Confirm=1, CloseX=2 }`

**Functions:**
- `layout(double W, double H, double uiScale)` - computes card geometry
- `paint(cairo_t*, W, H, pendingIdx, px, py, uiScale, chrome)` - renders the modal
- `pick(W, H, pendingIdx, lx, ly)` - hit tests modal elements, returns `Pick`

### `power_confirm.cpp` (196 lines)

**Purpose:** Implementation of the power confirmation modal.

**Anonymous namespace helpers:**
- `in_rect(lx, ly, rx, ry, rw, rh)` - point-in-rect test
- `rr(cr, x, y, w, h, rad)` - rounded rectangle path
- `title_for(idx)` - "Log out?", "Restart?", "Shut down?"
- `message_for(idx)` - descriptive text: "End this session...", "The system will restart.", "The system will power off."
- `confirm_label_for(idx)` - "Log out", "Restart", "Shut down"

**`layout()` function:**
Computes geometry centered on screen:
1. Card: max 400px wide, approximately 200px tall, centered
2. Title at top of card with padding
3. Message below title
4. Cancel button (left) and Confirm button (right) at bottom
5. Close X button in top-right corner of card

**`paint()` function:**
1. Dark semi-transparent overlay (full screen dim)
2. Card shadow (Gaussian blur effect via multi-layer alpha)
3. Card background (surface fill color)
4. Close button (X glyph)
5. Title text (semibold, primary color)
6. Message text (normal, secondary color)
7. Cancel button (outlined, default)
8. Confirm button (filled, accent or error color for shutdown)

**`pick()` function:**
1. If inside close button → `CloseX`
2. If inside Cancel button → `Cancel`
3. If inside Confirm button → `Confirm`
4. If outside card → `Outside`
5. Otherwise → `Cancel` (treat as cancel-safe)

---

## Difference from App Drawer Modal

The app drawer power modal in `app_drawer_power_modal.cpp` is rendered within the app drawer surface. This standalone version is used by:
- The control center (if power button is placed there)
- The launchpad (if power actions are exposed)
- Future features requiring power confirmation outside the app drawer

Both share the same visual design but have different geometry computation (this version is centered on screen, the app drawer version is centered within the drawer).
