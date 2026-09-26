#include "ux/disks/ui/icons.hpp"

#include <algorithm>

#include "desktop_shell/common/asset/asset_loader.hpp"
#include "ux/disks/drive.hpp"

namespace eh::disks::icons {

void load_disk_icons(DiskIconSet& s) {
  using eh::shell::asset::load_asset_svg;
  constexpr int px = 48;
  s.menu = load_asset_svg("UI", "menu.svg", px);
  s.more = load_asset_svg("UI", "more-vertical.svg", px);
  s.search = load_asset_svg("UI", "search.svg", px);
  s.settings = load_asset_svg("UI", "settings.svg", px);
  s.power = load_asset_svg("UI", "power.svg", px);
  s.lock = load_asset_svg("UI", "lock.svg", px);
  s.unlock = load_asset_svg("UI", "unlock.svg", px);
  s.plus = load_asset_svg("UI", "plus.svg", px);
  s.trash = load_asset_svg("UI", "trash-2.svg", px);
  s.check = load_asset_svg("UI", "checkmark.svg", px);
  s.edit = load_asset_svg("UI", "edit-2.svg", px);
  s.tag = load_asset_svg("UI", "pricetags.svg", px);
  s.swap = load_asset_svg("UI", "swap.svg", px);
  s.repeat = load_asset_svg("UI", "repeat.svg", px);
  s.pin = load_asset_svg("UI", "pin.svg", px);
  s.info = load_asset_svg("UI", "info.svg", px);
  s.moon = load_asset_svg("UI", "moon.svg", px);
  s.keypad = load_asset_svg("UI", "keypad.svg", px);
  s.download = load_asset_svg("UI", "download.svg", px);
  s.upload = load_asset_svg("UI", "upload.svg", px);
  s.close = load_asset_svg("UI", "close.svg", px);
  s.warn = load_asset_svg("UI", "alert-triangle.svg", px);
  s.hard_drive = load_asset_svg("UI", "hard-drive.svg", px);
  s.eject = load_asset_svg("UI", "eject.svg", px);
  s.usb = load_asset_svg("UI", "usb.svg", px);
  s.disc = load_asset_svg("UI", "disc.svg", px);
  s.play = load_asset_svg("UI", "play.svg", px);
  s.key = load_asset_svg("UI", "key.svg", px);
  s.gauge = load_asset_svg("UI", "gauge.svg", px);
  s.pulse = load_asset_svg("UI", "heart-pulse.svg", px);
  s.unplug = load_asset_svg("UI", "unplug.svg", px);
  s.user = load_asset_svg("UI", "user-round.svg", px);
}

void free_disk_icons(DiskIconSet& s) {
  cairo_surface_t** all[] = {
      &s.menu, &s.more, &s.search, &s.settings, &s.power, &s.lock, &s.unlock,
      &s.plus, &s.trash, &s.check, &s.edit, &s.tag, &s.swap, &s.repeat, &s.pin,
      &s.info, &s.moon, &s.keypad, &s.download, &s.upload, &s.close, &s.warn,
      &s.hard_drive, &s.eject, &s.usb, &s.disc, &s.play, &s.key, &s.gauge,
      &s.pulse, &s.unplug, &s.user,
  };
  for (auto* p : all) {
    if (*p) {
      cairo_surface_destroy(*p);
      *p = nullptr;
    }
  }
}

void paint_svg(cairo_t* cr, cairo_surface_t* s, double x, double y, double side,
               Tint tint) {
  if (!s || side <= 0) return;
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return;
  int w = cairo_image_surface_get_width(s);
  int h = cairo_image_surface_get_height(s);
  if (w <= 0 || h <= 0) return;
  double sc = side / double(std::max(w, h));
  cairo_save(cr);
  cairo_translate(cr, x + (side - w * sc) / 2.0, y + (side - h * sc) / 2.0);
  cairo_scale(cr, sc, sc);
  cairo_set_source_rgb(cr, tint.r, tint.g, tint.b);
  cairo_mask_surface(cr, s, 0, 0);
  cairo_restore(cr);
}

cairo_surface_t* menu_icon_for_glyph(const DiskIconSet& s, std::string_view g) {
  // Main menu
  if (g == "◎") return s.disc;
  if (g == "⤓") return s.download;
  if (g == "⤒") return s.upload;
  if (g == "⚙") return s.settings;
  if (g == "⌨") return s.keypad;
  if (g == "ⓘ") return s.info;
  // Drive menu
  if (g == "✕") return s.close;
  if (g == "♥") return s.pulse;
  if (g == "▦") return s.gauge;
  if (g == "☾") return s.moon;
  if (g == "⏏") return s.eject;
  if (g == "⎋") return s.unplug;
  if (g == "⏻") return s.power;
  // Block menu
  if (g == "▶") return s.play;
  if (g == "🔒") return s.lock;
  if (g == "🔓") return s.unlock;
  if (g == "＋") return s.plus;
  if (g == "✎") return s.edit;
  if (g == "🏷") return s.tag;
  if (g == "⇔") return s.swap;
  if (g == "✓") return s.check;
  if (g == "👤") return s.user;
  if (g == "📌") return s.pin;
  if (g == "⇄") return s.repeat;
  if (g == "🗑") return s.trash;
  if (g == "🔑" || g == "🔐") return s.key;
  return nullptr;
}

cairo_surface_t* drive_svg(const DiskIconSet& s, const Drive& d) {
  if (d.is_loop()) return s.disc;
  if (d.is_removable() || d.is_ejectable()) return s.usb;
  return s.hard_drive;
}

}  // namespace eh::disks::icons
