#pragma once

#include <cairo/cairo.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/file_browser/features/progress.hpp"

#include "wayland/buffer/shm_buffer.hpp"
#include "wayland/buffer/cairo_cpu_buffer.hpp"
#include "wayland/core/connection.hpp"
#include "wayland/core/seat.hpp"
#include "wayland/input/clipboard.hpp"
#include "platform/common/icon_cache/icon_cache.hpp"
#include "theme/controls/containers/button.hpp"
#include "theme/controls/input/toggle.hpp"
#include "theme/controls/navigation/top_app_bar.hpp"
#include "theme/controls/navigation/nav_rail.hpp"

struct wl_callback;

namespace eh::file_browser {

enum class ViewMode {
  List,
  Grid,
  Computer,
  Tree,
  Compact,
};

enum class SortField {
  Name,
  Size,
  Modified,
  Type,
};

enum class FileType {
  Folder,
  Image,
  Audio,
  Video,
  Text,
  Markdown,
  Code,
  Document,
  Font,
  Archive,
  Executable,
  Web,
  File,
};

struct FileEntry {
  std::string name;
  std::string path;
  std::string mime_type;
  std::string icon_name;
  FileType type = FileType::File;
  uint64_t size = 0;
  int64_t modified_sec = 0;
  bool is_dir = false;
  bool is_hidden = false;
  bool is_symlink = false;
};

struct SidebarLocation {
  enum class Kind {
    Home,
    Desktop,
    Documents,
    Downloads,
    Pictures,
    Music,
    Videos,
    Trash,
    Favorite,
    Drive,
    Root,
    Computer,
    Other,
  };

  Kind kind = Kind::Other;
  std::string label;
  std::string path;
  std::string icon_name;
  bool is_mounted = true;
  // For drives
  std::string drive_id;
  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;
};

// ── Thumbnail pending entry ──
struct ThumbPending {
  int visible_idx;
  int size;
};

// ── Computer view item (Deepin "My Computer" style) ──
struct ComputerItem {
  enum class ShapeType { Splitter, Small, Large };
  enum class Group { UserDirs, Disks, Removable, Network };

  ShapeType shape = ShapeType::Large;
  Group group = Group::Disks;
  std::string label;
  std::string icon_name;
  std::string path;
  std::string drive_id;
  std::string filesystem;
  uint64_t total_bytes = 0;
  uint64_t used_bytes = 0;
  bool is_mounted = true;
  bool show_progress = false;
  bool is_user_label = false;
};

// ── Tree view entry ──
struct TreeEntry {
  std::string name;
  std::string path;
  bool is_dir = false;
  int depth = 0;
  bool has_children = false;
  bool is_expanded = false;
};

// ── Per-folder view mode settings ──
struct FolderSettings {
  ViewMode view_mode = ViewMode::List;
  SortField sort_field = SortField::Name;
  bool sort_descending = false;
  bool group_by_type = false;
};

// ── Breadcrumb segment ──
struct BreadcrumbSegment {
  std::string label;
  std::string path;
  int x = 0;
  int w = 0;
};

// ── Tab ──
struct Tab {
  std::string current_path;
  std::vector<std::string> nav_history;   // back stack
  std::vector<std::string> nav_forward;   // forward stack
  std::vector<FileEntry> entries;
  std::vector<int> visible_entries;       // indices after filter

  ViewMode view_mode = ViewMode::List;
  SortField sort_field = SortField::Name;
  bool sort_descending = false;
  bool group_by_type = false;

  // Scroll
  int scroll_px = 0;
  double scroll_smooth_current = 0.0;
  double scroll_smooth_target = 0.0;
  int content_h = 0;

  // Selection
  int hover_idx = -1;
  int selected_idx = -1;
  int sel_anchor = -1;                      // anchor for Shift-click range
  std::vector<int> multi_selected;        // indices into visible_entries

  // Directory auto-refresh
  int64_t dir_mtime = 0;

  // Tree view
  std::vector<TreeEntry> tree_entries;
  std::unordered_set<std::string> tree_expanded;
};

struct AppState {
  AppState();
  ~AppState();

