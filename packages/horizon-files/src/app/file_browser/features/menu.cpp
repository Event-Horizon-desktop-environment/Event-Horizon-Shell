#include "../app.hpp"
#include "app/file_browser/features/compress.hpp"
#include "app/file_browser/features/progress.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/shell_config.hpp"
#include "platform/common/palette/matugen_palette.hpp"
#include "platform/desktop/entries/desktop_xdg_ops.hpp"
#include "dialog/file_chooser_dialog.hpp"
#include "platform/widgets/app_drawer/list/desktop_list.hpp"

namespace fs = std::filesystem;
namespace xdg = eh::shell::desktop::xdg;
using menu_clock = std::chrono::steady_clock;

static std::uint64_t menu_expiry_3s() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             (menu_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch())
      .count();
}

namespace eh::file_browser {

// ── context menu ─────────────────────────────────────────────────

void open_context_menu(AppState& app, int item_idx, int x, int y) {
  app.context_menu_open = true;
  app.context_menu_x = x;
  app.context_menu_y = y;
  app.context_menu_hover = -1; app.context_menu_hover_prev = -1; app.context_menu_sub_hover = -1;
  app.context_menu_file_idx = item_idx;

  std::string term_label = "Open in Terminal";
  {
    const auto& sc = eh::config::shell_config_snapshot();
    if (!sc.defaultApps.terminal.empty()) {
      scan_terminal_apps(app);
      for (const auto& ta : app.term_chooser_apps) {
        std::string stem = ta.desktop_id;
        auto slash = stem.rfind('/');
        if (slash != std::string::npos) stem = stem.substr(slash + 1);
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        if (stem == sc.defaultApps.terminal || ta.desktop_id == sc.defaultApps.terminal ||
            ta.desktop_id == sc.defaultApps.terminal + ".desktop") {
          term_label = "Open in " + ta.name;
          break;
        }
      }
    }
  }

  if (item_idx >= 0 &&
      item_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
    app.cur_tab().selected_idx = item_idx;
    // Keep existing multi-selection if right-clicked item is already part of it
    if (std::find(app.cur_tab().multi_selected.begin(), app.cur_tab().multi_selected.end(), item_idx) == app.cur_tab().multi_selected.end()) {
      app.cur_tab().multi_selected = {item_idx};
    }
    app.context_menu_items = {
      AppState::menu_item(AppState::ContextMenuAction::Open, "Open"),
      AppState::menu_item(AppState::ContextMenuAction::OpenWith, "Open With..."),
    };
    int real_idx = app.cur_tab().visible_entries[item_idx];
    bool is_dir = real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()) &&
                  app.cur_tab().entries[real_idx].is_dir;
    if (is_dir) {
      app.context_menu_items.push_back(
        AppState::menu_item(AppState::ContextMenuAction::OpenInNewTab, "Open in new tab"));
      app.context_menu_items.push_back(
        AppState::menu_item(AppState::ContextMenuAction::OpenInTerminal, term_label));
      app.context_menu_items.push_back(
        AppState::menu_item(AppState::ContextMenuAction::AddToFavorites, "Add to Favorites"));
    }
    app.context_menu_items.push_back(AppState::menu_separator());
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Copy, "Copy"));
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Paste, "Paste"));
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Rename, "Rename"));
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::OpenFileLocation, "Open File Location"));
    app.context_menu_items.push_back(AppState::menu_separator());

    // Actions submenu
    {
      AppState::ContextMenuItem actions_item;
      actions_item.action = AppState::ContextMenuAction::Separator;
      actions_item.label = "Actions";
      actions_item.sub_items = {
        AppState::menu_item(AppState::ContextMenuAction::CopyPath, "Copy Path"),
        AppState::menu_item(AppState::ContextMenuAction::Duplicate, "Duplicate"),
        AppState::menu_item(AppState::ContextMenuAction::CreateSymlink, "Create Symlink"),
        AppState::menu_separator(),
        AppState::menu_item(AppState::ContextMenuAction::Cut, "Cut"),
        AppState::menu_item(AppState::ContextMenuAction::CopyTo, "Copy to..."),
        AppState::menu_item(AppState::ContextMenuAction::MoveTo, "Move to..."),
        AppState::menu_item(AppState::ContextMenuAction::PermanentDelete, "Permanent Delete"),
      };
      app.context_menu_items.push_back(std::move(actions_item));
    }

    // Archive submenu
    {
      AppState::ContextMenuItem archive_item;
      archive_item.action = AppState::ContextMenuAction::Separator;
      archive_item.label = "Archive";
      archive_item.sub_items = {
        AppState::menu_item(AppState::ContextMenuAction::Compress, "Compress..."),
      };
      if (!is_dir && real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size())) {
        if (is_archive_extension(app.cur_tab().entries[real_idx].path)) {
          archive_item.sub_items.push_back(AppState::menu_item(AppState::ContextMenuAction::BrowseArchive, "Browse Archive"));
          archive_item.sub_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Extract, "Extract"));
          archive_item.sub_items.push_back(AppState::menu_item(AppState::ContextMenuAction::ExtractTo, "Extract to..."));
        }
      }
      app.context_menu_items.push_back(std::move(archive_item));
    }

    app.context_menu_items.push_back(AppState::menu_separator());
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::MoveToTrash, "Move to Trash"));
    app.context_menu_items.push_back(AppState::menu_separator());
    app.context_menu_items.push_back(AppState::menu_item(AppState::ContextMenuAction::Properties, "Properties"));
  } else {
    app.context_menu_items = {
      AppState::menu_item(AppState::ContextMenuAction::NewFolder, "New Folder"),
      AppState::menu_separator(),
      AppState::menu_item(AppState::ContextMenuAction::OpenInTerminal, term_label),
      AppState::menu_separator(),
      AppState::menu_item(AppState::ContextMenuAction::Paste, "Paste"),
      AppState::menu_item(AppState::ContextMenuAction::Properties, "Properties"),
    };
  }

  int cm_w = 200;
  int cm_h = 0;
  for (const auto& item : app.context_menu_items) {
    if (item.action == AppState::ContextMenuAction::Separator && item.sub_items.empty())
      cm_h += 8;
    else
      cm_h += 32;
  }
  if (app.context_menu_x + cm_w > app.width)
    app.context_menu_x = app.width - cm_w - 4;
  if (app.context_menu_y + cm_h > app.height)
    app.context_menu_y = app.height - cm_h - 4;
  if (app.context_menu_x < 4) app.context_menu_x = 4;
  if (app.context_menu_y < 4) app.context_menu_y = 4;
}

// ── context menu action execution ────────────────────────────────

