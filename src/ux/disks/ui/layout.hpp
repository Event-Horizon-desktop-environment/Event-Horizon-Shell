#pragma once
// Single source of truth for hit-testing + painting geometry.
// draw.cpp paints from Layout; events.cpp hit-tests against the same Layout.
// This fixes the old duplication bug where content_y was computed differently.

#include <cstdint>
#include <memory>
#include <vector>

namespace eh::disks {
class Drive;
class Block;
struct AppState;
}  // namespace eh::disks

namespace eh::disks::layout {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

struct VolSeg {
  double sx = 0;  // offset inside bar
  double sw = 0;
  bool is_free = false;
  bool is_extended = false;
  std::shared_ptr<Block> block;  // null for free
  int color_idx = 0;
  Rect rect;  // absolute
};

struct BlockRow {
  std::shared_ptr<Block> block;
  int index = -1;  // index into filtered blocks
  Rect rect;       // full row
  Rect mount_btn;  // may be empty (w==0)
  Rect lock_btn;   // LUKS lock/unlock quick button (may be empty)
  Rect gear_btn;
};

struct Layout {
  int W = 0, H = 0;
  // Header
  Rect header;
  Rect btn_menu;     // hamburger (global)
  Rect search_field;
  Rect btn_attach;
  Rect btn_benchmark;
  Rect btn_headermenu;  // ⋮
  Rect job_pill;        // spinner pill when jobs active
  // Sidebar
  Rect sidebar;
  Rect sidebar_list;  // clipped scroll region
  std::vector<Rect> drive_rows;
  std::vector<Rect> drive_row_menu_btn;
  // Content
  Rect content;  // scroll viewport
  Rect drive_card;
  Rect drive_icon;
  Rect btn_power, btn_eject, btn_settings, btn_drive_menu;
  Rect smart_banner;
  // Volumes
  Rect vol_section;
  Rect vol_bar;
  Rect btn_add_part;
  std::vector<VolSeg> vol_segs;
  // Table
  Rect table_head;
  Rect table_list;  // clipped scroll region
  std::vector<BlockRow> rows;
  int content_total_h = 0;
  // Details card (selected block)
  Rect details_card;
  Rect btn_mount_opts, btn_edit_part, btn_resize, btn_check, btn_ownership;
  // Footer
  Rect status_bar;
  Rect job_bar;  // above status when jobs active
  // Menus (absolute, when open)
  Rect menu_rect;
};

Rect dialog_rect(const AppState& app, int dw, int dh);

// Compute layout for current state. Must be called at the start of draw
// and at the start of every input handler (cheap: O(drives + blocks)).
Layout compute(const AppState& app, const std::vector<std::shared_ptr<Drive>>& drives,
               const std::vector<std::shared_ptr<Block>>& blocks_filtered,
               uint64_t drive_size);

}  // namespace eh::disks::layout
