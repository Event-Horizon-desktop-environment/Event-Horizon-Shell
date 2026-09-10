#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eh::settings_mango {

// Mango config keys.
struct MangoConfig {
  // Decorations – border
  int borderpx = 4;
  int border_radius = 0;
  int border_radius_location_default = 0;
  bool no_border_when_single = false;
  bool no_radius_when_single = false;
  double focused_opacity = 1.0;
  double unfocused_opacity = 1.0;

  // Decorations – shadow
  bool shadows = false;
  bool layer_shadows = false;
  bool shadow_only_floating = true;
  int shadows_size = 10;
  int shadows_blur = 15;
  int shadows_position_x = 0;
  int shadows_position_y = 0;
  std::string shadowscolor = "0x000000ff";

  // Decorations – blur
  bool blur = false;
  bool blur_layer = false;
  bool blur_optimized = true;
  int blur_params_radius = 5;
  int blur_params_num_passes = 2;
  double blur_params_noise = 0.02;
  double blur_params_brightness = 0.9;
  double blur_params_contrast = 0.9;
  double blur_params_saturation = 1.2;

  // Colors
  std::string rootcolor = "0x323232ff";
  std::string bordercolor = "0x444444ff";
  std::string focuscolor = "0xc66b25ff";
  std::string urgentcolor = "0xad401fff";
  std::string maximizescreencolor = "0x89aa61ff";
  std::string scratchpadcolor = "0x516c93ff";
  std::string globalcolor = "0xb153a7ff";
  std::string overlaycolor = "0x14a57cff";
  std::string dropcolor = "0x8FBA7C55";
  std::string splitcolor = "0xEB441EFF";

  // Animations
  bool animations = true;
  bool layer_animations = true;
  std::string animation_type_open = "slide";
  std::string animation_type_close = "slide";
  std::string layer_animation_type_open = "slide";
  std::string layer_animation_type_close = "slide";
  bool animation_fade_in = true;
  bool animation_fade_out = true;
  double fadein_begin_opacity = 0.5;
  double fadeout_begin_opacity = 0.5;
  double zoom_initial_ratio = 0.4;
  double zoom_end_ratio = 0.8;
  int animation_duration_move = 500;
  int animation_duration_open = 400;
  int animation_duration_tag = 300;
  int animation_duration_close = 300;
  int animation_duration_focus = 0;
  std::string animation_curve_open = "0.46,1.0,0.29,0.99";
  std::string animation_curve_move = "0.46,1.0,0.29,0.99";
  std::string animation_curve_tag = "0.46,1.0,0.29,0.99";
  std::string animation_curve_close = "0.46,1.0,0.29,0.99";
  std::string animation_curve_focus = "0.46,1.0,0.29,0.99";
  std::string animation_curve_opafadein = "0.46,1.0,0.29,0.99";
  std::string animation_curve_opafadeout = "0.5,0.5,0.5,0.5";
  int tag_animation_direction = 1;

  // Layout
  int gappih = 5;
  int gappiv = 5;
  int gappoh = 10;
  int gappov = 10;
  bool smartgaps = false;
  bool new_is_master = true;
  double default_mfact = 0.55;
  int default_nmaster = 1;
  double default_smfact = 0.5;
  bool center_when_single_stack = true;
  bool center_master_overspread = false;
  int scroller_structs = 20;
  double scroller_default_proportion = 0.9;
  bool scroller_focus_center = false;
  bool scroller_prefer_center = false;
  bool scroller_prefer_overspread = true;
  bool edge_scroller_pointer_focus = true;
  double scroller_default_proportion_single = 1.0;
  std::string scroller_proportion_preset = "0.5,0.8,1.0";
  bool scroller_ignore_proportion_single = true;
  double dwindle_split_ratio = 0.5;
  bool dwindle_smart_split = false;
  int dwindle_hsplit = 1;
  int dwindle_vsplit = 1;
  bool dwindle_preserve_split = false;
  bool dwindle_smart_resize = false;
  bool dwindle_drop_simple_split = true;
  bool dwindle_manual_split = false;
  std::string circle_layout;

  // Input – keyboard
  int repeat_rate = 25;
  int repeat_delay = 600;
  bool numlockon = false;
  std::string xkb_rules_layout;
  std::string xkb_rules_variant;
  std::string xkb_rules_options;

  // Input – mouse
  bool mouse_natural_scrolling = false;
  int mouse_accel_profile = 2;
  double mouse_accel_speed = 0.0;
  bool left_handed = false;
  double axis_scroll_factor = 1.0;

  // Input – trackpad
  bool disable_trackpad = false;
  bool tap_to_click = true;
  bool tap_and_drag = true;
  bool drag_lock = true;
  bool trackpad_natural_scrolling = false;
  int trackpad_accel_profile = 2;
  double trackpad_accel_speed = 0.0;
  int scroll_button = 274;
  int scroll_method = 1;
  int click_method = 1;
  int send_events_mode = 0;
  bool disable_while_typing = true;
  bool middle_button_emulation = false;
  int swipe_min_threshold = 1;
  int button_map = 0;
  double trackpad_scroll_factor = 1.0;

  // Misc
  bool focus_on_activate = true;
  bool sloppyfocus = true;
  bool warpcursor = true;
  int cursor_hide_timeout = 0;
  int cursor_size = 24;
  std::string cursor_theme;
  bool enable_floating_snap = false;
  int snap_distance = 30;
  bool focus_cross_monitor = false;
  bool exchange_cross_monitor = false;
  bool focus_cross_tag = false;
  int allow_tearing = 0;
  bool xwayland_persistence = true;
  bool syncobj_enable = false;
  bool allow_lock_transparent = false;
  bool allow_shortcuts_inhibit = true;
  bool drag_tile_to_tile = false;
  bool drag_tile_small = true;
  bool drag_warp_cursor = true;
  int drag_corner = 3;
  int axis_bind_apply_timeout = 100;
  bool view_current_to_back = false;
  bool scratchpad_cross_monitor = false;
  bool single_scratchpad = true;
  bool idleinhibit_ignore_visible = false;
  double drag_tile_refresh_interval = 8.0;
  double drag_floating_refresh_interval = 8.0;

  // Keybinds (parsed from file, display-only in UI)
  std::vector<std::string> keybind_lines;
};

// Config file representation.
struct MangoConfigFile {
  MangoConfig cfg;
  std::vector<std::string> lines; // full file lines (preserves comments, includes, blank lines)
  bool valid = false;
};

MangoConfigFile read_mango_config();
bool write_mango_config(const MangoConfigFile& mcf);
bool apply_mango_config();
std::string mango_config_path();

// Converts MangoConfig to key=value lines (for keys that belong in main config,
// e.g. not source=, exec, env, bind, windowrule, tagrule, monitorrule, etc.)
void config_to_lines(const MangoConfig& cfg, std::vector<std::string>& out_lines);

} // namespace eh::settings_mango