void execute_context_menu_action(AppState& app, int item_idx) {
  if (item_idx < 0 ||
      item_idx >= static_cast<int>(app.context_menu_items.size()))
    return;

  auto action = app.context_menu_items[item_idx].action;
  app.context_menu_open = false;

  if (action == AppState::ContextMenuAction::Separator)
    return;

  // ── Path editing context menu actions ──
  if (app.context_menu_file_idx == -3) {
    auto& m_buf = app.active_pane ? app.r_path_edit_buf : app.path_edit_buf;
    auto& m_sel_start = app.active_pane ? app.r_path_edit_sel_start : app.path_edit_sel_start;
    auto& m_sel_end = app.active_pane ? app.r_path_edit_sel_end : app.path_edit_sel_end;
    if (action == AppState::ContextMenuAction::Copy) {
      if (m_sel_start >= 0 && m_sel_start != m_sel_end) {
        int sel_a = std::min(m_sel_start, m_sel_end);
        int sel_b = std::max(m_sel_start, m_sel_end);
        std::string sel = m_buf.substr(sel_a, sel_b - sel_a);
        if (!sel.empty()) app.clipboard.copy_text(sel);
      } else {
        app.clipboard.copy_text(m_buf);
      }
      draw(app);
      return;
    }
  }

  if (action == AppState::ContextMenuAction::NewFolder) {
    app.create_dialog_open = true;
    app.create_is_folder = true;
    app.create_buf = "New Folder";
    app.create_cursor_pos = static_cast<int>(app.create_buf.size());
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::NewDocument) {
    app.create_dialog_open = true;
    app.create_is_folder = false;
    app.create_buf = "New Document";
    app.create_cursor_pos = static_cast<int>(app.create_buf.size());
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::Reload) {
    reload_dir(app);
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::CopyLocation) {
    app.clipboard.copy_text(app.cur_tab().current_path);
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::SelectAll) {
    app.cur_tab().multi_selected.clear();
    for (int vi = 0; vi < static_cast<int>(app.cur_tab().visible_entries.size()); ++vi)
      app.cur_tab().multi_selected.push_back(vi);
    draw(app);
    return;
  }

  // ── Dots menu "Open With…" — use selected file if any ──
  if (action == AppState::ContextMenuAction::OpenWith) {
    int sel = app.cur_tab().selected_idx;
    if (sel >= 0 && sel < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real = app.cur_tab().visible_entries[sel];
      if (real >= 0 && real < static_cast<int>(app.cur_tab().entries.size()))
        open_with_open(app, app.cur_tab().entries[real].path);
    }
    draw(app);
    return;
  }

  // ── Background/dots menu "Properties" — current directory ──
  if (action == AppState::ContextMenuAction::Properties &&
      (app.context_menu_file_idx == -1 || app.context_menu_file_idx == -5)) {
    show_properties(app, app.cur_tab().current_path);
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::Paste) {
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
              app.operation_status_expires_ms = menu_expiry_3s();
            }
            app.op_progress.reset();
            reload_dir(app);
            draw(app);
          });
      app.op_progress = prog;
    }
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::OpenInTerminal) {
    std::string target_dir = app.cur_tab().current_path;
    if (app.context_menu_file_idx >= 0 &&
        app.context_menu_file_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.context_menu_file_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()) &&
          app.cur_tab().entries[real_idx].is_dir) {
        target_dir = app.cur_tab().entries[real_idx].path;
      }
    }
    open_terminal_at(app, target_dir);
    return;
  }

  if (action == AppState::ContextMenuAction::RemoveFromFavorites) {
    if (app.context_menu_sidebar_idx >= 0 &&
        app.context_menu_sidebar_idx < static_cast<int>(app.sidebar_locations.size())) {
      const auto& loc = app.sidebar_locations[app.context_menu_sidebar_idx];
      auto it = std::find(app.favorites.begin(), app.favorites.end(), loc.path);
      if (it != app.favorites.end()) {
        app.favorites.erase(it);
        save_file_browser_settings(app);
        refresh_sidebar(app);
      }
    }
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::UnmountDrive) {
    if (app.context_menu_sidebar_idx >= 0 &&
        app.context_menu_sidebar_idx < static_cast<int>(app.sidebar_locations.size())) {
      unmount_drive(app, app.context_menu_sidebar_idx);
    }
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::MountDrive) {
    if (app.context_menu_sidebar_idx >= 0 &&
        app.context_menu_sidebar_idx < static_cast<int>(app.sidebar_locations.size())) {
      mount_drive(app, app.context_menu_sidebar_idx);
    }
    draw(app);
    return;
  }

  // ── Open Trash in current tab ──
  if (action == AppState::ContextMenuAction::Open) {
    if (app.context_menu_sidebar_idx >= 0 &&
        app.context_menu_sidebar_idx < static_cast<int>(app.sidebar_locations.size())) {
      auto& loc = app.sidebar_locations[app.context_menu_sidebar_idx];
      if (loc.kind == SidebarLocation::Kind::Trash) {
        app.cur_tab().current_path = loc.path;
        navigate_to(app, loc.path);
      }
    }
    draw(app);
    return;
  }

  // ── Empty Trash ──
  if (action == AppState::ContextMenuAction::EmptyTrash) {
    const char* home = std::getenv("HOME");
    if (home) {
      fs::path trash_dir(home);
      trash_dir /= ".local/share/Trash";
      std::error_code ec;
      fs::remove_all(trash_dir / "files", ec);
      fs::remove_all(trash_dir / "info", ec);
      fs::create_directories(trash_dir / "files", ec);
      fs::create_directories(trash_dir / "info", ec);
      app.operation_status = "Trash emptied";
      app.operation_status_expires_ms = menu_expiry_3s();
      reload_dir(app);
    }
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::AddToFavorites) {
    if (app.context_menu_file_idx >= 0 &&
        app.context_menu_file_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.context_menu_file_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()) &&
          app.cur_tab().entries[real_idx].is_dir) {
        const auto& path = app.cur_tab().entries[real_idx].path;
        if (std::find(app.favorites.begin(), app.favorites.end(), path) == app.favorites.end()) {
          app.favorites.push_back(path);
          save_file_browser_settings(app);
          refresh_sidebar(app);
        }
      }
    }
    draw(app);
    return;
  }

  if (action == AppState::ContextMenuAction::Settings) {
    open_settings(app);
    draw(app);
    return;
  }

  // ── Open in new tab ──
  if (action == AppState::ContextMenuAction::OpenInNewTab) {
    std::string target_dir;
    if (app.context_menu_file_idx == -2) {
      // Sidebar item
      if (app.context_menu_sidebar_idx >= 0 &&
          app.context_menu_sidebar_idx < static_cast<int>(app.sidebar_locations.size())) {
        target_dir = app.sidebar_locations[app.context_menu_sidebar_idx].path;
      }
    } else if (app.context_menu_file_idx >= 0 &&
        app.context_menu_file_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.context_menu_file_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()) &&
          app.cur_tab().entries[real_idx].is_dir) {
        target_dir = app.cur_tab().entries[real_idx].path;
      }
    }
    if (!target_dir.empty()) {
      int idx = static_cast<int>(app.tabs.size());
      app.tabs.emplace_back();
      app.tabs[idx].current_path = target_dir;
      app.active_tab = idx;
      navigate_to(app, target_dir);
    }
    draw(app);
    return;
  }

  // ── Open file location (navigate to parent dir) ──
  if (action == AppState::ContextMenuAction::OpenFileLocation) {
    if (app.context_menu_file_idx >= 0 &&
        app.context_menu_file_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
      int real_idx = app.cur_tab().visible_entries[app.context_menu_file_idx];
      if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size())) {
        std::string file_path = app.cur_tab().entries[real_idx].path;
        auto slash = file_path.rfind('/');
        if (slash != std::string::npos) {
          std::string parent = file_path.substr(0, slash);
          navigate_to(app, parent);
        }
      }
    }
    draw(app);
    return;
  }

  // ── Tab context menu actions ──
  if (app.context_menu_file_idx == -4) {
    if (action == AppState::ContextMenuAction::CloseTab) {
      if (app.context_menu_tab_idx >= 0 &&
          app.context_menu_tab_idx < static_cast<int>(app.tabs.size())) {
        app.active_tab = app.context_menu_tab_idx;
        close_tab(app);
      }
      draw(app);
      return;
    }
    if (action == AppState::ContextMenuAction::CloseOtherTabs) {
      if (app.context_menu_tab_idx >= 0 &&
          app.context_menu_tab_idx < static_cast<int>(app.tabs.size())) {
        Tab kept = std::move(app.tabs[app.context_menu_tab_idx]);
        app.tabs.clear();
        app.tabs.push_back(std::move(kept));
        app.active_tab = 0;
        reload_dir(app);
      }
      draw(app);
      return;
    }
    if (action == AppState::ContextMenuAction::CloseAllTabs) {
      app.tabs.clear();
      app.tabs.emplace_back();
      app.tabs[0].current_path = home_dir();
      app.active_tab = 0;
      reload_dir(app);
      draw(app);
      return;
    }
    if (action == AppState::ContextMenuAction::DuplicateTab) {
      if (app.context_menu_tab_idx >= 0 &&
          app.context_menu_tab_idx < static_cast<int>(app.tabs.size())) {
        auto src_path = app.tabs[app.context_menu_tab_idx].current_path;
        auto src_view = app.tabs[app.context_menu_tab_idx].view_mode;
        auto src_sort = app.tabs[app.context_menu_tab_idx].sort_field;
        auto src_desc = app.tabs[app.context_menu_tab_idx].sort_descending;
        int idx = static_cast<int>(app.tabs.size());
        app.tabs.emplace_back();
        auto& dup = app.tabs.back();
        dup.current_path = src_path;
        dup.view_mode = src_view;
        dup.sort_field = src_sort;
        dup.sort_descending = src_desc;
        dup.group_by_type = app.tabs[app.context_menu_tab_idx].group_by_type;
        app.active_tab = idx;
        navigate_to(app, dup.current_path);
      }
      draw(app);
      return;
    }
    draw(app);
    return;
  }

  if (app.context_menu_file_idx < 0 ||
      app.context_menu_file_idx >= static_cast<int>(app.cur_tab().visible_entries.size()))
    return;

  int real_idx = app.cur_tab().visible_entries[app.context_menu_file_idx];
  if (real_idx < 0 || real_idx >= static_cast<int>(app.cur_tab().entries.size()))
    return;

  auto& entry = app.cur_tab().entries[real_idx];

  switch (action) {
    case AppState::ContextMenuAction::Open:
      if (entry.is_dir) {
        navigate_to(app, entry.path);
      } else {
        xdg::open_path_in_default_application(entry.path);
      }
      break;

    case AppState::ContextMenuAction::OpenWith:
      open_with_open(app, entry.path);
      draw(app);
      return;

    case AppState::ContextMenuAction::Cut: {
      if (app.cur_tab().multi_selected.size() > 1) {
        std::vector<std::string> paths;
        for (int vis_idx : app.cur_tab().multi_selected) {
          if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
            int r = app.cur_tab().visible_entries[vis_idx];
            if (r >= 0 && r < static_cast<int>(app.cur_tab().entries.size()))
              paths.push_back(app.cur_tab().entries[r].path);
          }
        }
        if (!paths.empty()) app.clipboard.copy_files(true, paths);
      } else {
        app.clipboard.copy_files(true, {entry.path});
      }
      app.operation_status = "Cut to clipboard";
      app.operation_status_expires_ms = menu_expiry_3s();
      break;
    }

    case AppState::ContextMenuAction::Copy: {
      if (app.cur_tab().multi_selected.size() > 1) {
        std::vector<std::string> paths;
        for (int vis_idx : app.cur_tab().multi_selected) {
          if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
            int r = app.cur_tab().visible_entries[vis_idx];
            if (r >= 0 && r < static_cast<int>(app.cur_tab().entries.size()))
              paths.push_back(app.cur_tab().entries[r].path);
          }
        }
        if (!paths.empty()) app.clipboard.copy_files(false, paths);
      } else {
        app.clipboard.copy_files(false, {entry.path});
      }
      app.operation_status = "Copied to clipboard";
      app.operation_status_expires_ms = menu_expiry_3s();
      break;
    }

    case AppState::ContextMenuAction::CopyTo: {
      std::string dest;
      if (!eh::dialog::show_native_folder_picker(&dest)) { draw(app); return; }
      if (!dest.empty()) {
        fs::path src = entry.path;
        fs::path dst = fs::path(dest) / src.filename();

        // Push undo record first
        {
          AppState::UndoRecord rec{AppState::UndoRecord::Type::PasteCopy, {}, {}};
          rec.paths_b.push_back(dst.string());
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
        }

        // Start async copy
        auto prog = std::make_shared<OperationProgress>();
        prog->type = OperationType::Copy;
        start_async_op({src.string()}, dest, false, prog,
            [&app](bool cancelled) {
              if (!cancelled) {
                app.operation_status = "Copied to destination";
                app.operation_status_expires_ms = menu_expiry_3s();
              }
              app.op_progress.reset();
              reload_dir(app);
              draw(app);
            });
        app.op_progress = prog;
      }
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::MoveTo: {
      std::string dest;
      if (!eh::dialog::show_native_folder_picker(&dest)) { draw(app); return; }
      if (!dest.empty()) {
        fs::path src = entry.path;
        fs::path dst = fs::path(dest) / src.filename();

        // Push undo record first
        {
          AppState::UndoRecord rec{AppState::UndoRecord::Type::PasteCut, {}, {}};
          rec.paths_a.push_back(src.string());
          rec.paths_b.push_back(dst.string());
          app.redo_stack.clear();
          app.undo_stack.push_back(std::move(rec));
          if (app.undo_stack.size() > app.kMaxUndo)
            app.undo_stack.erase(app.undo_stack.begin());
        }

        // Start async move
        auto prog = std::make_shared<OperationProgress>();
        prog->type = OperationType::Move;
        start_async_op({src.string()}, dest, true, prog,
            [&app](bool cancelled) {
              if (!cancelled) {
                app.operation_status = "Moved to destination";
                app.operation_status_expires_ms = menu_expiry_3s();
              }
              app.op_progress.reset();
              reload_dir(app);
              draw(app);
            });
        app.op_progress = prog;
      }
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::Rename: {
      bool is_multi = app.cur_tab().multi_selected.size() > 1;
      if (is_multi) {
        // Batch rename via right-click
        app.batch_rename_entries.clear();
        auto all = app.cur_tab().multi_selected;
        // Ensure the right-clicked entry is included
        bool has_clicked = false;
        for (int vis_idx : all) {
          if (vis_idx == app.context_menu_file_idx) { has_clicked = true; break; }
        }
        if (!has_clicked && app.context_menu_file_idx >= 0)
          all.push_back(app.context_menu_file_idx);
        for (int vis_idx : all) {
          if (vis_idx < 0 || vis_idx >= static_cast<int>(app.cur_tab().visible_entries.size())) continue;
          int r = app.cur_tab().visible_entries[vis_idx];
          if (r < 0 || r >= static_cast<int>(app.cur_tab().entries.size())) continue;
          const auto& e2 = app.cur_tab().entries[r];
          std::string ext;
          auto dot = e2.name.rfind('.');
          if (dot == std::string::npos || dot == 0) ext.clear();
          else ext = e2.name.substr(dot);
          app.batch_rename_entries.push_back({e2.path, e2.name, ext, e2.name});
        }
        if (!app.batch_rename_entries.empty()) {
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
        }
      } else {
        app.rename_ui_open = true;
        app.rename_ui_old_name = entry.name;
        app.rename_ui_buf = entry.name;
        app.rename_ui_cursor_pos = static_cast<int>(entry.name.size());
        app.rename_ui_entry_path = entry.path;
      }
      break;
    }

    case AppState::ContextMenuAction::MoveToTrash: {
      {
        bool is_multi = app.cur_tab().multi_selected.size() > 1;
        int count = is_multi ? static_cast<int>(app.cur_tab().multi_selected.size()) : 1;
        app.confirm_title = "Move to Trash";
        if (count == 1)
          app.confirm_message = "Move \"" + entry.name + "\" to trash?";
        else
          app.confirm_message = "Move " + std::to_string(count) + " items to trash?";
        app.confirm_preview_path = count == 1 ? entry.path : "";
        app.confirm_item_count = count;
        std::vector<std::string> trash_paths;
        if (is_multi) {
          for (int vis_idx : app.cur_tab().multi_selected) {
            if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
              int r = app.cur_tab().visible_entries[vis_idx];
              if (r >= 0 && r < static_cast<int>(app.cur_tab().entries.size()))
                trash_paths.push_back(app.cur_tab().entries[r].path);
            }
          }
        } else {
          trash_paths.push_back(entry.path);
        }
        app.confirm_callback = [&app, paths = std::move(trash_paths)](bool ok) {
          if (!ok) return;
          for (const auto& p : paths) (void)xdg::trash_file(p);
          reload_dir(app);
          app.operation_status = "Moved to trash";
          app.operation_status_expires_ms = menu_expiry_3s();
        };
      }
      app.confirm_hover_btn = -1;
      app.confirm_open = true;
      draw(app);
      break;
    }

    case AppState::ContextMenuAction::PermanentDelete: {
      {
        bool is_multi = app.cur_tab().multi_selected.size() > 1;
        int count = is_multi ? static_cast<int>(app.cur_tab().multi_selected.size()) : 1;
        app.confirm_title = "Permanently Delete?";
        if (count == 1)
          app.confirm_message = "Permanently delete \"" + entry.name + "\"?";
        else
          app.confirm_message = "Permanently delete " + std::to_string(count) + " items?";
        app.confirm_preview_path = count == 1 ? entry.path : "";
        app.confirm_item_count = count;
        std::vector<std::pair<fs::path, bool>> delete_paths;
        if (is_multi) {
          for (int vis_idx : app.cur_tab().multi_selected) {
            if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
              int r = app.cur_tab().visible_entries[vis_idx];
              if (r >= 0 && r < static_cast<int>(app.cur_tab().entries.size()))
                delete_paths.emplace_back(app.cur_tab().entries[r].path, app.cur_tab().entries[r].is_dir);
            }
          }
        } else {
          delete_paths.emplace_back(entry.path, entry.is_dir);
        }
        app.confirm_callback = [&app, paths = std::move(delete_paths)](bool ok) {
          if (!ok) return;
          std::error_code ec;
          for (const auto& [p, is_dir] : paths) {
            if (is_dir) fs::remove_all(p, ec); else fs::remove(p, ec);
          }
          reload_dir(app);
          app.operation_status = "Permanently deleted";
          app.operation_status_expires_ms = menu_expiry_3s();
        };
      }
      app.confirm_hover_btn = -1;
      app.confirm_open = true;
      draw(app);
      break;
    }

    case AppState::ContextMenuAction::BrowseArchive: {
      pid_t pid = fork();
      if (pid == 0) {
        execlp("horizon-archive", "horizon-archive", entry.path.c_str(), nullptr);
        _exit(1);
      }
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::Extract: {
      execute_extract_async(app, entry.path, default_extract_dir(entry.path));
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::ExtractTo: {
      std::string dest;
      if (!eh::dialog::show_native_folder_picker(&dest)) { draw(app); return; }
      if (!dest.empty()) {
        execute_extract_async(app, entry.path, dest);
      }
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::Compress: {
      std::vector<std::string> paths;
      if (app.cur_tab().multi_selected.size() > 1) {
        for (int vis_idx : app.cur_tab().multi_selected) {
          if (vis_idx >= 0 && vis_idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
            int r = app.cur_tab().visible_entries[vis_idx];
            if (r >= 0 && r < static_cast<int>(app.cur_tab().entries.size()))
              paths.push_back(app.cur_tab().entries[r].path);
          }
        }
      } else {
        paths.push_back(entry.path);
      }
      app.compress_source_paths = paths;
      app.compress_source_name = entry.name;
      // Strip extension for default archive name
      {
        std::string stem = entry.name;
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        app.compress_name_buf = stem;
      }
      check_compress_tool_availability(app);
      app.compress_name_cursor = static_cast<int>(entry.name.size());
      app.compress_format = 1; // tar.gz
      app.compress_level = 6;
      app.compress_hover_format = -1;
      app.compress_hover_level = -1;
      app.compress_hover_btn = -1;
      app.compress_dialog_open = true;
      draw(app);
      return;
    }

    case AppState::ContextMenuAction::Duplicate: {
      {
        fs::path src(entry.path);
        fs::path parent = src.parent_path();
        fs::path target = parent / (src.stem().string() + " (copy)" + src.extension().string());
        int n = 2;
        std::error_code ec;
        while (fs::exists(target, ec)) {
          target = parent / (src.stem().string() + " (" + std::to_string(n) + ")" + src.extension().string());
          n++;
        }
        if (entry.is_dir)
          fs::copy(src, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        else
          fs::copy_file(src, target, fs::copy_options::copy_symlinks, ec);
        if (!ec) {
          reload_dir(app);
          app.operation_status = "Duplicated";
          app.operation_status_expires_ms = menu_expiry_3s();
        }
      }
      draw(app);
      return;
    }
    case AppState::ContextMenuAction::CreateSymlink: {
      std::string dest;
      if (!eh::dialog::show_native_folder_picker(&dest)) { draw(app); return; }
      if (!dest.empty()) {
        fs::path src = entry.path;
        fs::path link_path = fs::path(dest) / (entry.name + " (link)");
        std::error_code ec;
        if (entry.is_dir)
          fs::create_directory_symlink(src, link_path, ec);
        else
          fs::create_symlink(src, link_path, ec);
        if (!ec) {
          reload_dir(app);
          app.operation_status = "Symlink created";
          app.operation_status_expires_ms = menu_expiry_3s();
        }
      }
      draw(app);
      return;
    }
    case AppState::ContextMenuAction::CopyPath:
      app.clipboard.copy_text(entry.path);
      app.operation_status = "Path copied";
      app.operation_status_expires_ms = menu_expiry_3s();
      break;
    case AppState::ContextMenuAction::Properties:
      show_properties(app, entry.path, entry.icon_name);
      break;

    case AppState::ContextMenuAction::Settings:
      break;

    default:
      break;
  }
  draw(app);
}

// ── Open With dialog ──────────────────────────────────────────────

static std::string detect_mime_type(const std::string& file_path) {
  const std::string quoted = "'" + file_path + "'";
  const std::string cmd = "xdg-mime query filetype " + quoted + " 2>/dev/null || "
                          "file -b --mime-type " + quoted + " 2>/dev/null";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return {};
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), f)) out += buf;
  pclose(f);

  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                          out.back() == ' ' || out.back() == '\t'))
    out.pop_back();
  while (!out.empty() && (out.front() == ' ' || out.front() == '\t'))
    out.erase(out.begin());

  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

