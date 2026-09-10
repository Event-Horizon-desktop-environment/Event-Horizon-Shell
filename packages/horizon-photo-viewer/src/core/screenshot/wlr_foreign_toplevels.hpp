#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;
struct wl_display;
struct wl_array;

namespace hpv::sc {

class WlrForeignToplevels {
public:
  struct Toplevel {
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    std::string appId{};
    std::string title{};
    bool activated = false;
    zwlr_foreign_toplevel_handle_v1* parent = nullptr;
  };

  WlrForeignToplevels() = default;
  WlrForeignToplevels(const WlrForeignToplevels&) = delete;
  WlrForeignToplevels& operator=(const WlrForeignToplevels&) = delete;
  WlrForeignToplevels(WlrForeignToplevels&&) = delete;
  WlrForeignToplevels& operator=(WlrForeignToplevels&&) = delete;
  ~WlrForeignToplevels();

  void bind(zwlr_foreign_toplevel_manager_v1* manager, wl_display* display);
  void shutdown();

  [[nodiscard]] size_t size() const { return toplevels_.size(); }
  [[nodiscard]] const std::vector<Toplevel>& list() const { return toplevels_; }
  [[nodiscard]] std::vector<Toplevel>& list() { dirty_ = false; return toplevels_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  auto begin() { return toplevels_.begin(); }
  auto end() { return toplevels_.end(); }
  auto begin() const { return toplevels_.begin(); }
  auto end() const { return toplevels_.end(); }

private:
  static void on_toplevel(void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle);
  static void on_finished(void* data, zwlr_foreign_toplevel_manager_v1*);

  static void on_title(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title);
  static void on_app_id(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* app_id);
  static void on_state(void* data, zwlr_foreign_toplevel_handle_v1* handle, struct wl_array* state);
  static void on_parent(void* data, zwlr_foreign_toplevel_handle_v1* handle, zwlr_foreign_toplevel_handle_v1* parent);
  static void on_done(void* data, zwlr_foreign_toplevel_handle_v1* handle);
  static void on_closed(void* data, zwlr_foreign_toplevel_handle_v1* handle);

  zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
  wl_display* display_ = nullptr;
  std::vector<Toplevel> toplevels_{};
  bool initialSyncDone_ = false;
  bool dirty_ = false;
};

}
