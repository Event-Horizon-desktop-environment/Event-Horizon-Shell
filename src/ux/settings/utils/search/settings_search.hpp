#pragma once

// Global settings search index: every tab + every major setting.
// Header-only so no build-system changes are needed.

#include <cctype>
#include <string>
#include <vector>

#include "desktop_shell/unified/compositor_kind.hpp"

struct SettingsSearchEntry {
  int tab = -1;              // activeTab to navigate to
  const char* title = "";    // setting name, e.g. "Dock Opacity"
  const char* section = "";  // tab / category name, e.g. "Dock"
  const char* keywords = ""; // extra searchable aliases, space separated
  const char* glyph = "";    // material symbols ligature
};

// Search-bar geometry. Must stay in sync with settings_common.hpp layout
// constants (kSpacingL=16, kContentTop=16, kSidebarW=260).
static constexpr int kSettingsSearchBarH = 36;
static constexpr int kSettingsSearchResultH = 48;
static constexpr int kSettingsSearchResultPitch = 52;

static inline void settings_search_bar_geom(int* x, int* y, int* w, int* h) {
  if (x) *x = 16 + 8;          // kSpacingL + 8
  if (y) *y = 16 + 8;          // kContentTop + 8
  if (w) *w = 260 - 16;        // kSidebarW - 16
  if (h) *h = kSettingsSearchBarH;
}

// Tabs start below the fixed search bar.
static inline int settings_sidebar_tabs_base_y() {
  return 16 + 8 + kSettingsSearchBarH + 8;  // kContentTop + 52 = 68
}

