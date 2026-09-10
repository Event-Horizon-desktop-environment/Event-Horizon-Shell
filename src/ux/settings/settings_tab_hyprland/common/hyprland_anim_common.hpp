#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"

// Animation entry metadata.

struct AnimEntryMeta {
  const char* name;
  const char* label;
  const char* styles;
};

static constexpr int kAnimRowH = 48;

static constexpr AnimEntryMeta kAnimEntries[] = {
  {"global",             "Global",              ""},
  {"windows",            "Windows",             ""},
  {"windowsIn",          "Open (Window)",       "slide, slidevert"},
  {"windowsOut",         "Close (Window)",      "slide, slidevert"},
  {"windowsMove",        "Move",                "slide, slidevert"},
  {"layers",             "Layers",              ""},
  {"layersIn",           "Open (Layer)",        "slide, slidevert, fade"},
  {"layersOut",          "Close (Layer)",       "slide, slidevert, fade"},
  {"fade",               "Fade",                ""},
  {"fadeIn",             "Fade In",             ""},
  {"fadeOut",            "Fade Out",            ""},
  {"fadeSwitch",         "Fade Switch",         ""},
  {"fadeShadow",         "Fade Shadow",         ""},
  {"fadeDim",            "Fade Dim",            ""},
  {"fadeLayers",         "Fade Layers",         ""},
  {"fadeLayersIn",       "Layers In",           ""},
  {"fadeLayersOut",      "Layers Out",          ""},
  {"fadePopups",         "Fade Popups",         ""},
  {"fadePopupsIn",       "Popups In",           "popin"},
  {"fadePopupsOut",      "Popups Out",          "popout"},
  {"fadeDpms",           "DPMS",                ""},
  {"workspaces",         "Workspaces",          ""},
  {"workspacesIn",       "Switch In",           "slide, slidevert, fade"},
  {"workspacesOut",      "Switch Out",          "slide, slidevert, fade"},
  {"specialWorkspace",   "Special Workspace",   ""},
  {"specialWorkspaceIn", "Special In",          "slide, slidevert, fade"},
  {"specialWorkspaceOut","Special Out",         "slide, slidevert, fade"},
  {"border",             "Border",              ""},
  {"borderangle",        "Border Angle",        ""},
  {"zoomFactor",         "Zoom Factor",         ""},
  {"monitorAdded",       "Monitor Added",       ""},
};

static constexpr int kAnimEntryCount = sizeof(kAnimEntries) / sizeof(kAnimEntries[0]);

struct AnimCategoryInfo {
  const char* label;
  int entry_start;
  int entry_count;
};

static constexpr AnimCategoryInfo kAnimCategories[] = {
  {"Global",              0,  1},
  {"Windows && Layers",   1,  7},
  {"Fading",              8,  13},
  {"Workspaces",          21, 6},
  {"Other",               27, 4},
};
static constexpr int kAnimCategoryCount = 5;

// Curve names.

static constexpr const char* kCurveNames[] = {
  "default", "linear", "overshot", "smooth",
  "easeInOut", "easeIn", "easeOut",
  "easeOutCirc", "easeInCirc", "easeOutExpo", "easeInExpo",
  "easeOutBack", "easeInBack",
};
static constexpr int kCurveCount = sizeof(kCurveNames) / sizeof(kCurveNames[0]);

// Bezier presets.

struct BezierPreset { const char* name; double p1x, p1y, p2x, p2y; };
static constexpr BezierPreset kBezierPresets[] = {
  {"default",      0.25, 0.1,  0.25, 1.0},
  {"linear",       0.0,  0.0,  1.0,  1.0},
  {"overshot",     0.05, 0.9,  0.1,  1.1},
  {"smooth",       0.25, 0.1,  0.25, 1.0},
  {"easeInOut",    0.42, 0.0,  0.58, 1.0},
  {"easeIn",       0.42, 0.0,  1.0,  1.0},
  {"easeOut",      0.0,  0.0,  0.58, 1.0},
  {"easeOutCirc",  0.0,  0.55, 0.45, 1.0},
  {"easeInCirc",   0.55, 0.0,  1.0,  0.45},
  {"easeOutExpo",  0.16, 1.0,  0.3,  1.0},
  {"easeInExpo",   0.7,  0.0,  0.84, 0.0},
  {"easeOutBack",  0.34, 1.56, 0.64, 1.0},
  {"easeInBack",   0.36, 0.0,  0.66, -0.56},
};
static constexpr int kBezierPresetCount = sizeof(kBezierPresets) / sizeof(kBezierPresets[0]);

// Helpers.

inline bool get_bezier_for_curve(const std::string& name, double& p1x, double& p1y, double& p2x, double& p2y) {
  for (int i = 0; i < kBezierPresetCount; ++i) {
    if (name == kBezierPresets[i].name) {
      p1x = kBezierPresets[i].p1x; p1y = kBezierPresets[i].p1y;
      p2x = kBezierPresets[i].p2x; p2y = kBezierPresets[i].p2y;
      return true;
    }
  }
  return false;
}

inline eh::settings_hyprland::HyprlandAnimationEntry& find_or_create_entry(App& app, const std::string& name) {
  for (auto& e : app.hyprlandConfig.animations.entries)
    if (e.name == name) return e;
  eh::settings_hyprland::HyprlandAnimationEntry e;
  e.name = name;
  app.hyprlandConfig.animations.entries.push_back(std::move(e));
  return app.hyprlandConfig.animations.entries.back();
}

inline std::vector<std::string> split_styles(const char* styles) {
  std::vector<std::string> result;
  if (!styles || !styles[0]) return result;
  std::string cur;
  for (const char* p = styles; *p; ++p) {
    if (*p == ',') { result.push_back(cur); cur.clear(); }
    else if (*p != ' ') cur += *p;
  }
  if (!cur.empty()) result.push_back(cur);
  return result;
}

inline const char* get_anim_styles(const char* name) {
  for (int i = 0; i < kAnimEntryCount; ++i)
    if (kAnimEntries[i].name && std::strcmp(kAnimEntries[i].name, name) == 0)
      return kAnimEntries[i].styles ? kAnimEntries[i].styles : "";
  return "";
}
