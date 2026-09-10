#include "../app.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include "../manager.hpp"
#include "../drive.hpp"
#include "../block.hpp"

namespace eh::disks {

// Header.

void draw_header(AppState& app, cairo_t* cr, int w) {
  int y = 0;
  set_rgba(cr, app.surfaceR, app.surfaceG, app.surfaceB, 0.85);
  cairo_rectangle(cr, 0, y, w, kHeaderH);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, 0, kHeaderH - 0.5);
  cairo_line_to(cr, w, kHeaderH - 0.5);
  cairo_stroke(cr);

  // Hamburger menu
  {
    int bx = 12, by = (kHeaderH - 18) / 2;
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_line_width(cr, 2);
    for (int i = 0; i < 3; ++i) {
      cairo_move_to(cr, bx, by + 4 + i * 6);
      cairo_line_to(cr, bx + 16, by + 4 + i * 6);
    }
    cairo_stroke(cr);
  }

  // Title
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16);
  set_rgb(cr, app.textR, app.textG, app.textB);
  cairo_move_to(cr, 44, (kHeaderH + 6) / 2);
  cairo_show_text(cr, "Disks");

  // Button positions
  int btn_x = w - 12;

  // ▸ Menu ⋮ button
  {
    int bw = 36;
    btn_x -= bw;
    bool hover = app.headerMenuHover;
    if (hover) {
      set_rgb(cr, 0.22, 0.22, 0.24);
      draw_rounded_rect(cr, btn_x, (kHeaderH - kBtnH) / 2, bw, kBtnH, 6);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 20);
    cairo_move_to(cr, btn_x + 10, (kHeaderH + 7) / 2);
    cairo_show_text(cr, "⋮");
    app.headerMenuX = btn_x;
  }

  // ▸ Create Disk Image button
  {
    int bw = 120;
    btn_x -= bw + 6;
    bool hover = app.headerCreateHover;
    if (hover) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.15);
      draw_rounded_rect(cr, btn_x, (kHeaderH - kBtnH) / 2, bw, kBtnH, 6);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 12);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_move_to(cr, btn_x + 10, (kHeaderH + 4) / 2);
    cairo_show_text(cr, "Create");
    app.headerCreateX = btn_x;
  }

  // ▸ Attach Disk Image button
  {
    int bw = 100;
    btn_x -= bw + 6;
    bool hover = app.headerAttachHover;
    if (hover) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.15);
      draw_rounded_rect(cr, btn_x, (kHeaderH - kBtnH) / 2, bw, kBtnH, 6);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, btn_x + 10, (kHeaderH + 4) / 2);
    cairo_show_text(cr, "Attach");
    app.headerAttachX = btn_x;
  }
}

// Sidebar.

