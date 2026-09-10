#pragma once

#include <cstdint>

namespace m3 {

// M3 Expressive (May 2025) adds cut corners as an alternative to rounded.
enum class CornerFamily : uint8_t { Rounded, Cut };

// Seven M3 shape families. All values in dp.
struct ShapeTokens {
  CornerFamily family = CornerFamily::Rounded;

  float none          = 0.0f;   // No rounding
  float extraSmall    = 4.0f;   // Checkbox, snackbar, menu, tooltip
  float small         = 8.0f;   // Chips, segmented segments
  float medium        = 12.0f;  // Cards, FAB (small), text field
  float large         = 16.0f;  // FAB (standard), extended FAB
  float extraLarge    = 28.0f;  // Dialog, bottom sheet (top), search bar
  float full          = 999.0f; // Pill / circle
};

// Per-component shape assignments:
//
//   Component              | Shape          | Value
//   -----------------------|----------------|-------
//   Filled button          | full           | 999dp
//   Outlined button        | full           | 999dp
//   Tonal button           | full           | 999dp
//   Text button            | full           | 999dp
//   FAB (small)            | medium         | 12dp
//   FAB (standard)         | large          | 16dp
//   FAB (large)            | extra-large    | 28dp
//   Extended FAB           | large          | 16dp
//   Card (all)             | medium         | 12dp
//   Dialog                 | extra-large    | 28dp
//   Bottom sheet (top)     | extra-large    | 28dp top only
//   Nav bar indicator      | full           | 999dp
//   Nav rail indicator     | full           | 999dp
//   Search bar             | extra-large    | 28dp
//   Menu                   | extra-small    | 4dp
//   Tooltip                | extra-small    | 4dp
//   Snackbar               | extra-small    | 4dp
//   Chip                   | small          | 8dp
//   Segmented (outer)      | full           | 999dp
//   Segmented (inner)      | small          | 8dp
//   Checkbox               | extra-small    | 4dp
//   Switch track           | full           | 999dp
//   Slider track           | full           | 999dp
//   Slider thumb           | full           | 999dp
//   Text field (filled)    | extra-small-top| 4dp top only
//   Text field (outlined)  | extra-small    | 4dp
//   Square button          | medium         | 12dp (M3 Expressive)

} // namespace m3