static std::vector<std::string> get_mime_associations(const std::string& mime_type) {
  auto read_cache = [&](const std::string& path) -> std::string {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return {};
    std::string result;
    char buf[1024];
    std::string prefix = mime_type + "=";
    while (fgets(buf, sizeof(buf), f)) {
      std::string line = buf;
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
      if (line.size() > prefix.size() &&
          line.compare(0, prefix.size(), prefix) == 0) {
        result = line.substr(prefix.size());
        break;
      }
    }
    fclose(f);
    return result;
  };

  auto read_apps = [&](const std::string& path) -> std::string {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return {};
    std::string result;
    char buf[1024];
    std::string prefix = mime_type + "=";
    bool inDefaults = false, inAdded = false;
    while (fgets(buf, sizeof(buf), f)) {
      std::string line = buf;
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
      if (line == "[Default Applications]") { inDefaults = true; inAdded = false; continue; }
      if (line == "[Added Associations]") { inAdded = true; inDefaults = false; continue; }
      if (line.empty() || line[0] == '#' || line[0] == '[') {
        if (!line.empty() && line[0] == '[' && line != "[Default Applications]" && line != "[Added Associations]")
          inDefaults = false, inAdded = false;
        continue;
      }
      if ((inDefaults || inAdded) && line.size() > prefix.size() &&
          line.compare(0, prefix.size(), prefix) == 0) {
        if (!result.empty()) result += ';';
        result += line.substr(prefix.size());
      }
    }
    fclose(f);
    return result;
  };

  auto parse_ids = [](const std::string& list) -> std::vector<std::string> {
    std::vector<std::string> ids;
    size_t start = 0;
    while (start < list.size()) {
      size_t semi = list.find(';', start);
      std::string id = (semi == std::string::npos)
          ? list.substr(start) : list.substr(start, semi - start);
      while (!id.empty() && id.front() == ' ') id.erase(id.begin());
      while (!id.empty() && id.back() == ' ') id.pop_back();
      if (!id.empty()) ids.push_back(id);
      if (semi == std::string::npos) break;
      start = semi + 1;
    }
    return ids;
  };

  std::vector<std::string> all;
  auto add = [&](const std::string& list) {
    auto v = parse_ids(list);
    all.insert(all.end(), v.begin(), v.end());
  };

  const char* home = getenv("HOME");
  if (home) {
    std::string ud = std::string(home) + "/.local/share/applications";
    add(read_cache(ud + "/mimeinfo.cache"));
    add(read_apps(ud + "/mimeapps.list"));
    add(read_apps(std::string(home) + "/.config/mimeapps.list"));
  }
  add(read_cache("/usr/share/applications/mimeinfo.cache"));
  add(read_apps("/usr/share/applications/mimeapps.list"));

  return all;
}

