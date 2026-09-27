// Compositor detection — original implementation for Event Horizon.
//
// Detection order, match substrings, and fallback behavior below define this
// file's contract; results are cached after the first call. Implemented
// around lookup tables so adding a compositor is one table row.

#include "desktop_shell/unified/compositor_kind.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

// Ordered socket/marker variables: first non-empty hit wins. Order matches
// the historical precedence (Hyprland > Niri > Sway > Labwc > Mango).
struct EnvProbe {
  const char* var;
  CompositorKind kind;
};

constexpr std::array<EnvProbe, 5> kEnvProbes{{
    {"HYPRLAND_INSTANCE_SIGNATURE", CompositorKind::Hyprland},
    {"NIRI_SOCKET", CompositorKind::Niri},
    {"SWAYSOCK", CompositorKind::Sway},
    {"LABWC_PID", CompositorKind::Labwc},
    {"MANGO_INSTANCE_SIGNATURE", CompositorKind::Mango},
}};

// Desktop-hint substrings for compositors that expose no socket variable.
// Each entry is tried in order against the combined XDG hint string.
struct HintProbe {
  std::string_view needle;
  CompositorKind kind;
};

constexpr std::array<HintProbe, 5> kHintProbes{{
    {"niri", CompositorKind::Niri},
    {"hypr", CompositorKind::Hyprland},
    {"sway", CompositorKind::Sway},
    {"mango", CompositorKind::Mango},
    {"labwc", CompositorKind::Labwc},
}};

// "dwl" is an alias hint historically mapped onto Mango's tag-based backend.
constexpr std::string_view kDwlAlias = "dwl";

[[nodiscard]] bool env_set(const char* var) noexcept {
  const char* v = std::getenv(var);
  return v != nullptr && v[0] != '\0';
}

[[nodiscard]] std::string desktop_hint() {
  constexpr const char* vars[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION"};
  std::string out;
  for (const char* var : vars) {
    const char* v = std::getenv(var);
    if (v == nullptr || v[0] == '\0') continue;
    if (!out.empty()) out += ':';
    out += v;
  }
  return out;
}

[[nodiscard]] bool contains_folded(std::string_view haystack, std::string_view needle) noexcept {
  if (needle.empty() || needle.size() > haystack.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool hit = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      const auto a = static_cast<unsigned char>(haystack[i + j]);
      const auto b = static_cast<unsigned char>(needle[j]);
      if (std::tolower(a) != std::tolower(b)) {
        hit = false;
        break;
      }
    }
    if (hit) return true;
  }
  return false;
}

CompositorKind detect_impl() {
  for (const EnvProbe& p : kEnvProbes) {
    if (env_set(p.var)) return p.kind;
  }
  const std::string hint = desktop_hint();
  if (hint.empty()) return CompositorKind::Unknown;
  for (const HintProbe& p : kHintProbes) {
    if (contains_folded(hint, p.needle)) return p.kind;
  }
  if (contains_folded(hint, kDwlAlias)) return CompositorKind::Mango;
  return CompositorKind::Unknown;
}

struct KindName {
  CompositorKind kind;
  const char* name;
};

constexpr std::array<KindName, 6> kKindNames{{
    {CompositorKind::Unknown, "Unknown"},
    {CompositorKind::Niri, "Niri"},
    {CompositorKind::Hyprland, "Hyprland"},
    {CompositorKind::Sway, "Sway"},
    {CompositorKind::Mango, "Mango"},
    {CompositorKind::Labwc, "Labwc"},
}};

}  // namespace

CompositorKind detect_compositor_kind() {
  static const CompositorKind cached = detect_impl();
  return cached;
}

const char* compositor_kind_cstr(CompositorKind k) {
  for (const KindName& e : kKindNames) {
    if (e.kind == k) return e.name;
  }
  return "Unknown";
}

std::string_view compositor_env_hint() {
  static const std::string cached = desktop_hint();
  return cached;
}
