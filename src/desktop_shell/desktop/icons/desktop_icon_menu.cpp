#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"

#include "desktop_shell/desktop/entries/desktop_archive.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/entries/desktop_open_with.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"
#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"

#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.hpp"

#include "configuration/shell_config.hpp"
#include "services/udisks2/udisks2_drive_service.hpp"

#include <cairo/cairo.h>

#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <cstdlib>
#include <future>
#include <iostream>
#include <filesystem>
#include <numbers>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>



#include <wayland-client.h>

namespace fs = std::filesystem;

namespace eh::shell::desktop {

inline double kMenuW() { return 360.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kMenuRightPad() { return 6.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kRowH() { return 28.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kPad() { return 10.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kMenuCornerRadius() { return 8.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kRowHighlightRadius() { return 5.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kCascadeGap() { return 4.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kMinCompressMenuW() { return 180.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kIconMinMenuW() { return 120.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kChevronGap() { return 8.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
constexpr int kCompressSubRows = 4;

using eh::shell::shared::path_rounded_rect;

double menu_height_rows(size_t n) {
   
  return static_cast<double>(n) * kRowH() + kPad() * 2;
}

void menu_bounds(const DesktopApp& app, double* mx, double* my, double* mw, double* mh) {
   
  *mx = app.iconCtxMenuX;
  *my = app.iconCtxMenuY;
  *mw = std::max(app.iconCtxMenuW, kIconMinMenuW() + kMenuRightPad());
  *mh = menu_height_rows(app.iconCtxMenuRows.size());
}

void refresh_compress_cascade_width(DesktopApp& app) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(surf);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0 * us);
  double maxw = kMinCompressMenuW();
  static const char* const kLabs[kCompressSubRows] = {"Compress to ZIP", "Compress to tar.gz", "Compress to tar",
                                                      "Compress to 7z"};
  for (const char* lab : kLabs) {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, lab, &ex);
    maxw = std::max(maxw, kPad() + 22.0 * us + ex.x_advance + kPad());
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  app.iconCtxCompressCascadeW = maxw + kMenuRightPad();
}

void layout_compress_cascade(DesktopApp& app, int layer_w, int layer_h) {
   
  if (!app.iconCtxCompressCascadeOpen) return;
  if (app.iconCtxCompressCascadeW <= 0) refresh_compress_cascade_width(app);
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  double mx, my, mw, mh;
  menu_bounds(app, &mx, &my, &mw, &mh);
  const double subw = std::max(app.iconCtxCompressCascadeW, kMinCompressMenuW());
  const double subh = menu_height_rows(static_cast<size_t>(kCompressSubRows));
  const double anchor_y = my + kPad() + static_cast<double>(app.iconCtxCompressAnchorRow) * kRowH();

  double sx = mx + mw + kCascadeGap();
  if (layer_w > 0 && sx + subw > static_cast<double>(layer_w) - 4 * us) sx = mx - subw - kCascadeGap();

  double sy = anchor_y;
  if (layer_h > 0)
    sy = std::clamp(sy, 4.0 * us, std::max(4.0 * us, static_cast<double>(layer_h) - subh - 4 * us));
  else
    sy = std::max(4.0 * us, sy);

  app.iconCtxCompressCascadeX = sx;
  app.iconCtxCompressCascadeY = sy;
}

void menu_full_bounds(const DesktopApp& app, double* bx, double* by, double* bw, double* bh) {
   
  double mx, my, mw, mh;
  menu_bounds(app, &mx, &my, &mw, &mh);
  if (!app.iconCtxCompressCascadeOpen) {
    *bx = mx;
    *by = my;
    *bw = mw;
    *bh = mh;
    return;
  }
  const double subw = std::max(app.iconCtxCompressCascadeW, kMinCompressMenuW());
  const double subh = menu_height_rows(static_cast<size_t>(kCompressSubRows));
  const double cx = app.iconCtxCompressCascadeX;
  const double cy = app.iconCtxCompressCascadeY;
  const double x0 = std::min(mx, cx);
  const double y0 = std::min(my, cy);
  const double x1 = std::max(mx + mw, cx + subw);
  const double y1 = std::max(my + mh, cy + subh);
  *bx = x0;
  *by = y0;
  *bw = x1 - x0;
  *bh = y1 - y0;
}

void clamp_menu(DesktopApp& app, int layer_w, int layer_h) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  double bx, by, bw, bh;
  menu_full_bounds(app, &bx, &by, &bw, &bh);
  if (layer_w > 0 && bx + bw > layer_w - 4 * us) app.iconCtxMenuX = std::max(4.0 * us, static_cast<double>(layer_w) - bw - 4 * us);
  if (layer_h > 0 && by + bh > layer_h - 4 * us) app.iconCtxMenuY = std::max(4.0 * us, static_cast<double>(layer_h) - bh - 4 * us);
  if (app.iconCtxMenuX < 4 * us) app.iconCtxMenuX = 4 * us;
  if (app.iconCtxMenuY < 4 * us) app.iconCtxMenuY = 4 * us;
  layout_compress_cascade(app, layer_w, layer_h);
}

bool row_is_sep(const IconCtxMenuRow& r) {
   
  return r.kind == IconCtxMenuRow::Kind::Separator || r.kind == IconCtxMenuRow::Kind::Separator2;
}

bool row_is_compress_submenu(const IconCtxMenuRow& r) {
   
  return r.kind == IconCtxMenuRow::Kind::CompressSubmenu;
}

int hit_icon_menu_main_row(const DesktopApp& app, double lx, double ly) {
   
  double mx, my, mw, mh;
  menu_bounds(app, &mx, &my, &mw, &mh);
  if (lx < mx || ly < my || lx >= mx + mw || ly >= my + mh) return -2;
  const double rel = ly - my - kPad();
  const int row = static_cast<int>(std::floor(rel / kRowH()));
  if (row < 0 || static_cast<size_t>(row) >= app.iconCtxMenuRows.size()) return -1;
  if (row_is_sep(app.iconCtxMenuRows[static_cast<size_t>(row)])) return -1;
  return row;
}

int hit_icon_menu_compress_row(const DesktopApp& app, double lx, double ly) {
   
  if (!app.iconCtxCompressCascadeOpen) return -2;
  const double sx = app.iconCtxCompressCascadeX;
  const double sy = app.iconCtxCompressCascadeY;
  const double sw = std::max(app.iconCtxCompressCascadeW, kMinCompressMenuW());
  const double sh = menu_height_rows(static_cast<size_t>(kCompressSubRows));
  if (lx < sx || ly < sy || lx >= sx + sw || ly >= sy + sh) return -2;
  const double rel = ly - sy - kPad();
  const int row = static_cast<int>(std::floor(rel / kRowH()));
  if (row < 0 || row >= kCompressSubRows) return -1;
  return row;
}

bool pointer_in_icon_menu_bounds(const DesktopApp& app, double lx, double ly) {
   
  double bx, by, bw, bh;
  menu_full_bounds(app, &bx, &by, &bw, &bh);
  return lx >= bx && ly >= by && lx < bx + bw && ly < by + bh;
}

void update_icon_menu_hover(DesktopApp& app, double lx, double ly) {
   
  const int sub = hit_icon_menu_compress_row(app, lx, ly);
  if (sub >= 0) {
    app.iconCtxMenuHoverCompressRow = sub;
    app.iconCtxMenuHoverMainRow = app.iconCtxCompressAnchorRow;
    return;
  }
  const int main = hit_icon_menu_main_row(app, lx, ly);
  if (main >= 0) {
    app.iconCtxMenuHoverMainRow = main;
    app.iconCtxMenuHoverCompressRow = -1;
    return;
  }
  app.iconCtxMenuHoverMainRow = -1;
  app.iconCtxMenuHoverCompressRow = -1;
}

void append_compress_submenu_row(DesktopApp& app) {
   
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::CompressSubmenu, 0});
}

void rebuild_icon_ctx_rows(DesktopApp& app, const std::vector<int>& sel) {
   
  app.iconCtxMenuRows.clear();
  app.iconCtxDesktopEntry.reset();
  if (sel.empty()) return;

  std::vector<DesktopIconItem> chosen;
  chosen.reserve(sel.size());
  for (int i : sel) {
    if (i >= 0 && static_cast<size_t>(i) < app.icons.size()) chosen.push_back(app.icons[static_cast<size_t>(i)]);
  }
  if (chosen.empty()) return;

  std::vector<std::string> paths;
  paths.reserve(chosen.size());
  for (const auto& ic : chosen) paths.push_back(ic.desktop_path);
  const std::vector<std::string> archives = archive::filter_archive_paths(paths);
  const bool show_extract = !archives.empty();
  const bool multi = chosen.size() > 1;

  if (multi) {
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Open, 0});
    if (show_extract) app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::ExtractArchive, 0});
    append_compress_submenu_row(app);
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Cut, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Copy, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator2, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Delete, 0});
    return;
  }