void open_with_open(AppState& app, const std::string& file_path) {
  open_with_close(app);

  const std::string mime_type = detect_mime_type(file_path);
  auto entries = eh::app_drawer::copy_desktop_entries();

  auto recommended_ids = get_mime_associations(mime_type);
  std::unordered_set<std::string> rec_set;
  for (const auto& id : recommended_ids) rec_set.insert(id);

  // Also scan desktop file MimeType fields for exact match
  if (!mime_type.empty()) {
    for (const auto& ent : entries) {
      if (ent.noDisplay || ent.hidden) continue;
      bool found = (";" + ent.mimeTypesLower + ";").find(";" + mime_type + ";") != std::string::npos;
      if (found) {
        std::string id = fs::path(ent.path).stem().string();
        rec_set.insert(id);
        rec_set.insert(id + ".desktop");
      }
    }
  }

  std::vector<AppState::OpenWithEntry> recommended, other;
  for (const auto& ent : entries) {
    if (ent.noDisplay || ent.hidden) continue;
    AppState::OpenWithEntry ae;
    ae.desktop_id = fs::path(ent.path).stem().string();
    ae.desktop_path = ent.path;
    ae.name = ent.name;
    if (rec_set.count(ae.desktop_id) || rec_set.count(ae.desktop_id + ".desktop"))
      recommended.push_back(std::move(ae));
    else
      other.push_back(std::move(ae));
  }

  app.open_with_exact_count = static_cast<int>(recommended.size());
  app.open_with_apps = std::move(recommended);
  app.open_with_apps.insert(app.open_with_apps.end(),
                             std::make_move_iterator(other.begin()),
                             std::make_move_iterator(other.end()));

  app.open_with_open = true;
  app.open_with_file_path = file_path;
  app.open_with_mime = mime_type;
  app.open_with_hover = -1;
  app.open_with_selected = -1;
  app.open_with_scroll = 0;
  app.open_with_set_default = false;
}

