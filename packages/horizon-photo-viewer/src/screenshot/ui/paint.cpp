#include "screenshot/ui/paint.hpp"
#include "screenshot/ui/layout.hpp"
#include "screenshot/ui/button/button.hpp"
#include "screenshot/app.hpp"
#include "screenshot/capture/capture.hpp"
#include "core/screenshot/icon_cache.hpp"
#include "wayland/core/connection.hpp"
#include <ext-foreign-toplevel-list-v1-client-protocol.h>

#include "core/screenshot/logging.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hpv::sc {

static void set_surface_dim(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen) {
    double f = 0.7;
    cairo_set_source_rgba(cr, app.chrome.dockFillR * f, app.chrome.dockFillG * f, app.chrome.dockFillB * f, a);
  } else {
    cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, a);
  }
}

static void set_panel(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen)
    cairo_set_source_rgba(cr, app.chrome.panelFillR, app.chrome.panelFillG, app.chrome.panelFillB, a);
  else
    cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, a);
}

static void set_accent_container(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen)
    cairo_set_source_rgba(cr, app.chrome.accentR, app.chrome.accentG, app.chrome.accentB, a * 0.18);
  else
    cairo_set_source_rgba(cr, 0.39, 0.40, 0.96, a * 0.15);
}

static void set_text(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen)
    cairo_set_source_rgba(cr, app.chrome.textR, app.chrome.textG, app.chrome.textB, a);
  else
    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, a);
}

static void set_text_secondary(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen)
    cairo_set_source_rgba(cr, app.chrome.textR * 0.55, app.chrome.textG * 0.55, app.chrome.textB * 0.55, a);
  else
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, a);
}

static void set_text_muted(cairo_t* cr, const AppState& app, double a)
{
  if (app.haveMatugen)
    cairo_set_source_rgba(cr, app.chrome.textR * 0.35, app.chrome.textG * 0.35, app.chrome.textB * 0.35, a);
  else
    cairo_set_source_rgba(cr, 0.33, 0.33, 0.33, a);
}


void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r)
{
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  cairo_move_to(cr, x + r, y);
  cairo_line_to(cr, x + w - r, y);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_line_to(cr, x + w, y + h - r);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_line_to(cr, x + r, y + h);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_line_to(cr, x, y + r);
  cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
  cairo_close_path(cr);
}

static void draw_text(cairo_t* cr, const char* text, double x, double y, double font_size, bool bold)
{
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, font_size);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

static void draw_text_centered(cairo_t* cr, const char* text, double cx, double cy, double font_size, bool bold)
{
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, font_size);
  cairo_text_extents_t te;
  cairo_text_extents(cr, text, &te);
  cairo_move_to(cr, cx - te.width / 2, cy + te.height / 2);
  cairo_show_text(cr, text);
}

static void draw_preview(AppState& app, cairo_t* cr, const Layout& l)
{
  SC_LOG("draw_preview: enter captured_valid=%d zoom=%.2f", app.captured.valid, app.zoom);
  set_surface_dim(cr, app, 0.75);
  cairo_rectangle(cr, l.preview_x, l.preview_y,
                  l.preview_w, l.preview_h);
  cairo_fill(cr);

  if (app.captured.valid && app.captured.width > 0 && app.captured.height > 0) {
    int pad = 20;
    int avail_w = l.preview_w - pad * 2;
    int avail_h = l.preview_h - pad * 2;
    double fit_scale = (std::min)(
        static_cast<double>(avail_w) / app.captured.width,
        static_cast<double>(avail_h) / app.captured.height);
    double scale = fit_scale * app.zoom;
    if (scale > 20.0) scale = 20.0;

    int img_w = static_cast<int>(app.captured.width * scale);
    int img_h = static_cast<int>(app.captured.height * scale);
    int img_x = l.preview_cx - img_w / 2 + static_cast<int>(app.pan_x);
    int img_y = l.preview_cy - img_h / 2 + static_cast<int>(app.pan_y);

    cairo_save(cr);
    cairo_rectangle(cr, l.preview_x, l.preview_y, l.preview_w, l.preview_h);
    cairo_clip(cr);

    if (!app.cached_img) {
      app.cached_img = cairo_image_surface_create_for_data(
          app.captured.pixels.data(), CAIRO_FORMAT_ARGB32,
          app.captured.width, app.captured.height,
          app.captured.width * 4);
    }

    cairo_save(cr);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, app.cached_img, img_x / scale, img_y / scale);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_restore(cr);

    if (app.zoom != 1.0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.0f%%", app.zoom * 100.0);
      set_text(cr, app, 0.9);
      draw_text(cr, buf, l.preview_x + 20, l.preview_y + 34, 13, false);
    }
  } else {
    set_text_muted(cr, app, 0.6);
    draw_text_centered(cr, "No capture yet", l.preview_cx, l.preview_cy - 8, 15, false);
    set_text_muted(cr, app, 0.4);
    draw_text_centered(cr, "Select a source and press Capture", l.preview_cx, l.preview_cy + 16, 11, false);
  }
}