static const SettingsSearchEntry kSettingsSearchEntries[] = {
    // ── Tabs (so every tab name itself is searchable) ──
    {0, "Dock", "Panels & UI", "dock bar app launcher", "dock_to_bottom"},
    {1, "Panel", "Panels & UI", "panel top bar indicators", "dock_to_top"},
    {11, "Taskbar", "Panels & UI", "taskbar window list", "dock_to_bottom"},
    {9, "Launcher", "Panels & UI", "launcher app grid overview workspaces", "apps"},
    {29, "Desktop", "Panels & UI", "desktop icons menu", "desktop_windows"},
    {27, "Desktop Widgets", "Panels & UI", "desktop widgets clock weather", "widgets"},
    {7, "Monitors", "Display", "monitors displays screens resolution refresh", "monitor"},
    {2, "Appearance", "Display", "appearance theme matugen colors opacity", "palette"},
    {45, "Themes", "Display", "themes gtk qt cursor plasma", "palette"},
    {50, "Color Themes", "Display", "color themes custom matugen engine", "colorize"},
    {44, "Icons", "Display", "icons theme", "photo_library"},
    {19, "Nightlight", "Display", "night light nightlight temperature schedule", "dark_mode"},
    {20, "MangoWM Decoration", "MangoWM", "mango decoration borders opacity shadows corners", "border_style"},
    {21, "MangoWM Colors", "MangoWM", "mango colors window border focus", "palette"},
    {22, "MangoWM Animations", "MangoWM", "mango animations duration fade", "play_arrow"},
    {23, "MangoWM Keybinds", "MangoWM", "mango keybinds shortcuts keys", "keyboard"},
    {24, "MangoWM Layout", "MangoWM", "mango layout gaps master stack scroller", "grid_view"},
    {25, "MangoWM Input", "MangoWM", "mango input keyboard mouse trackpad", "mouse"},
    {26, "MangoWM Misc", "MangoWM", "mango misc focus cursor window behavior", "tune"},
    {33, "Hyprland", "Hyprland", "hyprland compositor general decoration input binds animations", "view_module"},
    {5, "Notifications", "System", "notifications dnd toast banner", "notifications"},
    {8, "Sound", "System", "sound audio volume output input devices engine", "volume_up"},
    {10, "Default Apps", "System", "default apps browser email terminal", "app_registration"},
    {30, "Time", "System", "time clock date timezone ntp", "schedule"},
    {31, "Keyboard & Language", "System", "keyboard language layout repeat locale", "keyboard"},
    {47, "Bluetooth", "System", "bluetooth adapter devices pairing", "bluetooth"},
    {32, "Power", "System", "power sleep suspend battery performance cpu", "power_settings_new"},
    {46, "Accounts", "System", "accounts users password hostname login", "account_circle"},
    {48, "Startup", "System", "startup autostart login apps", "play_arrow"},
    {6, "Wallpaper", "Wallpaper Settings", "wallpaper background image gallery", "wallpaper"},
    {16, "Bing Wallpaper", "Wallpaper Settings", "bing wallpaper daily archive download", "photo_library"},
    {17, "Wi-Fi", "Network", "wifi wireless network", "wifi"},
    {18, "Wired", "Network", "wired ethernet ipv4 ipv6 connection", "lan"},
    {28, "VPN", "Network", "vpn tunnel", "vpn_key"},

    // ── Dock (0) ──
    {0, "Show dock", "Dock", "dock show enable visible", "dock_to_bottom"},
    {0, "Auto-hide dock", "Dock", "dock autohide hide", "dock_to_bottom"},
    {0, "Group running windows", "Dock", "dock group windows", "dock_to_bottom"},
    {0, "Dock tooltips", "Dock", "dock tooltips hover", "dock_to_bottom"},
    {0, "Pinned apps tray pill", "Dock", "dock pinned tray pill", "dock_to_bottom"},
    {0, "Running apps tray pill", "Dock", "dock running tray pill", "dock_to_bottom"},
    {0, "Display", "Dock", "dock display monitor output auto all", "monitor"},
    {0, "Shell renderer", "Dock", "dock renderer vulkan cairo gpu cpu", "memory"},
    {0, "Corner Radius", "Dock", "dock corner radius round", "rounded_corners"},
    {0, "Dock Scale", "Dock", "dock scale size ui", "zoom_in"},
    {0, "Icon Size", "Dock", "dock icon size", "apps"},
    {0, "Icon Spacing", "Dock", "dock icon spacing gap", "space_bar"},
    {0, "Dock Opacity", "Dock", "dock opacity transparency", "opacity"},
    {0, "Dock Border", "Dock", "dock border outline", "border_style"},
    {0, "Border Size", "Dock", "dock border size thickness", "border_style"},
    {0, "Border Opacity", "Dock", "dock border opacity transparency", "opacity"},
    {0, "Liquid Glass", "Dock", "dock liquid glass blur", "blur_on"},
    {0, "Colored Glass", "Dock", "dock colored glass tint", "palette"},
    {0, "Dock Margin", "Dock", "dock margin bottom gap", "margins"},
    {0, "Exclusive Zone", "Dock", "dock exclusive zone reserve space", "space_bar"},
    {0, "Dock widgets", "Dock", "dock widgets left center right add", "widgets"},

    // ── Panel (1) ──
    {1, "Show panel", "Panel", "panel show enable visible", "dock_to_top"},
    {1, "Width mode", "Panel", "panel width floating edge fill", "width"},
    {1, "Position at top", "Panel", "panel position top bottom", "vertical_align_top"},
    {1, "Auto-hide", "Panel", "panel autohide hide", "visibility_off"},
    {1, "Tooltips", "Panel", "panel tooltips hover", "tooltip"},
    {1, "Display", "Panel", "panel display monitor output", "monitor"},
    {1, "Height", "Panel", "panel height size", "height"},
    {1, "Corner Radius", "Panel", "panel corner radius", "rounded_corners"},
    {1, "Opacity", "Panel", "panel opacity transparency", "opacity"},
    {1, "Icon Size", "Panel", "panel icon size", "apps"},
    {1, "Icon Spacing", "Panel", "panel icon spacing", "space_bar"},
    {1, "Panel Border", "Panel", "panel border", "border_style"},
    {1, "Scale", "Panel", "panel scale", "zoom_in"},
    {1, "Panel Margin", "Panel", "panel margin gap", "margins"},
    {1, "Exclusive Zone", "Panel", "panel exclusive zone", "space_bar"},
    {1, "Panel widgets", "Panel", "panel widgets left center right add", "widgets"},

    // ── Taskbar (11) ──
    {11, "Show taskbar", "Taskbar", "taskbar show enable", "dock_to_bottom"},
    {11, "Width mode", "Taskbar", "taskbar width floating edge fill", "width"},
    {11, "Position at top", "Taskbar", "taskbar position top bottom", "vertical_align_top"},
    {11, "Group running windows", "Taskbar", "taskbar group windows", "group_work"},
    {11, "Auto-hide", "Taskbar", "taskbar autohide", "visibility_off"},
    {11, "Show window labels", "Taskbar", "taskbar labels titles text", "label"},
    {11, "Collapse when full", "Taskbar", "taskbar collapse overflow", "compress"},
    {11, "Window previews", "Taskbar", "taskbar thumbnails previews", "preview"},
    {11, "Enlarge preview on hover", "Taskbar", "taskbar thumbnail peek hover enlarge", "zoom_in"},
    {11, "Preview threshold", "Taskbar", "taskbar thumbnail threshold count", "numbers"},
    {11, "Compact media", "Taskbar", "taskbar compact media player", "music_note"},
    {11, "Display", "Taskbar", "taskbar display monitor", "monitor"},
    {11, "Height", "Taskbar", "taskbar height", "height"},
    {11, "Corner Radius", "Taskbar", "taskbar corner radius", "rounded_corners"},
    {11, "Opacity", "Taskbar", "taskbar opacity", "opacity"},
    {11, "Taskbar widgets", "Taskbar", "taskbar widgets add", "widgets"},

    // ── Launcher (9) ──
    {9, "Layout scale", "Launcher", "launcher layout scale size", "zoom_in"},
    {9, "Icon size", "Launcher", "launcher icon size", "apps"},
    {9, "Icon spacing", "Launcher", "launcher icon spacing gap folders", "space_bar"},
    {9, "Folder size", "Launcher", "launcher folder size", "folder"},
    {9, "Columns", "Launcher", "launcher grid columns", "grid_view"},
    {9, "Rows", "Launcher", "launcher grid rows", "grid_view"},
    {9, "DPI scale", "Launcher", "launcher dpi scale", "zoom_in"},
    {9, "Widget opacity", "Launcher", "launcher widget opacity", "opacity"},
    {9, "Number of workspaces", "Launcher", "launcher workspaces number count slots", "workspaces"},
    {9, "Show app icons", "Launcher", "launcher workspaces show app icons", "apps"},
    {9, "Live updates", "Launcher", "overview live updates stream", "live_tv"},
    {9, "Multi-monitor", "Launcher", "overview multi monitor", "monitor"},
    {9, "Capture mode", "Launcher", "overview capture snapshot screencopy", "screenshot_monitor"},
    {9, "Scroll axis", "Launcher", "overview scroll axis vertical horizontal", "swap_vert"},
    {9, "Card scale", "Launcher", "overview card scale size", "zoom_in"},
    {9, "Search bar width", "Launcher", "overview search width", "search"},

    // ── Desktop / widgets ──
    {29, "Show desktop", "Desktop", "desktop show icons menu widgets", "desktop_windows"},
    {27, "Display", "Desktop Widgets", "desktop widgets display monitor", "monitor"},
    {27, "Add widget", "Desktop Widgets", "desktop widgets add world clock", "add"},
    {27, "World Clock", "Desktop Widgets", "desktop widgets world clock timezone cities", "schedule"},

    // ── Monitors (7) ──
    {7, "Refresh", "Monitors", "monitors refresh rescan", "refresh"},
    {7, "Apply", "Monitors", "monitors apply save", "check"},
    {7, "Revert", "Monitors", "monitors revert undo", "undo"},
    {7, "Resolution", "Monitors", "monitors resolution size mode", "resolution"},
    {7, "Refresh rate", "Monitors", "monitors refresh rate hz", "refresh"},
    {7, "VRR", "Monitors", "monitors vrr variable refresh freesync", "sync"},
    {7, "Scale", "Monitors", "monitors scale hidpi fractional", "zoom_in"},
    {7, "Transform", "Monitors", "monitors transform rotation orientation flip", "screen_rotation"},
    {7, "Bit depth", "Monitors", "monitors bit depth 10bit color", "colorize"},
    {7, "Color profile", "Monitors", "monitors icc icm color profile", "palette"},
    {7, "HDR support", "Monitors", "monitors hdr support", "hdr_on"},
    {7, "Wide color gamut", "Monitors", "monitors wide color gamut", "palette"},
    {7, "SDR brightness", "Monitors", "monitors sdr brightness", "brightness_6"},
    {7, "SDR saturation", "Monitors", "monitors sdr saturation", "invert_colors"},
    {7, "Luminance", "Monitors", "monitors luminance nits hdr sdr min max", "light_mode"},

    // ── Appearance (2) ──
    {2, "Wallpaper tint", "Appearance", "appearance wallpaper tint", "wallpaper"},
    {2, "Color engine", "Appearance", "appearance color engine matugen horizon", "palette"},
    {2, "Color scheme", "Appearance", "appearance matugen scheme content expressive tonal vibrant", "palette"},
    {2, "Theme mode", "Appearance", "appearance dark light mode", "dark_mode"},
    {2, "Brightness", "Appearance", "appearance brightness", "brightness_6"},
    {2, "Contrast", "Appearance", "appearance contrast", "contrast"},
    {2, "Vibrance", "Appearance", "appearance vibrance saturation", "invert_colors"},
    {2, "Gamma", "Appearance", "appearance gamma", "gamma"},
    {2, "Master opacity", "Appearance", "appearance master opacity", "opacity"},
    {2, "External matugen templates", "Appearance", "appearance templates matugen niri hyprland gtk kitty alacritty vscode firefox", "description"},

    // ── Themes / colors / icons / nightlight ──
    {45, "Color scheme", "Themes", "themes qt color scheme gtk cursor plasma", "palette"},
    {45, "Cursor size", "Themes", "themes cursor size", "mouse"},
    {50, "Matugen", "Color Themes", "color themes matugen source", "palette"},
    {50, "Color Engine", "Color Themes", "color themes engine source", "colorize"},
    {50, "Custom Theme", "Color Themes", "color themes custom user theme", "brush"},
    {44, "Icon theme", "Icons", "icons theme system apply", "photo_library"},
    {19, "Enable Night Light", "Nightlight", "nightlight enable night light", "dark_mode"},
    {19, "Day Temperature", "Nightlight", "nightlight day temperature kelvin", "light_mode"},
    {19, "Night Temperature", "Nightlight", "nightlight night temperature kelvin", "dark_mode"},
    {19, "Schedule Mode", "Nightlight", "nightlight schedule manual sunset sunrise", "schedule"},
    {19, "Start Time", "Nightlight", "nightlight start time", "schedule"},
    {19, "End Time", "Nightlight", "nightlight end time", "schedule"},

    // ── MangoWM (20-26) ──
    {20, "Border width", "MangoWM Decoration", "mango border width", "border_style"},
    {20, "Corner radius", "MangoWM Decoration", "mango corner radius", "rounded_corners"},
    {20, "Focused opacity", "MangoWM Decoration", "mango opacity focused", "opacity"},
    {20, "Unfocused opacity", "MangoWM Decoration", "mango opacity unfocused", "opacity"},
    {20, "Enable shadows", "MangoWM Decoration", "mango shadows", "blur_on"},
    {21, "Window Colors", "MangoWM Colors", "mango colors border focus urgent", "palette"},
    {22, "Enable animations", "MangoWM Animations", "mango animations enable", "play_arrow"},
    {22, "Move duration", "MangoWM Animations", "mango animation move duration ms", "timer"},
    {22, "Open duration", "MangoWM Animations", "mango animation open duration", "timer"},
    {22, "Close duration", "MangoWM Animations", "mango animation close duration", "timer"},
    {23, "Keybinds", "MangoWM Keybinds", "mango keybinds shortcuts", "keyboard"},
    {24, "Inner gap", "MangoWM Layout", "mango gaps inner", "space_bar"},
    {24, "Outer gap", "MangoWM Layout", "mango gaps outer", "space_bar"},
    {24, "Smart gaps", "MangoWM Layout", "mango smart gaps", "space_bar"},
    {24, "Master factor", "MangoWM Layout", "mango master factor stack", "grid_view"},
    {25, "Repeat rate", "MangoWM Input", "mango keyboard repeat rate", "keyboard"},
    {25, "Repeat delay", "MangoWM Input", "mango keyboard repeat delay", "keyboard"},
    {25, "NumLock on startup", "MangoWM Input", "mango numlock", "keyboard"},
    {25, "Natural scrolling", "MangoWM Input", "mango mouse natural scroll trackpad", "mouse"},
    {25, "Tap to click", "MangoWM Input", "mango trackpad tap click", "touch_app"},
    {26, "Cursor size", "MangoWM Misc", "mango cursor size theme", "mouse"},
    {26, "Focus on activate", "MangoWM Misc", "mango focus activate sloppy warp", "center_focus_strong"},

    // ── Hyprland (33) ──
    {33, "Layout", "Hyprland", "hyprland layout dwindle master scrolling monocle", "grid_view"},
    {33, "Rounding", "Hyprland", "hyprland decoration rounding corners", "rounded_corners"},
    {33, "Opacity", "Hyprland", "hyprland decoration opacity", "opacity"},
    {33, "Blur", "Hyprland", "hyprland decoration blur", "blur_on"},
    {33, "Shadow", "Hyprland", "hyprland decoration shadow glow", "blur_on"},
    {33, "Keyboard", "Hyprland", "hyprland input keyboard", "keyboard"},
    {33, "Mouse", "Hyprland", "hyprland input mouse", "mouse"},
    {33, "Touchpad", "Hyprland", "hyprland input touchpad gestures", "touch_app"},
    {33, "Animations", "Hyprland", "hyprland animations bezier speed curve", "play_arrow"},
    {33, "VRR", "Hyprland", "hyprland vrr misc render", "sync"},
    {33, "Cursor", "Hyprland", "hyprland cursor zoom", "mouse"},
    {33, "XWayland", "Hyprland", "hyprland xwayland compat", "computer"},

    // ── Notifications (5) ──
    {5, "Position", "Notifications", "notifications position toast top right left bottom", "place"},
    {5, "Display", "Notifications", "notifications display monitor", "monitor"},
    {5, "Do not disturb", "Notifications", "notifications dnd disturb server", "do_not_disturb"},
    {5, "Default timeout", "Notifications", "notifications timeout duration ms", "timer"},
    {5, "Edge margin", "Notifications", "notifications margin edge", "margins"},
    {5, "Corner radius", "Notifications", "notifications corner radius", "rounded_corners"},
    {5, "Max width", "Notifications", "notifications max width", "width"},
    {5, "Layer-shell toasts", "Notifications", "notifications layer shell toasts", "layers"},
    {5, "Send a test notification", "Notifications", "notifications test send", "send"},

    // ── Sound (8) ──
    {8, "Default output", "Sound", "sound default output sink speaker volume", "volume_up"},
    {8, "Default input", "Sound", "sound default input source mic volume", "mic"},
    {8, "Allow volume above 100%", "Sound", "sound overamp volume above 100", "volume_up"},
    {8, "Test Speakers", "Sound", "sound speaker test", "speaker"},
    {8, "Default rate", "Sound", "sound engine rate hz clock", "speed"},
    {8, "Default quantum", "Sound", "sound engine quantum buffer latency", "timer"},
    {8, "Restart audio services", "Sound", "sound restart pipewire wireplumber audio", "restart_alt"},
    {8, "Playback", "Sound", "sound playback apps streams", "play_arrow"},
    {8, "Recording", "Sound", "sound recording apps streams mic", "fiber_manual_record"},

    // ── Default Apps (10) ──
    {10, "Web browser", "Default Apps", "default apps web browser", "language"},
    {10, "Email", "Default Apps", "default apps email mail", "mail"},
    {10, "Calendar", "Default Apps", "default apps calendar", "calendar_month"},
    {10, "File manager", "Default Apps", "default apps file manager files", "folder"},
    {10, "Terminal", "Default Apps", "default apps terminal console", "terminal"},
    {10, "Audio", "Default Apps", "default apps audio music", "music_note"},
    {10, "Video", "Default Apps", "default apps video", "movie"},
    {10, "Images", "Default Apps", "default apps images photos", "image"},
    {10, "PDF", "Default Apps", "default apps pdf documents", "picture_as_pdf"},

    // ── Time (30) ──
    {30, "Use 24-hour format", "Time", "time 24 hour format clock", "schedule"},
    {30, "Show seconds", "Time", "time show seconds clock", "timer"},
    {30, "Show date", "Time", "time show date calendar", "calendar_month"},
    {30, "Date display style", "Time", "time date format style iso custom", "calendar_month"},
    {30, "Automatic time (NTP)", "Time", "time ntp automatic network sync", "sync"},
    {30, "Timezone", "Time", "time timezone region city system", "public"},

    // ── Keyboard (31) ──
    {31, "Keyboard layout", "Keyboard & Language", "keyboard layout us input source", "keyboard"},
    {31, "Switch layout shortcut", "Keyboard & Language", "keyboard switch shortcut alt shift super", "keyboard"},
    {31, "Caps Lock behavior", "Keyboard & Language", "keyboard caps lock ctrl esc", "keyboard"},
    {31, "Compose key", "Keyboard & Language", "keyboard compose key", "keyboard"},
    {31, "Repeat rate", "Keyboard & Language", "keyboard repeat rate speed cps", "keyboard"},
    {31, "Repeat delay", "Keyboard & Language", "keyboard repeat delay ms", "keyboard"},
    {31, "System language", "Keyboard & Language", "keyboard language locale system login", "language"},

    // ── Bluetooth (47) ──
    {47, "Bluetooth", "Bluetooth", "bluetooth power adapter on off", "bluetooth"},
    {47, "Auto-reconnect", "Bluetooth", "bluetooth auto reconnect", "autorenew"},
    {47, "Scan", "Bluetooth", "bluetooth scan discovery nearby", "search"},
    {47, "Connect", "Bluetooth", "bluetooth connect device", "link"},
    {47, "Disconnect", "Bluetooth", "bluetooth disconnect device", "link_off"},
    {47, "Forget", "Bluetooth", "bluetooth forget remove device", "delete"},

    // ── Power (32) ──
    {32, "Sleep after", "Power", "power display sleep timeout", "bedtime"},
    {32, "Suspend after", "Power", "power idle suspend timeout", "power_settings_new"},
    {32, "Power Button", "Power", "power button action suspend shutdown hibernate", "power_settings_new"},
    {32, "Lid Close", "Power", "power lid close suspend hibernate", "laptop"},
    {32, "Show battery percentage", "Power", "power battery percentage", "battery_full"},
    {32, "Scaling Governor", "Power", "power cpu governor performance powersave", "speed"},
    {32, "Energy Performance Preference", "Power", "power epp energy performance", "eco"},
    {32, "Profile", "Power", "power tuned profile system", "tune"},

    // ── Accounts (46) ──
    {46, "Full name", "Accounts", "accounts full name profile", "person"},
    {46, "Account Locked", "Accounts", "accounts locked disable login", "lock"},
    {46, "Log In Automatically", "Accounts", "accounts autologin automatic login", "login"},
    {46, "Set Password", "Accounts", "accounts password set change", "key"},
    {46, "Create User", "Accounts", "accounts create new user add", "person_add"},
    {46, "Delete User", "Accounts", "accounts delete remove user", "person_remove"},
    {46, "Hostname", "Accounts", "accounts hostname computer name", "computer"},

    // ── Startup (48) ──
    {48, "Add Entry", "Startup", "startup autostart add entry name command", "add"},
    {48, "Run in terminal", "Startup", "startup terminal command", "terminal"},
    {48, "Start delay", "Startup", "startup delay seconds", "timer"},
    {48, "Enable", "Startup", "startup enable disable entry", "check"},
    {48, "Reset to System", "Startup", "startup reset system default", "restart_alt"},

    // ── Wallpaper (6) ──
    {6, "Columns", "Wallpaper", "wallpaper grid columns gallery", "grid_view"},
    {6, "Rows", "Wallpaper", "wallpaper grid rows gallery", "grid_view"},
    {6, "Thumbnail scale", "Wallpaper", "wallpaper thumbnail scale zoom", "zoom_in"},
    {6, "Corner radius", "Wallpaper", "wallpaper corner radius", "rounded_corners"},
    {6, "Window opacity", "Wallpaper", "wallpaper window opacity", "opacity"},
    {6, "Fill mode", "Wallpaper", "wallpaper mode fill fit stretch center tile", "wallpaper"},

    // ── Bing (16) ──
    {16, "Save location", "Bing Wallpaper", "bing save location folder browse path", "folder"},
    {16, "Fetch Now", "Bing Wallpaper", "bing daily fetch now download", "download"},
    {16, "Check Now", "Bing Wallpaper", "bing updates check now", "refresh"},
    {16, "Download All", "Bing Wallpaper", "bing download all archive", "download"},
    {16, "Blocked Keywords", "Bing Wallpaper", "bing blocked keywords filter", "block"},

    // ── Network ──
    {17, "Wi-Fi", "Wi-Fi", "wifi enable scan connect disconnect", "wifi"},
    {17, "Available Networks", "Wi-Fi", "wifi networks available scan list", "list"},
    {18, "Wired Connection", "Wired", "wired connection status speed duplex mac", "lan"},
    {18, "IPv4 Settings", "Wired", "wired ipv4 dns dhcp hostname address", "computer"},
    {18, "IPv6 Settings", "Wired", "wired ipv6 dns address", "computer"},
    {18, "Auto-connect", "Wired", "wired autoconnect priority profile", "autorenew"},
    {28, "VPN Connections", "VPN", "vpn connections connect disconnect add", "vpn_key"},
};