  const DesktopIconItem& ic = chosen[0];
  if (ic.kind == DesktopIconKind::File || ic.kind == DesktopIconKind::Folder) {
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Open, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::OpenWith, 0});
    if (show_extract) app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::ExtractArchive, 0});
    append_compress_submenu_row(app);
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::OpenLocation, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator2, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Cut, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Copy, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Delete, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Rename, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
    return;
  }

  if (ic.kind == DesktopIconKind::Drive) {
    if (ic.drive_mounted) {
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Open, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::OpenLocation, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator2, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Copy, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Unmount, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
    } else {
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Mount, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
      app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
    }
    return;
  }

  if (ic.kind == DesktopIconKind::Trash) {
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Open, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::EmptyTrash, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
    return;
  }

  if (ic.kind == DesktopIconKind::MyComputer) {
    app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
    return;
  }

  app.iconCtxDesktopEntry = read_desktop_entry_info(ic.desktop_path);
  const DesktopEntryInfo* inf = app.iconCtxDesktopEntry.has_value() ? &*app.iconCtxDesktopEntry : nullptr;

  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Open, 0});
  if (inf) {
    for (size_t i = 0; i < inf->actions.size(); ++i) {
      if (!inf->actions[i].exec.empty() && !inf->actions[i].name.empty())
        app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::DesktopAction, static_cast<uint16_t>(i)});
    }
  }
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::OpenLocation, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::RunElevated, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator2, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::PinStart, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::PinTaskbar, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Separator, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Cut, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Copy, 0});
  append_compress_submenu_row(app);
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::CreateShortcut, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Delete, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Rename, 0});
  app.iconCtxMenuRows.push_back({IconCtxMenuRow::Kind::Properties, 0});
}

const char* row_label(const DesktopApp& app, size_t row_idx, const DesktopIconItem&  , bool pinned) {
   
  if (row_idx >= app.iconCtxMenuRows.size()) return "";
  const IconCtxMenuRow& r = app.iconCtxMenuRows[row_idx];
  switch (r.kind) {
    case IconCtxMenuRow::Kind::Open:
      return "Open";
    case IconCtxMenuRow::Kind::OpenWith:
      return "Open With";
    case IconCtxMenuRow::Kind::DesktopAction: {
      const DesktopEntryInfo* inf = app.iconCtxDesktopEntry.has_value() ? &*app.iconCtxDesktopEntry : nullptr;
      if (!inf || r.action_index >= inf->actions.size()) return "";
      return inf->actions[r.action_index].name.c_str();
    }
    case IconCtxMenuRow::Kind::OpenLocation:
      return "Open file location";
    case IconCtxMenuRow::Kind::RunElevated:
      return "Run as administrator";
    case IconCtxMenuRow::Kind::PinStart:
      return pinned ? "Unpin from Start menu" : "Pin to Start menu";
    case IconCtxMenuRow::Kind::PinTaskbar:
      return pinned ? "Unpin from Dock" : "Pin to Dock";
    case IconCtxMenuRow::Kind::Cut:
      return "Cut";
    case IconCtxMenuRow::Kind::Copy:
      return "Copy";
    case IconCtxMenuRow::Kind::CreateShortcut:
      return "Create shortcut";
    case IconCtxMenuRow::Kind::Delete:
      return "Delete";
    case IconCtxMenuRow::Kind::Rename:
      return "Rename";
    case IconCtxMenuRow::Kind::Properties:
      return "Properties";
    case IconCtxMenuRow::Kind::ExtractArchive:
      return "Extract to folder";
    case IconCtxMenuRow::Kind::CompressSubmenu:
      return "Compress";
    case IconCtxMenuRow::Kind::CompressZip:
      return "Compress to ZIP";
    case IconCtxMenuRow::Kind::CompressTarGz:
      return "Compress to tar.gz";
    case IconCtxMenuRow::Kind::CompressTar:
      return "Compress to tar";
    case IconCtxMenuRow::Kind::Compress7z:
      return "Compress to 7z";
    case IconCtxMenuRow::Kind::Mount:
      return "Mount";
    case IconCtxMenuRow::Kind::Unmount:
      return "Unmount";
    case IconCtxMenuRow::Kind::EmptyTrash:
      return "Empty Trash";
    default:
      return "";
  }
}

void refresh_icon_menu_width(DesktopApp& app, const DesktopIconItem& ic, bool pinned) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cairo_t* cr = cairo_create(surf);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0 * us);
  double maxw = 0;
  for (size_t i = 0; i < app.iconCtxMenuRows.size(); ++i) {
    if (row_is_sep(app.iconCtxMenuRows[i])) continue;
    const char* lab = row_label(app, i, ic, pinned);
    if (!lab || !*lab) continue;
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, lab, &ex);
    double rowW = kPad() + ex.x_advance + kPad();
    if (row_is_compress_submenu(app.iconCtxMenuRows[i])) {
      cairo_set_font_size(cr, 14.0 * us);
      cairo_text_extents_t chex{};
      cairo_text_extents(cr, "\xE2\x80\xBA", &chex);
      cairo_set_font_size(cr, 13.0 * us);
      rowW = kPad() + ex.x_advance + kChevronGap() + chex.x_advance + kPad();
    }
    maxw = std::max(maxw, rowW);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  app.iconCtxMenuW = maxw + kMenuRightPad();
}

