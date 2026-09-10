#include "platform/common/palette/matugen_palette.hpp"
#include "config/shell_config.hpp"

namespace eh::matugen {

std::string normalize_matugen_scheme(std::string_view in) {
  return std::string(in);
}

std::string normalize_matugen_mode(std::string_view in) {
  return std::string(in);
}

void refresh_wallpaper_derived_palette(eh::config::ShellAppearance& appearance,
                                         const std::string& normalized_wallpaper_image_path) {
  (void)appearance;
  (void)normalized_wallpaper_image_path;
  // No-op: standalone file browser doesn't use matugen
}

}
