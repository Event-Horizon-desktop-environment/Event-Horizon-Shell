#pragma once
// Bundled SVG icon set for Horizon Disks (Eva MIT + Lucide ISC, assets/UI/).
// Surfaces are tinted at paint time via cairo_mask, so one raster serves
// both light/dark palettes. Falls back to text glyphs when a file is missing.

#include <cairo/cairo.h>

#include <string_view>

namespace eh::disks {
class Drive;
}

namespace eh::disks::icons {

struct Tint {
  double r = 1, g = 1, b = 1;
};

struct DiskIconSet {
  // Actions (Eva)
  cairo_surface_t* menu = nullptr;
  cairo_surface_t* more = nullptr;
  cairo_surface_t* search = nullptr;
  cairo_surface_t* settings = nullptr;
  cairo_surface_t* power = nullptr;
  cairo_surface_t* lock = nullptr;
  cairo_surface_t* unlock = nullptr;
  cairo_surface_t* plus = nullptr;
  cairo_surface_t* trash = nullptr;
  cairo_surface_t* check = nullptr;
  cairo_surface_t* edit = nullptr;
  cairo_surface_t* tag = nullptr;
  cairo_surface_t* swap = nullptr;
  cairo_surface_t* repeat = nullptr;
  cairo_surface_t* pin = nullptr;
  cairo_surface_t* info = nullptr;
  cairo_surface_t* moon = nullptr;
  cairo_surface_t* keypad = nullptr;
  cairo_surface_t* download = nullptr;
  cairo_surface_t* upload = nullptr;
  cairo_surface_t* close = nullptr;
  cairo_surface_t* warn = nullptr;
  cairo_surface_t* hard_drive = nullptr;
  // Gaps (Lucide)
  cairo_surface_t* eject = nullptr;
  cairo_surface_t* usb = nullptr;
  cairo_surface_t* disc = nullptr;
  cairo_surface_t* play = nullptr;
  cairo_surface_t* key = nullptr;
  cairo_surface_t* gauge = nullptr;
  cairo_surface_t* pulse = nullptr;
  cairo_surface_t* unplug = nullptr;
  cairo_surface_t* user = nullptr;
};

void load_disk_icons(DiskIconSet& set);
void free_disk_icons(DiskIconSet& set);

// Paint surface centered in an s×s box at (x,y), tinted. No-op on null.
void paint_svg(cairo_t* cr, cairo_surface_t* s, double x, double y, double sidelength,
               Tint tint);

// Menu glyph (unicode, from build_*_menu) → bundled icon, or null = draw text.
cairo_surface_t* menu_icon_for_glyph(const DiskIconSet& set, std::string_view glyph);

// Drive → device icon (hard-drive / usb / disc).
cairo_surface_t* drive_svg(const DiskIconSet& set, const Drive& d);

}  // namespace eh::disks::icons