  eh::wayland::WaylandConnection wl{};
  eh::wayland::WaylandSeat seat{};
  eh::wayland::ClipboardService clipboard{};

  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;

  int width = 960;
  int height = 680;
  bool running = true;
  bool pendingRedraw = false;

  double pointerX = 0;
  double pointerY = 0;
  bool pointerLeftDown = false;

  // ── Double-click tracking ──
  uint64_t last_click_ns = 0;
  int last_click_x = -1;
  int last_click_y = -1;
  int last_click_idx = -1;

  wl_shm* shm = nullptr;
  eh::icons::IconCache icons{};
  std::array<eh::wayland::ShmBuffer, 2> buf{};

  bool embedded = false;

  // ── Picker modes ──
  bool select_dir_mode = false;
  bool select_file_mode = false;
  std::string select_dir_result;
  int select_bar_h = 48;
  int select_bar_y = 0;
  int select_btn_x = 0, select_btn_w = 0;
  int cancel_btn_x = 0, cancel_btn_w = 0;
  bool select_btn_hover = false;
  bool cancel_btn_hover = false;

  // ── Tabs ──
  std::vector<Tab> tabs;
  int active_tab = 0;
  Tab& cur_tab() {
    return split_view && active_pane == 1 ? right_pane : tabs[active_tab];
  }
  const Tab& cur_tab() const {
    return split_view && active_pane == 1 ? right_pane : tabs[active_tab];
  }

  // ── Split pane ──
  bool split_view = false;
  int active_pane = 0;          // 0=left, 1=right
  int split_divider_x = 0;      // pixel position of divider (from content left edge)
  bool split_divider_hover = false;
  bool split_divider_dragging = false;
  Tab right_pane;

  bool show_hidden = false;

  // ── Selection ──
  bool rubber_banding = false;
  double rubber_x0 = 0, rubber_y0 = 0;

  // ── Sidebar ──
  double sidebar_scale = 1.0;          // < 1.0 when items don't fit vertically
  std::vector<SidebarLocation> sidebar_locations;
  int sidebar_hover_idx = -1;
  int sidebar_mount_hover_idx = -1;    // sidebar item index whose mount indicator is hovered
  int sidebar_scroll_px = 0;
  bool sidebar_expanded = true;
  int sidebar_width = 0;          // computed: sidebar_width_base * zoom_pct / 100
  int sidebar_width_base = 288;   // user's preferred width at 100% zoom
  bool sidebar_dragging = false;
  bool sidebar_hover_resize = false;
  int sidebar_drag_start_x = 0;
  int sidebar_drag_start_width = 0;

  // ── Favorites (persistent bookmark folders) ──
  std::vector<std::string> favorites;

  // ── Search ──
  bool search_active = false;          // local folder inline filter
  bool r_search_active = false;
  std::string search_query;
  std::string r_search_query;
  bool recursive_search_active = false; // home directory recursive search
  bool r_recursive_search_active = false;
  std::string recursive_search_query;
  std::string r_recursive_search_query;
  int search_cursor = 0;
  int r_search_cursor = 0;
  int search_sel_start = -1;
  int r_search_sel_start = -1;
  int search_sel_end = -1;
  int r_search_sel_end = -1;
  int search_bar_x = 0, search_bar_w = 0;
  int r_search_bar_x = 0, r_search_bar_w = 0;
  int search_clear_x = 0, search_clear_w = 0;
  int r_search_clear_x = 0, r_search_clear_w = 0;
  int search_btn_x = 0, search_btn_w = 0;
  int r_search_btn_x = 0, r_search_btn_w = 0;
  bool search_btn_hover = false;
  bool r_search_btn_hover = false;
  int folder_search_btn_x = 0, folder_search_btn_w = 0;
  int r_folder_search_btn_x = 0, r_folder_search_btn_w = 0;
  bool folder_search_btn_hover = false;
  bool r_folder_search_btn_hover = false;