void draw_sidebar(AppState& app, cairo_t* cr, int w, int view_h) {
  int y = kHeaderH;
  int sh = view_h - kHeaderH;

  set_rgba(cr, app.surfaceR, app.surfaceG, app.surfaceB, 0.85);
  cairo_rectangle(cr, 0, y, w, sh);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, w - 0.5, y);
  cairo_line_to(cr, w - 0.5, y + sh);
  cairo_stroke(cr);

  auto drives = Manager::instance().get_drives();
  int item_h = kSidebarRowH;
  int total_h = static_cast<int>(drives.size()) * item_h;

  cairo_save(cr);
  cairo_rectangle(cr, 0, y, w, sh);
  cairo_clip(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11);

  for (size_t i = 0; i < drives.size(); ++i) {
    int iy = y + static_cast<int>(i) * item_h - app.sidebarScroll;
    if (iy + item_h < y || iy > y + sh) continue;

    bool sel = static_cast<int>(i) == app.selected_drive;
    bool hover = static_cast<int>(i) == app.sidebarHover;

    // Row background
    if (sel || hover) {
      if (sel)
        set_rgba(cr, app.accentR, app.accentG, app.accentB, sel ? 0.25 : 0.12);
      else
        set_rgba(cr, 1, 1, 1, 0.06);
      draw_rounded_rect(cr, 4, iy + 2, w - 8, item_h - 4, 6);
      cairo_fill(cr);
    }

    auto& d = drives[i];
    std::string model = d->get_model();
    if (model.empty()) model = d->get_description();
    std::string sub = size_str(d->get_size()) + "  " + d->get_serial();

    // Drive icon — centred vertically in the taller row
    {
      const auto* icon = app.icons.tray_icon(drive_icon_name(d));
      if (icon && icon->surface) {
        draw_icon_surface(cr, icon, 12, iy + 20, 24);
      } else {
        bool is_removable = d->is_removable();
        set_rgb(cr, is_removable ? 0.85 : app.accentR,
                is_removable ? 0.85 : app.accentG,
                is_removable ? 0.40 : app.accentB);
        cairo_set_line_width(cr, 2);
        cairo_new_sub_path(cr);
        cairo_arc(cr, 24, iy + 32, 7, 0, 2 * M_PI);
        cairo_stroke(cr);
      }
    }

    // Model name
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 12);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_move_to(cr, 42, iy + 26);
    cairo_show_text(cr, model.c_str());

    // Size & serial
    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_set_font_size(cr, 10);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_move_to(cr, 42, iy + 50);
    cairo_show_text(cr, sub.c_str());

    // SMART warning indicator
    if (d->smart_supported() && d->smart_failing()) {
      set_rgb(cr, 0.95, 0.30, 0.30);
      cairo_arc(cr, w - 18, iy + 20, 5, 0, 2 * M_PI);
      cairo_fill(cr);
    }

    // Hamburger menu button on each drive row.
    {
      bool btn_hover = static_cast<int>(i) == app.sidebarDriveBtnHover;
      int bw = 20;
      int bx = w - 28;
      int by = iy + (item_h - bw) / 2;
      if (btn_hover) {
        set_rgba(cr, 1, 1, 1, 0.1);
        draw_rounded_rect(cr, bx, by, bw, bw, 4);
        cairo_fill(cr);
      }
      set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
      cairo_set_font_size(cr, 14);
      cairo_move_to(cr, bx + 4, by + 16);
      cairo_show_text(cr, "⋮");
    }

    // Row divider
    set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 8, iy + item_h - 0.5);
    cairo_line_to(cr, w - 8, iy + item_h - 0.5);
    cairo_stroke(cr);
  }

  cairo_restore(cr);

  // Scrollbar
  if (total_h > sh) {
    int scroll_h = std::max(sh * sh / total_h, 20);
    int scroll_y = y + (sh - scroll_h) * app.sidebarScroll / (total_h - sh);
    set_rgba(cr, 1, 1, 1, 0.3);
    draw_rounded_rect(cr, w - 6, scroll_y, 4, scroll_h, 2);
    cairo_fill(cr);
  }
}

// Partition bar.

