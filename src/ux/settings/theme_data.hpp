#pragma once
#include <algorithm>
#include <array>
#include <vector>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace eh::settings::theme {

// 16-color palette matching the FallbackPalette structure.
// Stored as normalized float triples (0..1) for runtime and hex strings for TOML.
struct ThemePalette {
  float primaryR = 0.769f, primaryG = 0.659f, primaryB = 0.941f;
  float onPrimaryR = 1.0f, onPrimaryG = 1.0f, onPrimaryB = 1.0f;
  float secondaryR = 0.239f, secondaryG = 0.125f, secondaryB = 0.439f;
  float onSecondaryR = 1.0f, onSecondaryG = 1.0f, onSecondaryB = 1.0f;
  float tertiaryR = 0.910f, tertiaryG = 0.522f, tertiaryB = 0.290f;
  float onTertiaryR = 1.0f, onTertiaryG = 1.0f, onTertiaryB = 1.0f;
  float errorR = 0.910f, errorG = 0.416f, errorB = 0.353f;
  float onErrorR = 1.0f, onErrorG = 1.0f, onErrorB = 1.0f;
  float surfaceR = 0.102f, surfaceG = 0.075f, surfaceB = 0.188f;
  float onSurfaceR = 1.0f, onSurfaceG = 1.0f, onSurfaceB = 1.0f;
  float surfaceVariantR = 0.133f, surfaceVariantG = 0.102f, surfaceVariantB = 0.227f;
  float onSurfaceVariantR = 1.0f, onSurfaceVariantG = 1.0f, onSurfaceVariantB = 1.0f;
  float outlineR = 0.478f, outlineG = 0.416f, outlineB = 0.588f;
  float shadowR = 0.031f, shadowG = 0.020f, shadowB = 0.063f;
  float hoverR = 0.212f, hoverG = 0.165f, hoverB = 0.337f;
  float onHoverR = 1.0f, onHoverG = 1.0f, onHoverB = 1.0f;
};

struct ThemePreset {
  std::string_view name;
  std::string_view source;   // "catppuccin" | "google" | "gruvbox"
  std::string_view variant;  // e.g. "mocha", "dark", "light"
  ThemePalette palette;
};

// Catppuccin.
inline constexpr ThemePalette kCatppuccinLatte{
  .primaryR = 0.298f, .primaryG = 0.310f, .primaryB = 0.412f,
  .onPrimaryR = 1.0f, .onPrimaryG = 1.0f, .onPrimaryB = 1.0f,
  .secondaryR = 0.584f, .secondaryG = 0.420f, .secondaryB = 0.541f,
  .onSecondaryR = 1.0f, .onSecondaryG = 1.0f, .onSecondaryB = 1.0f,
  .tertiaryR = 0.424f, .tertiaryG = 0.522f, .tertiaryB = 0.459f,
  .onTertiaryR = 1.0f, .onTertiaryG = 1.0f, .onTertiaryB = 1.0f,
  .errorR = 0.820f, .errorG = 0.251f, .errorB = 0.255f,
  .onErrorR = 1.0f, .onErrorG = 1.0f, .onErrorB = 1.0f,
  .surfaceR = 0.937f, .surfaceG = 0.945f, .surfaceB = 0.961f,
  .onSurfaceR = 0.298f, .onSurfaceG = 0.310f, .onSurfaceB = 0.412f,
  .surfaceVariantR = 0.882f, .surfaceVariantG = 0.890f, .surfaceVariantB = 0.922f,
  .onSurfaceVariantR = 0.388f, .onSurfaceVariantG = 0.404f, .onSurfaceVariantB = 0.490f,
  .outlineR = 0.584f, .outlineG = 0.584f, .outlineB = 0.584f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.820f, .hoverG = 0.839f, .hoverB = 0.875f,
  .onHoverR = 0.298f, .onHoverG = 0.310f, .onHoverB = 0.412f,
};

inline constexpr ThemePalette kCatppuccinFrappe{
  .primaryR = 0.776f, .primaryG = 0.816f, .primaryB = 0.961f,
  .onPrimaryR = 0.169f, .onPrimaryG = 0.180f, .onPrimaryB = 0.247f,
  .secondaryR = 0.737f, .secondaryG = 0.616f, .secondaryB = 0.694f,
  .onSecondaryR = 0.169f, .onSecondaryG = 0.180f, .onSecondaryB = 0.247f,
  .tertiaryR = 0.624f, .tertiaryG = 0.745f, .tertiaryB = 0.655f,
  .onTertiaryR = 0.169f, .onTertiaryG = 0.180f, .onTertiaryB = 0.247f,
  .errorR = 0.914f, .errorG = 0.365f, .errorB = 0.404f,
  .onErrorR = 0.169f, .onErrorG = 0.180f, .onErrorB = 0.247f,
  .surfaceR = 0.188f, .surfaceG = 0.204f, .surfaceB = 0.275f,
  .onSurfaceR = 0.776f, .onSurfaceG = 0.816f, .onSurfaceB = 0.961f,
  .surfaceVariantR = 0.212f, .surfaceVariantG = 0.231f, .surfaceVariantB = 0.306f,
  .onSurfaceVariantR = 0.639f, .onSurfaceVariantG = 0.651f, .onSurfaceVariantB = 0.722f,
  .outlineR = 0.502f, .outlineG = 0.514f, .outlineB = 0.584f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.239f, .hoverG = 0.263f, .hoverB = 0.341f,
  .onHoverR = 0.776f, .onHoverG = 0.816f, .onHoverB = 0.961f,
};

inline constexpr ThemePalette kCatppuccinMacchiato{
  .primaryR = 0.792f, .primaryG = 0.827f, .primaryB = 0.961f,
  .onPrimaryR = 0.129f, .onPrimaryG = 0.141f, .onPrimaryB = 0.212f,
  .secondaryR = 0.741f, .secondaryG = 0.620f, .secondaryB = 0.698f,
  .onSecondaryR = 0.129f, .onSecondaryG = 0.141f, .onSecondaryB = 0.212f,
  .tertiaryR = 0.631f, .tertiaryG = 0.753f, .tertiaryB = 0.663f,
  .onTertiaryR = 0.129f, .onTertiaryG = 0.141f, .onTertiaryB = 0.212f,
  .errorR = 0.918f, .errorG = 0.345f, .errorB = 0.384f,
  .onErrorR = 0.129f, .onErrorG = 0.141f, .onErrorB = 0.212f,
  .surfaceR = 0.141f, .surfaceG = 0.153f, .surfaceB = 0.227f,
  .onSurfaceR = 0.792f, .onSurfaceG = 0.827f, .onSurfaceB = 0.961f,
  .surfaceVariantR = 0.161f, .surfaceVariantG = 0.173f, .surfaceVariantB = 0.255f,
  .onSurfaceVariantR = 0.608f, .onSurfaceVariantG = 0.624f, .onSurfaceVariantB = 0.702f,
  .outlineR = 0.471f, .outlineG = 0.482f, .outlineB = 0.557f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.184f, .hoverG = 0.200f, .hoverB = 0.278f,
  .onHoverR = 0.792f, .onHoverG = 0.827f, .onHoverB = 0.961f,
};

inline constexpr ThemePalette kCatppuccinMocha{
  .primaryR = 0.804f, .primaryG = 0.839f, .primaryB = 0.961f,
  .onPrimaryR = 0.067f, .onPrimaryG = 0.067f, .onPrimaryB = 0.133f,
  .secondaryR = 0.741f, .secondaryG = 0.620f, .secondaryB = 0.698f,
  .onSecondaryR = 0.067f, .onSecondaryG = 0.067f, .onSecondaryB = 0.133f,
  .tertiaryR = 0.631f, .tertiaryG = 0.753f, .tertiaryB = 0.663f,
  .onTertiaryR = 0.067f, .onTertiaryG = 0.067f, .onTertiaryB = 0.133f,
  .errorR = 0.922f, .errorG = 0.325f, .errorB = 0.365f,
  .onErrorR = 0.067f, .onErrorG = 0.067f, .onErrorB = 0.133f,
  .surfaceR = 0.118f, .surfaceG = 0.118f, .surfaceB = 0.180f,
  .onSurfaceR = 0.804f, .onSurfaceG = 0.839f, .onSurfaceB = 0.961f,
  .surfaceVariantR = 0.141f, .surfaceVariantG = 0.141f, .surfaceVariantB = 0.212f,
  .onSurfaceVariantR = 0.573f, .onSurfaceVariantG = 0.588f, .onSurfaceVariantB = 0.671f,
  .outlineR = 0.439f, .outlineG = 0.455f, .outlineB = 0.533f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.157f, .hoverG = 0.161f, .hoverB = 0.231f,
  .onHoverR = 0.804f, .onHoverG = 0.839f, .onHoverB = 0.961f,
};

