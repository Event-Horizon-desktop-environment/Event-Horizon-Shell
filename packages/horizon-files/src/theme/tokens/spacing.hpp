#pragma once

namespace m3 {

// Base unit = 8dp. 18 named levels on a linear scale.
struct SpacingTokens {
  float space0   = 0.0f;    // 0×    — none
  float space2   = 2.0f;    // 0.25× — micro gap
  float space4   = 4.0f;    // 0.5×  — tight
  float space6   = 6.0f;    // 0.75× — nested
  float space8   = 8.0f;    // 1×    — default small
  float space10  = 10.0f;   // 1.25× — nested
  float space12  = 12.0f;   // 1.5×  — medium
  float space16  = 16.0f;   // 2×    — standard padding
  float space20  = 20.0f;   // 2.5×  — large inner
  float space24  = 24.0f;   // 3×    — wide padding (dialog)
  float space28  = 28.0f;   // 3.5×  — extra
  float space32  = 32.0f;   // 4×    — section
  float space36  = 36.0f;   // 4.5×
  float space40  = 40.0f;   // 5×    — large section
  float space48  = 48.0f;   // 6×    — touch target minimum
  float space56  = 56.0f;   // 7×    — FAB diameter
  float space64  = 64.0f;   // 8×
  float space72  = 72.0f;   // 9×
};

// Semantic spacing conventions:
//
//   Context                    | Value | Usage
//   ---------------------------|-------|------------------------------
//   Container content          | 16dp  | Cards, dialogs, sheets, menus
//   Container compact          | 12dp  | Small containers, chips
//   Container wide             | 24dp  | Large dialogs, wide cards
//   Between elements (tight)   | 8dp   | Icon-label pairs
//   Between elements (default) | 12dp  | Related controls
//   Between elements (group)   | 16dp  | Distinct groups
//   Section spacing            | 24dp  | Page sections
//   Touch target minimum       | 48dp  | All interactive elements
//   Button horizontal          | 24dp  | Filled, tonal, outlined
//   Button horizontal (compact)| 16dp  | Small / icon buttons
//   List item horizontal       | 16dp  | List rows
//   List item vertical (1L)    | 12dp  | Single-line rows
//   List item vertical (multi) | 16dp  | Multi-line rows

// Per-component sizing:
//
//   Component              | Size / Height / Width
//   -----------------------|-----------------------
//   Button XS              | 24dp height, 16dp horizontal pad
//   Button S               | 32dp height, 16dp horizontal pad
//   Button M (default)     | 40dp height, 24dp horizontal pad
//   Button L               | 48dp height, 24dp horizontal pad
//   Button XL              | 56dp height, 24dp horizontal pad
//   Slider track height    | 4dp default, 6dp (L)
//   Slider thumb XS/S      | 16dp
//   Slider thumb M         | 20dp
//   Slider thumb L/XL      | 24dp
//   Switch track S         | 28×16dp
//   Switch track M         | 32×20dp
//   Switch track L         | 40×24dp
//   Switch handle S        | 12dp
//   Switch handle M        | 16dp
//   Switch handle L        | 20dp
//   Checkbox visual        | 18dp
//   Checkbox touch target  | 48×48dp
//   Radio visual           | 20dp
//   Radio touch target     | 48×48dp
//   Chip height            | 32dp
//   Chip leading icon      | 18dp
//   Chip close icon        | 18dp
//   Chip horizontal pad    | 16dp (8dp with icon)
//   Card padding           | 16dp
//   Dialog title pad       | 24/24/16/24 (T/R/B/L)
//   Dialog content pad     | 0/24/24/24  (T/R/B/L)
//   Nav bar height         | 80dp
//   Nav bar icon           | 24dp
//   Nav bar indicator      | 64×32dp
//   Nav rail width         | 80dp
//   Nav rail item height   | 56dp
//   Nav rail indicator     | 56×32dp
//   Tab height             | 48dp
//   Tab min width          | 90dp primary / 64dp secondary
//   Tab indicator height   | 3dp
//   Top app bar (small)    | 64dp
//   Top app bar (medium)   | 112dp
//   Top app bar (large)    | 152dp
//   FAB (standard)         | 56dp
//   FAB (small)            | 40dp
//   FAB (large)            | 96dp
//   Bottom bar height      | 80dp
//   Snackbar min width     | 344dp
//   Snackbar max width     | 564dp
//   Snackbar margin        | 24dp from edges
//   Dialog width           | 328dp min
//   Bottom sheet drag hdl  | 32×4dp
//   Search bar height      | 56dp
//   Date picker width      | 328dp
//   Date day cell          | 40dp
//   Time picker clock      | 256dp diam
//   FAB icon (small/std)   | 24dp
//   FAB icon (large)       | 36dp
//   Icon button            | 24dp icon, 48×48 touch
//   Button icon            | 20dp leading/trailing

} // namespace m3