  // ── Search filters ──
  int filter_type_idx = 0;   // 0=All, 1=Folder, 2=Image, 3=Audio, 4=Video, 5=Text, 6=Document, 7=Archive, 8=Code, 9=Executable, 10=Web, 11=Font, 12=Markdown
  int r_filter_type_idx = 0;
  int filter_size_idx = 0;   // 0=Any, 1=<10K, 2=10-100K, 3=100K-1M, 4=1-10M, 5=10-100M, 6=>100M
  int r_filter_size_idx = 0;
  int filter_date_idx = 0;   // 0=Any, 1=Today, 2=This week, 3=This month, 4=This year
  int r_filter_date_idx = 0;
  int filter_btn_x = 0, filter_btn_w = 0;
  int r_filter_btn_x = 0, r_filter_btn_w = 0;
  bool filter_btn_hover = false;
  bool r_filter_btn_hover = false;
  int filter_dropdown_section = 0;  // 0=none, 1=type expanded, 2=size expanded, 3=date expanded
  int r_filter_dropdown_section = 0;
  int filter_dropdown_hover = -1;
  int r_filter_dropdown_hover = -1;
  int filter_dropdown_x = 0, filter_dropdown_y = 0;
  int filter_dropdown_w = 0, filter_dropdown_h = 0;
  int r_filter_dropdown_x = 0, r_filter_dropdown_y = 0;
  int r_filter_dropdown_w = 0, r_filter_dropdown_h = 0;

  // ── Path bar ──
  std::string path_edit_buf;
  std::string r_path_edit_buf;
  bool path_editing = false;
  bool r_path_editing = false;

  // ── Operations ──
  bool operation_in_progress = false;
  std::string operation_status;
  std::uint64_t operation_status_expires_ms = 0;
  std::shared_ptr<OperationProgress> op_progress;

  // ── Sidebar progress panel ──
  int progress_panel_h = 0;
  int progress_cancel_x = 0;
  int progress_cancel_y = 0;
  int progress_cancel_w = 0;
  int progress_cancel_h = 0;

  // ── Context menu ──
  enum class ContextMenuAction {
    Open,
    OpenWith,
    OpenInTerminal,
    Cut,
    Copy,
    CopyTo,
    MoveTo,
    Paste,
    Rename,
    MoveToTrash,
    PermanentDelete,
    NewFolder,
    NewDocument,
    Reload,
    CopyLocation,
    SelectAll,
    Properties,
    AddToFavorites,
    RemoveFromFavorites,
    MountDrive,
    UnmountDrive,
    Duplicate,
    CreateSymlink,
    CopyPath,
    Compress,
    BrowseArchive,
    Extract,
    ExtractTo,
    Settings,
    OpenInNewTab,
    EmptyTrash,
    OpenFileLocation,
    CloseTab,
    CloseOtherTabs,
    CloseAllTabs,
    DuplicateTab,
    Separator,
  };
  struct ContextMenuItem {
    ContextMenuAction action;
    std::string label;
    std::vector<ContextMenuItem> sub_items; // non-empty = submenu header
    bool submenu_open = false;             // whether submenu is currently shown
  };
  static ContextMenuItem menu_item(ContextMenuAction a, const std::string& l) {
    return {a, l, {}, false};
  }
  static ContextMenuItem menu_separator() {
    return {ContextMenuAction::Separator, "", {}, false};
  }
  bool context_menu_open = false;
  int context_menu_x = 0;
  int context_menu_y = 0;
  int context_menu_hover = -1;
  int context_menu_hover_prev = -1; // previous hover index (for submenu tracking)
  int context_menu_sub_hover = -1;  // submenu item hover index (-1 = none)
  int context_menu_file_idx = -1; // -1 = background, -2 = sidebar, -3 = path editing, -4 = tab, -5 = dots menu
  int context_menu_sidebar_idx = -1;
  int context_menu_tab_idx = -1;
  std::vector<ContextMenuItem> context_menu_items;

  // ── Path bar dots menu button hit target ──
  int dots_btn_x = 0, dots_btn_y = 0, dots_btn_w = 0, dots_btn_h = 0;
  int r_dots_btn_x = 0, r_dots_btn_y = 0, r_dots_btn_w = 0, r_dots_btn_h = 0;
  bool dots_btn_hover = false;
  bool r_dots_btn_hover = false;

