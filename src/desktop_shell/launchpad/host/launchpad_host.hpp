#pragma once

#include "desktop_shell/launchpad/layer/launchpad_layer.hpp"
#include "desktop_shell/launchpad/search/launchpad_search.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "wl/core/connection.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <xkbcommon/xkbcommon.h>

struct DockApp;
struct wl_array;
struct wl_callback;
struct wl_keyboard;
struct wl_pointer;
struct wl_seat;
struct wp_viewport;
struct wl_surface;
struct zwlr_layer_surface_v1;

namespace eh::shell::launchpad {

void launchpad_host_page_frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms);

// Paint-only dock-button rendering (launchpad + start-menu surfaces now live in
// the split-out horizon-dock child, so the supervisor's taskbar cannot reach
// the launchpad Host). Pure paint: loads Dark/Light_Launchpad.png lazily and
// draws the icon + hover highlight. Both processes share this one painter.
void paint_launchpad_dock_button(cairo_t* cr, double x, double y, double size, bool hovered);

class Host {
  friend void launchpad_host_page_frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms);

public:
  explicit Host(DockApp& dock, std::unique_ptr<eh::wayland::WaylandConnection> wl);
  ~Host();

  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
  [[nodiscard]] wl_surface* backdrop_surface() const noexcept { return backdrop_surface_; }
  [[nodiscard]] bool owns_surface(const wl_surface* s) const noexcept {
    return s && (s == surface_ || s == backdrop_surface_);
  }

  [[nodiscard]] wl_display* display() const noexcept { return wl_ ? wl_->display() : nullptr; }
  void detach_vk() noexcept { vk_layer_.detach(); }

  void toggle(int anchor_x, uint32_t serial);
  void close();

  void paint_dock_button(cairo_t* cr, double x, double y, double size, bool hovered) const;
  bool hit_test_dock_button(double px, double py, double btn_x, double btn_y, double btn_size) const noexcept;

  static void close_before_dock_popup(DockApp& dock);

  static void layer_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t w, uint32_t h);
  static void layer_closed(void* data, zwlr_layer_surface_v1* surface);

  static void seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities);

  void request_repaint();
  static void seat_name(void* data, wl_seat* seat, const char* name);

  static void pointer_enter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
                            wl_fixed_t surface_x, wl_fixed_t surface_y);
  static void pointer_leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
  static void pointer_motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t surface_x,
                             wl_fixed_t surface_y);
  static void pointer_button(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time,
                             uint32_t button, uint32_t state);
  static void pointer_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis,
                           wl_fixed_t value);
  static void pointer_frame(void* data, wl_pointer* pointer);
  static void pointer_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source);
  static void pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
  static void pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);

  static void keyboard_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd,
                              uint32_t size);
  static void keyboard_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
                             wl_array* keys);
  static void keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
  static void keyboard_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time,
                           uint32_t key, uint32_t state);
  static void keyboard_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial,
                                 uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                 uint32_t group);
  static void keyboard_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);

