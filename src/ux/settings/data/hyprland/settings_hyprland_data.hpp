#pragma once

#include <string>
#include <vector>

namespace eh::settings_hyprland {

struct HyprlandBlur {
  bool enabled = true;
  bool xray = false;
  bool special = false;
  bool new_optimizations = true;
  bool ignore_opacity = true;
  int size = 8;
  int passes = 1;
  double noise = 0.0117;
  double contrast = 0.8916;
  double brightness = 1.0;
  double vibrancy = 0.1696;
  bool popups = false;
  double popups_ignorealpha = 0.2;
  bool input_methods = false;
  double vibrancy_darkness = 0.0;
  double input_methods_ignorealpha = 0.2;
};

struct HyprlandShadow {
  bool enabled = true;
  int range = 4;
  int render_power = 3;
  bool sharp = false;
  std::string offset = "0, 0";
  double scale = 1.0;
  std::string color = "0xee1a1a1a";
  std::string color_inactive = "";
};

struct HyprlandGlow {
  bool enabled = false;
  int range = 10;
  int render_power = 3;
  std::string color = "0xee33ccff";
  std::string color_inactive = "";
};

struct HyprlandMotionBlur {
  bool enabled = false;
  int samples = 7;
};

struct HyprlandDecoration {
  int rounding = 0;
  double rounding_power = 2.0;
  double active_opacity = 1.0;
  double inactive_opacity = 1.0;
  double fullscreen_opacity = 1.0;
  bool dim_modal = true;
  bool dim_inactive = false;
  double dim_strength = 0.5;
  double dim_special = 0.2;
  double dim_around = 0.4;
  bool border_part_of_window = true;
  HyprlandBlur blur{};
  HyprlandShadow shadow{};
  HyprlandGlow glow{};
  HyprlandMotionBlur motion_blur{};
  bool screen_shader_enabled = false;
  std::string screen_shader = "";
};

struct HyprlandTouchpad {
  bool natural_scroll = false;
  bool disable_while_typing = true;
  bool tap_to_click = true;
  bool clickfinger_behavior = false;
  double scroll_factor = 1.0;
  bool middle_button_emulation = false;
  std::string tap_button_map = "";
  int drag_lock = 0;
  bool tap_and_drag = true;
  bool flip_x = false;
  bool flip_y = false;
  int drag_3fg = 0;
};

struct HyprlandTouchDevice {
  int transform = 0;
  std::string output = "";
  bool enabled = true;
};

struct HyprlandVirtualKeyboard {
  int share_states = 2;
  bool release_pressed_on_close = false;
};

struct HyprlandTablet {
  int transform = 0;
  std::string output = "";
  std::string region_position = "";
  bool absolute_region_position = false;
  std::string region_size = "";
  bool relative_input = false;
  bool left_handed = false;
  std::string active_area_size = "";
  std::string active_area_position = "";
};

struct HyprlandTabletTool {
  int eraser_button_mode = 0;
  int eraser_button_override = 0;
  double pressure_range_min = -1.0;
  double pressure_range_max = -1.0;
};

struct HyprlandInput {
  std::string kb_layout = "us";
  std::string kb_variant = "";
  std::string kb_options = "";
  std::string kb_rules = "";
  std::string kb_model = "";
  std::string kb_file = "";
  bool numlock_by_default = false;
  int repeat_delay = 600;
  int repeat_rate = 25;
  double sensitivity = 0.0;
  std::string accel_profile = "adaptive";
  std::string scroll_method = "2fg";
  bool natural_scroll = false;
  bool resolve_binds_by_sym = false;
  bool force_no_accel = false;
  int rotation = 0;
  bool left_handed = false;
  std::string scroll_points = "";
  int scroll_button = 0;
  bool scroll_button_lock = false;
  double scroll_factor = 1.0;
  int follow_mouse_shrink = 0;
  double follow_mouse_threshold = 0.0;
  int focus_on_close = 0;
  int off_window_axis_events = 1;
  int emulate_discrete_scroll = 1;
  HyprlandTouchpad touchpad{};
  HyprlandTouchDevice touchdevice{};
  HyprlandVirtualKeyboard virtualkeyboard{};
  HyprlandTablet tablet{};
  HyprlandTabletTool tablettool{};
  bool special_fallthrough = false;
  int follow_mouse = 1;
  int float_switch_override_focus = 0;
  bool mouse_refocus = true;
};

struct HyprlandColors {
  std::string active_border = "";
  std::string inactive_border = "";
  std::string nogroup_border = "";
  std::string nogroup_border_active = "";
};

struct HyprlandSnap {
  bool enabled = false;
  int window_gap = 10;
  int monitor_gap = 0;
  bool border_overlap = false;
  bool respect_gaps = false;
};

struct HyprlandGeneral {
  int border_size = 1;
  int gaps_in = 5;
  int gaps_out = 20;
  int float_gaps = 0;
  int gaps_workspaces = 0;
  std::string layout = "dwindle";
  bool allow_tearing = false;
  bool resize_on_border = false;
  HyprlandSnap snap{};
  bool no_focus_fallback = false;
  int extend_border_grab_area = 15;
  bool hover_icon_on_border = true;
  int resize_corner = 0;
  bool modal_parent_blocking = true;
  std::string locale = "";
  HyprlandColors col{};
};

struct HyprlandDwindle {
  int force_split = 0;
  bool preserve_split = false;
  bool smart_split = false;
  bool smart_resizing = true;
  bool permanent_direction_override = false;
  double special_scale_factor = 1.0;
  double split_width_multiplier = 1.0;
  bool use_active_for_splits = true;
  double default_split_ratio = 1.0;
  int split_bias = 0;
  bool precise_mouse_move = false;
};

struct HyprlandMaster {
  bool allow_small_split = false;
  double special_scale_factor = 1.0;
  double mfact = 0.55;
  std::string new_status = "slave";
  bool new_on_top = false;
  std::string new_on_active = "none";
  std::string orientation = "left";
  int slave_count_for_center_master = 2;
  std::string center_master_fallback = "left";
  bool smart_resizing = true;
  bool drop_at_cursor = true;
  bool always_keep_position = false;
  bool focus_master_on_close = false;
};

struct HyprlandScrolling {
  double column_width = 0.5;
  std::string direction = "right";
  bool fullscreen_on_one_column = true;
  bool follow_focus = true;
  double follow_min_visible = 0.4;
  int focus_fit_method = 1;
  std::string explicit_column_widths = "0.333,0.5,0.62,0.75,1.0";
  bool wrap_focus = true;
  bool wrap_swapcol = true;
};

struct HyprlandAnimationEntry {
  std::string name;
  bool enabled = true;
  double speed = 3.0;
  std::string curve = "default";
  std::string style;
};

struct HyprlandAnimations {
  bool enabled = true;
  bool workspace_wraparound = false;
  std::vector<HyprlandAnimationEntry> entries;
};

struct HyprlandBinds {
  int scroll_event_delay = 0;
  int focus_preferred_method = 0;
  bool workspace_back_and_forth = false;
  bool allow_workspace_cycles = true;
  bool pass_mouse_when_bound = false;
  bool hide_special_on_workspace_change = false;
  int workspace_center_on = 0;
  bool ignore_group_lock = false;
  bool movefocus_cycles_fullscreen = false;
  bool movefocus_cycles_groupfirst = false;
  bool window_direction_monitor_fallback = true;
  bool disable_keybind_grabbing = false;
  bool allow_pin_fullscreen = false;
  int drag_threshold = 0;
};

struct HyprlandMisc {
  int vrr = 0;
  bool animate_manual_resizes = false;
  bool animate_mouse_windowdragging = false;
  bool enable_swallow = false;
  std::string swallow_regex = "(foot|kitty|allacritty|Alacritty)";
  std::string swallow_exception_regex = "";
  bool disable_hyprland_logo = false;
  bool disable_splash_rendering = false;
  bool disable_scale_notification = false;
  std::string col_splash = "";
  int force_default_wallpaper = -1;
  bool allow_session_lock_restore = false;
  int initial_workspace_tracking = 1;
  bool disable_autoreload = false;
  std::string background_color = "";
  std::string font_family = "Sans";
  std::string splash_font_family = "";
  bool mouse_move_enables_dpms = false;
  bool key_press_enables_dpms = false;
  bool name_vk_after_proc = true;
  bool always_follow_on_dnd = true;
  bool layers_hog_keyboard_focus = true;
  bool focus_on_activate = false;
  bool mouse_move_focuses_monitor = true;
  bool session_lock_xray = false;
  bool session_lock_blur = false;
  bool close_special_on_empty = true;
  int on_focus_under_fullscreen = 2;
  bool exit_window_retains_fullscreen = false;
  int initial_workspace_token_timeout = 10;
  int render_unfocused_fps = 15;
  bool disable_xdg_env_checks = false;
  bool disable_hyprland_guiutils_check = false;
  int lockdead_screen_delay = 1000;
  bool enable_anr_dialog = true;
  int anr_missed_pings = 5;
  bool size_limits_tiled = false;
  bool screencopy_force_8b = true;
  bool disable_watchdog_warning = false;
  bool middle_click_paste = true;
};

struct HyprlandRender {
  bool new_render_scheduling = false;
  bool expand_undersized_textures = true;
  int direct_scanout = 2;
  bool cm_enabled = true;
  bool send_content_type = true;
  int fp16_sdr_tf = 0;
  bool xp_mode = false;
  int ctm_animation = 1;
  int cm_auto_hdr = 0;
  int non_shader_cm = 1;
  int non_shader_cm_interop = 0;
  std::string cm_sdr_eotf = "";
  bool commit_timing_enabled = true;
  int use_fp16 = 2;
  int keep_unmodified_copy = 0;
  bool icc_vcgt_enabled = false;
  bool use_shader_blur_blend = false;
};

struct HyprlandCursorEcosystem {
  bool no_update_news = false;
  bool no_donation_nag = false;
  bool enforce_permissions = false;
};

struct HyprlandCursor {
  bool invisible = false;
  int no_hardware_cursors = 2;
  bool enable_hyprcursor = true;
  bool sync_gsettings_theme = true;
  int min_refresh_rate = 24;
  int hotspot_padding = 0;
  double inactive_timeout = 0.0;
  bool no_warps = false;
  bool persistent_warps = false;
  int warp_on_change_workspace = 0;
  int warp_on_toggle_special = 0;
  std::string default_monitor = "";
  double zoom_factor = 1.0;
  bool zoom_rigid = false;
  bool zoom_detached_camera = true;
  bool hide_on_key_press = false;
  bool hide_on_touch = true;
  bool hide_on_tablet = false;
  bool warp_back_after_non_mouse_input = false;
  bool zoom_disable_aa = false;
  int no_break_fs_vrr = 2;
  int use_cpu_buffer = 2;
  HyprlandCursorEcosystem ecosystem{};
};

struct HyprlandGestures {
  int workspace_swipe_distance = 300;
  bool workspace_swipe_create_new = true;
  bool workspace_swipe_touch = false;
  bool workspace_swipe_invert = true;
  bool workspace_swipe_touch_invert = true;
  int workspace_swipe_min_speed_to_force = 300;
  double workspace_swipe_cancel_ratio = 0.2;
  bool workspace_swipe_direction_lock = true;
  int workspace_swipe_direction_lock_threshold = 10;
  bool workspace_swipe_forever = false;
  bool workspace_swipe_use_r = false;
  int close_max_timeout = 1000;
  bool move_snap_to_grid = true;
  bool move_snap_cursor = true;
};

struct HyprlandGroup {
  bool auto_group = true;
  bool insert_after_current = true;
  bool focus_removed_window = true;
  int drag_into_group = 1;
  bool merge_groups_on_drag = true;
  bool merge_groups_on_groupbar = true;
  bool merge_floated_into_tiled_on_groupbar = false;
  bool group_on_movetoworkspace = false;
  std::string col_border_active = "";
  std::string col_border_inactive = "";
  std::string col_border_locked_active = "";
  std::string col_border_locked_inactive = "";
};

struct HyprlandXwayland {
  bool enabled = true;
  bool use_nearest_neighbor = false;
  bool force_zero_scaling = false;
  bool create_abstract_socket = false;
};

struct HyprlandOpenGL {
  bool nvidia_anti_flicker = true;
};

struct HyprlandQuirks {
  int prefer_hdr = 0;
  bool skip_non_kms_dmabuf_formats = false;
};

struct HyprlandConfig {
  HyprlandGeneral general{};
  HyprlandDecoration decoration{};
  HyprlandInput input{};
  HyprlandBinds binds{};
  HyprlandDwindle dwindle{};
  HyprlandMaster master{};
  HyprlandScrolling scrolling{};
  HyprlandAnimations animations{};
  HyprlandMisc misc{};
  HyprlandRender render{};
  HyprlandCursor cursor{};
  HyprlandGestures gestures{};
  HyprlandGroup group{};
  HyprlandXwayland xwayland{};
  HyprlandOpenGL opengl{};
  HyprlandQuirks quirks{};
};

std::string hyprland_config_dir();
HyprlandConfig read_config();
bool write_general(const HyprlandGeneral& cfg);
bool write_decoration(const HyprlandDecoration& cfg);
bool write_input(const HyprlandInput& cfg);
bool write_binds(const HyprlandBinds& cfg);
bool write_dwindle(const HyprlandDwindle& cfg);
bool write_master(const HyprlandMaster& cfg);
bool write_scrolling(const HyprlandScrolling& cfg);
bool write_animations(const HyprlandAnimations& cfg);
bool write_misc(const HyprlandMisc& cfg);
bool write_render(const HyprlandRender& cfg);
bool write_cursor(const HyprlandCursor& cfg);
bool write_gestures(const HyprlandGestures& cfg);
bool write_group(const HyprlandGroup& cfg);
bool write_xwayland(const HyprlandXwayland& cfg);
bool write_opengl(const HyprlandOpenGL& cfg);
bool write_quirks(const HyprlandQuirks& cfg);
bool write_all(const HyprlandConfig& cfg);
bool apply_config();

} // namespace eh::settings_hyprland