  // ── Sidebar context menu actions ──
  enum class SidebarMenuAction {
    RemoveFavorite,
  };

  // ── New folder/file dialog ──
  bool create_dialog_open = false;
  bool create_is_folder = true;
  std::string create_buf;
  int create_cursor_pos = 0;

  // ── Rename UI dialog ──
  bool rename_ui_open = false;
  std::string rename_ui_old_name;
  std::string rename_ui_buf;
  int rename_ui_cursor_pos = 0;
  std::string rename_ui_entry_path;

  // ── Key repeat (application-level, for consistent repeat across compositors) ──
  uint32_t key_repeat_sym = 0;   // 0 = none
  uint64_t key_repeat_start_ms = 0; // steady_clock epoch ms of initial press
  uint64_t key_repeat_last_ms = 0;  // steady_clock epoch ms of last repeat fire

  // ── Batch rename dialog (Nautilus-style) ──
  struct BatchRenameEntry {
    std::string old_path;
    std::string old_name;
    std::string ext;       // file extension (incl. dot), empty if none
    std::string new_name;
  };
  bool batch_rename_open = false;
  std::vector<BatchRenameEntry> batch_rename_entries;
  int batch_rename_mode = 0;      // 0=template, 1=find_replace
  // Template mode
  std::string batch_rename_template;
  int batch_rename_template_cursor = 0;
  bool batch_rename_show_add = false;
  int batch_rename_add_hover = -1;
  // Find & Replace mode
  std::string batch_rename_find;
  int batch_rename_find_cursor = 0;
  std::string batch_rename_replace;
  int batch_rename_replace_cursor = 0;
  // Shared
  int batch_rename_edit_focus = 0; // 0=main (template/find), 1=replace (find mode)
  int batch_rename_hover_btn = -1; // 0=Rename, 1=Cancel, -1=none
  int batch_rename_hover_mode = -1;// 0=template tab, 1=find_replace tab

  // ── Terminal chooser ──
  struct TerminalApp {
    std::string desktop_id;
    std::string name;
    std::string exec;
  };
  bool term_chooser_open = false;
  int term_chooser_x = 0;
  int term_chooser_y = 0;
  int term_chooser_w = 360;
  int term_chooser_h = 0;
  int term_chooser_hover = -1;
  int term_chooser_scroll = 0;
  std::string term_chooser_target_dir;
  std::vector<TerminalApp> term_chooser_apps;

  // ── Confirm dialog ──
  bool confirm_open = false;
  std::string confirm_title;
  std::string confirm_message;
  std::function<void(bool)> confirm_callback;
  std::string confirm_preview_path;  // path to show file-icon / image-preview for
  int confirm_item_count = 1;        // number of items being operated on
  int confirm_hover_btn = -1;        // 0=Cancel, 1=Delete, -1=none

  // ── Compress dialog ──
  bool compress_dialog_open = false;
  int compress_format = 1;          // 0=zip, 1=tar.gz, 2=tar.bz2, 3=tar.xz, 4=7z, 5=rar, 6=tar
  int compress_level = 6;           // 0-9
  std::string compress_name_buf;
  int compress_name_cursor = 0;
  std::vector<std::string> compress_source_paths;
  std::string compress_source_name; // base name for default archive name
  bool compress_format_available[7]{};
  int compress_hover_format = -1;
  int compress_hover_level = -1;
  int compress_hover_btn = -1;      // 0=Cancel, 1=Compress

  std::string last_icon_theme;

  // ── Thumbnail cache ──
  std::unordered_map<std::string, cairo_surface_t*> thumb_cache;
  std::list<std::string> thumb_lru;
  std::size_t thumb_cache_bytes = 0;
  static constexpr std::size_t kThumbCacheMaxBytes = 64 * 1024 * 1024; // 64 MB

  // ── Desktop file icon cache ──
  std::string last_reload_path;
  std::unordered_map<std::string, std::string> desktop_icon_cache;

