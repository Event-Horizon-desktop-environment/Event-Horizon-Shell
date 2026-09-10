#pragma once

namespace eh::wallpaper {

// Entry point for the split-out `horizon-wallpaper` child: it owns
// a WaylandConnection + WallpaperRenderer, paints the configured wallpaper on
// BACKGROUND layer surfaces, and re-applies on the `config.applied` broadcast.
[[nodiscard]] int run_wallpaper_standalone();

}