static constexpr int kSettingsSearchEntryCount =
    static_cast<int>(sizeof(kSettingsSearchEntries) / sizeof(kSettingsSearchEntries[0]));

static inline std::string settings_search_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static inline bool settings_search_entry_visible(const SettingsSearchEntry& e, CompositorKind kind) {
  if (e.tab >= 20 && e.tab <= 26) return kind == CompositorKind::Mango;
  if (e.tab == 33) return kind == CompositorKind::Hyprland;
  return true;
}

// AND-token substring match against title + section + keywords.
static inline bool settings_search_entry_matches(const SettingsSearchEntry& e, const std::string& qlower) {
  if (qlower.empty()) return false;
  std::string hay = e.title;
  hay += ' ';
  hay += e.section;
  hay += ' ';
  hay += e.keywords;
  hay = settings_search_lower(std::move(hay));
  size_t start = 0;
  while (start < qlower.size()) {
    while (start < qlower.size() && std::isspace(static_cast<unsigned char>(qlower[start]))) ++start;
    if (start >= qlower.size()) break;
    size_t end = start;
    while (end < qlower.size() && !std::isspace(static_cast<unsigned char>(qlower[end]))) ++end;
    const std::string tok = qlower.substr(start, end - start);
    if (!tok.empty() && hay.find(tok) == std::string::npos) return false;
    start = end;
  }
  return true;
}

static inline std::vector<int> settings_search_collect(const std::string& query, CompositorKind kind) {
  std::vector<int> out;
  const std::string q = settings_search_lower(query);
  // Trim; empty query means "no search".
  size_t a = 0;
  while (a < q.size() && std::isspace(static_cast<unsigned char>(q[a]))) ++a;
  size_t b = q.size();
  while (b > a && std::isspace(static_cast<unsigned char>(q[b - 1]))) --b;
  if (b <= a) return out;
  const std::string qt = q.substr(a, b - a);
  out.reserve(32);
  for (int i = 0; i < kSettingsSearchEntryCount; ++i) {
    const auto& e = kSettingsSearchEntries[i];
    if (!settings_search_entry_visible(e, kind)) continue;
    if (settings_search_entry_matches(e, qt)) out.push_back(i);
    if (out.size() >= 120) break;
  }
  return out;
}
