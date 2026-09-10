#include "ux/settings/data/mango/settings_mango_data.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

namespace eh::settings_mango {

static std::string home_dir() {
   
  const char* h = std::getenv("HOME");
  return h ? h : "";
}

std::string mango_config_path() {
   
  return home_dir() + "/.config/mango/config.conf";
}

// Line classification.
enum class LineKind { Managed, ManagedPerKey, Source, Exec, Env, Bind, WindowRule, TagRule, MonitorRule, Comment, Blank, Other };

static bool is_managed_key(const std::string& key) {
   
  static const char* const kManagedKeys[] = {
    // decorations – border
    "borderpx", "border_radius", "border_radius_location_default",
    "no_border_when_single", "no_radius_when_single",
    "focused_opacity", "unfocused_opacity",
    // decorations – shadow
    "shadows", "layer_shadows", "shadow_only_floating",
    "shadows_size", "shadows_blur", "shadows_position_x", "shadows_position_y",
    "shadowscolor",
    // decorations – blur
    "blur", "blur_layer", "blur_optimized",
    "blur_params_radius", "blur_params_num_passes", "blur_params_noise",
    "blur_params_brightness", "blur_params_contrast", "blur_params_saturation",
    // colors
    "rootcolor", "bordercolor", "focuscolor", "urgentcolor",
    "maximizescreencolor", "scratchpadcolor", "globalcolor", "overlaycolor",
    "dropcolor", "splitcolor",
    // animations
    "animations", "layer_animations",
    "animation_type_open", "animation_type_close",
    "layer_animation_type_open", "layer_animation_type_close",
    "animation_fade_in", "animation_fade_out",
    "fadein_begin_opacity", "fadeout_begin_opacity",
    "zoom_initial_ratio", "zoom_end_ratio",
    "animation_duration_move", "animation_duration_open",
    "animation_duration_tag", "animation_duration_close", "animation_duration_focus",
    "animation_curve_open", "animation_curve_move", "animation_curve_tag",
    "animation_curve_close", "animation_curve_focus",
    "animation_curve_opafadein", "animation_curve_opafadeout",
    "tag_animation_direction",
    // layout
    "gappih", "gappiv", "gappoh", "gappov", "smartgaps",
    "new_is_master", "default_mfact", "default_nmaster", "default_smfact",
    "center_when_single_stack", "center_master_overspread",
    "scroller_structs", "scroller_default_proportion",
    "scroller_focus_center", "scroller_prefer_center",
    "scroller_prefer_overspread", "edge_scroller_pointer_focus",
    "scroller_default_proportion_single", "scroller_proportion_preset",
    "scroller_ignore_proportion_single",
    "dwindle_split_ratio", "dwindle_smart_split",
    "dwindle_hsplit", "dwindle_vsplit",
    "dwindle_preserve_split", "dwindle_smart_resize",
    "dwindle_drop_simple_split", "dwindle_manual_split",
    "circle_layout",
    // input – keyboard
    "repeat_rate", "repeat_delay", "numlockon",
    "xkb_rules_layout", "xkb_rules_variant", "xkb_rules_options",
    // input – mouse
    "mouse_natural_scrolling", "mouse_accel_profile", "mouse_accel_speed",
    "left_handed", "axis_scroll_factor",
    // input – trackpad
    "disable_trackpad", "tap_to_click", "tap_and_drag", "drag_lock",
    "trackpad_natural_scrolling", "trackpad_accel_profile", "trackpad_accel_speed",
    "scroll_button", "scroll_method", "click_method", "send_events_mode",
    "disable_while_typing", "middle_button_emulation",
    "swipe_min_threshold", "button_map", "trackpad_scroll_factor",
    // misc
    "focus_on_activate", "sloppyfocus", "warpcursor",
    "cursor_hide_timeout", "cursor_size", "cursor_theme",
    "enable_floating_snap", "snap_distance",
    "focus_cross_monitor", "exchange_cross_monitor", "focus_cross_tag",
    "allow_tearing", "xwayland_persistence", "syncobj_enable",
    "allow_lock_transparent", "allow_shortcuts_inhibit",
    "drag_tile_to_tile", "drag_tile_small", "drag_warp_cursor", "drag_corner",
    "axis_bind_apply_timeout", "view_current_to_back",
    "scratchpad_cross_monitor", "single_scratchpad",
    "idleinhibit_ignore_visible",
    "drag_tile_refresh_interval", "drag_floating_refresh_interval",
  };
  for (auto mk : kManagedKeys) {
    if (key == mk) return true;
  }
  return false;
}

// Trim helper.
static std::string trim(const std::string& s) {
   
  size_t b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}

// Parse a key=value line.
static bool parse_kv(const std::string& line, std::string& key, std::string& value) {
   
  size_t eq = line.find('=');
  if (eq == std::string::npos) return false;
  key = trim(line.substr(0, eq));
  value = trim(line.substr(eq + 1));
  return !key.empty();
}

// Parse an int from a value.
static int parse_int(const std::string& v, int fallback) {
   
  char* end = nullptr;
  long r = std::strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || *end != '\0') return fallback;
  return static_cast<int>(r);
}

