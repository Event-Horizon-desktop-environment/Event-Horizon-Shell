#include "desktop_shell/desktop/icons/desktop_menu.hpp"

#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"
#include "desktop_shell/desktop/core/desktop_preferences.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include "desktop_shell/shared/core/app_launch.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

#include "ux/settings/common/embed/settings_embed.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"

#include "configuration/shell_config.hpp"

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <unistd.h>
#include <sys/wait.h>

namespace eh::shell::desktop {

using eh::shell::shared::path_rounded_rect;

namespace {

const char* row_label(DesktopMenuPanel p, int row);
int menu_row_count(DesktopMenuPanel p);
bool row_is_separator(DesktopMenuPanel p, int row);
bool row_is_submenu_chevron(DesktopMenuPanel p, int row);
bool cascade_is_open(const DesktopApp& app);

double cairo_utf8_advance(cairo_t* cr, const char* utf8, double font_size) {
   
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, font_size);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, utf8, &ex);
  return ex.x_advance;
}

double measure_panel_width(DesktopMenuPanel panel) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kPad_loc = 10.0 * us;
  const double kTextLeft_loc = kPad_loc + 22.0 * us;
  const double kChevronGap_loc = 8.0 * us;
  const double kMinMenuWidth_loc = 120.0 * us;
  const double kMenuRightPad_loc = 6.0 * us;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(surf);
  double maxw = kMinMenuWidth_loc;
  const int n = menu_row_count(panel);
  for (int i = 0; i < n; ++i) {
    if (row_is_separator(panel, i)) continue;
    const char* lab = row_label(panel, i);
    if (!lab || !*lab) continue;
    const double adv = cairo_utf8_advance(cr, lab, 13.0 * us);
    double rowW = kTextLeft_loc + adv + kPad_loc;
    if (panel == DesktopMenuPanel::Main && row_is_submenu_chevron(panel, i)) {
      const double chev = cairo_utf8_advance(cr, "\xE2\x80\xBA", 14.0 * us);
      rowW = kTextLeft_loc + adv + kChevronGap_loc + chev + kPad_loc;
    }
    maxw = std::max(maxw, rowW);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return maxw + kMenuRightPad_loc;
}

void refresh_desktop_menu_widths(DesktopApp& app) {
   
  app.desktopMenuMainWidth = measure_panel_width(DesktopMenuPanel::Main);
  if (cascade_is_open(app)) app.desktopMenuCascadeWidth = measure_panel_width(app.desktopMenuCascade);
}

int menu_row_count(DesktopMenuPanel p) {
   
  switch (p) {
    case DesktopMenuPanel::Main: return 14;
    case DesktopMenuPanel::View: return 6;
    case DesktopMenuPanel::SortBy: return 7;
    case DesktopMenuPanel::AddNew: return 2;
  }
  return 1;
}

bool row_is_separator(DesktopMenuPanel p, int row) {
   
  if (p == DesktopMenuPanel::Main) return row == 2 || row == 6;
  if (p == DesktopMenuPanel::View) return row == 2;
  if (p == DesktopMenuPanel::SortBy) return row == 4;
  return false;
}

bool row_is_submenu_chevron(DesktopMenuPanel p, int row) {
   
  if (p != DesktopMenuPanel::Main) return false;
  return row == 0 || row == 1 || row == 13;
}

double menu_height(DesktopMenuPanel p) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kRowH_loc = 28.0 * us;
  const double kPad_loc = 10.0 * us;
  return static_cast<double>(menu_row_count(p)) * kRowH_loc + kPad_loc * 2;
}

void main_menu_bounds(const DesktopApp& app, double* mx, double* my, double* mw, double* mh) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kMinMenuWidth_loc = 120.0 * us;
  *mx = app.desktopMenuX;
  *my = app.desktopMenuY;
  *mw = std::max(app.desktopMenuMainWidth, kMinMenuWidth_loc);
  *mh = menu_height(DesktopMenuPanel::Main);
}

bool cascade_is_open(const DesktopApp& app) { return app.desktopMenuCascade != DesktopMenuPanel::Main; }

