#include "desktop_shell/ui/theme.hpp"

namespace eh::ui {

namespace {

FallbackPalette g_palette = kFallbackDark;
bool g_light = false;

} // namespace

float global_accent_r() { return g_palette.primary.r; }
float global_accent_g() { return g_palette.primary.g; }
float global_accent_b() { return g_palette.primary.b; }
float global_surface_r() { return g_palette.surface.r; }
float global_surface_g() { return g_palette.surface.g; }
float global_surface_b() { return g_palette.surface.b; }
float global_outline_r() { return g_palette.outline.r; }
float global_outline_g() { return g_palette.outline.g; }
float global_outline_b() { return g_palette.outline.b; }
float global_text_r() { return g_palette.onSurface.r; }
float global_text_g() { return g_palette.onSurface.g; }
float global_text_b() { return g_palette.onSurface.b; }

void set_global_accent(float r, float g, float b) {
  g_palette.primary    = { r, g, b };
  g_palette.onPrimary  = { 1.0f, 1.0f, 1.0f };
  g_palette.hover      = { r * 1.18f, g * 1.18f, b * 1.18f };
  g_palette.onHover    = { 0.969f, 0.718f, 0.200f };
}

void set_global_surface(float r, float g, float b) {
  g_palette.surface = { r, g, b };
}

void set_global_outline(float r, float g, float b) {
  g_palette.outline = { r, g, b };
}

void set_global_text(float r, float g, float b) {
  g_palette.onSurface = { r, g, b };
}

const FallbackPalette& fallback_palette() { return g_palette; }
bool fallback_is_light() { return g_light; }

void set_fallback_light(bool light) {
  g_light = light;
  g_palette = light ? kFallbackLight : kFallbackDark;
}

} // namespace eh::ui
