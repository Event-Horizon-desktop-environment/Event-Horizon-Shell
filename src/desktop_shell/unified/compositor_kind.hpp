#pragma once

#include <cstdint>
#include <string_view>

enum class CompositorKind : std::uint8_t {
  Unknown = 0,
  Niri,
  Hyprland,
  Sway,
  Mango,
  Labwc,
  Triad,
};

[[nodiscard]] CompositorKind detect_compositor_kind();
[[nodiscard]] const char* compositor_kind_cstr(CompositorKind k);
[[nodiscard]] std::string_view compositor_env_hint();
[[nodiscard]] inline bool is_niri() { return detect_compositor_kind() == CompositorKind::Niri; }
[[nodiscard]] inline bool is_hyprland() { return detect_compositor_kind() == CompositorKind::Hyprland; }
[[nodiscard]] inline bool is_sway() { return detect_compositor_kind() == CompositorKind::Sway; }
[[nodiscard]] inline bool is_mango() { return detect_compositor_kind() == CompositorKind::Mango; }
[[nodiscard]] inline bool is_labwc() { return detect_compositor_kind() == CompositorKind::Labwc; }
[[nodiscard]] inline bool is_triad() { return detect_compositor_kind() == CompositorKind::Triad; }