void layout_cascade_position(DesktopApp& app, int layer_w, int layer_h) {
   
  refresh_desktop_menu_widths(app);
  app.desktopMenuCascadeX = 0;
  app.desktopMenuCascadeY = 0;
  if (!cascade_is_open(app)) return;

  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kRowH_loc = 28.0 * us;
  const double kPad_loc = 10.0 * us;
  const double kMinMenuWidth_loc = 120.0 * us;
  const double kCascadeGap_loc = 4.0 * us;

  double mx, my, mw, mh;
  main_menu_bounds(app, &mx, &my, &mw, &mh);
  const double subw = std::max(app.desktopMenuCascadeWidth, kMinMenuWidth_loc);
  const double subh = menu_height(app.desktopMenuCascade);
  const double anchor_y = my + kPad_loc + static_cast<double>(app.desktopMenuCascadeAnchorRow) * kRowH_loc;

  double sx = mx + mw + kCascadeGap_loc;
  if (layer_w > 0 && sx + subw > static_cast<double>(layer_w) - 4 * us) sx = mx - subw - kCascadeGap_loc;

  double sy = anchor_y;
  if (layer_h > 0) {
    sy = std::clamp(sy, 4.0 * us, std::max(4.0 * us, static_cast<double>(layer_h) - subh - 4 * us));
  } else {
    sy = std::max(4.0 * us, sy);
  }

  app.desktopMenuCascadeX = sx;
  app.desktopMenuCascadeY = sy;
}

void clamp_main_menu_to_layer(DesktopApp& app, int layer_w, int layer_h) {
   
  refresh_desktop_menu_widths(app);
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  double mx, my, mw, mh;
  main_menu_bounds(app, &mx, &my, &mw, &mh);
  if (layer_w > 0 && mx + mw > layer_w - 4 * us) app.desktopMenuX = std::max(4.0 * us, static_cast<double>(layer_w) - mw - 4 * us);
  if (layer_h > 0 && my + mh > layer_h - 4 * us) app.desktopMenuY = std::max(4.0 * us, static_cast<double>(layer_h) - mh - 4 * us);
  if (app.desktopMenuX < 4 * us) app.desktopMenuX = 4 * us;
  if (app.desktopMenuY < 4 * us) app.desktopMenuY = 4 * us;
  layout_cascade_position(app, layer_w, layer_h);
}

int hit_main_menu_row(const DesktopApp& app, double lx, double ly) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kRowH_loc = 28.0 * us;
  const double kPad_loc = 10.0 * us;
  double mx, my, mw, mh;
  main_menu_bounds(app, &mx, &my, &mw, &mh);
  if (lx < mx || ly < my || lx >= mx + mw || ly >= my + mh) return -2;
  const double rel = ly - my - kPad_loc;
  const int row = static_cast<int>(std::floor(rel / kRowH_loc));
  const int n = menu_row_count(DesktopMenuPanel::Main);
  if (row < 0 || row >= n) return -1;
  if (row_is_separator(DesktopMenuPanel::Main, row)) return -1;
  return row;
}

int hit_cascade_menu_row(const DesktopApp& app, double lx, double ly) {
   
  if (!cascade_is_open(app)) return -2;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kRowH_loc = 28.0 * us;
  const double kPad_loc = 10.0 * us;
  const double kMinMenuWidth_loc = 120.0 * us;
  const double sx = app.desktopMenuCascadeX;
  const double sy = app.desktopMenuCascadeY;
  const double sw = std::max(app.desktopMenuCascadeWidth, kMinMenuWidth_loc);
  const double sh = menu_height(app.desktopMenuCascade);
  if (lx < sx || ly < sy || lx >= sx + sw || ly >= sy + sh) return -2;
  const double rel = ly - sy - kPad_loc;
  const int row = static_cast<int>(std::floor(rel / kRowH_loc));
  const int n = menu_row_count(app.desktopMenuCascade);
  if (row < 0 || row >= n) return -1;
  if (row_is_separator(app.desktopMenuCascade, row)) return -1;
  return row;
}

bool pointer_in_any_menu_bounds(const DesktopApp& app, double lx, double ly) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kMinMenuWidth_loc = 120.0 * us;
  double mx, my, mw, mh;
  main_menu_bounds(app, &mx, &my, &mw, &mh);
  const bool in_main = lx >= mx && ly >= my && lx < mx + mw && ly < my + mh;
  if (in_main) return true;
  if (!cascade_is_open(app)) return false;
  const double sx = app.desktopMenuCascadeX;
  const double sy = app.desktopMenuCascadeY;
  const double sw = std::max(app.desktopMenuCascadeWidth, kMinMenuWidth_loc);
  const double sh = menu_height(app.desktopMenuCascade);
  return lx >= sx && ly >= sy && lx < sx + sw && ly < sy + sh;
}

