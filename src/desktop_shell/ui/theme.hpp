#pragma once

#include "desktop_shell/ui/fallback_palette.hpp"

namespace eh::ui {

float global_accent_r();
float global_accent_g();
float global_accent_b();
float global_surface_r();
float global_surface_g();
float global_surface_b();
float global_outline_r();
float global_outline_g();
float global_outline_b();
float global_text_r();
float global_text_g();
float global_text_b();

void set_global_accent(float r, float g, float b);
void set_global_surface(float r, float g, float b);
void set_global_outline(float r, float g, float b);
void set_global_text(float r, float g, float b);

// Full fallback palette access (wider than the 3-role system above).
// Components that need more than accent/surface/outline can call this.
const FallbackPalette& fallback_palette();
bool fallback_is_light();

// Switch between dark/light fallback palette at runtime.
void set_fallback_light(bool light);

} // namespace eh::ui
