#pragma once

#include "desktop_shell/Overview/overview_input.hpp"
#include "desktop_shell/Overview/overview_capture.hpp"
#include "desktop_shell/Overview/overview_actions.hpp"
#include "desktop_shell/Overview/overview_reveal.hpp"
#include "desktop_shell/Overview/overview_painter.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "wl/core/connection.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct DockApp;
struct wl_array;
struct wl_callback;
struct wl_keyboard;
struct wl_pointer;
struct wl_seat;
struct wp_viewport;
struct wl_surface;
struct zwlr_layer_surface_v1;

namespace eh::wayland {
struct ToplevelStreamFrame;
class ToplevelStream;
} // namespace eh::wayland

namespace eh::shell::overview {

void overview_host_page_frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms);

class Host {
  friend void overview_host_page_frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms);

public:
  explicit Host(DockApp& dock, std::unique_ptr<eh::wayland::WaylandConnection> wl);
  ~Host();

  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  void toggle();
  void close();

  [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
  [[nodiscard]] wl_surface* backdrop_surface() const noexcept { return backdrop_surface_; }
  [[nodiscard]] bool owns_surface(const wl_surface* s) const noexcept {
    if (!s) return false;
    if (s == surface_ || s == backdrop_surface_) return true;
    for (const auto& e : extra_outputs_)
      if (s == e->surface || s == e->backdrop_surface) return true;
    return false;
  }
  [[nodiscard]] wl_display* display() const noexcept { return wl_ ? wl_->display() : nullptr; }
  void detach_vk() noexcept;
  [[nodiscard]] bool multi_monitor() const noexcept { return multi_monitor_; }

  static void layer_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t w, uint32_t h);
  static void layer_closed(void* data, zwlr_layer_surface_v1* surface);

  static void seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities);
  static void seat_name(void* data, wl_seat* seat, const char* name);

  static void pointer_enter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
                             wl_fixed_t surface_x, wl_fixed_t surface_y);
  static void pointer_leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
  static void pointer_motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t surface_x,
                              wl_fixed_t surface_y);
  static void pointer_button(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time,
                              uint32_t button, uint32_t state);
  static void pointer_frame(void* data, wl_pointer* pointer);
  static void pointer_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source);
  static void pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
  static void pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
  static void pointer_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis,
                            wl_fixed_t value);

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

  void select_workspace(int wsIndex);
  void activate_window(int flatIdx);
  void close_window(int flatIdx);
  void move_window_to_workspace(int flatIdx, int targetWs);
  void move_window_to_new_workspace(int flatIdx);
  void swap_windows_in_place(int flatIdx, int targetFlatIdx);

  [[nodiscard]] const std::vector<OverviewWorkspace>& workspaces() const noexcept { return workspaces_; }
  [[nodiscard]] std::vector<OverviewWorkspace>& workspaces_mut() noexcept { return workspaces_; }
  [[nodiscard]] const std::vector<std::pair<int, int>>& nav_windows() const noexcept { return actions_->nav_windows(); }
  [[nodiscard]] const OverviewLayout& layout() const noexcept { return layout_; }
  [[nodiscard]] double scroll_pos() const noexcept { return scroll_pos_; }
  [[nodiscard]] double& scroll_target() noexcept { return scroll_target_; }
  void scroll_by(double delta, const wl_surface* surface);
  [[nodiscard]] int selected_index() const noexcept { return selected_index_; }
  [[nodiscard]] int scroll_event_delay_ms() const noexcept { return scroll_event_delay_ms_; }
  [[nodiscard]] bool show_apps() const noexcept { return show_apps_; }
  [[nodiscard]] uint64_t prefetch_start_ms() const noexcept { return prefetch_start_ms_; }
  [[nodiscard]] double app_scroll_pos() const noexcept { return app_scroll_pos_; }
  void scroll_app_grid(double delta);
  void make_app_visible(int idx);
  void set_show_apps(bool v) noexcept { show_apps_ = v; }
  [[nodiscard]] std::string& search_query() noexcept { return search_query_; }
  [[nodiscard]] const std::string& search_query() const noexcept { return search_query_; }
  [[nodiscard]] const std::vector<SpotlightHit>& apps() const noexcept { return apps_; }
  [[nodiscard]] DockApp& dock() noexcept { return dock_; }
  [[nodiscard]] const std::unique_ptr<eh::wayland::WaylandConnection>& wl() const noexcept { return wl_; }
  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] bool has_frame_cb() const noexcept { return frame_cb_ != nullptr; }
  [[nodiscard]] AnimationManager& anim() noexcept { return anim_; }
  [[nodiscard]] Input& input() noexcept { return *input_; }
  [[nodiscard]] Capture& capture() noexcept { return *capture_; }
  [[nodiscard]] Actions& actions() noexcept { return *actions_; }

  void schedule_frame();
  void refresh_workspace_data();
  void recompute_layout();
  void recompute_scroll_bounds();
  void refresh_app_list();
  void toggle_apps_mode();
  void set_data_dirty() noexcept { data_dirty_ = true; }
  void on_hyprland_event(std::string_view event, std::string_view data);

  // Compositor event socket (.socket2.sock) for event-driven state updates.
  bool connect_hyprland_events();
  void disconnect_hyprland_events();
  void drain_hyprland_events();
  [[nodiscard]] int hyprland_event_fd() const noexcept { return hypr_ev_fd_; }
  void set_cards_invalidated() noexcept { cards_invalidated_ = true; }
  [[nodiscard]] bool consume_cards_invalidated() noexcept {
    const bool v = cards_invalidated_;
    cards_invalidated_ = false;
    return v;
  }
  void set_recapture();
  void maybe_refresh_data();

  // Multi-monitor support.
  struct OverviewOutput {
    wl_output* output = nullptr;
    std::string name;  // e.g. "DP-2"
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    wp_viewport* viewport = nullptr;
    eh::wayland::VulkanLayerSurface vk_layer{};
    wl_surface* backdrop_surface = nullptr;
    zwlr_layer_surface_v1* backdrop_layer = nullptr;
    wp_viewport* backdrop_viewport = nullptr;
    eh::wayland::ShmBuffer backdrop_buf{};
    // CPU fallback presentation buffer (used when Vulkan is unavailable).
    eh::wayland::ShmBuffer cpu_buf{};
    int w = 0;
    int h = 0;
    // Per-output workspace/scroll state for independent navigation.
    std::vector<OverviewWorkspace> workspaces;
    double scroll_pos = 0.0;
    double scroll_target = 0.0;
    int selected_index = 0;
    OverviewOutput() = default;
    OverviewOutput(const OverviewOutput&) = delete;
    OverviewOutput& operator=(const OverviewOutput&) = delete;
    OverviewOutput(OverviewOutput&&) = delete;
    OverviewOutput& operator=(OverviewOutput&&) = delete;
  };