void update_hover_from_pointer(DesktopApp& app, double lx, double ly) {
   
  const int sub_row = hit_cascade_menu_row(app, lx, ly);
  const int main_row = hit_main_menu_row(app, lx, ly);

  if (sub_row >= 0) {
    app.desktopMenuHoverSubRow = sub_row;
    app.desktopMenuHoverMainRow = app.desktopMenuCascadeAnchorRow;
    return;
  }
  if (main_row >= 0) {
    app.desktopMenuHoverMainRow = main_row;
    app.desktopMenuHoverSubRow = -1;
    if (main_row == 0) {
      app.desktopMenuCascade = DesktopMenuPanel::View;
      app.desktopMenuCascadeAnchorRow = 0;
    } else if (main_row == 1) {
      app.desktopMenuCascade = DesktopMenuPanel::SortBy;
      app.desktopMenuCascadeAnchorRow = 1;
    } else if (main_row == 13) {
      app.desktopMenuCascade = DesktopMenuPanel::AddNew;
      app.desktopMenuCascadeAnchorRow = 13;
    } else {
      app.desktopMenuCascade = DesktopMenuPanel::Main;
    }
    if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
      DesktopLayer& L = *app.layers[app.pointerLayerIdx];
      layout_cascade_position(app, L.configuredWidth, L.configuredHeight);
    }
    return;
  }
  app.desktopMenuHoverMainRow = -1;
  app.desktopMenuHoverSubRow = -1;
}

void draw_check(cairo_t* cr, double x, double y, bool on, double ov) {
   
  if (!on) return;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.85, 0.9, 0.98, 0.95 * ov);
  cairo_set_line_width(cr, 2.0);
  cairo_move_to(cr, x, y + 8 * us);
  cairo_line_to(cr, x + 4 * us, y + 12 * us);
  cairo_line_to(cr, x + 12 * us, y + 4 * us);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void paint_after_menu_action(DesktopApp& app) {
   
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

static void launch_binary(const char* bin) {
  if (!bin || !*bin) return;
  const pid_t pid = ::fork();
  if (pid < 0) return;
  if (pid > 0) { ::waitpid(pid, nullptr, 0); return; }
  if (::setsid() < 0) ::_exit(1);
  const pid_t worker = ::fork();
  if (worker < 0) ::_exit(1);
  if (worker > 0) ::_exit(0);
  const int devnull = ::open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    ::dup2(devnull, STDIN_FILENO);
    ::dup2(devnull, STDOUT_FILENO);
    ::dup2(devnull, STDERR_FILENO);
    ::close(devnull);
  }
  ::execlp(bin, bin, nullptr);
  ::_exit(127);
}

void apply_main_menu_action(DesktopApp& app, int row) {
   
  if (row == 3) desktop_icons_copy_marquee_selection_to_clipboard(app);
  if (row == 4) desktop_icons_paste_from_clipboard(app);
  if (row == 5 && !app.desktopUndoPasteDestPaths.empty()) desktop_icons_undo_last_paste(app);
  if (row == 7) desktop_icons_delete_marquee_selection(app);
  if (row == 8) desktop_icons_refresh_workspace(app);
  if (row == 9) eh::settings::embed_show_tab(7);
  if (row == 10) eh::settings::request_launch_settings();
  if (row == 11) launch_binary("horizon-live-wallpaper");
  if (row == 12) launch_binary("horizon-live-wallpaper");
  paint_after_menu_action(app);
}

void apply_cascade_menu_action(DesktopApp& app, DesktopMenuPanel cascade, int row) {
   
  switch (cascade) {
    case DesktopMenuPanel::View:
      if (row == 0) desktop_prefs_set_icons_visible(app, true);
      if (row == 1) desktop_prefs_set_icons_visible(app, false);
      if (row == 3) desktop_prefs_set_icon_size(app, DesktopIconSizeKind::Large);
      if (row == 4) desktop_prefs_set_icon_size(app, DesktopIconSizeKind::Medium);
      if (row == 5) desktop_prefs_set_icon_size(app, DesktopIconSizeKind::Small);
      break;
    case DesktopMenuPanel::SortBy:
      if (row == 0) desktop_prefs_set_arrangement(app, DesktopIconArrangement::AutoArrange);
      if (row == 1) desktop_prefs_set_arrangement(app, DesktopIconArrangement::AlignToGrid);
      if (row == 2) desktop_prefs_set_arrangement(app, DesktopIconArrangement::Stacked);
      if (row == 3) desktop_prefs_set_arrangement(app, DesktopIconArrangement::StackedRight);
      if (row == 5) desktop_prefs_set_sort_mode(app, DesktopIconSort::Name);
      if (row == 6) desktop_prefs_set_sort_mode(app, DesktopIconSort::Type);
      break;
    case DesktopMenuPanel::AddNew:
      if (row == 0) desktop_icons_create_new_text_file(app);
      if (row == 1) desktop_icons_create_new_folder(app);
      break;
    default:
      break;
  }
  paint_after_menu_action(app);
}

