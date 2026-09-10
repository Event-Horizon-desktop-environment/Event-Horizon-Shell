#pragma once

#include <string>
#include <string_view>

namespace eh::config {
struct ShellAppearance;
}

namespace eh::matugen {

[[nodiscard]] std::string normalize_matugen_scheme(std::string_view in);
[[nodiscard]] std::string normalize_matugen_mode(std::string_view in);

void refresh_wallpaper_derived_palette(eh::config::ShellAppearance& appearance,
                                         const std::string& normalized_wallpaper_image_path);

}