private:
  void refresh_workspace_data_impl();
  bool refresh_from_hyprland();
  void refresh_from_dock();
  void update_scroll(double dt);
  void snap_scroll();
  [[nodiscard]] bool scroll_active() const noexcept;
  [[nodiscard]] bool has_hover_animation_active() const;
  void paint_backdrop();
  void paint_content();
  void paint_extra_content(OverviewOutput& e, bool use_vk);
  void repaint_all();
  [[nodiscard]] bool vulkan_enabled() const;
  [[nodiscard]] bool ensure_vk_context();
  void prewarm_caches();
  bool create_layer();
  void destroy_layer();
  void clear_extra_outputs();
  int count_painted_windows() const;

  DockApp& dock_;
  std::unique_ptr<eh::wayland::WaylandConnection> wl_;

  std::unique_ptr<Input> input_;
  std::unique_ptr<Capture> capture_;
  std::unique_ptr<Actions> actions_;
  std::unique_ptr<RevealPass> reveal_;

  wl_surface* backdrop_surface_ = nullptr;
  zwlr_layer_surface_v1* backdrop_layer_ = nullptr;
  wp_viewport* backdrop_viewport_ = nullptr;
  eh::wayland::ShmBuffer backdrop_buf_{};

  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* layer_ = nullptr;
  eh::wayland::CairoCpuBuffer cpu_buf_{};
  eh::wayland::VulkanLayerSurface vk_layer_{};
  // The overview owns its presentation path and must not depend on the dock's
  // renderer mode: vk_ctx_ is created lazily for the overview alone, and
  // cpu_present_buf_ presents via shm whenever Vulkan is disabled or failed.
  std::shared_ptr<eh::wayland::VulkanDisplayContext> vk_ctx_{};
  bool vk_unavailable_ = false;
  eh::wayland::ShmBuffer cpu_present_buf_{};

  int w_ = 0;
  int h_ = 0;
  bool open_ = false;
  // True from close() until its close-animation completion callback has run
  // destroy_layer(). toggle() checks this to detect "reopened while still
  // closing" and finish the pending close synchronously instead of letting
  // its deferred destroy_layer() fire later and tear down the surface the
  // reopened overview is now using.
  bool closing_ = false;

  AnimationManager anim_{};
  float progress_ = 0.f;
  wl_callback* frame_cb_ = nullptr;

  OverviewLayout layout_{};
  OverviewColors colors_{};

  std::vector<OverviewWorkspace> workspaces_{};

  double scroll_pos_ = 0.0;
  double scroll_target_ = 0.0;
  int selected_index_ = 0;

  uint64_t last_frame_ms_ = 0;
  uint64_t last_data_refresh_ms_ = 0;
  bool data_dirty_ = true;
  uint64_t event_coalesce_deadline_ms_ = 0;
  bool event_pending_ = false;

  int hypr_ev_fd_ = -1;
  std::string hypr_ev_buf_;

  // Multi-monitor support.
  bool multi_monitor_ = false;
  std::vector<std::unique_ptr<OverviewOutput>> extra_outputs_;

  OverviewAxis axis_ = OverviewAxis::Vertical;
  double card_scale_ = 0.5;
  double card_gap_ = 24;
  int scroll_event_delay_ms_ = 200;

  bool backdrop_dirty_ = false;
  bool cards_invalidated_ = false;
  uint64_t prefetch_start_ms_ = 0;

  struct MonitorGeom {
    double x = 0, y = 0, w = 0, h = 0;
  };
  std::unordered_map<int, MonitorGeom> mon_cache_{};
  uint64_t mon_cache_ms_ = 0;
  static constexpr uint64_t kMonCacheTtlMs = 5000;

  std::vector<float> ws_hover_lifts_{};
  std::vector<float> win_hover_lifts_{};
  float search_hover_lift_ = 0.f;

  double app_scroll_pos_ = 0.0;
  double app_scroll_target_ = 0.0;

  // App-grid icons are loaded through the icon cache's background worker;
  // no host-side prewarm state is needed.

  uint64_t apps_open_req_ms_ = 0;
  bool apps_first_paint_logged_ = false;

  bool show_apps_ = false;
  std::string search_query_{};
  std::vector<SpotlightHit> apps_{};
};

} // namespace eh::shell::overview
