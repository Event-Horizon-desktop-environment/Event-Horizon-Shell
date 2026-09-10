#pragma once

namespace m3 {

// Six M3 elevation levels. Shadow colour from ColorRole::Shadow.
// Ambient shadow = blur radius. Penumbra offset Y = level * 0.5dp.
struct ElevationTokens {
  float level0 = 0.0f;    // No shadow. Cards (filled), buttons, nav rail
  float level1 = 1.0f;    // Hint of depth. Elevated cards, modal drawer
  float level2 = 3.0f;    // Subtle. Nav bar, FAB resting
  float level3 = 6.0f;    // Medium. Dialog, FAB pressed, snackbar
  float level4 = 8.0f;    // Prominent. FAB hover, menu
  float level5 = 12.0f;   // Deepest. FAB focus (keyboard)
};

// Per-component elevation per state:
//
//   Component              | Rest | Hover | Focus | Press
//   -----------------------|------|-------|-------|-------
//   Filled button          | 0    | 0     | 0     | 0
//   Elevated button        | 1    | 2     | 2     | 2
//   FAB                    | 3    | 4     | 5     | 4
//   Card (elevated)        | 1    | 2     | 2     | —
//   Card (filled)          | 0    | 1     | 1     | —
//   Card (outlined)        | 0    | 1     | 1     | —
//   Dialog                 | 3    | —     | —     | —
//   Bottom sheet           | 0    | —     | —     | —
//   Snackbar               | 3    | —     | —     | —
//   Navigation bar         | 2    | —     | —     | —
//   Navigation drawer (mod)| 1    | —     | —     | —
//   Menu                   | 2    | —     | —     | —
//   Tooltip                | 2    | —     | —     | —
//   Chip                   | 0    | —     | —     | —

} // namespace m3