void draw_partition_bar(AppState& app, cairo_t* cr,
                         const std::vector<std::shared_ptr<Block>>& blocks_orig,
                         int x, int y, int w,
                         uint64_t drive_size) {
  (void)app;
  if (blocks_orig.empty() || w < 10) return;

  // Sort by partition offset (copies are cheap, these are shared_ptr)
  auto blocks = blocks_orig;
  std::sort(blocks.begin(), blocks.end(), [](auto& a, auto& b) {
    return a->get_partition_offset() < b->get_partition_offset();
  });

  // Find the total span: from 0 to the end of the last partition (or drive size)
  uint64_t total_span = 0;
  for (auto& b : blocks) {
    uint64_t end = b->get_partition_offset() + b->get_size();
    if (end > total_span) total_span = end;
  }
  if (total_span == 0) return;
  if (drive_size > 0 && drive_size > total_span) total_span = drive_size;

  // Build segments: partition blocks interleaved with free-space gaps
  struct Seg { uint64_t start, end; std::shared_ptr<Block> block; };
  std::vector<Seg> segs;
  uint64_t pos = 0;
  for (auto& b : blocks) {
    uint64_t offset = b->get_partition_offset();
    uint64_t sz = b->get_size();
    if (offset > pos) {
      segs.push_back({pos, offset, nullptr});
    }
    segs.push_back({std::max(pos, offset), offset + sz, b});
    pos = std::max(pos, offset + sz);
  }
  // Trailing free space
  if (pos < total_span) {
    segs.push_back({pos, total_span, nullptr});
  }

  static const double kColors[7][3] = {
    {0.30, 0.55, 0.85},
    {0.35, 0.75, 0.45},
    {0.85, 0.75, 0.30},
    {0.85, 0.50, 0.20},
    {0.80, 0.30, 0.30},
    {0.60, 0.35, 0.75},
    {0.60, 0.50, 0.35},
  };
  static const double kFreeColor[3] = {0.40, 0.40, 0.42};

  int bar_h = 28;

  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, bar_h);
  cairo_clip(cr);

  // Sequential layout: each segment placed end-to-end, minimum 30px,
  // no overlap.  Positions are proportional but small segments get
  // boosted to visibility without obscuring their neighbours.
  struct PxSeg {
    double sx, sw;
    bool is_partition;
    std::shared_ptr<Block> block;
    int color_idx;
  };
  std::vector<PxSeg> psegs;
  {
    double cursor = 0.0;
    int color_idx = 0;
    for (auto& seg : segs) {
      // Skip leading free space (partition table area)
      if (psegs.empty() && !seg.block && cursor == 0.0) continue;
      uint64_t seg_size = seg.end - seg.start;
      double sw = std::max(w * (static_cast<double>(seg_size) / total_span), 150.0);
      // Clamp to remaining bar width so we never overflow
      if (cursor + sw > w) sw = w - cursor;
      if (sw <= 0) break;
      int ci = seg.block ? color_idx++ : -1;
      psegs.push_back({cursor, sw, seg.block != nullptr, seg.block, ci});
      cursor += sw;
    }
  }

  // Draw left-to-right (segments are already in sorted order)
  for (auto& ps : psegs) {
    if (ps.sw <= 0) continue;
    if (ps.is_partition) {
      auto& c = kColors[ps.color_idx % 7];
      set_rgb(cr, c[0], c[1], c[2]);
    } else {
      set_rgb(cr, kFreeColor[0], kFreeColor[1], kFreeColor[2]);
    }
    cairo_rectangle(cr, x + ps.sx, y, ps.sw, bar_h);
    cairo_fill(cr);
  }

  cairo_restore(cr);

  set_rgb(cr, 0.25, 0.25, 0.27);
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, x, y, w, bar_h);
  cairo_stroke(cr);

  int label_y = y + bar_h + 3;
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 9);

  for (auto& ps : psegs) {
    if (ps.is_partition && ps.sw >= 30) {
      std::string dev = ps.block->get_device();
      auto pos2 = dev.find_last_of('/');
      if (pos2 != std::string::npos) dev = dev.substr(pos2 + 1);
      std::string sz = size_str(ps.block->get_size());

      cairo_text_extents_t te;
      cairo_text_extents(cr, dev.c_str(), &te);
      double tx = x + ps.sx + (ps.sw - te.width) * 0.5;
      if (tx < x) tx = x;
      if (tx + te.width > x + w) tx = x + w - te.width;

      set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
      cairo_move_to(cr, tx, label_y + 9);
      cairo_show_text(cr, dev.c_str());

      cairo_text_extents(cr, sz.c_str(), &te);
      tx = x + ps.sx + (ps.sw - te.width) * 0.5;
      if (tx < x) tx = x;
      if (tx + te.width > x + w) tx = x + w - te.width;

      set_rgba(cr, app.textSecR, app.textSecG, app.textSecB, 0.7);
      cairo_move_to(cr, tx, label_y + 20);
      cairo_show_text(cr, sz.c_str());
    }
  }
}

// Drive details.

void draw_drive_details(AppState& app, cairo_t* cr, int x, int y,
                         const std::shared_ptr<Drive>& drive) {
  if (!drive) return;

  int lx = x + 12;
  int ly = y + 16;
  int lh = 18;

  auto draw_field = [&](const char* label, const std::string& value, int& ly) {
    // Label
    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    cairo_move_to(cr, lx, ly);
    cairo_show_text(cr, label);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);

    // Value directly after label
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, lx + te.width + 6, ly);
    cairo_show_text(cr, value.c_str());

    ly += lh;
  };

  draw_field("Model: ", drive->get_model(), ly);
  draw_field("Size: ", size_str(drive->get_size()), ly);
  draw_field("Serial: ", drive->get_serial(), ly);
  draw_field("Firmware: ", drive->get_firmware_version(), ly);

  if (drive->smart_supported()) {
    bool warn = false;
    std::string assessment = drive->smart_one_liner_assessment(&warn);
    draw_field("Assessment:", assessment, ly);
  } else {
    draw_field("SMART:", "Not available", ly);
  }
}