static double parse_double(const std::string& v, double fallback) {
   
  char* end = nullptr;
  double r = std::strtod(v.c_str(), &end);
  if (end == v.c_str() || *end != '\0') return fallback;
  return r;
}

static bool parse_bool(const std::string& v, bool fallback) {
   
  int iv = parse_int(v, -1);
  if (iv == 0) return false;
  if (iv == 1) return true;
  if (v == "true" || v == "yes") return true;
  if (v == "false" || v == "no") return false;
  return fallback;
}

// Set a value in MangoConfig.
static void set_config_value(MangoConfig& cfg, const std::string& key, const std::string& value) {
   
  // Decorations.
  if (key == "borderpx")                    cfg.borderpx = parse_int(value, cfg.borderpx);
  else if (key == "border_radius")          cfg.border_radius = parse_int(value, cfg.border_radius);
  else if (key == "border_radius_location_default") cfg.border_radius_location_default = parse_int(value, cfg.border_radius_location_default);
  else if (key == "no_border_when_single")  cfg.no_border_when_single = parse_bool(value, cfg.no_border_when_single);
  else if (key == "no_radius_when_single")  cfg.no_radius_when_single = parse_bool(value, cfg.no_radius_when_single);
  else if (key == "focused_opacity")        cfg.focused_opacity = parse_double(value, cfg.focused_opacity);
  else if (key == "unfocused_opacity")      cfg.unfocused_opacity = parse_double(value, cfg.unfocused_opacity);

  // Shadows.
  else if (key == "shadows")                cfg.shadows = parse_bool(value, cfg.shadows);
  else if (key == "layer_shadows")          cfg.layer_shadows = parse_bool(value, cfg.layer_shadows);
  else if (key == "shadow_only_floating")   cfg.shadow_only_floating = parse_bool(value, cfg.shadow_only_floating);
  else if (key == "shadows_size")           cfg.shadows_size = parse_int(value, cfg.shadows_size);
  else if (key == "shadows_blur")           cfg.shadows_blur = parse_int(value, cfg.shadows_blur);
  else if (key == "shadows_position_x")     cfg.shadows_position_x = parse_int(value, cfg.shadows_position_x);
  else if (key == "shadows_position_y")     cfg.shadows_position_y = parse_int(value, cfg.shadows_position_y);
  else if (key == "shadowscolor")           cfg.shadowscolor = value;

  // Blur.
  else if (key == "blur")                    cfg.blur = parse_bool(value, cfg.blur);
  else if (key == "blur_layer")             cfg.blur_layer = parse_bool(value, cfg.blur_layer);
  else if (key == "blur_optimized")         cfg.blur_optimized = parse_bool(value, cfg.blur_optimized);
  else if (key == "blur_params_radius")     cfg.blur_params_radius = parse_int(value, cfg.blur_params_radius);
  else if (key == "blur_params_num_passes") cfg.blur_params_num_passes = parse_int(value, cfg.blur_params_num_passes);
  else if (key == "blur_params_noise")      cfg.blur_params_noise = parse_double(value, cfg.blur_params_noise);
  else if (key == "blur_params_brightness") cfg.blur_params_brightness = parse_double(value, cfg.blur_params_brightness);
  else if (key == "blur_params_contrast")   cfg.blur_params_contrast = parse_double(value, cfg.blur_params_contrast);
  else if (key == "blur_params_saturation") cfg.blur_params_saturation = parse_double(value, cfg.blur_params_saturation);

  // Colors.
  else if (key == "rootcolor")              cfg.rootcolor = value;
  else if (key == "bordercolor")            cfg.bordercolor = value;
  else if (key == "focuscolor")            cfg.focuscolor = value;
  else if (key == "urgentcolor")           cfg.urgentcolor = value;
  else if (key == "maximizescreencolor")   cfg.maximizescreencolor = value;
  else if (key == "scratchpadcolor")       cfg.scratchpadcolor = value;
  else if (key == "globalcolor")           cfg.globalcolor = value;
  else if (key == "overlaycolor")          cfg.overlaycolor = value;
  else if (key == "dropcolor")             cfg.dropcolor = value;
  else if (key == "splitcolor")            cfg.splitcolor = value;

  // Animations.
  else if (key == "animations")             cfg.animations = parse_bool(value, cfg.animations);
  else if (key == "layer_animations")       cfg.layer_animations = parse_bool(value, cfg.layer_animations);
  else if (key == "animation_type_open")    cfg.animation_type_open = value;
  else if (key == "animation_type_close")   cfg.animation_type_close = value;
  else if (key == "layer_animation_type_open")  cfg.layer_animation_type_open = value;
  else if (key == "layer_animation_type_close") cfg.layer_animation_type_close = value;
  else if (key == "animation_fade_in")      cfg.animation_fade_in = parse_bool(value, cfg.animation_fade_in);
  else if (key == "animation_fade_out")     cfg.animation_fade_out = parse_bool(value, cfg.animation_fade_out);
  else if (key == "fadein_begin_opacity")   cfg.fadein_begin_opacity = parse_double(value, cfg.fadein_begin_opacity);
  else if (key == "fadeout_begin_opacity")  cfg.fadeout_begin_opacity = parse_double(value, cfg.fadeout_begin_opacity);
  else if (key == "zoom_initial_ratio")     cfg.zoom_initial_ratio = parse_double(value, cfg.zoom_initial_ratio);
  else if (key == "zoom_end_ratio")         cfg.zoom_end_ratio = parse_double(value, cfg.zoom_end_ratio);
  else if (key == "animation_duration_move") cfg.animation_duration_move = parse_int(value, cfg.animation_duration_move);
  else if (key == "animation_duration_open") cfg.animation_duration_open = parse_int(value, cfg.animation_duration_open);
  else if (key == "animation_duration_tag") cfg.animation_duration_tag = parse_int(value, cfg.animation_duration_tag);
  else if (key == "animation_duration_close") cfg.animation_duration_close = parse_int(value, cfg.animation_duration_close);
  else if (key == "animation_duration_focus") cfg.animation_duration_focus = parse_int(value, cfg.animation_duration_focus);
  else if (key == "animation_curve_open")    cfg.animation_curve_open = value;
  else if (key == "animation_curve_move")    cfg.animation_curve_move = value;
  else if (key == "animation_curve_tag")     cfg.animation_curve_tag = value;
  else if (key == "animation_curve_close")   cfg.animation_curve_close = value;
  else if (key == "animation_curve_focus")   cfg.animation_curve_focus = value;
  else if (key == "animation_curve_opafadein") cfg.animation_curve_opafadein = value;
  else if (key == "animation_curve_opafadeout") cfg.animation_curve_opafadeout = value;
  else if (key == "tag_animation_direction") cfg.tag_animation_direction = parse_int(value, cfg.tag_animation_direction);

  // Layout.
  else if (key == "gappih")                 cfg.gappih = parse_int(value, cfg.gappih);
  else if (key == "gappiv")                 cfg.gappiv = parse_int(value, cfg.gappiv);
  else if (key == "gappoh")                 cfg.gappoh = parse_int(value, cfg.gappoh);
  else if (key == "gappov")                 cfg.gappov = parse_int(value, cfg.gappov);
  else if (key == "smartgaps")              cfg.smartgaps = parse_bool(value, cfg.smartgaps);
  else if (key == "new_is_master")          cfg.new_is_master = parse_bool(value, cfg.new_is_master);
  else if (key == "default_mfact")          cfg.default_mfact = parse_double(value, cfg.default_mfact);
  else if (key == "default_nmaster")        cfg.default_nmaster = parse_int(value, cfg.default_nmaster);
  else if (key == "default_smfact")         cfg.default_smfact = parse_double(value, cfg.default_smfact);
  else if (key == "center_when_single_stack")  cfg.center_when_single_stack = parse_bool(value, cfg.center_when_single_stack);
  else if (key == "center_master_overspread")  cfg.center_master_overspread = parse_bool(value, cfg.center_master_overspread);
  else if (key == "scroller_structs")       cfg.scroller_structs = parse_int(value, cfg.scroller_structs);
  else if (key == "scroller_default_proportion")  cfg.scroller_default_proportion = parse_double(value, cfg.scroller_default_proportion);
  else if (key == "scroller_focus_center")  cfg.scroller_focus_center = parse_bool(value, cfg.scroller_focus_center);
  else if (key == "scroller_prefer_center") cfg.scroller_prefer_center = parse_bool(value, cfg.scroller_prefer_center);
  else if (key == "scroller_prefer_overspread") cfg.scroller_prefer_overspread = parse_bool(value, cfg.scroller_prefer_overspread);
  else if (key == "edge_scroller_pointer_focus") cfg.edge_scroller_pointer_focus = parse_bool(value, cfg.edge_scroller_pointer_focus);
  else if (key == "scroller_default_proportion_single") cfg.scroller_default_proportion_single = parse_double(value, cfg.scroller_default_proportion_single);
  else if (key == "scroller_proportion_preset") cfg.scroller_proportion_preset = value;
  else if (key == "scroller_ignore_proportion_single") cfg.scroller_ignore_proportion_single = parse_bool(value, cfg.scroller_ignore_proportion_single);
  else if (key == "dwindle_split_ratio")    cfg.dwindle_split_ratio = parse_double(value, cfg.dwindle_split_ratio);
  else if (key == "dwindle_smart_split")    cfg.dwindle_smart_split = parse_bool(value, cfg.dwindle_smart_split);
  else if (key == "dwindle_hsplit")         cfg.dwindle_hsplit = parse_int(value, cfg.dwindle_hsplit);
  else if (key == "dwindle_vsplit")         cfg.dwindle_vsplit = parse_int(value, cfg.dwindle_vsplit);
  else if (key == "dwindle_preserve_split") cfg.dwindle_preserve_split = parse_bool(value, cfg.dwindle_preserve_split);
  else if (key == "dwindle_smart_resize")   cfg.dwindle_smart_resize = parse_bool(value, cfg.dwindle_smart_resize);
  else if (key == "dwindle_drop_simple_split") cfg.dwindle_drop_simple_split = parse_bool(value, cfg.dwindle_drop_simple_split);
  else if (key == "dwindle_manual_split")   cfg.dwindle_manual_split = parse_bool(value, cfg.dwindle_manual_split);
  else if (key == "circle_layout")          cfg.circle_layout = value;

  // Input – keyboard.
  else if (key == "repeat_rate")            cfg.repeat_rate = parse_int(value, cfg.repeat_rate);
  else if (key == "repeat_delay")           cfg.repeat_delay = parse_int(value, cfg.repeat_delay);
  else if (key == "numlockon")              cfg.numlockon = parse_bool(value, cfg.numlockon);
  else if (key == "xkb_rules_layout")       cfg.xkb_rules_layout = value;
  else if (key == "xkb_rules_variant")      cfg.xkb_rules_variant = value;
  else if (key == "xkb_rules_options")      cfg.xkb_rules_options = value;

  // Input – mouse.
  else if (key == "mouse_natural_scrolling")  cfg.mouse_natural_scrolling = parse_bool(value, cfg.mouse_natural_scrolling);
  else if (key == "mouse_accel_profile")    cfg.mouse_accel_profile = parse_int(value, cfg.mouse_accel_profile);
  else if (key == "mouse_accel_speed")      cfg.mouse_accel_speed = parse_double(value, cfg.mouse_accel_speed);
  else if (key == "left_handed")            cfg.left_handed = parse_bool(value, cfg.left_handed);
  else if (key == "axis_scroll_factor")     cfg.axis_scroll_factor = parse_double(value, cfg.axis_scroll_factor);

  // Input – trackpad.
  else if (key == "disable_trackpad")       cfg.disable_trackpad = parse_bool(value, cfg.disable_trackpad);
  else if (key == "tap_to_click")           cfg.tap_to_click = parse_bool(value, cfg.tap_to_click);
  else if (key == "tap_and_drag")           cfg.tap_and_drag = parse_bool(value, cfg.tap_and_drag);
  else if (key == "drag_lock")              cfg.drag_lock = parse_bool(value, cfg.drag_lock);
  else if (key == "trackpad_natural_scrolling")  cfg.trackpad_natural_scrolling = parse_bool(value, cfg.trackpad_natural_scrolling);
  else if (key == "trackpad_accel_profile") cfg.trackpad_accel_profile = parse_int(value, cfg.trackpad_accel_profile);
  else if (key == "trackpad_accel_speed")   cfg.trackpad_accel_speed = parse_double(value, cfg.trackpad_accel_speed);
  else if (key == "scroll_button")          cfg.scroll_button = parse_int(value, cfg.scroll_button);
  else if (key == "scroll_method")          cfg.scroll_method = parse_int(value, cfg.scroll_method);
  else if (key == "click_method")           cfg.click_method = parse_int(value, cfg.click_method);
  else if (key == "send_events_mode")       cfg.send_events_mode = parse_int(value, cfg.send_events_mode);
  else if (key == "disable_while_typing")   cfg.disable_while_typing = parse_bool(value, cfg.disable_while_typing);
  else if (key == "middle_button_emulation") cfg.middle_button_emulation = parse_bool(value, cfg.middle_button_emulation);
  else if (key == "swipe_min_threshold")    cfg.swipe_min_threshold = parse_int(value, cfg.swipe_min_threshold);
  else if (key == "button_map")             cfg.button_map = parse_int(value, cfg.button_map);
  else if (key == "trackpad_scroll_factor") cfg.trackpad_scroll_factor = parse_double(value, cfg.trackpad_scroll_factor);

  // Misc.
  else if (key == "focus_on_activate")      cfg.focus_on_activate = parse_bool(value, cfg.focus_on_activate);
  else if (key == "sloppyfocus")            cfg.sloppyfocus = parse_bool(value, cfg.sloppyfocus);
  else if (key == "warpcursor")             cfg.warpcursor = parse_bool(value, cfg.warpcursor);
  else if (key == "cursor_hide_timeout")    cfg.cursor_hide_timeout = parse_int(value, cfg.cursor_hide_timeout);
  else if (key == "cursor_size")            cfg.cursor_size = parse_int(value, cfg.cursor_size);
  else if (key == "cursor_theme")           cfg.cursor_theme = value;
  else if (key == "enable_floating_snap")   cfg.enable_floating_snap = parse_bool(value, cfg.enable_floating_snap);
  else if (key == "snap_distance")          cfg.snap_distance = parse_int(value, cfg.snap_distance);
  else if (key == "focus_cross_monitor")    cfg.focus_cross_monitor = parse_bool(value, cfg.focus_cross_monitor);
  else if (key == "exchange_cross_monitor") cfg.exchange_cross_monitor = parse_bool(value, cfg.exchange_cross_monitor);
  else if (key == "focus_cross_tag")        cfg.focus_cross_tag = parse_bool(value, cfg.focus_cross_tag);
  else if (key == "allow_tearing")          cfg.allow_tearing = parse_int(value, cfg.allow_tearing);
  else if (key == "xwayland_persistence")   cfg.xwayland_persistence = parse_bool(value, cfg.xwayland_persistence);
  else if (key == "syncobj_enable")         cfg.syncobj_enable = parse_bool(value, cfg.syncobj_enable);
  else if (key == "allow_lock_transparent") cfg.allow_lock_transparent = parse_bool(value, cfg.allow_lock_transparent);
  else if (key == "allow_shortcuts_inhibit") cfg.allow_shortcuts_inhibit = parse_bool(value, cfg.allow_shortcuts_inhibit);
  else if (key == "drag_tile_to_tile")      cfg.drag_tile_to_tile = parse_bool(value, cfg.drag_tile_to_tile);
  else if (key == "drag_tile_small")        cfg.drag_tile_small = parse_bool(value, cfg.drag_tile_small);
  else if (key == "drag_warp_cursor")       cfg.drag_warp_cursor = parse_bool(value, cfg.drag_warp_cursor);
  else if (key == "drag_corner")            cfg.drag_corner = parse_int(value, cfg.drag_corner);
  else if (key == "axis_bind_apply_timeout") cfg.axis_bind_apply_timeout = parse_int(value, cfg.axis_bind_apply_timeout);
  else if (key == "view_current_to_back")   cfg.view_current_to_back = parse_bool(value, cfg.view_current_to_back);
  else if (key == "scratchpad_cross_monitor") cfg.scratchpad_cross_monitor = parse_bool(value, cfg.scratchpad_cross_monitor);
  else if (key == "single_scratchpad")      cfg.single_scratchpad = parse_bool(value, cfg.single_scratchpad);
  else if (key == "idleinhibit_ignore_visible")  cfg.idleinhibit_ignore_visible = parse_bool(value, cfg.idleinhibit_ignore_visible);
  else if (key == "drag_tile_refresh_interval")  cfg.drag_tile_refresh_interval = parse_double(value, cfg.drag_tile_refresh_interval);
  else if (key == "drag_floating_refresh_interval") cfg.drag_floating_refresh_interval = parse_double(value, cfg.drag_floating_refresh_interval);
}

