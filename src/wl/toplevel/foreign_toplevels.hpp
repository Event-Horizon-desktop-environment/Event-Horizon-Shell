#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eh::wayland {

class ForeignToplevels {
public:
  struct Toplevel {
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    std::string appId{};
    std::string title{};
    bool activated = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool closed = false;
    uint64_t serial = 0;
  };

  ForeignToplevels() = default;
  ForeignToplevels(const ForeignToplevels&) = delete;
  ForeignToplevels& operator=(const ForeignToplevels&) = delete;
  ForeignToplevels(ForeignToplevels&&) = delete;
  ForeignToplevels& operator=(ForeignToplevels&&) = delete;
  ~ForeignToplevels();

  void set_debug(bool on) { debug_ = on; }

  void attach(zwlr_foreign_toplevel_manager_v1* mgr, wl_display* display = nullptr);

  void shutdown();

  void set_handle_destroyed_hook(std::function<void(zwlr_foreign_toplevel_handle_v1*)> cb) {
    handleDestroyedHook_ = std::move(cb);
  }

  void set_handle_done_hook(std::function<void(zwlr_foreign_toplevel_handle_v1*)> cb) {
    handleDoneHook_ = std::move(cb);
  }

  void set_visual_dirty_hook(std::function<void()> cb) { visualDirtyHook_ = std::move(cb); }

  // Extra observers of the full snapshot. Fired at the same moments as the
  // visual-dirty hook (toplevel added, done, closed, manager finished, prune)
  // so a bus bridge (ToplevelService) can re-derive and broadcast the window
  // list. The returned token removes the callback later.
  uint64_t add_snapshot_cb(std::function<void()> cb);
  void remove_snapshot_cb(uint64_t token);

  void prune_closed();

  [[nodiscard]] size_t size() const { return toplevels_.size(); }
  [[nodiscard]] const std::vector<Toplevel>& list() const { return toplevels_; }
  [[nodiscard]] std::vector<Toplevel>& list() { return toplevels_; }

  auto begin() { return toplevels_.begin(); }
  auto end() { return toplevels_.end(); }
  auto begin() const { return toplevels_.begin(); }
  auto end() const { return toplevels_.end(); }

  const Toplevel* find(zwlr_foreign_toplevel_handle_v1* handle) const;

private:
  static void on_title(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title);
  static void on_app_id(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* app_id);
  static void on_state(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_array* states);
  static void on_done(void* data, zwlr_foreign_toplevel_handle_v1* handle);
  static void on_closed(void* data, zwlr_foreign_toplevel_handle_v1* handle);
  static void on_parent(void* data, zwlr_foreign_toplevel_handle_v1* handle, zwlr_foreign_toplevel_handle_v1* parent);
  static void on_output_enter(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_output* output);
  static void on_output_leave(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_output* output);

  static void on_manager_toplevel(void* data, zwlr_foreign_toplevel_manager_v1* mgr,
                                 zwlr_foreign_toplevel_handle_v1* toplevel);
  static void on_manager_finished(void* data, zwlr_foreign_toplevel_manager_v1* mgr);

  const Toplevel* find_mutable(zwlr_foreign_toplevel_handle_v1* handle, size_t* idx_out);

  zwlr_foreign_toplevel_manager_v1* mgr_ = nullptr;
  wl_display* display_ = nullptr;
  std::vector<Toplevel> toplevels_{};
  uint64_t nextSerial_ = 1;
  bool debug_ = false;

  bool pruningClosed_ = false;
  std::function<void(zwlr_foreign_toplevel_handle_v1*)> handleDestroyedHook_{};
  std::function<void(zwlr_foreign_toplevel_handle_v1*)> handleDoneHook_{};
  std::function<void()> visualDirtyHook_{};

  struct SnapshotCb {
    uint64_t token = 0;
    std::function<void()> fn;
  };
  std::vector<SnapshotCb> snapshotCbs_{};
  uint64_t nextSnapshotCbToken_ = 1;

  void notify_visual_dirty() {
    if (visualDirtyHook_) visualDirtyHook_();
  }
  void notify_snapshot_cbs();
};

}
