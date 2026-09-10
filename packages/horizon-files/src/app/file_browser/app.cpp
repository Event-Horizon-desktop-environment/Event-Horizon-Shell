#include "app/file_browser/app.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include <unistd.h>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>

#include "config/shell_config.hpp"
#include "theme/core/primitives/box.hpp"

namespace eh::file_browser {

// ── paint (extracted draw logic, no buffer/surface dependency) ────

void paint(AppState& app, cairo_t* cr) {
  // Check if settings TOML changed and re-apply colors if so
  {
    static timespec s_last_mtime{};
    static bool s_initialized = false;
    const std::string toml_path = eh::config::state_settings_toml_path();
    struct stat st{};
    if (::stat(toml_path.c_str(), &st) == 0) {
      if (!s_initialized) {
        s_last_mtime = st.st_mtim;
        s_initialized = true;
      } else if (st.st_mtim.tv_sec != s_last_mtime.tv_sec ||
                 st.st_mtim.tv_nsec != s_last_mtime.tv_nsec) {
        s_last_mtime = st.st_mtim;
        reload_colors_from_config(app);
      }
    }
  }

  // Reset thumbnail decode budget for this frame
  app.thumb_decodes_this_frame = 0;
  app.thumb_pending_queue.clear();

  // Smooth scroll (tab content)
  auto smooth_scroll_tab = [&](Tab& tab) {
    bool scrolling = std::abs(tab.scroll_smooth_current - tab.scroll_smooth_target) > 0.5;
    if (scrolling) {
      timespec ts{};
      clock_gettime(CLOCK_MONOTONIC, &ts);
      auto now_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                    static_cast<uint64_t>(ts.tv_nsec);
      double elapsed_ms =
          static_cast<double>(now_ns - app.scroll_anim_start_ns) / 1000000.0;
      constexpr double kTau = 40.0;
      double factor = 1.0 - std::exp(-elapsed_ms / kTau);
      double diff = tab.scroll_smooth_target - tab.scroll_smooth_current;
      tab.scroll_smooth_current += diff * factor;
      if (std::abs(tab.scroll_smooth_current - tab.scroll_smooth_target) <= 0.5) {
        tab.scroll_smooth_current = tab.scroll_smooth_target;
      }
      tab.scroll_px = static_cast<int>(std::lround(tab.scroll_smooth_current));
      app.scroll_needs_redraw = true;
    } else {
      tab.scroll_px = static_cast<int>(std::lround(tab.scroll_smooth_target));
      tab.scroll_smooth_current = tab.scroll_smooth_target;
    }
  };

  smooth_scroll_tab(app.cur_tab());
  if (app.split_view) smooth_scroll_tab(app.right_pane);

  // Smooth scroll (computer view)
  bool comp_scrolling = std::abs(app.computer_scroll_smooth_current - app.computer_scroll_smooth_target) > 0.5;
  if (comp_scrolling) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    auto now_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                  static_cast<uint64_t>(ts.tv_nsec);
    double elapsed_ms =
        static_cast<double>(now_ns - app.scroll_anim_start_ns) / 1000000.0;
    constexpr double kTau = 40.0;
    double factor = 1.0 - std::exp(-elapsed_ms / kTau);
    double diff = app.computer_scroll_smooth_target - app.computer_scroll_smooth_current;
    app.computer_scroll_smooth_current += diff * factor;
    if (std::abs(app.computer_scroll_smooth_current - app.computer_scroll_smooth_target) <= 0.5) {
      app.computer_scroll_smooth_current = app.computer_scroll_smooth_target;
    }
    app.computer_scroll_px = static_cast<int>(std::lround(app.computer_scroll_smooth_current));
    app.scroll_needs_redraw = true;
  } else {
    app.computer_scroll_px = static_cast<int>(std::lround(app.computer_scroll_smooth_target));
    app.computer_scroll_smooth_current = app.computer_scroll_smooth_target;
  }

  bool tab_scrolling = std::abs(app.cur_tab().scroll_smooth_current - app.cur_tab().scroll_smooth_target) > 0.5;
  if (app.split_view)
    tab_scrolling = tab_scrolling ||
      std::abs(app.right_pane.scroll_smooth_current - app.right_pane.scroll_smooth_target) > 0.5;

  if (!tab_scrolling && !comp_scrolling) {
    app.scroll_needs_redraw = false;
  }

