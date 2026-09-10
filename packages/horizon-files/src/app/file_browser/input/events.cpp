#include "../app.hpp"
#include "../features/drag.hpp"
#include "../features/progress.hpp"
#include "../features/recursive_search_worker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "config/shell_config.hpp"
#include "platform/desktop/entries/desktop_xdg_ops.hpp"

namespace fs = std::filesystem;
namespace xdg = eh::shell::desktop::xdg;

namespace eh::file_browser {

// Constants matching draw.cpp for filter dropdown layout
static constexpr int kFilterHdrH = 28;
static constexpr int kFilterItemH = 24;
static constexpr int kFilterPD = 6;
static constexpr int kFilterSep = 4;

// ── properties hit test ──────────────────────────────────────────

int properties_hit_test(AppState& app, int x, int y) {
  const auto& p = app.properties;
  if (!p.open) return -1;
  const int card_w = static_cast<int>(p.w);
  const int card_h = static_cast<int>(p.h);
  const int cx = static_cast<int>(p.x);
  const int cy = static_cast<int>(p.y);

  if (x < cx || x >= cx + card_w || y < cy || y >= cy + card_h)
    return -1;

  // Close X button
  if (x >= p.hit_close[0] && x < p.hit_close[0] + p.hit_close[2] &&
      y >= p.hit_close[1] && y < p.hit_close[1] + p.hit_close[3])
    return -2;

  // Bottom "Close" button
  if (x >= p.hit_close_btn[0] && x < p.hit_close_btn[0] + p.hit_close_btn[2] &&
      y >= p.hit_close_btn[1] && y < p.hit_close_btn[1] + p.hit_close_btn[3])
    return -2;

  // Tab clicks (only if no combo open)
  if (p.combo_open < 0) {
    int num_tabs = 2;
    if (p.image_w > 0 && p.image_h > 0) ++num_tabs;
    if (p.is_media) ++num_tabs;
    for (int t = 0; t < num_tabs; ++t) {
      if (x >= p.hit_tabs[t][0] && x < p.hit_tabs[t][0] + p.hit_tabs[t][2] &&
          y >= p.hit_tabs[t][1] && y < p.hit_tabs[t][1] + p.hit_tabs[t][3])
        return -10 - t; // -10=Basic, -11=Permissions, -12=Image, -13=Media
    }
  }

  // If a combo is open, check dropdown items first (they take priority)
  if (p.combo_open >= 0 && p.combo_open < 3) {
    int pi = p.combo_open;
    for (int ci = 0; ci < 4; ++ci) {
      if (x >= p.hit_combo_items[pi][ci][0] && x < p.hit_combo_items[pi][ci][0] + p.hit_combo_items[pi][ci][2] &&
          y >= p.hit_combo_items[pi][ci][1] && y < p.hit_combo_items[pi][ci][1] + p.hit_combo_items[pi][ci][3])
        return 200 + pi * 4 + ci;
    }
  }

  // Combo boxes
  for (int pi = 0; pi < 3; ++pi) {
    if (x >= p.hit_combo[pi][0] && x < p.hit_combo[pi][0] + p.hit_combo[pi][2] &&
        y >= p.hit_combo[pi][1] && y < p.hit_combo[pi][1] + p.hit_combo[pi][3])
      return 10 + pi; // 10=owner, 11=group, 12=other
  }

  // Executable toggle
  if (!p.is_dir && p.hit_exec_toggle[2] > 0) {
    if (x >= p.hit_exec_toggle[0] && x < p.hit_exec_toggle[0] + p.hit_exec_toggle[2] &&
        y >= p.hit_exec_toggle[1] && y < p.hit_exec_toggle[1] + p.hit_exec_toggle[3])
      return 15;
  }

  return 0; // inside dialog but no specific widget
}

// ── settings hit test ────────────────────────────────────────────

int settings_hit_test(AppState& app, int x, int y) {
  const int card_w = 420;
  const int card_h = 412;
  const int cx = (app.width - card_w) / 2;
  const int cy = (app.height - card_h) / 2;
  const int pad = 20;

  if (x < cx || x >= cx + card_w || y < cy || y >= cy + card_h)
    return -1;

  {
    const int close_x = cx + card_w - pad - 24;
    const int close_y = cy + 8;
    if (x >= close_x && x < close_x + 24 && y >= close_y && y < close_y + 24)
      return -2;
  }

  const int top_bar_h = 44;
  const int tab_y = cy + top_bar_h + 4;
  const int tab_w = 200;
  const int tab_h = 36;

  {
    const int tx = cx + pad;
    if (x >= tx && x < tx + tab_w && y >= tab_y && y < tab_y + tab_h)
      return -3;
  }
  {
    const int tx = cx + pad + tab_w;
    if (x >= tx && x < tx + tab_w && y >= tab_y && y < tab_y + tab_h)
      return -4;
  }

  const int content_y = tab_y + tab_h + 12;
  const int btn_y = cy + card_h - 50;
  const int btn_h = 30;
  const int btn_w = 80;
  const int btn_gap = 10;

  {
    const int ok_x = cx + card_w - pad - btn_w * 3 - btn_gap * 2;
    if (x >= ok_x && x < ok_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      return -5;
  }
  {
    const int apply_x = cx + card_w - pad - btn_w;
    if (x >= apply_x && x < apply_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      return -6;
  }
  {
    const int cancel_x = cx + card_w - pad - btn_w * 2 - btn_gap;
    if (x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      return -7;
  }

  const int left_x = cx + 28;
  const int ly = content_y;
  const int z_btn_y = ly - 4;
  const int z_btn_s = 28;

  {
    const int zoom_minus_x = left_x + 220;
    if (x >= zoom_minus_x && x < zoom_minus_x + z_btn_s &&
        y >= z_btn_y && y < z_btn_y + z_btn_s)
      return -8;
  }
  {
    const int zoom_plus_x = left_x + 220 + z_btn_s + 6;
    if (x >= zoom_plus_x && x < zoom_plus_x + z_btn_s &&
        y >= z_btn_y && y < z_btn_y + z_btn_s)
      return -9;
  }

  // Click on zoom value text → inline edit
  {
    const int zoom_val_x = left_x + 180;
    const int zoom_val_y = ly - 2;
    const int zoom_val_w = 36;
    const int zoom_val_h = 22;
    if (x >= zoom_val_x && x < zoom_val_x + zoom_val_w &&
        y >= zoom_val_y && y < zoom_val_y + zoom_val_h) {
      if (!app.settings_zoom_editing) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", app.settings_zoom_pct);
        app.settings_zoom_buf = buf;
        app.settings_zoom_editing = true;
      }
      return -16;
    }
  }

  {
    const int toggle_x = left_x + 220;
    const int toggle_y = content_y + 40 - 2;
    const int toggle_w = 40;
    const int toggle_h = 22;
    if (x >= toggle_x && x < toggle_x + toggle_w &&
        y >= toggle_y && y < toggle_y + toggle_h)
      return -10;
  }

  {
    const int slider_x = cx + pad + 8;
    const int slider_y = content_y + 24;
    const int slider_w = card_w - 2 * pad - 16;
    const int slider_h = 6;
    if (x >= slider_x && x < slider_x + slider_w &&
        y >= slider_y - 10 && y < slider_y + slider_h + 20)
      return -11;
  }

  // Sidebar opacity slider (Appearance tab, below surface opacity slider)
  if (app.settings_tab == 1) {
    const int sb_slider_y = content_y + 76;
    const int sb_slider_w = card_w - 2 * pad - 16;
    const int sb_slider_h = 6;
    if (x >= cx + pad + 8 && x < cx + pad + 8 + sb_slider_w &&
        y >= sb_slider_y - 10 && y < sb_slider_y + sb_slider_h + 20)
      return -13;
  }

  // Top bar opacity slider (Appearance tab)
  if (app.settings_tab == 1) {
    const int tb_slider_y = content_y + 128;
    const int tb_slider_w = card_w - 2 * pad - 16;
    const int tb_slider_h = 6;
    if (x >= cx + pad + 8 && x < cx + pad + 8 + tb_slider_w &&
        y >= tb_slider_y - 10 && y < tb_slider_y + tb_slider_h + 20)
      return -14;
  }

  // Status bar opacity slider (Appearance tab)
  if (app.settings_tab == 1) {
    const int st_slider_y = content_y + 180;
    const int st_slider_w = card_w - 2 * pad - 16;
    const int st_slider_h = 6;
    if (x >= cx + pad + 8 && x < cx + pad + 8 + st_slider_w &&
        y >= st_slider_y - 10 && y < st_slider_y + st_slider_h + 20)
      return -15;
  }

  // Preview opacity slider (Appearance tab)
  if (app.settings_tab == 1) {
    const int pv_slider_y = content_y + 232;
    const int pv_slider_w = card_w - 2 * pad - 16;
    const int pv_slider_h = 6;
    if (x >= cx + pad + 8 && x < cx + pad + 8 + pv_slider_w &&
        y >= pv_slider_y - 10 && y < pv_slider_y + pv_slider_h + 20)
      return -17;
  }

  {
    const int drop_x = left_x + 130;
    const int drop_y = content_y + 76;
    const int drop_w = 226;
    const int drop_h = 30;

    if (x >= drop_x && x < drop_x + drop_w &&
        y >= drop_y && y < drop_y + drop_h)
      return -12;

    if (app.settings_dropdown_open) {
      const int dd_y = drop_y + drop_h + 2;
      const int dd_entry_h = 28;
      const int total = static_cast<int>(app.settings_term_opts.size());
      int remaining = total - app.settings_dropdown_scroll;
      int visible = std::min(remaining, 6);

      for (int i = 0; i < visible; ++i) {
        int item_y = dd_y + i * dd_entry_h;
        if (y >= item_y && y < item_y + dd_entry_h)
          return i;
      }
    }
  }

  return -1;
}

// ── event handling ───────────────────────────────────────────────

void handle_click(AppState& app, int x, int y, int button) {
  app.pointerX = static_cast<double>(x);
  app.pointerY = static_cast<double>(y);

  // Split pane: determine which pane was clicked
  if (app.split_view) {
    int s_w = app.sidebar_expanded ? app.sidebar_width : 0;
    int content_w = app.width - s_w - (app.info_panel_open ? app.info_panel_width : 0);
    int split = app.split_divider_x;
    if (split <= 0) split = content_w / 2;
    int div_x = s_w + split;
    int div_w = 4;
    if (x >= div_x && x < div_x + div_w) {
      if (button == 0x110) { app.split_divider_dragging = true; app.split_divider_hover = true; }
      return;
    }
    app.active_pane = (x >= div_x + div_w) ? 1 : 0;
  }

  // ── Picker bar buttons (dir or file) ──
  if ((app.select_dir_mode || app.select_file_mode) && button == 0x110) {
    if (y >= app.select_bar_y && y < app.select_bar_y + app.select_bar_h) {
      if (x >= app.select_btn_x && x < app.select_btn_x + app.select_btn_w) {
        if (app.select_file_mode) {
          auto& tab = app.cur_tab();
          if (tab.selected_idx >= 0 && tab.selected_idx < static_cast<int>(tab.visible_entries.size())) {
            auto& fe = tab.entries[tab.visible_entries[tab.selected_idx]];
            app.select_dir_result = fe.path;
            app.running = false;
          }
        } else {
          app.select_dir_result = app.cur_tab().current_path;
          app.running = false;
        }
        return;
      }
      if (x >= app.cancel_btn_x && x < app.cancel_btn_x + app.cancel_btn_w) {
        app.running = false;
        return;
      }
    }
  }

  // ── Info panel tab click (in the left‑click handler) ──
  if (app.info_panel_open) {
    int panel_px = app.width - app.info_panel_width;
    int top_h = app.top_bar_height + app.tab_bar_height;
    int tab_h = static_cast<int>(38 * app.zoom_pct / 100.0);
    if (x >= panel_px && y >= top_h && y < top_h + tab_h) {
      for (int i = 0; i < 3; ++i) {
        if (x >= static_cast<int>(app.info_panel_hit_tabs[i][0]) &&
            x < static_cast<int>(app.info_panel_hit_tabs[i][0] + app.info_panel_hit_tabs[i][2]) &&
            y >= static_cast<int>(app.info_panel_hit_tabs[i][1]) &&
            y < static_cast<int>(app.info_panel_hit_tabs[i][1] + app.info_panel_hit_tabs[i][3])) {
          app.info_panel_tab = i;
          draw(app);
          return;
        }
      }
    }
  }

  // ── Sidebar drag start ──
  if (app.sidebar_expanded && button == 0x110) {
    int edge_x = app.sidebar_width;
    if (x >= edge_x - 4 && x <= edge_x + 4) {
      app.sidebar_dragging = true;
      app.sidebar_drag_start_x = x;
      app.sidebar_drag_start_width = app.sidebar_width;
      return;
    }
  }

  // ── Properties dialog clicks ──
  if (app.properties.open) {
    if (button == 0x110) {
      int hit = properties_hit_test(app, x, y);

      if (hit == -1 || hit == -2) {
        app.properties.open = false;
        draw(app);
        return;
      }

      // Tab switch
      if (hit <= -10 && hit >= -13) {
        int new_tab = -(hit + 10);
        int num_tabs = 2;
        if (app.properties.image_w > 0 && app.properties.image_h > 0) ++num_tabs;
        if (app.properties.is_media) ++num_tabs;
        if (new_tab >= 0 && new_tab < num_tabs) {
          app.properties.tab = new_tab;
          app.properties.combo_open = -1;
          app.properties.scroll_px = 0;
        }
        draw(app);
        return;
      }

      // Combo dropdown toggle
      if (hit >= 10 && hit <= 12) {
        int pi = hit - 10;
        if (app.properties.combo_open == pi)
          app.properties.combo_open = -1;
        else
          app.properties.combo_open = pi;
        draw(app);
        return;
      }

      // Combo item selection
      if (hit >= 200 && hit < 212) {
        int idx = hit - 200;
        int pi = idx / 4;
        int ci = idx % 4;
        int* targets[3] = {&app.properties.perm_owner, &app.properties.perm_group, &app.properties.perm_other};
        *targets[pi] = ci;
        app.properties.combo_open = -1;

        // Compute permission bits from combo values
        auto perm_bits = [](int level) -> mode_t {
          switch (level) {
            case 0: return 0;
            case 1: return S_IRUSR;
            case 2: return S_IRUSR | S_IWUSR;
            case 3: return S_IRUSR | S_IWUSR | S_IXUSR;
            default: return 0;
          }
        };
        mode_t mode = 0;
        mode |= perm_bits(app.properties.perm_owner) * (S_IRUSR | S_IWUSR | S_IXUSR) / (S_IRUSR | S_IWUSR | S_IXUSR);
        // Need per-user-group bit mapping
        mode = 0;
        mode |= (app.properties.perm_owner >= 1 ? S_IRUSR : 0);
        mode |= (app.properties.perm_owner >= 2 ? S_IWUSR : 0);
        mode |= (app.properties.perm_owner >= 3 ? S_IXUSR : 0);
        mode |= (app.properties.perm_group >= 1 ? S_IRGRP : 0);
        mode |= (app.properties.perm_group >= 2 ? S_IWGRP : 0);
        mode |= (app.properties.perm_group >= 3 ? S_IXGRP : 0);
        mode |= (app.properties.perm_other >= 1 ? S_IROTH : 0);
        mode |= (app.properties.perm_other >= 2 ? S_IWOTH : 0);
        mode |= (app.properties.perm_other >= 3 ? S_IXOTH : 0);

        // Preserve non-permission bits (setuid, setgid, sticky, etc.)
        mode |= (app.properties.current_mode & ~(S_IRWXU | S_IRWXG | S_IRWXO));

        chmod(app.properties.path.c_str(), mode);
        app.properties.current_mode = mode;
        draw(app);
        return;
      }

      // Executable toggle
      if (hit == 15) {
        app.properties.executable = !app.properties.executable;

        mode_t mode = app.properties.current_mode;
        if (app.properties.executable) {
          mode |= S_IXUSR | S_IXGRP | S_IXOTH;
        } else {
          mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
        }
        chmod(app.properties.path.c_str(), mode);
        app.properties.current_mode = mode;
        draw(app);
        return;
      }

      // Clicked elsewhere inside dialog — close any open combo
      if (app.properties.combo_open >= 0) {
        app.properties.combo_open = -1;
        draw(app);
        return;
      }
    }
  }

  // ── Settings dialog clicks ──
  if (app.settings_open) {
    if (button == 0x110) {
      int hit = settings_hit_test(app, x, y);

      if (hit != -16) app.settings_zoom_editing = false;

      if (hit == -1 || hit == -2 || hit == -7) {
        app.settings_open = false;
        draw(app);
        return;
      }
      if (hit == -3) {
        app.settings_tab = 0;
        draw(app);
        return;
      }
      if (hit == -4) {
        app.settings_tab = 1;
        draw(app);
        return;
      }
      if (hit == -8) {
        app.settings_zoom_pct = std::clamp(app.settings_zoom_pct - 10.0, 50.0, 200.0);
        app.zoom_pct = app.settings_zoom_pct;
        app.entry_height = std::max(20, static_cast<int>(36.0 * app.zoom_pct / 100.0));
        int icon_sz = static_cast<int>(48.0 * app.zoom_pct / 100.0);
        app.grid_cell_size = std::max(40, icon_sz + static_cast<int>(8.0 * app.zoom_pct / 100.0));
        app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));
        draw(app);
        return;
      }
      if (hit == -9) {
        app.settings_zoom_pct = std::clamp(app.settings_zoom_pct + 10.0, 50.0, 200.0);
        app.zoom_pct = app.settings_zoom_pct;
        app.entry_height = std::max(20, static_cast<int>(36.0 * app.zoom_pct / 100.0));
        int icon_sz = static_cast<int>(48.0 * app.zoom_pct / 100.0);
        app.grid_cell_size = std::max(40, icon_sz + static_cast<int>(8.0 * app.zoom_pct / 100.0));
        app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));
        draw(app);
        return;
      }
      if (hit == -10) {
        app.settings_folders_before_files = !app.settings_folders_before_files;
        draw(app);
        return;
      }
      if (hit == -11) {
        const int slider_x = (app.width - 420) / 2 + 28;
        const int slider_w = 364;
        double pct = static_cast<double>(x - slider_x) / slider_w * 100.0;
        app.settings_opacity_pct = std::clamp(static_cast<int>(pct), 0, 100);
        app.surface_opacity_pct = app.settings_opacity_pct;
        draw(app);
        return;
      }
      if (hit == -12) {
        app.settings_dropdown_open = !app.settings_dropdown_open;
        draw(app);
        return;
      }
      if (hit == -13) {
        const int slider_x = (app.width - 420) / 2 + 28;
        const int slider_w = 364;
        double pct = static_cast<double>(x - slider_x) / slider_w * 100.0;
        app.settings_sidebar_opacity_pct = std::clamp(static_cast<int>(pct), 0, 100);
        app.sidebar_opacity_pct = app.settings_sidebar_opacity_pct;
        draw(app);
        return;
      }
      if (hit == -14) {
        const int slider_x = (app.width - 420) / 2 + 28;
        const int slider_w = 364;
        double pct = static_cast<double>(x - slider_x) / slider_w * 100.0;
        app.settings_topbar_opacity_pct = std::clamp(static_cast<int>(pct), 0, 100);
        app.topbar_opacity_pct = app.settings_topbar_opacity_pct;
        draw(app);
        return;
      }
      if (hit == -15) {
        const int slider_x = (app.width - 420) / 2 + 28;
        const int slider_w = 364;
        double pct = static_cast<double>(x - slider_x) / slider_w * 100.0;
        app.settings_statusbar_opacity_pct = std::clamp(static_cast<int>(pct), 0, 100);
        app.statusbar_opacity_pct = app.settings_statusbar_opacity_pct;
        draw(app);
        return;
      }
      if (hit == -17) {
        const int slider_x = (app.width - 420) / 2 + 28;
        const int slider_w = 364;
        double pct = static_cast<double>(x - slider_x) / slider_w * 100.0;
        app.settings_preview_opacity_pct = std::clamp(static_cast<int>(pct), 0, 100);
        app.preview_opacity_pct = app.settings_preview_opacity_pct;
        draw(app);
        return;
      }
      if (hit >= 0) {
        app.settings_default_term_idx = hit + app.settings_dropdown_scroll;
        app.settings_dropdown_open = false;
        draw(app);
        return;
      }
      if (hit == -5) {
        settings_apply(app);
        app.settings_open = false;
        draw(app);
        return;
      }
      if (hit == -6) {
        settings_apply(app);
        draw(app);
        return;
      }
    }
    return;
  }

  // ── Confirm dialog clicks ──
  if (app.confirm_open) {
    int dlg_w = 380;
    int dlg_h = 170;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int cancel_x = dlg_x + dlg_w - 220;
    int delete_x = dlg_x + dlg_w - 110;
    int btn_y = dlg_y + dlg_h - 50;
    int btn_h = 32;
    int btn_w = 90;

    if (button == 0x110 && x >= delete_x && x < delete_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) {
      app.confirm_hover_btn = -1;
      if (app.confirm_callback) app.confirm_callback(true);
      app.confirm_open = false;
      draw(app);
      return;
    }

    if (button == 0x110 && ((x >= cancel_x && x < cancel_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) ||
        (x < dlg_x || x > dlg_x + dlg_w ||
         y < dlg_y || y > dlg_y + dlg_h))) {
      app.confirm_hover_btn = -1;
      if (app.confirm_callback) app.confirm_callback(false);
      app.confirm_open = false;
      draw(app);
      return;
    }
    return;
  }

  // ── Compress dialog clicks ──
  if (app.compress_dialog_open) {
    int dlg_w = 420;
    int dlg_h = 310;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int content_x = dlg_x + 20;
    int content_y = dlg_y + 50;
    int fmt_w = 80;
    int fmt_h = 28;
    int fmt_gap = 8;
    int fmy = content_y + 18;

    if (button == 0x110) {
      // Format buttons
      for (int i = 0; i < 4; ++i) {
        int fmx = content_x + i * (fmt_w + fmt_gap);
        if (x >= fmx && x < fmx + fmt_w && y >= fmy && y < fmy + fmt_h) {
          if (app.compress_format_available[i]) {
            app.compress_format = i;
            draw(app);
          }
          return;
        }
      }
      int fmy2 = fmy + fmt_h + fmt_gap;
      for (int i = 4; i < 7; ++i) {
        int fmx = content_x + (i - 4) * (fmt_w + fmt_gap);
        if (x >= fmx && x < fmx + fmt_w && y >= fmy2 && y < fmy2 + fmt_h) {
          if (app.compress_format_available[i]) {
            app.compress_format = i;
            draw(app);
          }
          return;
        }
      }

      // Level buttons
      int name_y = fmy2 + fmt_h + 14;
      int input_y = name_y + 18;
      int input_h = 32;
      int lvl_y = input_y + input_h + 14;
      int lvl_btn_y = lvl_y + 18;
      int lvl_btn_w = 68;
      int lvl_btn_h = 28;
      int lvl_gap = 8;
      static constexpr int kLevelValues[5] = {0, 3, 6, 8, 9};
      for (int i = 0; i < 5; ++i) {
        int lx = content_x + i * (lvl_btn_w + lvl_gap);
        if (x >= lx && x < lx + lvl_btn_w && y >= lvl_btn_y && y < lvl_btn_y + lvl_btn_h) {
          app.compress_level = kLevelValues[i];
          draw(app);
          return;
        }
      }

      // Bottom buttons
      int btn_y = dlg_y + dlg_h - 50;
      int btn_w = 90;
      int btn_h = 32;
      int cancel_x = dlg_x + dlg_w - 220;
      int compress_x = dlg_x + dlg_w - 110;

      if (x >= compress_x && x < compress_x + btn_w &&
          y >= btn_y && y < btn_y + btn_h) {
        app.compress_hover_btn = -1;
        execute_compress_async(app);
        return;
      }

      if ((x >= cancel_x && x < cancel_x + btn_w &&
           y >= btn_y && y < btn_y + btn_h) ||
          (x < dlg_x || x > dlg_x + dlg_w ||
           y < dlg_y || y > dlg_y + dlg_h)) {
        app.compress_dialog_open = false;
        draw(app);
        return;
      }
    }
    return;
  }

  // ── Create dialog clicks ──
  if (app.create_dialog_open) {
    int dlg_w = 340;
    int dlg_h = 160;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;

    int cancel_x = dlg_x + dlg_w - 220;
    int create_x = dlg_x + dlg_w - 110;
    int btn_y = dlg_y + dlg_h - 50;
    int btn_h = 32;
    int btn_w = 90;

    if (button == 0x110 && x >= create_x && x < create_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) {
      if (!app.create_buf.empty()) {
        fs::path new_path = fs::path(app.cur_tab().current_path) / app.create_buf;
        std::error_code ec;
        if (app.create_is_folder)
          fs::create_directory(new_path, ec);
        else {
          FILE* f = std::fopen(new_path.c_str(), "w");
          if (f) std::fclose(f);
        }
        reload_dir(app);
      }
      app.create_dialog_open = false;
      draw(app);
      return;
    }

    if (button == 0x110 && x >= cancel_x && x < cancel_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) {
      app.create_dialog_open = false;
      draw(app);
      return;
    }

    if (button == 0x110 && (x < dlg_x || x > dlg_x + dlg_w ||
        y < dlg_y || y > dlg_y + dlg_h)) {
      app.create_dialog_open = false;
      draw(app);
      return;
    }
    return;
  }

  // ── Rename UI dialog clicks ──
  if (app.rename_ui_open) {
    int dlg_w = 400;
    int dlg_h = 190;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;

    int cancel_x = dlg_x + dlg_w - 230;
    int rename_x = dlg_x + dlg_w - 120;
    int btn_y = dlg_y + dlg_h - 52;
    int btn_h = 34;
    int btn_w = 90;

    if (button == 0x110 && x >= rename_x && x < rename_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) {
      if (!app.rename_ui_buf.empty() && app.rename_ui_buf != app.rename_ui_old_name) {
        fs::path src(app.rename_ui_entry_path);
        fs::path dest = src.parent_path() / app.rename_ui_buf;
        std::error_code ec;
        fs::rename(src, dest, ec);
        if (!ec) {
          AppState::UndoRecord rec{AppState::UndoRecord::Type::Rename, {}, {}};
          rec.paths_a.push_back(src.string());
          rec.paths_b.push_back(dest.string());
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
          app.operation_status = "Renamed";
          app.operation_status_expires_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::steady_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch()).count();
          reload_dir(app);
        }
      }
      app.rename_ui_open = false;
      draw(app);
      return;
    }

    if (button == 0x110 && x >= cancel_x && x < cancel_x + btn_w &&
        y >= btn_y && y < btn_y + btn_h) {
      app.rename_ui_open = false;
      draw(app);
      return;
    }

    if (button == 0x110 && (x < dlg_x || x > dlg_x + dlg_w ||
        y < dlg_y || y > dlg_y + dlg_h)) {
      app.rename_ui_open = false;
      draw(app);
      return;
    }
    return;
  }

  // ── Batch rename click handling ──
  if (app.batch_rename_open) {
    int n = static_cast<int>(app.batch_rename_entries.size());
    bool is_template = (app.batch_rename_mode == 0);

    int dlg_w = 540;
    int list_h = std::min(n * 28 + 4, 280) + 4;
    int input_area_h = is_template ? 70 : 80;
    int dlg_h = 24 + 28 + input_area_h + list_h + 56;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int cx = dlg_x + 20;

    int tab_y = dlg_y + 42;
    int tab_h = 26;
    int tab_w = 210;
    int input_y = tab_y + tab_h + 10;
    int field_h = 30;
    int btn_y = dlg_y + dlg_h - 44;
    int btn_w = 90;
    int btn_h = 32;
    int cancel_x = dlg_x + dlg_w - 230;
    int rename_x = dlg_x + dlg_w - 120;

    if (button == 0x110) {
      // ── Mode tab clicks ──
      if (y >= tab_y && y < tab_y + tab_h) {
        if (x >= cx && x < cx + tab_w) {
          if (app.batch_rename_mode != 0) {
            app.batch_rename_mode = 0;
            app.batch_rename_edit_focus = 0;
            app.batch_rename_show_add = false;
            draw(app);
          }
          return;
        }
        if (x >= cx + tab_w + 8 && x < cx + tab_w + 8 + tab_w) {
          if (app.batch_rename_mode != 1) {
            app.batch_rename_mode = 1;
            app.batch_rename_edit_focus = 0;
            draw(app);
          }
          return;
        }
      }

      if (is_template) {
        // ── Template field click ──
        int tf_x = cx;
        int tf_y = input_y;
        int tf_w = 360;
        if (x >= tf_x && x < tf_x + tf_w && y >= tf_y && y < tf_y + field_h) {
          app.batch_rename_edit_focus = 0;
          app.batch_rename_show_add = false;
          // Position cursor based on click
          app.batch_rename_template_cursor = static_cast<int>(app.batch_rename_template.size());
          draw(app);
          return;
        }

        // ── [+ Add] button click ──
        int add_x = tf_x + tf_w + 8;
        int add_w = 70;
        if (x >= add_x && x < add_x + add_w && y >= tf_y && y < tf_y + field_h) {
          app.batch_rename_show_add = !app.batch_rename_show_add;
          app.batch_rename_add_hover = -1;
          draw(app);
          return;
        }

        // ── [+ Add] dropdown option click ──
        if (app.batch_rename_show_add) {
          int dd_x = add_x;
          int dd_y = tf_y + field_h + 2;
          int dd_w = add_w;
          int dd_item_h = 26;
          int dd_h = 3 * dd_item_h + 4;
          if (x >= dd_x && x < dd_x + dd_w && y >= dd_y && y < dd_y + dd_h) {
            int option = (y - dd_y - 2) / dd_item_h;
            if (option >= 0 && option <= 2) {
              const char* inserts[] = {"[1]", "[01]", "[001]"};
              app.batch_rename_template.insert(app.batch_rename_template_cursor, inserts[option]);
              app.batch_rename_template_cursor += static_cast<int>(std::strlen(inserts[option]));
              app.batch_rename_show_add = false;
              draw(app);
            }
            return;
          }
          // Click outside dropdown closes it
          if (!(x >= dd_x && x < dd_x + dd_w && y >= dd_y && y < dd_y + dd_h)) {
            app.batch_rename_show_add = false;
            draw(app);
          }
        }
      } else {
        // ── Find mode field clicks ──
        int label_w = 100;
        int fld_x = cx + label_w;
        int fld_w = 240;

        // Find field
        if (x >= fld_x && x < fld_x + fld_w && y >= input_y && y < input_y + field_h) {
          app.batch_rename_edit_focus = 0;
          app.batch_rename_find_cursor = static_cast<int>(app.batch_rename_find.size());
          draw(app);
          return;
        }

        // Replace field
        int rl_y = input_y + field_h + 6;
        if (x >= fld_x && x < fld_x + fld_w && y >= rl_y && y < rl_y + field_h) {
          app.batch_rename_edit_focus = 1;
          app.batch_rename_replace_cursor = static_cast<int>(app.batch_rename_replace.size());
          draw(app);
          return;
        }
      }

      // ── Rename button ──
      if (x >= rename_x && x < rename_x + btn_w && y >= btn_y && y < btn_y + btn_h) {
        if (!app.batch_rename_entries.empty()) {
          AppState::UndoRecord rec{AppState::UndoRecord::Type::Rename, {}, {}};
          std::error_code ec;
          int renamed = 0;
          for (const auto& e : app.batch_rename_entries) {
            if (e.new_name.empty() || e.new_name == e.old_name) continue;
            fs::path src(e.old_path);
            fs::path dest = src.parent_path() / e.new_name;
            fs::rename(src, dest, ec);
            if (!ec) {
              rec.paths_a.push_back(e.old_path);
              rec.paths_b.push_back(dest.string());
              ++renamed;
            }
          }
          if (!rec.paths_a.empty()) {
            app.redo_stack.clear();
            app.undo_stack.push_back(std::move(rec));
            if (app.undo_stack.size() > app.kMaxUndo)
              app.undo_stack.erase(app.undo_stack.begin());
            app.operation_status = std::to_string(renamed) + " file" + (renamed == 1 ? "" : "s") + " renamed";
            app.operation_status_expires_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              (std::chrono::steady_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch()).count();
            reload_dir(app);
          }
        }
        app.batch_rename_open = false;
        draw(app);
        return;
      }

      // ── Cancel button or click outside ──
      if ((x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h) ||
          (x < dlg_x || x > dlg_x + dlg_w || y < dlg_y || y > dlg_y + dlg_h)) {
        app.batch_rename_open = false;
        draw(app);
        return;
      }
    }
    return;
  }

  // ── Open With dialog clicks ──
  if (app.open_with_open) {
    if (button == 0x110) {
      double dx = static_cast<double>(x), dy = static_cast<double>(y);

      // Outside card → close
      if (dx < app.open_with_x || dx > app.open_with_x + app.open_with_w ||
          dy < app.open_with_y || dy > app.open_with_y + app.open_with_h) {
        open_with_close(app);
        draw(app);
        return;
      }

      // Close button
      if (dx >= app.open_with_hit_close[0] && dx < app.open_with_hit_close[0] + app.open_with_hit_close[2] &&
          dy >= app.open_with_hit_close[1] && dy < app.open_with_hit_close[1] + app.open_with_hit_close[3]) {
        open_with_close(app);
        draw(app);
        return;
      }

      // Cancel button
      if (dx >= app.open_with_hit_cancel[0] && dx < app.open_with_hit_cancel[0] + app.open_with_hit_cancel[2] &&
          dy >= app.open_with_hit_cancel[1] && dy < app.open_with_hit_cancel[1] + app.open_with_hit_cancel[3]) {
        open_with_close(app);
        draw(app);
        return;
      }

      auto launch = [&](const AppState::OpenWithEntry& e) {
        std::string desktop = e.desktop_path;
        std::string file = app.open_with_file_path;
        for (size_t p = 0; (p = desktop.find('\'', p)) != std::string::npos; p += 4)
          desktop.replace(p, 1, "'\\''");
        for (size_t p = 0; (p = file.find('\'', p)) != std::string::npos; p += 4)
          file.replace(p, 1, "'\\''");
        std::string cmd = "gio launch '" + desktop + "' '" + file + "' &";
        (void)std::system(cmd.c_str());
      };

      // Open button
      if (dx >= app.open_with_hit_open[0] && dx < app.open_with_hit_open[0] + app.open_with_hit_open[2] &&
          dy >= app.open_with_hit_open[1] && dy < app.open_with_hit_open[1] + app.open_with_hit_open[3]) {
        if (app.open_with_selected >= 0 &&
            app.open_with_selected < static_cast<int>(app.open_with_apps.size())) {
          launch(app.open_with_apps[app.open_with_selected]);
        }
        open_with_close(app);
        draw(app);
        return;
      }

      // App list
      int pad = 16, pad_in = 12, top_bar_h = 44, entry_h = 40;
      int total = static_cast<int>(app.open_with_apps.size());
      int max_list_h = 320;
      int visible = std::max(1, std::min(total, max_list_h / entry_h));
      int list_h = visible * entry_h;
      int list_x = static_cast<int>(app.open_with_x) + pad_in;
      int list_y = static_cast<int>(app.open_with_y) + pad + top_bar_h + pad_in;
      int list_w = static_cast<int>(app.open_with_w) - 2 * pad_in;

      if (dx >= list_x && dx < list_x + list_w &&
          dy >= list_y && dy < list_y + list_h) {
        int idx = app.open_with_scroll + static_cast<int>((dy - list_y) / entry_h);
        if (idx >= 0 && idx < total) {
          app.open_with_selected = idx;
          // Launch immediately on single click
          launch(app.open_with_apps[idx]);
          open_with_close(app);
          draw(app);
          return;
        }
      }
    }
    return;
  }

  // ── Terminal chooser clicks ──
  if (app.term_chooser_open) {
    const int kPad = 20, kTopBarH = 44, kEntryH = 40, kBottomBarH = 52;
    const int kMaxListH = 300;
    const int total = static_cast<int>(app.term_chooser_apps.size());
    const int max_visible = std::max(1, kMaxListH / kEntryH);
    const int visible = std::min(total, max_visible);
    const int list_h = visible * kEntryH;
    const int card_w = app.term_chooser_w;
    const int card_h = kPad + kTopBarH + 8 + list_h + 8 + kBottomBarH + kPad;
    const int card_x = app.term_chooser_x;
    const int card_y = (app.height - card_h) / 2;

    const int close_x = card_x + card_w - kPad - 28;
    const int close_y = card_y + kPad - 4;
    const int list_x = card_x + 12;
    const int list_y = card_y + kPad + kTopBarH + 8;

    if (button == 0x110) {
      if (x < card_x || x > card_x + card_w || y < card_y || y > card_y + card_h) {
        app.term_chooser_open = false;
        draw(app);
        return;
      }
      if (x >= close_x && x < close_x + 28 && y >= close_y && y < close_y + 28) {
        app.term_chooser_open = false;
        draw(app);
        return;
      }
      if (x >= list_x && x < list_x + card_w - 24 && y >= list_y && y < list_y + list_h) {
        int rel_y = y - list_y + app.term_chooser_scroll * kEntryH;
        int item_idx = rel_y / kEntryH;
        if (item_idx >= 0 && item_idx < total) {
          auto& chosen = app.term_chooser_apps[item_idx];
          std::string chosen_id = chosen.desktop_id;
          auto dot = chosen_id.rfind('.');
          if (dot != std::string::npos) chosen_id = chosen_id.substr(0, dot);
          auto slash = chosen_id.rfind('/');
          if (slash != std::string::npos) chosen_id = chosen_id.substr(slash + 1);
          eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
          sc.defaultApps.terminal = chosen_id;
          (void)eh::config::write_state_settings_toml(sc);
          eh::config::shell_config_apply_from_memory(std::move(sc));
          app.term_chooser_open = false;
          open_terminal_at(app, app.term_chooser_target_dir);
          draw(app);
          return;
        }
      }
    }
    return;
  }

  // Left click
  if (button == 0x110) {
    if (app.context_menu_open) {
      int cm_idx = hit_test_context_menu(app, x, y);
      if (cm_idx >= 0) {
        // Main menu item - if it has a submenu, just keep hover; otherwise execute
        if (static_cast<size_t>(cm_idx) < app.context_menu_items.size() &&
            !app.context_menu_items[cm_idx].sub_items.empty()) {
          app.context_menu_hover = cm_idx;
          draw(app);
          return;
        }
        execute_context_menu_action(app, cm_idx);
      } else if (cm_idx < -9) {
        // Submenu item: decode and execute
        int sub_idx = -(cm_idx + 10);
        int saved_parent = app.context_menu_hover_prev;
        app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
        if (saved_parent >= 0 && static_cast<size_t>(saved_parent) < app.context_menu_items.size()) {
          auto& item = app.context_menu_items[saved_parent];
          if (sub_idx >= 0 && static_cast<size_t>(sub_idx) < item.sub_items.size()) {
            auto action = item.sub_items[sub_idx].action;
            if (action != AppState::ContextMenuAction::Separator) {
              // Clone the menu items to set file_idx for execute
              app.context_menu_open = false;
              // Rebuild a single-item context menu to reuse execute_context_menu_action
              auto saved_items = std::move(app.context_menu_items);
              app.context_menu_items = {item.sub_items[sub_idx]};
              execute_context_menu_action(app, 0);
              draw(app);
              if (!app.context_menu_open) {
                // Action handled, restore nothing
              } else {
                app.context_menu_items = std::move(saved_items);
                app.context_menu_hover_prev = saved_parent;
              }
              return;
            }
          }
        }
        app.context_menu_open = false;
      } else {
        app.context_menu_open = false;
      }
      draw(app);
      return;
    }

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    auto now_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                  static_cast<uint64_t>(ts.tv_nsec);

    // ── Sort menu item click (handle before top bar, so menu stays on top) ──
    if ((app.active_pane ? app.r_sort_menu_open : app.sort_menu_open)) {
      if (x >= (app.active_pane ? app.r_sort_menu_x : app.sort_menu_x) && x < (app.active_pane ? app.r_sort_menu_x : app.sort_menu_x) + (app.active_pane ? app.r_sort_menu_w : app.sort_menu_w) &&
          y >= (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) && y < (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) + (app.active_pane ? app.r_sort_menu_h : app.sort_menu_h)) {
        int rel_y = y - (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) - 6;
        int idx = rel_y / 30;
        if (idx >= 0 && idx <= 3) {
          app.cur_tab().sort_field = static_cast<SortField>(idx);
          (app.active_pane ? app.r_sort_menu_open : app.sort_menu_open) = false;
          save_file_browser_settings(app);
          reload_dir(app);
          draw(app);
          return;
        }
      } else {
        (app.active_pane ? app.r_sort_menu_open : app.sort_menu_open) = false;
        draw(app);
        return;
      }
    }

    // ── Filter dropdown click (handled before top bar) ──
    auto& click_filter_dd_x = app.active_pane ? app.r_filter_dropdown_x : app.filter_dropdown_x;
    auto& click_filter_dd_y = app.active_pane ? app.r_filter_dropdown_y : app.filter_dropdown_y;
    auto& click_filter_dd_w = app.active_pane ? app.r_filter_dropdown_w : app.filter_dropdown_w;
    auto& click_filter_dd_h = app.active_pane ? app.r_filter_dropdown_h : app.filter_dropdown_h;
    auto& click_filter_section = app.active_pane ? app.r_filter_dropdown_section : app.filter_dropdown_section;
    auto& click_filter_hover = app.active_pane ? app.r_filter_dropdown_hover : app.filter_dropdown_hover;
    auto& click_filter_type = app.active_pane ? app.r_filter_type_idx : app.filter_type_idx;
    auto& click_filter_size = app.active_pane ? app.r_filter_size_idx : app.filter_size_idx;
    auto& click_filter_date = app.active_pane ? app.r_filter_date_idx : app.filter_date_idx;
    if (click_filter_section > 0) {
      if (x >= click_filter_dd_x && x < click_filter_dd_x + click_filter_dd_w &&
          y >= click_filter_dd_y && y < click_filter_dd_y + click_filter_dd_h) {
        // Map click to global index, then to section+item or header
        int rel_y = y - click_filter_dd_y - kFilterPD;
        int gy = 0;
        int glob = 0;
        int section = click_filter_section;
        int clicked_section = 0, clicked_item = -1;
        for (int si = 1; si <= 3; ++si) {
          // Header
          if (rel_y >= gy && rel_y < gy + kFilterHdrH) {
            clicked_section = si;
            clicked_item = -1; // header click
            break;
          }
          ++glob;
          gy += kFilterHdrH;

          // Items if expanded
          if (section == si) {
            int cnt = (si == 1) ? 13 : (si == 2) ? 7 : 5;
            int item_y = gy;
            for (int i = 0; i < cnt; ++i) {
              if (rel_y >= item_y && rel_y < item_y + kFilterItemH) {
                clicked_section = si;
                clicked_item = i;
                break;
              }
              ++glob;
              item_y += kFilterItemH;
            }
            gy = item_y;
            if (clicked_item >= 0) break;
          }

          gy += kFilterSep;
        }

        if (clicked_item >= 0) {
          // Item clicked — select it and close dropdown
          if (clicked_section == 1) click_filter_type = clicked_item;
          else if (clicked_section == 2) click_filter_size = clicked_item;
          else if (clicked_section == 3) click_filter_date = clicked_item;
          click_filter_section = 0;
          trigger_search_on_filter_change(app);
          draw(app);
          return;
        } else if (clicked_section > 0) {
          // Header clicked — toggle expansion
          click_filter_section = (click_filter_section == clicked_section) ? 0 : clicked_section;
          click_filter_hover = -1;
          draw(app);
          return;
        }
      } else {
        click_filter_section = 0;
        draw(app);
        return;
      }
    }

    int bar_y = y;
    if (app.split_view) {
      int content_y = app.top_bar_height + app.tab_bar_height;
      if (y >= content_y && y < content_y + app.top_bar_height)
        bar_y = y - content_y;
    }
    if (bar_y < app.top_bar_height) {
      app.last_click_ns = 0;
      double zf = app.zoom_pct / 100.0;

      auto& in_search_btn_x = app.active_pane ? app.r_search_btn_x : app.search_btn_x;
      auto& in_search_btn_w = app.active_pane ? app.r_search_btn_w : app.search_btn_w;
      auto& in_folder_search_btn_x = app.active_pane ? app.r_folder_search_btn_x : app.folder_search_btn_x;
      auto& in_folder_search_btn_w = app.active_pane ? app.r_folder_search_btn_w : app.folder_search_btn_w;
      auto& in_view_btn_x = app.active_pane ? app.r_view_btn_x : app.view_btn_x;
      auto& in_view_btn_w = app.active_pane ? app.r_view_btn_w : app.view_btn_w;
      auto& in_sort_btn_x = app.active_pane ? app.r_sort_btn_x : app.sort_btn_x;
      auto& in_sort_btn_w = app.active_pane ? app.r_sort_btn_w : app.sort_btn_w;
      auto& in_dots_btn_x = app.active_pane ? app.r_dots_btn_x : app.dots_btn_x;
      auto& in_dots_btn_y = app.active_pane ? app.r_dots_btn_y : app.dots_btn_y;
      auto& in_dots_btn_w = app.active_pane ? app.r_dots_btn_w : app.dots_btn_w;
      auto& in_dots_btn_h = app.active_pane ? app.r_dots_btn_h : app.dots_btn_h;
      auto& in_arrow_back_x = app.active_pane ? app.r_arrow_back_x : app.arrow_back_x;
      auto& in_arrow_forward_x = app.active_pane ? app.r_arrow_forward_x : app.arrow_forward_x;
      auto& in_search_bar_x = app.active_pane ? app.r_search_bar_x : app.search_bar_x;
      auto& in_search_bar_w = app.active_pane ? app.r_search_bar_w : app.search_bar_w;
      auto& in_search_clear_x = app.active_pane ? app.r_search_clear_x : app.search_clear_x;
      auto& in_search_clear_w = app.active_pane ? app.r_search_clear_w : app.search_clear_w;
      auto& in_filter_btn_x = app.active_pane ? app.r_filter_btn_x : app.filter_btn_x;
      auto& in_filter_btn_w = app.active_pane ? app.r_filter_btn_w : app.filter_btn_w;
      auto& in_breadcrumbs = app.active_pane ? app.r_breadcrumbs : app.breadcrumbs;
      auto& in_breadcrumb_hover = app.active_pane ? app.r_breadcrumb_hover : app.breadcrumb_hover;
      auto& in_search_active = app.active_pane ? app.r_search_active : app.search_active;
      auto& in_recursive_search_active = app.active_pane ? app.r_recursive_search_active : app.recursive_search_active;
      auto& in_search_query = app.active_pane ? app.r_search_query : app.search_query;
      auto& in_recursive_search_query = app.active_pane ? app.r_recursive_search_query : app.recursive_search_query;
      auto& in_search_cursor = app.active_pane ? app.r_search_cursor : app.search_cursor;
      auto& in_search_sel_start = app.active_pane ? app.r_search_sel_start : app.search_sel_start;
      auto& in_search_sel_end = app.active_pane ? app.r_search_sel_end : app.search_sel_end;

      // Window control buttons (traffic lights on the right: max | min | close)
      if (app.win_btn_x > 0) {
        int rel = x - app.win_btn_x;
        if (rel >= 0 && rel < 3 * app.win_btn_w) {
          int slot = rel / app.win_btn_w;
          if (slot == 0) {
            // Maximize (green)
            if (app.toplevel) {
              xdg_toplevel_set_maximized(app.toplevel);
              wl_display_flush(app.wl.display());
            }
          } else if (slot == 1) {
            // Minimize (yellow)
            if (app.toplevel) {
              xdg_toplevel_set_minimized(app.toplevel);
              wl_display_flush(app.wl.display());
            }
          } else {
            // Close (red)
            app.running = false;
          }
          return;
        }
      }

      // Settings gear button
      {
        int gap4 = static_cast<int>(4.0 * zf);
        int gear_w = static_cast<int>(36.0 * zf);
        int gear_x = in_sort_btn_x + in_sort_btn_w + gap4;
        if (x >= gear_x && x < gear_x + gear_w) {
          open_settings(app);
          draw(app);
          return;
        }
      }

      // Path bar dots menu button
      if (in_dots_btn_w > 0 &&
          x >= in_dots_btn_x && x < in_dots_btn_x + in_dots_btn_w &&
          bar_y >= in_dots_btn_y && bar_y < in_dots_btn_y + in_dots_btn_h) {
        app.context_menu_open = true;
        app.context_menu_x = in_dots_btn_x;
        app.context_menu_y = in_dots_btn_y + in_dots_btn_h;
        app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
        app.context_menu_file_idx = -5;
        app.context_menu_items = {
          AppState::menu_item(AppState::ContextMenuAction::NewFolder, "New Folder"),
          AppState::menu_item(AppState::ContextMenuAction::NewDocument, "New Document"),
          AppState::menu_item(AppState::ContextMenuAction::OpenWith, "Open With\u2026"),
          AppState::menu_item(AppState::ContextMenuAction::Separator, ""),
          AppState::menu_item(AppState::ContextMenuAction::Reload, "Reload"),
          AppState::menu_item(AppState::ContextMenuAction::CopyLocation, "Copy Location"),
          AppState::menu_item(AppState::ContextMenuAction::Separator, ""),
          AppState::menu_item(AppState::ContextMenuAction::Paste, "Paste"),
          AppState::menu_item(AppState::ContextMenuAction::SelectAll, "Select All"),
          AppState::menu_item(AppState::ContextMenuAction::Separator, ""),
          AppState::menu_item(AppState::ContextMenuAction::OpenInTerminal, "Open in Terminal"),
          AppState::menu_item(AppState::ContextMenuAction::Separator, ""),
          AppState::menu_item(AppState::ContextMenuAction::Properties, "Properties"),
        };
        draw(app);
        return;
      }

      // Navigation arrows (back, forward only)
      int bx = (app.sidebar_expanded ? app.sidebar_width : 0) + static_cast<int>(20.0 * zf);
      int btn_w = static_cast<int>(36.0 * zf);
      int gap4 = static_cast<int>(4.0 * zf);
      if (x >= bx && x < bx + btn_w && !app.cur_tab().nav_history.empty()) {
        navigate_back(app);
        draw(app);
        return;
      }
      bx += btn_w + gap4;
      if (x >= bx && x < bx + btn_w && !app.cur_tab().nav_forward.empty()) {
        navigate_forward(app);
        draw(app);
        return;
      }

      // Folder-search button (folder + magnifying glass) → recursive search in current dir
      if (x >= in_folder_search_btn_x && x < in_folder_search_btn_x + in_folder_search_btn_w) {
      if (in_search_active) {
        reset_search_filters(app);
        in_search_active = false;
        in_recursive_search_active = false;
        in_search_query.clear();
        in_recursive_search_query.clear();
        recursive_search_worker().cancel();
        reload_dir(app);
      } else {
        in_recursive_search_active = false;
        in_search_active = true;
        in_search_query.clear();
        in_recursive_search_query.clear();
        recursive_search_worker().cancel();
        in_search_cursor = 0;
        in_search_sel_start = -1;
        in_search_sel_end = -1;
      }
      (app.active_pane ? app.r_path_editing : app.path_editing) = false;
      draw(app);
      return;
    }
    // Search button (magnifying glass) → recursive home search
      if (x >= in_search_btn_x && x < in_search_btn_x + in_search_btn_w) {
      if (in_recursive_search_active) {
        reset_search_filters(app);
        in_recursive_search_active = false;
        in_search_active = false;
        in_search_query.clear();
        in_recursive_search_query.clear();
        recursive_search_worker().cancel();
        reload_dir(app);
      } else {
        in_search_active = false;
        in_recursive_search_active = true;
          in_search_query.clear();
          in_recursive_search_query.clear();
          recursive_search_worker().cancel();
          in_search_cursor = 0;
          in_search_sel_start = -1;
          in_search_sel_end = -1;
        }
        (app.active_pane ? app.r_path_editing : app.path_editing) = false;
        draw(app);
        return;
      }

      // View-mode toggle (cycles through List → Grid → Compact → Tree → List)
      if (x >= in_view_btn_x && x < in_view_btn_x + in_view_btn_w) {
        auto cur = app.cur_tab().view_mode;
        if (cur == ViewMode::List) app.cur_tab().view_mode = ViewMode::Grid;
        else if (cur == ViewMode::Grid) app.cur_tab().view_mode = ViewMode::Compact;
        else if (cur == ViewMode::Compact) app.cur_tab().view_mode = ViewMode::Tree;
        else app.cur_tab().view_mode = ViewMode::List;
        save_file_browser_settings(app);
        draw(app);
        return;
      }
      if (x >= in_sort_btn_x && x < in_sort_btn_x + in_sort_btn_w) {
        // Close sort menu in both panes, then toggle active pane only
        bool was_open = app.active_pane ? app.r_sort_menu_open : app.sort_menu_open;
        app.r_sort_menu_open = false;
        app.sort_menu_open = false;
        if (!was_open)
          (app.active_pane ? app.r_sort_menu_open : app.sort_menu_open) = true;
        (app.active_pane ? app.r_sort_menu_hover : app.sort_menu_hover) = -1;
        draw(app);
        return;
      }

      // Filter button click (only when search is active)
      if (in_search_active || in_recursive_search_active) {
        if (x >= in_filter_btn_x && x < in_filter_btn_x + in_filter_btn_w) {
          auto& click_filter_section = app.active_pane ? app.r_filter_dropdown_section : app.filter_dropdown_section;
          auto& click_filter_hover = app.active_pane ? app.r_filter_dropdown_hover : app.filter_dropdown_hover;
          click_filter_section = (click_filter_section > 0) ? 0 : 1;
          click_filter_hover = -1;
          draw(app);
          return;
        }
      }

      // Search bar click — set cursor position or clear
      if (in_search_active || in_recursive_search_active) {
        // Clear button hit test
        if (in_search_clear_w > 0 && x >= in_search_clear_x && x < in_search_clear_x + in_search_clear_w) {
          in_search_query.clear();
          in_search_cursor = 0;
          in_search_sel_start = -1;
          in_search_sel_end = -1;
          recursive_search_worker().cancel();
          reload_dir(app);
          draw(app);
          return;
        }
        // Click in search bar area — set cursor position
        if (x >= in_search_bar_x && x < in_search_bar_x + in_search_bar_w) {
          double zf = app.zoom_pct / 100.0;
          int search_icon_size = static_cast<int>(14.0 * zf);
          int text_left = in_search_bar_x + search_icon_size + static_cast<int>(8.0 * zf);
          int click_x = x - text_left;
          int best_pos = static_cast<int>(in_search_query.size());
          cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
          cairo_t* cr_tmp = cairo_create(tmp);
          cairo_select_font_face(cr_tmp, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr_tmp, 13.0 * zf);
          for (int ci = 0; ci <= static_cast<int>(in_search_query.size()); ++ci) {
            std::string sub = in_search_query.substr(0, static_cast<std::size_t>(ci));
            cairo_text_extents_t te;
            cairo_text_extents(cr_tmp, sub.c_str(), &te);
            if (te.width >= click_x) { best_pos = ci; break; }
          }
          cairo_destroy(cr_tmp);
          cairo_surface_destroy(tmp);
          in_search_cursor = best_pos;
          in_search_sel_start = -1;
          in_search_sel_end = -1;
          draw(app);
          return;
        }
      }

      // Path editing click — set cursor position by character hit-test
      if (app.active_pane ? app.r_path_editing : app.path_editing) {
        auto& pe_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
        auto& pe_cursor = app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor;
        auto& pe_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
        auto& pe_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
        auto& pe_dragging = app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging;
        double zf = app.zoom_pct / 100.0;
        int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int arrow_w = static_cast<int>(36.0 * zf);
        int gap4 = static_cast<int>(4.0 * zf);
        int mx6 = static_cast<int>(24.0 * zf);
        int path_pad = static_cast<int>(12.0 * zf);
        int house_w = static_cast<int>(16.0 * zf);
        int gap12 = static_cast<int>(12.0 * zf);
        int nav_origin = sidebar_w + static_cast<int>(20.0 * zf);
        int path_x_inner = nav_origin + 2 * arrow_w + gap4 + mx6 + path_pad + house_w + gap12;
        int path_w_inner = in_search_btn_x - static_cast<int>(4.0 * zf) - path_x_inner;
        int text_x = path_x_inner;
        int field_right = path_x_inner + path_w_inner - static_cast<int>(14.0 * zf);
        if (x >= path_x_inner && x < field_right) {
          cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
          cairo_t* cr_tmp = cairo_create(tmp);
          cairo_select_font_face(cr_tmp, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr_tmp, 13.0 * zf);
          const std::string& buf = pe_buf;
          cairo_text_extents_t full_te;
          cairo_text_extents(cr_tmp, buf.c_str(), &full_te);
          int scroll_offset = 0;
          std::string display = buf;
          if (full_te.width > path_w_inner - static_cast<int>(20.0 * zf)) {
            int keep = static_cast<int>(display.size()) * (path_w_inner - static_cast<int>(20.0 * zf)) /
                      std::max(1, static_cast<int>(full_te.width));
            if (keep > 3 && keep < static_cast<int>(display.size())) {
              int trim = static_cast<int>(display.size()) - keep + 3;
              scroll_offset = trim;
              display = "..." + display.substr(static_cast<std::size_t>(trim));
            }
          }
          int click_x = x - text_x;
          int best_pos = static_cast<int>(buf.size());
          for (int ci = 0; ci <= static_cast<int>(display.size()); ++ci) {
            std::string sub = display.substr(0, static_cast<std::size_t>(ci));
            cairo_text_extents_t te;
            cairo_text_extents(cr_tmp, sub.c_str(), &te);
            if (te.width >= click_x) {
              if (scroll_offset > 0) {
                best_pos = (ci <= 3) ? scroll_offset : scroll_offset + ci - 3;
              } else {
                best_pos = ci;
              }
              break;
            }
          }
          cairo_destroy(cr_tmp);
          cairo_surface_destroy(tmp);
          pe_cursor = best_pos;
          pe_sel_start = -1;
          pe_sel_end = -1;
          pe_dragging = true;
          draw(app);
        } else {
          pe_cursor = static_cast<int>(pe_buf.size());
          pe_sel_start = -1;
          pe_sel_end = -1;
          pe_dragging = true;
          draw(app);
        }
        return;
      }

      // Breadcrumb click
      for (size_t i = 0; i < in_breadcrumbs.size(); ++i) {
        auto& seg = in_breadcrumbs[i];
        if (x >= seg.x && x < seg.x + seg.w) {
          navigate_to(app, seg.path);
          draw(app);
          return;
        }
      }
      // Click on empty area in top bar → enter path editing mode
      (app.active_pane ? app.r_path_edit_buf : app.path_edit_buf) = app.cur_tab().current_path;
      (app.active_pane ? app.r_path_editing : app.path_editing) = true;
      {
        double zf = app.zoom_pct / 100.0;
        int s_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int arrow_w = static_cast<int>(36.0 * zf);
        int gap4 = static_cast<int>(4.0 * zf);
        int mx6 = static_cast<int>(24.0 * zf);
        int path_pad = static_cast<int>(12.0 * zf);
        int house_w = static_cast<int>(16.0 * zf);
        int gap12 = static_cast<int>(12.0 * zf);
        int nav_origin = s_w + static_cast<int>(20.0 * zf);
        int path_x = nav_origin + 2 * arrow_w + gap4 + mx6 + path_pad + house_w + gap12;
        int path_w = in_search_btn_x - static_cast<int>(4.0 * zf) - path_x;
        int text_x = path_x;
        cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cairo_t* cr_tmp = cairo_create(tmp);
        cairo_select_font_face(cr_tmp, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr_tmp, 13.0 * zf);
        const std::string& buf = (app.active_pane ? app.r_path_edit_buf : app.path_edit_buf);
        cairo_text_extents_t full_te;
        cairo_text_extents(cr_tmp, buf.c_str(), &full_te);
        int scroll_offset = 0;
        std::string display = buf;
        if (full_te.width > path_w - static_cast<int>(20.0 * zf)) {
          int keep = static_cast<int>(display.size()) * (path_w - static_cast<int>(20.0 * zf)) /
                    std::max(1, static_cast<int>(full_te.width));
          if (keep > 3 && keep < static_cast<int>(display.size())) {
            int trim = static_cast<int>(display.size()) - keep + 3;
            scroll_offset = trim;
            display = "..." + display.substr(static_cast<std::size_t>(trim));
          }
        }
        int click_x = x - text_x;
        int best_pos = static_cast<int>(buf.size());
        for (int ci = 0; ci <= static_cast<int>(display.size()); ++ci) {
          std::string sub = display.substr(0, static_cast<std::size_t>(ci));
          cairo_text_extents_t te;
          cairo_text_extents(cr_tmp, sub.c_str(), &te);
          if (te.width >= click_x) {
            if (scroll_offset > 0) {
              best_pos = (ci <= 3) ? scroll_offset : scroll_offset + ci - 3;
            } else {
              best_pos = ci;
            }
            break;
          }
        }
        cairo_destroy(cr_tmp);
        cairo_surface_destroy(tmp);
        (app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor) = best_pos;
      }
      (app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start) = -1;
      (app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end) = -1;
      (app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging) = true;
      draw(app);
      return;
    }

    // ── Tab bar click ──
    if (y >= app.top_bar_height && y < app.top_bar_height + app.tab_bar_height) {
      app.last_click_ns = 0;
      for (size_t i = 0; i < app.tab_hits.size(); ++i) {
        auto& hit = app.tab_hits[i];
        if (x >= hit.x && x < hit.x + hit.w) {
          if (button == 0x110 && hit.close_x > 0 && x >= hit.close_x) {
            // Left-click on close button
            if (i < app.tabs.size()) {
              app.active_tab = static_cast<int>(i);
              close_tab(app);
              draw(app);
              return;
            }
          }
          if (button == 0x110 && i < app.tabs.size()) {
            // Left-click on tab — set up drag potential
            app.tab_drag_from = static_cast<int>(i);
            app.tab_drag_start_x = x;
            if (static_cast<int>(i) != app.active_tab) {
              app.active_tab = static_cast<int>(i);
              reload_dir(app);
            }
            draw(app);
            return;
          }
          if (button == 0x210 && i < app.tabs.size()) {
            // Middle-click on tab → close
            app.active_tab = static_cast<int>(i);
            close_tab(app);
            draw(app);
            return;
          }
          if (button == 0x210 && i < app.tabs.size()) {
            // Middle-click on tab → close (only reached for left-clicks due to outer scope)
            app.active_tab = static_cast<int>(i);
            close_tab(app);
            draw(app);
            return;
          }
        }
      }
      return;
    }

    // ── Progress panel cancel button ──
    if (app.progress_panel_h > 0 && x < app.sidebar_width) {
      int sb_bottom = app.height - app.status_bar_height;
      int panel_y = sb_bottom - app.progress_panel_h;
      if (y >= panel_y && y < sb_bottom &&
          x >= app.progress_cancel_x && x < app.progress_cancel_x + app.progress_cancel_w &&
          y >= app.progress_cancel_y && y < app.progress_cancel_y + app.progress_cancel_h) {
        if (app.op_progress) app.op_progress->cancel = true;
        app.operation_status = "Cancelling...";
        app.operation_status_expires_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
        draw(app);
        return;
      }
    }

    int sb_idx = hit_test_sidebar(app, x, y);
    if (sb_idx >= 0 && sb_idx < static_cast<int>(app.sidebar_locations.size())) {
      app.last_click_ns = 0;
      app.sidebar_hover_idx = sb_idx;
      auto& loc = app.sidebar_locations[sb_idx];
      if (loc.kind == SidebarLocation::Kind::Computer) {
        app.cur_tab().view_mode = ViewMode::Computer;
        app.cur_tab().current_path = "computer://";
        app.cur_tab().selected_idx = -1;
        app.cur_tab().hover_idx = -1;
        app.cur_tab().scroll_px = 0;
        app.computer_scroll_px = 0;
        app.computer_scroll_smooth_current = 0;
        app.computer_scroll_smooth_target = 0;
        app.computer_needs_refresh = true;
        draw(app);
        return;
      } else if (loc.kind == SidebarLocation::Kind::Drive && !loc.drive_id.empty()) {
        if (loc.is_mounted) {
          double zf = app.zoom_pct / 100.0;
          int icon_left = app.sidebar_width - static_cast<int>(22.0 * zf);
          // Only unmount when clicking the mount indicator icon (right side of the item)
          if (x >= icon_left) {
            unmount_drive(app, sb_idx);
          } else {
            navigate_to(app, loc.path);
          }
        } else {
          mount_drive(app, sb_idx);
        }
      } else {
        navigate_to(app, loc.path);
      }
      // Set up potential drag for reordering favorites
      if (loc.kind == SidebarLocation::Kind::Favorite) {
        // Compute fav index from sidebar location index
        int places_end = 0;
        while (places_end < static_cast<int>(app.sidebar_locations.size()) &&
               app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Favorite &&
               app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Root &&
               app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Drive)
          ++places_end;
        int fav_idx = sb_idx - places_end;
        if (fav_idx >= 0 && fav_idx < static_cast<int>(app.favorites.size())) {
          app.sidebar_fav_dragging = false;
          app.sidebar_fav_drag_from = fav_idx;
          app.sidebar_fav_drag_start_y = y;
          app.sidebar_fav_drag_to = -1;
        }
      }
      draw(app);
      return;
    }

    // ── Column header click (list view) ──
    if (app.cur_tab().view_mode == ViewMode::List && y >= app.top_bar_height + app.tab_bar_height &&
        y < app.top_bar_height + app.tab_bar_height + app.entry_height) {
      int s_w = app.sidebar_expanded ? app.sidebar_width : 0;
      double zf = app.zoom_pct / 100.0;
      int text_x = s_w + static_cast<int>(28.0 * zf);
      int content_w = app.width - s_w;
      int name_w = static_cast<int>(content_w * app.col_name_frac);
      int size_w = static_cast<int>(content_w * app.col_size_frac);
      int date_w = static_cast<int>(content_w * app.col_date_frac);
      int x1 = text_x + name_w;
      int x2 = x1 + size_w;
      int x3 = x2 + date_w;

      if (std::abs(x - x1) < 4) {
        app.col_resizing = 0;
        app.col_resize_start_frac = static_cast<double>(x - s_w) /
                                     static_cast<double>(std::max(1, content_w));
        draw(app); return;
      }
      if (std::abs(x - x2) < 4) {
        app.col_resizing = 1;
        app.col_resize_start_frac = static_cast<double>(x - s_w) /
                                     static_cast<double>(std::max(1, content_w));
        draw(app); return;
      }
      if (std::abs(x - x3) < 4) {
        app.col_resizing = 2;
        app.col_resize_start_frac = static_cast<double>(x - s_w) /
                                     static_cast<double>(std::max(1, content_w));
        draw(app); return;
      }

      SortField clicked = SortField::Name;
      if (x >= text_x && x < x1) clicked = SortField::Name;
      else if (x >= x1 && x < x2) clicked = SortField::Size;
      else if (x >= x2 && x < x3) clicked = SortField::Modified;
      else if (x >= x3) clicked = SortField::Type;
      else { draw(app); return; }

      if (app.cur_tab().sort_field == clicked)
        app.cur_tab().sort_descending = !app.cur_tab().sort_descending;
      else {
        app.cur_tab().sort_field = clicked;
        app.cur_tab().sort_descending = false;
      }
      reload_dir(app);
      draw(app);
      return;
    }

    // ── Content-area hit-test ──
    int idx = -1;
    if (app.cur_tab().view_mode == ViewMode::List) {
      idx = hit_test_list(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Grid) {
      idx = hit_test_grid(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Computer) {
      idx = hit_test_computer(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Tree) {
      idx = hit_test_tree(app, x, y, true);
    } else if (app.cur_tab().view_mode == ViewMode::Compact) {
      idx = hit_test_compact(app, x, y);
    }

    if (idx == -2) {
      // Tree view arrow toggle
      build_tree_entries(app);
      draw(app);
      return;
    }

    if (idx >= 0) {
      // Computer view: single click selects/mounts, double click opens
      if (app.cur_tab().view_mode == ViewMode::Computer) {
        auto& item = app.computer_items[idx];
        // Single-click mount for unmounted drives (matches sidebar behavior)
        if (item.shape == ComputerItem::ShapeType::Large && !item.is_mounted && !item.drive_id.empty()) {
          for (size_t si = 0; si < app.sidebar_locations.size(); ++si) {
            auto& sloc = app.sidebar_locations[si];
            if (sloc.kind == SidebarLocation::Kind::Drive && sloc.drive_id == item.drive_id) {
              mount_drive(app, static_cast<int>(si));
              break;
            }
          }
          app.computer_hover_idx = idx;
          app.last_click_ns = now_ns;
          app.last_click_x = x;
          app.last_click_y = y;
          app.last_click_idx = idx;
          draw(app);
          return;
        }
        uint64_t elapsed_ns = now_ns - app.last_click_ns;
        bool same_pos = std::abs(x - app.last_click_x) < 8 &&
                        std::abs(y - app.last_click_y) < 8;
        if (idx >= 0 && idx == app.last_click_idx && same_pos && elapsed_ns < 400000000ull) {
          // Double-click
          if (item.shape == ComputerItem::ShapeType::Small && !item.path.empty()) {
            navigate_to(app, item.path);
            app.last_click_ns = 0;
            return;
          } else if (item.shape == ComputerItem::ShapeType::Large && item.is_mounted && !item.path.empty()) {
            navigate_to(app, item.path);
            app.last_click_ns = 0;
            return;
          }
          app.last_click_ns = 0;
          draw(app);
          return;
        }
        // Single click: select
        app.computer_hover_idx = idx;
        app.last_click_ns = now_ns;
        app.last_click_x = x;
        app.last_click_y = y;
        app.last_click_idx = idx;
        draw(app);
        return;
      }

      auto* xkb = app.seat.xkb_state_ptr();
      bool ctrl_mod = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CTRL,
                                                           XKB_STATE_MODS_EFFECTIVE) != 0;
      bool shift_mod = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_SHIFT,
                                                             XKB_STATE_MODS_EFFECTIVE) != 0;

      uint64_t elapsed_ns = now_ns - app.last_click_ns;
      bool same_pos = std::abs(x - app.last_click_x) < 8 &&
                      std::abs(y - app.last_click_y) < 8;
      if (idx == app.last_click_idx && same_pos && elapsed_ns < 400000000ull) {
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        app.drag_potential = false;
        app.drag_potential_idx = -1;
        open_selected(app);
        app.last_click_ns = 0;
        draw(app);
        return;
      }

      if (shift_mod && !ctrl_mod) {
        // Shift-click: select range from anchor to idx
        if (app.cur_tab().sel_anchor < 0) app.cur_tab().sel_anchor = 0;
        int lo = std::min(app.cur_tab().sel_anchor, idx);
        int hi = std::max(app.cur_tab().sel_anchor, idx);
        app.cur_tab().multi_selected.clear();
        for (int i = lo; i <= hi; ++i) app.cur_tab().multi_selected.push_back(i);
        app.cur_tab().selected_idx = idx;
      } else if (ctrl_mod && !shift_mod) {
        // Ctrl-click: toggle idx in multi_selected
        auto it = std::find(app.cur_tab().multi_selected.begin(), app.cur_tab().multi_selected.end(), idx);
        if (it != app.cur_tab().multi_selected.end()) {
          app.cur_tab().multi_selected.erase(it);
          // If selected_idx was this item, pick another or -1
          if (app.cur_tab().selected_idx == idx) {
            app.cur_tab().selected_idx = app.cur_tab().multi_selected.empty() ? -1 : app.cur_tab().multi_selected.back();
          }
        } else {
          app.cur_tab().multi_selected.push_back(idx);
          app.cur_tab().selected_idx = idx;
        }
        app.cur_tab().sel_anchor = idx;
      } else {
        // Plain click: single select
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        app.cur_tab().sel_anchor = idx;
      }
      app.last_click_ns = now_ns;
      app.last_click_x = x;
      app.last_click_y = y;
      app.last_click_idx = idx;

      // Set drag potential (only for plain click)
      if (!shift_mod && !ctrl_mod) {
        app.drag_potential = true;
        app.drag_potential_idx = idx;
        app.drag_start_x = static_cast<double>(x);
        app.drag_start_y = static_cast<double>(y);
        app.drag_button_serial = app.seat.last_pointer_button_serial();
        app.drag_paths.clear();
        for (int vis_idx : app.cur_tab().multi_selected) {
          if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
            int real_idx = app.cur_tab().visible_entries[vis_idx];
            if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()))
              app.drag_paths.push_back(app.cur_tab().entries[real_idx].path);
          }
        }
      }
    } else {
      // Start marquee selection on empty background
      app.marquee_x0 = static_cast<double>(x);
      app.marquee_y0 = static_cast<double>(y);
      app.marquee_x1 = static_cast<double>(x);
      app.marquee_y1 = static_cast<double>(y);
      app.marquee_active = true;
      app.cur_tab().selected_idx = -1;
      app.cur_tab().multi_selected.clear();
      app.cur_tab().sel_anchor = -1;
      app.last_click_ns = 0;
    }
    draw(app);
    return;
  }

  // Right click (BTN_RIGHT = 0x111)
  if (button == 0x111) {
    // Path editing right-click context menu
    if ((app.active_pane ? app.r_path_editing : app.path_editing)) {
      int rc_bar_y = y;
      if (app.split_view) {
        int content_y = app.top_bar_height + app.tab_bar_height;
        if (y >= content_y && y < content_y + app.top_bar_height)
          rc_bar_y = y - content_y;
      }
      if (rc_bar_y < app.top_bar_height) {
      app.context_menu_open = true;
      app.context_menu_x = x;
      app.context_menu_y = y;
      app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
      app.context_menu_file_idx = -3; // path editing
      app.context_menu_sidebar_idx = -1;
      app.context_menu_items = {
        AppState::menu_item(AppState::ContextMenuAction::Copy, "Copy"),
      };
      draw(app);
      return;
    }
    }

    // Tab bar right-click context menu
    if (y >= app.top_bar_height && y < app.top_bar_height + app.tab_bar_height) {
      for (size_t i = 0; i < app.tab_hits.size(); ++i) {
        auto& hit = app.tab_hits[i];
        if (x >= hit.x && x < hit.x + hit.w && i < app.tabs.size()) {
          app.context_menu_open = true;
          app.context_menu_x = x;
          app.context_menu_y = y;
          app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
          app.context_menu_file_idx = -4;
          app.context_menu_tab_idx = static_cast<int>(i);
          app.context_menu_items = {
            AppState::menu_item(AppState::ContextMenuAction::CloseTab, "Close Tab"),
            AppState::menu_item(AppState::ContextMenuAction::CloseOtherTabs, "Close Other Tabs"),
            AppState::menu_item(AppState::ContextMenuAction::CloseAllTabs, "Close All Tabs"),
            AppState::menu_item(AppState::ContextMenuAction::DuplicateTab, "Duplicate Tab"),
          };
          draw(app);
          return;
        }
      }
      return;
    }

    // Check Computer view right-click for drive mount/unmount
    if (app.cur_tab().view_mode == ViewMode::Computer) {
      int cidx = hit_test_computer(app, x, y);
      if (cidx >= 0 && cidx < static_cast<int>(app.computer_items.size())) {
        auto& citem = app.computer_items[cidx];
        if (citem.shape == ComputerItem::ShapeType::Large && !citem.drive_id.empty()) {
          app.context_menu_open = true;
          app.context_menu_x = x;
          app.context_menu_y = y;
          app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
          app.context_menu_file_idx = cidx;
          app.context_menu_items = {};
          // Find matching sidebar item for mount_drive/unmount_drive
          app.context_menu_sidebar_idx = -1;
          for (size_t si = 0; si < app.sidebar_locations.size(); ++si) {
            if (app.sidebar_locations[si].kind == SidebarLocation::Kind::Drive &&
                app.sidebar_locations[si].drive_id == citem.drive_id) {
              app.context_menu_sidebar_idx = static_cast<int>(si);
              break;
            }
          }
          if (citem.is_mounted)
            app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::UnmountDrive, "Unmount"));
          else
            app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::MountDrive, "Mount"));
          draw(app);
          return;
        }
      }
    }

    if (app.context_menu_open) {
      app.context_menu_open = false;
      draw(app);
      return;
    }

    // ── Progress panel cancel (right-click too) ──
    if (app.progress_panel_h > 0 && x < app.sidebar_width) {
      int sb_bottom = app.height - app.status_bar_height;
      int panel_y = sb_bottom - app.progress_panel_h;
      if (y >= panel_y && y < sb_bottom &&
          x >= app.progress_cancel_x && x < app.progress_cancel_x + app.progress_cancel_w &&
          y >= app.progress_cancel_y && y < app.progress_cancel_y + app.progress_cancel_h) {
        if (app.op_progress) app.op_progress->cancel = true;
        app.operation_status = "Cancelling...";
        app.operation_status_expires_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
        draw(app);
        return;
      }
    }

    // Check sidebar right-click first (sidebar items get context menu)
    int sb_idx = hit_test_sidebar(app, x, y);
    if (sb_idx >= 0 && sb_idx < static_cast<int>(app.sidebar_locations.size())) {
      auto& loc = app.sidebar_locations[sb_idx];
      app.context_menu_open = true;
      app.context_menu_x = x;
      app.context_menu_y = y;
      app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
      app.context_menu_file_idx = -2; // sidebar item
      app.context_menu_sidebar_idx = sb_idx;
      app.context_menu_items = {};

      if (loc.kind == SidebarLocation::Kind::Favorite) {
        app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::RemoveFromFavorites, "Remove from Favorites"));
      } else if (loc.kind == SidebarLocation::Kind::Drive) {
        if (loc.is_mounted && !loc.drive_id.empty()) {
          app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::UnmountDrive, "Unmount"));
        }
        if (!loc.is_mounted && !loc.drive_id.empty()) {
          app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::MountDrive, "Mount"));
        }
      } else if (loc.kind == SidebarLocation::Kind::Trash) {
        app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Open, "Open"));
        app.context_menu_items.push_back(AppState::menu_separator());
        app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::EmptyTrash, "Empty Trash"));
        app.context_menu_items.push_back(AppState::menu_separator());
        app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::OpenInNewTab, "Open in new tab"));
      } else if (loc.kind == SidebarLocation::Kind::Computer) {
        // No context menu actions for computer view virtual path
      }

      if (!loc.path.empty() && loc.kind != SidebarLocation::Kind::Trash) {
        if (!app.context_menu_items.empty())
          app.context_menu_items.push_back(AppState::menu_separator());
        app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::OpenInNewTab, "Open in new tab"));
      }

      draw(app);
      return;
    }

    int idx = -1;
    if (app.cur_tab().view_mode == ViewMode::List) {
      idx = hit_test_list(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Grid) {
      idx = hit_test_grid(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Computer) {
      idx = hit_test_computer(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Tree) {
      idx = hit_test_tree(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Compact) {
      idx = hit_test_compact(app, x, y);
    }

    open_context_menu(app, idx, x, y);
    draw(app);
    return;
  }
}

void handle_pointer_move(AppState& app, int x, int y) {
  app.pointerX = static_cast<double>(x);
  app.pointerY = static_cast<double>(y);

  // ── Split pane divider drag ──
  if (app.split_view) {
    int s_w = app.sidebar_expanded ? app.sidebar_width : 0;
    int content_w = app.width - s_w - (app.info_panel_open ? app.info_panel_width : 0);
    int split = app.split_divider_x;
    if (split <= 0) split = content_w / 2;
    int div_x = s_w + split;
    int div_w = 4;
    if (app.split_divider_dragging) {
      app.split_divider_x = std::clamp(x - s_w, 100, app.width - s_w - 100 - app.info_panel_width);
      draw(app);
      return;
    }
    bool over_div = (x >= div_x && x < div_x + div_w);
    if (over_div != app.split_divider_hover) {
      app.split_divider_hover = over_div;
      draw(app);
      return;
    }
    // Set active pane based on pointer position (after divider checks)
    app.active_pane = (x >= div_x + div_w) ? 1 : 0;
  }

  // ── Marquee drag ──
  if (app.marquee_active) {
    app.marquee_x1 = static_cast<double>(x);
    app.marquee_y1 = static_cast<double>(y);
    hit_test_marquee(app);
    draw(app);
    return;
  }

  // ── Drag potential: initiate drag if past threshold ──
  if (app.drag_potential && !app.marquee_active) {
    double dx = x - app.drag_start_x;
    double dy = y - app.drag_start_y;
    if (dx * dx + dy * dy > 64.0) {
      if (!app.drag_paths.empty()) {
        start_drag(app);
      } else {
        cancel_drag(app);
      }
      if (app.wl.display()) wl_display_flush(app.wl.display());
      return;
    }
  }

  // ── Sidebar drag ──
  if (app.sidebar_dragging) {
    int delta = x - app.sidebar_drag_start_x;
    int new_width = std::clamp(app.sidebar_drag_start_width + delta, 120, 400);
    app.sidebar_width = new_width;
    app.sidebar_width_base = static_cast<int>(new_width * 100.0 / app.zoom_pct);
    draw(app);
    return;
  }

  // ── Sidebar favorite reorder drag ──
  if (app.sidebar_fav_drag_from >= 0 && !app.sidebar_fav_dragging) {
    int dy = y - app.sidebar_fav_drag_start_y;
    if (dy * dy > 64) {
      app.sidebar_fav_dragging = true;
    }
  }
  if (app.sidebar_fav_dragging && app.sidebar_expanded) {
    app.sidebar_fav_drag_current_y = y;
    // Recalculate places_end to map fav indices
    int total = static_cast<int>(app.sidebar_locations.size());
    int places_end = 0;
    while (places_end < total &&
           app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Favorite &&
           app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Root &&
           app.sidebar_locations[places_end].kind != SidebarLocation::Kind::Drive)
      ++places_end;
    int fav_start = places_end;
    while (fav_start < total &&
           app.sidebar_locations[fav_start].kind == SidebarLocation::Kind::Favorite)
      ++fav_start;
    int fav_count = fav_start - places_end;

    // Compute which fav slot the cursor is over
    double zf = 1.2 * app.sidebar_scale;
    int item_h = static_cast<int>(36.0 * zf);
    int header_h = static_cast<int>(24.0 * zf);
    int div_pad = static_cast<int>(8.0 * zf);
    int div_total = div_pad + 1 + div_pad;

    // Y position of the Favorites section first item
    int fav_top = app.top_bar_height + app.tab_bar_height - app.sidebar_scroll_px +
                  header_h + places_end * item_h + div_total + header_h;

    int rel_y = y - fav_top;
    int slot = rel_y / item_h;
    if (slot < 0) slot = 0;
    if (slot > fav_count) slot = fav_count;

    int visual_to = slot;
    int new_to = slot;
    if (new_to > app.sidebar_fav_drag_from) {
      // Remove shifts items left → insertion index is one less
      new_to = new_to - 1;
    }

    if (new_to != app.sidebar_fav_drag_to)
      app.sidebar_fav_drag_to = new_to;
    app.sidebar_fav_drag_to_visual = visual_to;
    draw(app);
    return;
  }

  // ── Tab reorder drag ──
  if (app.tab_drag_from >= 0 && !app.tab_dragging) {
    int dx = x - app.tab_drag_start_x;
    if (dx * dx > 64) {
      app.tab_dragging = true;
      app.tab_drag_current_x = x;
    }
  }
  if (app.tab_dragging) {
    app.tab_drag_current_x = x;
    int n = static_cast<int>(app.tabs.size());
    // Determine slot based on mouse x relative to tab hit rects
    int slot = n;
    for (int i = 0; i < n; ++i) {
      auto& hit = app.tab_hits[i];
      int mid = hit.x + hit.w / 2;
      if (x < mid) {
        slot = i;
        break;
      }
    }
    int visual_to = slot;
    int new_to = slot;
    if (new_to > app.tab_drag_from) {
      new_to = new_to - 1;
    }
    if (new_to != app.tab_drag_to)
      app.tab_drag_to = new_to;
    app.tab_drag_to_visual = visual_to;
    draw(app);
    return;
  }

  // ── Sidebar resize edge hover ──
  if (app.sidebar_expanded) {
    int edge_x = app.sidebar_width;
    if (x >= edge_x - 4 && x <= edge_x + 4) {
      if (!app.sidebar_hover_resize) {
        app.sidebar_hover_resize = true;
        draw(app);
      }
    } else {
      if (app.sidebar_hover_resize) {
        app.sidebar_hover_resize = false;
        draw(app);
      }
    }
  } else if (app.sidebar_hover_resize) {
    app.sidebar_hover_resize = false;
    draw(app);
  }

  // ── Confirm dialog hover ──
  if (app.confirm_open) {
    int dlg_w = 380;
    int dlg_h = 170;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int btn_y = dlg_y + dlg_h - 50;
    int btn_h = 32;
    int btn_w = 90;
    int cancel_x = dlg_x + dlg_w - 220;
    int delete_x = dlg_x + dlg_w - 110;
    int new_hover = -1;
    if (x >= delete_x && x < delete_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover = 1;
    else if (x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover = 0;
    if (new_hover != app.confirm_hover_btn) {
      app.confirm_hover_btn = new_hover;
      draw(app);
    }
    return;
  }

  // ── Compress dialog hover ──
  if (app.compress_dialog_open) {
    int dlg_w = 420;
    int dlg_h = 310;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int content_x = dlg_x + 20;
    int content_y = dlg_y + 50;
    int fmt_w = 80;
    int fmt_h = 28;
    int fmt_gap = 8;
    int fmy = content_y + 18;

    int new_hover_fmt = -1;
    for (int i = 0; i < 4; ++i) {
      if (!app.compress_format_available[i]) continue;
      int fmx = content_x + i * (fmt_w + fmt_gap);
      if (x >= fmx && x < fmx + fmt_w && y >= fmy && y < fmy + fmt_h) {
        new_hover_fmt = i;
        break;
      }
    }
    if (new_hover_fmt < 0) {
      int fmy2 = fmy + fmt_h + fmt_gap;
      for (int i = 4; i < 7; ++i) {
        if (!app.compress_format_available[i]) continue;
        int fmx = content_x + (i - 4) * (fmt_w + fmt_gap);
        if (x >= fmx && x < fmx + fmt_w && y >= fmy2 && y < fmy2 + fmt_h) {
          new_hover_fmt = i;
          break;
        }
      }
    }

    int name_y = fmy + fmt_h + fmt_gap + fmt_h + 14;
    int input_y = name_y + 18;
    int input_h = 32;
    int lvl_y = input_y + input_h + 14;
    int lvl_btn_y = lvl_y + 18;
    int lvl_btn_w = 68;
    int lvl_btn_h = 28;
    int lvl_gap = 8;
    int new_hover_lvl = -1;
    for (int i = 0; i < 5; ++i) {
      int lx = content_x + i * (lvl_btn_w + lvl_gap);
      if (x >= lx && x < lx + lvl_btn_w && y >= lvl_btn_y && y < lvl_btn_y + lvl_btn_h) {
        new_hover_lvl = i;
        break;
      }
    }

    int btn_y = dlg_y + dlg_h - 50;
    int btn_w = 90;
    int btn_h = 32;
    int cancel_x = dlg_x + dlg_w - 220;
    int compress_x = dlg_x + dlg_w - 110;
    int new_hover_btn = -1;
    if (x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover_btn = 0;
    else if (x >= compress_x && x < compress_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover_btn = 1;

    bool changed = (new_hover_fmt != app.compress_hover_format) ||
                   (new_hover_lvl != app.compress_hover_level) ||
                   (new_hover_btn != app.compress_hover_btn);
    if (changed) {
      app.compress_hover_format = new_hover_fmt;
      app.compress_hover_level = new_hover_lvl;
      app.compress_hover_btn = new_hover_btn;
      draw(app);
    }
    return;
  }

  // ── Batch rename dialog hover ──
  if (app.batch_rename_open) {
    bool is_template = (app.batch_rename_mode == 0);
    int n = static_cast<int>(app.batch_rename_entries.size());
    int dlg_w = 540;
    int list_h = std::min(n * 28 + 4, 280) + 4;
    int input_area_h = is_template ? 70 : 80;
    int dlg_h = 24 + 28 + input_area_h + list_h + 56;
    int dlg_x = (app.width - dlg_w) / 2;
    int dlg_y = (app.height - dlg_h) / 2;
    int cx = dlg_x + 20;

    int tab_y = dlg_y + 42;
    int tab_h = 26;
    int tab_w = 210;
    int input_y = tab_y + tab_h + 10;
    int field_h = 30;
    int btn_y = dlg_y + dlg_h - 44;
    int btn_w = 90;
    int btn_h = 32;
    int cancel_x = dlg_x + dlg_w - 230;
    int rename_x = dlg_x + dlg_w - 120;

    int new_hover_mode = -1;
    if (y >= tab_y && y < tab_y + tab_h) {
      if (x >= cx && x < cx + tab_w)
        new_hover_mode = 0;
      else if (x >= cx + tab_w + 8 && x < cx + tab_w + 8 + tab_w)
        new_hover_mode = 1;
    }

    int new_hover_btn = -1;
    if (x >= rename_x && x < rename_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover_btn = 0;
    else if (x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h)
      new_hover_btn = 1;
    else if (is_template) {
      // +Add button hover
      int tf_x = cx;
      int tf_y = input_y;
      int tf_w = 360;
      int add_x = tf_x + tf_w + 8;
      int add_w = 70;
      if (x >= add_x && x < add_x + add_w && y >= tf_y && y < tf_y + field_h)
        new_hover_btn = 3;

      // +Add dropdown item hover
      if (app.batch_rename_show_add) {
        int dd_x = add_x;
        int dd_y = tf_y + field_h + 2;
        int dd_w = add_w;
        int dd_item_h = 26;
        int dd_h2 = 3 * dd_item_h + 4;
        int new_add_hover = -1;
        if (x >= dd_x && x < dd_x + dd_w && y >= dd_y && y < dd_y + dd_h2) {
          int opt = (y - dd_y - 2) / dd_item_h;
          if (opt >= 0 && opt <= 2) new_add_hover = opt;
        }
        if (new_add_hover != app.batch_rename_add_hover) {
          app.batch_rename_add_hover = new_add_hover;
          draw(app);
        }
      }
    }

    bool changed = (new_hover_mode != app.batch_rename_hover_mode) ||
                   (new_hover_btn != app.batch_rename_hover_btn);
    if (changed) {
      app.batch_rename_hover_mode = new_hover_mode;
      app.batch_rename_hover_btn = new_hover_btn;
      draw(app);
    }
    return;
  }

  // ── Properties dialog hover ──
  if (app.properties.open) {
    draw(app);
    return;
  }

  // ── Settings dialog hover ──
  if (app.settings_open) {
    int hit = settings_hit_test(app, x, y);
    if (hit >= 0)
      app.settings_dropdown_hover = hit;
    else
      app.settings_dropdown_hover = -1;
    draw(app);
    return;
  }

  if (app.open_with_open) {
    double dx = static_cast<double>(x), dy = static_cast<double>(y);
    int new_hover = -1;

    if (dx >= app.open_with_hit_close[0] && dx < app.open_with_hit_close[0] + app.open_with_hit_close[2] &&
        dy >= app.open_with_hit_close[1] && dy < app.open_with_hit_close[1] + app.open_with_hit_close[3]) {
      new_hover = -2;
    } else if (dx >= app.open_with_hit_cancel[0] && dx < app.open_with_hit_cancel[0] + app.open_with_hit_cancel[2] &&
               dy >= app.open_with_hit_cancel[1] && dy < app.open_with_hit_cancel[1] + app.open_with_hit_cancel[3]) {
      new_hover = -3;
    } else if (dx >= app.open_with_hit_open[0] && dx < app.open_with_hit_open[0] + app.open_with_hit_open[2] &&
               dy >= app.open_with_hit_open[1] && dy < app.open_with_hit_open[1] + app.open_with_hit_open[3]) {
      new_hover = -4;
    } else {
      int pad = 16, pad_in = 12, top_bar_h = 44, entry_h = 40;
      int total = static_cast<int>(app.open_with_apps.size());
      int max_list_h = 320;
      int visible = std::max(1, std::min(total, max_list_h / entry_h));
      int list_h = visible * entry_h;
      int list_x = static_cast<int>(app.open_with_x) + pad_in;
      int list_y = static_cast<int>(app.open_with_y) + pad + top_bar_h + pad_in;
      int list_w = static_cast<int>(app.open_with_w) - 2 * pad_in;
      if (dx >= list_x && dx < list_x + list_w &&
          dy >= list_y && dy < list_y + list_h) {
        int idx = app.open_with_scroll + static_cast<int>((dy - list_y) / entry_h);
        if (idx >= 0 && idx < total) new_hover = idx;
      }
    }

    if (new_hover != app.open_with_hover) {
      app.open_with_hover = new_hover;
      draw(app);
    }
    return;
  }

  if (app.term_chooser_open) {
    const int kPad = 20, kTopBarH = 44, kEntryH = 40, kBottomBarH = 52;
    const int kMaxListH = 300;
    const int total = static_cast<int>(app.term_chooser_apps.size());
    const int max_visible = std::max(1, kMaxListH / kEntryH);
    const int visible = std::min(total, max_visible);
    const int list_h = visible * kEntryH;
    const int card_w = app.term_chooser_w;
    const int card_h = kPad + kTopBarH + 8 + list_h + 8 + kBottomBarH + kPad;
    const int card_x = app.term_chooser_x;
    const int card_y = (app.height - card_h) / 2;

    const int close_x = card_x + card_w - kPad - 28;
    const int close_y = card_y + kPad - 4;
    const int list_x = card_x + 12;
    const int list_y = card_y + kPad + kTopBarH + 8;

    int new_hover = -1;
    if (x >= close_x && x < close_x + 28 && y >= close_y && y < close_y + 28)
      new_hover = -2;
    else if (x >= list_x && x < list_x + card_w - 24 && y >= list_y && y < list_y + list_h) {
      int rel_y = y - list_y + app.term_chooser_scroll * kEntryH;
      int idx = rel_y / kEntryH;
      if (idx >= 0 && idx < total) new_hover = idx;
    }
    app.term_chooser_hover = new_hover;
    draw(app);
    return;
  }

  // ── Per-pane position helpers ──
  auto& pm_search_btn_x = app.active_pane ? app.r_search_btn_x : app.search_btn_x;
  auto& pm_search_btn_w = app.active_pane ? app.r_search_btn_w : app.search_btn_w;
  auto& pm_folder_search_btn_x = app.active_pane ? app.r_folder_search_btn_x : app.folder_search_btn_x;
  auto& pm_folder_search_btn_w = app.active_pane ? app.r_folder_search_btn_w : app.folder_search_btn_w;
  auto& pm_view_btn_x = app.active_pane ? app.r_view_btn_x : app.view_btn_x;
  auto& pm_view_btn_w = app.active_pane ? app.r_view_btn_w : app.view_btn_w;
  auto& pm_sort_btn_x = app.active_pane ? app.r_sort_btn_x : app.sort_btn_x;
  auto& pm_sort_btn_w = app.active_pane ? app.r_sort_btn_w : app.sort_btn_w;
  auto& pm_dots_btn_x = app.active_pane ? app.r_dots_btn_x : app.dots_btn_x;
  auto& pm_dots_btn_y = app.active_pane ? app.r_dots_btn_y : app.dots_btn_y;
  auto& pm_dots_btn_w = app.active_pane ? app.r_dots_btn_w : app.dots_btn_w;
  auto& pm_dots_btn_h = app.active_pane ? app.r_dots_btn_h : app.dots_btn_h;
  auto& pm_filter_btn_x = app.active_pane ? app.r_filter_btn_x : app.filter_btn_x;
  auto& pm_filter_btn_w = app.active_pane ? app.r_filter_btn_w : app.filter_btn_w;
  auto& pm_filter_dd_x = app.active_pane ? app.r_filter_dropdown_x : app.filter_dropdown_x;
  auto& pm_filter_dd_y = app.active_pane ? app.r_filter_dropdown_y : app.filter_dropdown_y;
  auto& pm_filter_dd_w = app.active_pane ? app.r_filter_dropdown_w : app.filter_dropdown_w;
  auto& pm_filter_dd_h = app.active_pane ? app.r_filter_dropdown_h : app.filter_dropdown_h;
  auto& pm_search_bar_x = app.active_pane ? app.r_search_bar_x : app.search_bar_x;
  auto& pm_search_bar_w = app.active_pane ? app.r_search_bar_w : app.search_bar_w;
  auto& pm_search_clear_x = app.active_pane ? app.r_search_clear_x : app.search_clear_x;
  auto& pm_search_clear_w = app.active_pane ? app.r_search_clear_w : app.search_clear_w;
  auto& pm_breadcrumbs = app.active_pane ? app.r_breadcrumbs : app.breadcrumbs;
  auto& pm_breadcrumb_hover = app.active_pane ? app.r_breadcrumb_hover : app.breadcrumb_hover;

  // ── Top-bar button hover (arrows + view mode + sort + gear + window controls) ──
  {
    int bar_y = y;
    if (app.split_view) {
      int content_y = app.top_bar_height + app.tab_bar_height;
      if (y >= content_y && y < content_y + app.top_bar_height)
        bar_y = y - content_y;
    }
    double zf = app.zoom_pct / 100.0;
    int btn_w = static_cast<int>(36.0 * zf);
    int gap4 = static_cast<int>(4.0 * zf);
    int bx = (app.sidebar_expanded ? app.sidebar_width : 0) + static_cast<int>(20.0 * zf);
    bool bh = (bar_y < app.top_bar_height && x >= bx && x < bx + btn_w);
    bx += btn_w + gap4;
    bool fh = (bar_y < app.top_bar_height && x >= bx && x < bx + btn_w);

    bool vh = (bar_y < app.top_bar_height && x >= pm_view_btn_x && x < pm_view_btn_x + pm_view_btn_w);
    bool search_h = (bar_y < app.top_bar_height && x >= pm_search_btn_x && x < pm_search_btn_x + pm_search_btn_w);
    bool folder_search_h = (bar_y < app.top_bar_height && x >= pm_folder_search_btn_x && x < pm_folder_search_btn_x + pm_folder_search_btn_w);
    bool sh = (bar_y < app.top_bar_height && x >= pm_sort_btn_x && x < pm_sort_btn_x + pm_sort_btn_w);
    bool filter_h = ((app.active_pane ? app.r_search_active : app.search_active) || (app.active_pane ? app.r_recursive_search_active : app.recursive_search_active)) &&
                     bar_y < app.top_bar_height && x >= pm_filter_btn_x && x < pm_filter_btn_x + pm_filter_btn_w;

    int gear_w = static_cast<int>(36.0 * zf);
    int gear_x = pm_sort_btn_x + pm_sort_btn_w + gap4;
    bool gh = (bar_y < app.top_bar_height && x >= gear_x && x < gear_x + gear_w);

    bool dh = (pm_dots_btn_w > 0 &&
               bar_y >= pm_dots_btn_y && bar_y < pm_dots_btn_y + pm_dots_btn_h &&
               x >= pm_dots_btn_x && x < pm_dots_btn_x + pm_dots_btn_w);

    // Window control buttons (traffic lights on right: max | min | close)
    bool close_h = false, min_h = false, max_h = false;
    if (app.win_btn_x > 0 && bar_y < app.top_bar_height) {
      int rel = x - app.win_btn_x;
      if (rel >= 0 && rel < 3 * app.win_btn_w) {
        int slot = rel / app.win_btn_w;
        max_h   = (slot == 0);
        min_h   = (slot == 1);
        close_h = (slot == 2);
      }
    }

    if (bh != (app.active_pane ? app.r_arrow_back_hover : app.arrow_back_hover) ||
        fh != (app.active_pane ? app.r_arrow_forward_hover : app.arrow_forward_hover) ||
        vh != (app.active_pane ? app.r_view_mode_btn_hover : app.view_mode_btn_hover) ||
        search_h != (app.active_pane ? app.r_search_btn_hover : app.search_btn_hover) ||
        folder_search_h != (app.active_pane ? app.r_folder_search_btn_hover : app.folder_search_btn_hover) ||
        sh != (app.active_pane ? app.r_sort_btn_hover : app.sort_btn_hover) ||
        gh != (app.active_pane ? app.r_settings_btn_hover : app.settings_btn_hover) ||
        dh != (app.active_pane ? app.r_dots_btn_hover : app.dots_btn_hover) ||
        filter_h != (app.active_pane ? app.r_filter_btn_hover : app.filter_btn_hover) ||
        close_h != app.win_btn_close_hover || min_h != app.win_btn_min_hover ||
        max_h != app.win_btn_max_hover) {
      (app.active_pane ? app.r_arrow_back_hover : app.arrow_back_hover) = bh;
      (app.active_pane ? app.r_arrow_forward_hover : app.arrow_forward_hover) = fh;
      (app.active_pane ? app.r_view_mode_btn_hover : app.view_mode_btn_hover) = vh;
      (app.active_pane ? app.r_search_btn_hover : app.search_btn_hover) = search_h;
      (app.active_pane ? app.r_folder_search_btn_hover : app.folder_search_btn_hover) = folder_search_h;
      (app.active_pane ? app.r_sort_btn_hover : app.sort_btn_hover) = sh;
      (app.active_pane ? app.r_settings_btn_hover : app.settings_btn_hover) = gh;
      (app.active_pane ? app.r_dots_btn_hover : app.dots_btn_hover) = dh;
      (app.active_pane ? app.r_filter_btn_hover : app.filter_btn_hover) = filter_h;
      app.win_btn_close_hover = close_h;
      app.win_btn_min_hover = min_h;
      app.win_btn_max_hover = max_h;
      draw(app);
      return;
    }
  }

  // ── Directory picker bar hover ──
  if (app.select_dir_mode || app.select_file_mode) {
    bool sh = (y >= app.select_bar_y && y < app.select_bar_y + app.select_bar_h &&
               x >= app.select_btn_x && x < app.select_btn_x + app.select_btn_w);
    bool ch = (y >= app.select_bar_y && y < app.select_bar_y + app.select_bar_h &&
               x >= app.cancel_btn_x && x < app.cancel_btn_x + app.cancel_btn_w);
    if (sh != app.select_btn_hover || ch != app.cancel_btn_hover) {
      app.select_btn_hover = sh;
      app.cancel_btn_hover = ch;
      draw(app);
      return;
    }
  }

  // ── Sort menu item hover ──
  if ((app.active_pane ? app.r_sort_menu_open : app.sort_menu_open)) {
    int new_hover = -1;
    if (x >= (app.active_pane ? app.r_sort_menu_x : app.sort_menu_x) && x < (app.active_pane ? app.r_sort_menu_x : app.sort_menu_x) + (app.active_pane ? app.r_sort_menu_w : app.sort_menu_w) &&
        y >= (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) && y < (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) + (app.active_pane ? app.r_sort_menu_h : app.sort_menu_h)) {
      int rel_y = y - (app.active_pane ? app.r_sort_menu_y : app.sort_menu_y) - 6;
      int idx = rel_y / 30;
      if (idx >= 0 && idx <= 3) new_hover = idx;
    }
    if (new_hover != (app.active_pane ? app.r_sort_menu_hover : app.sort_menu_hover)) {
      (app.active_pane ? app.r_sort_menu_hover : app.sort_menu_hover) = new_hover;
      draw(app);
      return;
    }
  }

  // ── Filter dropdown item hover ──
  {
    auto& pm_filter_section = app.active_pane ? app.r_filter_dropdown_section : app.filter_dropdown_section;
    auto& pm_filter_hover = app.active_pane ? app.r_filter_dropdown_hover : app.filter_dropdown_hover;
    if (pm_filter_section > 0) {
      int new_hover = -1;
      if (x >= pm_filter_dd_x && x < pm_filter_dd_x + pm_filter_dd_w &&
          y >= pm_filter_dd_y && y < pm_filter_dd_y + pm_filter_dd_h) {
        int rel_y = y - pm_filter_dd_y - kFilterPD;
        int gy = 0;
        int glob = 0;
        int section = pm_filter_section;
        for (int si = 1; si <= 3; ++si) {
          // Header
          if (rel_y >= gy && rel_y < gy + kFilterHdrH) { new_hover = glob; break; }
          ++glob;
          gy += kFilterHdrH;

          // Items if expanded
          if (section == si) {
            int cnt = (si == 1) ? 13 : (si == 2) ? 7 : 5;
            int item_y = gy;
            for (int i = 0; i < cnt; ++i) {
              if (rel_y >= item_y && rel_y < item_y + kFilterItemH) { new_hover = glob; break; }
              ++glob;
              item_y += kFilterItemH;
            }
            gy = item_y;
            if (new_hover >= 0) break;
          }

          gy += kFilterSep;
        }
      }
      if (new_hover != pm_filter_hover) {
        pm_filter_hover = new_hover;
        draw(app);
        return;
      }
    }
  }

  // ── Path editing drag selection ──
  if ((app.active_pane ? app.r_path_editing : app.path_editing) && (app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging)) {
    int pm_pe_bar_y = y;
    if (app.split_view) {
      int content_y = app.top_bar_height + app.tab_bar_height;
      if (y >= content_y && y < content_y + app.top_bar_height)
        pm_pe_bar_y = y - content_y;
    }
    if (pm_pe_bar_y < app.top_bar_height) {
    auto& pm_pe_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
    auto& pm_pe_cursor = app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor;
    auto& pm_pe_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
    auto& pm_pe_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
    double zf = app.zoom_pct / 100.0;
    int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
    int arrow_w = static_cast<int>(36.0 * zf);
    int gap4 = static_cast<int>(4.0 * zf);
    int mx6 = static_cast<int>(24.0 * zf);
    int path_pad = static_cast<int>(12.0 * zf);
    int house_w = static_cast<int>(16.0 * zf);
    int gap12 = static_cast<int>(12.0 * zf);
    int nav_origin = sidebar_w + static_cast<int>(20.0 * zf);
    int path_x_inner = nav_origin + 2 * arrow_w + gap4 + mx6 + path_pad + house_w + gap12;
    int path_w_inner = pm_search_btn_x - static_cast<int>(4.0 * zf) - path_x_inner;
    int text_x = path_x_inner;
    cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr_tmp = cairo_create(tmp);
    cairo_select_font_face(cr_tmp, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr_tmp, 13.0 * zf);
    const std::string& buf = pm_pe_buf;
    cairo_text_extents_t full_te;
    cairo_text_extents(cr_tmp, buf.c_str(), &full_te);
    int scroll_offset = 0;
    std::string display = buf;
    if (full_te.width > path_w_inner - static_cast<int>(20.0 * zf)) {
      int keep = static_cast<int>(display.size()) * (path_w_inner - static_cast<int>(20.0 * zf)) /
                std::max(1, static_cast<int>(full_te.width));
      if (keep > 3 && keep < static_cast<int>(display.size())) {
        int trim = static_cast<int>(display.size()) - keep + 3;
        scroll_offset = trim;
        display = "..." + display.substr(static_cast<std::size_t>(trim));
      }
    }
    int click_x = x - text_x;
    int best_pos = static_cast<int>(buf.size());
    for (int ci = 0; ci <= static_cast<int>(display.size()); ++ci) {
      std::string sub = display.substr(0, static_cast<std::size_t>(ci));
      cairo_text_extents_t te;
      cairo_text_extents(cr_tmp, sub.c_str(), &te);
      if (te.width >= click_x) {
        if (scroll_offset > 0) {
          best_pos = (ci <= 3) ? scroll_offset : scroll_offset + ci - 3;
        } else {
          best_pos = ci;
        }
        break;
      }
    }
    cairo_destroy(cr_tmp);
    cairo_surface_destroy(tmp);
    if (best_pos != pm_pe_cursor || pm_pe_sel_start < 0) {
      if (pm_pe_sel_start < 0) {
        pm_pe_sel_start = pm_pe_cursor;
        // Force at least 1-char highlight on the very first drag move
        // so selection appears immediately rather than waiting for a
        // character-boundary crossing.
        if (best_pos == pm_pe_cursor &&
            pm_pe_cursor < static_cast<int>(pm_pe_buf.size())) {
          pm_pe_cursor = pm_pe_cursor + 1;
        } else {
          pm_pe_cursor = best_pos;
        }
      } else {
        pm_pe_cursor = best_pos;
      }
      pm_pe_sel_end = pm_pe_cursor;
      draw(app);
    }
    return;
    }
  }

  // ── Breadcrumb hover tracking ──
  {
    int pm_bc_bar_y = y;
    if (app.split_view) {
      int content_y = app.top_bar_height + app.tab_bar_height;
      if (y >= content_y && y < content_y + app.top_bar_height)
        pm_bc_bar_y = y - content_y;
    }
    if (pm_bc_bar_y < app.top_bar_height && !(app.active_pane ? app.r_path_editing : app.path_editing)) {
      int hover = -1;
      for (size_t i = 0; i < pm_breadcrumbs.size(); ++i) {
        if (x >= pm_breadcrumbs[i].x && x < pm_breadcrumbs[i].x + pm_breadcrumbs[i].w) {
          hover = static_cast<int>(i);
          break;
        }
      }
      if (hover != pm_breadcrumb_hover) {
        pm_breadcrumb_hover = hover;
        draw(app);
        return;
      }
    } else if (pm_breadcrumb_hover >= 0) {
      pm_breadcrumb_hover = -1;
      draw(app);
      return;
    }
  }

  // ── Column divider hover ──
  if (!(app.active_pane ? app.r_path_editing : app.path_editing) && app.cur_tab().view_mode == ViewMode::List && y >= app.top_bar_height + app.tab_bar_height &&
      y < app.top_bar_height + app.tab_bar_height + app.entry_height) {
    int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
    double zf = app.zoom_pct / 100.0;
    int icon_size = static_cast<int>(24.0 * zf);
    int text_x = sidebar_w + icon_size + static_cast<int>(12.0 * zf);
    int content_w = app.width - sidebar_w;
    int name_w = static_cast<int>(content_w * app.col_name_frac);
    int size_w = static_cast<int>(content_w * app.col_size_frac);
    int date_w = static_cast<int>(content_w * app.col_date_frac);
    int col_y = app.top_bar_height + app.tab_bar_height;
    int col_h = app.entry_height;
    int x1 = text_x + name_w;
    int x2 = x1 + size_w;
    int x3 = x2 + date_w;
    bool new_col_hover = (abs(x - x1) < 4 || abs(x - x2) < 4 || abs(x - x3) < 4) &&
                          y >= col_y && y < col_y + col_h;
    if (new_col_hover != app.col_hover_divider) {
      app.col_hover_divider = new_col_hover;
      draw(app);
      return;
    }
  } else if (app.col_hover_divider) {
    app.col_hover_divider = false;
    draw(app);
    return;
  }

  // ── Column divider drag ──
  if (app.col_resizing >= 0 && app.cur_tab().view_mode == ViewMode::List) {
    int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
    int content_w = app.width - sidebar_w;
    double frac = static_cast<double>(x - sidebar_w) / static_cast<double>(std::max(1, content_w));
    frac = std::clamp(frac, 0.05, 0.85);
    double delta = frac - app.col_resize_start_frac;
    if (app.col_resizing == 0) {
      app.col_name_frac = std::clamp(app.col_resize_start_frac + delta, 0.10, 0.80);
    } else if (app.col_resizing == 1) {
      app.col_size_frac = std::clamp(app.col_resize_start_frac + delta, 0.05, 0.50);
    } else if (app.col_resizing == 2) {
      app.col_date_frac = std::clamp(app.col_resize_start_frac + delta, 0.05, 0.50);
    }
    draw(app);
    return;
  }

  int sb_idx = hit_test_sidebar(app, x, y);
  app.sidebar_hover_idx = sb_idx;

  // Mount indicator hover — check if pointer is over the indicator zone of a mounted drive
  app.sidebar_mount_hover_idx = -1;
  if (sb_idx >= 0 && sb_idx < static_cast<int>(app.sidebar_locations.size())) {
    auto& loc = app.sidebar_locations[sb_idx];
    if (loc.kind == SidebarLocation::Kind::Drive && loc.is_mounted) {
      double zf = 1.2 * app.sidebar_scale;
      int ind_sz = static_cast<int>(18.0 * zf);
      int ind_x = app.sidebar_width - static_cast<int>(24.0 * zf);
      if (x >= ind_x - 4 && x <= ind_x + ind_sz + 4)
        app.sidebar_mount_hover_idx = sb_idx;
    }
  }

  // Store previous hover before updating
  int prev_hover = app.cur_tab().hover_idx;

  if (!app.context_menu_open) {
    int new_hover;
    if (app.cur_tab().view_mode == ViewMode::List) {
      new_hover = hit_test_list(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Grid) {
      new_hover = hit_test_grid(app, x, y);
    } else if (app.cur_tab().view_mode == ViewMode::Tree) {
      new_hover = hit_test_tree(app, x, y);
      if (new_hover == -2) {
        // Arrow hover: recalc actual entry index from y-position
        double zf = app.zoom_pct / 100.0;
        int entry_h = static_cast<int>(28.0 * zf);
        int content_y = app.top_bar_height + app.tab_bar_height;
        if (app.split_view) content_y += app.top_bar_height;
        int rel_y = y - content_y + app.cur_tab().scroll_px;
        int ti = rel_y / entry_h;
        if (ti >= 0 && ti < static_cast<int>(app.cur_tab().tree_entries.size()))
          new_hover = ti;
        else
          new_hover = -1;
      }
    } else if (app.cur_tab().view_mode == ViewMode::Compact) {
      new_hover = hit_test_compact(app, x, y);
    } else {
      new_hover = -1;
    }

    // Lock hover to preview entry while hover preview is active
    if (app.preview_mode == AppState::PreviewMode::Hover && app.preview_entry_idx >= 0) {
      if (new_hover < 0) {
        // Mouse left the entries area — dismiss preview
        reset_preview(app);
        app.cur_tab().hover_idx = -1;
      } else {
        app.cur_tab().hover_idx = app.preview_entry_idx;
      }
    } else {
      app.cur_tab().hover_idx = new_hover;
    }

    if (app.cur_tab().view_mode == ViewMode::Computer) {
      app.computer_hover_idx = hit_test_computer(app, x, y);
    }
  } else {
    app.cur_tab().hover_idx = -1;
    app.computer_hover_idx = -1;
  }

  // ── Hover preview timer ──
  if (app.cur_tab().hover_idx != prev_hover &&
      app.preview_mode != AppState::PreviewMode::Space) {
    reset_preview(app);
    if (app.cur_tab().hover_idx >= 0) {
      int vi = app.cur_tab().hover_idx;
      if (vi >= 0 && vi < static_cast<int>(app.cur_tab().visible_entries.size())) {
        int ri = app.cur_tab().visible_entries[vi];
        if (ri >= 0 && ri < static_cast<int>(app.cur_tab().entries.size())) {
          const auto& entry = app.cur_tab().entries[ri];
          if (entry.type == FileType::Image || entry.type == FileType::Video ||
              entry.type == FileType::Audio || entry.type == FileType::Text ||
              entry.type == FileType::Document || entry.type == FileType::Code ||
              entry.type == FileType::Archive || entry.type == FileType::Web ||
              entry.type == FileType::Font || entry.type == FileType::Executable ||
              entry.type == FileType::Markdown) {
            app.preview_entry_idx = app.cur_tab().hover_idx;
            app.preview_path = entry.path;
            timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            app.preview_hover_start_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                                          static_cast<uint64_t>(ts.tv_nsec);
          }
        }
      }
    }
  }

  if (app.context_menu_open) {
    int prev = app.context_menu_hover;
    int hit = hit_test_context_menu(app, x, y);
    app.context_menu_sub_hover = -1;
    if (hit < -9) {
      // Submenu item hovered: parent = context_menu_hover_prev, sub = -(hit + 10)
      app.context_menu_sub_hover = -(hit + 10);
      int parent = app.context_menu_hover_prev;
      if (parent >= 0 && static_cast<size_t>(parent) < app.context_menu_items.size())
        app.context_menu_hover = parent;
      else
        app.context_menu_hover = -1;
    } else {
      app.context_menu_hover = hit;
      // Keep submenu open when hovering its area (between items / separator)
      if (app.context_menu_hover < 0 && prev >= 0 &&
          static_cast<size_t>(prev) < app.context_menu_items.size() &&
          !app.context_menu_items[prev].sub_items.empty()) {
        app.context_menu_hover = prev;
      }
    }
    if (app.context_menu_hover >= 0 &&
        static_cast<size_t>(app.context_menu_hover) < app.context_menu_items.size() &&
        !app.context_menu_items[app.context_menu_hover].sub_items.empty()) {
      app.context_menu_hover_prev = app.context_menu_hover;
    }
  }
  draw(app);
}

static int headers_before(AppState const& app, int vi) {
  if (!app.cur_tab().group_by_type) return 0;
  int count = 0, prev = -1;
  for (int i = 0; i <= vi; ++i) {
    int r = app.cur_tab().visible_entries[i];
    if (r < 0 || r >= static_cast<int>(app.cur_tab().entries.size())) continue;
    int t = static_cast<int>(app.cur_tab().entries[r].type);
    if (t != prev) { ++count; prev = t; }
  }
  return count;
}

static int header_h(AppState const& app) {
  return static_cast<int>(app.entry_height * 0.55);
}

static int entry_top(AppState const& app, int vi) {
  return vi * app.entry_height + headers_before(app, vi) * header_h(app);
}

static int entry_bottom(AppState const& app, int vi) {
  return (vi + 1) * app.entry_height + headers_before(app, vi) * header_h(app);
}

void handle_scroll(AppState& app, int x, int, double, double dy) {
  if (app.open_with_open) {
    int total = static_cast<int>(app.open_with_apps.size());
    int max_visible = std::max(1, 320 / 40);
    int visible = std::min(total, max_visible);
    if (total > visible) {
      int delta = (dy > 0) ? -1 : (dy < 0) ? 1 : 0;
      app.open_with_scroll = std::clamp(app.open_with_scroll + delta, 0, total - visible);
      draw(app);
    }
    return;
  }

  // ── Properties scroll ──
  if (app.properties.open) {
    int max_scroll = std::max(0, app.properties.content_h - (static_cast<int>(app.properties.h) - 80));
    int delta = (dy > 0) ? 20 : -20;
    int new_scroll = std::clamp(app.properties.scroll_px + delta, 0, max_scroll);
    if (new_scroll != app.properties.scroll_px) {
      app.properties.scroll_px = new_scroll;
      draw(app);
    }
    return;
  }

  // ── Settings dropdown scroll ──
  if (app.settings_open && app.settings_dropdown_open) {
    int total = static_cast<int>(app.settings_term_opts.size());
    int visible = std::min(total, 6);
    int max_scroll = total - visible;
    int delta = (dy > 0) ? -1 : (dy < 0) ? 1 : 0;
    app.settings_dropdown_scroll = std::clamp(app.settings_dropdown_scroll + delta, 0, max_scroll);
    draw(app);
    return;
  }

  if (x < (app.sidebar_expanded ? app.sidebar_width : 0)) {
    app.sidebar_scroll_px = std::max(0, app.sidebar_scroll_px - static_cast<int>(dy * 10));
    draw(app);
    return;
  }

  if (app.cur_tab().view_mode == ViewMode::Computer) {
    int max_h = std::max(0, app.computer_content_h - (app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height));
    int target = std::clamp(app.computer_scroll_px - static_cast<int>(dy * 40), 0, max_h);
    app.computer_scroll_smooth_target = static_cast<double>(target);
    app.computer_scroll_smooth_current = static_cast<double>(app.computer_scroll_px);
  } else {
    int max_h = std::max(0, app.cur_tab().content_h - (app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height));
    int target = std::clamp(app.cur_tab().scroll_px - static_cast<int>(dy * 40), 0, max_h);
    app.cur_tab().scroll_smooth_target = static_cast<double>(target);
    app.cur_tab().scroll_smooth_current = static_cast<double>(app.cur_tab().scroll_px);
  }

  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  app.scroll_anim_start_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                              static_cast<uint64_t>(ts.tv_nsec);
  app.scroll_needs_redraw = true;
  draw(app);
}

bool handle_key(AppState& app, uint32_t, uint32_t state,
                xkb_keysym_t sym, const char* utf8, int utf8_len) {
  // Key release — clear repeat tracking
  if (state == 0) {
    if (sym == app.key_repeat_sym) app.key_repeat_sym = 0;
    return false;
  }
  if (state != 1) return false;

  auto* xkb = app.seat.xkb_state_ptr();
  bool ctrl = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CTRL,
                                                   XKB_STATE_MODS_EFFECTIVE) != 0;
  bool shift = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_SHIFT,
                                                    XKB_STATE_MODS_EFFECTIVE) != 0;
  bool alt = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_ALT,
                                                  XKB_STATE_MODS_EFFECTIVE) != 0;

  // In split view, determine which pane has keyboard focus
  if (app.split_view) {
    if (app.r_path_editing || app.r_search_active || app.r_recursive_search_active)
      app.active_pane = 1;
    else if (app.path_editing || app.search_active || app.recursive_search_active)
      app.active_pane = 0;
  }

  // ── Cancel running operation ──
  if (sym == XKB_KEY_Escape && app.op_progress && app.op_progress->active) {
    app.op_progress->cancel = true;
    app.operation_status = "Cancelling...";
    app.operation_status_expires_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
    draw(app);
    return true;
  }

  // ── Confirm dialog keys ──
  if (app.confirm_open) {
    if (sym == XKB_KEY_Escape) {
      app.confirm_hover_btn = -1;
      if (app.confirm_callback) app.confirm_callback(false);
      app.confirm_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      app.confirm_hover_btn = -1;
      if (app.confirm_callback) app.confirm_callback(true);
      app.confirm_open = false;
      draw(app);
      return true;
    }
    return true;
  }

  // ── Settings dialog keys ──
  if (app.settings_open) {
    if (app.settings_zoom_editing) {
      if (sym == XKB_KEY_Escape) {
        app.settings_zoom_editing = false;
        draw(app);
        return true;
      }
      if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        double val = std::atof(app.settings_zoom_buf.c_str());
        if (val >= 50.0 && val <= 200.0) {
          app.settings_zoom_pct = val;
          app.zoom_pct = val;
          app.entry_height = std::max(20, static_cast<int>(36.0 * app.zoom_pct / 100.0));
          int icon_sz = static_cast<int>(48.0 * app.zoom_pct / 100.0);
          app.grid_cell_size = std::max(40, icon_sz + static_cast<int>(8.0 * app.zoom_pct / 100.0));
          app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));
        }
        app.settings_zoom_editing = false;
        draw(app);
        return true;
      }
      if (sym == XKB_KEY_BackSpace) {
        if (!app.settings_zoom_buf.empty())
          app.settings_zoom_buf.pop_back();
        draw(app);
        return true;
      }
      if (utf8_len == 1 && utf8[0] >= '0' && utf8[0] <= '9') {
        if (app.settings_zoom_buf.size() < 3)
          app.settings_zoom_buf += utf8[0];
        draw(app);
        return true;
      }
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      app.settings_open = false;
      draw(app);
      return true;
    }
    if ((sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) && app.settings_dropdown_open) {
      if (app.settings_dropdown_hover >= 0) {
        app.settings_default_term_idx = app.settings_dropdown_hover + app.settings_dropdown_scroll;
        app.settings_dropdown_open = false;
      }
      draw(app);
      return true;
    }
  }

  // ── Compress dialog keys ──
  if (app.compress_dialog_open) {
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      app.compress_hover_btn = -1;
      execute_compress_async(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      app.compress_dialog_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace && !app.compress_name_buf.empty()) {
      app.compress_name_buf.pop_back();
      app.compress_name_cursor = static_cast<int>(app.compress_name_buf.size());
      draw(app);
      return true;
    }
    if (utf8 && utf8_len > 0 && utf8_len <= 4) {
      app.compress_name_buf.append(utf8, utf8_len);
      app.compress_name_cursor = static_cast<int>(app.compress_name_buf.size());
      draw(app);
      return true;
    }
    return false;
  }

  if (app.create_dialog_open) {
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (!app.create_buf.empty()) {
        fs::path new_path = fs::path(app.cur_tab().current_path) / app.create_buf;
        std::error_code ec;
        bool ok = false;
        if (app.create_is_folder) {
          ok = fs::create_directory(new_path, ec);
        } else {
          FILE* f = std::fopen(new_path.c_str(), "w");
          if (f) { ok = true; std::fclose(f); }
        }
        if (ok) {
          AppState::UndoRecord rec{app.create_is_folder
            ? AppState::UndoRecord::Type::NewFolder
            : AppState::UndoRecord::Type::NewFile, {}, {}};
          rec.paths_b.push_back(new_path.string());
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
        }
        reload_dir(app);
      }
      app.create_dialog_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      app.create_dialog_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace && !app.create_buf.empty()) {
      app.create_buf.pop_back();
      app.create_cursor_pos = static_cast<int>(app.create_buf.size());
      draw(app);
      return true;
    }
    return false;
  }

  if (app.rename_ui_open) {
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (!app.rename_ui_buf.empty() && app.rename_ui_buf != app.rename_ui_old_name) {
        fs::path src(app.rename_ui_entry_path);
        fs::path dest = src.parent_path() / app.rename_ui_buf;
        std::error_code ec;
        fs::rename(src, dest, ec);
        if (!ec) {
          AppState::UndoRecord rec{AppState::UndoRecord::Type::Rename, {}, {}};
          rec.paths_a.push_back(src.string());
          rec.paths_b.push_back(dest.string());
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
          app.operation_status = "Renamed";
          app.operation_status_expires_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::steady_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch()).count();
          reload_dir(app);
        }
      }
      app.rename_ui_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      app.rename_ui_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace && !app.rename_ui_buf.empty()) {
      if (app.rename_ui_cursor_pos > 0) {
        app.rename_ui_buf.erase(app.rename_ui_cursor_pos - 1, 1);
        --app.rename_ui_cursor_pos;
      }
      { auto _n = std::chrono::steady_clock::now(); app.key_repeat_sym = sym;
        app.key_repeat_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_n.time_since_epoch()).count();
        app.key_repeat_last_ms = app.key_repeat_start_ms; }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Delete && !app.rename_ui_buf.empty()) {
      if (app.rename_ui_cursor_pos < static_cast<int>(app.rename_ui_buf.size()))
        app.rename_ui_buf.erase(app.rename_ui_cursor_pos, 1);
      else
        app.rename_ui_buf.pop_back();
      if (app.rename_ui_cursor_pos > static_cast<int>(app.rename_ui_buf.size()))
        app.rename_ui_cursor_pos = static_cast<int>(app.rename_ui_buf.size());
      { auto _n = std::chrono::steady_clock::now(); app.key_repeat_sym = sym;
        app.key_repeat_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_n.time_since_epoch()).count();
        app.key_repeat_last_ms = app.key_repeat_start_ms; }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Left && app.rename_ui_cursor_pos > 0) {
      --app.rename_ui_cursor_pos;
      { auto _n = std::chrono::steady_clock::now(); app.key_repeat_sym = sym;
        app.key_repeat_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_n.time_since_epoch()).count();
        app.key_repeat_last_ms = app.key_repeat_start_ms; }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Right && app.rename_ui_cursor_pos < static_cast<int>(app.rename_ui_buf.size())) {
      ++app.rename_ui_cursor_pos;
      { auto _n = std::chrono::steady_clock::now(); app.key_repeat_sym = sym;
        app.key_repeat_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_n.time_since_epoch()).count();
        app.key_repeat_last_ms = app.key_repeat_start_ms; }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Home) {
      app.rename_ui_cursor_pos = 0;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_End) {
      app.rename_ui_cursor_pos = static_cast<int>(app.rename_ui_buf.size());
      draw(app);
      return true;
    }
    if (utf8 && utf8_len > 0 && utf8_len <= 4) {
      app.rename_ui_buf.insert(app.rename_ui_cursor_pos, utf8, utf8_len);
      app.rename_ui_cursor_pos += utf8_len;
      draw(app);
      return true;
    }
    return false;
  }

  if (app.batch_rename_open) {
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (!app.batch_rename_entries.empty()) {
        AppState::UndoRecord rec{AppState::UndoRecord::Type::Rename, {}, {}};
        std::error_code ec;
        int renamed = 0;
        for (const auto& e : app.batch_rename_entries) {
          if (e.new_name.empty() || e.new_name == e.old_name) continue;
          fs::path src(e.old_path);
          fs::path dest = src.parent_path() / e.new_name;
          fs::rename(src, dest, ec);
          if (!ec) {
            rec.paths_a.push_back(e.old_path);
            rec.paths_b.push_back(dest.string());
            ++renamed;
          }
        }
        if (!rec.paths_a.empty()) {
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
          app.operation_status = std::to_string(renamed) + " file" + (renamed == 1 ? "" : "s") + " renamed";
          app.operation_status_expires_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::steady_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch()).count();
          reload_dir(app);
        }
      }
      app.batch_rename_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      app.batch_rename_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
      if (app.batch_rename_mode == 1) {
        // Find/replace mode: swap between find and replace
        app.batch_rename_edit_focus = (app.batch_rename_edit_focus == 0) ? 1 : 0;
        if (app.batch_rename_edit_focus == 0)
          app.batch_rename_find_cursor = static_cast<int>(app.batch_rename_find.size());
        else
          app.batch_rename_replace_cursor = static_cast<int>(app.batch_rename_replace.size());
        draw(app);
      }
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      if (app.batch_rename_mode == 0) {
        // Template mode
        if (!app.batch_rename_template.empty()) {
          app.batch_rename_template.pop_back();
          app.batch_rename_template_cursor = static_cast<int>(app.batch_rename_template.size());
          draw(app);
        }
      } else {
        // Find/replace mode
        if (app.batch_rename_edit_focus == 0 && !app.batch_rename_find.empty()) {
          app.batch_rename_find.pop_back();
          app.batch_rename_find_cursor = static_cast<int>(app.batch_rename_find.size());
          draw(app);
          return true;
        }
        if (app.batch_rename_edit_focus == 1 && !app.batch_rename_replace.empty()) {
          app.batch_rename_replace.pop_back();
          app.batch_rename_replace_cursor = static_cast<int>(app.batch_rename_replace.size());
          draw(app);
          return true;
        }
      }
      return true;
    }
    // Text input for batch rename
    if (utf8 && utf8_len > 0 && utf8_len <= 4) {
      if (app.batch_rename_mode == 0) {
        app.batch_rename_template.append(utf8, utf8_len);
        app.batch_rename_template_cursor = static_cast<int>(app.batch_rename_template.size());
      } else {
        if (app.batch_rename_edit_focus == 0) {
          app.batch_rename_find.append(utf8, utf8_len);
          app.batch_rename_find_cursor = static_cast<int>(app.batch_rename_find.size());
        } else {
          app.batch_rename_replace.append(utf8, utf8_len);
          app.batch_rename_replace_cursor = static_cast<int>(app.batch_rename_replace.size());
        }
      }
      draw(app);
      return true;
    }
    return false;
  }

  if (app.term_chooser_open) {
    if (sym == XKB_KEY_Escape) {
      app.term_chooser_open = false;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Up) {
      if (app.term_chooser_hover <= 0 || app.term_chooser_hover > static_cast<int>(app.term_chooser_apps.size()))
        app.term_chooser_hover = static_cast<int>(app.term_chooser_apps.size()) - 1;
      else
        app.term_chooser_hover--;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Down) {
      if (app.term_chooser_hover < 0 || app.term_chooser_hover >= static_cast<int>(app.term_chooser_apps.size()) - 1)
        app.term_chooser_hover = 0;
      else
        app.term_chooser_hover++;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (app.term_chooser_hover >= 0 &&
          app.term_chooser_hover < static_cast<int>(app.term_chooser_apps.size())) {
        auto& chosen = app.term_chooser_apps[app.term_chooser_hover];
        std::string chosen_id = chosen.desktop_id;
        auto dot = chosen_id.rfind('.');
        if (dot != std::string::npos) chosen_id = chosen_id.substr(0, dot);
        auto slash = chosen_id.rfind('/');
        if (slash != std::string::npos) chosen_id = chosen_id.substr(slash + 1);
        eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
        sc.defaultApps.terminal = chosen_id;
        (void)eh::config::write_state_settings_toml(sc);
        eh::config::shell_config_apply_from_memory(std::move(sc));
        app.term_chooser_open = false;
        open_terminal_at(app, app.term_chooser_target_dir);
        draw(app);
      }
      return true;
    }
    return false;
  }

  // ── Search bar keyboard handler (local + recursive) ──
  if ((app.active_pane ? app.r_search_active : app.search_active) || (app.active_pane ? app.r_recursive_search_active : app.recursive_search_active)) {
    auto& k_search_active = app.active_pane ? app.r_search_active : app.search_active;
    auto& k_recursive_search_active = app.active_pane ? app.r_recursive_search_active : app.recursive_search_active;
    auto& k_search_query = app.active_pane ? app.r_search_query : app.search_query;
    auto& k_recursive_search_query = app.active_pane ? app.r_recursive_search_query : app.recursive_search_query;
    auto& k_search_cursor = app.active_pane ? app.r_search_cursor : app.search_cursor;
    auto& k_search_sel_start = app.active_pane ? app.r_search_sel_start : app.search_sel_start;
    auto& k_search_sel_end = app.active_pane ? app.r_search_sel_end : app.search_sel_end;
    auto trigger_search = [&] {
      bool search_from_home = k_recursive_search_active;
      k_recursive_search_query = k_search_query;
      if (!k_search_query.empty()) {
        app.cur_tab().entries.clear();
        app.cur_tab().visible_entries.clear();
        std::string root = search_from_home ? home_dir() : app.cur_tab().current_path;
        recursive_search_worker().start_search(root, k_search_query);
      } else {
        recursive_search_worker().cancel();
      }
    };

    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      reset_search_filters(app);
      bool was_recursive = k_recursive_search_active;
      k_search_active = false;
      k_recursive_search_active = false;
      k_search_query.clear();
      k_recursive_search_query.clear();
      recursive_search_worker().cancel();
      if (was_recursive)
        reload_dir(app);
      else
        reload_dir(app);
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      reset_search_filters(app);
      bool was_recursive = k_recursive_search_active;
      k_search_active = false;
      k_recursive_search_active = false;
      k_search_query.clear();
      k_recursive_search_query.clear();
      recursive_search_worker().cancel();
      if (was_recursive)
        reload_dir(app);
      else
        reload_dir(app);
      draw(app);
      return true;
    }
    if (ctrl && (sym == XKB_KEY_A || sym == XKB_KEY_a)) {
      k_search_sel_start = 0;
      k_search_sel_end = static_cast<int>(k_search_query.size());
      k_search_cursor = k_search_sel_end;
      draw(app);
      return true;
    }
    if (ctrl && (sym == XKB_KEY_C || sym == XKB_KEY_c)) {
      if (k_search_sel_start >= 0 && k_search_sel_start != k_search_sel_end) {
        int a = std::min(k_search_sel_start, k_search_sel_end);
        int b = std::max(k_search_sel_start, k_search_sel_end);
        app.clipboard.copy_text(k_search_query.substr(a, b - a));
      }
      return true;
    }
    if (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left) {
      if (k_search_cursor > 0) {
        if (shift) {
          if (k_search_sel_start < 0) k_search_sel_start = k_search_cursor;
          k_search_cursor--;
          k_search_sel_end = k_search_cursor;
        } else {
          k_search_cursor--;
          k_search_sel_start = -1;
          k_search_sel_end = -1;
        }
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right) {
      if (k_search_cursor < static_cast<int>(k_search_query.size())) {
        if (shift) {
          if (k_search_sel_start < 0) k_search_sel_start = k_search_cursor;
          k_search_cursor++;
          k_search_sel_end = k_search_cursor;
        } else {
          k_search_cursor++;
          k_search_sel_start = -1;
          k_search_sel_end = -1;
        }
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Home || sym == XKB_KEY_KP_Home) {
      if (shift) {
        if (k_search_sel_start < 0) k_search_sel_start = k_search_cursor;
        k_search_cursor = 0;
        k_search_sel_end = k_search_cursor;
      } else {
        k_search_cursor = 0;
        k_search_sel_start = -1;
        k_search_sel_end = -1;
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_End || sym == XKB_KEY_KP_End) {
      int end = static_cast<int>(k_search_query.size());
      if (shift) {
        if (k_search_sel_start < 0) k_search_sel_start = k_search_cursor;
        k_search_cursor = end;
        k_search_sel_end = k_search_cursor;
      } else {
        k_search_cursor = end;
        k_search_sel_start = -1;
        k_search_sel_end = -1;
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      if (k_search_sel_start >= 0 && k_search_sel_start != k_search_sel_end) {
        int a = std::min(k_search_sel_start, k_search_sel_end);
        int b = std::max(k_search_sel_start, k_search_sel_end);
        k_search_query.erase(a, b - a);
        k_search_cursor = a;
        k_search_sel_start = -1;
        k_search_sel_end = -1;
      } else if (k_search_cursor > 0) {
        k_search_query.erase(k_search_cursor - 1, 1);
        k_search_cursor--;
      }
      trigger_search();
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Delete || sym == XKB_KEY_KP_Delete) {
      if (k_search_sel_start >= 0 && k_search_sel_start != k_search_sel_end) {
        int a = std::min(k_search_sel_start, k_search_sel_end);
        int b = std::max(k_search_sel_start, k_search_sel_end);
        k_search_query.erase(a, b - a);
        k_search_cursor = a;
        k_search_sel_start = -1;
        k_search_sel_end = -1;
      } else if (k_search_cursor < static_cast<int>(k_search_query.size())) {
        k_search_query.erase(k_search_cursor, 1);
      }
      trigger_search();
      draw(app);
      return true;
    }
    if (utf8_len > 0 && utf8[0] >= 32) {
      if (k_search_sel_start >= 0 && k_search_sel_start != k_search_sel_end) {
        int a = std::min(k_search_sel_start, k_search_sel_end);
        int b = std::max(k_search_sel_start, k_search_sel_end);
        k_search_query.erase(a, b - a);
        k_search_cursor = a;
        k_search_sel_start = -1;
        k_search_sel_end = -1;
      }
      k_search_query.insert(static_cast<std::size_t>(k_search_cursor), utf8, utf8_len);
      k_search_cursor += utf8_len;
      if (k_search_cursor > static_cast<int>(k_search_query.size()))
        k_search_cursor = static_cast<int>(k_search_query.size());
      trigger_search();
      draw(app);
      return true;
    }
    return true;
  }

  // ── Path editing keyboard handler ──
  if (app.active_pane ? app.r_path_editing : app.path_editing) {
    auto& pe_editing = app.active_pane ? app.r_path_editing : app.path_editing;
    auto& pe_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
    auto& pe_cursor = app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor;
    auto& pe_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
    auto& pe_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
    auto& pe_dragging = app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging;
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      pe_editing = false;
      pe_dragging = false;
      std::string new_path = pe_buf;
      if (fs::exists(new_path)) {
        navigate_to(app, new_path);
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Escape) {
      pe_editing = false;
      pe_dragging = false;
      pe_sel_start = -1;
      pe_sel_end = -1;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      if (pe_sel_start >= 0 && pe_sel_start != pe_sel_end) {
        int sel_a = std::min(pe_sel_start, pe_sel_end);
        int sel_b = std::max(pe_sel_start, pe_sel_end);
        pe_buf.erase(sel_a, sel_b - sel_a);
        pe_cursor = sel_a;
      } else if (pe_cursor > 0) {
        pe_buf.erase(pe_cursor - 1, 1);
        pe_cursor--;
      }
      pe_sel_start = -1;
      pe_sel_end = -1;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Delete) {
      if (pe_sel_start >= 0 && pe_sel_start != pe_sel_end) {
        int sel_a = std::min(pe_sel_start, pe_sel_end);
        int sel_b = std::max(pe_sel_start, pe_sel_end);
        pe_buf.erase(sel_a, sel_b - sel_a);
        pe_cursor = sel_a;
      } else if (pe_cursor < static_cast<int>(pe_buf.size())) {
        pe_buf.erase(pe_cursor, 1);
      }
      pe_sel_start = -1;
      pe_sel_end = -1;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Left) {
      if (pe_cursor > 0) {
        if (shift) {
          if (pe_sel_start < 0)
            pe_sel_start = pe_cursor;
          pe_cursor--;
          pe_sel_end = pe_cursor;
        } else {
          pe_cursor--;
          pe_sel_start = -1;
          pe_sel_end = -1;
        }
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Right) {
      if (pe_cursor < static_cast<int>(pe_buf.size())) {
        if (shift) {
          if (pe_sel_start < 0)
            pe_sel_start = pe_cursor;
          pe_cursor++;
          pe_sel_end = pe_cursor;
        } else {
          pe_cursor++;
          pe_sel_start = -1;
          pe_sel_end = -1;
        }
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Home) {
      if (shift) {
        if (pe_sel_start < 0)
          pe_sel_start = pe_cursor;
        pe_cursor = 0;
        pe_sel_end = 0;
      } else {
        pe_cursor = 0;
        pe_sel_start = -1;
        pe_sel_end = -1;
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_End) {
      int end_pos = static_cast<int>(pe_buf.size());
      if (shift) {
        if (pe_sel_start < 0)
          pe_sel_start = pe_cursor;
        pe_cursor = end_pos;
        pe_sel_end = end_pos;
      } else {
        pe_cursor = end_pos;
        pe_sel_start = -1;
        pe_sel_end = -1;
      }
      draw(app);
      return true;
    }
    if (ctrl && (sym == XKB_KEY_A || sym == XKB_KEY_a)) {
      pe_sel_start = 0;
      pe_sel_end = static_cast<int>(pe_buf.size());
      pe_cursor = pe_sel_end;
      draw(app);
      return true;
    }
    if (ctrl && (sym == XKB_KEY_C || sym == XKB_KEY_c)) {
      if (pe_sel_start >= 0 && pe_sel_start != pe_sel_end) {
        int sel_a = std::min(pe_sel_start, pe_sel_end);
        int sel_b = std::max(pe_sel_start, pe_sel_end);
        std::string selected = pe_buf.substr(sel_a, sel_b - sel_a);
        if (!selected.empty()) {
          app.clipboard.copy_text(selected);
        }
      }
      draw(app);
      return true;
    }
    if (utf8 && utf8_len > 0 && !ctrl && !alt) {
      if (pe_sel_start >= 0 && pe_sel_start != pe_sel_end) {
        int sel_a = std::min(pe_sel_start, pe_sel_end);
        int sel_b = std::max(pe_sel_start, pe_sel_end);
        pe_buf.erase(sel_a, sel_b - sel_a);
        pe_cursor = sel_a;
        pe_sel_start = -1;
        pe_sel_end = -1;
      }
      pe_buf.insert(static_cast<std::size_t>(pe_cursor), utf8, utf8_len);
      pe_cursor += utf8_len;
      draw(app);
      return true;
    }
    return true;
  }

  // ── Tab shortcuts ──
  if (ctrl && (sym == XKB_KEY_T || sym == XKB_KEY_t)) {
    new_tab(app);
    draw(app);
    return true;
  }
  if (ctrl && (sym == XKB_KEY_W || sym == XKB_KEY_w)) {
    close_tab(app);
    draw(app);
    return true;
  }
  if (ctrl && (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab)) {
    if (shift)
      prev_tab(app);
    else
      next_tab(app);
    draw(app);
    return true;
  }

  if (ctrl && shift && (sym == XKB_KEY_N || sym == XKB_KEY_n)) {
    app.create_dialog_open = true;
    app.create_is_folder = true;
    app.create_buf = "New Folder";
    app.create_cursor_pos = static_cast<int>(app.create_buf.size());
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_C || sym == XKB_KEY_c)) {
    if (!app.cur_tab().multi_selected.empty()) {
      std::vector<std::string> paths;
      for (int vis_idx : app.cur_tab().multi_selected) {
        if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
          int real_idx = app.cur_tab().visible_entries[vis_idx];
          if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()))
            paths.push_back(app.cur_tab().entries[real_idx].path);
        }
      }
      if (!paths.empty()) app.clipboard.copy_files(false, paths);
    } else if (app.cur_tab().selected_idx >= 0 &&
               app.cur_tab().selected_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.cur_tab().selected_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()))
        app.clipboard.copy_files(false, {app.cur_tab().entries[real_idx].path});
    }
    return true;
  }

  if (ctrl && (sym == XKB_KEY_X || sym == XKB_KEY_x)) {
    if (!app.cur_tab().multi_selected.empty()) {
      std::vector<std::string> paths;
      for (int vis_idx : app.cur_tab().multi_selected) {
        if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
          int real_idx = app.cur_tab().visible_entries[vis_idx];
          if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()))
            paths.push_back(app.cur_tab().entries[real_idx].path);
        }
      }
      if (!paths.empty()) app.clipboard.copy_files(true, paths);
    } else if (app.cur_tab().selected_idx >= 0 &&
               app.cur_tab().selected_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.cur_tab().selected_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()))
        app.clipboard.copy_files(true, {app.cur_tab().entries[real_idx].path});
    }
    return true;
  }

  if (ctrl && !shift && (sym == XKB_KEY_Z || sym == XKB_KEY_z)) {
    if (app.undo_stack.empty()) return true;
    auto rec = std::move(app.undo_stack.back());
    app.undo_stack.pop_back();
    std::error_code ec;
    switch (rec.type) {
      case AppState::UndoRecord::Type::PasteCopy:
        for (const auto& path : rec.paths_b) {
          if (fs::is_directory(path, ec)) fs::remove_all(path, ec);
          else fs::remove(path, ec);
        }
        break;
      case AppState::UndoRecord::Type::PasteCut:
        for (std::size_t i = 0; i < rec.paths_b.size() && i < rec.paths_a.size(); ++i) {
          fs::rename(rec.paths_b[i], rec.paths_a[i], ec);
        }
        break;
      case AppState::UndoRecord::Type::Rename:
        for (std::size_t i = 0; i < rec.paths_b.size() && i < rec.paths_a.size(); ++i) {
          fs::rename(rec.paths_b[i], rec.paths_a[i], ec);
        }
        break;
      case AppState::UndoRecord::Type::NewFolder:
        for (const auto& path : rec.paths_b) {
          fs::remove_all(path, ec);
        }
        break;
      case AppState::UndoRecord::Type::NewFile:
        for (const auto& path : rec.paths_b) {
          fs::remove(path, ec);
        }
        break;
      default:
        break;
    }
    app.redo_stack.push_back(std::move(rec));
    if (app.redo_stack.size() > app.kMaxUndo)
      app.redo_stack.erase(app.redo_stack.begin());
    reload_dir(app);
    draw(app);
    return true;
  }

  if (ctrl && ((sym == XKB_KEY_Y || sym == XKB_KEY_y) || (shift && (sym == XKB_KEY_Z || sym == XKB_KEY_z)))) {
    if (app.redo_stack.empty()) return true;
    auto rec = std::move(app.redo_stack.back());
    app.redo_stack.pop_back();
    std::error_code ec;
    switch (rec.type) {
      case AppState::UndoRecord::Type::PasteCopy:
        for (std::size_t i = 0; i < rec.paths_a.size() && i < rec.paths_b.size(); ++i) {
          if (!fs::exists(rec.paths_a[i], ec)) continue;
          fs::path dest = rec.paths_b[i];
          if (fs::is_directory(rec.paths_a[i], ec))
            fs::copy(rec.paths_a[i], dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
          else
            fs::copy_file(rec.paths_a[i], dest, fs::copy_options::copy_symlinks, ec);
        }
        break;
      case AppState::UndoRecord::Type::PasteCut:
        for (std::size_t i = 0; i < rec.paths_a.size() && i < rec.paths_b.size(); ++i) {
          fs::rename(rec.paths_a[i], rec.paths_b[i], ec);
        }
        break;
      case AppState::UndoRecord::Type::Rename:
        for (std::size_t i = 0; i < rec.paths_a.size() && i < rec.paths_b.size(); ++i) {
          fs::rename(rec.paths_a[i], rec.paths_b[i], ec);
        }
        break;
      case AppState::UndoRecord::Type::NewFolder:
        for (const auto& path : rec.paths_b) {
          fs::create_directory(path, ec);
        }
        break;
      case AppState::UndoRecord::Type::NewFile:
        for (const auto& path : rec.paths_b) {
          std::ofstream ofs(path);
          ofs.close();
        }
        break;
      default:
        break;
    }
    app.undo_stack.push_back(std::move(rec));
    if (app.undo_stack.size() > app.kMaxUndo)
      app.undo_stack.erase(app.undo_stack.begin());
    reload_dir(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_V || sym == XKB_KEY_v)) {
    auto cf = app.clipboard.read_files(app.wl.display());
    if (!cf.paths.empty()) {
      std::error_code ec;
      fs::path dest(app.cur_tab().current_path);
      AppState::UndoRecord rec{cf.is_cut ? AppState::UndoRecord::Type::PasteCut
                                         : AppState::UndoRecord::Type::PasteCopy, {}, {}};
      if (cf.is_cut) rec.paths_a = cf.paths;

      // Resolve target paths first (synchronous)
      std::vector<std::string> src_paths;
      std::vector<std::string> dst_paths;
      for (const auto& src : cf.paths) {
        fs::path sp(src);
        if (!fs::exists(sp, ec)) continue;
        fs::path target = dest / sp.filename();
        int n = 2;
        while (fs::exists(target, ec)) {
          target = dest / (sp.stem().string() + " (" + std::to_string(n) + ")" + sp.extension().string());
          n++;
        }
        src_paths.push_back(src);
        dst_paths.push_back(target.string());
      }

      // Push undo record with resolved paths
      if (!dst_paths.empty()) {
        rec.paths_b = dst_paths;
        app.redo_stack.clear();
        app.undo_stack.push_back(std::move(rec));
        if (app.undo_stack.size() > app.kMaxUndo)
          app.undo_stack.erase(app.undo_stack.begin());
      }

      // Start async background copy/move
      bool is_cut = cf.is_cut;
      auto prog = std::make_shared<OperationProgress>();
      prog->type = is_cut ? OperationType::Move : OperationType::Copy;
      start_async_op(src_paths, dest.string(), is_cut, prog,
          [&app, is_cut](bool cancelled) {
            if (!cancelled) {
              app.operation_status = is_cut ? "Moved" : "Pasted";
              app.operation_status_expires_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
            }
            app.op_progress.reset();
            reload_dir(app);
            draw(app);
          });
      app.op_progress = prog;
    } else {
      reload_dir(app);
      draw(app);
    }
    return true;
  }

  if (ctrl && (sym == XKB_KEY_A || sym == XKB_KEY_a)) {
    app.cur_tab().multi_selected.clear();
    for (int i = 0; i < static_cast<int>(app.cur_tab().visible_entries.size()); ++i) {
      app.cur_tab().multi_selected.push_back(i);
    }
    if (!app.cur_tab().visible_entries.empty()) app.cur_tab().selected_idx = 0;
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_H || sym == XKB_KEY_h)) {
    app.show_hidden = !app.show_hidden;
    save_file_browser_settings(app);
    reload_dir(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_L || sym == XKB_KEY_l)) {
    auto& pe_editing = app.active_pane ? app.r_path_editing : app.path_editing;
    auto& pe_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
    auto& pe_cursor = app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor;
    auto& pe_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
    auto& pe_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
    pe_editing = true;
    pe_buf = app.cur_tab().current_path;
    pe_cursor = static_cast<int>(pe_buf.size());
    pe_sel_start = -1;
    pe_sel_end = -1;
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_F || sym == XKB_KEY_f)) {
    if ((app.active_pane ? app.r_search_active : app.search_active) || (app.active_pane ? app.r_recursive_search_active : app.recursive_search_active)) {
      reset_search_filters(app);
      (app.active_pane ? app.r_search_active : app.search_active) = false;
      (app.active_pane ? app.r_recursive_search_active : app.recursive_search_active) = false;
      (app.active_pane ? app.r_search_query : app.search_query).clear();
      (app.active_pane ? app.r_recursive_search_query : app.recursive_search_query).clear();
      recursive_search_worker().cancel();
      reload_dir(app);
    } else {
      (app.active_pane ? app.r_search_active : app.search_active) = true;
      (app.active_pane ? app.r_recursive_search_active : app.recursive_search_active) = false;
      (app.active_pane ? app.r_search_query : app.search_query).clear();
      (app.active_pane ? app.r_recursive_search_query : app.recursive_search_query).clear();
      recursive_search_worker().cancel();
      (app.active_pane ? app.r_search_cursor : app.search_cursor) = 0;
      (app.active_pane ? app.r_search_sel_start : app.search_sel_start) = -1;
      (app.active_pane ? app.r_search_sel_end : app.search_sel_end) = -1;
    }
    (app.active_pane ? app.r_path_editing : app.path_editing) = false;
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_1)) {
    app.cur_tab().view_mode = ViewMode::List;
    save_file_browser_settings(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_2)) {
    app.cur_tab().view_mode = ViewMode::Grid;
    save_file_browser_settings(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_3)) {
    app.cur_tab().current_path = "computer://";
    navigate_to(app, "computer://");
    return true;
  }

  if (ctrl && (sym == XKB_KEY_4)) {
    app.cur_tab().view_mode = ViewMode::Tree;
    save_file_browser_settings(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_5)) {
    app.cur_tab().view_mode = ViewMode::Compact;
    save_file_browser_settings(app);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_G || sym == XKB_KEY_g)) {
    app.cur_tab().group_by_type = !app.cur_tab().group_by_type;
    reload_dir(app);
    save_file_browser_settings(app);
    draw(app);
    return true;
  }

  auto zoom_fn = [&](double pct) {
    app.settings_zoom_pct = std::clamp(pct, 50.0, 200.0);
    app.zoom_pct = app.settings_zoom_pct;
    app.entry_height = std::max(20, static_cast<int>(36.0 * app.zoom_pct / 100.0));
    int icon_sz = static_cast<int>(48.0 * app.zoom_pct / 100.0);
    app.grid_cell_size = std::max(40, icon_sz + static_cast<int>(8.0 * app.zoom_pct / 100.0));
    app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));
  };

  if (ctrl && (sym == XKB_KEY_equal || sym == XKB_KEY_KP_Add)) {
    zoom_fn(app.settings_zoom_pct + 10.0);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_minus || sym == XKB_KEY_KP_Subtract)) {
    zoom_fn(app.settings_zoom_pct - 10.0);
    draw(app);
    return true;
  }

  if (ctrl && (sym == XKB_KEY_0 || sym == XKB_KEY_KP_0)) {
    zoom_fn(100.0);
    draw(app);
    return true;
  }

  if (alt && (sym == XKB_KEY_Up || sym == XKB_KEY_KP_Up)) {
    navigate_up(app);
    return true;
  }

  if (alt && (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left)) {
    if (!app.cur_tab().nav_history.empty()) {
      navigate_back(app);
      return true;
    }
  }

  if (alt && (sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right)) {
    if (!app.cur_tab().nav_forward.empty()) {
      navigate_forward(app);
      return true;
    }
  }

  if (sym == XKB_KEY_F3) {
    app.split_view = !app.split_view;
    if (app.split_view) {
      app.right_pane = app.cur_tab();
      app.split_divider_x = app.width / 2;
      app.active_pane = 0;
    } else {
      app.active_pane = 0;
    }
    draw(app);
    return true;
  }

  if (sym == XKB_KEY_F5) {
    reload_dir(app);
    draw(app);
    return true;
  }

  if (sym == XKB_KEY_F11) {
    app.info_panel_open = !app.info_panel_open;
    if (app.info_panel_open) app.info_panel_needs_update = true;
    draw(app);
    return true;
  }

  switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      open_selected(app);
      draw(app);
      return true;

    case XKB_KEY_BackSpace:
      navigate_up(app);
      return true;

    case XKB_KEY_space:
      toggle_space_preview(app);
      draw(app);
      return true;

    case XKB_KEY_Left: {
      // Tree view: collapse expanded directory
      if (app.cur_tab().view_mode == ViewMode::Tree) {
        auto& tab = app.cur_tab();
        if (tab.selected_idx >= 0 && tab.selected_idx < static_cast<int>(tab.tree_entries.size())) {
          auto& te = tab.tree_entries[tab.selected_idx];
          if (te.is_dir && tab.tree_expanded.count(te.path)) {
            tab.tree_expanded.erase(te.path);
            build_tree_entries(app);
          } else {
            // Navigate to parent: find an entry with lower depth
            for (int i = tab.selected_idx - 1; i >= 0; --i) {
              if (tab.tree_entries[i].depth < te.depth) {
                tab.selected_idx = i;
                tab.multi_selected = {i};
                break;
              }
            }
          }
        }
        draw(app);
        return true;
      }
      if (app.preview_mode == AppState::PreviewMode::Space) {
        int idx = std::max(0, app.cur_tab().selected_idx - 1);
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        activate_space_preview(app);
        draw(app);
        return true;
      }
      int idx = app.cur_tab().selected_idx >= 0 ? app.cur_tab().selected_idx : 0;
      idx = std::max(0, idx - 1);
      app.cur_tab().selected_idx = idx;
      app.cur_tab().multi_selected = {idx};
      if (app.cur_tab().view_mode == ViewMode::Grid) {
        double zf = app.zoom_pct / 100.0;
        int icon_size = std::max(16, static_cast<int>(48.0 * zf));
        int label_h = static_cast<int>(20.0 * zf);
        int cell_size = app.grid_cell_size;
        int gap = app.grid_cell_gap;
        int row_h = icon_size + 8 + 4 + label_h + gap;
        int top_gap = gap;
        int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int content_w = app.width - sidebar_w;
        int cols = std::max(1, (content_w - gap) / (cell_size + gap));
        int target_line = top_gap + (idx / cols) * row_h;
        if (target_line < app.cur_tab().scroll_px)
          app.cur_tab().scroll_px = target_line;
      } else {
        int target_line = entry_top(app, idx);
        if (target_line < app.cur_tab().scroll_px)
          app.cur_tab().scroll_px = target_line;
      }
      draw(app);
      return true;
    }

    case XKB_KEY_Right: {
      // Tree view: expand collapsed directory
      if (app.cur_tab().view_mode == ViewMode::Tree) {
        auto& tab = app.cur_tab();
        if (tab.selected_idx >= 0 && tab.selected_idx < static_cast<int>(tab.tree_entries.size())) {
          auto& te = tab.tree_entries[tab.selected_idx];
          if (te.is_dir && !tab.tree_expanded.count(te.path)) {
            tab.tree_expanded.insert(te.path);
            build_tree_entries(app);
            draw(app);
          } else {
            open_selected(app);
          }
        }
        draw(app);
        return true;
      }
      if (app.preview_mode == AppState::PreviewMode::Space) {
        int idx = app.cur_tab().selected_idx + 1;
        int max_i = static_cast<int>(app.cur_tab().visible_entries.size()) - 1;
        if (idx > max_i) idx = max_i;
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        activate_space_preview(app);
        draw(app);
        return true;
      }
      int idx = app.cur_tab().selected_idx >= 0 ? app.cur_tab().selected_idx : -1;
      idx = std::min(static_cast<int>(app.cur_tab().visible_entries.size()) - 1, idx + 1);
      app.cur_tab().selected_idx = idx;
      app.cur_tab().multi_selected = {idx};
      if (app.cur_tab().view_mode == ViewMode::Grid) {
        double zf = app.zoom_pct / 100.0;
        int icon_size = std::max(16, static_cast<int>(48.0 * zf));
        int label_h = static_cast<int>(20.0 * zf);
        int cell_size = app.grid_cell_size;
        int gap = app.grid_cell_gap;
        int row_h = icon_size + 8 + 4 + label_h + gap;
        int top_gap = gap;
        int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int content_w = app.width - sidebar_w;
        int cols = std::max(1, (content_w - gap) / (cell_size + gap));
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        int target_line = top_gap + (idx / cols + 1) * row_h;
        if (target_line > app.cur_tab().scroll_px + view_h)
          app.cur_tab().scroll_px = target_line - view_h;
      } else {
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        int target_line = entry_bottom(app, idx);
        if (target_line > app.cur_tab().scroll_px + view_h)
          app.cur_tab().scroll_px = target_line - view_h;
      }
      draw(app);
      return true;
    }

    case XKB_KEY_Up: {
      // Tree view: navigate tree_entries
      if (app.cur_tab().view_mode == ViewMode::Tree) {
        auto& tab = app.cur_tab();
        int idx = tab.selected_idx >= 0 ? tab.selected_idx : 0;
        idx = std::max(0, idx - 1);
        tab.selected_idx = idx;
        tab.multi_selected = {idx};
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        double zf = app.zoom_pct / 100.0;
        int entry_h = static_cast<int>(28.0 * zf);
        int target_line = idx * entry_h;
        if (target_line < tab.scroll_px)
          tab.scroll_px = target_line;
        draw(app);
        return true;
      }
      int idx = app.cur_tab().selected_idx >= 0 ? app.cur_tab().selected_idx : 0;
      if (app.cur_tab().view_mode == ViewMode::Grid) {
        int cell_size = app.grid_cell_size;
        int gap = app.grid_cell_gap;
        double zf = app.zoom_pct / 100.0;
        int icon_size = std::max(16, static_cast<int>(48.0 * zf));
        int label_h = static_cast<int>(20.0 * zf);
        int row_h = icon_size + 8 + 4 + label_h + gap;
        int top_gap = gap;
        int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int content_w = app.width - sidebar_w;
        int cols = std::max(1, (content_w - gap) / (cell_size + gap));
        idx = std::max(0, idx - cols);
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        int target_line = top_gap + (idx / cols) * row_h;
        if (target_line < app.cur_tab().scroll_px)
          app.cur_tab().scroll_px = target_line;
      } else {
        idx = std::max(0, idx - 1);
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        int target_line = entry_top(app, idx);
        if (target_line < app.cur_tab().scroll_px)
          app.cur_tab().scroll_px = target_line;
      }
      draw(app);
      return true;
    }

    case XKB_KEY_Down: {
      // Tree view: navigate tree_entries
      if (app.cur_tab().view_mode == ViewMode::Tree) {
        auto& tab = app.cur_tab();
        int idx = tab.selected_idx >= 0 ? tab.selected_idx : -1;
        int max_idx = static_cast<int>(tab.tree_entries.size()) - 1;
        idx = std::min(max_idx, idx + 1);
        tab.selected_idx = idx;
        tab.multi_selected = {idx};
        double zf = app.zoom_pct / 100.0;
        int entry_h = static_cast<int>(28.0 * zf);
        int target_line = (idx + 1) * entry_h;
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        if (target_line > tab.scroll_px + view_h)
          tab.scroll_px = target_line - view_h;
        draw(app);
        return true;
      }
      int idx = app.cur_tab().selected_idx >= 0 ? app.cur_tab().selected_idx : -1;
      int max_idx = static_cast<int>(app.cur_tab().visible_entries.size()) - 1;
      if (app.cur_tab().view_mode == ViewMode::Grid) {
        int cell_size = app.grid_cell_size;
        int gap = app.grid_cell_gap;
        double zf = app.zoom_pct / 100.0;
        int icon_size = std::max(16, static_cast<int>(48.0 * zf));
        int label_h = static_cast<int>(20.0 * zf);
        int row_h = icon_size + 8 + 4 + label_h + gap;
        int top_gap = gap;
        int sidebar_w = app.sidebar_expanded ? app.sidebar_width : 0;
        int content_w = app.width - sidebar_w;
        int cols = std::max(1, (content_w - gap) / (cell_size + gap));
        idx = std::min(max_idx, idx + cols);
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        int target_line = top_gap + (idx / cols + 1) * row_h;
        if (target_line > app.cur_tab().scroll_px + view_h)
          app.cur_tab().scroll_px = target_line - view_h;
      } else {
        idx = std::min(max_idx, idx + 1);
        app.cur_tab().selected_idx = idx;
        app.cur_tab().multi_selected = {idx};
        int target_line = entry_bottom(app, idx);
        int view_h = app.height - app.top_bar_height - app.tab_bar_height - app.status_bar_height;
        if (target_line > app.cur_tab().scroll_px + view_h)
          app.cur_tab().scroll_px = target_line - view_h;
      }
      draw(app);
      return true;
    }

    case XKB_KEY_F2: {
      auto targets = app.cur_tab().multi_selected.empty()
        ? std::vector<int>{}
        : app.cur_tab().multi_selected;
      if (!targets.empty()) {
        // Batch rename
        app.batch_rename_entries.clear();
        for (int vis_idx : targets) {
          if (vis_idx < 0 || vis_idx >= static_cast<int>(app.cur_tab().visible_entries.size())) continue;
          int real_idx = app.cur_tab().visible_entries[vis_idx];
          if (real_idx < 0 || real_idx >= static_cast<int>(app.cur_tab().entries.size())) continue;
          const auto& e = app.cur_tab().entries[real_idx];
          std::string ext;
          std::string base;
          auto dot = e.name.rfind('.');
          if (dot == std::string::npos || dot == 0) {
            ext.clear();
            base = e.name;
          } else {
            ext = e.name.substr(dot);
            base = e.name.substr(0, dot);
          }
          app.batch_rename_entries.push_back({e.path, e.name, ext, e.name});
        }
        if (app.batch_rename_entries.empty()) return true;
        app.batch_rename_mode = 0;
        app.batch_rename_template = "[Original filename]";
        app.batch_rename_template_cursor = static_cast<int>(app.batch_rename_template.size());
        app.batch_rename_find.clear();
        app.batch_rename_find_cursor = 0;
        app.batch_rename_replace.clear();
        app.batch_rename_replace_cursor = 0;
        app.batch_rename_show_add = false;
        app.batch_rename_add_hover = -1;
        app.batch_rename_hover_btn = -1;
        app.batch_rename_hover_mode = -1;
        app.batch_rename_edit_focus = 0;
        app.batch_rename_open = true;
        draw(app);
        return true;
      }
      // Single rename (existing behavior)
      if (app.cur_tab().selected_idx < 0 ||
          app.cur_tab().selected_idx >= static_cast<int>(app.cur_tab().visible_entries.size()))
        return true;
      int real_idx = app.cur_tab().visible_entries[app.cur_tab().selected_idx];
      if (real_idx < 0 || real_idx >= static_cast<int>(app.cur_tab().entries.size()))
        return true;
      auto& entry = app.cur_tab().entries[real_idx];
      app.rename_ui_open = true;
      app.rename_ui_old_name = entry.name;
      app.rename_ui_buf = entry.name;
      app.rename_ui_cursor_pos = static_cast<int>(entry.name.size());
      app.rename_ui_entry_path = entry.path;
      draw(app);
      return true;
    }

    case XKB_KEY_Delete: {
      auto targets = app.cur_tab().multi_selected.empty()
        ? std::vector<int>{app.cur_tab().selected_idx}
        : app.cur_tab().multi_selected;
      std::vector<std::string> del_paths;
      for (int vis_idx : targets) {
        if (vis_idx < 0 || vis_idx >= static_cast<int>(app.cur_tab().visible_entries.size()))
          continue;
        int real_idx = app.cur_tab().visible_entries[vis_idx];
        if (real_idx < 0 || real_idx >= static_cast<int>(app.cur_tab().entries.size()))
          continue;
        del_paths.push_back(app.cur_tab().entries[real_idx].path);
      }
      if (del_paths.empty()) return true;
      if (shift) {
        app.confirm_title = "Permanently Delete?";
        app.confirm_message = (del_paths.size() == 1)
          ? "Permanently delete \"" + fs::path(del_paths[0]).filename().string() + "\"?"
          : "Permanently delete " + std::to_string(del_paths.size()) + " items?";
        app.confirm_preview_path = del_paths.size() == 1 ? del_paths[0] : "";
        app.confirm_item_count = static_cast<int>(del_paths.size());
        app.confirm_callback = [&app, paths = std::move(del_paths)](bool ok) {
          if (!ok) return;
          std::error_code ec;
          for (const auto& p : paths) {
            bool is_dir = fs::is_directory(p, ec);
            if (is_dir) fs::remove_all(p, ec); else fs::remove(p, ec);
          }
          reload_dir(app);
        };
      } else {
        app.confirm_title = "Move to Trash";
        app.confirm_message = (del_paths.size() == 1)
          ? "Move \"" + fs::path(del_paths[0]).filename().string() + "\" to trash?"
          : "Move " + std::to_string(del_paths.size()) + " items to trash?";
        app.confirm_preview_path = del_paths.size() == 1 ? del_paths[0] : "";
        app.confirm_item_count = static_cast<int>(del_paths.size());
        app.confirm_callback = [&app, paths = std::move(del_paths)](bool ok) {
          if (!ok) return;
          for (const auto& p : paths) (void)xdg::trash_file(p);
          reload_dir(app);
        };
      }
      app.confirm_hover_btn = -1;
      app.confirm_open = true;
      draw(app);
      return true;
    }
  }

  if (app.create_dialog_open && utf8 && utf8_len > 0 && utf8_len <= 4) {
    app.create_buf.append(utf8, utf8_len);
    app.create_cursor_pos = static_cast<int>(app.create_buf.size());
    draw(app);
    return true;
  }

  if (app.rename_ui_open && utf8 && utf8_len > 0 && utf8_len <= 4) {
    app.rename_ui_buf.append(utf8, utf8_len);
    app.rename_ui_cursor_pos = static_cast<int>(app.rename_ui_buf.size());
    draw(app);
    return true;
  }

  // ── Type-to-find: printable character activates search bar ──
  if (!(app.active_pane ? app.r_search_active : app.search_active) && !(app.active_pane ? app.r_path_editing : app.path_editing) && !app.settings_open &&
      !app.confirm_open && !app.create_dialog_open && !app.rename_ui_open &&
      !app.properties.open && !app.open_with_open && !app.compress_dialog_open &&
      !app.batch_rename_open && !app.settings_dropdown_open &&
      (app.active_pane ? !app.r_sort_menu_open : !app.sort_menu_open) && !app.context_menu_open &&
      !ctrl && !alt && utf8_len == 1 && utf8[0] >= 32 && utf8[0] < 127 &&
      app.cur_tab().current_path != "computer://") {
    auto& active = app.active_pane ? app.r_search_active : app.search_active;
    auto& recursive = app.active_pane ? app.r_recursive_search_active : app.recursive_search_active;
    auto& query = app.active_pane ? app.r_search_query : app.search_query;
    auto& rec_query = app.active_pane ? app.r_recursive_search_query : app.recursive_search_query;
    auto& cursor = app.active_pane ? app.r_search_cursor : app.search_cursor;
    auto& sel_start = app.active_pane ? app.r_search_sel_start : app.search_sel_start;
    auto& sel_end = app.active_pane ? app.r_search_sel_end : app.search_sel_end;
    active = true;
    recursive = false;
    query.clear();
    rec_query.clear();
    cursor = 0;
    sel_start = -1;
    sel_end = -1;
    query += utf8[0];
    cursor = 1;
    app.cur_tab().entries.clear();
    app.cur_tab().visible_entries.clear();
    recursive_search_worker().start_search(app.cur_tab().current_path, query);
    draw(app);
    return true;
  }

  // ── Space preview dismiss ──
  if (app.preview_mode == AppState::PreviewMode::Space && sym == XKB_KEY_Escape) {
    reset_preview(app);
    draw(app);
    return true;
  }

  return false;
}