void open_with_close(AppState& app) {
  app.open_with_open = false;
  app.open_with_apps.clear();
  app.open_with_file_path.clear();
  app.open_with_mime.clear();
  app.open_with_hover = -1;
  app.open_with_selected = -1;
  app.open_with_scroll = 0;
  app.open_with_exact_count = 0;
  app.open_with_set_default = false;
}

// ── Settings dialog open / apply ──────────────────────────────────

void open_settings(AppState& app) {
  // File browser settings come from the live app state (already loaded
  // by reload_settings_from_config).  Terminal choice is a global setting
  // stored in the main settings.toml.
  app.settings_zoom_pct = app.zoom_pct;
  app.settings_folders_before_files = app.folders_before_files;
  app.settings_opacity_pct = app.surface_opacity_pct;
  app.settings_sidebar_opacity_pct = app.sidebar_opacity_pct;
  app.settings_topbar_opacity_pct = app.topbar_opacity_pct;
  app.settings_statusbar_opacity_pct = app.statusbar_opacity_pct;
  app.settings_preview_opacity_pct = app.preview_opacity_pct;

  // Build terminal options list
  scan_terminal_apps(app);
  app.settings_term_opts.clear();
  app.settings_term_opts.push_back("System default");
  int default_idx = 0;
  {
    const auto& sc = eh::config::shell_config_snapshot();
    for (size_t i = 0; i < app.term_chooser_apps.size(); ++i) {
      app.settings_term_opts.push_back(app.term_chooser_apps[i].name);
      std::string id = app.term_chooser_apps[i].desktop_id;
      auto dot = id.rfind('.');
      if (dot != std::string::npos) id = id.substr(0, dot);
      auto slash = id.rfind('/');
      if (slash != std::string::npos) id = id.substr(slash + 1);
      if (!sc.defaultApps.terminal.empty() &&
          (id == sc.defaultApps.terminal ||
           app.term_chooser_apps[i].desktop_id == sc.defaultApps.terminal ||
           app.term_chooser_apps[i].desktop_id == sc.defaultApps.terminal + ".desktop")) {
        default_idx = static_cast<int>(i) + 1;
      }
    }
  }
  app.settings_default_term_idx = default_idx;

  app.settings_open = true;
  app.settings_tab = 0;
  app.settings_dropdown_open = false;
  app.settings_dropdown_hover = -1;
  app.settings_dropdown_scroll = 0;
}