  // ── Lazy thumbnail loading ──
  std::vector<ThumbPending> thumb_pending_queue;
  int thumb_decodes_this_frame = 0;
  static constexpr int kThumbDecodesPerFrame = 16;
  static constexpr int kThumbDecodesPerLoop = 200;

  // ── Preview (Phase 8) ──
  enum class PreviewMode { None, Hover, Space };
  PreviewMode preview_mode = PreviewMode::None;
  int preview_entry_idx = -1;        // visible_entries index being previewed (-1 = none)
  std::string preview_path;
  uint64_t preview_hover_start_ns = 0;
  bool preview_active = false;
  cairo_surface_t* preview_thumb = nullptr;
  std::string preview_text;        // text content for text file previews
  int preview_x = 0, preview_y = 0;
  int preview_w = 0, preview_h = 0;

  // ── Preview popup surface (wl_subsurface) ──
  wl_surface* previewPopupSurface = nullptr;
  wl_subsurface* previewPopupSub = nullptr;
  eh::wayland::ShmBuffer previewPopupBuf{};

  // ── Info panel (F11) (Phase 8) ──
  bool info_panel_open = false;
  int info_panel_tab = 0;       // 0=Preview, 1=Properties, 2=Terminal
  int info_panel_width = 0;     // computed from zoom_pct
  std::string info_panel_path;
  std::string info_panel_name;
  uint64_t info_panel_size = 0;
  int64_t info_panel_modified_sec = 0;
  bool info_panel_is_dir = false;
  bool info_panel_needs_update = true;
  std::string info_panel_mime_type;
  std::string info_panel_owner;
  std::string info_panel_group;
  mode_t info_panel_mode = 0;
  // Hit rects (set during draw, queried by click handler)
  double info_panel_hit_tabs[3][4]{};

  // ── Background pre-cache: all image thumbnails in current folder ──
  std::vector<std::string> precache_paths;   // image file paths to pre-cache
  size_t precache_idx = 0;                   // next index to process; SIZE_MAX = done
  static constexpr int kPrecacheBatchSize = 48;

  // ── Open With dialog ──
  struct OpenWithEntry {
    std::string desktop_id;
    std::string desktop_path;
    std::string name;
  };
  bool open_with_open = false;
  std::string open_with_file_path;
  std::string open_with_mime;
  std::vector<OpenWithEntry> open_with_apps;
  int open_with_hover = -1;
  int open_with_selected = -1;
  int open_with_scroll = 0;
  int open_with_exact_count = 0;
  bool open_with_set_default = false;
  // Layout / hit rects (set during draw)
  double open_with_x = 0, open_with_y = 0;
  double open_with_w = 0, open_with_h = 0;
  double open_with_hit_close[4]{};
  double open_with_hit_cancel[4]{};
  double open_with_hit_open[4]{};
  double open_with_hit_default[4]{};

  // ── Settings dialog ──
  bool settings_open = false;
  int settings_tab = 0;
  double settings_zoom_pct = 100.0;
  bool settings_folders_before_files = true;
  int settings_opacity_pct = 0;
  int settings_sidebar_opacity_pct = 100;
  int settings_topbar_opacity_pct = 100;
  int settings_statusbar_opacity_pct = 100;
  int settings_preview_opacity_pct = 100;
  int settings_default_term_idx = -1;
  std::vector<std::string> settings_term_opts;
  double settings_x = 0, settings_y = 0;
  double settings_w = 0, settings_h = 0;
  double settings_tab_hit[2][4]{};
  double settings_hit_ok[4]{};
  double settings_hit_apply[4]{};
  double settings_hit_cancel[4]{};
  double settings_hit_zoom_down[4]{};
  double settings_hit_zoom_up[4]{};
  bool settings_zoom_editing = false;
  std::string settings_zoom_buf;
  double settings_hit_folders_toggle[4]{};
  double settings_hit_opacity_slider[4]{};
  double settings_hit_sidebar_opacity_slider[4]{};
  double settings_hit_topbar_opacity_slider[4]{};
  double settings_hit_statusbar_opacity_slider[4]{};
  double settings_hit_preview_opacity_slider[4]{};
  double settings_hit_term_dropdown[4]{};
  bool settings_dropdown_open = false;
  int settings_dropdown_hover = -1;
  int settings_dropdown_scroll = 0;

