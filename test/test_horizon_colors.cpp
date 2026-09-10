#include "color/horizon_colors.hpp"
#include "stb/stb_image_write.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(expr)                                                                                     \
  do {                                                                                                  \
    if (expr) {                                                                                         \
      ++g_passed;                                                                                       \
    } else {                                                                                            \
      ++g_failed;                                                                                       \
      std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                            \
    }                                                                                                   \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                                           \
  do {                                                                                                  \
    const double _a = (a);                                                                              \
    const double _b = (b);                                                                              \
    if (std::fabs(_a - _b) <= (eps)) {                                                                  \
      ++g_passed;                                                                                       \
    } else {                                                                                            \
      ++g_failed;                                                                                       \
      std::fprintf(stderr, "FAIL  %s:%d  %s ~= %s  (%.6f != %.6f, eps=%.6f)\n", __FILE__, __LINE__,   \
                   #a, #b, _a, _b, static_cast<double>(eps));                                           \
    }                                                                                                   \
  } while (0)

static bool in_range(float v, float lo = 0.0f, float hi = 1.0f) { return v >= lo && v <= hi; }

// ── Helper: generate a tiny synthetic PNG on disk ───────────────────────────
static std::string make_test_png(const std::string& label, int w, int h,
                                 const std::vector<uint8_t>& rgb) {
  namespace fs = std::filesystem;
  const std::string dir = (fs::temp_directory_path() / "eh_horizon_colors_test").string();
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string path = dir + "/" + label + ".png";
  if (!stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3)) {
    std::fprintf(stderr, "failed to write %s\n", path.c_str());
    return {};
  }
  return path;
}

static void cleanup_test_dir() {
  namespace fs = std::filesystem;
  const std::string dir = (fs::temp_directory_path() / "eh_horizon_colors_test").string();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// ── Test: scheme_variant_from_name / scheme_variant_name roundtrip ──────────
static void test_variant_names() {
  const char* names[] = {"scheme-tonal-spot", "scheme-vibrant", "scheme-expressive",
                          "scheme-content", "scheme-fidelity", "scheme-monochrome",
                          "scheme-neutral", "scheme-rainbow", "scheme-fruit-salad"};
  const eh::color::SchemeVariant variants[] = {
      eh::color::SchemeVariant::TonalSpot,   eh::color::SchemeVariant::Vibrant,
      eh::color::SchemeVariant::Expressive,   eh::color::SchemeVariant::Content,
      eh::color::SchemeVariant::Fidelity,     eh::color::SchemeVariant::Monochrome,
      eh::color::SchemeVariant::Neutral,      eh::color::SchemeVariant::Rainbow,
      eh::color::SchemeVariant::FruitSalad,
  };

  for (int i = 0; i < 9; ++i) {
    const auto v = eh::color::scheme_variant_from_name(names[i]);
    CHECK(v == variants[i]);
    const char* back = eh::color::scheme_variant_name(v);
    CHECK(std::string_view(back) == std::string_view(names[i]));
  }

  // Unknown name should default to Content
  CHECK(eh::color::scheme_variant_from_name("garbage") == eh::color::SchemeVariant::Content);
  CHECK(eh::color::scheme_variant_from_name("") == eh::color::SchemeVariant::Content);
}

// ── Test: format utilities ──────────────────────────────────────────────────
static void test_format_utils() {
  using namespace eh::color;

  // Pure red
  const Argb red = argb_from_rgb(255, 0, 0);
  CHECK(red_from_argb(red) == 255);
  CHECK(green_from_argb(red) == 0);
  CHECK(blue_from_argb(red) == 0);
  CHECK(alpha_from_argb(red) == 255);

  const std::string hex = hex_from_argb(red);
  CHECK(hex == "#FF0000" || hex == "#ff0000");

  // Black
  const Argb black = argb_from_rgb(0, 0, 0);
  CHECK(hex_from_argb(black) == "#000000");

  // White
  const Argb white = argb_from_rgb(255, 255, 255);
  CHECK(hex_from_argb(white) == "#FFFFFF" || hex_from_argb(white) == "#ffffff");

  // Roundtrip
  const Argb color = argb_from_rgb(42, 128, 200);
  CHECK(red_from_argb(color) == 42);
  CHECK(green_from_argb(color) == 128);
  CHECK(blue_from_argb(color) == 200);

  // RGB string
  const std::string rgb = rgb_string_from_argb(color);
  CHECK(!rgb.empty());

  // HSL string
  const std::string hsl = hsl_string_from_argb(color);
  CHECK(!hsl.empty());
}

// ── Test: generate_palette_from_color (single seed) ─────────────────────────
static void test_palette_from_color() {
  using namespace eh::color;

  const Argb seed = argb_from_rgb(100, 150, 200);

  const SchemeVariant all[] = {
      SchemeVariant::TonalSpot,   SchemeVariant::Vibrant,    SchemeVariant::Expressive,
      SchemeVariant::Content,     SchemeVariant::Fidelity,    SchemeVariant::Monochrome,
      SchemeVariant::Neutral,     SchemeVariant::Rainbow,     SchemeVariant::FruitSalad,
  };

  for (const auto v : all) {
    // Dark
    {
      PaletteResult r = generate_palette_from_color(seed, v, true, 0.0f);
      CHECK(r.ok);
      CHECK(r.sourceColorArgb == seed);
      CHECK(r.roles.size() >= 10);
      CHECK(in_range(r.accentR));
      CHECK(in_range(r.accentG));
      CHECK(in_range(r.accentB));
      CHECK(in_range(r.dockFillR));
      CHECK(in_range(r.textR));
      CHECK(in_range(r.outlineR));
      CHECK(in_range(r.notifCriticalBgR));
      // Dark mode: dock fill should be relatively dark
      const float dockLum = 0.2126f * r.dockFillR + 0.7152f * r.dockFillG + 0.0722f * r.dockFillB;
      CHECK(dockLum < 0.6f);
    }
    // Light
    {
      PaletteResult r = generate_palette_from_color(seed, v, false, 0.0f);
      CHECK(r.ok);
      CHECK(r.sourceColorArgb == seed);
      CHECK(in_range(r.accentR));
      // Light mode: dock fill should be relatively light
      const float dockLum = 0.2126f * r.dockFillR + 0.7152f * r.dockFillG + 0.0722f * r.dockFillB;
      CHECK(dockLum > 0.4f);
    }
  }

  // Contrast level
  {
    PaletteResult r0 = generate_palette_from_color(seed, SchemeVariant::TonalSpot, true, 0.0f);
    PaletteResult r1 = generate_palette_from_color(seed, SchemeVariant::TonalSpot, true, 1.0f);
    CHECK(r0.ok && r1.ok);
    // Dark mode: text should be white regardless of contrast level
    CHECK(r0.textR == 1.0f);
    CHECK(r0.textG == 1.0f);
    CHECK(r0.textB == 1.0f);
    CHECK(r1.textR == 1.0f);
    CHECK(r1.textG == 1.0f);
    CHECK(r1.textB == 1.0f);
    // Contrast level should still shift accent colors
    const float accentDiff = std::fabs(r0.accentR - r1.accentR) +
                             std::fabs(r0.accentG - r1.accentG) +
                             std::fabs(r0.accentB - r1.accentB);
    CHECK(accentDiff > 0.001f);
  }
}

// ── Test: generate_palette_from_image ───────────────────────────────────────
static void test_palette_from_image() {
  using namespace eh::color;

  // Create a 16x16 test image with a gradient
  std::vector<uint8_t> pixels(16 * 16 * 3);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const int i = (y * 16 + x) * 3;
      pixels[i + 0] = static_cast<uint8_t>(x * 16);  // R: gradient
      pixels[i + 1] = static_cast<uint8_t>(y * 16);  // G: gradient
      pixels[i + 2] = 128;                             // B: constant
    }
  }

  const std::string path = make_test_png("gradient", 16, 16, pixels);
  CHECK(!path.empty());

  // Test all variants
  const SchemeVariant all[] = {
      SchemeVariant::TonalSpot,  SchemeVariant::Vibrant,   SchemeVariant::Expressive,
      SchemeVariant::Content,    SchemeVariant::Fidelity,   SchemeVariant::Monochrome,
      SchemeVariant::Neutral,    SchemeVariant::Rainbow,    SchemeVariant::FruitSalad,
  };

  for (const auto v : all) {
    PaletteResult r = generate_palette_from_image(path, v, true, 0.0f);
    CHECK(r.ok);
    CHECK(!r.roles.empty());
    CHECK(in_range(r.accentR));
    CHECK(in_range(r.accentG));
    CHECK(in_range(r.accentB));
    CHECK(in_range(r.dockFillR));
    CHECK(in_range(r.dockFillG));
    CHECK(in_range(r.dockFillB));
    CHECK(in_range(r.outlineR));
    CHECK(in_range(r.textR));
    CHECK(in_range(r.notifCriticalBgR));
    // Source color should have full alpha
    CHECK(alpha_from_argb(r.sourceColorArgb) == 255);
  }

  // Non-existent file should fail gracefully
  {
    PaletteResult r = generate_palette_from_image("/nonexistent/image.png");
    CHECK(!r.ok);
  }
}