void save_current_folder_settings(AppState& app) {
  if (app.cur_tab().current_path.empty()) return;
  app.per_folder_settings[app.cur_tab().current_path] = {
    app.cur_tab().view_mode,
    app.cur_tab().sort_field,
    app.cur_tab().sort_descending,
    app.cur_tab().group_by_type
  };
}

void save_file_browser_settings(AppState& app) {
  save_current_folder_settings(app);

  eh::config::FileBrowserSettings fbs;
  fbs.zoom_pct = app.zoom_pct;
  fbs.folders_before_files = app.folders_before_files;
  fbs.surface_opacity_pct = app.surface_opacity_pct;
  fbs.sidebar_opacity_pct = app.sidebar_opacity_pct;
  fbs.topbar_opacity_pct = app.topbar_opacity_pct;
  fbs.statusbar_opacity_pct = app.statusbar_opacity_pct;
  fbs.preview_opacity_pct = app.preview_opacity_pct;
  fbs.view_mode = static_cast<int>(app.cur_tab().view_mode);
  fbs.sort_field = static_cast<int>(app.cur_tab().sort_field);
  fbs.sort_descending = app.cur_tab().sort_descending;
  fbs.group_by_type = app.cur_tab().group_by_type;
  fbs.show_hidden = app.show_hidden;
  for (const auto& [path, fs] : app.per_folder_settings) {
    eh::config::FileBrowserSettings::PerFolder pf;
    pf.view_mode = static_cast<int>(fs.view_mode);
    pf.sort_field = static_cast<int>(fs.sort_field);
    pf.sort_descending = fs.sort_descending;
    pf.group_by_type = fs.group_by_type;
    fbs.per_folder[path] = pf;
  }
  fbs.favorites = app.favorites;
  fbs.window_controls_left = app.window_controls_left;
  (void)eh::config::write_file_browser_toml(fbs);
  reload_settings_from_config(app);
}

void settings_apply(AppState& app) {
  save_current_folder_settings(app);

  // Save file browser settings to its own file
  {
    eh::config::FileBrowserSettings fbs;
    fbs.zoom_pct = app.settings_zoom_pct;
    fbs.folders_before_files = app.settings_folders_before_files;
    fbs.surface_opacity_pct = app.settings_opacity_pct;
    fbs.sidebar_opacity_pct = app.settings_sidebar_opacity_pct;
    fbs.topbar_opacity_pct = app.settings_topbar_opacity_pct;
    fbs.statusbar_opacity_pct = app.settings_statusbar_opacity_pct;
    fbs.preview_opacity_pct = app.settings_preview_opacity_pct;
    fbs.view_mode = static_cast<int>(app.cur_tab().view_mode);
    fbs.sort_field = static_cast<int>(app.cur_tab().sort_field);
    fbs.sort_descending = app.cur_tab().sort_descending;
    fbs.group_by_type = app.cur_tab().group_by_type;
    fbs.show_hidden = app.show_hidden;
    for (const auto& [path, fs] : app.per_folder_settings) {
      eh::config::FileBrowserSettings::PerFolder pf;
      pf.view_mode = static_cast<int>(fs.view_mode);
      pf.sort_field = static_cast<int>(fs.sort_field);
      pf.sort_descending = fs.sort_descending;
      pf.group_by_type = fs.group_by_type;
      fbs.per_folder[path] = pf;
    }
    fbs.favorites = app.favorites;
    (void)eh::config::write_file_browser_toml(fbs);
  }

  // Terminal preference is a global setting — update the main config
  {
    eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
    if (app.settings_default_term_idx > 0 &&
        app.settings_default_term_idx - 1 < static_cast<int>(app.term_chooser_apps.size())) {
      sc.defaultApps.terminal = app.term_chooser_apps[app.settings_default_term_idx - 1].desktop_id;
    } else {
      sc.defaultApps.terminal.clear();
    }
    (void)eh::config::write_state_settings_toml(sc);
    eh::config::shell_config_apply_from_memory(std::move(sc));
  }

  reload_settings_from_config(app);
}

void reload_colors_from_config(AppState& app) {
  eh::config::shell_config_reload_from_disk_now();
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  eh::config::ShellAppearance ap = sc.appearance;
  eh::matugen::refresh_wallpaper_derived_palette(ap, sc.wallpaperImage);
  const auto mc = eh::config::derived_chrome_colors(ap);
  if (ap.matugenThemingEnabled && ap.matugenPaletteOk) {
    // Use matugen colors for all UI with vibrance boost
    // Mix surface/outline/bg with accent color to avoid flat grey M3 tones
    app.bg_r = mc.panelFillR * 0.5 + mc.accentR * 0.015;
    app.bg_g = mc.panelFillG * 0.5 + mc.accentG * 0.015;
    app.bg_b = mc.panelFillB * 0.5 + mc.accentB * 0.015;
    double s_scale = 0.6;
    app.surface_r = mc.panelFillR * s_scale + mc.accentR * 0.015;
    app.surface_g = mc.panelFillG * s_scale + mc.accentG * 0.015;
    app.surface_b = mc.panelFillB * s_scale + mc.accentB * 0.015;
    app.accent_r = mc.accentR;
    app.accent_g = mc.accentG;
    app.accent_b = mc.accentB;
    double o_blend = 0.6;
    app.outline_r = mc.outlineR * o_blend + mc.accentR * (1.0 - o_blend);
    app.outline_g = mc.outlineG * o_blend + mc.accentG * (1.0 - o_blend);
    app.outline_b = mc.outlineB * o_blend + mc.accentB * (1.0 - o_blend);
    app.text_r = mc.textR;
    app.text_g = mc.textG;
    app.text_b = mc.textB;
    app.text_secondary_r = mc.textR * 0.65;
    app.text_secondary_g = mc.textG * 0.65;
    app.text_secondary_b = mc.textB * 0.65;
  } else {
    app.bg_r = 0.0;
    app.bg_g = 0.0;
    app.bg_b = 0.0;
    app.surface_r = 0.08;
    app.surface_g = 0.08;
    app.surface_b = 0.10;
    eh::config::apply_color_adjustment(app.surface_r, app.surface_g, app.surface_b,
                                       ap.colorBrightness, ap.colorContrast, ap.colorVibrance, ap.colorGamma);
    app.accent_r = mc.accentR;
    app.accent_g = mc.accentG;
    app.accent_b = mc.accentB;
    app.outline_r = 0.06;
    app.outline_g = 0.06;
    app.outline_b = 0.08;
    eh::config::apply_color_adjustment(app.outline_r, app.outline_g, app.outline_b,
                                       ap.colorBrightness, ap.colorContrast, ap.colorVibrance, ap.colorGamma);
    app.text_r = mc.textR;
    app.text_g = mc.textG;
    app.text_b = mc.textB;
    app.text_secondary_r = 0.50;
    app.text_secondary_g = 0.50;
    app.text_secondary_b = 0.55;
  }
}

