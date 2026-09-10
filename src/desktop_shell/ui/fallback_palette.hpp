#pragma once

// Fallback M3 palette used when dynamic colour is disabled.
// Updated June 2026 for improved contrast — matches the custom M3 scheme.

namespace eh::ui {

struct Rgb { float r, g, b; };

struct FallbackPalette {
  Rgb primary;
  Rgb onPrimary;
  Rgb secondary;
  Rgb onSecondary;
  Rgb tertiary;
  Rgb onTertiary;
  Rgb error;
  Rgb onError;
  Rgb surface;
  Rgb onSurface;
  Rgb surfaceVariant;
  Rgb onSurfaceVariant;
  Rgb outline;
  Rgb shadow;
  Rgb hover;
  Rgb onHover;
};

// Dark theme
inline constexpr FallbackPalette kFallbackDark = {
  .primary          = { 0.769f, 0.659f, 0.941f },  // #C4A8F0
  .onPrimary        = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
  .secondary        = { 0.239f, 0.125f, 0.439f },  // #3D2070 (primaryContainer)
  .onSecondary      = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
  .tertiary         = { 0.910f, 0.522f, 0.290f },  // #E8854A
  .onTertiary       = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
  .error            = { 0.910f, 0.416f, 0.353f },  // #E86A5A (unchanged)
  .onError          = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
  .surface          = { 0.102f, 0.075f, 0.188f },  // #1A1330
  .onSurface        = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
  .surfaceVariant   = { 0.133f, 0.102f, 0.227f },  // #221A3A (surfaceContainer)
  .onSurfaceVariant = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF (at 65% — see color.hpp)
  .outline          = { 0.478f, 0.416f, 0.588f },  // #7A6A96
  .shadow           = { 0.031f, 0.020f, 0.063f },  // #080510
  .hover            = { 0.212f, 0.165f, 0.337f },  // #362A56 (surfaceBright)
  .onHover          = { 1.0f,   1.0f,   1.0f   },  // #FFFFFF
};

// Light theme
inline constexpr FallbackPalette kFallbackLight = {
  .primary          = { 0.361f, 0.208f, 0.659f },  // #5C35A8
  .onPrimary        = { 0.0f,   0.0f,   0.0f   },  // #000000
  .secondary        = { 0.929f, 0.878f, 1.0f   },  // #EDE0FF (primaryContainer light)
  .onSecondary      = { 0.0f,   0.0f,   0.0f   },  // #000000
  .tertiary         = { 0.910f, 0.522f, 0.290f },  // #E8854A
  .onTertiary       = { 0.0f,   0.0f,   0.0f   },  // #000000
  .error            = { 0.953f, 0.655f, 0.604f },  // #F3A79A (unchanged light)
  .onError          = { 0.0f,   0.0f,   0.0f   },  // #000000
  .surface          = { 0.965f, 0.941f, 1.0f   },  // #F6F0FF
  .onSurface        = { 0.0f,   0.0f,   0.0f   },  // #000000
  .surfaceVariant   = { 0.929f, 0.894f, 1.0f   },  // #EDE4FF (surfaceContainer)
  .onSurfaceVariant = { 0.0f,   0.0f,   0.0f   },  // #000000 (at 55%)
  .outline          = { 0.478f, 0.416f, 0.588f },  // #7A6A96
  .shadow           = { 0.031f, 0.020f, 0.063f },  // #080510
  .hover            = { 0.929f, 0.878f, 1.0f   },  // #EDE0FF (primaryContainer)
  .onHover          = { 0.0f,   0.0f,   0.0f   },  // #000000
};

} // namespace eh::ui
