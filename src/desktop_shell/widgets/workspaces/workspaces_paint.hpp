#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_model.hpp"

#include <cairo.h>
#include <cstdint>
#include <ctime>
#include <string_view>
#include <vector>

namespace eh::widgets {

struct CellGeom {
  double x = 0;
  double w = 0;
};

struct WsStripAnimState {
  std::vector<CellGeom> fromCells;
  std::vector<CellGeom> toCells;
  uint64_t startMs = 0;
  uint64_t durationMs = 200;
  bool active = false;

  void updateTargets(const std::vector<CellGeom>& newCells, uint64_t nowMs);
  [[nodiscard]] double getX(size_t i, uint64_t nowMs) const;
  [[nodiscard]] double getW(size_t i, uint64_t nowMs) const;
  [[nodiscard]] float progress(uint64_t nowMs) const;
};

[[nodiscard]] bool widget_list_contains_workspaces(const eh::config::ShellConfig& sc,
                                                   const std::vector<std::string>& widgets);

void workspace_strip_poll(std::vector<WorkspaceEntry>& out, timespec& last_poll_mono, CompositorKind kind);

[[nodiscard]] int workspace_max_slots_from_settings(const eh::config::ShellConfig& sc, std::string_view instance_id);
void workspace_strip_apply_max_slots(std::vector<WorkspaceEntry>& out, int max_slots);

[[nodiscard]] double dock_workspaces_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                                std::string_view instance_id, double icon_ref_px, double bar_height,
                                                const std::vector<WorkspaceEntry>& entries);

[[nodiscard]] bool paint_workspaces_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                                         double x, double y, double slot_w, double slot_h, double icon_ref_px,
                                         const std::vector<WorkspaceEntry>& entries, bool hovered, bool pressed,
                                         eh::icons::IconCache* icon_cache = nullptr,
                                         WsStripAnimState* anim = nullptr);

[[nodiscard]] int workspaces_pick_index(double local_x, double slot_w, const eh::config::ShellConfig& sc,
                                        std::string_view instance_id, double icon_ref_px,
                                        const std::vector<WorkspaceEntry>& entries);

void workspace_activate_entry(const WorkspaceEntry& e, CompositorKind kind);

}