// Google Material.
inline constexpr ThemePalette kGoogleMaterialLight{
  .primaryR = 0.404f, .primaryG = 0.227f, .primaryB = 0.718f,
  .onPrimaryR = 1.0f, .onPrimaryG = 1.0f, .onPrimaryB = 1.0f,
  .secondaryR = 0.757f, .secondaryG = 0.714f, .secondaryB = 0.808f,
  .onSecondaryR = 0.114f, .onSecondaryG = 0.106f, .onSecondaryB = 0.125f,
  .tertiaryR = 0.463f, .tertiaryG = 0.451f, .tertiaryB = 0.882f,
  .onTertiaryR = 1.0f, .onTertiaryG = 1.0f, .onTertiaryB = 1.0f,
  .errorR = 0.918f, .errorG = 0.322f, .errorB = 0.322f,
  .onErrorR = 1.0f, .onErrorG = 1.0f, .onErrorB = 1.0f,
  .surfaceR = 0.996f, .surfaceG = 0.969f, .surfaceB = 1.0f,
  .onSurfaceR = 0.114f, .onSurfaceG = 0.106f, .onSurfaceB = 0.125f,
  .surfaceVariantR = 0.929f, .surfaceVariantG = 0.886f, .surfaceVariantB = 0.949f,
  .onSurfaceVariantR = 0.494f, .onSurfaceVariantG = 0.463f, .onSurfaceVariantB = 0.518f,
  .outlineR = 0.737f, .outlineG = 0.702f, .outlineB = 0.761f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.929f, .hoverG = 0.886f, .hoverB = 0.949f,
  .onHoverR = 0.114f, .onHoverG = 0.106f, .onHoverB = 0.125f,
};

inline constexpr ThemePalette kGoogleMaterialDark{
  .primaryR = 0.675f, .primaryG = 0.522f, .primaryB = 1.0f,
  .onPrimaryR = 0.122f, .onPrimaryG = 0.0f, .onPrimaryB = 0.267f,
  .secondaryR = 0.761f, .secondaryG = 0.718f, .secondaryB = 0.812f,
  .onSecondaryR = 0.114f, .onSecondaryG = 0.106f, .onSecondaryB = 0.125f,
  .tertiaryR = 0.522f, .tertiaryG = 0.506f, .tertiaryB = 0.890f,
  .onTertiaryR = 0.114f, .onTertiaryG = 0.106f, .onTertiaryB = 0.125f,
  .errorR = 1.0f, .errorG = 0.443f, .errorB = 0.443f,
  .onErrorR = 0.114f, .onErrorG = 0.106f, .onErrorB = 0.125f,
  .surfaceR = 0.110f, .surfaceG = 0.106f, .surfaceB = 0.122f,
  .onSurfaceR = 0.929f, .onSurfaceG = 0.914f, .onSurfaceB = 0.953f,
  .surfaceVariantR = 0.286f, .surfaceVariantG = 0.275f, .surfaceVariantB = 0.306f,
  .onSurfaceVariantR = 0.780f, .onSurfaceVariantG = 0.757f, .onSurfaceVariantB = 0.804f,
  .outlineR = 0.494f, .outlineG = 0.475f, .outlineB = 0.522f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.184f, .hoverG = 0.176f, .hoverB = 0.200f,
  .onHoverR = 0.929f, .onHoverG = 0.914f, .onHoverB = 0.953f,
};

// Gruvbox.
inline constexpr ThemePalette kGruvboxDark{
  .primaryR = 0.855f, .primaryG = 0.553f, .primaryB = 0.239f,
  .onPrimaryR = 0.157f, .onPrimaryG = 0.157f, .onPrimaryB = 0.157f,
  .secondaryR = 0.584f, .secondaryG = 0.584f, .secondaryB = 0.424f,
  .onSecondaryR = 0.157f, .onSecondaryG = 0.157f, .onSecondaryB = 0.157f,
  .tertiaryR = 0.459f, .tertiaryG = 0.631f, .tertiaryB = 0.459f,
  .onTertiaryR = 0.157f, .onTertiaryG = 0.157f, .onTertiaryB = 0.157f,
  .errorR = 0.792f, .errorG = 0.271f, .errorB = 0.271f,
  .onErrorR = 0.157f, .onErrorG = 0.157f, .onErrorB = 0.157f,
  .surfaceR = 0.157f, .surfaceG = 0.157f, .surfaceB = 0.157f,
  .onSurfaceR = 0.922f, .onSurfaceG = 0.859f, .onSurfaceB = 0.698f,
  .surfaceVariantR = 0.200f, .surfaceVariantG = 0.200f, .surfaceVariantB = 0.200f,
  .onSurfaceVariantR = 0.663f, .onSurfaceVariantG = 0.604f, .onSurfaceVariantB = 0.463f,
  .outlineR = 0.365f, .outlineG = 0.365f, .outlineB = 0.365f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.235f, .hoverG = 0.235f, .hoverB = 0.235f,
  .onHoverR = 0.922f, .onHoverG = 0.859f, .onHoverB = 0.698f,
};

inline constexpr ThemePalette kGruvboxLight{
  .primaryR = 0.859f, .primaryG = 0.573f, .primaryB = 0.239f,
  .onPrimaryR = 0.157f, .onPrimaryG = 0.157f, .onPrimaryB = 0.157f,
  .secondaryR = 0.584f, .secondaryG = 0.584f, .secondaryB = 0.424f,
  .onSecondaryR = 0.922f, .onSecondaryG = 0.859f, .onSecondaryB = 0.698f,
  .tertiaryR = 0.471f, .tertiaryG = 0.635f, .tertiaryB = 0.471f,
  .onTertiaryR = 0.157f, .onTertiaryG = 0.157f, .onTertiaryB = 0.157f,
  .errorR = 0.792f, .errorG = 0.271f, .errorB = 0.271f,
  .onErrorR = 0.922f, .onErrorG = 0.859f, .onErrorB = 0.698f,
  .surfaceR = 0.984f, .surfaceG = 0.949f, .surfaceB = 0.780f,
  .onSurfaceR = 0.235f, .onSurfaceG = 0.220f, .onSurfaceB = 0.212f,
  .surfaceVariantR = 0.937f, .surfaceVariantG = 0.878f, .surfaceVariantB = 0.702f,
  .onSurfaceVariantR = 0.486f, .onSurfaceVariantG = 0.459f, .onSurfaceVariantB = 0.412f,
  .outlineR = 0.706f, .outlineG = 0.667f, .outlineB = 0.545f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.898f, .hoverG = 0.835f, .hoverB = 0.639f,
  .onHoverR = 0.235f, .onHoverG = 0.220f, .onHoverB = 0.212f,
};

// Rosé Pine.
// Based on the official Rosé Pine palette (https://rosepinetheme.com/)
inline constexpr ThemePalette kRosePine{
  .primaryR = 0.769f, .primaryG = 0.655f, .primaryB = 0.906f,
  .onPrimaryR = 0.122f, .onPrimaryG = 0.114f, .onPrimaryB = 0.180f,
  .secondaryR = 0.922f, .secondaryG = 0.737f, .secondaryB = 0.729f,
  .onSecondaryR = 0.122f, .onSecondaryG = 0.114f, .onSecondaryB = 0.180f,
  .tertiaryR = 0.612f, .tertiaryG = 0.812f, .tertiaryB = 0.847f,
  .onTertiaryR = 0.122f, .onTertiaryG = 0.114f, .onTertiaryB = 0.180f,
  .errorR = 0.922f, .errorG = 0.435f, .errorB = 0.573f,
  .onErrorR = 0.098f, .onErrorG = 0.090f, .onErrorB = 0.141f,
  .surfaceR = 0.098f, .surfaceG = 0.090f, .surfaceB = 0.141f,
  .onSurfaceR = 0.878f, .onSurfaceG = 0.871f, .onSurfaceB = 0.957f,
  .surfaceVariantR = 0.122f, .surfaceVariantG = 0.114f, .surfaceVariantB = 0.180f,
  .onSurfaceVariantR = 0.565f, .onSurfaceVariantG = 0.549f, .onSurfaceVariantB = 0.667f,
  .outlineR = 0.431f, .outlineG = 0.416f, .outlineB = 0.525f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.322f, .hoverG = 0.310f, .hoverB = 0.404f,
  .onHoverR = 0.878f, .onHoverG = 0.871f, .onHoverB = 0.957f,
};

