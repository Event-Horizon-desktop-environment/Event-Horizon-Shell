#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#pragma GCC diagnostic pop

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <set>

#include <sys/stat.h>

namespace eh::settings_hyprland {

namespace fs = std::filesystem;

std::string hyprland_config_dir() {
   
  const char* home = std::getenv("HOME");
  if (!home) return "/tmp";
  return std::string(home) + "/.config/hypr/hyprland";
}

static std::string file_path(const std::string& name) {
  return hyprland_config_dir() + "/" + name + ".lua";
}

// Helpers.

static std::string trim(std::string s) {
  auto notspace = [](char c) { return c != ' ' && c != '\t'; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
  return s;
}

static std::vector<std::pair<std::string, std::string>> parse_lua_kv(const std::string& path) {
  std::vector<std::pair<std::string, std::string>> out;
  std::ifstream f(path);
  if (!f.is_open()) return out;

  std::vector<std::string> stack;
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line.starts_with("--") || line.starts_with("//")) continue;

    int open = 0, close = 0;
    for (char c : line) { if (c == '{') ++open; if (c == '}') ++close; }

    for (int i = 0; i < close; ++i)
      if (!stack.empty()) stack.pop_back();

    auto eq = line.find('=');
    if (eq == std::string::npos) {
      for (int i = 0; i < open; ++i) stack.emplace_back();
      continue;
    }

    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    while (!val.empty() && val.back() == ',') val.pop_back();
    val = trim(val);

    if (val == "{") {
      stack.push_back(key);
      continue;
    }

    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);

    std::string full_key;
    bool section_skipped = false;
    for (auto& p : stack) {
      if (p.empty()) continue;
      if (!section_skipped) { section_skipped = true; continue; }
      if (!full_key.empty()) full_key += ".";
      full_key += p;
    }
    if (!full_key.empty()) full_key += ".";
    full_key += key;

    out.emplace_back(std::move(full_key), std::move(val));
  }
  return out;
}

static std::string str_val(const std::vector<std::pair<std::string, std::string>>& kv,
                            const std::string& key, const std::string& def) {
  for (auto& [k, v] : kv)
    if (k == key) return v;
  return def;
}

static int int_val(const std::vector<std::pair<std::string, std::string>>& kv,
                    const std::string& key, int def) {
  auto s = str_val(kv, key, "");
  if (s.empty()) return def;
  try { return std::stoi(s); } catch (const std::exception&) { return def; }
}

static bool bool_val(const std::vector<std::pair<std::string, std::string>>& kv,
                      const std::string& key, bool def) {
  auto s = str_val(kv, key, "");
  if (s.empty()) return def;
  return s == "true" || s == "yes" || s == "1";
}

static double double_val(const std::vector<std::pair<std::string, std::string>>& kv,
                          const std::string& key, double def) {
  auto s = str_val(kv, key, "");
  if (s.empty()) return def;
  try { return std::stod(s); } catch (const std::exception&) { return def; }
}

// Lua writer.

static std::string escape_lua(const std::string& s) {
  if (s == "true" || s == "false") return s;
  if (s.empty()) return "\"\"";
  bool is_number = true;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '+') {
      is_number = false;
      break;
    }
  }
  if (is_number) return s;
  return '"' + s + '"';
}

static void write_line(std::ofstream& f, int indent, const std::string& line) {
  for (int i = 0; i < indent; ++i) f << "  ";
  f << line << "\n";
}

static void write_nested_entries(std::ofstream& f, int indent,
                                  const std::vector<std::pair<std::string, std::string>>& entries) {
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> subs;
  std::vector<std::pair<std::string, std::string>> flat;

  for (auto& [k, v] : entries) {
    auto dot = k.find('.');
    if (dot != std::string::npos) {
      std::string prefix = k.substr(0, dot);
      std::string suffix = k.substr(dot + 1);
      subs[prefix].emplace_back(std::move(suffix), v);
    } else {
      flat.emplace_back(k, v);
    }
  }

  for (auto& [k, v] : flat) {
    write_line(f, indent, k + " = " + v + ",");
  }

  for (auto& [prefix, sub_entries] : subs) {
    write_line(f, indent, prefix + " = {");
    write_nested_entries(f, indent + 1, sub_entries);
    write_line(f, indent, "},");
  }
}

static void write_hl_config(std::ofstream& f, const std::string& section,
                             const std::vector<std::pair<std::string, std::string>>& entries) {
  write_line(f, 0, "hl.config({");
  write_line(f, 1, section + " = {");

  write_nested_entries(f, 2, entries);

  write_line(f, 1, "},");
  write_line(f, 0, "})");
}

// General.

HyprlandGeneral read_general() {
   
  auto kv = parse_lua_kv(file_path("general"));
  HyprlandGeneral c;
  c.border_size = int_val(kv, "border_size", 1);
  c.gaps_in = int_val(kv, "gaps_in", 5);
  c.gaps_out = int_val(kv, "gaps_out", 20);
  c.float_gaps = int_val(kv, "float_gaps", 0);
  c.gaps_workspaces = int_val(kv, "gaps_workspaces", 0);
  c.layout = str_val(kv, "layout", "dwindle");
  c.allow_tearing = bool_val(kv, "allow_tearing", false);
  c.resize_on_border = bool_val(kv, "resize_on_border", false);
  c.snap.enabled = bool_val(kv, "snap.enabled", false);
  c.snap.window_gap = int_val(kv, "snap.window_gap", 10);
  c.snap.monitor_gap = int_val(kv, "snap.monitor_gap", 0);
  c.snap.border_overlap = bool_val(kv, "snap.border_overlap", false);
  c.snap.respect_gaps = bool_val(kv, "snap.respect_gaps", false);
  c.no_focus_fallback = bool_val(kv, "no_focus_fallback", false);
  c.extend_border_grab_area = int_val(kv, "extend_border_grab_area", 15);
  c.hover_icon_on_border = bool_val(kv, "hover_icon_on_border", true);
  c.resize_corner = int_val(kv, "resize_corner", 0);
  c.modal_parent_blocking = bool_val(kv, "modal_parent_blocking", true);
  c.locale = str_val(kv, "locale", "");
  c.col.active_border = str_val(kv, "col.active_border", "");
  c.col.inactive_border = str_val(kv, "col.inactive_border", "");
  c.col.nogroup_border = str_val(kv, "col.nogroup_border", "");
  c.col.nogroup_border_active = str_val(kv, "col.nogroup_border_active", "");
  return c;
}