// Read config (main + sourced files).
MangoConfigFile read_mango_config() {
   
  MangoConfigFile mcf;
  std::string path = mango_config_path();
  std::ifstream f(path);
  if (!f) {
    mcf.valid = true; // no file yet — use defaults
    return mcf;
  }

  std::vector<std::string> source_paths;
  std::string line;

  // First pass: read main config, preserve its lines for round-trip,
  // collect source= paths, and parse any inline managed keys.
  while (std::getline(f, line)) {
    mcf.lines.push_back(line);
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    std::string lower = trimmed;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Collect source= / source-optional= paths for later processing
    if (lower.rfind("source-optional", 0) == 0 || lower.rfind("source", 0) == 0) {
      size_t eq = trimmed.find('=');
      if (eq != std::string::npos) {
        std::string src_path = trim(trimmed.substr(eq + 1));
        if (!src_path.empty() && src_path[0] == '~')
          src_path = home_dir() + src_path.substr(1);
        source_paths.push_back(src_path);
      }
      continue;
    }

    // Skip other non-managed line types (exec, env, bind, rules, etc.)
    if (lower.rfind("exec", 0) == 0 ||
        lower.rfind("env=", 0) == 0 ||
        lower.rfind("bind", 0) == 0 ||
        lower.rfind("windowrule", 0) == 0 ||
        lower.rfind("tagrule", 0) == 0 ||
        lower.rfind("monitorrule", 0) == 0 ||
        lower.rfind("keymode", 0) == 0 ||
        lower.rfind("layerrule", 0) == 0) {
       
      continue;
    }

    // Parse managed key=value from main config
    std::string key, value;
    if (parse_kv(trimmed, key, value)) {
      if (is_managed_key(key)) {
        set_config_value(mcf.cfg, key, value);
      }
    }
  }

  // Second pass: read managed keys from sourced files.
  // Each sourced file's values can override the main config values
  // (mirroring MangoWM's top-down override behavior).
  for (const auto& src_path : source_paths) {
    std::ifstream sf(src_path);
    if (!sf) continue;
    while (std::getline(sf, line)) {
      std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed[0] == '#') continue;
      std::string key, value;
      if (parse_kv(trimmed, key, value)) {
        if (is_managed_key(key)) {
          set_config_value(mcf.cfg, key, value);
        }
      }
    }
  }

  // Third pass: read settings-ui.conf (may not yet be in source= list on first run).
  // Its values override everything, matching its last-sourced position.
  {
    std::string ui_path = home_dir() + "/.config/mango/hyprmango/settings-ui.conf";
    std::ifstream sf(ui_path);
    if (sf) {
      while (std::getline(sf, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        std::string key, value;
        if (parse_kv(trimmed, key, value)) {
          if (is_managed_key(key)) {
            set_config_value(mcf.cfg, key, value);
          }
        }
      }
    }
  }

  mcf.valid = true;
  return mcf;
}

