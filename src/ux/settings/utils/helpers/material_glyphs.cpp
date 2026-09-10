#include "ux/settings/utils/helpers/material_glyphs.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

void material_symbols_draw_glyph(cairo_t* cr, double cx, double cy, double px, const char* ligature, double r, double g,
                                 double b, double a) {
   
  eh::shell::draw_material_glyph(cr, cx, cy, px, ligature, r, g, b, a);
}

const char* dock_widget_material_ligature(const std::string& id) {
   
  if (id == "pinned_apps") return "push_pin";
  if (id == "running_apps") return "apps";
  if (eh::config::widget_token_is_system_tray(id)) return "notifications";
  if (id == "settings_button") return "settings";
  if (id == "distro_spotlight") return "search";
  if (id == "app_menu") return "apps";
  if (id == "app_drawer") return "widgets";
  if (eh::config::widget_implementation_type(id) == "smenu") return "menu";
  if (id == "launchpad") return "view_quilt";
  if (id == "clock") return "schedule";
  if (id == "weather") return "partly_cloudy_day";
  if (id == "weather_fancy") return "cloud";
  if (id == "calendar") return "calendar_month";
  if (id == "system_monitor") return "monitoring";
  if (id == "media") return "music_note";
  if (id == "media_compact") return "music_video";
  if (id == "workspaces") return "view_module";
  if (id == "world_clock") return "public";
  if (id == "control_center") return "tune";
  if (id == "notifications") return "notifications";
  if (id == "spacer" || eh::config::widget_implementation_type(id) == "spacer") return "horizontal_rule";
  if (id == "trash" || eh::config::widget_implementation_type(id) == "trash") return "delete";
  if (id == "volume_mixer" || eh::config::widget_implementation_type(id) == "volume_mixer") return "volume_up";
  if (id == "vpn" || eh::config::widget_implementation_type(id) == "vpn") return "vpn_key";
  if (id == "battery" || eh::config::widget_implementation_type(id) == "battery") return "battery_full";
  if (id == "bluetooth" || eh::config::widget_implementation_type(id) == "bluetooth") return "bluetooth";
  return "extension";
}
