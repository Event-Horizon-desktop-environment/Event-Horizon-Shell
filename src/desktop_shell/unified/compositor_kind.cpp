#include "desktop_shell/unified/compositor_kind.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace {

std::string build_env_hint() {
   
  constexpr const char* vars[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION"};
  std::string hint;
  for (const char* var : vars) {
    const char* value = std::getenv(var);
    if (value == nullptr || value[0] == '\0') continue;
    if (!hint.empty()) hint += ':';
    hint += value;
  }
  return hint;
}

bool ci_contains(const std::string& haystack, const std::string& needle) {
   
  auto it = std::search(
      haystack.begin(), haystack.end(),
      needle.begin(), needle.end(),
      [](unsigned char ch1, unsigned char ch2) { return std::tolower(ch1) == std::tolower(ch2); });
  return it != haystack.end();
}

CompositorKind detect_impl() {
   
  if (const char* v = std::getenv("HYPRLAND_INSTANCE_SIGNATURE"); v != nullptr && v[0] != '\0')
    return CompositorKind::Hyprland;
  if (const char* v = std::getenv("NIRI_SOCKET"); v != nullptr && v[0] != '\0')
    return CompositorKind::Niri;
  if (const char* v = std::getenv("SWAYSOCK"); v != nullptr && v[0] != '\0')
    return CompositorKind::Sway;
  if (const char* v = std::getenv("TRIAD_SOCKET"); v != nullptr && v[0] != '\0')
    return CompositorKind::Triad;
  if (const char* v = std::getenv("LABWC_PID"); v != nullptr && v[0] != '\0')
    return CompositorKind::Labwc;

  if (const char* v = std::getenv("MANGO_INSTANCE_SIGNATURE"); v != nullptr && v[0] != '\0')
    return CompositorKind::Mango;

  const std::string hint = build_env_hint();
  if (hint.empty()) return CompositorKind::Unknown;

  if (ci_contains(hint, "niri")) return CompositorKind::Niri;
  if (ci_contains(hint, "hypr")) return CompositorKind::Hyprland;
  if (ci_contains(hint, "sway")) return CompositorKind::Sway;
  if (ci_contains(hint, "mango") || ci_contains(hint, "dwl")) return CompositorKind::Mango;
  if (ci_contains(hint, "triad")) return CompositorKind::Triad;
  if (ci_contains(hint, "labwc")) return CompositorKind::Labwc;

  return CompositorKind::Unknown;
}

}

CompositorKind detect_compositor_kind() {
   
  static const CompositorKind cached = detect_impl();
  return cached;
}

const char* compositor_kind_cstr(CompositorKind k) {
   
  switch (k) {
    case CompositorKind::Niri:    return "Niri";
    case CompositorKind::Hyprland: return "Hyprland";
    case CompositorKind::Sway:    return "Sway";
    case CompositorKind::Mango:   return "Mango";
    case CompositorKind::Triad:   return "Triad";
    case CompositorKind::Labwc:   return "Labwc";
    case CompositorKind::Unknown: return "Unknown";
  }
  return "Unknown";
}

std::string_view compositor_env_hint() {
   
  static const std::string cached = build_env_hint();
  return cached;
}
