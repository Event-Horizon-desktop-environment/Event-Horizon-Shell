#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "ux/settings/common/logo/settings_logo.hpp"

cairo_surface_t* load_logo_surface() {
   
  return eh::shell::create_material_glyph_surface("settings", 48);
}