// Convert config back to key=value lines.
void config_to_lines(const MangoConfig& cfg, std::vector<std::string>& out_lines) {
   
  auto add = [&](const std::string& k, const auto& v) {
    std::ostringstream oss;
    oss << k << "=" << v;
    out_lines.push_back(oss.str());
  };
  auto add_bool = [&](const std::string& k, bool v) {
    out_lines.push_back(k + "=" + (v ? "1" : "0"));
  };
  auto add_dbl = [&](const std::string& k, double v) {
    std::ostringstream oss;
    oss << k << "=" << v;
    out_lines.push_back(oss.str());
  };

  // Decorations
  add("borderpx", cfg.borderpx);
  add("border_radius", cfg.border_radius);
  add("border_radius_location_default", cfg.border_radius_location_default);
  add_bool("no_border_when_single", cfg.no_border_when_single);
  add_bool("no_radius_when_single", cfg.no_radius_when_single);
  add_dbl("focused_opacity", cfg.focused_opacity);
  add_dbl("unfocused_opacity", cfg.unfocused_opacity);

  // Shadows
  add_bool("shadows", cfg.shadows);
  add_bool("layer_shadows", cfg.layer_shadows);
  add_bool("shadow_only_floating", cfg.shadow_only_floating);
  add("shadows_size", cfg.shadows_size);
  add("shadows_blur", cfg.shadows_blur);
  add("shadows_position_x", cfg.shadows_position_x);
  add("shadows_position_y", cfg.shadows_position_y);
  add("shadowscolor", cfg.shadowscolor);

  // Blur
  add_bool("blur", cfg.blur);
  add_bool("blur_layer", cfg.blur_layer);
  add_bool("blur_optimized", cfg.blur_optimized);
  add("blur_params_radius", cfg.blur_params_radius);
  add("blur_params_num_passes", cfg.blur_params_num_passes);
  add_dbl("blur_params_noise", cfg.blur_params_noise);
  add_dbl("blur_params_brightness", cfg.blur_params_brightness);
  add_dbl("blur_params_contrast", cfg.blur_params_contrast);
  add_dbl("blur_params_saturation", cfg.blur_params_saturation);

  // Colors
  add("rootcolor", cfg.rootcolor);
  add("bordercolor", cfg.bordercolor);
  add("focuscolor", cfg.focuscolor);
  add("urgentcolor", cfg.urgentcolor);
  add("maximizescreencolor", cfg.maximizescreencolor);
  add("scratchpadcolor", cfg.scratchpadcolor);
  add("globalcolor", cfg.globalcolor);
  add("overlaycolor", cfg.overlaycolor);
  add("dropcolor", cfg.dropcolor);
  add("splitcolor", cfg.splitcolor);

  // Animations
  add_bool("animations", cfg.animations);
  add_bool("layer_animations", cfg.layer_animations);
  add("animation_type_open", cfg.animation_type_open);
  add("animation_type_close", cfg.animation_type_close);
  add("layer_animation_type_open", cfg.layer_animation_type_open);
  add("layer_animation_type_close", cfg.layer_animation_type_close);
  add_bool("animation_fade_in", cfg.animation_fade_in);
  add_bool("animation_fade_out", cfg.animation_fade_out);
  add_dbl("fadein_begin_opacity", cfg.fadein_begin_opacity);
  add_dbl("fadeout_begin_opacity", cfg.fadeout_begin_opacity);
  add_dbl("zoom_initial_ratio", cfg.zoom_initial_ratio);
  add_dbl("zoom_end_ratio", cfg.zoom_end_ratio);
  add("animation_duration_move", cfg.animation_duration_move);
  add("animation_duration_open", cfg.animation_duration_open);
  add("animation_duration_tag", cfg.animation_duration_tag);
  add("animation_duration_close", cfg.animation_duration_close);
  add("animation_duration_focus", cfg.animation_duration_focus);
  add("animation_curve_open", cfg.animation_curve_open);
  add("animation_curve_move", cfg.animation_curve_move);
  add("animation_curve_tag", cfg.animation_curve_tag);
  add("animation_curve_close", cfg.animation_curve_close);
  add("animation_curve_focus", cfg.animation_curve_focus);
  add("animation_curve_opafadein", cfg.animation_curve_opafadein);
  add("animation_curve_opafadeout", cfg.animation_curve_opafadeout);
  add("tag_animation_direction", cfg.tag_animation_direction);

  // Layout
  add("gappih", cfg.gappih);
  add("gappiv", cfg.gappiv);
  add("gappoh", cfg.gappoh);
  add("gappov", cfg.gappov);
  add_bool("smartgaps", cfg.smartgaps);
  add_bool("new_is_master", cfg.new_is_master);
  add_dbl("default_mfact", cfg.default_mfact);
  add("default_nmaster", cfg.default_nmaster);
  add_dbl("default_smfact", cfg.default_smfact);
  add_bool("center_when_single_stack", cfg.center_when_single_stack);
  add_bool("center_master_overspread", cfg.center_master_overspread);
  add("scroller_structs", cfg.scroller_structs);
  add_dbl("scroller_default_proportion", cfg.scroller_default_proportion);
  add_bool("scroller_focus_center", cfg.scroller_focus_center);
  add_bool("scroller_prefer_center", cfg.scroller_prefer_center);
  add_bool("scroller_prefer_overspread", cfg.scroller_prefer_overspread);
  add_bool("edge_scroller_pointer_focus", cfg.edge_scroller_pointer_focus);
  add_dbl("scroller_default_proportion_single", cfg.scroller_default_proportion_single);
  add("scroller_proportion_preset", cfg.scroller_proportion_preset);
  add_bool("scroller_ignore_proportion_single", cfg.scroller_ignore_proportion_single);
  add_dbl("dwindle_split_ratio", cfg.dwindle_split_ratio);
  add_bool("dwindle_smart_split", cfg.dwindle_smart_split);
  add("dwindle_hsplit", cfg.dwindle_hsplit);
  add("dwindle_vsplit", cfg.dwindle_vsplit);
  add_bool("dwindle_preserve_split", cfg.dwindle_preserve_split);
  add_bool("dwindle_smart_resize", cfg.dwindle_smart_resize);
  add_bool("dwindle_drop_simple_split", cfg.dwindle_drop_simple_split);
  add_bool("dwindle_manual_split", cfg.dwindle_manual_split);
  if (!cfg.circle_layout.empty()) add("circle_layout", cfg.circle_layout);

  // Input – keyboard
  add("repeat_rate", cfg.repeat_rate);
  add("repeat_delay", cfg.repeat_delay);
  add_bool("numlockon", cfg.numlockon);
  if (!cfg.xkb_rules_layout.empty()) add("xkb_rules_layout", cfg.xkb_rules_layout);
  if (!cfg.xkb_rules_variant.empty()) add("xkb_rules_variant", cfg.xkb_rules_variant);
  if (!cfg.xkb_rules_options.empty()) add("xkb_rules_options", cfg.xkb_rules_options);

  // Input – mouse
  add_bool("mouse_natural_scrolling", cfg.mouse_natural_scrolling);
  add("mouse_accel_profile", cfg.mouse_accel_profile);
  add_dbl("mouse_accel_speed", cfg.mouse_accel_speed);
  add_bool("left_handed", cfg.left_handed);
  add_dbl("axis_scroll_factor", cfg.axis_scroll_factor);

  // Input – trackpad
  add_bool("disable_trackpad", cfg.disable_trackpad);
  add_bool("tap_to_click", cfg.tap_to_click);
  add_bool("tap_and_drag", cfg.tap_and_drag);
  add_bool("drag_lock", cfg.drag_lock);
  add_bool("trackpad_natural_scrolling", cfg.trackpad_natural_scrolling);
  add("trackpad_accel_profile", cfg.trackpad_accel_profile);
  add_dbl("trackpad_accel_speed", cfg.trackpad_accel_speed);
  add("scroll_button", cfg.scroll_button);
  add("scroll_method", cfg.scroll_method);
  add("click_method", cfg.click_method);
  add("send_events_mode", cfg.send_events_mode);
  add_bool("disable_while_typing", cfg.disable_while_typing);
  add_bool("middle_button_emulation", cfg.middle_button_emulation);
  add("swipe_min_threshold", cfg.swipe_min_threshold);
  add("button_map", cfg.button_map);
  add_dbl("trackpad_scroll_factor", cfg.trackpad_scroll_factor);

  // Misc
  add_bool("focus_on_activate", cfg.focus_on_activate);
  add_bool("sloppyfocus", cfg.sloppyfocus);
  add_bool("warpcursor", cfg.warpcursor);
  add("cursor_hide_timeout", cfg.cursor_hide_timeout);
  add("cursor_size", cfg.cursor_size);
  if (!cfg.cursor_theme.empty()) add("cursor_theme", cfg.cursor_theme);
  add_bool("enable_floating_snap", cfg.enable_floating_snap);
  add("snap_distance", cfg.snap_distance);
  add_bool("focus_cross_monitor", cfg.focus_cross_monitor);
  add_bool("exchange_cross_monitor", cfg.exchange_cross_monitor);
  add_bool("focus_cross_tag", cfg.focus_cross_tag);
  add("allow_tearing", cfg.allow_tearing);
  add_bool("xwayland_persistence", cfg.xwayland_persistence);
  add_bool("syncobj_enable", cfg.syncobj_enable);
  add_bool("allow_lock_transparent", cfg.allow_lock_transparent);
  add_bool("allow_shortcuts_inhibit", cfg.allow_shortcuts_inhibit);
  add_bool("drag_tile_to_tile", cfg.drag_tile_to_tile);
  add_bool("drag_tile_small", cfg.drag_tile_small);
  add_bool("drag_warp_cursor", cfg.drag_warp_cursor);
  add("drag_corner", cfg.drag_corner);
  add("axis_bind_apply_timeout", cfg.axis_bind_apply_timeout);
  add_bool("view_current_to_back", cfg.view_current_to_back);
  add_bool("scratchpad_cross_monitor", cfg.scratchpad_cross_monitor);
  add_bool("single_scratchpad", cfg.single_scratchpad);
  add_bool("idleinhibit_ignore_visible", cfg.idleinhibit_ignore_visible);
  add_dbl("drag_tile_refresh_interval", cfg.drag_tile_refresh_interval);
  add_dbl("drag_floating_refresh_interval", cfg.drag_floating_refresh_interval);
}