// ── Test: color extremes ────────────────────────────────────────────────────
static void test_color_extremes() {
  using namespace eh::color;

  PaletteResult r;

  // Pure black
  r = generate_palette_from_color(argb_from_rgb(0, 0, 0), SchemeVariant::Content, true);
  CHECK(r.ok);
  CHECK(in_range(r.accentR));
  CHECK(!r.roles.empty());

  // Pure white
  r = generate_palette_from_color(argb_from_rgb(255, 255, 255), SchemeVariant::Content, false);
  CHECK(r.ok);
  CHECK(in_range(r.accentR));

  // Single channel: pure red
  r = generate_palette_from_color(argb_from_rgb(255, 0, 0), SchemeVariant::Vibrant, true);
  CHECK(r.ok);
  CHECK(in_range(r.accentR));

  // Single channel: pure green
  r = generate_palette_from_color(argb_from_rgb(0, 255, 0), SchemeVariant::TonalSpot, true);
  CHECK(r.ok);
}

// ── Test: PaletteResult roles contain M3 keys ───────────────────────────────
static void test_roles_content() {
  using namespace eh::color;

  PaletteResult r = generate_palette_from_color(argb_from_rgb(120, 80, 200), SchemeVariant::Content, true);
  CHECK(r.ok);

  // M3 content scheme should have primary, secondary, tertiary tones
  // We expect at least 10 distinct roles
  CHECK(r.roles.size() >= 10);

  // All role ARGB values should have full alpha
  for (const auto& [key, argb] : r.roles) {
    CHECK(alpha_from_argb(argb) == 255);
    (void)key;
  }
}