void reload_settings_from_config(AppState& app) {
  reload_colors_from_config(app);
  eh::config::FileBrowserSettings fbs = eh::config::read_file_browser_toml();
  app.zoom_pct = fbs.zoom_pct;
  app.settings_zoom_pct = app.zoom_pct;
  app.folders_before_files = fbs.folders_before_files;
  app.surface_opacity_pct = fbs.surface_opacity_pct;
  app.sidebar_opacity_pct = fbs.sidebar_opacity_pct;
  app.topbar_opacity_pct = fbs.topbar_opacity_pct;
  app.statusbar_opacity_pct = fbs.statusbar_opacity_pct;
  app.preview_opacity_pct = fbs.preview_opacity_pct;
  app.cur_tab().view_mode = static_cast<ViewMode>(fbs.view_mode);
  app.cur_tab().sort_field = static_cast<SortField>(std::clamp(fbs.sort_field, 0, 3));
  app.cur_tab().sort_descending = fbs.sort_descending;
  app.cur_tab().group_by_type = fbs.group_by_type;
  app.show_hidden = fbs.show_hidden;
  app.entry_height = std::max(20, static_cast<int>(36.0 * app.zoom_pct / 100.0));
  int icon_sz = static_cast<int>(48.0 * app.zoom_pct / 100.0);
  app.grid_cell_size = std::max(40, icon_sz + static_cast<int>(8.0 * app.zoom_pct / 100.0));
  app.sidebar_width = std::max(120, static_cast<int>(app.sidebar_width_base * app.zoom_pct / 100.0));

  app.favorites = fbs.favorites;
  app.window_controls_left = fbs.window_controls_left;

  // Load per-folder settings
  app.per_folder_settings.clear();
  for (const auto& [path, pf] : fbs.per_folder) {
    app.per_folder_settings[path] = {
      static_cast<ViewMode>(pf.view_mode),
      static_cast<SortField>(pf.sort_field),
      pf.sort_descending,
      pf.group_by_type
    };
  }
}