static void draw_grid_cell(cairo_t* cr, const AppState& app, const Layout& l,
                           int col, int row, const char* label, const char* sublabel,
                           bool selected)
{
  int x = l.sidebar_content_x + col * (l.grid_cell_w + 12);
  int y = l.grid_y + row * (l.grid_cell_h + 12);
  int idx = row * 2 + col;
  bool hovered = (app.hovered_area == 2 && app.hovered_item == idx);
  bool pressed = (app.pressed_area == 2 && app.pressed_item == idx);

  draw_vista_button(cr, app, static_cast<double>(x), static_cast<double>(y),
                    static_cast<double>(l.grid_cell_w), static_cast<double>(l.grid_cell_h),
                    label, sublabel, selected, hovered, pressed);
}

static void draw_output_list(AppState& app, cairo_t* cr, const Layout& l)
{
  SC_LOG("draw_output_list: enter outputs=%zu selected=%d", app.output_list.size(), app.selected_output_idx);
  if (app.source != Source::Screen || app.output_list.empty()) return;

  int list_section_y = l.list_y;
  int header_h = 20;
  int n = static_cast<int>(app.output_list.size());

  set_text_secondary(cr, app, 0.8);
  draw_text(cr, "Outputs", l.sidebar_content_x, list_section_y + header_h - 4, 11, false);

  int item_y = list_section_y + header_h + 4;
  int max_items = (l.export_y - item_y - 8) / l.list_item_h;
  if (max_items < 1) max_items = 1;

  int visual_count = n + 1;
  for (int vi = 0; vi < max_items && vi < visual_count; ++vi) {
    int iy = item_y + vi * l.list_item_h;
    bool hovered = (app.hovered_area == 3 && app.hovered_item == vi);
    bool selected = false;

    if (vi == 0) {
      selected = app.capture_all_screens;

      if (selected || hovered) {
        set_accent_container(cr, app, hovered ? 0.7 : 1.0);
        draw_rounded_rect(cr, l.sidebar_content_x, iy, l.sidebar_content_w, l.list_item_h - 2, 8);
        cairo_fill(cr);
      }

      set_text(cr, app, 0.9);
      draw_text(cr, "All Screens", l.sidebar_content_x + 12, iy + 16, 12, false);
      set_text_secondary(cr, app, 0.5);
      std::string sub = std::to_string(n) + " outputs";
      draw_text(cr, sub.c_str(), l.sidebar_content_x + 12, iy + 30, 10, false);
      continue;
    }

    int oi = vi - 1;
    const auto& entry = app.output_list[oi];
    selected = (app.selected_output_idx == oi && !app.capture_all_screens);

    if (selected || hovered) {
      set_accent_container(cr, app, hovered ? 0.7 : 1.0);
      draw_rounded_rect(cr, l.sidebar_content_x, iy, l.sidebar_content_w, l.list_item_h - 2, 8);
      cairo_fill(cr);
    }

    set_text(cr, app, 0.9);
    std::string label = entry.name.empty() ? "Output " + std::to_string(oi) : entry.name;
    draw_text(cr, label.c_str(), l.sidebar_content_x + 12, iy + 16, 12, false);

    int badge_x = l.sidebar_content_x + 12 + static_cast<int>(label.size()) * 7 + 8;
    if (entry.is_hdr) {
      cairo_set_source_rgba(cr, 0.95, 0.65, 0.05, 0.9);
      draw_rounded_rect(cr, badge_x, iy + 6, 30, 14, 3);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
      draw_text(cr, "HDR", badge_x + 3, iy + 16, 8, true);
      badge_x += 32;
    }
    if (entry.is_10bit) {
      cairo_set_source_rgba(cr, 0.1, 0.7, 0.9, 0.9);
      draw_rounded_rect(cr, badge_x, iy + 6, 36, 14, 3);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
      draw_text(cr, "10bit", badge_x + 3, iy + 16, 8, true);
    }

    set_text_secondary(cr, app, 0.5);
    std::string dims = std::to_string(entry.width) + "x" + std::to_string(entry.height);
    draw_text(cr, dims.c_str(), l.sidebar_content_x + 12, iy + 30, 10, false);
  }
}