private:
  void handle_pointer_enter(wl_surface* surface, double sx, double sy);
  void handle_pointer_leave();
  void handle_pointer_motion(double sx, double sy);
  void handle_pointer_button(uint32_t button, uint32_t state);
  void handle_pointer_axis(double delta);
  void handle_key(uint32_t key, uint32_t state);
  void open_folder(int hit_idx);
  void close_folder();
  void commit_folder_rename();
  void create_folder_from_drag(int src_idx, int dst_idx);
  void add_app_to_folder(int folder_def_idx, const std::string& appId);
  void remove_app_from_folder(int folder_def_idx, int child_hit_idx);
  void remove_empty_folders();
  void organize_folders();

  static std::string launchpad_state_path();
  void load_launchpad_settings();
  void save_launchpad_settings() const;

  std::vector<LaunchpadFolderDef> folders_{};
  std::unique_ptr<eh::wayland::WaylandConnection> wl_;
  wl_seat* seat_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  wl_keyboard* keyboard_ = nullptr;

  xkb_context* xkbCtx_ = nullptr;
  xkb_keymap* xkbKeymap_ = nullptr;
  xkb_state* xkbState_ = nullptr;

  bool pointer_focus_ = false;

  // Drag-to-create-folder state
  int drag_src_idx_ = -1;
  bool drag_candidate_ = false;
  bool drag_active_ = false;
  double drag_start_x_ = 0;
  double drag_start_y_ = 0;

  // Folder-open state
  bool folder_open_ = false;
  int folder_open_idx_ = -1;          // index in hits_ of the open folder entry
  int folder_hover_child_ = -1;       // index into folder_children_ for hover, -1 if none
  int folder_page_ = 0;               // current page in folder overlay (0-based)
  std::vector<LaunchpadHit> folder_children_{};  // resolved children for open folder
  bool folder_editing_ = false;
  std::string folder_edit_buffer_;
  bool folder_title_hovered_ = false;
  bool organize_hovered_ = false;
  // Context menu state (right-click pin options)
  bool ctx_open_ = false;
  int ctx_anchor_row_ = -1;
  double ctx_menu_x_ = 0;
  double ctx_menu_y_ = 0;
  int ctx_hover_item_ = -1;
  bool ctx_pinned_dock_ = false;
  bool ctx_pinned_start_ = false;
  bool ctx_pinned_drawer_ = false;

  // App directory inotify watch for auto-refresh on install/uninstall
  int appdir_inotify_fd_ = -1;
  int open_appdir_inotify();
  void drain_appdir_inotify();

  // Drag-from-folder state
  bool folder_drag_candidate_ = false;
  bool folder_drag_active_ = false;
  int folder_drag_child_idx_ = -1;
  double folder_drag_start_x_ = 0;
  double folder_drag_start_y_ = 0;

  DockApp& dock_;

  wl_surface* backdrop_surface_ = nullptr;
  zwlr_layer_surface_v1* backdrop_layer_ = nullptr;
  wp_viewport* backdrop_viewport_ = nullptr;
  eh::wayland::ShmBuffer backdrop_buf_{};
  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* layer_ = nullptr;
  eh::wayland::CairoCpuBuffer cpu_buf_{};
  // SHM fallback so launchpad renders even when the dock runs the CPU
  // backend and there is no Vulkan context to upload into.
  eh::wayland::ShmBuffer shm_buf_{};
  eh::wayland::VulkanLayerSurface vk_layer_{};
  int w_ = 0;
  int h_ = 0;
  int page_ = 0;
  int sel_ = -1;
  int hover_idx_ = -1;
  double scrollAccum_ = 0.0;
  std::vector<LaunchpadHit> hits_{};
  LaunchpadLayout cached_layout_{};
  uint64_t cached_layout_gen_ = 0;
  std::string query_{};
  std::string prev_query_{};
  std::vector<size_t> prev_cat_indices_{};
  bool power_confirm_open_ = false;
  int power_confirm_idx_ = -1;
  double ptr_x_ = 0;
  double ptr_y_ = 0;
  bool open_ = false;
  eh::shell::AnimationManager page_anim_{};
  eh::shell::AnimationManager hover_anim_{};
  float hover_anim_scale_ = 1.f;
  int hover_anim_target_ = -1;
  wl_callback* page_frame_cb_ = nullptr;
  float page_slide_t_ = -1.f;
  int page_slide_from_ = 0;
  int page_slide_to_ = 0;
  eh::wayland::CairoCpuBuffer output_cache_;
  uint64_t cached_content_key_ = 0;
  eh::wayland::CairoCpuBuffer cached_page_from_;
  eh::wayland::CairoCpuBuffer cached_page_to_;
  int cached_from_page_ = -1;
  int cached_to_page_ = -1;

  // Track last applied launchpad grid size so we can detect live changes from
  // the launcher settings (Columns/Rows) and force the grid + folder cards to
  // re-layout immediately (so 9 folders wrap at the configured count instead
  // of staying in a long "first row").
  int last_grid_cols_ = -1;
  int last_grid_rows_ = -1;

  [[nodiscard]] LaunchpadPaintModel paint_model();
  void refresh_catalog();
  void begin_page_slide(int from_page, int to_page);
  void schedule_page_frame();
  void paint_backdrop();
  void paint_content();
  void repaint_all();
  [[nodiscard]] wl_output* pick_dock_follow_output() const;
  bool create_layer();
  void destroy_layer();
  void destroy_seat();
  [[nodiscard]] uint64_t base_content_key() const;
};

}