// ── Main ────────────────────────────────────────────────────────────────────
void dump_wallpaper_palette(const char* path) {
  using namespace eh::color;
  fprintf(stderr, "\n=== WALLPALETTE DUMP: %s ===\n", path);

  auto dump = [&](const char* label, SchemeVariant v, bool dark) {
    PaletteResult r = generate_palette_from_image(path, v, dark, 0.0f);
    if (!r.ok) { fprintf(stderr, "%s: FAILED\n", label); return; }
    fprintf(stderr, "\n--- %s (is_dark=%d) ---\n", label, dark);
    fprintf(stderr, "source: 0x%08x (#%06x)\n", r.sourceColorArgb, r.sourceColorArgb & 0x00FFFFFF);

    const char* names[] = {
      "primary", "on_primary", "primary_container", "on_primary_container",
      "secondary", "on_secondary", "secondary_container", "on_secondary_container",
      "tertiary", "on_tertiary", "tertiary_container", "on_tertiary_container",
      "error", "on_error", "error_container", "on_error_container",
      "surface", "on_surface", "surface_variant", "on_surface_variant",
      "surface_dim", "surface_bright", "surface_container_lowest",
      "surface_container_low", "surface_container", "surface_container_high",
      "surface_container_highest", "inverse_surface", "inverse_on_surface",
      "inverse_primary", "outline", "outline_variant", "shadow", "scrim",
      "surface_tint",
      "primary_fixed", "primary_fixed_dim", "on_primary_fixed", "on_primary_fixed_variant",
      "secondary_fixed", "secondary_fixed_dim", "on_secondary_fixed", "on_secondary_fixed_variant",
      "tertiary_fixed", "tertiary_fixed_dim", "on_tertiary_fixed", "on_tertiary_fixed_variant"
    };

    for (int i = 0; i < 47; ++i) {
      auto it = r.roles.find(i);
      if (it != r.roles.end())
        fprintf(stderr, "  [%2d] %-30s #%06x\n", i, names[i], it->second & 0x00FFFFFF);
    }
  };

  dump("Content-dark", SchemeVariant::Content, true);
  dump("Content-light", SchemeVariant::Content, false);
  dump("Vibrant-dark", SchemeVariant::Vibrant, true);
  dump("Vibrant-light", SchemeVariant::Vibrant, false);
}

int main(int argc, char** argv) {
  std::fprintf(stderr, "=== Horizon Colors Engine Tests ===\n");

  test_variant_names();
  test_format_utils();
  test_palette_from_color();
  test_palette_from_image();
  test_color_extremes();
  test_roles_content();

  cleanup_test_dir();

  if (argc > 1) {
    dump_wallpaper_palette(argv[1]);
  }

  std::fprintf(stderr, "\n%d passed, %d failed\n", g_passed, g_failed);
  return g_failed > 0 ? 1 : 0;
}