// Partition list.

void draw_partition_list(AppState& app, cairo_t* cr, int x, int y, int w,
                          const std::vector<std::shared_ptr<Block>>& blocks) {
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  for (size_t i = 0; i < blocks.size(); ++i) {
    auto& b = blocks[i];
    int ry = y + static_cast<int>(i) * kPartitionRowH - app.contentScroll;
    if (ry + kPartitionRowH < kHeaderH || ry > app.height) continue;

    bool hover = static_cast<int>(i) == app.partitionHover;
    bool sel = static_cast<int>(i) == app.selected_block;

    if (sel) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.15);
    } else if (hover) {
      set_rgba(cr, 1, 1, 1, 0.04);
    } else if (i % 2 == 0) {
      set_rgba(cr, 1, 1, 1, 0.02);
    } else {
      set_rgba(cr, 0, 0, 0, 0.04);
    }
    cairo_rectangle(cr, x + 2, ry + 2, w - 4, kPartitionRowH - 4);
    cairo_fill(cr);

    std::string dev = b->get_device();
    auto pos = dev.find_last_of('/');
    if (pos != std::string::npos) dev = dev.substr(pos + 1);

    int col_x = x + 12;
    int btn_right_x = x + w - 12;

    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, col_x, ry + 19);
    cairo_show_text(cr, dev.c_str());

    set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
    cairo_set_font_size(cr, 11);
    cairo_move_to(cr, col_x, ry + 38);
    cairo_show_text(cr, size_str(b->get_size()).c_str());

    {
      std::string type = b->get_fstype();
      if (type.empty()) type = b->get_fsusage();
      if (type.empty()) type = b->get_partition_type();
      if (type.empty()) type = "—";
      cairo_move_to(cr, col_x + 100, ry + 38);
      cairo_show_text(cr, type.c_str());
    }

    {
      std::string label = b->get_label();
      if (!label.empty()) {
        set_rgb(cr, app.textR, app.textG, app.textB);
        cairo_set_font_size(cr, 12);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_move_to(cr, col_x + 180, ry + 19);
        cairo_show_text(cr, label.c_str());
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      }
    }

    bool is_mounted = b->is_mounted();
    uint64_t fs_total = 0, fs_used = 0;
    if (is_mounted) {
      fs_total = b->get_fs_total_bytes();
      fs_used = b->get_fs_used_bytes();
    }
    if (is_mounted && fs_total > 0) {
      int bar_x = col_x + 220;
      int bar_w = 64;
      int bar_h = 8;
      int bar_y = ry + 34;
      double pct = static_cast<double>(fs_used) / static_cast<double>(fs_total);

      set_rgba(cr, 0.25, 0.25, 0.27, 0.6);
      draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, 3);
      cairo_fill(cr);

      int fill_w = static_cast<int>(bar_w * pct);
      if (fill_w > 0) {
        if (pct < 0.75) {
          set_rgb(cr, 0.35, 0.70, 0.45);
        } else if (pct < 0.90) {
          set_rgb(cr, 0.85, 0.75, 0.30);
        } else {
          set_rgb(cr, 0.85, 0.30, 0.30);
        }
        draw_rounded_rect(cr, bar_x, bar_y, fill_w, bar_h, 3);
        cairo_fill(cr);
      }

      set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
      cairo_set_font_size(cr, 10);
      std::string usage = size_str(fs_used) + " / " + size_str(fs_total);
      cairo_move_to(cr, bar_x + bar_w + 6, bar_y + 8);
      cairo_show_text(cr, usage.c_str());
    }

    auto feats = b->get_features();
    if (feats & FEATURE_CAN_MOUNT) {
      bool btn_hover = static_cast<int>(i) == app.partitionBtnHover;
      int bw = 70;
      int bx = btn_right_x - bw;
      int by = ry + (kPartitionRowH - kBtnH) / 2;
      if (btn_hover) {
        set_rgb(cr, 0.25, 0.45, 0.30);
      } else {
        set_rgb(cr, 0.20, 0.35, 0.25);
      }
      draw_rounded_rect(cr, bx, by, bw, kBtnH, 6);
      cairo_fill(cr);
      set_rgb(cr, 0.70, 0.95, 0.75);
      cairo_set_font_size(cr, 11);
      cairo_move_to(cr, bx + 14, by + 21);
      cairo_show_text(cr, "Mount");
    } else if (feats & FEATURE_CAN_UNMOUNT) {
      bool btn_hover = static_cast<int>(i) == app.partitionBtnHover;
      int bw = 80;
      int bx = btn_right_x - bw;
      int by = ry + (kPartitionRowH - kBtnH) / 2;
      if (btn_hover) {
        set_rgb(cr, 0.45, 0.25, 0.25);
      } else {
        set_rgb(cr, 0.35, 0.20, 0.20);
      }
      draw_rounded_rect(cr, bx, by, bw, kBtnH, 6);
      cairo_fill(cr);
      set_rgb(cr, 0.95, 0.70, 0.70);
      cairo_set_font_size(cr, 11);
      cairo_move_to(cr, bx + 8, by + 21);
      cairo_show_text(cr, "Unmount");

      auto mps = b->get_mount_points();
      if (!mps.empty()) {
        set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
        cairo_set_font_size(cr, 11);
        std::string mp = mps[0];
        int mp_x = col_x + 450;
        int mp_max_w = btn_right_x - (feats & FEATURE_CAN_UNMOUNT ? 80 + 4 + 30 : 30) - 4 - mp_x;
        if (mp_max_w > 20) {
          cairo_text_extents_t te;
          cairo_text_extents(cr, mp.c_str(), &te);
          if (te.width > mp_max_w) {
            while (mp.size() > 3) {
              cairo_text_extents(cr, (mp + "...").c_str(), &te);
              if (te.width <= mp_max_w - 6) break;
              mp = mp.substr(0, mp.size() - 1);
            }
            mp += "...";
          }
        }
        cairo_move_to(cr, mp_x, ry + 38);
        cairo_show_text(cr, mp.c_str());
      }
    }

    // Menu button (⋯)
    {
      bool mnu_hover = static_cast<int>(i) == app.partitionMenuHover;
      int mw = 30;
      int btn_w_max = (feats & FEATURE_CAN_UNMOUNT) ? 80 : 70;
      int mx = btn_right_x - (feats & (FEATURE_CAN_MOUNT | FEATURE_CAN_UNMOUNT) ? btn_w_max + 4 : 0) - mw;
      int my = ry + (kPartitionRowH - kBtnH) / 2;
      if (mnu_hover) {
        set_rgba(cr, 1, 1, 1, 0.1);
        draw_rounded_rect(cr, mx, my, mw, kBtnH, 6);
        cairo_fill(cr);
      }
      set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
      cairo_set_font_size(cr, 18);
      cairo_move_to(cr, mx + 6, my + 23);
      cairo_show_text(cr, "⋯");
    }

    set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x + 4, ry + kPartitionRowH - 0.5);
    cairo_line_to(cr, x + w - 4, ry + kPartitionRowH - 0.5);
    cairo_stroke(cr);
  }
}