  int w = app.width;
  int h = app.height;
  int top_h = app.top_bar_height;
  app.tab_bar_height = (app.tabs.size() > 1) ? 44 : 0;
  int tab_h = app.tab_bar_height;
  int status_h = app.status_bar_height;

  // Fit sidebar width to longest label
  if (app.sidebar_expanded && !app.sidebar_locations.empty()) {
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * 1.2);
    double max_text_w = 0;
    for (const auto& loc : app.sidebar_locations) {
      cairo_text_extents_t te;
      cairo_text_extents(cr, loc.label.c_str(), &te);
      if (te.x_advance > max_text_w) max_text_w = te.x_advance;
    }
    cairo_restore(cr);
    int needed = static_cast<int>(std::ceil(68.0 + max_text_w));
    int new_base = std::max(150, needed);
    if (new_base != app.sidebar_width_base) {
      app.sidebar_width_base = new_base;
      app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));
    }
  }

  int info_panel_w = 0;
  if (app.info_panel_open) {
    app.info_panel_width = std::max(200, static_cast<int>(280 * app.zoom_pct / 100.0));
    info_panel_w = app.info_panel_width;
  }
  int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
  int content_x = sidebar_w;
  int content_w = w - sidebar_w - info_panel_w;
  int selector_h = (app.select_dir_mode || app.select_file_mode) ? app.select_bar_h : 0;
  int content_y = top_h + tab_h;
  int view_h = h - top_h - tab_h - status_h - selector_h;
  int pane_top_h = app.split_view ? app.top_bar_height : 0;

  // Update info panel metadata if selection changed
  if (app.info_panel_open) {
    std::string cur_path;
    int si = app.cur_tab().selected_idx;
    if (si >= 0 && si < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int ri = app.cur_tab().visible_entries[si];
      if (ri >= 0 && ri < static_cast<int>(app.cur_tab().entries.size()))
        cur_path = app.cur_tab().entries[ri].path;
    }
    if (cur_path != app.info_panel_path) {
      app.info_panel_path = cur_path;
      app.info_panel_name.clear();
      app.info_panel_size = 0;
      app.info_panel_modified_sec = 0;
      app.info_panel_is_dir = false;
      app.info_panel_owner.clear();
      app.info_panel_group.clear();
      app.info_panel_mode = 0;
      app.info_panel_mime_type.clear();

      if (!cur_path.empty()) {
        auto& e = app.cur_tab().entries[app.cur_tab().visible_entries[app.cur_tab().selected_idx]];
        app.info_panel_name = e.name;
        app.info_panel_is_dir = e.is_dir;
        struct stat st;
        if (stat(cur_path.c_str(), &st) == 0) {
          app.info_panel_size = static_cast<uint64_t>(st.st_size);
          app.info_panel_modified_sec = st.st_mtime;
          app.info_panel_mode = st.st_mode;
          struct passwd* pw = getpwuid(st.st_uid);
          app.info_panel_owner = pw ? pw->pw_name : std::to_string(st.st_uid);
          struct group* gr = getgrgid(st.st_gid);
          app.info_panel_group = gr ? gr->gr_name : std::to_string(st.st_gid);
        }
        if (!e.mime_type.empty())
          app.info_panel_mime_type = e.mime_type;
      }
    }
  }

  double surf_alpha = app.surface_opacity_pct / 100.0;

  // Background (content column)
  cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, surf_alpha);
  cairo_rectangle(cr, sidebar_w, 0, content_w, h - status_h);
  cairo_fill(cr);

  // Sidebar
  int sidebar_h = h - status_h;
  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, sidebar_w, sidebar_h);
  cairo_clip(cr);
  double sidebar_alpha = app.sidebar_opacity_pct / 100.0;
  cairo_set_source_rgba(cr, app.surface_r * 2, app.surface_g * 2, app.surface_b * 2, sidebar_alpha);
  cairo_rectangle(cr, 0, 0, sidebar_w, sidebar_h);
  cairo_fill(cr);
  draw_sidebar(app, cr, sidebar_w, 0, view_h);
  cairo_restore(cr);

  // Sidebar separator (and resize handle)
  if (sidebar_w > 0) {
    int sep_h = h - status_h;
    if (app.sidebar_hover_resize) {
      cairo_set_source_rgba(cr, 0.4, 0.6, 1.0, 0.6);
      cairo_rectangle(cr, sidebar_w - 1, 0, 3, sep_h);
    } else {
      cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b,
                             0.3);
      cairo_rectangle(cr, sidebar_w, 0, 1, sep_h);
    }
    cairo_fill(cr);
  }

  // Content area
  cairo_save(cr);
  cairo_rectangle(cr, content_x, content_y, content_w, view_h);
  cairo_clip(cr);

  if (app.split_view) {
    int div_w = 4;
    int split = app.split_divider_x;
    if (split <= 0) split = content_w / 2;
    int left_w = std::max(100, split - div_w / 2);
    int right_x = std::min(content_x + content_w - 100, content_x + split + div_w / 2);
    int right_w = content_x + content_w - right_x;
    int list_y = content_y + pane_top_h;
    int list_h = view_h - pane_top_h;

    // Left pane
    app.active_pane = 0;
    {
      cairo_save(cr);
      cairo_translate(cr, 0, content_y);
      draw_top_bar(app, cr, left_w, pane_top_h, content_x, left_w);
      cairo_restore(cr);
      int sb_lx = right_x - 10;
      if (app.cur_tab().view_mode == ViewMode::List) {
        draw_scrollbar(cr, sb_lx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_list_view(app, cr, content_x, list_y, left_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Grid) {
        draw_scrollbar(cr, sb_lx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_grid_view(app, cr, content_x, list_y, left_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Computer) {
        draw_computer_view(app, cr, content_x, list_y, left_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Tree) {
        draw_scrollbar(cr, sb_lx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_tree_view(app, cr, content_x, list_y, left_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Compact) {
        draw_scrollbar(cr, sb_lx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_compact_view(app, cr, content_x, list_y, left_w, list_h);
      }
    }
    cairo_restore(cr);

    // Divider
    cairo_save(cr);
    int div_x = content_x + split;
    if (app.split_divider_hover || app.split_divider_dragging) {
      cairo_set_source_rgba(cr, 0.4, 0.6, 1.0, 0.6);
    } else {
      cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b, 0.3);
    }
    cairo_rectangle(cr, div_x, content_y, div_w, view_h);
    cairo_fill(cr);
    cairo_restore(cr);

    // Right pane
    cairo_save(cr);
    cairo_rectangle(cr, right_x, content_y, right_w, view_h);
    cairo_clip(cr);
    app.active_pane = 1;
    {
      cairo_save(cr);
      cairo_translate(cr, 0, content_y);
      draw_top_bar(app, cr, right_w, pane_top_h, right_x, right_w);
      cairo_restore(cr);
      int sb_rx = content_x + content_w - 10;
      if (app.cur_tab().view_mode == ViewMode::List) {
        draw_scrollbar(cr, sb_rx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_list_view(app, cr, right_x, list_y, right_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Grid) {
        draw_scrollbar(cr, sb_rx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_grid_view(app, cr, right_x, list_y, right_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Computer) {
        draw_computer_view(app, cr, right_x, list_y, right_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Tree) {
        draw_scrollbar(cr, sb_rx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_tree_view(app, cr, right_x, list_y, right_w, list_h);
      } else if (app.cur_tab().view_mode == ViewMode::Compact) {
        draw_scrollbar(cr, sb_rx, list_y, list_h, app.cur_tab().content_h, list_h,
                       app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
        draw_compact_view(app, cr, right_x, list_y, right_w, list_h);
      }
    }
    app.active_pane = 0;
  } else {
    // Single pane
    int sb_x = w - info_panel_w - 10;
    if (app.cur_tab().view_mode == ViewMode::Computer) {
      draw_scrollbar(cr, sb_x, content_y, view_h, app.computer_content_h, view_h,
                     app.computer_scroll_px, app.outline_r, app.outline_g, app.outline_b);
    } else {
      draw_scrollbar(cr, sb_x, content_y, view_h, app.cur_tab().content_h, view_h,
                     app.cur_tab().scroll_px, app.outline_r, app.outline_g, app.outline_b);
    }
    if (app.cur_tab().view_mode == ViewMode::List) {
      draw_list_view(app, cr, content_x, content_y, content_w, view_h);
    } else if (app.cur_tab().view_mode == ViewMode::Grid) {
      draw_grid_view(app, cr, content_x, content_y, content_w, view_h);
    } else if (app.cur_tab().view_mode == ViewMode::Computer) {
      draw_computer_view(app, cr, content_x, content_y, content_w, view_h);
    } else if (app.cur_tab().view_mode == ViewMode::Tree) {
      draw_tree_view(app, cr, content_x, content_y, content_w, view_h);
    } else if (app.cur_tab().view_mode == ViewMode::Compact) {
      draw_compact_view(app, cr, content_x, content_y, content_w, view_h);
    }
  }

  // Marquee selection rectangle
  if (app.marquee_active) {
    draw_marquee(app, cr);
  }

  cairo_restore(cr);  // restore content-area clip

  // Top bar
  if (top_h > 0)
    draw_top_bar(app, cr, w, top_h);

  // Tab bar (shifted down by top_h)
  cairo_save(cr);
  cairo_translate(cr, 0, top_h);
  cairo_rectangle(cr, 0, 0, w, tab_h);
  cairo_clip(cr);
  draw_tab_bar(app, cr, w, tab_h);
  cairo_restore(cr);

  // Status bar
  draw_status_bar(app, cr, w, h, status_h);

  // Directory/file picker bar
  if (app.select_dir_mode || app.select_file_mode)
    draw_select_dir_bar(app, cr, w, h, selector_h);

  // Dialogs
  if (app.create_dialog_open) draw_create_dialog(app, cr);
  if (app.rename_ui_open) draw_rename_ui(app, cr);
  if (app.batch_rename_open) draw_batch_rename(app, cr);
  if (app.confirm_open) draw_confirm_dialog(app, cr);
  if (app.compress_dialog_open) draw_compress_dialog(app, cr);
  if (app.term_chooser_open) draw_terminal_chooser(app, cr);
  if (app.properties.open) draw_properties_dialog(app, cr);
  if (app.open_with_open) draw_open_with(app, cr);
  if (app.settings_open) draw_settings_dialog(app, cr);

  // Sort menu
  if (app.r_sort_menu_open || app.sort_menu_open) {
    app.active_pane = app.r_sort_menu_open ? 1 : 0;
    draw_sort_menu(app, cr);
  }

  // Filter dropdown
  if (app.r_filter_dropdown_section > 0 || app.filter_dropdown_section > 0) {
    app.active_pane = app.r_filter_dropdown_section > 0 ? 1 : 0;
    draw_filter_dropdown(app, cr, app.active_pane ? app.r_filter_dropdown_section : app.filter_dropdown_section);
  }

  // Context menu
  if (app.context_menu_open)
    draw_context_menu(app, cr);

  // Preview popup (skipped when a separate layer‑shell surface is active)
  if (app.preview_active && !app.previewPopupSurface)
    draw_hover_preview(app, cr);

  // Info panel (F11)
  draw_info_panel(app, cr);
}

// ── main draw ────────────────────────────────────────────────────

void draw(AppState& app) {
  if (!app.surface) return;

  // Pick buffer
  int paint_bi = -1;
  for (int i = 0; i < 2; ++i) {
    if (!app.buf[i].busy()) { paint_bi = i; break; }
  }
  if (paint_bi < 0) {
    app.pendingRedraw = true;
    return;
  }

  cairo_t* cr = app.buf[paint_bi].cairo();
  cairo_save(cr);

  // Clear
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  paint(app, cr);

  cairo_restore(cr);

  int w = app.width;
  int h = app.height;

  cairo_surface_flush(app.buf[paint_bi].cairo_surface());

  // Commit
  wl_surface_attach(app.surface, app.buf[paint_bi].wl(), 0, 0);
  wl_surface_damage_buffer(app.surface, 0, 0, w, h);
  app.buf[paint_bi].mark_busy();
  schedule_frame(app);
  wl_surface_commit(app.surface);
  if (app.wl.display()) wl_display_flush(app.wl.display());
}

// ── frame scheduling ─────────────────────────────────────────────

void schedule_frame(AppState& app) {
  if (!app.surface) return;

  bool need_anim =
      std::abs(app.cur_tab().scroll_smooth_current - app.cur_tab().scroll_smooth_target) > 0.5;
  if (!need_anim && app.split_view)
    need_anim =
      std::abs(app.right_pane.scroll_smooth_current - app.right_pane.scroll_smooth_target) > 0.5;

  if (!need_anim && !app.scroll_needs_redraw) return;

  if (app.frame_cb) return;

  static const wl_callback_listener kListener = {
    .done = [](void* data, wl_callback* cb, uint32_t) {
      wl_callback_destroy(cb);
      auto& a = *static_cast<AppState*>(data);
      a.frame_cb = nullptr;
      draw(a);
    }
  };

  app.frame_cb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.frame_cb, &kListener, &app);
}

} // namespace eh::file_browser