void apply_icon_menu_snapshot(DesktopApp& app, const std::vector<DesktopIconItem>& items, const IconCtxMenuRow& spec,
                             const std::optional<DesktopEntryInfo>& entry_opt) {
   
  if (items.empty()) return;
  std::vector<std::string> paths;
  paths.reserve(items.size());
  for (const auto& ic : items) paths.push_back(ic.desktop_path);

  auto compress_fmt = [&]() -> std::optional<archive::CompressFormat> {
    switch (spec.kind) {
      case IconCtxMenuRow::Kind::CompressZip:
        return archive::CompressFormat::Zip;
      case IconCtxMenuRow::Kind::CompressTarGz:
        return archive::CompressFormat::TarGz;
      case IconCtxMenuRow::Kind::CompressTar:
        return archive::CompressFormat::Tar;
      case IconCtxMenuRow::Kind::Compress7z:
        return archive::CompressFormat::SevenZ;
      default:
        return std::nullopt;
    }
  };

  switch (spec.kind) {
    case IconCtxMenuRow::Kind::Open:
      if (items.size() == 1 && items[0].kind != DesktopIconKind::File && items[0].kind != DesktopIconKind::Folder &&
          items[0].kind != DesktopIconKind::Drive && items[0].kind != DesktopIconKind::Trash &&
          items[0].kind != DesktopIconKind::MyComputer && entry_opt.has_value()) {
        xdg::open_desktop_default(items[0].desktop_path, entry_opt, items[0].exec);
      } else {
        for (const auto& ic : items) {
          if (ic.kind == DesktopIconKind::Trash) {
            const char* home = std::getenv("HOME");
            if (home)
              xdg::open_path_in_default_application((fs::path(home) / ".local/share/Trash").string());
          } else if (ic.kind == DesktopIconKind::MyComputer) {
            xdg::open_uri("computer:///");
          } else if (ic.kind == DesktopIconKind::File || ic.kind == DesktopIconKind::Folder ||
                     ic.kind == DesktopIconKind::Drive)
            xdg::open_path_in_default_application(ic.desktop_path);
          else
            xdg::open_desktop_default(ic.desktop_path, read_desktop_entry_info(ic.desktop_path), ic.exec);
        }
      }
      break;
    case IconCtxMenuRow::Kind::OpenWith: {
      if (items.size() == 1 && (items[0].kind == DesktopIconKind::File || items[0].kind == DesktopIconKind::Folder)) {
        eh_desktop_log("apply_snapshot: OpenWith path=%s", items[0].desktop_path.c_str());
        open_with_open(app, items[0].desktop_path);
      } else {
        eh_desktop_log("apply_snapshot: OpenWith SKIP items=%zu kind=%d", items.size(), items.empty() ? -1 : static_cast<int>(items[0].kind));
      }
      break;
    }
    case IconCtxMenuRow::Kind::DesktopAction: {
      if (items.size() != 1) break;
      const DesktopIconItem& ic = items[0];
      const DesktopEntryInfo* entry = entry_opt.has_value() ? &*entry_opt : nullptr;
      if (!entry || spec.action_index >= entry->actions.size()) break;
      const std::string display_name = !entry->name.empty() ? entry->name : ic.name;
      xdg::launch_action_exec(entry->actions[spec.action_index].exec, ic.desktop_path, display_name, entry->terminal);
      break;
    }
    case IconCtxMenuRow::Kind::OpenLocation:
      if (items.size() != 1) break;
      if (items[0].kind == DesktopIconKind::File || items[0].kind == DesktopIconKind::Folder ||
          items[0].kind == DesktopIconKind::Drive)
        xdg::open_file_location(items[0].desktop_path, nullptr);
      else
        xdg::open_file_location(items[0].desktop_path, entry_opt.has_value() ? &*entry_opt : nullptr);
      break;
    case IconCtxMenuRow::Kind::RunElevated: {
      if (items.size() != 1) break;
      const DesktopIconItem& ic = items[0];
      const DesktopEntryInfo* entry = entry_opt.has_value() ? &*entry_opt : nullptr;
      std::string exec_raw = ic.exec;
      bool term = false;
      if (entry) {
        if (!entry->exec.empty()) exec_raw = entry->exec;
        term = entry->terminal;
      }
      xdg::launch_pkexec_exec_raw(exec_raw, ic.desktop_path, entry && !entry->name.empty() ? entry->name : ic.name, term);
      break;
    }
    case IconCtxMenuRow::Kind::PinStart:
    case IconCtxMenuRow::Kind::PinTaskbar:
      if (items.size() == 1) dock_settings_pinned_toggle(items[0].desktop_path);
      break;
    case IconCtxMenuRow::Kind::Cut:
      if (paths.size() == 1)
        xdg::clipboard_files_cut_copy(true, paths[0]);
      else
        xdg::clipboard_files_cut_copy_multi(true, paths);
      break;
    case IconCtxMenuRow::Kind::Copy:
      if (paths.size() == 1)
        xdg::clipboard_files_cut_copy(false, paths[0]);
      else
        xdg::clipboard_files_cut_copy_multi(false, paths);
      break;
    case IconCtxMenuRow::Kind::CreateShortcut: {
      if (items.size() != 1) break;
      const std::string path = items[0].desktop_path;
      fs::path p(path);
      fs::path dir = p.parent_path();
      std::string stem = p.stem().string();
      fs::path dest = dir / (stem + std::string(" shortcut.desktop"));
      for (int n = 2; fs::exists(dest); ++n) dest = dir / (stem + " shortcut (" + std::to_string(n) + ").desktop");
      std::error_code ec;
      fs::copy_file(p, dest, fs::copy_options::none, ec);
      if (!ec) desktop_icons_reload_from_disk(app);
      break;
    }
    case IconCtxMenuRow::Kind::Delete: {
      bool any = false;
      for (const std::string& pth : paths) {
        if (xdg::trash_file(pth)) any = true;
      }
      if (any) desktop_icons_reload_from_disk(app);
      break;
    }
    case IconCtxMenuRow::Kind::Rename: {
      if (items.size() != 1) break;
      const DesktopIconItem& ic = items[0];
      fs::path p(ic.desktop_path);
      const std::string ext = p.extension().string();
      const std::string initial =
          ic.kind == DesktopIconKind::Folder ? p.filename().string() : p.stem().string();
      const char* rename_title = "Rename desktop shortcut";
      if (ic.kind == DesktopIconKind::File) rename_title = "Rename file";
      else if (ic.kind == DesktopIconKind::Folder)
        rename_title = "Rename folder";
      const std::string neu = xdg::prompt_rename_text(rename_title, initial);
      if (neu.empty() || neu == initial) break;
      std::string safe = neu;
      for (auto& c : safe)
        if (c == '/' || c == '\0') c = '_';
      fs::path dest = p.parent_path() / (safe + ".desktop");
      if (ic.kind == DesktopIconKind::File)
        dest = p.parent_path() / (safe + ext);
      else if (ic.kind == DesktopIconKind::Folder)
        dest = p.parent_path() / safe;
      if (dest == p) break;
      std::error_code ec;
      fs::rename(p, dest, ec);
      if (!ec) desktop_icons_reload_from_disk(app);
      break;
    }
    case IconCtxMenuRow::Kind::Properties:
      if (items.size() == 1) xdg::open_properties_for_desktop_file(items[0].desktop_path);
      break;
    case IconCtxMenuRow::Kind::Mount: {
      if (items.size() != 1 || items[0].drive_object_path.empty()) break;
      auto& svc = eh::drives::UDisks2DriveService::instance();
      if (svc.has_fstab_entry(items[0].drive_object_path)) {
        app.mountDialogDeferredKind = MountDeferredKind::FstabMount;
        app.mountDialogDeferredObjectPath = items[0].drive_object_path;
        desktop_icon_menu_close(app);
        break;
      }
      app.mountDialogOpen = true;
      app.mountDialogLayerIdx = app.iconCtxMenuLayerIdx;
      app.mountDialogObjectPath = items[0].drive_object_path;
      app.mountDialogLabel = items[0].name;
      app.mountDialogFstype = items[0].drive_fstype;
      app.mountDialogUuid = items[0].drive_uuid;
      app.mountDialogDevice = items[0].desktop_path;
      app.mountDialogMountBase = 0;
      app.mountDialogBoot = false;
      app.mountDialogDropdownOpen = false;
      app.mountDialogHoverItem = -1;
      app.mountDialogArmItem = -1;
      break;
    }
    case IconCtxMenuRow::Kind::Unmount: {
      if (items.size() != 1 || items[0].drive_object_path.empty()) break;
      app.mountDialogDeferredKind = MountDeferredKind::Unmount;
      app.mountDialogDeferredObjectPath = items[0].drive_object_path;
      desktop_icon_menu_close(app);
      break;
    }
    case IconCtxMenuRow::Kind::EmptyTrash: {
      const char* home = std::getenv("HOME");
      if (home) {
        const std::string trash = std::string(home) + "/.local/share/Trash";
        const std::string cmd = "rm -rf '" + trash + "/files' '" + trash + "/info'" +
                                " && mkdir -p '" + trash + "/files' '" + trash + "/info'";
        (void)std::system(cmd.c_str());
        desktop_icons_reload_from_disk(app);
      }
      break;
    }
    case IconCtxMenuRow::Kind::FstabDismiss:
    case IconCtxMenuRow::Kind::FstabYes:
    case IconCtxMenuRow::Kind::FstabNo:
      break;
    case IconCtxMenuRow::Kind::ExtractArchive: {
      const std::vector<std::string> arch = archive::filter_archive_paths(paths);
      if (!arch.empty()) {
        archive::extract_archives_detached(arch);
        desktop_icons_defer_rescan_after_background_io(app);
      }
      break;
    }
    case IconCtxMenuRow::Kind::CompressZip:
    case IconCtxMenuRow::Kind::CompressTarGz:
    case IconCtxMenuRow::Kind::CompressTar:
    case IconCtxMenuRow::Kind::Compress7z: {
      if (auto fmt = compress_fmt()) {
        fs::path parent = fs::path(paths[0]).parent_path();
        if (!parent.empty()) {
          archive::compress_paths_detached(*fmt, paths, parent.string());
          desktop_icons_defer_rescan_after_background_io(app);
        }
      }
      break;
    }
    case IconCtxMenuRow::Kind::CompressSubmenu:
      break;
    default:
      break;
  }
}