const char* row_label(DesktopMenuPanel p, int row) {
   
  switch (p) {
    case DesktopMenuPanel::Main:
      switch (row) {
        case 0: return "View";
        case 1: return "Sort by";
        case 3: return "Copy";
        case 4: return "Paste";
        case 5: return "Undo paste";
        case 7: return "Delete";
        case 8: return "Refresh";
        case 9: return "Display settings";
        case 10: return "Open settings";
        case 11: return "Wallpaper";
        case 12: return "Live wallpaper";
        case 13: return "New";
        default: return "";
      }
    case DesktopMenuPanel::View:
      switch (row) {
        case 0: return "Show desktop icons";
        case 1: return "Don't show desktop icons";
        case 3: return "Large icons";
        case 4: return "Medium icons";
        case 5: return "Small icons";
        default: return "";
      }
    case DesktopMenuPanel::SortBy:
      switch (row) {
        case 0: return "Auto arrange icons";
        case 1: return "Align icons to grid";
        case 2: return "Stack left";
        case 3: return "Stack right";
        case 5: return "Sort by name";
        case 6: return "Sort by type";
        default: return "";
      }
    case DesktopMenuPanel::AddNew:
      switch (row) {
        case 0: return "Text document";
        case 1: return "Folder";
        default: return "";
      }
    default:
      return "";
  }
}

void paint_menu_panel(cairo_t* cr, double mx, double my, DesktopMenuPanel panel, double panel_w,
                       const DesktopApp& app, double ov, const eh::config::ChromePaintColors& mc) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kRowH_loc = 28.0 * us;
  const double kPad_loc = 10.0 * us;
  const double kMinMenuWidth_loc = 120.0 * us;
  const double kMenuCornerRadius_loc = 8.0 * us;
  const double kRowHighlightRadius_loc = 5.0 * us;
  const double mw = std::max(panel_w, kMinMenuWidth_loc);
  const double mh = menu_height(panel);
  eh::shell::shared::paint_glass_card(cr, mx, my, mw, mh, kMenuCornerRadius_loc, mc, ov);

  const int n = menu_row_count(panel);
  const bool undo_disabled = panel == DesktopMenuPanel::Main && app.desktopUndoPasteDestPaths.empty();
  const bool delete_disabled = panel == DesktopMenuPanel::Main && app.iconMarqueeSelection.empty();

  for (int i = 0; i < n; ++i) {
    const double ry = my + kPad_loc + static_cast<double>(i) * kRowH_loc;
    const bool on_main = panel == DesktopMenuPanel::Main;
    const bool on_cascade = cascade_is_open(app) && panel == app.desktopMenuCascade;
    const bool row_hover_main = on_main && i == app.desktopMenuHoverMainRow;
    const bool row_hover_cascade = on_cascade && i == app.desktopMenuHoverSubRow;
    const bool anchor_glow =
        on_main && cascade_is_open(app) && i == app.desktopMenuCascadeAnchorRow && app.desktopMenuHoverSubRow >= 0;

    if ((row_hover_main || row_hover_cascade || anchor_glow) && !row_is_separator(panel, i)) {
      const bool dim_undo_hover =
          panel == DesktopMenuPanel::Main &&
          ((i == 5 && undo_disabled) || (i == 7 && delete_disabled));
      if (!dim_undo_hover) {
        cairo_set_source_rgba(cr, 0.18, 0.22, 0.3, 0.85 * ov);
        path_rounded_rect(cr, mx + 2 * us, ry, mw - 4 * us, kRowH_loc, kRowHighlightRadius_loc);
        cairo_fill(cr);
      }
    }
    if (row_is_separator(panel, i)) {
      cairo_save(cr);
      cairo_new_path(cr);
      cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.7 * ov);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, mx + kPad_loc, ry + kRowH_loc * 0.5);
      cairo_line_to(cr, mx + mw - kPad_loc, ry + kRowH_loc * 0.5);
      cairo_stroke(cr);
      cairo_restore(cr);
      continue;
    }

    const bool dim_undo =
        panel == DesktopMenuPanel::Main &&
        ((i == 5 && undo_disabled) || (i == 7 && delete_disabled));
    if (dim_undo)
      cairo_set_source_rgba(cr, 0.45, 0.46, 0.48, 0.75);
    else
      cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 0.95);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_move_to(cr, mx + kPad_loc + 22.0 * us, ry + kRowH_loc * 0.72);
    cairo_show_text(cr, row_label(panel, i));

    if (row_is_submenu_chevron(panel, i)) {
      cairo_set_font_size(cr, 14.0 * us);
      cairo_move_to(cr, mx + mw - kPad_loc - 14 * us, ry + kRowH_loc * 0.72);
      cairo_show_text(cr, "\xE2\x80\xBA");
    }

    if (panel == DesktopMenuPanel::View) {
      if (i == 0) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.desktopIconsVisible, ov);
      if (i == 1) draw_check(cr, mx + kPad_loc, ry + 6 * us, !app.desktopIconsVisible, ov);
      if (i == 3) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconSizeKind == DesktopIconSizeKind::Large, ov);
      if (i == 4) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconSizeKind == DesktopIconSizeKind::Medium, ov);
      if (i == 5) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconSizeKind == DesktopIconSizeKind::Small, ov);
    }
    if (panel == DesktopMenuPanel::SortBy) {
      if (i == 0) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconArrangement == DesktopIconArrangement::AutoArrange, ov);
      if (i == 1) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconArrangement == DesktopIconArrangement::AlignToGrid, ov);
      if (i == 2) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconArrangement == DesktopIconArrangement::Stacked, ov);
      if (i == 3) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconArrangement == DesktopIconArrangement::StackedRight, ov);
      if (i == 5) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconSortMode == DesktopIconSort::Name, ov);
      if (i == 6) draw_check(cr, mx + kPad_loc, ry + 6 * us, app.iconSortMode == DesktopIconSort::Type, ov);
    }
  }
}

}