inline constexpr ThemePalette kRosePineMoon{
  .primaryR = 0.769f, .primaryG = 0.655f, .primaryB = 0.906f,
  .onPrimaryR = 0.165f, .onPrimaryG = 0.153f, .onPrimaryB = 0.247f,
  .secondaryR = 0.918f, .secondaryG = 0.604f, .secondaryB = 0.592f,
  .onSecondaryR = 0.165f, .onSecondaryG = 0.153f, .onSecondaryB = 0.247f,
  .tertiaryR = 0.612f, .tertiaryG = 0.812f, .tertiaryB = 0.847f,
  .onTertiaryR = 0.165f, .onTertiaryG = 0.153f, .onTertiaryB = 0.247f,
  .errorR = 0.922f, .errorG = 0.435f, .errorB = 0.573f,
  .onErrorR = 0.137f, .onErrorG = 0.129f, .onErrorB = 0.212f,
  .surfaceR = 0.137f, .surfaceG = 0.129f, .surfaceB = 0.212f,
  .onSurfaceR = 0.878f, .onSurfaceG = 0.871f, .onSurfaceB = 0.957f,
  .surfaceVariantR = 0.165f, .surfaceVariantG = 0.153f, .surfaceVariantB = 0.247f,
  .onSurfaceVariantR = 0.565f, .onSurfaceVariantG = 0.549f, .onSurfaceVariantB = 0.667f,
  .outlineR = 0.431f, .outlineG = 0.416f, .outlineB = 0.525f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.337f, .hoverG = 0.322f, .hoverB = 0.431f,
  .onHoverR = 0.878f, .onHoverG = 0.871f, .onHoverB = 0.957f,
};

inline constexpr ThemePalette kRosePineDawn{
  .primaryR = 0.565f, .primaryG = 0.478f, .primaryB = 0.663f,
  .onPrimaryR = 0.980f, .onPrimaryG = 0.957f, .onPrimaryB = 0.929f,
  .secondaryR = 0.843f, .secondaryG = 0.510f, .secondaryB = 0.494f,
  .onSecondaryR = 0.980f, .onSecondaryG = 0.957f, .onSecondaryB = 0.929f,
  .tertiaryR = 0.337f, .tertiaryG = 0.580f, .tertiaryB = 0.624f,
  .onTertiaryR = 0.980f, .onTertiaryG = 0.957f, .onTertiaryB = 0.929f,
  .errorR = 0.706f, .errorG = 0.388f, .errorB = 0.478f,
  .onErrorR = 0.980f, .onErrorG = 0.957f, .onErrorB = 0.929f,
  .surfaceR = 0.980f, .surfaceG = 0.957f, .surfaceB = 0.929f,
  .onSurfaceR = 0.333f, .onSurfaceG = 0.318f, .onSurfaceB = 0.412f,
  .surfaceVariantR = 1.0f, .surfaceVariantG = 0.980f, .surfaceVariantB = 0.953f,
  .onSurfaceVariantR = 0.475f, .onSurfaceVariantG = 0.459f, .onSurfaceVariantB = 0.576f,
  .outlineR = 0.596f, .outlineG = 0.576f, .outlineB = 0.647f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.875f, .hoverG = 0.855f, .hoverB = 0.851f,
  .onHoverR = 0.333f, .onHoverG = 0.318f, .onHoverB = 0.412f,
};

// Google Brand.
// Based on Google's official brand/product colors
inline constexpr ThemePalette kGoogleBrandDark{
  .primaryR = 0.259f, .primaryG = 0.522f, .primaryB = 0.957f,
  .onPrimaryR = 1.0f, .onPrimaryG = 1.0f, .onPrimaryB = 1.0f,
  .secondaryR = 0.204f, .secondaryG = 0.659f, .secondaryB = 0.325f,
  .onSecondaryR = 1.0f, .onSecondaryG = 1.0f, .onSecondaryB = 1.0f,
  .tertiaryR = 0.984f, .tertiaryG = 0.737f, .tertiaryB = 0.016f,
  .onTertiaryR = 0.125f, .onTertiaryG = 0.129f, .onTertiaryB = 0.141f,
  .errorR = 0.918f, .errorG = 0.263f, .errorB = 0.208f,
  .onErrorR = 1.0f, .onErrorG = 1.0f, .onErrorB = 1.0f,
  .surfaceR = 0.125f, .surfaceG = 0.129f, .surfaceB = 0.141f,
  .onSurfaceR = 0.910f, .onSurfaceG = 0.918f, .onSurfaceB = 0.929f,
  .surfaceVariantR = 0.188f, .surfaceVariantG = 0.192f, .surfaceVariantB = 0.204f,
  .onSurfaceVariantR = 0.604f, .onSurfaceVariantG = 0.627f, .onSurfaceVariantB = 0.651f,
  .outlineR = 0.373f, .outlineG = 0.388f, .outlineB = 0.408f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.224f, .hoverG = 0.231f, .hoverB = 0.243f,
  .onHoverR = 0.910f, .onHoverG = 0.918f, .onHoverB = 0.929f,
};

inline constexpr ThemePalette kGoogleBrandLight{
  .primaryR = 0.102f, .primaryG = 0.451f, .primaryB = 0.910f,
  .onPrimaryR = 1.0f, .onPrimaryG = 1.0f, .onPrimaryB = 1.0f,
  .secondaryR = 0.094f, .secondaryG = 0.502f, .secondaryB = 0.220f,
  .onSecondaryR = 1.0f, .onSecondaryG = 1.0f, .onSecondaryB = 1.0f,
  .tertiaryR = 0.976f, .tertiaryG = 0.671f, .tertiaryB = 0.0f,
  .onTertiaryR = 0.125f, .onTertiaryG = 0.129f, .onTertiaryB = 0.141f,
  .errorR = 0.851f, .errorG = 0.188f, .errorB = 0.145f,
  .onErrorR = 1.0f, .onErrorG = 1.0f, .onErrorB = 1.0f,
  .surfaceR = 1.0f, .surfaceG = 1.0f, .surfaceB = 1.0f,
  .onSurfaceR = 0.125f, .onSurfaceG = 0.129f, .onSurfaceB = 0.141f,
  .surfaceVariantR = 0.945f, .surfaceVariantG = 0.953f, .surfaceVariantB = 0.957f,
  .onSurfaceVariantR = 0.373f, .onSurfaceVariantG = 0.388f, .onSurfaceVariantB = 0.408f,
  .outlineR = 0.855f, .outlineG = 0.863f, .outlineB = 0.878f,
  .shadowR = 0.0f, .shadowG = 0.0f, .shadowB = 0.0f,
  .hoverR = 0.910f, .hoverG = 0.918f, .hoverB = 0.929f,
  .onHoverR = 0.125f, .onHoverG = 0.129f, .onHoverB = 0.141f,
};

// Compact seed for mass-generated palettes.
// Defines only 5 key colors + a dark flag; seed_to_palette() fills the rest.
struct PaletteSeed {
  std::string_view name;
  std::string_view source;
  std::string_view variant;
  float pR, pG, pB;   // primary
  float sR, sG, sB;   // secondary
  float tR, tG, tB;   // tertiary
  float suR, suG, suB; // surface
  float oR, oG, oB;   // outline
  bool dark;
};

inline ThemePalette seed_to_palette(const PaletteSeed& s) {
  ThemePalette p{};
  p.primaryR = s.pR; p.primaryG = s.pG; p.primaryB = s.pB;
  p.secondaryR = s.sR; p.secondaryG = s.sG; p.secondaryB = s.sB;
  p.tertiaryR = s.tR; p.tertiaryG = s.tG; p.tertiaryB = s.tB;
  p.surfaceR = s.suR; p.surfaceG = s.suG; p.surfaceB = s.suB;
  p.outlineR = s.oR; p.outlineG = s.oG; p.outlineB = s.oB;
  if (s.dark) {
    p.onPrimaryR = 0.9f; p.onPrimaryG = 0.9f; p.onPrimaryB = 0.95f;
    p.onSecondaryR = 0.9f; p.onSecondaryG = 0.9f; p.onSecondaryB = 0.95f;
    p.onTertiaryR = 0.9f; p.onTertiaryG = 0.9f; p.onTertiaryB = 0.95f;
    p.onErrorR = 1.0f; p.onErrorG = 1.0f; p.onErrorB = 1.0f;
    p.onSurfaceR = 0.85f; p.onSurfaceG = 0.85f; p.onSurfaceB = 0.90f;
    p.surfaceVariantR = std::min(1.0f, s.suR * 1.20f);
    p.surfaceVariantG = std::min(1.0f, s.suG * 1.20f);
    p.surfaceVariantB = std::min(1.0f, s.suB * 1.20f);
    p.onSurfaceVariantR = 0.60f; p.onSurfaceVariantG = 0.60f; p.onSurfaceVariantB = 0.65f;
    p.errorR = 0.92f; p.errorG = 0.27f; p.errorB = 0.27f;
    p.shadowR = 0.0f; p.shadowG = 0.0f; p.shadowB = 0.0f;
    p.hoverR = std::min(1.0f, s.suR * 1.35f);
    p.hoverG = std::min(1.0f, s.suG * 1.35f);
    p.hoverB = std::min(1.0f, s.suB * 1.35f);
    p.onHoverR = 0.85f; p.onHoverG = 0.85f; p.onHoverB = 0.90f;
  } else {
    p.onPrimaryR = 1.0f; p.onPrimaryG = 1.0f; p.onPrimaryB = 1.0f;
    p.onSecondaryR = 1.0f; p.onSecondaryG = 1.0f; p.onSecondaryB = 1.0f;
    p.onTertiaryR = 1.0f; p.onTertiaryG = 1.0f; p.onTertiaryB = 1.0f;
    p.onErrorR = 1.0f; p.onErrorG = 1.0f; p.onErrorB = 1.0f;
    p.onSurfaceR = 0.15f; p.onSurfaceG = 0.15f; p.onSurfaceB = 0.17f;
    p.surfaceVariantR = std::max(0.0f, s.suR * 0.90f);
    p.surfaceVariantG = std::max(0.0f, s.suG * 0.90f);
    p.surfaceVariantB = std::max(0.0f, s.suB * 0.90f);
    p.onSurfaceVariantR = 0.45f; p.onSurfaceVariantG = 0.45f; p.onSurfaceVariantB = 0.50f;
    p.errorR = 0.80f; p.errorG = 0.18f; p.errorB = 0.18f;
    p.shadowR = 0.0f; p.shadowG = 0.0f; p.shadowB = 0.0f;
    p.hoverR = std::max(0.0f, s.suR * 0.80f);
    p.hoverG = std::max(0.0f, s.suG * 0.80f);
    p.hoverB = std::max(0.0f, s.suB * 0.80f);
    p.onHoverR = 0.15f; p.onHoverG = 0.15f; p.onHoverB = 0.17f;
  }
  return p;
}

