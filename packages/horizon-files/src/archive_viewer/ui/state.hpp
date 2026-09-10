#pragma once

#include "archive_viewer/core/archive.hpp"
#include "archive_viewer/fb/fb_panel.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct wl_callback;
struct wl_compositor;
struct wl_keyboard;
struct wl_pointer;
struct wl_seat;
struct wl_shm;
struct wl_surface;

namespace archive_viewer {

struct ArchiveState {
  // Wayland objects
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  void* wm_base = nullptr; // xdg_wm_base
  wl_surface* surface = nullptr;
  void* xdg_surf = nullptr; // xdg_surface
  void* toplevel = nullptr; // xdg_toplevel
  wl_callback* frame_cb = nullptr;

  // xkb
  xkb_context* xkb_ctx = nullptr;
  xkb_keymap* keymap = nullptr;
  xkb_state* xkb = nullptr;

  // Double-buffered rendering
  struct Buffer {
    wl_buffer* wl_buf = nullptr;
    cairo_surface_t* cairo_surface = nullptr;
    cairo_t* cr = nullptr;
    void* data = nullptr;
    int width = 0;
    int height = 0;
    bool busy = false;
  };
  Buffer bufs[2];
  int front = 0;
  int pending_w = 700, pending_h = 550;
  int width = 700, height = 550;
  bool running = true;
  bool needs_redraw = true;
  bool configured = false;
  uint32_t serial = 0;

  // Theme colors
  double bg_r = 0.12, bg_g = 0.12, bg_b = 0.14;
  double surface_r = 0.16, surface_g = 0.16, surface_b = 0.18;
  double text_r = 0.92, text_g = 0.92, text_b = 0.94;
  double accent_r = 0.30, accent_g = 0.55, accent_b = 0.90;
  double outline_r = 0.25, outline_g = 0.25, outline_b = 0.27;
  double hover_r = 0.22, hover_g = 0.22, hover_b = 0.24;
  double selected_r = 0.20, selected_g = 0.40, selected_b = 0.70;

  // Settings
  double bg_opacity = 1.0;
  double panel_opacity = 1.0;
  double zoom_level = 1.0;
  bool show_settings = false;
  bool hover_gear = false;
  bool hover_settings_close = false;
  int hover_slider = -1; // 0=bg_opacity, 1=panel_opacity, 2=zoom, 3=fb_bg_opacity, 4=fb_panel_opacity
  int drag_slider = -1;

  // Archive data
  std::string archive_path;
  std::vector<ArchiveEntry> all_entries;
  bool entries_loaded = false;
  bool scan_error = false;
  std::string scan_error_msg;

  // View
  std::string current_dir;
  struct ViewEntry {
    std::string name;
    std::string vpath;
    uint64_t size = 0;
    int64_t mtime = 0;
    bool is_dir = false;
    bool selected = false;
    bool has_selected_descendants = false;
  };
  std::vector<ViewEntry> visible_entries;
  int scroll_px = 0;
  int max_scroll = 0;
  static constexpr int row_h = 34;

  // Extraction state
  bool extracting = false;
  std::atomic<float> progress{0.0f};
  std::string status_text;
  std::string last_extract_dest;
  bool show_open_dest = false;
  bool hover_open_dest = false;
  int open_dest_btn_x = 0, open_dest_btn_y = 0;
  int open_dest_btn_w = 0, open_dest_btn_h = 0;

  // ── Create mode ──
  bool create_mode = false;
  bool creating = false;
  std::vector<std::string> create_files;
  std::string create_dest_dir;
  std::string create_name;
  int create_format = 0;           // index into kFormatLabels
  bool create_format_open = false;
  int create_format_hover = -1;
  bool create_editing = false;     // keyboard focus on name field
  int create_cursor = 0;

  // ── Embedded file browser panel ──
  FbPanel fb_panel;
  enum class FbAction {
    None,
    CreateBrowse,    // browse destination for create archive dialog
    ExtractAll,      // pick destination for extract all
    ExtractSel,      // pick destination for extract selected
    OpenFile,        // pick archive file to open
  };
  FbAction fb_pending_action = FbAction::None;
  std::vector<std::string> fb_pending_sel;

  // Hover state
  int hover_entry = -1;
  int hover_btn = -1; // 0=Up, 1=ExtractAll, 2=ExtractSel, 3=Close, 4=Open
  bool hover_close = false;

  // Button rects (set during draw, used in events)
  int header_h = 40;
  int info_h = 28;
  int breadcrumb_h = 32;
  int footer_h = 72;
  int btn_x[5], btn_y[5], btn_w[5], btn_h[5];

  // Settings panel slider rects (set during draw, used in events)
  int settings_x, settings_y, settings_w, settings_h;
  int slider_track_x[5], slider_track_y[5], slider_track_w, slider_track_h;

  // Icon surfaces (loaded from embedded SVGs)
  cairo_surface_t* icon_close = nullptr;
  cairo_surface_t* icon_settings = nullptr;

  void rebuild_visible();
  void open_archive(const std::string& path);
};

// Launch the Wayland-based archive viewer GUI.
// Returns 0 on success, non-zero on error.
// When create_files is non-empty, launches in create-archive mode.
int run_gui(const std::string& archive_path,
            const std::vector<std::string>& create_files = {});

} // namespace archive_viewer
