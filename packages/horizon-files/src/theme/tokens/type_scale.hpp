#pragma once

#include <cstdint>
#include <string>

namespace m3 {

struct FontConfig {
  float size;             // sp (scaled pixels)
  int weight;             // 100–900
  float lineHeight;       // dp
  float letterSpacing;    // dp (can be negative)
  std::string family;     // "brand" | "plain" | "mono"
};

// M3 15-step typescale. All values match the Material Design type scale spec.
struct TypeScale {
  // Display — largest, shortest-lived (hero text, splash)
  FontConfig displayLarge   { 57,  400, 64,  -0.25f, "brand" };
  FontConfig displayMedium  { 45,  400, 52,   0.0f,  "brand" };
  FontConfig displaySmall   { 36,  400, 44,   0.0f,  "brand" };

  // Headline — medium emphasis, moderate lifespan
  FontConfig headlineLarge  { 32,  400, 40,   0.0f,  "brand" };
  FontConfig headlineMedium { 28,  400, 36,   0.0f,  "brand" };
  FontConfig headlineSmall  { 24,  400, 32,   0.0f,  "brand" };

  // Title — medium emphasis, persistent
  FontConfig titleLarge     { 22,  400, 28,   0.0f,  "plain" };
  FontConfig titleMedium    { 16,  500, 24,   0.15f, "plain" };
  FontConfig titleSmall     { 14,  500, 20,   0.1f,  "plain" };

  // Body — high emphasis, long lifespan
  FontConfig bodyLarge      { 16,  400, 24,   0.5f,  "plain" };
  FontConfig bodyMedium     { 14,  400, 20,   0.25f, "plain" };
  FontConfig bodySmall      { 12,  400, 16,   0.4f,  "plain" };

  // Label — small, utilitarian (buttons, captions, chips)
  FontConfig labelLarge     { 14,  500, 20,   0.1f,  "plain" };
  FontConfig labelMedium    { 12,  500, 16,   0.5f,  "plain" };
  FontConfig labelSmall     { 11,  500, 16,   0.5f,  "plain" };
};

// Current → M3 mapping:
//   fontSizeMini    (11)  → labelSmall   (11sp medium)
//   fontSizeCaption (13)  → bodyMedium   (14sp regular)
//   fontSizeBody    (14)  → bodyMedium   (14sp regular)
//   fontSizeTitle   (16)  → titleMedium  (16sp medium)
//   fontSizeHeader  (20)  → headlineSmall(24sp regular)
//   button text     (13)  → labelLarge   (14sp medium)
//   tab label              → titleSmall  (14sp medium)

} // namespace m3
