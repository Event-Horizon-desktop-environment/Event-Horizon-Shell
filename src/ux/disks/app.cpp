#include "ux/disks/app.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <mutex>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "configuration/shell_config.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/block.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"

namespace eh::disks {

// Utilities.

std::string size_str(uint64_t bytes) {
  char buf[64];
  if (bytes >= 1000000000000ull)
    std::snprintf(buf, sizeof(buf), "%.1f TB", bytes / 1000000000000.0);
  else if (bytes >= 1000000000ull)
    std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1000000000.0);
  else if (bytes >= 1000000ull)
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1000000.0);
  else
    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
  return buf;
}

void set_rgba(cairo_t* cr, double r, double g, double b, double a) {
  cairo_set_source_rgba(cr, r, g, b, a);
}

void set_rgb(cairo_t* cr, double r, double g, double b) {
  cairo_set_source_rgb(cr, r, g, b);
}

void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  if (r < 0) r = 0;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

void draw_icon_surface(cairo_t* cr, const eh::icons::IconEntry* icon,
                        int x, int y, int target_size) {
  if (!icon || !icon->surface) return;
  int iw = icon->width;
  int ih = icon->height;
  if (iw <= 0 || ih <= 0) return;
  double scale = static_cast<double>(target_size) / std::max(iw, ih);
  int dw = static_cast<int>(std::lround(iw * scale));
  int dh = static_cast<int>(std::lround(ih * scale));
  int dx = x + (target_size - dw) / 2;
  int dy = y + (target_size - dh) / 2;
  cairo_save(cr);
  cairo_translate(cr, dx, dy);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, icon->surface, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

const char* drive_icon_name(const std::shared_ptr<Drive>& d) {
  if (d->is_removable()) return "drive-removable-media";
  return "drive-harddisk";
}

std::vector<std::shared_ptr<Block>> filter_blocks(
    const std::vector<std::shared_ptr<Block>>& all) {
  std::vector<std::shared_ptr<Block>> result;
  bool has_any_partition = false;
  for (auto& b : all) {
    if (b->has_partition()) {
      result.push_back(b);
      has_any_partition = true;
    }
  }
  if (!has_any_partition) result = all;
  return result;
}

void disk_log(const char* fmt, ...) {
  static FILE* logf = [] {
    FILE* f = fopen("/tmp/horizon-disks.log", "w");
    return f ? f : stderr;
  }();
  va_list ap;
  va_start(ap, fmt);
  vfprintf(logf, fmt, ap);
  va_end(ap);
  fprintf(logf, "\n");
  fflush(logf);
}

// AppState.

AppState::AppState() = default;
AppState::~AppState() = default;

// Main draw.

void draw(AppState& app) {
  if (!app.surface) return;
  int w = app.width, h = app.height;
  if (w <= 0 || h <= 0) return;

  auto* buf = (!app.buf[0].busy()) ? &app.buf[0] : &app.buf[1];
  if (buf->busy()) return;

  if (app.shm && (buf->width() != w || buf->height() != h)) {
    const char* tag = (buf == &app.buf[0]) ? "eh-disks-a" : "eh-disks-b";
    buf->ensure(app.shm, tag, w, h);
  }

  cairo_t* cr = buf->cairo();
  if (!cr) return;

  app.last_paint_w = w;
  app.last_paint_h = h;

  // Clear to transparent so alpha layers correctly each frame
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  set_rgba(cr, app.bgR, app.bgG, app.bgB, 0.75);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  // Dynamically size sidebar width to fit the longest drive name
  if (app.needs_refresh) {
    auto drives = Manager::instance().get_drives();
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    double max_name_w = 0;
    for (auto& d : drives) {
      std::string model = d->get_model();
      if (model.empty()) model = d->get_description();
      cairo_text_extents_t te;
      cairo_text_extents(cr, model.c_str(), &te);
      if (te.width > max_name_w) max_name_w = te.width;
    }
    // Also measure subtitle (size + serial) at 10px normal
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    double max_sub_w = 0;
    for (auto& d : drives) {
      std::string sub = size_str(d->get_size()) + "  " + d->get_serial();
      cairo_text_extents_t te;
      cairo_text_extents(cr, sub.c_str(), &te);
      if (te.width > max_sub_w) max_sub_w = te.width;
    }
    double max_txt = std::max(max_name_w, max_sub_w);
    // 42 = icon left area (12) + icon (24) + gap (6)
    // 36 = hamburger button start (28) + button width (20) + right padding (4) - overlap
    //   = 28 + 4 = 32 actually.  Let's compute: button at w-28, 20px wide → right edge at w-8.
    //   Row bg ends at w-4 (x=4, w-8).  So right padding = 4.  Room needed = 28 + 4 = 32.
    //   But we want some gap between text and button too.
    //   42 + txt + 32 + 8 (gap) = 42 + txt + 40
    app.sidebarW = std::max(220, static_cast<int>(max_txt) + 42 + 40);
  }

  int sidebar_w = app.sidebarW;
  int content_x = sidebar_w;
  int content_w = w - sidebar_w;
  int content_y = kHeaderH;
  int view_h = h;

  // Header
  draw_header(app, cr, w);

  // Sidebar
  draw_sidebar(app, cr, sidebar_w, view_h);

  // Content area
  auto drives = Manager::instance().get_drives();

  if (app.selected_drive >= 0 && app.selected_drive < static_cast<int>(drives.size())) {
    auto& drive = drives[app.selected_drive];
    int cx = content_x + 12;
    int cw = content_w - 24;
    int cy = content_y + 8;

    cairo_save(cr);
    cairo_rectangle(cr, content_x, content_y, content_w, view_h - content_y);
    cairo_clip(cr);

    // Drive details section
    {
      int dh = 110;
      draw_drive_details(app, cr, cx, cy, drive);
      cy += dh + 8;

      set_rgb(cr, app.outlineR, app.outlineG, app.outlineB);
      cairo_move_to(cr, cx, cy);
      cairo_line_to(cr, cx + cw, cy);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);
      cy += 8;
    }

    // Volumes heading
    {
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 14);
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_move_to(cr, cx, cy + 16);
      cairo_show_text(cr, "Volumes");
      cy += 24;
    }

    // Partition map
    auto& all_blocks = drive->blocks();
    auto blocks = filter_blocks(all_blocks);
    {
      draw_partition_bar(app, cr, blocks, cx, cy, cw, drive->get_size());
      cy += 28 + 22 + 8;
    }

    // Partition rows
    {
      int rows_y = cy;
      int rows_total = static_cast<int>(blocks.size()) * kPartitionRowH;
      app.contentH = rows_total + 20;

      draw_partition_list(app, cr, cx, rows_y, cw, blocks);
      cy += rows_total;
    }

    cairo_restore(cr);

    // Content scrollbar
    int view_content_h = view_h - content_y - 20;
    if (app.contentH > view_content_h && view_content_h > 0) {
      int scroll_h = std::max(view_content_h * view_content_h / app.contentH, 20);
      int scroll_y = content_y + (view_content_h - scroll_h) * app.contentScroll /
                         (app.contentH - view_content_h);
      set_rgba(cr, 1, 1, 1, 0.3);
      draw_rounded_rect(cr, content_x + content_w - 8, scroll_y, 4, scroll_h, 2);
      cairo_fill(cr);
    }

    // Context menus
    draw_partition_context_menu(app, cr);
    draw_drive_header_menu(app, cr);
    draw_sidebar_drive_menu(app, cr);

    // Dialogs
    if (app.createPartDlg.open) {
      draw_create_partition_dialog(app, cr);
    }
    if (app.fmtDiskDlg.open) {
      draw_format_disk_dialog(app, cr);
    }
    if (app.fmtDlg.open) {
      draw_format_volume_dialog(app, cr, app.selected_block);
    }
  }

  // Status bar
  draw_status_bar(app, cr, w);

  // Present
  wl_surface* surf = app.surface;
  wl_surface_set_buffer_scale(surf, 1);
  wl_surface_damage_buffer(surf, 0, 0, w, h);
  wl_surface_attach(surf, buf->wl(), 0, 0);
  buf->mark_busy();

  if (app.frame_cb) wl_callback_destroy(app.frame_cb);
  app.frame_cb = wl_surface_frame(surf);
  static constexpr wl_callback_listener kFrameListener{
    .done = [](void* data, wl_callback*, uint32_t) {
      auto& a = *static_cast<AppState*>(data);
      a.frame_cb = nullptr;
      if (a.pendingRedraw) draw(a);
    }
  };
  wl_callback_add_listener(app.frame_cb, &kFrameListener, &app);

  wl_surface_commit(surf);
  wl_display_flush(app.wl.display());
}

void schedule_frame(AppState& app) {
  app.pendingRedraw = true;
}

}