  // ── Properties dialog ──
  struct PropertiesState {
    bool open = false;
    std::string path;
    std::string name;
    std::string mime_type;
    std::string icon_name;
    uint64_t size = 0;
    std::string location;
    int64_t modified_sec = 0;
    int64_t accessed_sec = 0;
    int64_t created_sec = 0;
    bool is_dir = false;
    int image_w = 0;
    int image_h = 0;
    std::string image_colorspace;
    std::string image_bit_depth;
    bool image_has_alpha = false;
    std::string image_compression;
    std::string image_resolution;
    std::string image_res_unit;
    // Media metadata
    bool is_media = false;
    bool has_video = false;
    bool has_audio = false;
    double media_duration = 0.0;      // seconds
    std::string container;             // mkv, mp4, mp3, etc.
    std::string video_codec;
    int video_w = 0;
    int video_h = 0;
    std::string video_framerate;
    int video_bitrate = 0;             // bps
    std::string audio_codec;
    int audio_sample_rate = 0;
    int audio_channels = 0;
    int audio_bitrate = 0;             // bps
    std::string owner_name;
    std::string group_name;
    mode_t current_mode = 0;
    // Permission combo values: 0=None, 1=Read, 2=Read+Write, 3=Read+Write+Exec
    int perm_owner = 0;
    int perm_group = 0;
    int perm_other = 0;
    bool executable = false; // derived execute bit (any of user/group/other exec)
    // Tabs: 0=Basic, 1=Permissions, 2=Image (only if applicable)
    int tab = 0;
    // Combo dropdown
    int combo_open = -1;     // -1=closed, 0-2 = which perm combo open
    int combo_hover_item = -1;
    int scroll_px = 0;
    int content_h = 0;
    double x = 0, y = 0, w = 0, h = 0;
    double hit_close[4]{};
    double hit_close_btn[4]{};     // bottom "Close" button
    double hit_tabs[4][4]{};       // up to 4 tab hit rects
    double hit_combo[3][4]{};      // 3 combo boxes: owner, group, other
    double hit_combo_items[3][4][4]{}; // up to 4 items per combo
    double hit_exec_toggle[4]{};   // executable toggle switch
  };
  PropertiesState properties;

  // ── Sidebar favorite reorder drag ──
  bool sidebar_fav_dragging = false;      // actively dragging a favorite to reorder
  int sidebar_fav_drag_from = -1;         // index in app.favorites being dragged
  int sidebar_fav_drag_to = -1;           // insertion index in app.favorites, -1 = no target
  int sidebar_fav_drag_to_visual = -1;    // visual insertion slot (unadjusted by drag_from), for drawing
  int sidebar_fav_drag_start_y = 0;      // pointer Y when drag started
  int sidebar_fav_drag_current_y = 0;    // pointer Y during drag (for ghost)

  // ── UDisks2 mount/unmount result (set from bg thread, consumed in event loop) ──
  std::mutex mount_mtx;
  std::string mount_pending_drive_id; // non-empty = mount in progress
  bool mount_success = false;
  std::string mount_result_drive_id;
  std::string unmount_pending_drive_id; // non-empty = unmount in progress
  bool unmount_success = false;
  std::string unmount_result_drive_id;
  std::atomic<bool> mount_poll_wake{false};

  // Deferred sidebar refresh (avoids synchronous D-Bus calls in event loop hot path)
  bool sidebar_needs_refresh = false;
  std::string mount_navigate_drive_id; // drive_id to navigate to after mount

  // ── Computer view state ──
  std::vector<ComputerItem> computer_items;
  int computer_hover_idx = -1;
  int computer_scroll_px = 0;
  double computer_scroll_smooth_current = 0.0;
  double computer_scroll_smooth_target = 0.0;
  int computer_content_h = 0;
  bool computer_needs_refresh = true;

