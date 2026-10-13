#pragma once

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct DockApp;

namespace eh::config {
struct ShellConfig;
}

namespace eh::widgets {

// Window-layout switcher slot. Shows the active layout glyph as an icon-only
// pill; clicking opens the layout popup which swaps the layout live.
//
// Backends (picked via detect_compositor_kind()):
// - Hyprland: dwindle / master / scrolling / monocle. Applied live with
//   `keyword general:layout` and persisted to the managed hyprland config.
// - MangoWM (mangowm/mango wiki "layouts"): tile / scroller / monocle /
//   grid / deck / center_tile / vertical_tile / right_tile /
//   vertical_scroller / vertical_grid / vertical_deck / dwindle / fair /
//   vertical_fair. Applied live with Mango IPC `dispatch setlayout,<name>`
//   (layouts are per-tag runtime state, so nothing is persisted).
// - Anything else: Hyprland list/behavior as the fallback.

enum class LayoutBackend : uint8_t { Hyprland, Mango };

[[nodiscard]] LayoutBackend layout_backend();
// Number of layouts offered for the active backend (4 / 14).
[[nodiscard]] int layout_count();
[[nodiscard]] const char* layout_id_at(int idx);
// Index into the backend list, or -1 when unknown to this backend.
[[nodiscard]] int layout_index_for(std::string_view name);
[[nodiscard]] const char* layout_glyph_for(std::string_view name);
[[nodiscard]] const char* layout_title_for(std::string_view name);
[[nodiscard]] const char* layout_desc_for(std::string_view name);
[[nodiscard]] std::string layout_normalize(std::string_view name);

// Live active layout (lowercase id). Never empty: Hyprland falls back to the
// persisted config value then "dwindle"; Mango falls back to the last applied
// value then "tile" (its default).
[[nodiscard]] std::string layout_current();
// Swap live (+ persist on Hyprland). Returns true when accepted.
bool layout_apply(const std::string& name);

[[nodiscard]] bool widget_list_contains_layout(const eh::config::ShellConfig& sc,
                                               const std::vector<std::string>& widgets);

[[nodiscard]] double dock_layout_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                            std::string_view instance_id, double icon_ref_px,
                                            double bar_height);

void paint_layout_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                       double x, double y, double slot_w, double slot_h, double icon_ref_px,
                       bool hovered, bool pressed);

}  // namespace eh::widgets