void handle_pointer_release(AppState& app, int x, int y, int button) {
  (void)x;
  (void)y;
  (void)button;
  if (app.split_divider_dragging) {
    app.split_divider_dragging = false;
    return;
  }
  if (app.col_resizing >= 0) {
    app.col_resizing = -1;
    draw(app);
    return;
  }
  if (button == 0x110 && app.marquee_active) {
    // Finalize marquee selection
    app.marquee_active = false;
    if (app.cur_tab().multi_selected.empty()) {
      app.cur_tab().selected_idx = -1;
    }
    draw(app);
    return;
  }
  if ((app.active_pane ? app.r_path_editing : app.path_editing) && (app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging)) {
    auto& rel_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
    auto& rel_cursor = app.active_pane ? app.r_path_edit_cursor : app.path_edit_cursor;
    auto& rel_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
    auto& rel_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
    auto& rel_dragging = app.active_pane ? app.r_path_edit_dragging : app.path_edit_dragging;
    rel_dragging = false;
    if (rel_sel_start >= 0 &&
        rel_sel_start != rel_sel_end) {
      int sel_a = std::min(rel_sel_start, rel_sel_end);
      int sel_b = std::max(rel_sel_start, rel_sel_end);
      std::string sel = rel_buf.substr(sel_a, sel_b - sel_a);
      if (!sel.empty()) app.clipboard.copy_text(sel);
    }
  }
  if (app.sidebar_dragging) {
    app.sidebar_dragging = false;
  }
  if (app.sidebar_fav_dragging) {
    if (app.sidebar_fav_drag_from >= 0 && app.sidebar_fav_drag_to >= 0 &&
        app.sidebar_fav_drag_to != app.sidebar_fav_drag_from &&
        app.sidebar_fav_drag_to < static_cast<int>(app.favorites.size())) {
      std::string path = app.favorites[app.sidebar_fav_drag_from];
      app.favorites.erase(app.favorites.begin() + app.sidebar_fav_drag_from);
      app.favorites.insert(app.favorites.begin() + app.sidebar_fav_drag_to, path);
      save_file_browser_settings(app);
      refresh_sidebar(app);
    }
    app.sidebar_fav_dragging = false;
    app.sidebar_fav_drag_from = -1;
    app.sidebar_fav_drag_to = -1;
    app.sidebar_fav_drag_to_visual = -1;
    draw(app);
    return;
  }
  if (app.tab_dragging) {
    if (app.tab_drag_from >= 0 && app.tab_drag_to >= 0 &&
        app.tab_drag_to != app.tab_drag_from &&
        app.tab_drag_to < static_cast<int>(app.tabs.size())) {
      // Reorder tabs
      int from = app.tab_drag_from;
      int to = app.tab_drag_to;
      // Adjust active tab index
      if (app.active_tab == from) {
        app.active_tab = to;
      } else if (from < app.active_tab && to >= app.active_tab) {
        app.active_tab--;
      } else if (from > app.active_tab && to <= app.active_tab) {
        app.active_tab++;
      }
      Tab tab = std::move(app.tabs[from]);
      app.tabs.erase(app.tabs.begin() + from);
      app.tabs.insert(app.tabs.begin() + to, std::move(tab));
    }
    app.tab_dragging = false;
    app.tab_drag_from = -1;
    app.tab_drag_to = -1;
    app.tab_drag_to_visual = -1;
    draw(app);
    return;
  }
  if (app.tab_drag_from >= 0) {
    // Drag didn't start yet — cancel potential
    app.tab_drag_from = -1;
  }
  if (app.sidebar_fav_drag_from >= 0) {
    // Drag didn't start yet — cancel potential
    app.sidebar_fav_drag_from = -1;
    app.sidebar_fav_drag_to = -1;
    app.sidebar_fav_drag_to_visual = -1;
  }
  if (app.drag_potential) {
    cancel_drag(app);
  }
}

} // namespace eh::file_browser