  // ── Per-folder view mode memory ──
  std::unordered_map<std::string, FolderSettings> per_folder_settings;

  // ── List view column widths (proportional, stored as fractions of total) ──
  double col_name_frac = 0.45;
  double col_size_frac = 0.20;
  double col_date_frac = 0.25;
  double col_type_frac = 0.10;
  int col_resizing = -1;       // 0=name, 1=size, 2=date, 3=type, -1=none
  int col_resize_start_x = 0;
  double col_resize_start_frac = 0;
  bool col_hover_divider = false;

  // ── List view column visibility ──
  bool col_show_name = true;
  bool col_show_size = true;
  bool col_show_date = true;
  bool col_show_type = false;  // hidden by default
  bool view_mode_btn_hover = false;
  bool r_view_mode_btn_hover = false;
  bool settings_btn_hover = false;
  bool r_settings_btn_hover = false;

  // ── Sort menu (dropdown from top-bar sort button) ──
  bool sort_menu_open = false;
  bool r_sort_menu_open = false;
  int sort_menu_x = 0, sort_menu_y = 0;
  int sort_menu_w = 0, sort_menu_h = 0;
  int r_sort_menu_x = 0, r_sort_menu_y = 0;
  int r_sort_menu_w = 0, r_sort_menu_h = 0;
  int sort_menu_hover = -1;
  int r_sort_menu_hover = -1;
  bool sort_btn_hover = false;
  bool r_sort_btn_hover = false;
  int sort_btn_x = 0, sort_btn_w = 0;   // stored during draw for hit-testing
  int r_sort_btn_x = 0, r_sort_btn_w = 0;
  int view_btn_x = 0, view_btn_w = 0;   // stored during draw for hit-testing
  int r_view_btn_x = 0, r_view_btn_w = 0;

  // ── Window control buttons (minimize / maximize / close) ──
  bool window_controls_left = false;
  int win_btn_x = 0, win_btn_w = 0;     // stored during draw for hit-testing
  bool win_btn_min_hover = false;
  bool win_btn_max_hover = false;
  bool win_btn_close_hover = false;

  // ── Marquee / rubber-band selection ──
  bool marquee_active = false;
  double marquee_x0 = 0, marquee_y0 = 0;
  double marquee_x1 = 0, marquee_y1 = 0;

  // ── Layout tracking ──
  int top_bar_height = 56;
  int status_bar_height = 44;
  int entry_height = 36;
  int grid_cell_size = 100;
  int grid_cell_gap = 8;
  int grid_cols = 1;           // cached column count for grid view
  int grid_row_h = 0;          // cached row height for grid view (set during draw)

  // ── Top-bar arrow button hover ──
  int arrow_back_x = 0;
  int arrow_forward_x = 0;
  int r_arrow_back_x = 0;
  int r_arrow_forward_x = 0;
  bool arrow_back_hover = false;
  bool r_arrow_back_hover = false;
  bool arrow_forward_hover = false;
  bool r_arrow_forward_hover = false;

  // ── Breadcrumb nav bar ──
  std::vector<BreadcrumbSegment> breadcrumbs;
  std::vector<BreadcrumbSegment> r_breadcrumbs;
  int breadcrumb_hover = -1;
  int r_breadcrumb_hover = -1;

  // ── Tab bar ──
  int tab_bar_height = 44;
  struct TabHit {
    int x = 0, w = 0;
    int close_x = 0;  // x-position of close button
  };
  std::vector<TabHit> tab_hits;

  // ── Tab reorder drag ──
  bool tab_dragging = false;
  int tab_drag_from = -1;
  int tab_drag_to = -1;
  int tab_drag_to_visual = -1;
  int tab_drag_start_x = 0;
  int tab_drag_current_x = 0;

  // ── Path editing ──
  int path_edit_cursor = 0;
  int r_path_edit_cursor = 0;
  int path_edit_sel_start = -1;
  int r_path_edit_sel_start = -1;
  int path_edit_sel_end = -1;
  int r_path_edit_sel_end = -1;
  bool path_edit_dragging = false;
  bool r_path_edit_dragging = false;