// Write config.
// Managed settings are written to hyprmango/settings-ui.conf, which is sourced
// LAST by config.conf so the UI's values always take priority.
bool write_mango_config(const MangoConfigFile& mcf) {
   
  std::string config_path = mango_config_path();
  std::string ui_conf_rel = "source=~/.config/mango/hyprmango/settings-ui.conf";
  std::string ui_conf_abs = home_dir() + "/.config/mango/hyprmango/settings-ui.conf";
  std::string ui_dir = home_dir() + "/.config/mango/hyprmango";

  // Build managed settings lines
  std::vector<std::string> managed_lines;
  config_to_lines(mcf.cfg, managed_lines);

  std::unordered_map<std::string, bool> managed_keys;
  for (auto& ml : managed_lines) {
    std::string key, val;
    if (parse_kv(ml, key, val)) {
      managed_keys[key] = true;
    }
  }

  // 1. Write managed settings to the dedicated override file
  ::mkdir(ui_dir.c_str(), 0755);
  {
    std::ofstream out(ui_conf_abs, std::ios::trunc);
    if (!out) return false;
    out << "# Event-Horizon Settings UI\n";
    out << "# Auto-generated — edits will be overwritten.\n\n";
    for (auto& ml : managed_lines) {
      out << ml << '\n';
    }
  }

  // 2. Rewrite config.conf preserving all non-managed lines and
  //    ensuring the settings-ui source line exists at the end.
  bool has_ui_source = false;
  std::ifstream fin(config_path);
  std::vector<std::string> config_lines;

  if (fin) {
    std::string line;
    while (std::getline(fin, line)) {
      std::string trimmed = trim(line);

      // Check if this line already sources settings-ui.conf
      std::string lowered = trimmed;
      for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lowered.rfind("source-optional", 0) == 0 || lowered.rfind("source", 0) == 0) {
        size_t eq = trimmed.find('=');
        if (eq != std::string::npos) {
          std::string val = trim(trimmed.substr(eq + 1));
          if (val.find("settings-ui.conf") != std::string::npos) {
            has_ui_source = true;
          }
        }
      }

      // Skip inline managed key=value lines (now in settings-ui.conf)
      if (!trimmed.empty() && trimmed[0] != '#') {
        std::string key, val;
        if (parse_kv(trimmed, key, val) && managed_keys.count(key)) {
          continue;
        }
      }

      config_lines.push_back(line);
    }
  }

  std::ofstream out(config_path, std::ios::trunc);
  if (!out) return false;

  for (auto& line : config_lines) {
    out << line << '\n';
  }

  // Append the source line at the end (highest priority in MangoWM)
  if (!has_ui_source) {
    out << '\n' << ui_conf_rel << '\n';
  }

  out.close();
  return true;
}

bool apply_mango_config() {
   
  int r = std::system("mmsg -d reload_config 2>/dev/null");
  return r == 0;
}

} // namespace eh::settings_mango
