#include "ux/disks/ui/layout.hpp"

#include <algorithm>

#include "ux/disks/app_types.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/block.hpp"
#include "ux/disks/ui/theme.hpp"

namespace eh::disks::layout {

Rect dialog_rect(const AppState& app, int dw, int dh) {
  Rect r;
  r.w = dw;
  r.h = dh;
  r.x = (app.width - dw) / 2;
  r.y = (app.height - dh) / 2;
  return r;
}

Layout compute(const AppState& app,
               const std::vector<std::shared_ptr<Drive>>& drives,
               const std::vector<std::shared_ptr<Block>>& blocks_filtered,
               uint64_t drive_size) {
  using namespace theme;
  Layout L;
  L.W = app.width;
  L.H = app.height;
  int W = L.W, H = L.H;
  if (W <= 0 || H <= 0) return L;

  // Header
  L.header = {0, 0, W, kHeaderH};
  L.btn_menu = {8, (kHeaderH - kHeaderIconBtn) / 2, kHeaderIconBtn, kHeaderIconBtn};
  // Search field centered-left
  int sx = 8 + kHeaderIconBtn + 8 + 180;  // after title
  L.search_field = {sx, (kHeaderH - kHeaderBtnH) / 2, 260, kHeaderBtnH};
  // Right-aligned buttons
  int bx = W - 8;
  L.btn_headermenu = {bx - 34, (kHeaderH - 34) / 2, 34, 34};
  bx -= 34 + 8;
  L.btn_benchmark = {bx - 104, (kHeaderH - kHeaderBtnH) / 2, 104, kHeaderBtnH};
  bx -= 104 + 8;
  L.btn_attach = {bx - 96, (kHeaderH - kHeaderBtnH) / 2, 96, kHeaderBtnH};
  if (JobTracker::instance().has_active()) {
    L.job_pill = {L.btn_attach.x - 8 - 150, (kHeaderH - 28) / 2, 150, 28};
  }

  // Sidebar
  int sb_h = H - kHeaderH - kStatusH;
  L.sidebar = {0, kHeaderH, kSidebarW, sb_h};
  L.sidebar_list = {0, kHeaderH + kSidebarSectionH + 34, kSidebarW,
                    sb_h - kSidebarSectionH - 34};
  L.drive_rows.reserve(drives.size());
  L.drive_row_menu_btn.reserve(drives.size());
  for (size_t i = 0; i < drives.size(); ++i) {
    int iy = kHeaderH + kSidebarSectionH + 34 + static_cast<int>(i) * kSidebarRowH -
             app.sidebarScroll;
    L.drive_rows.push_back({kSidebarPad, iy, kSidebarW - kSidebarPad * 2, kSidebarRowH - 4});
    L.drive_row_menu_btn.push_back(
        {kSidebarW - 8 - 28, iy + (kSidebarRowH - 4 - 28) / 2, 28, 28});
  }

  // Content viewport
  int cx = kSidebarW;
  int cw = W - kSidebarW;
  int cy = kHeaderH;
  int ch = H - kHeaderH - kStatusH - (JobTracker::instance().has_active() ? kJobBarH : 0);
  L.content = {cx, cy, cw, ch};

  int px = cx + kContentPad;
  int pw = cw - kContentPad * 2;
  int y = cy + kContentPad - app.contentScroll;

  // Drive card
  L.drive_card = {px, y, pw, kDriveHeadH + 44};
  L.drive_icon = {px + kCardPad, y + kCardPad, kDriveIconPx, kDriveIconPx};
  // Drive action buttons top-right of card
  int abx = px + pw - kCardPad;
  auto place_icon = [&](int s) {
    abx -= s;
    Rect r{abx, y + kCardPad, s, s};
    abx -= 4;
    return r;
  };
  L.btn_drive_menu = place_icon(32);
  L.btn_settings = place_icon(32);
  L.btn_eject = place_icon(32);
  L.btn_power = place_icon(32);
  L.smart_banner = {px + kCardPad, y + kDriveHeadH + 8, pw - kCardPad * 2, 28};

  y += kDriveHeadH + 44 + 12;

  // Volumes section
  int vol_sec_h = 26 + kVolBarH + kVolLegendH + 8;
  L.vol_section = {px, y, pw, vol_sec_h};
  L.vol_bar = {px + kCardPad, y + 26, pw - kCardPad * 2, kVolBarH};
  L.btn_add_part = {px + pw - kCardPad - 120, y - 2, 120, 28};

  // Build proportional segments (Disks 51: strictly proportional, free hatched)
  L.vol_segs.clear();
  if (!blocks_filtered.empty() && L.vol_bar.w > 10) {
    struct Seg {
      uint64_t start, end;
      std::shared_ptr<Block> b;
    };
    std::vector<Seg> segs;
    // Sort by offset
    auto sorted = blocks_filtered;
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
      return a->get_partition_offset() < b->get_partition_offset();
    });
    uint64_t total = drive_size;
    if (total == 0) {
      for (auto& b : sorted) {
        uint64_t e = b->get_partition_offset() + b->get_size();
        if (e > total) total = e;
      }
    }
    if (total == 0) total = 1;
    uint64_t pos = 0;
    for (auto& b : sorted) {
      uint64_t off = b->has_partition() ? b->get_partition_offset() : pos;
      if (off > pos) segs.push_back({pos, off, nullptr});
      uint64_t e = off + b->get_size();
      segs.push_back({std::min(off, e), e, b});
      pos = std::max(pos, e);
    }
    if (pos < total) segs.push_back({pos, total, nullptr});
    double bx0 = L.vol_bar.x, bw = L.vol_bar.w;
    int ci = 0;
    for (auto& s : segs) {
      double f0 = double(s.start) / double(total);
      double f1 = double(s.end) / double(total);
      double x0 = bx0 + f0 * bw, x1 = bx0 + f1 * bw;
      double sw = x1 - x0;
      if (sw < 1.0 && s.b) sw = 1.0;  // keep 1px visibility, no boosting (pixel truth)
      if (x0 >= bx0 + bw) break;
      if (x0 + sw > bx0 + bw) sw = bx0 + bw - x0;
      if (sw <= 0) continue;
      VolSeg v;
      v.sx = x0 - bx0;
      v.sw = sw;
      v.is_free = (s.b == nullptr);
      v.block = s.b;
      v.color_idx = s.b ? ci++ : -1;
      v.rect = {int(std::lround(x0)), L.vol_bar.y, int(std::lround(sw)),
                L.vol_bar.h};
      // Account gap: shrink by gap/2 each side for painting (hit stays full)
      L.vol_segs.push_back(v);
    }
    // Merge: drop leading tiny free sliver (<3px) into first partition for cleanliness
    if (L.vol_segs.size() > 1 && L.vol_segs[0].is_free && L.vol_segs[0].sw < 3)
      L.vol_segs.erase(L.vol_segs.begin());
  }

  y += vol_sec_h + 12;

  // Table head + rows
  L.table_head = {px, y, pw, kTableHeadH};
  y += kTableHeadH;
  L.table_list = {px, y, pw, 0};  // height filled below
  L.rows.reserve(blocks_filtered.size());
  for (size_t i = 0; i < blocks_filtered.size(); ++i) {
    Rect row{px, y + int(i) * kBlockRowH, pw, kBlockRowH};
    BlockRow br;
    br.block = blocks_filtered[i];
    br.index = int(i);
    br.rect = row;
    // Right-aligned action cluster
    int rx = row.x + row.w - kCardPad;
    br.gear_btn = {rx - 32, row.y + (row.h - 32) / 2, 32, 32};
    rx -= 32 + 4;
    // lock quick button only for LUKS
    if (br.block && br.block->is_encrypted()) {
      br.lock_btn = {rx - 32, row.y + (row.h - 32) / 2, 32, 32};
      rx -= 32 + 4;
    }
    // mount/unmount only when relevant
    auto f = br.block ? br.block->get_features() : 0;
    if (f & (FEATURE_CAN_MOUNT | FEATURE_CAN_UNMOUNT | FEATURE_CAN_SWAPON |
             FEATURE_CAN_SWAPOFF | FEATURE_CAN_UNLOCK | FEATURE_CAN_LOCK)) {
      br.mount_btn = {rx - 84, row.y + (row.h - 30) / 2, 84, 30};
    }
    L.rows.push_back(br);
  }
  int rows_h = int(blocks_filtered.size()) * kBlockRowH;
  int details_h = 0;
  if (app.selected_block >= 0 &&
      app.selected_block < int(blocks_filtered.size())) {
    details_h = 12 + 190;  // details card + gap
  }
  L.content_total_h = (kDriveHeadH + 44 + 12) + vol_sec_h + 12 + kTableHeadH +
                      rows_h + details_h + kContentPad;
  // Table list viewport height = remaining
  int table_view_h = ch - (kContentPad + kDriveHeadH + 44 + 12 + vol_sec_h + 12 +
                           kTableHeadH) -
                     details_h - kContentPad;
  if (table_view_h < 80) table_view_h = 80;
  L.table_list.h = table_view_h + rows_h;  // logical; clip applied in draw

  if (details_h > 0) {
    int dy = L.table_head.y + kTableHeadH + rows_h + 12;
    L.details_card = {px, dy, pw, 190};
    // Detail action buttons bottom of card
    int dbw = 130, dbh = 30, dby = dy + 190 - kCardPad - dbh;
    int dbx = px + pw - kCardPad;
    auto plc = [&](int w) {
      dbx -= w;
      Rect r{dbx, dby, w, dbh};
      dbx -= 8;
      return r;
    };
    L.btn_ownership = plc(dbw);
    L.btn_check = plc(110);
    L.btn_resize = plc(110);
    L.btn_edit_part = plc(130);
    L.btn_mount_opts = plc(140);
  }

  L.status_bar = {0, H - kStatusH, W, kStatusH};
  if (JobTracker::instance().has_active())
    L.job_bar = {0, H - kStatusH - kJobBarH, W, kJobBarH};

  // Active menu rect (computed by views when open; placeholder here)
  if (app.menu.open) {
    int mw = 240, mh = int(app.menu.items.size()) * kMenuItemH + kMenuPad * 2;
    int mx = std::min(app.menu.x, W - mw - 8);
    int my = std::min(app.menu.y, H - mh - kStatusH - 8);
    if (mx < 8) mx = 8;
    if (my < kHeaderH + 4) my = kHeaderH + 4;
    L.menu_rect = {mx, my, mw, mh};
  }

  return L;
}

}  // namespace eh::disks::layout