// Lookup.
inline constexpr std::array<ThemePreset, 13> kBuiltInPresets{{
  { "Catppuccin Latte",     "catppuccin", "latte",   kCatppuccinLatte     },
  { "Catppuccin Frappé",    "catppuccin", "frappe",  kCatppuccinFrappe    },
  { "Catppuccin Macchiato", "catppuccin", "macchiato", kCatppuccinMacchiato },
  { "Catppuccin Mocha",     "catppuccin", "mocha",   kCatppuccinMocha     },
  { "Google Material Light", "google",    "light",   kGoogleMaterialLight },
  { "Google Material Dark",  "google",    "dark",    kGoogleMaterialDark   },
  { "Gruvbox Dark",          "gruvbox",   "dark",    kGruvboxDark          },
  { "Gruvbox Light",         "gruvbox",   "light",   kGruvboxLight         },
  { "Rosé Pine",             "rose-pine", "dark",    kRosePine             },
  { "Rosé Pine Moon",        "rose-pine", "moon",    kRosePineMoon         },
  { "Rosé Pine Dawn",        "rose-pine", "dawn",    kRosePineDawn         },
  { "Google Brand Dark",     "google",    "brand-dark",  kGoogleBrandDark   },
  { "Google Brand Light",    "google",    "brand-light", kGoogleBrandLight  },
}};

inline constexpr int kBuiltInPresetCount = static_cast<int>(kBuiltInPresets.size());

// Seed-generated palettes (compact one-liners for mass addition)
// Primary | Secondary | Tertiary | Surface | Outline | Dark?
#define SEED(name, src, var, pr, pg, pb, sr, sg, sb, tr, tg, tb, sur, sug, sub, or_, og, ob, dk) \
  PaletteSeed{name, src, var, pr, pg, pb, sr, sg, sb, tr, tg, tb, sur, sug, sub, or_, og, ob, dk}