bool desktop_menu_handle_left_press(DesktopApp& app) {
   
  if (!app.desktopMenuOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.desktopMenuLayerIdx, &lx, &ly)) {
    desktop_menu_close(app);
    if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
      paint_layer(app, *app.layers[app.pointerLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }
  if (!pointer_in_any_menu_bounds(app, lx, ly)) {
    desktop_menu_close(app);
    if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
      paint_layer(app, *app.layers[app.pointerLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  const int main_row = hit_main_menu_row(app, lx, ly);
  const int sub_row = hit_cascade_menu_row(app, lx, ly);
  if (main_row == -1 && sub_row == -1) {
    app.desktopMenuArmMainRow = -1;
    app.desktopMenuArmSubRow = -1;
    return true;
  }
  if (sub_row >= 0) {
    app.desktopMenuArmMainRow = -1;
    app.desktopMenuArmSubRow = sub_row;
    return true;
  }
  if (main_row >= 0) {
    app.desktopMenuArmMainRow = main_row;
    app.desktopMenuArmSubRow = -1;
    return true;
  }
  app.desktopMenuArmMainRow = -1;
  app.desktopMenuArmSubRow = -1;
  return true;
}

bool desktop_menu_pointer_motion(DesktopApp& app) {
   
  if (!app.desktopMenuOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.desktopMenuLayerIdx, &lx, &ly)) return false;

  const int prev_main = app.desktopMenuHoverMainRow;
  const int prev_sub = app.desktopMenuHoverSubRow;
  const DesktopMenuPanel prev_cascade = app.desktopMenuCascade;
  update_hover_from_pointer(app, lx, ly);
  if (prev_main == app.desktopMenuHoverMainRow && prev_sub == app.desktopMenuHoverSubRow && prev_cascade == app.desktopMenuCascade)
    return true;

  if (app.desktopMenuLayerIdx < app.layers.size() && app.layers[app.desktopMenuLayerIdx])
    paint_layer(app, *app.layers[app.desktopMenuLayerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

bool desktop_menu_left_release(DesktopApp& app) {
   
  if (!app.desktopMenuOpen) return false;
  const int armed_main = app.desktopMenuArmMainRow;
  const int armed_sub = app.desktopMenuArmSubRow;
  double lx = 0, ly = 0;
  int rel_main = -3;
  int rel_sub = -3;
  if (desktop_pointer_local_xy(app, app.desktopMenuLayerIdx, &lx, &ly)) {
    rel_main = hit_main_menu_row(app, lx, ly);
    rel_sub = hit_cascade_menu_row(app, lx, ly);
  }

  const size_t menu_layer = app.desktopMenuLayerIdx;
  update_hover_from_pointer(app, lx, ly);

  const bool hit_sub = rel_sub >= 0;
  const bool hit_main = rel_main >= 0;

  if (hit_sub && armed_sub >= 0 && rel_sub == armed_sub) {
    const DesktopMenuPanel cascade = app.desktopMenuCascade;
    desktop_menu_close(app);
    apply_cascade_menu_action(app, cascade, rel_sub);
    return true;
  }

  if (hit_main && armed_main >= 0 && rel_main == armed_main && armed_sub < 0) {
    if (rel_main == 0 || rel_main == 1 || rel_main == 13) {
      app.desktopMenuCascade = rel_main == 0  ? DesktopMenuPanel::View
                             : rel_main == 1  ? DesktopMenuPanel::SortBy
                                              : DesktopMenuPanel::AddNew;
      app.desktopMenuCascadeAnchorRow = rel_main;
      app.desktopMenuArmMainRow = -1;
      app.desktopMenuArmSubRow = -1;
      if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
        DesktopLayer& L = *app.layers[menu_layer];
        layout_cascade_position(app, L.configuredWidth, L.configuredHeight);
        paint_layer(app, L);
        if (app.display) wl_display_flush(app.display);
      }
      return true;
    }
    desktop_menu_close(app);
    apply_main_menu_action(app, rel_main);
    return true;
  }

  desktop_menu_close(app);
  if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
    paint_layer(app, *app.layers[menu_layer]);
    if (app.display) wl_display_flush(app.display);
  }
  return true;
}

void desktop_menu_close(DesktopApp& app) {
   
  app.desktopMenuOpen = false;
  app.desktopMenuCascade = DesktopMenuPanel::Main;
  app.desktopMenuCascadeAnchorRow = 0;
  app.desktopMenuCascadeX = 0;
  app.desktopMenuCascadeY = 0;
  app.desktopMenuHoverMainRow = -1;
  app.desktopMenuHoverSubRow = -1;
  app.desktopMenuArmMainRow = -1;
  app.desktopMenuArmSubRow = -1;
}

void desktop_menu_open_for_workspace(DesktopApp& app) {
   
  desktop_icon_menu_close(app);
  desktop_prefs_ensure_loaded(app);
  if (app.pointerLayerIdx >= app.layers.size() || !app.layers[app.pointerLayerIdx]) return;
  DesktopLayer& L = *app.layers[app.pointerLayerIdx];
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.pointerLayerIdx, &lx, &ly)) return;
  app.desktopMenuOpen = true;
  app.desktopMenuCascade = DesktopMenuPanel::Main;
  app.desktopMenuCascadeAnchorRow = 0;
  app.desktopMenuCascadeX = 0;
  app.desktopMenuCascadeY = 0;
  app.desktopMenuLayerIdx = app.pointerLayerIdx;
  app.desktopMenuX = lx;
  app.desktopMenuY = ly;
  app.desktopMenuHoverMainRow = -1;
  app.desktopMenuHoverSubRow = -1;
  app.desktopMenuArmMainRow = -1;
  app.desktopMenuArmSubRow = -1;
  clamp_main_menu_to_layer(app, L.configuredWidth, L.configuredHeight);
  update_hover_from_pointer(app, lx, ly);
  paint_layer(app, L);
  if (app.display) wl_display_flush(app.display);
}

void desktop_menu_paint(DesktopApp& app, size_t layer_index, cairo_t* cr) {
   
  if (!app.desktopMenuOpen || layer_index != app.desktopMenuLayerIdx) return;

  double mx, my, mw, mh;
  main_menu_bounds(app, &mx, &my, &mw, &mh);
  if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
    DesktopLayer& L = *app.layers[app.pointerLayerIdx];
    layout_cascade_position(app, L.configuredWidth, L.configuredHeight);
  }

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double ov = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::DesktopContextMenu));
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);
  paint_menu_panel(cr, mx, my, DesktopMenuPanel::Main, app.desktopMenuMainWidth, app, ov, mc);
  if (cascade_is_open(app))
    paint_menu_panel(cr, app.desktopMenuCascadeX, app.desktopMenuCascadeY, app.desktopMenuCascade,
                     app.desktopMenuCascadeWidth, app, ov, mc);
  cairo_restore(cr);
}

}