// Partition context menu.

void draw_partition_context_menu(AppState& app, cairo_t* cr) {
  if (!app.partitionMenuOpen || app.selected_block < 0) return;

  int mx = app.partitionMenuX;
  int my = app.partitionMenuY;
  int mw = 180;
  int mh = 32 * 6 + 8;

  cairo_save(cr);
  cairo_translate(cr, 2, 2);
  set_rgba(cr, 0, 0, 0, 0.4);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);
  cairo_translate(cr, -2, -2);

  set_rgb(cr, 0.20, 0.20, 0.22);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_stroke(cr);

  static const char* kMenuItems[] = {
    "New Partition…", "Format…", "Edit Partition…",
    "Edit Filesystem…", "Check", "Repair"
  };
  int miy = my + 4;
  cairo_set_font_size(cr, 13);
  for (int mi = 0; mi < 6; ++mi) {
    if (mi == app.partitionMenuItemHover) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.25);
      cairo_rectangle(cr, mx + 4, miy, mw - 8, 32);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, mx + 14, miy + 22);
    cairo_show_text(cr, kMenuItems[mi]);
    miy += 32;
  }
  cairo_restore(cr);
}

// Drive header menu.

void draw_drive_header_menu(AppState& app, cairo_t* cr) {
  if (!app.driveMenuOpen) return;

  int mx = app.driveMenuX;
  int my = app.driveMenuY;
  int mw = 160;
  int mh = 32 * 3 + 8;

  cairo_save(cr);
  cairo_translate(cr, 2, 2);
  set_rgba(cr, 0, 0, 0, 0.4);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);
  cairo_translate(cr, -2, -2);

  set_rgb(cr, 0.20, 0.20, 0.22);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_stroke(cr);

  static const char* kDriveMenuItems[] = {
    "Power Off", "Standby", "Eject"
  };
  int miy = my + 4;
  cairo_set_font_size(cr, 13);
  for (int mi = 0; mi < 3; ++mi) {
    if (mi == app.driveMenuItemHover) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.25);
      cairo_rectangle(cr, mx + 4, miy, mw - 8, 32);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, mx + 14, miy + 22);
    cairo_show_text(cr, kDriveMenuItems[mi]);
    miy += 32;
  }
  cairo_restore(cr);
}