static IconCtxMenuRow compress_sub_row_to_spec(int sub_row) {
   
  switch (sub_row) {
    case 0:
      return {IconCtxMenuRow::Kind::CompressZip, 0};
    case 1:
      return {IconCtxMenuRow::Kind::CompressTarGz, 0};
    case 2:
      return {IconCtxMenuRow::Kind::CompressTar, 0};
    case 3:
      return {IconCtxMenuRow::Kind::Compress7z, 0};
    default:
      return {IconCtxMenuRow::Kind::Open, 0};
  }
}

static const char* compress_submenu_row_label(int row) {
   
  static const char* const kLabs[kCompressSubRows] = {"Compress to ZIP", "Compress to tar.gz", "Compress to tar",
                                                      "Compress to 7z"};
  if (row < 0 || row >= kCompressSubRows) return "";
  return kLabs[row];
}

namespace {

std::string sh_quote(const std::string& s) {
   
  std::string q = "'";
  for (char c : s) {
    if (c == '\'') q += "'\\''";
    else q += c;
  }
  q += '\'';
  return q;
}

std::string compute_mount_path(const std::string& label, int mountBase) {
   
  if (label.empty()) return {};
  const char* user = std::getenv("USER");
  if (!user) user = "user";
  switch (mountBase) {
    case 0:  return "/run/media/" + std::string(user) + "/" + label;
    case 1:  return "/media/" + label;
    case 2:  return "/mnt/" + label;
    default: return "/run/media/" + std::string(user) + "/" + label;
  }
}



}

int mount_dialog_hit_item(const DesktopApp& app, double lx, double ly) {
   
  if (!app.mountDialogOpen) return -1;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  const double boxX = app.mountDialogX + 16 * us;
  const double boxY = app.mountDialogY + 78 * us;
  const double boxW = app.mountDialogW - 32 * us;
  const double boxH = 26 * us;
  const double itemH = 22 * us;
  const double listTop = boxY + boxH;

  if (app.mountDialogDropdownOpen && lx >= boxX && lx < boxX + boxW) {
    for (int i = 0; i < 3; ++i) {
      const double iy = listTop + static_cast<double>(i) * itemH;
      if (ly >= iy && ly < iy + itemH) return i + 1;
    }
  }

  if (lx >= boxX && lx < boxX + boxW && ly >= boxY && ly < boxY + boxH)
    return 0;

  const double checkY = listTop + (app.mountDialogDropdownOpen ? (3 * itemH + 8 * us) : 8 * us);
  const double checkH = 22 * us;
  if (lx >= boxX && lx < boxX + boxW && ly >= checkY && ly < checkY + checkH)
    return 4;

  const double btnW = 100 * us;
  const double btnH = 32 * us;
  const double btnGap = 12 * us;
  const double totalBtnW = btnW * 2 + btnGap;
  const double btnY = app.mountDialogY + app.mountDialogH - 12 * us - btnH;
  const double mountX = app.mountDialogX + (app.mountDialogW - totalBtnW) * 0.5;
  const double cancelX = mountX + btnW + btnGap;

  if (ly >= btnY && ly < btnY + btnH) {
    if (lx >= mountX && lx < mountX + btnW) return 5;
    if (lx >= cancelX && lx < cancelX + btnW) return 6;
  }

  return -1;
}

void desktop_mount_dialog_close(DesktopApp& app) {
   
  app.mountDialogOpen = false;
  app.mountDialogObjectPath.clear();
  app.mountDialogLabel.clear();
  app.mountDialogDevice.clear();
  app.mountDialogFstype.clear();
  app.mountDialogUuid.clear();
  app.mountDialogHoverItem = -1;
  app.mountDialogArmItem = -1;
  app.mountDialogMountBase = 0;
  app.mountDialogBoot = false;
  app.mountDialogDropdownOpen = false;
}