void show_properties(AppState& app, const std::string& path, const std::string& icon_name) {
  auto& p = app.properties;
  p = AppState::PropertiesState{};
  p.open = true;
  p.path = path;
  p.icon_name = icon_name;
  p.location = fs::path(path).parent_path().string();

  struct stat st;
  if (stat(path.c_str(), &st) != 0) return;

  p.is_dir = S_ISDIR(st.st_mode);
  p.size = static_cast<uint64_t>(st.st_size);
  p.modified_sec = st.st_mtime;
  p.accessed_sec = st.st_atime;
  p.created_sec = st.st_ctime;

  p.current_mode = st.st_mode;
  // Compute combo values
  auto perm_level = [](bool r, bool w, bool x) {
    if (!r) return 0;
    if (!w) return 1;
    if (!x) return 2;
    return 3;
  };
  p.perm_owner = perm_level(st.st_mode & S_IRUSR, st.st_mode & S_IWUSR, st.st_mode & S_IXUSR);
  p.perm_group = perm_level(st.st_mode & S_IRGRP, st.st_mode & S_IWGRP, st.st_mode & S_IXGRP);
  p.perm_other = perm_level(st.st_mode & S_IROTH, st.st_mode & S_IWOTH, st.st_mode & S_IXOTH);
  p.executable = (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;

  // Owner/group names
  struct passwd* pw = getpwuid(st.st_uid);
  p.owner_name = pw ? pw->pw_name : std::to_string(st.st_uid);
  struct group* gr = getgrgid(st.st_gid);
  p.group_name = gr ? gr->gr_name : std::to_string(st.st_gid);

  p.name = fs::path(path).filename().string();
  if (p.name.empty()) p.name = path;

  // MIME type
  if (!p.is_dir) {
    std::string cmd = "xdg-mime query filetype '" + path + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (f) {
      char buf[256];
      if (fgets(buf, sizeof(buf), f)) {
        std::string mime(buf);
        while (!mime.empty() && (mime.back() == '\n' || mime.back() == '\r'))
          mime.pop_back();
        p.mime_type = mime;
      }
      pclose(f);
    }
  } else {
    p.mime_type = "inode/directory";
  }

  // Helper: shell-escape a path for single-quote quoting
  auto sq = [](const std::string& s) -> std::string {
    std::string r = "'";
    for (char c : s) {
      if (c == '\'') r += "'\\''";
      else r += c;
    }
    r += '\'';
    return r;
  };

  // Image dimensions
  static const std::vector<std::string> img_exts = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".tiff", ".tif"
  };
  std::string ext;
  auto dot = p.name.rfind('.');
  if (dot != std::string::npos) {
    ext = p.name.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  bool is_image = !ext.empty() && std::find(img_exts.begin(), img_exts.end(), ext) != img_exts.end();
  if (is_image) {
    // First pass: dimensions via identify
    std::string icmd = "identify -format '%w %h|%[colorspace]|%[bit-depth]|%A|%C|%x|%y' " + sq(path) + " 2>/dev/null";
    FILE* f = popen(icmd.c_str(), "r");
    if (f) {
      char buf[256];
      if (fgets(buf, sizeof(buf), f)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        // Parse pipe-delimited fields
        auto next_field = [&]() -> std::string {
          auto ppos = line.find('|');
          if (ppos == std::string::npos) { std::string r = line; line.clear(); return r; }
          std::string r = line.substr(0, ppos);
          line = line.substr(ppos + 1);
          return r;
        };
        std::string dims = next_field();
        {
          int w = 0, h = 0;
          if (sscanf(dims.c_str(), "%d %d", &w, &h) == 2) {
            p.image_w = (w > 0) ? w : 0;
            p.image_h = (h > 0) ? h : 0;
          }
        }
        p.image_colorspace = next_field();
        p.image_bit_depth = next_field();
        std::string alpha = next_field();
        p.image_has_alpha = !alpha.empty() && alpha != "None";
        p.image_compression = next_field();
        std::string res_x = next_field();
        std::string res_y = next_field();
        if (!res_x.empty() && !res_y.empty()) {
          try {
            double rx = std::stod(res_x);
            double ry = std::stod(res_y);
            if (rx > 0 && ry > 0) {
              char rbuf[32];
              snprintf(rbuf, sizeof(rbuf), "%.0f \u00d7 %.0f", rx, ry);
              p.image_resolution = rbuf;
              p.image_res_unit = (rx > 100) ? "DPI" : "DPCM";
            }
          } catch (...) {}
        }
      }
      pclose(f);
    }
    // Fallback: try ffprobe for image dimensions
    if (p.image_w == 0 || p.image_h == 0) {
      std::string fcmd = "ffprobe -v quiet -print_format json -show_streams " + sq(path) + " 2>/dev/null";
      FILE* f2 = popen(fcmd.c_str(), "r");
      if (f2) {
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf) - 1, f2)) > 0) { buf[n] = '\0'; out += buf; }
        pclose(f2);
        auto sfind = [&](const std::string& s, const std::string& key) -> std::string {
          auto kp = s.find("\"" + key + "\""); if (kp == std::string::npos) return {};
          auto cp = s.find(':', kp + key.size() + 2); if (cp == std::string::npos) return {};
          ++cp; while (cp < s.size() && (s[cp] == ' ' || s[cp] == '\t')) ++cp;
          if (cp >= s.size()) return {};
          if (s[cp] == '"') { ++cp; auto e = s.find('"', cp); if (e == std::string::npos) return {}; return s.substr(cp, e - cp); }
          auto e = s.find_first_of(",}\n\r", cp); if (e == std::string::npos) e = s.size();
          return s.substr(cp, e - cp);
        };
        auto sp = out.find("\"streams\"");
        if (sp != std::string::npos) {
          auto sobj = out.find('{', sp);
          if (sobj != std::string::npos) {
            auto sobj_end = out.find('}', sobj);
            if (sobj_end != std::string::npos) {
              std::string s = out.substr(sobj, sobj_end - sobj + 1);
              if (s.find("\"codec_type\"") != std::string::npos && sfind(s, "codec_type") == "video") {
                try {
                  std::string vws = sfind(s, "width"); if (!vws.empty()) p.image_w = std::stoi(vws);
                  std::string vhs = sfind(s, "height"); if (!vhs.empty()) p.image_h = std::stoi(vhs);
                } catch (...) {}
              }
            }
          }
        }
      }
    }
  }

  // Audio/Video metadata via ffprobe
  if (!is_image && !ext.empty()) {
    static const std::vector<std::string> media_exts = {
      ".mp3", ".flac", ".ogg", ".wav", ".aac", ".m4a", ".wma", ".opus", ".ac3", ".dsf", ".aiff",
      ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".webm", ".flv", ".m4v", ".ogv", ".3gp", ".mts", ".m2ts", ".ts"
    };
    if (std::find(media_exts.begin(), media_exts.end(), ext) != media_exts.end()) {
      std::string fcmd = "ffprobe -v quiet -print_format json -show_format -show_streams " + sq(path) + " 2>/dev/null";
      FILE* f = popen(fcmd.c_str(), "r");
      if (f) {
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
          buf[n] = '\0';
          out += buf;
        }
        pclose(f);

        if (!out.empty()) {
          p.is_media = true;

          auto find_val = [&](const std::string& key) -> std::string {
            auto kp = out.find("\"" + key + "\"");
            if (kp == std::string::npos) return {};
            auto cp = out.find(':', kp + key.size() + 2);
            if (cp == std::string::npos) return {};
            ++cp;
            while (cp < out.size() && (out[cp] == ' ' || out[cp] == '\t')) ++cp;
            if (cp >= out.size()) return {};
            if (out[cp] == '"') {
              ++cp;
              auto e = out.find('"', cp);
              if (e == std::string::npos) return {};
              return out.substr(cp, e - cp);
            }
            auto e = out.find_first_of(",}\n\r", cp);
            if (e == std::string::npos) e = out.size();
            return out.substr(cp, e - cp);
          };

          auto sfind = [&](const std::string& s, const std::string& key) -> std::string {
            auto kp = s.find("\"" + key + "\"");
            if (kp == std::string::npos) return {};
            auto cp = s.find(':', kp + key.size() + 2);
            if (cp == std::string::npos) return {};
            ++cp;
            while (cp < s.size() && (s[cp] == ' ' || s[cp] == '\t')) ++cp;
            if (cp >= s.size()) return {};
            if (s[cp] == '"') { ++cp; auto e = s.find('"', cp); if (e == std::string::npos) return {}; return s.substr(cp, e - cp); }
            auto e = s.find_first_of(",}\n\r", cp);
            if (e == std::string::npos) e = s.size();
            return s.substr(cp, e - cp);
          };

          p.container = find_val("format_name");
          std::string dur_str = find_val("duration");
          if (!dur_str.empty()) {
            try { p.media_duration = std::stod(dur_str); } catch (...) {}
          }

          // Parse streams array
          auto sp = out.find("\"streams\"");
          if (sp != std::string::npos) {
            size_t pos = sp;
            while (true) {
              auto sobj = out.find('{', pos);
              if (sobj == std::string::npos) break;
              auto sobj_end = out.find('}', sobj);
              if (sobj_end == std::string::npos) break;
              std::string s = out.substr(sobj, sobj_end - sobj + 1);
              pos = sobj_end + 1;
              if (s.find("\"codec_type\"") == std::string::npos) continue;

              std::string ct = sfind(s, "codec_type");
              if (ct == "video") {
                p.has_video = true;
                p.video_codec = sfind(s, "codec_name");
                try {
                  std::string vws = sfind(s, "width");
                  if (!vws.empty()) p.video_w = std::stoi(vws);
                  std::string vhs = sfind(s, "height");
                  if (!vhs.empty()) p.video_h = std::stoi(vhs);
                } catch (...) {}
                std::string fr = sfind(s, "r_frame_rate");
                if (!fr.empty()) {
                  auto sl = fr.find('/');
                  if (sl != std::string::npos) {
                    try {
                      double num = std::stod(fr.substr(0, sl));
                      double den = std::stod(fr.substr(sl + 1));
                      if (den > 0) {
                        char fpb[16];
                        snprintf(fpb, sizeof(fpb), "%.2f", num / den);
                        p.video_framerate = fpb;
                      }
                    } catch (...) {}
                  } else p.video_framerate = fr;
                }
                try {
                  std::string vbr = sfind(s, "bit_rate");
                  if (!vbr.empty()) p.video_bitrate = std::stoi(vbr);
                } catch (...) {}
              } else if (ct == "audio") {
                p.has_audio = true;
                p.audio_codec = sfind(s, "codec_name");
                try {
                  std::string sr = sfind(s, "sample_rate");
                  if (!sr.empty()) p.audio_sample_rate = std::stoi(sr);
                  std::string ch = sfind(s, "channels");
                  if (!ch.empty()) p.audio_channels = std::stoi(ch);
                  std::string abr = sfind(s, "bit_rate");
                  if (!abr.empty()) p.audio_bitrate = std::stoi(abr);
                } catch (...) {}
              }
            }
          }

          // Fallback: format-level bitrate (last "bit_rate" in JSON = format section)
          {
            auto extract_js_val = [&](size_t c) -> std::string {
              if (c >= out.size()) return {};
              if (out[c] == '"') { ++c; auto e = out.find('"', c); if (e == std::string::npos) return {}; return out.substr(c, e - c); }
              auto e = out.find_first_of(",}\n\r", c);
              if (e == std::string::npos) e = out.size();
              return out.substr(c, e - c);
            };
            auto fmt_br_pos = out.rfind("\"bit_rate\"");
            if (fmt_br_pos != std::string::npos) {
              auto cp = out.find(':', fmt_br_pos + 10);
              if (cp != std::string::npos) {
                ++cp;
                while (cp < out.size() && (out[cp] == ' ' || out[cp] == '\t')) ++cp;
                std::string fmt_br = extract_js_val(cp);
                if (!fmt_br.empty()) {
                  try {
                    int fbr = std::stoi(fmt_br);
                    if (p.video_bitrate == 0 && p.has_video) p.video_bitrate = fbr;
                    if (p.audio_bitrate == 0 && p.has_audio) p.audio_bitrate = fbr;
                  } catch (...) {}
                }
              }
            }
          }
        }
      }
    }
  }

  p.scroll_px = 0;
  p.combo_open = -1;
  p.combo_hover_item = -1;
  draw(app);
}

} // namespace eh::file_browser