  // ── Live config values ──
  double zoom_pct = 100.0;
  bool folders_before_files = true;
  int surface_opacity_pct = 100;
  int sidebar_opacity_pct = 100;
  int topbar_opacity_pct = 100;
  int statusbar_opacity_pct = 100;
  int preview_opacity_pct = 100;

  // ── Animation ──
  uint64_t scroll_anim_start_ns = 0;
  bool scroll_needs_redraw = false;

  // ── Frame callback ──
  wl_callback* frame_cb = nullptr;
  int last_paint_w = -1;
  int last_paint_h = -1;

  // ── Arrow icon surfaces (loaded from assets/UI/ SVGs) ──
  cairo_surface_t* arrow_left_svg = nullptr;
  cairo_surface_t* arrow_right_svg = nullptr;
  cairo_surface_t* arrow_up_svg = nullptr;
  cairo_surface_t* search_svg = nullptr;
  cairo_surface_t* folder_search_svg = nullptr;
  cairo_surface_t* mounted_svg = nullptr;
  cairo_surface_t* icon_desktop_svg = nullptr;
  cairo_surface_t* icon_documents_svg = nullptr;
  cairo_surface_t* icon_downloads_svg = nullptr;
  cairo_surface_t* icon_music_svg = nullptr;
  cairo_surface_t* icon_pictures_svg = nullptr;
  cairo_surface_t* icon_videos_svg = nullptr;
  cairo_surface_t* icon_publicshare_svg = nullptr;
  cairo_surface_t* icon_templates_svg = nullptr;

  // ── Color state ──
  double bg_r = 0.0, bg_g = 0.0, bg_b = 0.0;
  double surface_r = 0.08, surface_g = 0.08, surface_b = 0.10;
  double accent_r = 0.30, accent_g = 0.58, accent_b = 0.90;
  double text_r = 0.94, text_g = 0.94, text_b = 0.96;
  double text_secondary_r = 0.50, text_secondary_g = 0.50, text_secondary_b = 0.55;
  double outline_r = 0.06, outline_g = 0.06, outline_b = 0.08;

  // ── Drag and drop ──
  bool drag_potential = false;
  int drag_potential_idx = -1;
  double drag_start_x = 0;
  double drag_start_y = 0;
  uint32_t drag_button_serial = 0;
  std::vector<std::string> drag_paths;
  wl_data_source* drag_source = nullptr;
  wl_data_device* data_device = nullptr;
  wl_surface* drag_icon_surface = nullptr;
  bool drag_icon_attached = false;
  wl_data_offer* drop_offer = nullptr;

  // ── Drop target state (enhanced within-app DnD) ──
  int drop_x = 0;
  int drop_y = 0;
  std::string drop_target_path;
  int drop_target_idx = -1;           // visible_entries index being hovered, or -1
  bool drop_target_is_sidebar = false;
  int drop_target_sidebar_idx = -1;   // sidebar_locations index, or -1
  bool drop_target_fav_section = false; // true = drag over Favorites section (add new favorite)
  bool drop_target_is_valid = false;  // whether target accepts drops (directory)

  // ── Undo support ──
  struct UndoRecord {
    enum class Type : uint8_t {
      PasteCopy,   // undo: delete paths_b (newly created copies)
      PasteCut,    // undo: rename paths_b → paths_a (move back from dest to orig)
      Rename,      // undo: rename paths_b → paths_a (one pair)
      NewFolder,   // undo: remove paths_b (created dirs)
      NewFile,     // undo: remove paths_b (created files)
      MoveToTrash, // undo: rename paths_b (trash path) → paths_a (original path)
    };
    Type type;
    std::vector<std::string> paths_a;  // original paths before op
    std::vector<std::string> paths_b;  // new/created paths after op
  };
  std::vector<UndoRecord> undo_stack;
  std::vector<UndoRecord> redo_stack;
  static constexpr std::size_t kMaxUndo = 100;
};

} // namespace eh::file_browser