// Sidebar drive row menu.

void draw_sidebar_drive_menu(AppState& app, cairo_t* cr) {
  if (!app.sidebarDriveMenuOpen || app.sidebarDriveMenuIdx < 0) return;

  int mx = app.sidebarDriveMenuX;
  int my = app.sidebarDriveMenuY;
  int mw = 170;
  int mh = 32 * 6 + 8;

  cairo_save(cr);
  cairo_translate(cr, 2, 2);
  set_rgba(cr, 0, 0, 0, 0.4);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);
  cairo_translate(cr, -2, -2);

  set_rgb(cr, 0.20, 0.20, 0.22);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, mx, my, mw, mh, 8);
  cairo_stroke(cr);

  static const char* kItems[] = {
    "Format Disk…", "Create Image…", "SMART Data",
    "Benchmark…", "Drive Settings", "Power Off"
  };
  int miy = my + 4;
  cairo_set_font_size(cr, 13);
  for (int mi = 0; mi < 6; ++mi) {
    if (mi == app.sidebarDriveMenuHover) {
      set_rgba(cr, app.accentR, app.accentG, app.accentB, 0.25);
      cairo_rectangle(cr, mx + 4, miy, mw - 8, 32);
      cairo_fill(cr);
    }
    set_rgb(cr, app.textR, app.textG, app.textB);
    cairo_move_to(cr, mx + 14, miy + 22);
    cairo_show_text(cr, kItems[mi]);
    miy += 32;
  }
  cairo_restore(cr);
}

// Status bar.

void draw_status_bar(AppState& app, cairo_t* cr, int w) {
  int sb_h = 24;

  set_rgba(cr, app.surfaceR, app.surfaceG, app.surfaceB, 0.85);
  cairo_rectangle(cr, 0, app.height - sb_h, w, sb_h);
  cairo_fill(cr);

  set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
  cairo_set_line_width(cr, 1);
  cairo_move_to(cr, 0, app.height - sb_h + 0.5);
  cairo_line_to(cr, w, app.height - sb_h + 0.5);
  cairo_stroke(cr);

  set_rgb(cr, app.textSecR, app.textSecG, app.textSecB);
  cairo_set_font_size(cr, 10);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_move_to(cr, 10, app.height - sb_h + 16);

  auto drives = Manager::instance().get_drives();
  std::string text = std::to_string(drives.size()) + " drive" + (drives.size() != 1 ? "s" : "");
  if (!app.statusText.empty()) {
    text += "  ·  " + app.statusText;
  }
  cairo_show_text(cr, text.c_str());
}

}