bool write_general(const HyprlandGeneral& c) {
   
  std::ofstream f(file_path("general"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- General settings -- managed by Event Horizon");

  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("border_size", std::to_string(c.border_size));
  entries.emplace_back("gaps_in", std::to_string(c.gaps_in));
  entries.emplace_back("gaps_out", std::to_string(c.gaps_out));
  entries.emplace_back("float_gaps", std::to_string(c.float_gaps));
  entries.emplace_back("gaps_workspaces", std::to_string(c.gaps_workspaces));
  entries.emplace_back("layout", '"' + c.layout + '"');
  entries.emplace_back("allow_tearing", c.allow_tearing ? "true" : "false");
  entries.emplace_back("resize_on_border", c.resize_on_border ? "true" : "false");
  entries.emplace_back("snap.enabled", c.snap.enabled ? "true" : "false");
  entries.emplace_back("snap.window_gap", std::to_string(c.snap.window_gap));
  entries.emplace_back("snap.monitor_gap", std::to_string(c.snap.monitor_gap));
  entries.emplace_back("snap.border_overlap", c.snap.border_overlap ? "true" : "false");
  entries.emplace_back("snap.respect_gaps", c.snap.respect_gaps ? "true" : "false");
  entries.emplace_back("no_focus_fallback", c.no_focus_fallback ? "true" : "false");
  entries.emplace_back("extend_border_grab_area", std::to_string(c.extend_border_grab_area));
  entries.emplace_back("hover_icon_on_border", c.hover_icon_on_border ? "true" : "false");
  entries.emplace_back("resize_corner", std::to_string(c.resize_corner));
  entries.emplace_back("modal_parent_blocking", c.modal_parent_blocking ? "true" : "false");
  if (!c.locale.empty())
    entries.emplace_back("locale", escape_lua(c.locale));
  if (!c.col.active_border.empty())
    entries.emplace_back("col.active_border", escape_lua(c.col.active_border));
  if (!c.col.inactive_border.empty())
    entries.emplace_back("col.inactive_border", escape_lua(c.col.inactive_border));
  if (!c.col.nogroup_border.empty())
    entries.emplace_back("col.nogroup_border", escape_lua(c.col.nogroup_border));
  if (!c.col.nogroup_border_active.empty())
    entries.emplace_back("col.nogroup_border_active", escape_lua(c.col.nogroup_border_active));

  write_hl_config(f, "general", entries);
  return true;
}

// Decoration.

HyprlandDecoration read_decoration() {
   
  auto kv = parse_lua_kv(file_path("decoration"));
  HyprlandDecoration d;
  d.rounding = int_val(kv, "rounding", 0);
  d.rounding_power = double_val(kv, "rounding_power", 2.0);
  d.active_opacity = double_val(kv, "active_opacity", 1.0);
  d.inactive_opacity = double_val(kv, "inactive_opacity", 1.0);
  d.fullscreen_opacity = double_val(kv, "fullscreen_opacity", 1.0);
  d.dim_modal = bool_val(kv, "dim_modal", true);
  d.dim_inactive = bool_val(kv, "dim_inactive", false);
  d.dim_strength = double_val(kv, "dim_strength", 0.5);
  d.dim_special = double_val(kv, "dim_special", 0.2);
  d.dim_around = double_val(kv, "dim_around", 0.4);
  d.border_part_of_window = bool_val(kv, "border_part_of_window", true);
  d.blur.enabled = bool_val(kv, "blur.enabled", true);
  d.blur.xray = bool_val(kv, "blur.xray", false);
  d.blur.special = bool_val(kv, "blur.special", false);
  d.blur.new_optimizations = bool_val(kv, "blur.new_optimizations", true);
  d.blur.ignore_opacity = bool_val(kv, "blur.ignore_opacity", true);
  d.blur.size = int_val(kv, "blur.size", 8);
  d.blur.passes = int_val(kv, "blur.passes", 1);
  d.blur.noise = double_val(kv, "blur.noise", 0.0117);
  d.blur.contrast = double_val(kv, "blur.contrast", 0.8916);
  d.blur.brightness = double_val(kv, "blur.brightness", 1.0);
  d.blur.vibrancy = double_val(kv, "blur.vibrancy", 0.1696);
  d.blur.popups = bool_val(kv, "blur.popups", false);
  d.blur.popups_ignorealpha = double_val(kv, "blur.popups_ignorealpha", 0.2);
  d.blur.input_methods = bool_val(kv, "blur.input_methods", false);
  d.blur.vibrancy_darkness = double_val(kv, "blur.vibrancy_darkness", 0.0);
  d.blur.input_methods_ignorealpha = double_val(kv, "blur.input_methods_ignorealpha", 0.2);
  d.shadow.enabled = bool_val(kv, "shadow.enabled", true);
  d.shadow.range = int_val(kv, "shadow.range", 4);
  d.shadow.render_power = int_val(kv, "shadow.render_power", 3);
  d.shadow.sharp = bool_val(kv, "shadow.sharp", false);
  d.shadow.offset = str_val(kv, "shadow.offset", "0, 0");
  d.shadow.scale = double_val(kv, "shadow.scale", 1.0);
  d.shadow.color = str_val(kv, "shadow.color", "0xee1a1a1a");
  d.shadow.color_inactive = str_val(kv, "shadow.color_inactive", "");
  d.glow.enabled = bool_val(kv, "glow.enabled", false);
  d.glow.range = int_val(kv, "glow.range", 10);
  d.glow.render_power = int_val(kv, "glow.render_power", 3);
  d.glow.color = str_val(kv, "glow.color", "0xee33ccff");
  d.glow.color_inactive = str_val(kv, "glow.color_inactive", "");
  d.motion_blur.enabled = bool_val(kv, "motion_blur.enabled", false);
  d.motion_blur.samples = int_val(kv, "motion_blur.samples", 7);
  d.screen_shader_enabled = bool_val(kv, "screen_shader_enabled", false);
  d.screen_shader = str_val(kv, "screen_shader", "");
  return d;
}

static std::vector<std::pair<std::string, std::string>> decoration_entries(const HyprlandDecoration& d) {
  std::vector<std::pair<std::string, std::string>> e;
  e.emplace_back("rounding", std::to_string(d.rounding));
  e.emplace_back("rounding_power", std::to_string(d.rounding_power));
  e.emplace_back("active_opacity", std::to_string(d.active_opacity));
  e.emplace_back("inactive_opacity", std::to_string(d.inactive_opacity));
  e.emplace_back("fullscreen_opacity", std::to_string(d.fullscreen_opacity));
  e.emplace_back("dim_modal", d.dim_modal ? "true" : "false");
  e.emplace_back("dim_inactive", d.dim_inactive ? "true" : "false");
  e.emplace_back("dim_strength", std::to_string(d.dim_strength));
  e.emplace_back("dim_special", std::to_string(d.dim_special));
  e.emplace_back("dim_around", std::to_string(d.dim_around));
  e.emplace_back("border_part_of_window", d.border_part_of_window ? "true" : "false");
  e.emplace_back("blur.enabled", d.blur.enabled ? "true" : "false");
  e.emplace_back("blur.xray", d.blur.xray ? "true" : "false");
  e.emplace_back("blur.special", d.blur.special ? "true" : "false");
  e.emplace_back("blur.new_optimizations", d.blur.new_optimizations ? "true" : "false");
  e.emplace_back("blur.ignore_opacity", d.blur.ignore_opacity ? "true" : "false");
  e.emplace_back("blur.size", std::to_string(d.blur.size));
  e.emplace_back("blur.passes", std::to_string(d.blur.passes));
  e.emplace_back("blur.noise", std::to_string(d.blur.noise));
  e.emplace_back("blur.contrast", std::to_string(d.blur.contrast));
  e.emplace_back("blur.brightness", std::to_string(d.blur.brightness));
  e.emplace_back("blur.vibrancy", std::to_string(d.blur.vibrancy));
  e.emplace_back("blur.popups", d.blur.popups ? "true" : "false");
  e.emplace_back("blur.popups_ignorealpha", std::to_string(d.blur.popups_ignorealpha));
  e.emplace_back("blur.input_methods", d.blur.input_methods ? "true" : "false");
  e.emplace_back("blur.vibrancy_darkness", std::to_string(d.blur.vibrancy_darkness));
  e.emplace_back("blur.input_methods_ignorealpha", std::to_string(d.blur.input_methods_ignorealpha));
  e.emplace_back("shadow.enabled", d.shadow.enabled ? "true" : "false");
  e.emplace_back("shadow.range", std::to_string(d.shadow.range));
  e.emplace_back("shadow.render_power", std::to_string(d.shadow.render_power));
  e.emplace_back("shadow.sharp", d.shadow.sharp ? "true" : "false");
  e.emplace_back("shadow.offset", escape_lua(d.shadow.offset));
  e.emplace_back("shadow.scale", std::to_string(d.shadow.scale));
  if (!d.shadow.color.empty())
    e.emplace_back("shadow.color", escape_lua(d.shadow.color));
  if (!d.shadow.color_inactive.empty())
    e.emplace_back("shadow.color_inactive", escape_lua(d.shadow.color_inactive));
  e.emplace_back("glow.enabled", d.glow.enabled ? "true" : "false");
  e.emplace_back("glow.range", std::to_string(d.glow.range));
  e.emplace_back("glow.render_power", std::to_string(d.glow.render_power));
  if (!d.glow.color.empty())
    e.emplace_back("glow.color", escape_lua(d.glow.color));
  if (!d.glow.color_inactive.empty())
    e.emplace_back("glow.color_inactive", escape_lua(d.glow.color_inactive));
  e.emplace_back("motion_blur.enabled", d.motion_blur.enabled ? "true" : "false");
  e.emplace_back("motion_blur.samples", std::to_string(d.motion_blur.samples));
  if (d.screen_shader_enabled && !d.screen_shader.empty())
    e.emplace_back("screen_shader", escape_lua(d.screen_shader));
  return e;
}

bool write_decoration(const HyprlandDecoration& d) {
   
  std::ofstream f(file_path("decoration"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Decoration settings -- managed by Event Horizon");
  write_hl_config(f, "decoration", decoration_entries(d));
  return true;
}

// Input.

HyprlandInput read_input() {
   
  auto kv = parse_lua_kv(file_path("input"));
  HyprlandInput in;
  in.kb_layout = str_val(kv, "kb_layout", "us");
  in.kb_variant = str_val(kv, "kb_variant", "");
  in.kb_options = str_val(kv, "kb_options", "");
  in.kb_rules = str_val(kv, "kb_rules", "");
  in.kb_model = str_val(kv, "kb_model", "");
  in.kb_file = str_val(kv, "kb_file", "");
  in.numlock_by_default = bool_val(kv, "numlock_by_default", false);
  in.repeat_delay = int_val(kv, "repeat_delay", 600);
  in.repeat_rate = int_val(kv, "repeat_rate", 25);
  in.sensitivity = double_val(kv, "sensitivity", 0.0);
  in.accel_profile = str_val(kv, "accel_profile", "adaptive");
  if (in.accel_profile != "adaptive" && in.accel_profile != "flat" && in.accel_profile != "custom")
    in.accel_profile = "adaptive";
  in.scroll_method = str_val(kv, "scroll_method", "2fg");
  in.natural_scroll = bool_val(kv, "natural_scroll", false);
  in.resolve_binds_by_sym = bool_val(kv, "resolve_binds_by_sym", false);
  in.force_no_accel = bool_val(kv, "force_no_accel", false);
  in.rotation = int_val(kv, "rotation", 0);
  in.left_handed = bool_val(kv, "left_handed", false);
  in.scroll_points = str_val(kv, "scroll_points", "");
  in.scroll_button = int_val(kv, "scroll_button", 0);
  in.scroll_button_lock = bool_val(kv, "scroll_button_lock", false);
  in.scroll_factor = double_val(kv, "scroll_factor", 1.0);
  in.follow_mouse_shrink = int_val(kv, "follow_mouse_shrink", 0);
  in.follow_mouse_threshold = double_val(kv, "follow_mouse_threshold", 0.0);
  in.focus_on_close = int_val(kv, "focus_on_close", 0);
  in.off_window_axis_events = int_val(kv, "off_window_axis_events", 1);
  in.emulate_discrete_scroll = int_val(kv, "emulate_discrete_scroll", 1);
  in.touchpad.natural_scroll = bool_val(kv, "touchpad.natural_scroll", false);
  in.touchpad.disable_while_typing = bool_val(kv, "touchpad.disable_while_typing", true);
  in.touchpad.tap_to_click = bool_val(kv, "touchpad.tap_to_click", true);
  in.touchpad.clickfinger_behavior = bool_val(kv, "touchpad.clickfinger_behavior", false);
  in.touchpad.scroll_factor = double_val(kv, "touchpad.scroll_factor", 1.0);
  in.touchpad.middle_button_emulation = bool_val(kv, "touchpad.middle_button_emulation", false);
  in.touchpad.tap_button_map = str_val(kv, "touchpad.tap_button_map", "");
  in.touchpad.drag_lock = int_val(kv, "touchpad.drag_lock", 0);
  in.touchpad.tap_and_drag = bool_val(kv, "touchpad.tap_and_drag", true);
  in.touchpad.flip_x = bool_val(kv, "touchpad.flip_x", false);
  in.touchpad.flip_y = bool_val(kv, "touchpad.flip_y", false);
  in.touchpad.drag_3fg = int_val(kv, "touchpad.drag_3fg", 0);
  in.touchdevice.transform = int_val(kv, "touchdevice.transform", 0);
  in.touchdevice.output = str_val(kv, "touchdevice.output", "");
  in.touchdevice.enabled = bool_val(kv, "touchdevice.enabled", true);
  in.virtualkeyboard.share_states = int_val(kv, "virtualkeyboard.share_states", 2);
  in.virtualkeyboard.release_pressed_on_close = bool_val(kv, "virtualkeyboard.release_pressed_on_close", false);
  in.tablet.transform = int_val(kv, "tablet.transform", 0);
  in.tablet.output = str_val(kv, "tablet.output", "");
  in.tablet.region_position = str_val(kv, "tablet.region_position", "");
  in.tablet.absolute_region_position = bool_val(kv, "tablet.absolute_region_position", false);
  in.tablet.region_size = str_val(kv, "tablet.region_size", "");
  in.tablet.relative_input = bool_val(kv, "tablet.relative_input", false);
  in.tablet.left_handed = bool_val(kv, "tablet.left_handed", false);
  in.tablet.active_area_size = str_val(kv, "tablet.active_area_size", "");
  in.tablet.active_area_position = str_val(kv, "tablet.active_area_position", "");
  in.tablettool.eraser_button_mode = int_val(kv, "tablettool.eraser_button_mode", 0);
  in.tablettool.eraser_button_override = int_val(kv, "tablettool.eraser_button_override", 0);
  in.tablettool.pressure_range_min = double_val(kv, "tablettool.pressure_range_min", -1.0);
  in.tablettool.pressure_range_max = double_val(kv, "tablettool.pressure_range_max", -1.0);
  in.special_fallthrough = bool_val(kv, "special_fallthrough", false);
  in.follow_mouse = int_val(kv, "follow_mouse", 1);
  in.float_switch_override_focus = int_val(kv, "float_switch_override_focus", 0);
  in.mouse_refocus = bool_val(kv, "mouse_refocus", true);
  return in;
}

static std::vector<std::pair<std::string, std::string>> input_entries(const HyprlandInput& in) {
  std::vector<std::pair<std::string, std::string>> e;
  e.emplace_back("kb_layout", escape_lua(in.kb_layout));
  if (!in.kb_variant.empty())
    e.emplace_back("kb_variant", escape_lua(in.kb_variant));
  if (!in.kb_options.empty())
    e.emplace_back("kb_options", escape_lua(in.kb_options));
  if (!in.kb_rules.empty())
    e.emplace_back("kb_rules", escape_lua(in.kb_rules));
  if (!in.kb_model.empty())
    e.emplace_back("kb_model", escape_lua(in.kb_model));
  if (!in.kb_file.empty())
    e.emplace_back("kb_file", escape_lua(in.kb_file));
  e.emplace_back("numlock_by_default", in.numlock_by_default ? "true" : "false");
  e.emplace_back("repeat_delay", std::to_string(in.repeat_delay));
  e.emplace_back("repeat_rate", std::to_string(in.repeat_rate));
  e.emplace_back("sensitivity", std::to_string(in.sensitivity));
  e.emplace_back("accel_profile", escape_lua(in.accel_profile));

  e.emplace_back("scroll_method", escape_lua(in.scroll_method));
  e.emplace_back("natural_scroll", in.natural_scroll ? "true" : "false");
  e.emplace_back("resolve_binds_by_sym", in.resolve_binds_by_sym ? "true" : "false");
  e.emplace_back("force_no_accel", in.force_no_accel ? "true" : "false");
  e.emplace_back("rotation", std::to_string(in.rotation));
  e.emplace_back("left_handed", in.left_handed ? "true" : "false");
  if (!in.scroll_points.empty())
    e.emplace_back("scroll_points", escape_lua(in.scroll_points));
  e.emplace_back("scroll_button", std::to_string(in.scroll_button));
  e.emplace_back("scroll_button_lock", in.scroll_button_lock ? "true" : "false");
  e.emplace_back("scroll_factor", std::to_string(in.scroll_factor));
  e.emplace_back("follow_mouse_shrink", std::to_string(in.follow_mouse_shrink));
  e.emplace_back("follow_mouse_threshold", std::to_string(in.follow_mouse_threshold));
  e.emplace_back("focus_on_close", std::to_string(in.focus_on_close));
  e.emplace_back("off_window_axis_events", std::to_string(in.off_window_axis_events));
  e.emplace_back("emulate_discrete_scroll", std::to_string(in.emulate_discrete_scroll));
  e.emplace_back("touchpad.natural_scroll", in.touchpad.natural_scroll ? "true" : "false");
  e.emplace_back("touchpad.disable_while_typing", in.touchpad.disable_while_typing ? "true" : "false");
  e.emplace_back("touchpad.tap_to_click", in.touchpad.tap_to_click ? "true" : "false");
  e.emplace_back("touchpad.clickfinger_behavior", in.touchpad.clickfinger_behavior ? "true" : "false");
  e.emplace_back("touchpad.scroll_factor", std::to_string(in.touchpad.scroll_factor));
  e.emplace_back("touchpad.middle_button_emulation", in.touchpad.middle_button_emulation ? "true" : "false");
  e.emplace_back("touchpad.tap_button_map", escape_lua(in.touchpad.tap_button_map));
  e.emplace_back("touchpad.drag_lock", std::to_string(in.touchpad.drag_lock));
  e.emplace_back("touchpad.tap_and_drag", in.touchpad.tap_and_drag ? "true" : "false");
  e.emplace_back("touchpad.flip_x", in.touchpad.flip_x ? "true" : "false");
  e.emplace_back("touchpad.flip_y", in.touchpad.flip_y ? "true" : "false");
  e.emplace_back("touchpad.drag_3fg", std::to_string(in.touchpad.drag_3fg));
  e.emplace_back("touchdevice.transform", std::to_string(in.touchdevice.transform));
  if (!in.touchdevice.output.empty())
    e.emplace_back("touchdevice.output", escape_lua(in.touchdevice.output));
  e.emplace_back("touchdevice.enabled", in.touchdevice.enabled ? "true" : "false");
  e.emplace_back("virtualkeyboard.share_states", std::to_string(in.virtualkeyboard.share_states));
  e.emplace_back("virtualkeyboard.release_pressed_on_close", in.virtualkeyboard.release_pressed_on_close ? "true" : "false");
  e.emplace_back("tablet.transform", std::to_string(in.tablet.transform));
  if (!in.tablet.output.empty())
    e.emplace_back("tablet.output", escape_lua(in.tablet.output));
  if (!in.tablet.region_position.empty())
    e.emplace_back("tablet.region_position", escape_lua(in.tablet.region_position));
  e.emplace_back("tablet.absolute_region_position", in.tablet.absolute_region_position ? "true" : "false");
  if (!in.tablet.region_size.empty())
    e.emplace_back("tablet.region_size", escape_lua(in.tablet.region_size));
  e.emplace_back("tablet.relative_input", in.tablet.relative_input ? "true" : "false");
  e.emplace_back("tablet.left_handed", in.tablet.left_handed ? "true" : "false");
  if (!in.tablet.active_area_size.empty())
    e.emplace_back("tablet.active_area_size", escape_lua(in.tablet.active_area_size));
  if (!in.tablet.active_area_position.empty())
    e.emplace_back("tablet.active_area_position", escape_lua(in.tablet.active_area_position));
  e.emplace_back("tablettool.eraser_button_mode", std::to_string(in.tablettool.eraser_button_mode));
  e.emplace_back("tablettool.eraser_button_override", std::to_string(in.tablettool.eraser_button_override));
  e.emplace_back("tablettool.pressure_range_min", std::to_string(in.tablettool.pressure_range_min));
  e.emplace_back("tablettool.pressure_range_max", std::to_string(in.tablettool.pressure_range_max));
  e.emplace_back("special_fallthrough", in.special_fallthrough ? "true" : "false");
  e.emplace_back("follow_mouse", std::to_string(in.follow_mouse));
  e.emplace_back("float_switch_override_focus", std::to_string(in.float_switch_override_focus));
  e.emplace_back("mouse_refocus", in.mouse_refocus ? "true" : "false");
  return e;
}

bool write_input(const HyprlandInput& in) {
   
  std::ofstream f(file_path("input"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Input settings -- managed by Event Horizon");
  write_hl_config(f, "input", input_entries(in));
  return true;
}

// Binds.

HyprlandBinds read_binds() {
   
  auto kv = parse_lua_kv(file_path("binds"));
  HyprlandBinds b;
  b.scroll_event_delay = int_val(kv, "scroll_event_delay", 0);
  b.focus_preferred_method = int_val(kv, "focus_preferred_method", 0);
  b.workspace_back_and_forth = bool_val(kv, "workspace_back_and_forth", false);
  b.allow_workspace_cycles = bool_val(kv, "allow_workspace_cycles", true);
  b.pass_mouse_when_bound = bool_val(kv, "pass_mouse_when_bound", false);
  b.hide_special_on_workspace_change = bool_val(kv, "hide_special_on_workspace_change", false);
  b.workspace_center_on = int_val(kv, "workspace_center_on", 0);
  b.ignore_group_lock = bool_val(kv, "ignore_group_lock", false);
  b.movefocus_cycles_fullscreen = bool_val(kv, "movefocus_cycles_fullscreen", false);
  b.movefocus_cycles_groupfirst = bool_val(kv, "movefocus_cycles_groupfirst", false);
  b.window_direction_monitor_fallback = bool_val(kv, "window_direction_monitor_fallback", true);
  b.disable_keybind_grabbing = bool_val(kv, "disable_keybind_grabbing", false);
  b.allow_pin_fullscreen = bool_val(kv, "allow_pin_fullscreen", false);
  b.drag_threshold = int_val(kv, "drag_threshold", 0);
  return b;
}

bool write_binds(const HyprlandBinds& b) {
   
  std::ofstream f(file_path("binds"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Bind settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("scroll_event_delay", std::to_string(b.scroll_event_delay));
  entries.emplace_back("focus_preferred_method", std::to_string(b.focus_preferred_method));
  entries.emplace_back("workspace_back_and_forth", b.workspace_back_and_forth ? "true" : "false");
  entries.emplace_back("allow_workspace_cycles", b.allow_workspace_cycles ? "true" : "false");
  entries.emplace_back("pass_mouse_when_bound", b.pass_mouse_when_bound ? "true" : "false");
  entries.emplace_back("hide_special_on_workspace_change", b.hide_special_on_workspace_change ? "true" : "false");
  entries.emplace_back("workspace_center_on", std::to_string(b.workspace_center_on));
  entries.emplace_back("ignore_group_lock", b.ignore_group_lock ? "true" : "false");
  entries.emplace_back("movefocus_cycles_fullscreen", b.movefocus_cycles_fullscreen ? "true" : "false");
  entries.emplace_back("movefocus_cycles_groupfirst", b.movefocus_cycles_groupfirst ? "true" : "false");
  entries.emplace_back("window_direction_monitor_fallback", b.window_direction_monitor_fallback ? "true" : "false");
  entries.emplace_back("disable_keybind_grabbing", b.disable_keybind_grabbing ? "true" : "false");
  entries.emplace_back("allow_pin_fullscreen", b.allow_pin_fullscreen ? "true" : "false");
  entries.emplace_back("drag_threshold", std::to_string(b.drag_threshold));
  write_hl_config(f, "binds", entries);
  return true;
}

// Dwindle.

HyprlandDwindle read_dwindle() {
   
  auto kv = parse_lua_kv(file_path("dwindle"));
  HyprlandDwindle d;
  d.force_split = int_val(kv, "force_split", 0);
  d.preserve_split = bool_val(kv, "preserve_split", false);
  d.smart_split = bool_val(kv, "smart_split", false);
  d.smart_resizing = bool_val(kv, "smart_resizing", true);
  d.permanent_direction_override = bool_val(kv, "permanent_direction_override", false);
  d.special_scale_factor = double_val(kv, "special_scale_factor", 1.0);
  d.split_width_multiplier = double_val(kv, "split_width_multiplier", 1.0);
  d.use_active_for_splits = bool_val(kv, "use_active_for_splits", true);
  d.default_split_ratio = double_val(kv, "default_split_ratio", 1.0);
  d.split_bias = int_val(kv, "split_bias", 0);
  d.precise_mouse_move = bool_val(kv, "precise_mouse_move", false);
  return d;
}

bool write_dwindle(const HyprlandDwindle& d) {
   
  std::ofstream f(file_path("dwindle"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Dwindle layout settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("force_split", std::to_string(d.force_split));
  entries.emplace_back("preserve_split", d.preserve_split ? "true" : "false");
  entries.emplace_back("smart_split", d.smart_split ? "true" : "false");
  entries.emplace_back("smart_resizing", d.smart_resizing ? "true" : "false");
  entries.emplace_back("permanent_direction_override", d.permanent_direction_override ? "true" : "false");
  entries.emplace_back("special_scale_factor", std::to_string(d.special_scale_factor));
  entries.emplace_back("split_width_multiplier", std::to_string(d.split_width_multiplier));
  entries.emplace_back("use_active_for_splits", d.use_active_for_splits ? "true" : "false");
  entries.emplace_back("default_split_ratio", std::to_string(d.default_split_ratio));
  entries.emplace_back("split_bias", std::to_string(d.split_bias));
  entries.emplace_back("precise_mouse_move", d.precise_mouse_move ? "true" : "false");
  write_hl_config(f, "dwindle", entries);
  return true;
}

// Master.

HyprlandMaster read_master() {
   
  auto kv = parse_lua_kv(file_path("master"));
  HyprlandMaster m;
  m.allow_small_split = bool_val(kv, "allow_small_split", false);
  m.special_scale_factor = double_val(kv, "special_scale_factor", 1.0);
  m.mfact = double_val(kv, "mfact", 0.55);
  m.new_status = str_val(kv, "new_status", "slave");
  m.new_on_top = bool_val(kv, "new_on_top", false);
  m.new_on_active = str_val(kv, "new_on_active", "none");
  m.orientation = str_val(kv, "orientation", "left");
  m.slave_count_for_center_master = int_val(kv, "slave_count_for_center_master", 2);
  m.center_master_fallback = str_val(kv, "center_master_fallback", "left");
  m.smart_resizing = bool_val(kv, "smart_resizing", true);
  m.drop_at_cursor = bool_val(kv, "drop_at_cursor", true);
  m.always_keep_position = bool_val(kv, "always_keep_position", false);
  m.focus_master_on_close = bool_val(kv, "focus_master_on_close", false);
  return m;
}

bool write_master(const HyprlandMaster& m) {
  std::ofstream f(file_path("master"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Master layout settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("allow_small_split", m.allow_small_split ? "true" : "false");
  entries.emplace_back("special_scale_factor", std::to_string(m.special_scale_factor));
  entries.emplace_back("mfact", std::to_string(m.mfact));
  entries.emplace_back("new_status", escape_lua(m.new_status));
  entries.emplace_back("new_on_top", m.new_on_top ? "true" : "false");
  entries.emplace_back("new_on_active", escape_lua(m.new_on_active));
  entries.emplace_back("orientation", escape_lua(m.orientation));
  entries.emplace_back("slave_count_for_center_master", std::to_string(m.slave_count_for_center_master));
  entries.emplace_back("center_master_fallback", escape_lua(m.center_master_fallback));
  entries.emplace_back("smart_resizing", m.smart_resizing ? "true" : "false");
  entries.emplace_back("drop_at_cursor", m.drop_at_cursor ? "true" : "false");
  entries.emplace_back("always_keep_position", m.always_keep_position ? "true" : "false");
  entries.emplace_back("focus_master_on_close", m.focus_master_on_close ? "true" : "false");
  write_hl_config(f, "master", entries);
  return true;
}

// Scrolling.

HyprlandScrolling read_scrolling() {
  auto kv = parse_lua_kv(file_path("scrolling"));
  HyprlandScrolling s;
  s.column_width = double_val(kv, "column_width", 0.5);
  s.direction = str_val(kv, "direction", "right");
  s.fullscreen_on_one_column = bool_val(kv, "fullscreen_on_one_column", true);
  s.follow_focus = bool_val(kv, "follow_focus", true);
  s.follow_min_visible = double_val(kv, "follow_min_visible", 0.4);
  s.focus_fit_method = int_val(kv, "focus_fit_method", 1);
  s.explicit_column_widths = str_val(kv, "explicit_column_widths", "0.333,0.5,0.62,0.75,1.0");
  s.wrap_focus = bool_val(kv, "wrap_focus", true);
  s.wrap_swapcol = bool_val(kv, "wrap_swapcol", true);
  return s;
}

bool write_scrolling(const HyprlandScrolling& s) {
  std::ofstream f(file_path("scrolling"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Scrolling layout settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("column_width", std::to_string(s.column_width));
  entries.emplace_back("direction", escape_lua(s.direction));
  entries.emplace_back("fullscreen_on_one_column", s.fullscreen_on_one_column ? "true" : "false");
  entries.emplace_back("follow_focus", s.follow_focus ? "true" : "false");
  entries.emplace_back("follow_min_visible", std::to_string(s.follow_min_visible));
  entries.emplace_back("focus_fit_method", std::to_string(s.focus_fit_method));
  entries.emplace_back("explicit_column_widths", escape_lua(s.explicit_column_widths));
  entries.emplace_back("wrap_focus", s.wrap_focus ? "true" : "false");
  entries.emplace_back("wrap_swapcol", s.wrap_swapcol ? "true" : "false");
  write_hl_config(f, "scrolling", entries);
  return true;
}

// Animation entry parsing.

static HyprlandAnimationEntry parse_anim_entry(const std::string& raw) {
  HyprlandAnimationEntry e;
  std::vector<std::string> parts;
  std::string cur;
  for (char c : raw) {
    if (c == ',') { parts.push_back(trim(cur)); cur.clear(); }
    else cur += c;
  }
  if (!cur.empty()) parts.push_back(trim(cur));

  if (parts.size() >= 1) e.name = parts[0];
  if (parts.size() >= 2) e.enabled = (parts[1] == "1" || parts[1] == "true");
  if (parts.size() >= 3) {
    try { e.speed = std::stod(parts[2]); } catch (const std::exception&) {}
  }
  if (parts.size() >= 4) e.curve = parts[3];
  if (parts.size() >= 5) e.style = parts[4];
  return e;
}

static std::vector<HyprlandAnimationEntry> read_anim_lines(const std::string& path) {
  std::vector<HyprlandAnimationEntry> result;
  std::ifstream f(path);
  if (!f.is_open()) return result;

  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    if (key != "animation") continue;
    std::string val = trim(line.substr(eq + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);
    if (!val.empty() && val.back() == ',') val.pop_back();
    if (val.empty()) continue;
    result.push_back(parse_anim_entry(val));
  }
  return result;
}

// Animations.

HyprlandAnimations read_animations() {
  auto kv = parse_lua_kv(file_path("animations"));
  HyprlandAnimations a;
  a.enabled = bool_val(kv, "enabled", true);
  a.workspace_wraparound = bool_val(kv, "workspace_wraparound", false);
  a.entries = read_anim_lines(file_path("animations"));
  return a;
}

bool write_animations(const HyprlandAnimations& a) {
  std::ofstream f(file_path("animations"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Animation settings -- managed by Event Horizon");

  write_line(f, 0, "hl.config({");
  write_line(f, 1, "animations = {");
  write_line(f, 2, "enabled = " + std::string(a.enabled ? "true" : "false") + ",");
  write_line(f, 2, "workspace_wraparound = " + std::string(a.workspace_wraparound ? "true" : "false") + ",");
  write_line(f, 1, "},");
  write_line(f, 0, "})");

  for (auto& e : a.entries) {
    std::string val = e.name + ", " + (e.enabled ? "1" : "0")
                    + ", " + std::to_string(e.speed)
                    + ", " + e.curve;
    if (!e.style.empty()) val += ", " + e.style;
    write_line(f, 0, "");
    write_line(f, 0, "hl.config({");
    write_line(f, 1, "animation = \"" + val + "\",");
    write_line(f, 0, "})");
  }
  return true;
}

// Misc.

HyprlandMisc read_misc() {
  auto kv = parse_lua_kv(file_path("misc"));
  HyprlandMisc m;
  m.vrr = int_val(kv, "vrr", 0);
  m.animate_manual_resizes = bool_val(kv, "animate_manual_resizes", false);
  m.animate_mouse_windowdragging = bool_val(kv, "animate_mouse_windowdragging", false);
  m.enable_swallow = bool_val(kv, "enable_swallow", false);
  m.swallow_regex = str_val(kv, "swallow_regex", "(foot|kitty|allacritty|Alacritty)");
  m.swallow_exception_regex = str_val(kv, "swallow_exception_regex", "");
  m.disable_hyprland_logo = bool_val(kv, "disable_hyprland_logo", false);
  m.disable_splash_rendering = bool_val(kv, "disable_splash_rendering", false);
  m.disable_scale_notification = bool_val(kv, "disable_scale_notification", false);
  m.col_splash = str_val(kv, "col.splash", "");
  m.force_default_wallpaper = int_val(kv, "force_default_wallpaper", -1);
  m.allow_session_lock_restore = bool_val(kv, "allow_session_lock_restore", false);
  m.initial_workspace_tracking = int_val(kv, "initial_workspace_tracking", 1);
  m.disable_autoreload = bool_val(kv, "disable_autoreload", false);
  m.background_color = str_val(kv, "background_color", "");
  m.font_family = str_val(kv, "font_family", "Sans");
  m.splash_font_family = str_val(kv, "splash_font_family", "");
  m.mouse_move_enables_dpms = bool_val(kv, "mouse_move_enables_dpms", false);
  m.key_press_enables_dpms = bool_val(kv, "key_press_enables_dpms", false);
  m.name_vk_after_proc = bool_val(kv, "name_vk_after_proc", true);
  m.always_follow_on_dnd = bool_val(kv, "always_follow_on_dnd", true);
  m.layers_hog_keyboard_focus = bool_val(kv, "layers_hog_keyboard_focus", true);
  m.focus_on_activate = bool_val(kv, "focus_on_activate", false);
  m.mouse_move_focuses_monitor = bool_val(kv, "mouse_move_focuses_monitor", true);
  m.session_lock_xray = bool_val(kv, "session_lock_xray", false);
  m.session_lock_blur = bool_val(kv, "session_lock_blur", false);
  m.close_special_on_empty = bool_val(kv, "close_special_on_empty", true);
  m.on_focus_under_fullscreen = int_val(kv, "on_focus_under_fullscreen", 2);
  m.exit_window_retains_fullscreen = bool_val(kv, "exit_window_retains_fullscreen", false);
  m.initial_workspace_token_timeout = int_val(kv, "initial_workspace_token_timeout", 10);
  if (m.initial_workspace_token_timeout < 1) m.initial_workspace_token_timeout = 1;
  m.render_unfocused_fps = int_val(kv, "render_unfocused_fps", 15);
  if (m.render_unfocused_fps < 1) m.render_unfocused_fps = 1;
  m.disable_xdg_env_checks = bool_val(kv, "disable_xdg_env_checks", false);
  m.disable_hyprland_guiutils_check = bool_val(kv, "disable_hyprland_guiutils_check", false);
  m.lockdead_screen_delay = int_val(kv, "lockdead_screen_delay", 1000);
  m.enable_anr_dialog = bool_val(kv, "enable_anr_dialog", true);
  m.anr_missed_pings = int_val(kv, "anr_missed_pings", 5);
  m.size_limits_tiled = bool_val(kv, "size_limits_tiled", false);
  m.screencopy_force_8b = bool_val(kv, "screencopy_force_8b", true);
  m.disable_watchdog_warning = bool_val(kv, "disable_watchdog_warning", false);
  m.middle_click_paste = bool_val(kv, "middle_click_paste", true);
  return m;
}

bool write_misc(const HyprlandMisc& m) {
  std::ofstream f(file_path("misc"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Misc settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> e;
  e.emplace_back("vrr", std::to_string(m.vrr));
  e.emplace_back("animate_manual_resizes", m.animate_manual_resizes ? "true" : "false");
  e.emplace_back("animate_mouse_windowdragging", m.animate_mouse_windowdragging ? "true" : "false");
  e.emplace_back("enable_swallow", m.enable_swallow ? "true" : "false");
  e.emplace_back("swallow_regex", escape_lua(m.swallow_regex));
  if (!m.swallow_exception_regex.empty())
    e.emplace_back("swallow_exception_regex", escape_lua(m.swallow_exception_regex));
  e.emplace_back("disable_hyprland_logo", m.disable_hyprland_logo ? "true" : "false");
  e.emplace_back("disable_splash_rendering", m.disable_splash_rendering ? "true" : "false");
  e.emplace_back("disable_scale_notification", m.disable_scale_notification ? "true" : "false");
  if (!m.col_splash.empty())
    e.emplace_back("col.splash", escape_lua(m.col_splash));
  e.emplace_back("force_default_wallpaper", std::to_string(m.force_default_wallpaper));
  e.emplace_back("allow_session_lock_restore", m.allow_session_lock_restore ? "true" : "false");
  e.emplace_back("initial_workspace_tracking", std::to_string(m.initial_workspace_tracking));
  e.emplace_back("disable_autoreload", m.disable_autoreload ? "true" : "false");
  if (!m.background_color.empty())
    e.emplace_back("background_color", escape_lua(m.background_color));
  e.emplace_back("font_family", escape_lua(m.font_family));
  if (!m.splash_font_family.empty())
    e.emplace_back("splash_font_family", escape_lua(m.splash_font_family));
  e.emplace_back("mouse_move_enables_dpms", m.mouse_move_enables_dpms ? "true" : "false");
  e.emplace_back("key_press_enables_dpms", m.key_press_enables_dpms ? "true" : "false");
  e.emplace_back("name_vk_after_proc", m.name_vk_after_proc ? "true" : "false");
  e.emplace_back("always_follow_on_dnd", m.always_follow_on_dnd ? "true" : "false");
  e.emplace_back("layers_hog_keyboard_focus", m.layers_hog_keyboard_focus ? "true" : "false");
  e.emplace_back("focus_on_activate", m.focus_on_activate ? "true" : "false");
  e.emplace_back("mouse_move_focuses_monitor", m.mouse_move_focuses_monitor ? "true" : "false");
  e.emplace_back("session_lock_xray", m.session_lock_xray ? "true" : "false");
  e.emplace_back("session_lock_blur", m.session_lock_blur ? "true" : "false");
  e.emplace_back("close_special_on_empty", m.close_special_on_empty ? "true" : "false");
  e.emplace_back("on_focus_under_fullscreen", std::to_string(m.on_focus_under_fullscreen));
  e.emplace_back("exit_window_retains_fullscreen", m.exit_window_retains_fullscreen ? "true" : "false");
  const int token_timeout = m.initial_workspace_token_timeout < 1 ? 1 : m.initial_workspace_token_timeout;
  const int unfocused_fps = m.render_unfocused_fps < 1 ? 1 : m.render_unfocused_fps;
  e.emplace_back("initial_workspace_token_timeout", std::to_string(token_timeout));
  e.emplace_back("render_unfocused_fps", std::to_string(unfocused_fps));
  e.emplace_back("disable_xdg_env_checks", m.disable_xdg_env_checks ? "true" : "false");
  e.emplace_back("disable_hyprland_guiutils_check", m.disable_hyprland_guiutils_check ? "true" : "false");
  e.emplace_back("lockdead_screen_delay", std::to_string(m.lockdead_screen_delay));
  e.emplace_back("enable_anr_dialog", m.enable_anr_dialog ? "true" : "false");
  e.emplace_back("anr_missed_pings", std::to_string(m.anr_missed_pings));
  e.emplace_back("size_limits_tiled", m.size_limits_tiled ? "true" : "false");
  e.emplace_back("screencopy_force_8b", m.screencopy_force_8b ? "true" : "false");
  e.emplace_back("disable_watchdog_warning", m.disable_watchdog_warning ? "true" : "false");
  e.emplace_back("middle_click_paste", m.middle_click_paste ? "true" : "false");
  write_hl_config(f, "misc", e);
  return true;
}

// Render.

HyprlandRender read_render() {
  auto kv = parse_lua_kv(file_path("render"));
  HyprlandRender r;
  r.new_render_scheduling = bool_val(kv, "new_render_scheduling", false);
  r.expand_undersized_textures = bool_val(kv, "expand_undersized_textures", true);
  r.direct_scanout = int_val(kv, "direct_scanout", 2);
  r.cm_enabled = bool_val(kv, "cm_enabled", true);
  r.send_content_type = bool_val(kv, "send_content_type", true);
  r.fp16_sdr_tf = int_val(kv, "fp16_sdr_tf", 0);
  r.xp_mode = bool_val(kv, "xp_mode", false);
  r.ctm_animation = int_val(kv, "ctm_animation", 1);
  r.cm_auto_hdr = int_val(kv, "cm_auto_hdr", 0);
  r.non_shader_cm = int_val(kv, "non_shader_cm", 1);
  r.non_shader_cm_interop = int_val(kv, "non_shader_cm_interop", 0);
  r.cm_sdr_eotf = str_val(kv, "cm_sdr_eotf", "");
  r.commit_timing_enabled = bool_val(kv, "commit_timing_enabled", true);
  r.use_fp16 = int_val(kv, "use_fp16", 2);
  r.keep_unmodified_copy = int_val(kv, "keep_unmodified_copy", 0);
  r.icc_vcgt_enabled = bool_val(kv, "icc_vcgt_enabled", false);
  r.use_shader_blur_blend = bool_val(kv, "use_shader_blur_blend", false);
  return r;
}

bool write_render(const HyprlandRender& r) {
  std::ofstream f(file_path("render"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Render settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> e;
  e.emplace_back("new_render_scheduling", r.new_render_scheduling ? "true" : "false");
  e.emplace_back("expand_undersized_textures", r.expand_undersized_textures ? "true" : "false");
  e.emplace_back("direct_scanout", std::to_string(r.direct_scanout));
  e.emplace_back("cm_enabled", r.cm_enabled ? "true" : "false");
  e.emplace_back("send_content_type", r.send_content_type ? "true" : "false");
  e.emplace_back("fp16_sdr_tf", std::to_string(r.fp16_sdr_tf));
  e.emplace_back("xp_mode", r.xp_mode ? "true" : "false");
  e.emplace_back("ctm_animation", std::to_string(r.ctm_animation));
  e.emplace_back("cm_auto_hdr", std::to_string(r.cm_auto_hdr));
  e.emplace_back("non_shader_cm", std::to_string(r.non_shader_cm));
  e.emplace_back("non_shader_cm_interop", std::to_string(r.non_shader_cm_interop));
  if (!r.cm_sdr_eotf.empty())
    e.emplace_back("cm_sdr_eotf", escape_lua(r.cm_sdr_eotf));
  e.emplace_back("commit_timing_enabled", r.commit_timing_enabled ? "true" : "false");
  e.emplace_back("use_fp16", std::to_string(r.use_fp16));
  e.emplace_back("keep_unmodified_copy", std::to_string(r.keep_unmodified_copy));
  e.emplace_back("icc_vcgt_enabled", r.icc_vcgt_enabled ? "true" : "false");
  e.emplace_back("use_shader_blur_blend", r.use_shader_blur_blend ? "true" : "false");
  write_hl_config(f, "render", e);
  return true;
}

// Cursor.

static HyprlandCursor read_cursor() {
  auto kv = parse_lua_kv(file_path("cursor"));
  HyprlandCursor c;
  c.invisible = bool_val(kv, "invisible", false);
  c.no_hardware_cursors = int_val(kv, "no_hardware_cursors", 2);
  c.enable_hyprcursor = bool_val(kv, "enable_hyprcursor", true);
  c.sync_gsettings_theme = bool_val(kv, "sync_gsettings_theme", true);
  c.min_refresh_rate = int_val(kv, "min_refresh_rate", 24);
  c.hotspot_padding = int_val(kv, "hotspot_padding", 0);
  c.inactive_timeout = double_val(kv, "inactive_timeout", 0.0);
  c.no_warps = bool_val(kv, "no_warps", false);
  c.persistent_warps = bool_val(kv, "persistent_warps", false);
  c.warp_on_change_workspace = int_val(kv, "warp_on_change_workspace", 0);
  c.warp_on_toggle_special = int_val(kv, "warp_on_toggle_special", 0);
  c.default_monitor = str_val(kv, "default_monitor", "");
  c.zoom_factor = double_val(kv, "zoom_factor", 1.0);
  c.zoom_rigid = bool_val(kv, "zoom_rigid", false);
  c.zoom_detached_camera = bool_val(kv, "zoom_detached_camera", true);
  c.hide_on_key_press = bool_val(kv, "hide_on_key_press", false);
  c.hide_on_touch = bool_val(kv, "hide_on_touch", true);
  c.hide_on_tablet = bool_val(kv, "hide_on_tablet", false);
  c.warp_back_after_non_mouse_input = bool_val(kv, "warp_back_after_non_mouse_input", false);
  c.zoom_disable_aa = bool_val(kv, "zoom_disable_aa", false);
  c.no_break_fs_vrr = int_val(kv, "no_break_fs_vrr", 2);
  c.use_cpu_buffer = int_val(kv, "use_cpu_buffer", 2);
  c.ecosystem.no_update_news = bool_val(kv, "ecosystem.no_update_news", false);
  c.ecosystem.no_donation_nag = bool_val(kv, "ecosystem.no_donation_nag", false);
  c.ecosystem.enforce_permissions = bool_val(kv, "ecosystem.enforce_permissions", false);
  return c;
}

bool write_cursor(const HyprlandCursor& c) {
  std::ofstream f(file_path("cursor"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Cursor settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("invisible", c.invisible ? "true" : "false");
  entries.emplace_back("no_hardware_cursors", std::to_string(c.no_hardware_cursors));
  entries.emplace_back("enable_hyprcursor", c.enable_hyprcursor ? "true" : "false");
  entries.emplace_back("sync_gsettings_theme", c.sync_gsettings_theme ? "true" : "false");
  entries.emplace_back("min_refresh_rate", std::to_string(c.min_refresh_rate));
  entries.emplace_back("hotspot_padding", std::to_string(c.hotspot_padding));
  entries.emplace_back("inactive_timeout", std::to_string(c.inactive_timeout));
  entries.emplace_back("no_warps", c.no_warps ? "true" : "false");
  entries.emplace_back("persistent_warps", c.persistent_warps ? "true" : "false");
  entries.emplace_back("warp_on_change_workspace", std::to_string(c.warp_on_change_workspace));
  entries.emplace_back("warp_on_toggle_special", std::to_string(c.warp_on_toggle_special));
  if (!c.default_monitor.empty())
    entries.emplace_back("default_monitor", escape_lua(c.default_monitor));
  entries.emplace_back("zoom_factor", std::to_string(c.zoom_factor));
  entries.emplace_back("zoom_rigid", c.zoom_rigid ? "true" : "false");
  entries.emplace_back("zoom_detached_camera", c.zoom_detached_camera ? "true" : "false");
  entries.emplace_back("hide_on_key_press", c.hide_on_key_press ? "true" : "false");
  entries.emplace_back("hide_on_touch", c.hide_on_touch ? "true" : "false");
  entries.emplace_back("hide_on_tablet", c.hide_on_tablet ? "true" : "false");
  entries.emplace_back("warp_back_after_non_mouse_input", c.warp_back_after_non_mouse_input ? "true" : "false");
  entries.emplace_back("zoom_disable_aa", c.zoom_disable_aa ? "true" : "false");
  entries.emplace_back("no_break_fs_vrr", std::to_string(c.no_break_fs_vrr));
  entries.emplace_back("use_cpu_buffer", std::to_string(c.use_cpu_buffer));
  entries.emplace_back("ecosystem.no_update_news", c.ecosystem.no_update_news ? "true" : "false");
  entries.emplace_back("ecosystem.no_donation_nag", c.ecosystem.no_donation_nag ? "true" : "false");
  entries.emplace_back("ecosystem.enforce_permissions", c.ecosystem.enforce_permissions ? "true" : "false");
  write_hl_config(f, "cursor", entries);
  return true;
}

// Gestures.

HyprlandGestures read_gestures() {
  auto kv = parse_lua_kv(file_path("gestures"));
  HyprlandGestures g;
  g.workspace_swipe_distance = int_val(kv, "workspace_swipe_distance", 300);
  g.workspace_swipe_create_new = bool_val(kv, "workspace_swipe_create_new", true);
  g.workspace_swipe_touch = bool_val(kv, "workspace_swipe_touch", false);
  g.workspace_swipe_invert = bool_val(kv, "workspace_swipe_invert", true);
  g.workspace_swipe_touch_invert = bool_val(kv, "workspace_swipe_touch_invert", true);
  g.workspace_swipe_min_speed_to_force = int_val(kv, "workspace_swipe_min_speed_to_force", 300);
  g.workspace_swipe_cancel_ratio = double_val(kv, "workspace_swipe_cancel_ratio", 0.2);
  g.workspace_swipe_direction_lock = bool_val(kv, "workspace_swipe_direction_lock", true);
  g.workspace_swipe_direction_lock_threshold = int_val(kv, "workspace_swipe_direction_lock_threshold", 10);
  g.workspace_swipe_forever = bool_val(kv, "workspace_swipe_forever", false);
  g.workspace_swipe_use_r = bool_val(kv, "workspace_swipe_use_r", false);
  g.close_max_timeout = int_val(kv, "close_max_timeout", 1000);
  g.move_snap_to_grid = bool_val(kv, "scrolling.move_snap_to_grid", true);
  g.move_snap_cursor = bool_val(kv, "scrolling.move_snap_cursor", true);
  return g;
}

bool write_gestures(const HyprlandGestures& g) {
  std::ofstream f(file_path("gestures"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Gesture settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("workspace_swipe_distance", std::to_string(g.workspace_swipe_distance));
  entries.emplace_back("workspace_swipe_create_new", g.workspace_swipe_create_new ? "true" : "false");
  entries.emplace_back("workspace_swipe_touch", g.workspace_swipe_touch ? "true" : "false");
  entries.emplace_back("workspace_swipe_invert", g.workspace_swipe_invert ? "true" : "false");
  entries.emplace_back("workspace_swipe_touch_invert", g.workspace_swipe_touch_invert ? "true" : "false");
  entries.emplace_back("workspace_swipe_min_speed_to_force", std::to_string(g.workspace_swipe_min_speed_to_force));
  entries.emplace_back("workspace_swipe_cancel_ratio", std::to_string(g.workspace_swipe_cancel_ratio));
  entries.emplace_back("workspace_swipe_direction_lock", g.workspace_swipe_direction_lock ? "true" : "false");
  entries.emplace_back("workspace_swipe_direction_lock_threshold", std::to_string(g.workspace_swipe_direction_lock_threshold));
  entries.emplace_back("workspace_swipe_forever", g.workspace_swipe_forever ? "true" : "false");
  entries.emplace_back("workspace_swipe_use_r", g.workspace_swipe_use_r ? "true" : "false");
  entries.emplace_back("close_max_timeout", std::to_string(g.close_max_timeout));
  entries.emplace_back("scrolling.move_snap_to_grid", g.move_snap_to_grid ? "true" : "false");
  entries.emplace_back("scrolling.move_snap_cursor", g.move_snap_cursor ? "true" : "false");
  write_hl_config(f, "gestures", entries);
  return true;
}

// Group.

HyprlandGroup read_group() {
  auto kv = parse_lua_kv(file_path("group"));
  HyprlandGroup g;
  g.auto_group = bool_val(kv, "auto_group", true);
  g.insert_after_current = bool_val(kv, "insert_after_current", true);
  g.focus_removed_window = bool_val(kv, "focus_removed_window", true);
  g.drag_into_group = int_val(kv, "drag_into_group", 1);
  g.merge_groups_on_drag = bool_val(kv, "merge_groups_on_drag", true);
  g.merge_groups_on_groupbar = bool_val(kv, "merge_groups_on_groupbar", true);
  g.merge_floated_into_tiled_on_groupbar = bool_val(kv, "merge_floated_into_tiled_on_groupbar", false);
  g.group_on_movetoworkspace = bool_val(kv, "group_on_movetoworkspace", false);
  g.col_border_active = str_val(kv, "col.border_active", "");
  g.col_border_inactive = str_val(kv, "col.border_inactive", "");
  g.col_border_locked_active = str_val(kv, "col.border_locked_active", "");
  g.col_border_locked_inactive = str_val(kv, "col.border_locked_inactive", "");
  return g;
}

bool write_group(const HyprlandGroup& g) {
  std::ofstream f(file_path("group"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Group settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("auto_group", g.auto_group ? "true" : "false");
  entries.emplace_back("insert_after_current", g.insert_after_current ? "true" : "false");
  entries.emplace_back("focus_removed_window", g.focus_removed_window ? "true" : "false");
  entries.emplace_back("drag_into_group", std::to_string(g.drag_into_group));
  entries.emplace_back("merge_groups_on_drag", g.merge_groups_on_drag ? "true" : "false");
  entries.emplace_back("merge_groups_on_groupbar", g.merge_groups_on_groupbar ? "true" : "false");
  entries.emplace_back("merge_floated_into_tiled_on_groupbar", g.merge_floated_into_tiled_on_groupbar ? "true" : "false");
  entries.emplace_back("group_on_movetoworkspace", g.group_on_movetoworkspace ? "true" : "false");
  if (!g.col_border_active.empty())
    entries.emplace_back("col.border_active", escape_lua(g.col_border_active));
  if (!g.col_border_inactive.empty())
    entries.emplace_back("col.border_inactive", escape_lua(g.col_border_inactive));
  if (!g.col_border_locked_active.empty())
    entries.emplace_back("col.border_locked_active", escape_lua(g.col_border_locked_active));
  if (!g.col_border_locked_inactive.empty())
    entries.emplace_back("col.border_locked_inactive", escape_lua(g.col_border_locked_inactive));
  write_hl_config(f, "group", entries);
  return true;
}

// Xwayland.

HyprlandXwayland read_xwayland() {
  auto kv = parse_lua_kv(file_path("xwayland"));
  HyprlandXwayland x;
  x.enabled = bool_val(kv, "enabled", true);
  x.use_nearest_neighbor = bool_val(kv, "use_nearest_neighbor", false);
  x.force_zero_scaling = bool_val(kv, "force_zero_scaling", false);
  x.create_abstract_socket = bool_val(kv, "create_abstract_socket", false);
  return x;
}

bool write_xwayland(const HyprlandXwayland& x) {
  std::ofstream f(file_path("xwayland"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Xwayland settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("enabled", x.enabled ? "true" : "false");
  entries.emplace_back("use_nearest_neighbor", x.use_nearest_neighbor ? "true" : "false");
  entries.emplace_back("force_zero_scaling", x.force_zero_scaling ? "true" : "false");
  entries.emplace_back("create_abstract_socket", x.create_abstract_socket ? "true" : "false");
  write_hl_config(f, "xwayland", entries);
  return true;
}

// OpenGL.

HyprlandOpenGL read_opengl() {
  auto kv = parse_lua_kv(file_path("opengl"));
  HyprlandOpenGL o;
  o.nvidia_anti_flicker = bool_val(kv, "nvidia_anti_flicker", true);
  return o;
}

bool write_opengl(const HyprlandOpenGL& o) {
  std::ofstream f(file_path("opengl"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- OpenGL settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("nvidia_anti_flicker", o.nvidia_anti_flicker ? "true" : "false");
  write_hl_config(f, "opengl", entries);
  return true;
}

// Quirks.

HyprlandQuirks read_quirks() {
  auto kv = parse_lua_kv(file_path("quirks"));
  HyprlandQuirks q;
  q.prefer_hdr = int_val(kv, "prefer_hdr", 0);
  q.skip_non_kms_dmabuf_formats = bool_val(kv, "skip_non_kms_dmabuf_formats", false);
  return q;
}

bool write_quirks(const HyprlandQuirks& q) {
  std::ofstream f(file_path("quirks"));
  if (!f.is_open()) return false;
  write_line(f, 0, "-- Quirks settings -- managed by Event Horizon");
  std::vector<std::pair<std::string, std::string>> entries;
  entries.emplace_back("prefer_hdr", std::to_string(q.prefer_hdr));
  entries.emplace_back("skip_non_kms_dmabuf_formats", q.skip_non_kms_dmabuf_formats ? "true" : "false");
  write_hl_config(f, "quirks", entries);
  return true;
}

// Composite read / write / apply.

HyprlandConfig read_config() {
  HyprlandConfig c;
  c.general = read_general();
  c.decoration = read_decoration();
  c.input = read_input();
  c.binds = read_binds();
  c.dwindle = read_dwindle();
  c.master = read_master();
  c.scrolling = read_scrolling();
  c.animations = read_animations();
  c.misc = read_misc();
  c.render = read_render();
  c.cursor = read_cursor();
  c.gestures = read_gestures();
  c.group = read_group();
  c.xwayland = read_xwayland();
  c.opengl = read_opengl();
  c.quirks = read_quirks();
  return c;
}

bool write_all(const HyprlandConfig& c) {
  bool ok = true;
  ok = write_general(c.general) && ok;
  ok = write_decoration(c.decoration) && ok;
  ok = write_input(c.input) && ok;
  ok = write_binds(c.binds) && ok;
  ok = write_dwindle(c.dwindle) && ok;
  ok = write_master(c.master) && ok;
  ok = write_scrolling(c.scrolling) && ok;
  ok = write_animations(c.animations) && ok;
  ok = write_misc(c.misc) && ok;
  ok = write_render(c.render) && ok;
  ok = write_cursor(c.cursor) && ok;
  ok = write_gestures(c.gestures) && ok;
  ok = write_group(c.group) && ok;
  ok = write_xwayland(c.xwayland) && ok;
  ok = write_opengl(c.opengl) && ok;
  ok = write_quirks(c.quirks) && ok;
  return ok;
}

bool apply_config() {
  int ret = std::system("hyprctl reload 2>/dev/null");
  return ret == 0;
}

} // namespace eh::settings_hyprland