static void draw_window_list(AppState& app, cairo_t* cr, const Layout& l)
{
  SC_LOG("draw_window_list: enter windows=%zu selected=%d", app.window_list.size(), app.selected_window_idx);
  if (app.source != Source::Window || app.window_list.empty()) return;

  int list_section_y = l.list_y;
  int header_h = 20;

  set_text_secondary(cr, app, 0.8);
  draw_text(cr, "Open Windows", l.sidebar_content_x, list_section_y + header_h - 4, 11, false);

  int item_y = list_section_y + header_h + 4;
  int max_items = (l.export_y - item_y - 8) / l.list_item_h;
  if (max_items < 1) max_items = 1;

  for (int i = 0; i < max_items && i < static_cast<int>(app.window_list.size()); ++i) {
    int iy = item_y + i * l.list_item_h;
    bool hovered = (app.hovered_area == 1 && app.hovered_item == i);
    bool selected = (app.selected_window_idx == i);

    if (selected || hovered) {
      set_accent_container(cr, app, hovered ? 0.7 : 1.0);
      draw_rounded_rect(cr, l.sidebar_content_x, iy, l.sidebar_content_w, l.list_item_h - 2, 8);
      cairo_fill(cr);
    }

    const auto& entry = app.window_list[i];
    std::string label = entry.appId.empty() ? entry.title : entry.appId;
    if (label.empty()) label = "(unnamed)";

    int icon_size = 20;
    int icon_x = l.sidebar_content_x + 12;
    int icon_y = iy + (l.list_item_h - icon_size) / 2;

    const auto* icon = app.iconCache.app_icon(entry.appId);
    if (icon && icon->surface) {
      cairo_save(cr);
      double scale = static_cast<double>(icon_size) / icon->width;
      cairo_translate(cr, icon_x, icon_y);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, icon->surface, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      cairo_set_source_rgb(cr, 0.25, 0.28, 0.35);
      cairo_arc(cr, icon_x + icon_size / 2, icon_y + icon_size / 2, icon_size / 2, 0, 2 * M_PI);
      cairo_fill(cr);

      char initial[2] = {label.empty() ? '?' : static_cast<char>(std::toupper(label[0])), 0};
      set_text_secondary(cr, app, 0.7);
      draw_text_centered(cr, initial, icon_x + icon_size / 2, icon_y + icon_size / 2, 10, true);
    }

    set_text(cr, app, 0.9);
    draw_text(cr, label.c_str(), l.sidebar_content_x + 40, iy + 16, 12, false);

    if (!entry.title.empty() && entry.title != label) {
      set_text_secondary(cr, app, 0.5);
      draw_text(cr, entry.title.c_str(), l.sidebar_content_x + 40, iy + 30, 10, false);
    }
  }
}

static void draw_flat_btn(cairo_t* cr, const AppState&,
                           double x, double y, double w, double h,
                           const char* label, bool hovered)
{
  if (hovered) {
    cairo_set_source_rgba(cr, 0.247, 0.247, 0.275, 1.0);
  } else {
    cairo_set_source_rgba(cr, 0.153, 0.153, 0.165, 1.0);
  }
  draw_rounded_rect(cr, x, y, w, h, 12);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14);
  cairo_text_extents_t te;
  cairo_text_extents(cr, label, &te);
  cairo_move_to(cr, x + (w - te.width) / 2, y + h / 2 + te.height / 2);
  cairo_show_text(cr, label);
}