bool desktop_mount_dialog_hit(DesktopApp& app, double lx, double ly) {
   
  if (!app.mountDialogOpen) return false;
  return lx >= app.mountDialogX && ly >= app.mountDialogY &&
         lx <= app.mountDialogX + app.mountDialogW &&
         ly <= app.mountDialogY + app.mountDialogH;
}

bool desktop_mount_dialog_handle_left_press(DesktopApp& app) {
   
  if (!app.mountDialogOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.mountDialogLayerIdx, &lx, &ly)) {
    desktop_mount_dialog_close(app);
    return true;
  }
  if (!desktop_mount_dialog_hit(app, lx, ly)) {
    desktop_mount_dialog_close(app);
    return true;
  }
  app.mountDialogArmItem = mount_dialog_hit_item(app, lx, ly);
  return true;
}

void desktop_icon_menu_close(DesktopApp& app) {
   
  eh_desktop_log("icon_menu_close: wasOpen=%d openWith.open=%d targetIdx=%d layers=%zu",
                 app.iconCtxMenuOpen, app.openWith.open, app.iconCtxTargetIdx, app.layers.size());
  app.iconCtxMenuOpen = false;
  app.iconCtxMenuHoverMainRow = -1;
  app.iconCtxMenuHoverCompressRow = -1;
  app.iconCtxMenuArmMainRow = -1;
  app.iconCtxMenuArmCompressRow = -1;
  app.iconCtxCompressCascadeOpen = false;
  app.iconCtxCompressAnchorRow = -1;
  app.iconCtxCompressCascadeX = 0;
  app.iconCtxCompressCascadeY = 0;
  app.iconCtxCompressCascadeW = 0;
  app.iconCtxTargetIdx = -1;
  app.iconCtxSelection.clear();
  app.iconCtxMenuRows.clear();
  app.iconCtxDesktopEntry.reset();
  desktop_mount_dialog_close(app);
}

void desktop_icon_menu_open(DesktopApp& app, int targetIdx) {
   
  desktop_menu_close(app);
  desktop_icon_menu_close(app);
  if (targetIdx < 0 || static_cast<size_t>(targetIdx) >= app.icons.size()) {
    eh_desktop_log("icon_menu_open: FAIL targetIdx=%d out of range icons=%zu", targetIdx, app.icons.size());
    return;
  }
  if (app.pointerLayerIdx >= app.layers.size() || !app.layers[app.pointerLayerIdx]) {
    eh_desktop_log("icon_menu_open: FAIL pointerLayerIdx=%zu out of range layers=%zu", app.pointerLayerIdx, app.layers.size());
    return;
  }
  DesktopLayer& L = *app.layers[app.pointerLayerIdx];
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.pointerLayerIdx, &lx, &ly)) {
    eh_desktop_log("icon_menu_open: FAIL pointer_local_xy");
    return;
  }

  std::vector<int> sel;
  if (!app.iconMarqueeSelection.empty() &&
      std::find(app.iconMarqueeSelection.begin(), app.iconMarqueeSelection.end(), targetIdx) != app.iconMarqueeSelection.end()) {
    sel = app.iconMarqueeSelection;
    std::sort(sel.begin(), sel.end());
    sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
  }
  if (sel.empty()) sel.push_back(targetIdx);

  app.iconCtxMenuOpen = true;
  app.iconCtxMenuLayerIdx = app.pointerLayerIdx;
  app.iconCtxMenuX = lx;
  app.iconCtxMenuY = ly;
  app.iconCtxMenuHoverMainRow = -1;
  app.iconCtxMenuHoverCompressRow = -1;
  app.iconCtxMenuArmMainRow = -1;
  app.iconCtxMenuArmCompressRow = -1;
  app.iconCtxCompressCascadeOpen = false;
  app.iconCtxCompressAnchorRow = -1;
  refresh_compress_cascade_width(app);
  app.iconCtxTargetIdx = targetIdx;
  app.iconCtxSelection = std::move(sel);
  rebuild_icon_ctx_rows(app, app.iconCtxSelection);
  {
    const DesktopIconItem& ic = app.icons[static_cast<size_t>(targetIdx)];
    const bool pinned = dock_settings_is_app_pinned(ic.desktop_path);
    refresh_icon_menu_width(app, ic, pinned);
  }
  clamp_menu(app, L.configuredWidth, L.configuredHeight);
  eh_desktop_log("icon_menu_open: OK targetIdx=%d rows=%zu sel=%zu openWith.open=%d",
                 targetIdx, app.iconCtxMenuRows.size(), app.iconCtxSelection.size(), app.openWith.open);
  paint_layer(app, L);
  if (app.display) wl_display_flush(app.display);
}