// Mass-generated palettes from the Ptyxis & Dracula specs.
inline const std::vector<ThemePreset>& all_builtin_presets() {
  static const auto all = [] {
    std::vector<ThemePreset> v;
    for (const auto& p : kBuiltInPresets) v.push_back(p);

    auto add = [&](const PaletteSeed& s) {
      v.push_back({s.name, s.source, s.variant, seed_to_palette(s)});
    };

    // Dracula (classic dark)
    add(SEED("Dracula", "dracula", "dark",
      0.741f, 0.576f, 0.976f,  // primary purple #BD93F9
      1.000f, 0.475f, 0.776f,  // secondary pink #FF79C6
      0.314f, 0.980f, 0.482f,  // tertiary green #50FA7B
      0.157f, 0.165f, 0.212f,  // surface #282A36
      0.384f, 0.447f, 0.643f,  // outline #6272A4
      true));

    // Alucard (Dracula light)
    add(SEED("Alucard", "dracula", "light",
      0.392f, 0.290f, 0.788f,  // primary purple #644AC9
      0.639f, 0.078f, 0.302f,  // secondary pink #A3144D
      0.078f, 0.416f, 0.588f,  // tertiary cyan #036A96
      0.984f, 0.984f, 0.922f,  // surface #FFFBEB
      0.765f, 0.773f, 0.871f,  // outline #CFCFDE
      false));

    // Nord Dark
    add(SEED("Nord Dark", "nord", "dark",
      0.533f, 0.753f, 0.816f,  // primary frost #88C0D0
      0.706f, 0.557f, 0.678f,  // secondary purple #B48EAD
      0.639f, 0.745f, 0.549f,  // tertiary green #A3BE8C
      0.180f, 0.204f, 0.251f,  // surface #2E3440
      0.298f, 0.337f, 0.416f,  // outline #4C566A
      true));

    // Tokyo Night
    add(SEED("Tokyo Night", "tokyo-night", "dark",
      0.478f, 0.635f, 0.969f,  // primary blue #7AA2F7
      0.733f, 0.604f, 0.969f,  // secondary purple #BB9AF7
      0.620f, 0.808f, 0.416f,  // tertiary green #9ECE6A
      0.102f, 0.106f, 0.149f,  // surface #1A1B26
      0.337f, 0.373f, 0.537f,  // outline #565F89
      true));

    // Monokai Dark
    add(SEED("Monokai Dark", "monokai", "dark",
      0.651f, 0.886f, 0.180f,  // primary green #A6E22E
      0.992f, 0.592f, 0.122f,  // secondary orange #FD971F
      0.400f, 0.851f, 0.937f,  // tertiary cyan #66D9EF
      0.153f, 0.157f, 0.133f,  // surface #272822
      0.459f, 0.443f, 0.369f,  // outline #75715E
      true));

    // Solarized Dark
    add(SEED("Solarized Dark", "solarized", "dark",
      0.149f, 0.545f, 0.824f,  // primary blue #268BD2
      0.863f, 0.196f, 0.184f,  // secondary red #DC322F
      0.522f, 0.600f, 0.000f,  // tertiary green #859900
      0.000f, 0.169f, 0.212f,  // surface #002B36
      0.345f, 0.431f, 0.459f,  // outline #586E75
      true));

    // Everforest Dark
    add(SEED("Everforest Dark", "everforest", "dark",
      0.655f, 0.753f, 0.502f,  // primary green #A7C080
      0.827f, 0.776f, 0.667f,  // secondary beige #D3C6AA
      0.498f, 0.733f, 0.702f,  // tertiary teal #7FBBB3
      0.176f, 0.208f, 0.231f,  // surface #2D353B
      0.278f, 0.322f, 0.345f,  // outline #475258
      true));

    // Kanagawa
    add(SEED("Kanagawa", "kanagawa", "dark",
      0.498f, 0.706f, 0.792f,  // primary wave blue #7FB4CA
      0.753f, 0.639f, 0.431f,  // secondary gold #C0A36E
      0.596f, 0.733f, 0.424f,  // tertiary spring green #98BB6C
      0.122f, 0.122f, 0.157f,  // surface #1F1F28
      0.329f, 0.329f, 0.427f,  // outline #54546D
      true));

    // Ayu Mirage
    add(SEED("Ayu Mirage", "ayu", "mirage",
      0.451f, 0.816f, 1.000f,  // primary blue #73D0FF
      0.831f, 0.749f, 1.000f,  // secondary purple #D4BFFF
      0.722f, 0.800f, 0.322f,  // tertiary yellow-green #B8CC52
      0.118f, 0.141f, 0.188f,  // surface #1E2430
      0.361f, 0.404f, 0.451f,  // outline #5C6773
      true));

    // Atom One Dark
    add(SEED("Atom One Dark", "atom", "dark",
      0.380f, 0.686f, 0.937f,  // primary blue #61AFEF
      0.898f, 0.753f, 0.482f,  // secondary gold #E5C07B
      0.596f, 0.765f, 0.475f,  // tertiary green #98C379
      0.157f, 0.173f, 0.204f,  // surface #282C34
      0.361f, 0.388f, 0.439f,  // outline #5C6370
      true));

    // Night Owl
    add(SEED("Night Owl", "night-owl", "dark",
      0.494f, 0.341f, 0.761f,  // primary purple #7E57C2
      0.129f, 0.780f, 0.659f,  // secondary teal #21C7A8
      1.000f, 0.922f, 0.584f,  // tertiary yellow #FFEB95
      0.004f, 0.086f, 0.153f,  // surface #011627
      0.294f, 0.392f, 0.475f,  // outline #4B6479
      true));

    // Catppuccin variants (mocha, macchiato, frappe, latte already in kBuiltInPresets)
    // Adding Catppuccin-specific accent-based variants:
    add(SEED("Catppuccin Sky", "catppuccin", "mocha-sky",
      0.553f, 0.851f, 0.902f,  // primary sky #89DCEB
      0.741f, 0.620f, 0.698f,  // secondary mauve #BD93F9-like
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface mocha
      0.439f, 0.455f, 0.533f,  // outline
      true));

    add(SEED("Catppuccin Peach", "catppuccin", "mocha-peach",
      0.957f, 0.663f, 0.424f,  // primary peach #FAB387
      0.741f, 0.620f, 0.698f,  // secondary
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface
      0.439f, 0.455f, 0.533f,  // outline
      true));

    add(SEED("Catppuccin Green", "catppuccin", "mocha-green",
      0.604f, 0.780f, 0.529f,  // primary green #A6E3A1
      0.741f, 0.620f, 0.698f,  // secondary
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface
      0.439f, 0.455f, 0.533f,  // outline
      true));

    add(SEED("Catppuccin Lavender", "catppuccin", "mocha-lavender",
      0.690f, 0.702f, 0.937f,  // primary lavender #B4BEFE
      0.741f, 0.620f, 0.698f,  // secondary
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface
      0.439f, 0.455f, 0.533f,  // outline
      true));

    add(SEED("Catppuccin Sapphire", "catppuccin", "mocha-sapphire",
      0.463f, 0.686f, 0.824f,  // primary sapphire #74C7EC
      0.741f, 0.620f, 0.698f,  // secondary
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface
      0.439f, 0.455f, 0.533f,  // outline
      true));

    add(SEED("Catppuccin Maroon", "catppuccin", "mocha-maroon",
      0.922f, 0.549f, 0.604f,  // primary maroon #EBA0AC
      0.741f, 0.620f, 0.698f,  // secondary
      0.737f, 0.616f, 0.694f,  // tertiary
      0.118f, 0.118f, 0.180f,  // surface
      0.439f, 0.455f, 0.533f,  // outline
      true));

    // Tokyo Night Storm
    add(SEED("Tokyo Night Storm", "tokyo-night", "storm",
      0.404f, 0.557f, 0.871f,  // primary blue
      0.671f, 0.557f, 0.871f,  // secondary purple
      0.584f, 0.729f, 0.388f,  // tertiary green
      0.114f, 0.122f, 0.161f,  // surface #1E202D
      0.333f, 0.365f, 0.525f,  // outline
      true));

    // Nord Light (will use Aurora colors on a lighter surface)
    add(SEED("Nord Light", "nord", "light",
      0.322f, 0.627f, 0.678f,  // primary frost
      0.584f, 0.439f, 0.557f,  // secondary purple
      0.537f, 0.651f, 0.435f,  // tertiary green
      0.933f, 0.937f, 0.957f,  // surface #EEF0F4
      0.659f, 0.690f, 0.749f,  // outline
      false));

    // Gruvbox Material Dark (Medium contrast)
    add(SEED("Gruvbox Material", "gruvbox", "material-dark",
      0.851f, 0.561f, 0.255f,  // primary orange
      0.608f, 0.620f, 0.447f,  // secondary green-gray
      0.545f, 0.404f, 0.353f,  // tertiary brown
      0.145f, 0.141f, 0.114f,  // surface #1D2021
      0.365f, 0.349f, 0.278f,  // outline
      true));

    // Sonokai
    add(SEED("Sonokai", "sonokai", "dark",
      0.729f, 0.773f, 0.886f,  // primary light blue-gray
      0.980f, 0.714f, 0.592f,  // secondary peach #FC9873-like
      0.600f, 0.804f, 0.604f,  // tertiary green
      0.145f, 0.141f, 0.145f,  // surface #2C2E34
      0.373f, 0.365f, 0.384f,  // outline
      true));

    // Andromeda
    add(SEED("Andromeda", "andromeda", "dark",
      0.929f, 0.569f, 0.631f,  // primary pink-red
      0.659f, 0.714f, 1.000f,  // secondary light blue
      0.412f, 0.788f, 0.667f,  // tertiary teal
      0.125f, 0.129f, 0.149f,  // surface #23262B
      0.384f, 0.384f, 0.412f,  // outline
      true));

    // All 266 Ptyxis terminal palettes (auto-converted to M3).
      add(SEED("3024", "google", "dark",
        0.004f, 0.627f, 0.894f,
        0.631f, 0.416f, 0.580f,
        0.710f, 0.894f, 0.957f,
        0.035f, 0.012f, 0.000f,
        0.361f, 0.345f, 0.333f,
        true));
      add(SEED("3024", "google", "light",
        0.004f, 0.627f, 0.894f,
        0.631f, 0.416f, 0.580f,
        0.710f, 0.894f, 0.957f,
        0.969f, 0.969f, 0.969f,
        0.361f, 0.345f, 0.333f,
        false));
      add(SEED("Aci", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.051f, 0.098f, 0.149f,
        0.259f, 0.259f, 0.259f,
        true));
      add(SEED("Aco", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.122f, 0.075f, 0.020f,
        0.278f, 0.278f, 0.278f,
        true));
      add(SEED("Adventure Time", "google", "dark",
        0.059f, 0.290f, 0.776f,
        0.620f, 0.310f, 0.863f,
        0.502f, 0.843f, 1.000f,
        0.071f, 0.071f, 0.071f,
        0.318f, 0.318f, 0.318f,
        true));
      add(SEED("Afterglow", "google", "dark",
        0.118f, 0.565f, 0.725f,
        0.522f, 0.239f, 0.655f,
        0.031f, 0.729f, 0.718f,
        0.157f, 0.157f, 0.157f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Alien Blood", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.314f, 0.980f, 0.482f,
        0.055f, 0.078f, 0.071f,
        0.267f, 0.373f, 0.322f,
        true));
      add(SEED("Apprentice", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.522f, 0.522f,
        0.000f, 0.749f, 0.749f,
        0.149f, 0.149f, 0.149f,
        0.294f, 0.294f, 0.294f,
        true));
      add(SEED("Argonaut", "google", "dark",
        0.000f, 0.239f, 0.851f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.012f, 0.012f, 0.012f,
        0.310f, 0.310f, 0.310f,
        true));
      add(SEED("Arthur", "google", "dark",
        0.118f, 0.565f, 0.725f,
        0.522f, 0.239f, 0.655f,
        0.118f, 0.565f, 0.725f,
        0.122f, 0.122f, 0.122f,
        0.576f, 0.576f, 0.576f,
        true));
      add(SEED("Atom", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.333f, 0.333f, 0.333f,
        true));
      add(SEED("Aura", "google", "dark",
        0.004f, 0.522f, 0.749f,
        0.431f, 0.522f, 0.522f,
        0.000f, 0.522f, 0.749f,
        0.122f, 0.118f, 0.078f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Ayu", "google", "dark",
        0.196f, 0.631f, 0.980f,
        0.878f, 0.510f, 0.980f,
        0.722f, 0.800f, 0.322f,
        0.090f, 0.114f, 0.157f,
        0.420f, 0.475f, 0.545f,
        true));
      add(SEED("Ayu", "google", "light",
        0.125f, 0.545f, 0.949f,
        0.553f, 0.318f, 0.620f,
        0.600f, 0.682f, 0.271f,
        0.973f, 0.973f, 0.973f,
        0.518f, 0.573f, 0.639f,
        false));
      add(SEED("Ayu Mirage", "google", "dark",
        0.196f, 0.631f, 0.980f,
        0.878f, 0.510f, 0.980f,
        0.722f, 0.800f, 0.322f,
        0.110f, 0.125f, 0.157f,
        0.420f, 0.475f, 0.545f,
        true));
      add(SEED("Azu", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.310f, 0.310f, 0.310f,
        true));
      add(SEED("Belafonte", "google", "dark",
        0.016f, 0.545f, 0.914f,
        0.620f, 0.310f, 0.863f,
        0.000f, 0.522f, 0.749f,
        0.122f, 0.118f, 0.078f,
        0.494f, 0.475f, 0.443f,
        true));
      add(SEED("Belafonte", "google", "light",
        0.016f, 0.545f, 0.914f,
        0.620f, 0.310f, 0.863f,
        0.000f, 0.522f, 0.749f,
        0.965f, 0.965f, 0.961f,
        0.494f, 0.475f, 0.443f,
        false));
      add(SEED("Bim", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.325f, 0.325f, 0.325f,
        true));
      add(SEED("Birds Of Paradise", "google", "dark",
        0.000f, 0.357f, 0.686f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.420f, 0.345f, 0.227f,
        true));
      add(SEED("Blazer", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Bluloco Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.965f, 0.965f, 0.961f,
        0.400f, 0.400f, 0.400f,
        false));
      add(SEED("Bluloco Zsh Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.965f, 0.965f, 0.961f,
        0.400f, 0.400f, 0.400f,
        false));
      add(SEED("Borland", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.239f, 0.239f, 0.239f,
        true));
      add(SEED("Breath", "google", "dark",
        0.012f, 0.522f, 0.757f,
        0.522f, 0.239f, 0.663f,
        0.039f, 0.749f, 0.749f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Breath", "google", "light",
        0.012f, 0.522f, 0.757f,
        0.522f, 0.239f, 0.663f,
        0.039f, 0.749f, 0.749f,
        0.969f, 0.969f, 0.969f,
        0.357f, 0.357f, 0.357f,
        false));
      add(SEED("Breath Silverfox", "google", "dark",
        0.102f, 0.557f, 0.784f,
        0.522f, 0.239f, 0.663f,
        0.039f, 0.749f, 0.749f,
        0.071f, 0.071f, 0.071f,
        0.443f, 0.443f, 0.443f,
        true));
      add(SEED("Breeze", "google", "dark",
        0.000f, 0.522f, 0.522f,
        0.522f, 0.522f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.337f, 0.337f, 0.337f,
        true));
      add(SEED("Broadcast", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Brogrammer", "google", "dark",
        0.118f, 0.565f, 0.725f,
        0.522f, 0.239f, 0.655f,
        0.000f, 0.749f, 0.749f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("C64", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Cai", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Catppuccin Frappé", "google", "dark",
        0.388f, 0.694f, 0.945f,
        0.741f, 0.620f, 0.698f,
        0.878f, 0.753f, 0.902f,
        0.165f, 0.165f, 0.216f,
        0.498f, 0.502f, 0.584f,
        true));
      add(SEED("Catppuccin Latte", "google", "light",
        0.388f, 0.694f, 0.945f,
        0.741f, 0.620f, 0.698f,
        0.878f, 0.753f, 0.902f,
        0.945f, 0.929f, 0.890f,
        0.576f, 0.580f, 0.671f,
        false));
      add(SEED("Catppuccin Macchiato", "google", "dark",
        0.388f, 0.694f, 0.945f,
        0.741f, 0.620f, 0.698f,
        0.878f, 0.753f, 0.902f,
        0.141f, 0.137f, 0.192f,
        0.486f, 0.494f, 0.580f,
        true));
      add(SEED("Catppuccin Mocha", "google", "dark",
        0.388f, 0.694f, 0.945f,
        0.741f, 0.620f, 0.698f,
        0.878f, 0.753f, 0.902f,
        0.118f, 0.118f, 0.180f,
        0.439f, 0.455f, 0.533f,
        true));
      add(SEED("Chalk", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.106f, 0.094f, 0.075f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Chalkboard", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.102f, 0.337f, 0.573f,
        0.169f, 0.137f, 0.067f,
        0.290f, 0.290f, 0.290f,
        true));
      add(SEED("Chameleon", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.157f, 0.157f, 0.071f,
        0.361f, 0.361f, 0.361f,
        true));
      add(SEED("Ciapre", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Clrs", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.294f, 0.294f, 0.294f,
        true));
      add(SEED("Cobalt 2", "google", "dark",
        0.000f, 0.435f, 1.000f,
        0.769f, 0.435f, 1.000f,
        0.000f, 0.522f, 0.749f,
        0.071f, 0.071f, 0.071f,
        0.337f, 0.337f, 0.337f,
        true));
      add(SEED("Cobalt Neon", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.333f, 0.333f, 0.333f,
        true));
      add(SEED("Colorcli", "google", "dark",
        0.000f, 0.435f, 0.749f,
        0.431f, 0.271f, 0.639f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Crayon Pony Fish", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Dark Pastel", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Darkside", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.122f, 0.122f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Dehydration", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.247f, 0.247f, 0.247f,
        true));
      add(SEED("Desert", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.294f, 0.294f, 0.294f,
        true));
      add(SEED("Dimmed Monokai", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.188f, 0.180f, 0.173f,
        0.424f, 0.420f, 0.416f,
        true));
      add(SEED("Dissonance", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Earthsong", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.239f, 0.655f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Elemental", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Elementary", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Elic", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.271f, 0.271f, 0.271f,
        true));
      add(SEED("Elio", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Espresso Libre", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.169f, 0.129f, 0.094f,
        0.388f, 0.388f, 0.388f,
        true));
      add(SEED("Fairyfloss", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.851f, 0.651f, 0.867f,
        0.275f, 0.224f, 0.353f,
        0.545f, 0.518f, 0.584f,
        true));
      add(SEED("Fairyfloss", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.851f, 0.651f, 0.867f,
        0.965f, 0.965f, 0.961f,
        0.545f, 0.518f, 0.584f,
        false));
      add(SEED("Fahrenheit", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Fideloper", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.388f, 0.388f, 0.388f,
        true));
      add(SEED("Fish", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.545f, 0.545f, 0.545f,
        true));
      add(SEED("Flat", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.122f, 0.129f,
        0.376f, 0.376f, 0.380f,
        true));
      add(SEED("Flatland", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.078f, 0.082f, 0.067f,
        0.404f, 0.404f, 0.404f,
        true));
      add(SEED("Floraverse", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.043f, 0.024f, 0.071f,
        0.361f, 0.361f, 0.361f,
        true));
      add(SEED("Forest", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Front End Delight", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.306f, 0.306f, 0.306f,
        true));
      add(SEED("Front End Galaxy", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Front End Happy", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.404f, 0.404f, 0.404f,
        true));
      add(SEED("Fruit Soda", "google", "dark",
        0.071f, 0.149f, 0.545f,
        0.871f, 0.404f, 0.557f,
        0.000f, 0.522f, 0.749f,
        0.118f, 0.118f, 0.118f,
        0.294f, 0.294f, 0.294f,
        true));
      add(SEED("Fruit Soda", "google", "light",
        0.071f, 0.149f, 0.545f,
        0.871f, 0.404f, 0.557f,
        0.000f, 0.522f, 0.749f,
        0.118f, 0.118f, 0.118f,
        0.294f, 0.294f, 0.294f,
        false));
      add(SEED("Gitpod", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.333f, 0.333f, 0.333f,
        true));
      add(SEED("Gooey", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Google Dark", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.361f, 0.361f, 0.361f,
        true));
      add(SEED("Google Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.522f, 0.522f, 0.522f,
        false));
      add(SEED("Grape", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Greybird", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.455f, 0.455f, 0.455f,
        true));
      add(SEED("Greybird", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.455f, 0.455f, 0.455f,
        false));
      add(SEED("Gruvbox", "google", "dark",
        0.027f, 0.506f, 0.773f,
        0.659f, 0.306f, 0.639f,
        0.184f, 0.651f, 0.573f,
        0.122f, 0.106f, 0.063f,
        0.365f, 0.361f, 0.318f,
        true));
      add(SEED("Gruvbox", "google", "light",
        0.027f, 0.506f, 0.773f,
        0.659f, 0.306f, 0.639f,
        0.184f, 0.651f, 0.573f,
        0.957f, 0.925f, 0.792f,
        0.604f, 0.580f, 0.510f,
        false));
      add(SEED("Hardcore", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.353f, 0.353f, 0.353f,
        true));
      add(SEED("Harper", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.431f, 0.431f, 0.431f,
        true));
      add(SEED("HaX0R_GR33N", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("HaX0R_R3D", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("HaX0R_R3D", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        false));
      add(SEED("Himawari", "google", "dark",
        0.408f, 0.545f, 0.867f,
        0.867f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.071f, 0.071f, 0.071f,
        0.557f, 0.557f, 0.557f,
        true));
      add(SEED("Hipster Green", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.529f, 0.529f, 0.529f,
        true));
      add(SEED("Homebrew", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Hurtado", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.318f, 0.318f, 0.318f,
        true));
      add(SEED("Hybrid", "google", "dark",
        0.424f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.122f, 0.122f,
        0.373f, 0.361f, 0.345f,
        true));
      add(SEED("IC_Green_PPL", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("IC_Orange_PPL", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("idleToes", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.302f, 0.302f, 0.302f,
        true));
      add(SEED("IR_Black", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.388f, 0.388f, 0.388f,
        true));
      add(SEED("Jackie Brown", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.122f, 0.118f, 0.078f,
        0.522f, 0.522f, 0.522f,
        true));
      add(SEED("Japanesque", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Jellybeans", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.239f, 0.655f,
        0.114f, 0.525f, 0.651f,
        0.071f, 0.071f, 0.071f,
        0.337f, 0.337f, 0.337f,
        true));
      add(SEED("Jet Black", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Jup", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Kibble", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.012f, 0.012f, 0.012f,
        0.259f, 0.259f, 0.259f,
        true));
      add(SEED("Konsolas", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Lab Fox", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Laser", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.318f, 0.318f, 0.318f,
        true));
      add(SEED("Later This Evening", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.349f, 0.349f, 0.349f,
        true));
      add(SEED("Lavandula", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.349f, 0.349f, 0.349f,
        true));
      add(SEED("Liquid Carbon", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Liquid Carbon Transparent", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Maia", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Man Page", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.282f, 0.282f, 0.282f,
        true));
      add(SEED("Mar", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Material", "google", "dark",
        0.004f, 0.522f, 0.749f,
        0.522f, 0.239f, 0.655f,
        0.000f, 0.522f, 0.749f,
        0.122f, 0.122f, 0.122f,
        0.416f, 0.416f, 0.416f,
        true));
      add(SEED("Material", "google", "light",
        0.004f, 0.522f, 0.749f,
        0.522f, 0.239f, 0.655f,
        0.000f, 0.522f, 0.749f,
        0.969f, 0.969f, 0.969f,
        0.416f, 0.416f, 0.416f,
        false));
      add(SEED("Mathias", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Medallion", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.404f, 0.404f, 0.404f,
        true));
      add(SEED("Misterioso", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.353f, 0.353f, 0.353f,
        true));
      add(SEED("Miu", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.310f, 0.310f, 0.310f,
        true));
      add(SEED("Molokai", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.106f, 0.122f, 0.118f,
        0.416f, 0.404f, 0.388f,
        true));
      add(SEED("MonaLisa", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.325f, 0.325f, 0.325f,
        true));
      add(SEED("Monokai", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.153f, 0.157f, 0.133f,
        0.459f, 0.443f, 0.369f,
        true));
      add(SEED("Monokai Night", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.071f, 0.071f, 0.071f,
        0.459f, 0.443f, 0.369f,
        true));
      add(SEED("Monokai Pro", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.114f, 0.114f,
        0.459f, 0.443f, 0.369f,
        true));
      add(SEED("Monokai Pro", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.114f, 0.114f,
        0.459f, 0.443f, 0.369f,
        false));
      add(SEED("Monokai Remastered", "google", "dark",
        0.024f, 0.514f, 1.000f,
        0.514f, 0.024f, 1.000f,
        0.024f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.424f, 0.424f, 0.424f,
        true));
      add(SEED("Monokai Remastered", "google", "light",
        0.024f, 0.514f, 1.000f,
        0.514f, 0.024f, 1.000f,
        0.024f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.424f, 0.424f, 0.424f,
        false));
      add(SEED("Monokai Soda", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.671f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.114f, 0.114f,
        0.459f, 0.443f, 0.369f,
        true));
      add(SEED("Monokai Vivid", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.071f, 0.071f, 0.071f,
        0.459f, 0.443f, 0.369f,
        true));
      add(SEED("N0tch2k", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Neon", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Neopolitan", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Nep", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Network", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Night Owl", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.004f, 0.086f, 0.153f,
        0.294f, 0.392f, 0.475f,
        true));
      add(SEED("Night Owl", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.957f, 0.957f, 0.953f,
        0.294f, 0.392f, 0.475f,
        false));
      add(SEED("Nord", "google", "dark",
        0.294f, 0.561f, 0.608f,
        0.584f, 0.439f, 0.557f,
        0.557f, 0.671f, 0.463f,
        0.180f, 0.204f, 0.251f,
        0.373f, 0.404f, 0.475f,
        true));
      add(SEED("Nord", "google", "light",
        0.294f, 0.561f, 0.608f,
        0.584f, 0.439f, 0.557f,
        0.557f, 0.671f, 0.463f,
        0.925f, 0.933f, 0.945f,
        0.373f, 0.404f, 0.475f,
        false));
      add(SEED("Nova", "google", "dark",
        0.392f, 0.627f, 0.937f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.192f, 0.188f, 0.169f,
        0.369f, 0.345f, 0.341f,
        true));
      add(SEED("One Dark", "google", "dark",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.153f, 0.169f, 0.200f,
        0.360f, 0.388f, 0.439f,
        true));
      add(SEED("One Half Black", "google", "dark",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.071f, 0.071f, 0.071f,
        0.420f, 0.420f, 0.420f,
        true));
      add(SEED("One Half Black", "google", "light",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.071f, 0.071f, 0.071f,
        0.420f, 0.420f, 0.420f,
        false));
      add(SEED("One Half Dark", "google", "dark",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.071f, 0.071f, 0.071f,
        0.365f, 0.365f, 0.365f,
        true));
      add(SEED("One Half Dark", "google", "light",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.071f, 0.071f, 0.071f,
        0.365f, 0.365f, 0.365f,
        false));
      add(SEED("One Half Light", "google", "light",
        0.376f, 0.682f, 0.933f,
        0.894f, 0.749f, 0.478f,
        0.592f, 0.761f, 0.471f,
        0.957f, 0.957f, 0.953f,
        0.420f, 0.420f, 0.420f,
        false));
      add(SEED("Overnight", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.310f, 0.310f, 0.310f,
        true));
      add(SEED("Overnight Slumber", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.298f, 0.298f, 0.298f,
        true));
      add(SEED("Palenight", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Panda", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.239f, 0.655f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.518f, 0.518f, 0.518f,
        true));
      add(SEED("PaperColor", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.431f, 0.431f, 0.431f,
        true));
      add(SEED("PaperColor", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.431f, 0.431f, 0.431f,
        false));
      add(SEED("Paraiso", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.286f, 0.286f, 0.286f,
        true));
      add(SEED("Parasio", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.286f, 0.286f, 0.286f,
        true));
      add(SEED("Passion", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Pear", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Pencil Dark", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Pencil Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.545f, 0.545f, 0.545f,
        false));
      add(SEED("Peppermint", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.310f, 0.310f, 0.310f,
        true));
      add(SEED("Piatto Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.361f, 0.361f, 0.361f,
        false));
      add(SEED("Pnevma", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Pro Light", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.365f, 0.365f, 0.365f,
        false));
      add(SEED("Purple Rain", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.431f, 0.431f, 0.431f,
        true));
      add(SEED("Rapture", "google", "dark",
        0.024f, 0.514f, 1.000f,
        0.514f, 0.024f, 1.000f,
        0.024f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Rapture", "google", "light",
        0.024f, 0.514f, 1.000f,
        0.514f, 0.024f, 1.000f,
        0.024f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        false));
      add(SEED("Raycast Dark", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Raycast Dark", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Red Alert", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.439f, 0.439f, 0.439f,
        true));
      add(SEED("Red Sands", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Relaxed", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.376f, 0.376f, 0.376f,
        true));
      add(SEED("Relaxed", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.376f, 0.376f, 0.376f,
        false));
      add(SEED("Ripped", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.278f, 0.278f, 0.278f,
        true));
      add(SEED("Royal", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.404f, 0.404f, 0.404f,
        true));
      add(SEED("Sat", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.286f, 0.286f, 0.286f,
        true));
      add(SEED("Sea Shells", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.353f, 0.353f, 0.353f,
        true));
      add(SEED("Seafoam Pastel", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Seti", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.059f, 0.063f, 0.071f,
        0.361f, 0.361f, 0.361f,
        true));
      add(SEED("Shaman", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.012f, 0.012f, 0.012f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Shades Of Purple", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Slate", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.325f, 0.325f, 0.325f,
        true));
      add(SEED("Smyck", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Snazzy", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.157f, 0.165f, 0.212f,
        0.373f, 0.404f, 0.475f,
        true));
      add(SEED("Soft Server", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Solar Darcula", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Solar Darcula", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        false));
      add(SEED("Solarized Dark", "google", "dark",
        0.004f, 0.545f, 0.827f,
        0.522f, 0.000f, 0.522f,
        0.149f, 0.545f, 0.510f,
        0.000f, 0.169f, 0.212f,
        0.345f, 0.431f, 0.459f,
        true));
      add(SEED("Solarized Dark Higher Contrast", "google", "dark",
        0.004f, 0.545f, 0.827f,
        0.522f, 0.000f, 0.522f,
        0.149f, 0.545f, 0.510f,
        0.000f, 0.169f, 0.212f,
        0.345f, 0.431f, 0.459f,
        true));
      add(SEED("Solarized Light", "google", "light",
        0.004f, 0.545f, 0.827f,
        0.522f, 0.000f, 0.522f,
        0.149f, 0.545f, 0.510f,
        0.992f, 0.965f, 0.890f,
        0.498f, 0.553f, 0.588f,
        false));
      add(SEED("Sonokai", "google", "dark",
        0.729f, 0.773f, 0.886f,
        0.980f, 0.714f, 0.592f,
        0.404f, 0.557f, 0.871f,
        0.145f, 0.141f, 0.145f,
        0.373f, 0.365f, 0.384f,
        true));
      add(SEED("SpaceGray", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("SpaceGray", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Spacedust", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.039f, 0.051f, 0.047f,
        0.373f, 0.361f, 0.345f,
        true));
      add(SEED("Spiderman", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Square", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Srcery", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.412f, 0.412f, 0.412f,
        true));
      add(SEED("Sublime", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.184f, 0.180f, 0.176f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Sublime", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.969f, 0.961f, 0.961f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Sundried", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Symfonic", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.357f, 0.357f, 0.357f,
        true));
      add(SEED("Tango", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.337f, 0.337f, 0.337f,
        true));
      add(SEED("Tango", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.337f, 0.337f, 0.337f,
        false));
      add(SEED("Teerb", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.341f, 0.341f, 0.341f,
        true));
      add(SEED("Terminal Basic", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Terminal Basic", "google", "light",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        false));
      add(SEED("Thayer Bright", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("The Hulk", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("The Hulk", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        false));
      add(SEED("Tinacious Design", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.722f, 0.722f,
        0.071f, 0.071f, 0.071f,
        0.455f, 0.455f, 0.455f,
        true));
      add(SEED("Tokyo Night", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.059f, 0.063f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Tokyo Night", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.957f, 0.957f, 0.953f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Tokyo Night Storm", "google", "dark",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.122f, 0.161f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Tokyo Night Storm", "google", "light",
        0.404f, 0.557f, 0.871f,
        0.871f, 0.404f, 0.557f,
        0.404f, 0.557f, 0.871f,
        0.114f, 0.122f, 0.161f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Tomorrow", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Tomorrow", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Tomorrow Night", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Tomorrow Night", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.373f, 0.373f, 0.373f,
        false));
      add(SEED("Tomorrow Night Blue", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Tomorrow Night Bright", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.424f, 0.424f, 0.424f,
        true));
      add(SEED("Tomorrow Night Eighties", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Toolbox", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.294f, 0.294f, 0.294f,
        true));
      add(SEED("Treehouse", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Twilight", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.486f, 0.486f, 0.486f,
        true));
      add(SEED("Ubuntu", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Ubuntu", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.400f, 0.400f, 0.400f,
        false));
      add(SEED("Urple", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.541f, 0.541f, 0.541f,
        true));
      add(SEED("Vag", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.322f, 0.322f, 0.322f,
        true));
      add(SEED("Vaughn", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.325f, 0.325f, 0.325f,
        true));
      add(SEED("Vibrant Ink", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.376f, 0.376f, 0.376f,
        true));
      add(SEED("Violet", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Violet", "google", "light",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.969f, 0.969f, 0.969f,
        0.275f, 0.275f, 0.275f,
        false));
      add(SEED("Warm Neon", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.345f, 0.345f, 0.345f,
        true));
      add(SEED("Wez", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.275f, 0.275f, 0.275f,
        true));
      add(SEED("Wild Cherry", "google", "dark",
        0.000f, 0.522f, 0.749f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.373f, 0.373f, 0.373f,
        true));
      add(SEED("Wombat", "google", "dark",
        0.071f, 0.337f, 0.667f,
        0.522f, 0.000f, 0.522f,
        0.000f, 0.522f, 0.522f,
        0.071f, 0.071f, 0.071f,
        0.439f, 0.439f, 0.439f,
        true));
      add(SEED("Wryan", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.384f, 0.384f, 0.384f,
        true));
      add(SEED("Xterm", "google", "dark",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.071f, 0.071f, 0.071f,
        0.400f, 0.400f, 0.400f,
        true));
      add(SEED("Xterm", "google", "light",
        0.031f, 0.514f, 1.000f,
        0.514f, 0.031f, 1.000f,
        0.031f, 1.000f, 0.514f,
        0.969f, 0.969f, 0.969f,
        0.400f, 0.400f, 0.400f,
        false));

    return v;
  }();
  return all;
}

// Parse hex string "#rrggbb" → float triples (0..1).
// Returns {r, g, b} = {0,0,0} on failure.
inline ThemePalette hex_to_palette(const std::unordered_map<std::string, std::string>& hexMap) {
  auto hex_to_rgb = [](const std::string& h) -> std::array<float, 3> {
    if (h.size() < 7 || h[0] != '#') return {0,0,0};
    auto hx = [&](size_t i) -> int {
      char c = h[i];
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return 0;
    };
    float r = static_cast<float>(hx(1)*16 + hx(2)) / 255.0f;
    float g = static_cast<float>(hx(3)*16 + hx(4)) / 255.0f;
    float b = static_cast<float>(hx(5)*16 + hx(6)) / 255.0f;
    return {r, g, b};
  };

  ThemePalette p{};
  auto assign = [&](float& r, float& g, float& b, const std::string& key) {
    auto it = hexMap.find(key);
    if (it != hexMap.end()) {
      auto rgb = hex_to_rgb(it->second);
      r = rgb[0]; g = rgb[1]; b = rgb[2];
    }
  };
  assign(p.primaryR, p.primaryG, p.primaryB, "primary");
  assign(p.onPrimaryR, p.onPrimaryG, p.onPrimaryB, "onPrimary");
  assign(p.secondaryR, p.secondaryG, p.secondaryB, "secondary");
  assign(p.onSecondaryR, p.onSecondaryG, p.onSecondaryB, "onSecondary");
  assign(p.tertiaryR, p.tertiaryG, p.tertiaryB, "tertiary");
  assign(p.onTertiaryR, p.onTertiaryG, p.onTertiaryB, "onTertiary");
  assign(p.errorR, p.errorG, p.errorB, "error");
  assign(p.onErrorR, p.onErrorG, p.onErrorB, "onError");
  assign(p.surfaceR, p.surfaceG, p.surfaceB, "surface");
  assign(p.onSurfaceR, p.onSurfaceG, p.onSurfaceB, "onSurface");
  assign(p.surfaceVariantR, p.surfaceVariantG, p.surfaceVariantB, "surfaceVariant");
  assign(p.onSurfaceVariantR, p.onSurfaceVariantG, p.onSurfaceVariantB, "onSurfaceVariant");
  assign(p.outlineR, p.outlineG, p.outlineB, "outline");
  assign(p.shadowR, p.shadowG, p.shadowB, "shadow");
  assign(p.hoverR, p.hoverG, p.hoverB, "hover");
  assign(p.onHoverR, p.onHoverG, p.onHoverB, "onHover");
  return p;
}

// Convert ThemePalette to a hex-string map for TOML serialization.
inline std::unordered_map<std::string, std::string> palette_to_hex_map(const ThemePalette& p) {
  auto rgb_to_hex = [&](float r, float g, float b) -> std::string {
    int ir = std::clamp(static_cast<int>(r * 255.0f + 0.5f), 0, 255);
    int ig = std::clamp(static_cast<int>(g * 255.0f + 0.5f), 0, 255);
    int ib = std::clamp(static_cast<int>(b * 255.0f + 0.5f), 0, 255);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ir, ig, ib);
    return buf;
  };
  return {
    {"primary", rgb_to_hex(p.primaryR, p.primaryG, p.primaryB)},
    {"onPrimary", rgb_to_hex(p.onPrimaryR, p.onPrimaryG, p.onPrimaryB)},
    {"secondary", rgb_to_hex(p.secondaryR, p.secondaryG, p.secondaryB)},
    {"onSecondary", rgb_to_hex(p.onSecondaryR, p.onSecondaryG, p.onSecondaryB)},
    {"tertiary", rgb_to_hex(p.tertiaryR, p.tertiaryG, p.tertiaryB)},
    {"onTertiary", rgb_to_hex(p.onTertiaryR, p.onTertiaryG, p.onTertiaryB)},
    {"error", rgb_to_hex(p.errorR, p.errorG, p.errorB)},
    {"onError", rgb_to_hex(p.onErrorR, p.onErrorG, p.onErrorB)},
    {"surface", rgb_to_hex(p.surfaceR, p.surfaceG, p.surfaceB)},
    {"onSurface", rgb_to_hex(p.onSurfaceR, p.onSurfaceG, p.onSurfaceB)},
    {"surfaceVariant", rgb_to_hex(p.surfaceVariantR, p.surfaceVariantG, p.surfaceVariantB)},
    {"onSurfaceVariant", rgb_to_hex(p.onSurfaceVariantR, p.onSurfaceVariantG, p.onSurfaceVariantB)},
    {"outline", rgb_to_hex(p.outlineR, p.outlineG, p.outlineB)},
    {"shadow", rgb_to_hex(p.shadowR, p.shadowG, p.shadowB)},
    {"hover", rgb_to_hex(p.hoverR, p.hoverG, p.hoverB)},
    {"onHover", rgb_to_hex(p.onHoverR, p.onHoverG, p.onHoverB)},
  };
}

} // namespace eh::settings::theme
