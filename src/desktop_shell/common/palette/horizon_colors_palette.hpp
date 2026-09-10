#pragma once

#include <string>

namespace eh::config {
struct ShellAppearance;
}

namespace eh::horizon_colors {

// Native palette generation — replaces the external color-generator subprocess.
// Decodes the image, quantizes, scores, and generates a full M3 palette in-process.
// Writes results directly into the ShellAppearance struct.
void refresh_wallpaper_derived_palette_native(eh::config::ShellAppearance& appearance,
                                              const std::string& normalized_wallpaper_image_path);

}