bool desktop_mount_dialog_left_release(DesktopApp& app) {
   
  if (!app.mountDialogOpen) return false;

  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.mountDialogLayerIdx, &lx, &ly)) {
    desktop_mount_dialog_close(app);
    if (app.mountDialogLayerIdx < app.layers.size() && app.layers[app.mountDialogLayerIdx]) {
      paint_layer(app, *app.layers[app.mountDialogLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  const int armed = app.mountDialogArmItem;
  app.mountDialogArmItem = -1;
  const int item = mount_dialog_hit_item(app, lx, ly);

  if (item != armed || item < 0) {
    if (!desktop_mount_dialog_hit(app, lx, ly))
      desktop_mount_dialog_close(app);
    if (app.mountDialogLayerIdx < app.layers.size() && app.layers[app.mountDialogLayerIdx]) {
      paint_layer(app, *app.layers[app.mountDialogLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  if (item == 0) {
    app.mountDialogDropdownOpen = !app.mountDialogDropdownOpen;
  } else if (item >= 1 && item <= 3) {
    app.mountDialogMountBase = item - 1;
    app.mountDialogDropdownOpen = false;
  } else if (item == 4) {
    app.mountDialogBoot = !app.mountDialogBoot;
  } else if (item == 5) {
    // Fire immediately — mount_async is non-blocking (D-Bus), pkexec runs on std::async
    auto& svc = eh::drives::UDisks2DriveService::instance();
    if (app.mountDialogMountBase == 0) {
      auto p = std::make_shared<std::promise<void>>();
      app.mountAsyncOp = p->get_future().share();
      svc.mount_async(app.mountDialogObjectPath,
          [p, &svc, fstype = app.mountDialogFstype, uuid = app.mountDialogUuid,
           label = app.mountDialogLabel, boot = app.mountDialogBoot,
           objPath = app.mountDialogObjectPath] (bool ok) {
        if (ok && boot)
          svc.add_fstab_async(objPath, compute_mount_path(label, 0), fstype, uuid,
                              [p] (bool) { p->set_value(); });
        else
          p->set_value();
      });
    } else {
      app.mountAsyncOp = std::async(std::launch::async,
          [device = app.mountDialogDevice, label = app.mountDialogLabel,
           mountBase = app.mountDialogMountBase]() {
        const std::string mount_path = compute_mount_path(label, mountBase);
        const std::string cmd = "pkexec mkdir -p " + sh_quote(mount_path) +
                                " && pkexec mount " + sh_quote(device) + " " + sh_quote(mount_path);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
          std::cerr << "[mount] pkexec mount failed (rc=" << rc << ")\n";
      }).share();
    }
    desktop_mount_dialog_close(app);
  } else {
    desktop_mount_dialog_close(app);
  }

  if (app.mountDialogLayerIdx < app.layers.size() && app.layers[app.mountDialogLayerIdx]) {
    paint_layer(app, *app.layers[app.mountDialogLayerIdx]);
    if (app.display) wl_display_flush(app.display);
  }
  return true;
}

void desktop_mount_dialog_process_deferred_action(DesktopApp& app) {
   
  // Check if a previous async operation completed
  if (app.mountAsyncOp.valid()) {
    if (app.mountAsyncOp.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      app.mountAsyncOp = {};
      desktop_icons_defer_rescan_after_background_io(app);
      paint_all_layers(app);
      if (app.display) wl_display_flush(app.display);
    }
    return;
  }

  // Launch a new deferred operation
  auto kind = app.mountDialogDeferredKind;
  if (kind == MountDeferredKind::None) return;
  app.mountDialogDeferredKind = MountDeferredKind::None;

  auto& svc = eh::drives::UDisks2DriveService::instance();

  switch (kind) {
    case MountDeferredKind::MountDialog:
      // Handled directly in desktop_mount_dialog_left_release
      break;
    case MountDeferredKind::FstabMount: {
      auto p = std::make_shared<std::promise<void>>();
      app.mountAsyncOp = p->get_future().share();
      svc.mount_async(app.mountDialogDeferredObjectPath, [p](bool) { p->set_value(); });
      break;
    }
    case MountDeferredKind::Unmount: {
      auto p = std::make_shared<std::promise<void>>();
      app.mountAsyncOp = p->get_future().share();
      svc.unmount_async(app.mountDialogDeferredObjectPath, [p](bool) { p->set_value(); });
      break;
    }
    default:
      break;
  }
}

bool desktop_mount_dialog_pointer_motion(DesktopApp& app) {
   
  if (!app.mountDialogOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.mountDialogLayerIdx, &lx, &ly)) return false;
  const int prev = app.mountDialogHoverItem;
  app.mountDialogHoverItem = mount_dialog_hit_item(app, lx, ly);
  if (prev == app.mountDialogHoverItem) return true;
  if (app.mountDialogLayerIdx < app.layers.size() && app.layers[app.mountDialogLayerIdx])
    paint_layer(app, *app.layers[app.mountDialogLayerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

bool desktop_icon_menu_handle_left_press(DesktopApp& app) {
   
  if (!app.iconCtxMenuOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.iconCtxMenuLayerIdx, &lx, &ly)) {
    desktop_icon_menu_close(app);
    if (app.iconCtxMenuLayerIdx < app.layers.size() && app.layers[app.iconCtxMenuLayerIdx]) {
      paint_layer(app, *app.layers[app.iconCtxMenuLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }
  if (!pointer_in_icon_menu_bounds(app, lx, ly)) {
    desktop_icon_menu_close(app);
    if (app.iconCtxMenuLayerIdx < app.layers.size() && app.layers[app.iconCtxMenuLayerIdx]) {
      paint_layer(app, *app.layers[app.iconCtxMenuLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  const int sub = hit_icon_menu_compress_row(app, lx, ly);
  const int main = hit_icon_menu_main_row(app, lx, ly);
  if (sub == -2 && main == -2) {
    app.iconCtxMenuArmMainRow = -1;
    app.iconCtxMenuArmCompressRow = -1;
    return true;
  }
  if (sub >= 0) {
    app.iconCtxMenuArmMainRow = -1;
    app.iconCtxMenuArmCompressRow = sub;
    return true;
  }
  if (main >= 0) {
    app.iconCtxMenuArmMainRow = main;
    app.iconCtxMenuArmCompressRow = -1;
    return true;
  }
  app.iconCtxMenuArmMainRow = -1;
  app.iconCtxMenuArmCompressRow = -1;
  return true;
}

bool desktop_icon_menu_pointer_motion(DesktopApp& app) {
   
  if (!app.iconCtxMenuOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.iconCtxMenuLayerIdx, &lx, &ly)) return false;
  const int prev_m = app.iconCtxMenuHoverMainRow;
  const int prev_c = app.iconCtxMenuHoverCompressRow;
  update_icon_menu_hover(app, lx, ly);
  if (prev_m == app.iconCtxMenuHoverMainRow && prev_c == app.iconCtxMenuHoverCompressRow) return true;
  if (app.iconCtxMenuLayerIdx < app.layers.size() && app.layers[app.iconCtxMenuLayerIdx])
    paint_layer(app, *app.layers[app.iconCtxMenuLayerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

bool desktop_icon_menu_left_release(DesktopApp& app) {
   
  if (!app.iconCtxMenuOpen) return false;
  const int armed_m = app.iconCtxMenuArmMainRow;
  const int armed_c = app.iconCtxMenuArmCompressRow;
  double lx = 0, ly = 0;
  int rel_m = -3;
  int rel_c = -3;
  if (desktop_pointer_local_xy(app, app.iconCtxMenuLayerIdx, &lx, &ly)) {
    rel_m = hit_icon_menu_main_row(app, lx, ly);
    rel_c = hit_icon_menu_compress_row(app, lx, ly);
  }

  const size_t menu_layer = app.iconCtxMenuLayerIdx;
  update_icon_menu_hover(app, lx, ly);

  std::vector<IconCtxMenuRow> rows_snap = app.iconCtxMenuRows;
  std::optional<DesktopEntryInfo> entry_snap = app.iconCtxDesktopEntry;
  std::vector<DesktopIconItem> items_snap;
  for (int i : app.iconCtxSelection) {
    if (i >= 0 && static_cast<size_t>(i) < app.icons.size()) items_snap.push_back(app.icons[static_cast<size_t>(i)]);
  }

  const bool hit_sub = rel_c >= 0;
  const bool hit_main = rel_m >= 0;

  if (hit_sub && armed_c >= 0 && rel_c == armed_c) {
    const IconCtxMenuRow spec = compress_sub_row_to_spec(rel_c);
    eh_desktop_log("icon_menu_left_rel: compress_sub row=%d", rel_c);
    desktop_icon_menu_close(app);
    apply_icon_menu_snapshot(app, items_snap, spec, entry_snap);
    if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
      paint_layer(app, *app.layers[menu_layer]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  if (hit_main && armed_m >= 0 && rel_m == armed_m && armed_c < 0) {
    if (static_cast<size_t>(rel_m) < rows_snap.size() && rows_snap[static_cast<size_t>(rel_m)].kind == IconCtxMenuRow::Kind::CompressSubmenu) {
      app.iconCtxCompressCascadeOpen = true;
      app.iconCtxCompressAnchorRow = rel_m;
      if (app.iconCtxCompressCascadeW <= 0) refresh_compress_cascade_width(app);
      if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
        DesktopLayer& L = *app.layers[menu_layer];
        clamp_menu(app, L.configuredWidth, L.configuredHeight);
        app.iconCtxMenuArmMainRow = -1;
        app.iconCtxMenuArmCompressRow = -1;
        paint_layer(app, L);
        if (app.display) wl_display_flush(app.display);
      }
      return true;
    }
    int kind = static_cast<int>(rows_snap[static_cast<size_t>(rel_m)].kind);
    eh_desktop_log("icon_menu_left_rel: main row=%d kind=%d openWith.open=%d", rel_m, kind, app.openWith.open);
    desktop_icon_menu_close(app);
    apply_icon_menu_snapshot(app, items_snap, rows_snap[static_cast<size_t>(rel_m)], entry_snap);
    if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
      paint_layer(app, *app.layers[menu_layer]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  eh_desktop_log("icon_menu_left_rel: miss hit_main=%d armed_m=%d rel_m=%d -> close", hit_main, armed_m, rel_m);
  desktop_icon_menu_close(app);
  if (menu_layer < app.layers.size() && app.layers[menu_layer]) {
    paint_layer(app, *app.layers[menu_layer]);
    if (app.display) wl_display_flush(app.display);
  }
  return true;
}

static void paint_radio(cairo_t* cr, double x, double y, bool selected, bool hover) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double cx = std::floor(x) + 12.5 * us;
  const double cy = std::floor(y) + 12.5 * us;
  const double ri = 6 * us;
  if (hover) {
    cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.3);
    cairo_arc(cr, cx, cy, ri + 3 * us, 0, 2 * std::numbers::pi);
    cairo_fill(cr);
  }
  if (selected) {
    cairo_set_source_rgba(cr, 0.3, 0.55, 0.95, 0.95);
    cairo_arc(cr, cx, cy, ri - 2 * us, 0, 2 * std::numbers::pi);
    cairo_fill(cr);
  } else {
    cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.5);
    cairo_arc(cr, cx, cy, ri - 1 * us, 0, 2 * std::numbers::pi);
    cairo_fill(cr);
  }
}

static void paint_checkbox(cairo_t* cr, double x, double y, bool checked, bool hover) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double bx = std::floor(x) + 5.5 * us;
  const double by = std::floor(y) + 8.5 * us;
  const double s = 14 * us;
  if (hover) {
    cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.3);
    path_rounded_rect(cr, bx - 2 * us, by - 2 * us, s + 3 * us, s + 3 * us, 4 * us);
    cairo_fill(cr);
  }
  if (checked) {
    cairo_set_source_rgba(cr, 0.3, 0.55, 0.95, 0.3);
    path_rounded_rect(cr, bx, by, s - 1 * us, s - 1 * us, 3 * us);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.3, 0.55, 0.95, 0.95);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, bx + 3 * us, by + 7 * us);
    cairo_line_to(cr, bx + 7 * us, by + 11 * us);
    cairo_line_to(cr, bx + 12 * us, by + 4 * us);
    cairo_stroke(cr);
  } else {
    cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.4);
    path_rounded_rect(cr, bx, by, s - 1 * us, s - 1 * us, 3 * us);
    cairo_fill(cr);
  }
}

static void paint_mount_dialog(DesktopApp& app, cairo_t* cr) {
   
  if (!app.mountDialogOpen) return;
  if (app.mountDialogLayerIdx >= app.layers.size() || !app.layers[app.mountDialogLayerIdx]) return;
  const DesktopLayer& L = *app.layers[app.mountDialogLayerIdx];
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  const double dlgW = 400 * us;
  const double boxH = 26 * us;
  const double itemH = 22 * us;
  const double listExtra = app.mountDialogDropdownOpen ? (3 * itemH + 8 * us) : 0;
  const double dlgH = 210 * us + listExtra;
  app.mountDialogW = dlgW;
  app.mountDialogH = dlgH;
  app.mountDialogX = (static_cast<double>(L.configuredWidth) - dlgW) * 0.5;
  app.mountDialogY = (static_cast<double>(L.configuredHeight) - dlgH) * 0.5;

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
  const char* user = std::getenv("USER");
  if (!user) user = "user";

  cairo_save(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_paint(cr);

  const double radius = 10 * us;
  eh::shell::shared::paint_glass_card(cr, static_cast<double>(app.mountDialogX),
                                      static_cast<double>(app.mountDialogY), dlgW, dlgH, radius, mc, 1.0);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15.0 * us);
  cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
  cairo_move_to(cr, app.mountDialogX + 16 * us, app.mountDialogY + 24 * us);
  cairo_show_text(cr, "Mount Drive");

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0 * us);
  cairo_set_source_rgba(cr, 0.75, 0.78, 0.82, 0.9);
  std::string dl = "\"" + app.mountDialogLabel + "\"";
  cairo_move_to(cr, app.mountDialogX + 16 * us, app.mountDialogY + 46 * us);
  cairo_show_text(cr, dl.c_str());

  cairo_set_font_size(cr, 12.0 * us);
  cairo_set_source_rgba(cr, 0.65, 0.68, 0.72, 0.85);
  cairo_move_to(cr, app.mountDialogX + 16 * us, app.mountDialogY + 66 * us);
  cairo_show_text(cr, "Mount point:");

  const double boxX = app.mountDialogX + 16 * us;
  const double boxY = app.mountDialogY + 78 * us;
  const double boxW = dlgW - 32 * us;

  std::string labs[3];
  labs[0] = "/run/media/" + std::string(user) + "/" + app.mountDialogLabel;
  labs[1] = "/media/" + app.mountDialogLabel;
  labs[2] = "/mnt/" + app.mountDialogLabel;

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12.0 * us);

  // Dropdown box
  const bool boxHov = app.mountDialogHoverItem == 0;
  if (boxHov) {
    cairo_set_source_rgba(cr, 0.25, 0.28, 0.35, 0.85);
  } else {
    cairo_set_source_rgba(cr, 0.15, 0.18, 0.24, 0.85);
  }
  cairo_new_path(cr);
  path_rounded_rect(cr, boxX, boxY, boxW, boxH, 4 * us);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.25);
  cairo_set_line_width(cr, 1);
  cairo_new_path(cr);
  path_rounded_rect(cr, boxX + 0.5 * us, boxY + 0.5 * us, boxW - 1 * us, boxH - 1 * us, 3.5 * us);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 1.0);
  cairo_move_to(cr, boxX + 10 * us, boxY + 18 * us);
  cairo_show_text(cr, labs[app.mountDialogMountBase].c_str());

  // Chevron
  const char* chevron = app.mountDialogDropdownOpen ? "\xE2\x96\xB2" : "\xE2\x96\xBC";
  cairo_set_font_size(cr, 10.0 * us);
  cairo_set_source_rgba(cr, 0.6, 0.65, 0.7, 0.8);
  cairo_text_extents_t ce{};
  cairo_text_extents(cr, chevron, &ce);
  cairo_move_to(cr, boxX + boxW - 10 * us - ce.x_advance, boxY + 18 * us);
  cairo_show_text(cr, chevron);
  cairo_set_font_size(cr, 12.0 * us);

  // Dropdown list items
  if (app.mountDialogDropdownOpen) {
    const double listTop = boxY + boxH;
    const double listH = 3 * itemH;
    cairo_set_source_rgba(cr, 0.15, 0.18, 0.24, 0.95);
    path_rounded_rect(cr, boxX, listTop, boxW, listH, 4 * us);
    cairo_fill(cr);

    for (int i = 0; i < 3; ++i) {
      const double iy = listTop + static_cast<double>(i) * itemH;
      const bool sel = app.mountDialogMountBase == i;
      const bool hov = app.mountDialogHoverItem == i + 1;
      if (hov) {
        cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.25);
        cairo_rectangle(cr, boxX + 2 * us, iy + 1 * us, boxW - 4 * us, itemH - 2 * us);
        cairo_fill(cr);
      }
      paint_radio(cr, boxX, iy, sel, false);
      cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
      cairo_move_to(cr, boxX + 26 * us, iy + 16 * us);
      cairo_show_text(cr, labs[i].c_str());
    }
  }

  // Checkbox
  const double checkX = boxX;
  const double checkY = boxY + boxH + (app.mountDialogDropdownOpen ? (3 * itemH) : 0) + 8 * us;
  const bool checkHov = app.mountDialogHoverItem == 4;
  paint_checkbox(cr, checkX, checkY, app.mountDialogBoot, checkHov);
  cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
  cairo_move_to(cr, checkX + 26 * us, checkY + 22 * us);
  cairo_show_text(cr, "Mount at boot");

  // Buttons
  const double btnW = 100 * us;
  const double btnH = 32 * us;
  const double btnGap = 12 * us;
  const double totalW = btnW * 2 + btnGap;
  const double btnY = app.mountDialogY + dlgH - 12 * us - btnH;
  const double mountX = app.mountDialogX + (dlgW - totalW) * 0.5;
  const double cancelX = mountX + btnW + btnGap;

  auto paint_btn = [&](double bx, double by, const char* label, bool hover) {
    if (hover) {
      cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.9);
    } else {
      cairo_set_source_rgba(cr, 0.15, 0.18, 0.24, 0.85);
    }
    cairo_new_path(cr);
    path_rounded_rect(cr, bx, by, btnW, btnH, 6 * us);
    cairo_fill(cr);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
    cairo_text_extents_t te{};
    cairo_text_extents(cr, label, &te);
    cairo_move_to(cr, bx + (btnW - te.x_advance) * 0.5, by + btnH * 0.5 + te.height * 0.3);
    cairo_show_text(cr, label);
  };

  paint_btn(mountX, btnY, "Mount", app.mountDialogHoverItem == 5);
  paint_btn(cancelX, btnY, "Cancel", app.mountDialogHoverItem == 6);

  cairo_restore(cr);
}

void desktop_icon_menu_paint(DesktopApp& app, size_t layer_index, cairo_t* cr) {
   
  paint_mount_dialog(app, cr);
  if (app.worldClockSettingsOpen && app.worldClockSettingsLayerIdx == layer_index)
    world_clock_settings_paint(app, cr);
  paint_open_with(app, cr);
  if (!app.iconCtxMenuOpen || layer_index != app.iconCtxMenuLayerIdx) {
    eh_desktop_log("icon_menu_paint: skip menuOpen=%d layer_idx=%zu != menuLayerIdx=%zu",
                   app.iconCtxMenuOpen, layer_index, app.iconCtxMenuLayerIdx);
    return;
  }
  if (app.iconCtxTargetIdx < 0 || static_cast<size_t>(app.iconCtxTargetIdx) >= app.icons.size()) {
    eh_desktop_log("icon_menu_paint: bad targetIdx=%d icons=%zu", app.iconCtxTargetIdx, app.icons.size());
    return;
  }

  const DesktopIconItem& ic = app.icons[static_cast<size_t>(app.iconCtxTargetIdx)];
  const bool pinned = dock_settings_is_app_pinned(ic.desktop_path);

  double mx, my, mw, mh;
  menu_bounds(app, &mx, &my, &mw, &mh);

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double ov = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::DesktopContextMenu));
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  cairo_save(cr);
  auto paint_panel = [&](double px, double py, double pw, double ph) {
    eh::shell::shared::paint_glass_card(cr, px, py, pw, ph, kMenuCornerRadius(), mc, ov);
  };

  paint_panel(mx, my, mw, mh);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0 * us);

  for (size_t i = 0; i < app.iconCtxMenuRows.size(); ++i) {
    const double ry = my + kPad() + static_cast<double>(i) * kRowH();
    const IconCtxMenuRow& rr = app.iconCtxMenuRows[i];
    if (row_is_sep(rr)) {
      cairo_save(cr);
      cairo_new_path(cr);
      cairo_set_source_rgba(cr, 0.35, 0.38, 0.42, 0.7 * ov);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, mx + kPad(), ry + kRowH() * 0.5);
      cairo_line_to(cr, mx + mw - kPad(), ry + kRowH() * 0.5);
      cairo_stroke(cr);
      cairo_restore(cr);
      continue;
    }
    const bool row_hover = static_cast<int>(i) == app.iconCtxMenuHoverMainRow;
    const bool anchor_glow =
        app.iconCtxCompressCascadeOpen && static_cast<int>(i) == app.iconCtxCompressAnchorRow && app.iconCtxMenuHoverCompressRow >= 0;
    if (row_hover || anchor_glow) {
      cairo_set_source_rgba(cr, 0.18, 0.22, 0.3, 0.85 * ov);
      path_rounded_rect(cr, mx + 2 * us, ry, mw - 4 * us, kRowH(), kRowHighlightRadius());
      cairo_fill(cr);
    }
    cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 0.95);
    const char* lb = row_label(app, i, ic, pinned);
    cairo_move_to(cr, mx + kPad(), ry + kRowH() * 0.72);
    cairo_show_text(cr, lb);
    if (row_is_compress_submenu(rr)) {
      cairo_set_font_size(cr, 14.0 * us);
      cairo_move_to(cr, mx + mw - kPad() - 14 * us, ry + kRowH() * 0.72);
      cairo_show_text(cr, "\xE2\x80\xBA");
      cairo_set_font_size(cr, 13.0 * us);
    }
  }

  if (app.iconCtxCompressCascadeOpen) {
    const double cx = app.iconCtxCompressCascadeX;
    const double cy = app.iconCtxCompressCascadeY;
    const double cw = std::max(app.iconCtxCompressCascadeW, kMinCompressMenuW());
    const double ch = menu_height_rows(static_cast<size_t>(kCompressSubRows));
    paint_panel(cx, cy, cw, ch);
    for (int r = 0; r < kCompressSubRows; ++r) {
      const double ry = cy + kPad() + static_cast<double>(r) * kRowH();
      if (r == app.iconCtxMenuHoverCompressRow) {
        cairo_set_source_rgba(cr, 0.18, 0.22, 0.3, 0.85 * ov);
        path_rounded_rect(cr, cx + 2 * us, ry, cw - 4 * us, kRowH(), kRowHighlightRadius());
        cairo_fill(cr);
      }
      cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 0.95);
      cairo_move_to(cr, cx + kPad(), ry + kRowH() * 0.72);
      cairo_show_text(cr, compress_submenu_row_label(r));
    }
  }

  cairo_restore(cr);
}

}
