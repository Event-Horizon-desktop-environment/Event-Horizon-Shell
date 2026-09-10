#pragma once

// Horizon Colors — Event Horizon's native C++ Material Design 3 color engine.
// Bundles Google's material-color-utilities (Apache-2.0) under eh::color::.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace eh::color {

// ARGB32 color type, kept the same shape as material_color_utilities::Argb.
using Argb = uint32_t;

// Which scheme family to build.
enum class SchemeVariant : uint8_t {
  TonalSpot,
  Vibrant,
  Expressive,
  Content,
  Fidelity,
  Monochrome,
  Neutral,
  Rainbow,
  FruitSalad,
};

// What the full pipeline produces.
struct PaletteResult {
  bool ok = false;

  // Float RGB (0–1) chrome colors, laid out to match ShellAppearance's fields.
  float dockFillR = 0.f, dockFillG = 0.f, dockFillB = 0.f;
  float panelFillR = 0.f, panelFillG = 0.f, panelFillB = 0.f;
  float drawerDimR = 0.f, drawerDimG = 0.f, drawerDimB = 0.f;
  float outlineR = 0.f, outlineG = 0.f, outlineB = 0.f;
  float accentR = 0.f, accentG = 0.f, accentB = 0.f;
  float textR = 0.f, textG = 0.f, textB = 0.f;
  float notifCriticalBgR = 0.f, notifCriticalBgG = 0.f, notifCriticalBgB = 0.f;
  float notifCriticalOutlineR = 0.f, notifCriticalOutlineG = 0.f, notifCriticalOutlineB = 0.f;

  // The seed color the scorer picked.
  Argb sourceColorArgb = 0;

  // The complete M3 role set, for the template engine and anything else that wants it.
  std::unordered_map<uint8_t, Argb> roles;
};

// Decode the image, quantize it to a handful of colors, score those to pick a
// seed, then build the full M3 palette with the requested scheme variant.
// Returns a PaletteResult with ok=true when everything succeeded.
[[nodiscard]] PaletteResult generate_palette_from_image(
    const std::string& image_path,
    SchemeVariant variant = SchemeVariant::Content,
    bool is_dark = true,
    float contrast_level = 0.0f,
    int max_colors = 128);

// Same as the image version, but starting from one explicit seed color.
[[nodiscard]] PaletteResult generate_palette_from_color(
    Argb source_color,
    SchemeVariant variant = SchemeVariant::Content,
    bool is_dark = true,
    float contrast_level = 0.0f);

// Like generate_palette_from_image, but remembers the picked seed:
//
//   * an in-process memory cache, and
//   * a disk cache under $XDG_STATE_HOME/event-horizon/palette-cache/, keyed
//     by (path, mtime_ns, size, variant, dark, contrast, max_colors) so the
//     heavy image decode runs once per wallpaper state and is shared by every
//     shell component that reloads config.
//
// A miss computes the seed from the image; a hit rebuilds the palette from the
// cached seed deterministically.
[[nodiscard]] PaletteResult generate_palette_from_image_cached(
    const std::string& image_path,
    SchemeVariant variant = SchemeVariant::Content,
    bool is_dark = true,
    float contrast_level = 0.0f,
    int max_colors = 128);

// Scheme variant name ↔ enum conversion.
[[nodiscard]] SchemeVariant scheme_variant_from_name(std::string_view name);
[[nodiscard]] const char* scheme_variant_name(SchemeVariant v);

// ARGB format helpers.
[[nodiscard]] inline uint8_t red_from_argb(Argb c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
[[nodiscard]] inline uint8_t green_from_argb(Argb c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
[[nodiscard]] inline uint8_t blue_from_argb(Argb c) { return static_cast<uint8_t>(c & 0xFF); }
[[nodiscard]] inline uint8_t alpha_from_argb(Argb c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }
[[nodiscard]] inline Argb argb_from_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

[[nodiscard]] std::string hex_from_argb(Argb c);
[[nodiscard]] std::string rgb_string_from_argb(Argb c);
[[nodiscard]] std::string rgba_string_from_argb(Argb c, uint8_t alpha = 255);
[[nodiscard]] std::string hsl_string_from_argb(Argb c);

} // namespace eh::color