static void draw_export_bar(AppState& app, cairo_t* cr, const Layout& l)
{
  cairo_set_source_rgba(cr, 0.247, 0.247, 0.275, 0.8);
  cairo_rectangle(cr, l.sidebar_x, l.export_y, l.sidebar_w, 1);
  cairo_fill(cr);

  double pad = 20;
  double bx = l.sidebar_content_x;
  double bw = l.export_btn_w;
  double bh = 42;
  double by = l.export_y + pad;

  draw_flat_btn(cr, app, bx, by, bw, bh, "Save",
                app.hovered_area == 4 && app.hovered_item == 0);
  draw_flat_btn(cr, app, bx + bw + 12, by, bw, bh, "Copy",
                app.hovered_area == 4 && app.hovered_item == 1);
  draw_flat_btn(cr, app, bx + 2 * (bw + 12), by, bw, bh, "Save As",
                app.hovered_area == 4 && app.hovered_item == 2);
}

static void draw_sidebar(AppState& app, cairo_t* cr, const Layout& l)
{
  SC_LOG("draw_sidebar: enter source=%d", (int)app.source);
  set_panel(cr, app, 0.75);
  cairo_rectangle(cr, l.sidebar_x, l.sidebar_y, l.sidebar_w, l.sidebar_h);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.247, 0.247, 0.275, 0.8);
  cairo_rectangle(cr, l.sidebar_x, l.sidebar_y, 1, l.sidebar_h);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 26);
  cairo_text_extents_t te;
  cairo_text_extents(cr, "Horizon Shot", &te);
  double title_cx = l.sidebar_x + l.sidebar_w / 2.0;
  double title_cy = l.title_y + l.title_h / 2.0;
  cairo_move_to(cr, title_cx - te.width / 2.0, title_cy - te.y_bearing - te.height / 2.0);
  cairo_show_text(cr, "Horizon Shot");

  cairo_set_source_rgba(cr, 0.247, 0.247, 0.275, 0.8);
  cairo_rectangle(cr, l.sidebar_x, l.title_y + l.title_h, l.sidebar_w, 1);
  cairo_fill(cr);

  draw_grid_cell(cr, app, l, 0, 0, "Focused", nullptr, app.source == Source::Focused);
  draw_grid_cell(cr, app, l, 1, 0, "Window", nullptr, app.source == Source::Window);
  draw_grid_cell(cr, app, l, 0, 1, "Screen", nullptr, app.source == Source::Screen);
  draw_grid_cell(cr, app, l, 1, 1, "Selection", nullptr, app.source == Source::Selection);

  draw_output_list(app, cr, l);
  draw_window_list(app, cr, l);

  draw_export_bar(app, cr, l);
}

void paint_frame(AppState& app)
{
  SC_LOG("paint_frame: enter width=%d height=%d", app.width, app.height);
  if (!app.shm || !app.surface) return;

  int bi = app.paint_buf;
  if (app.buf[bi].busy()) {
    app.pendingRedraw = true;
    return;
  }

  if (!app.buf[bi].ensure(app.shm, "eh-shot", app.width, app.height)) return;

  cairo_t* cr = app.buf[bi].cairo();
  cairo_save(cr);

  auto l = compute_layout(app.width, app.height);

  if (app.dragging) {
    cairo_rectangle(cr, l.preview_x, l.preview_y, l.preview_w, l.preview_h);
    cairo_clip(cr);
    draw_preview(app, cr, l);

    wl_surface_attach(app.surface, app.buf[bi].wl(), 0, 0);
    wl_surface_damage_buffer(app.surface, l.preview_x, l.preview_y,
                             l.preview_w, l.preview_h);
  } else {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_preview(app, cr, l);
    draw_sidebar(app, cr, l);

    wl_surface_attach(app.surface, app.buf[bi].wl(), 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
  }

  cairo_restore(cr);

  wl_surface_commit(app.surface);
  app.paint_buf = 1 - bi;
  app.pendingRedraw = false;

  wl_display_flush(app.wl.display());
}

}
