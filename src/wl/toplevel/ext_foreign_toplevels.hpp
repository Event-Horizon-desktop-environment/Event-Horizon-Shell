#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct ext_foreign_toplevel_list_v1;
struct ext_foreign_toplevel_handle_v1;
struct wl_display;

namespace eh::wayland {

class ExtForeignToplevels {
public:
  struct Toplevel {
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    std::string appId{};
    std::string title{};
    std::string identifier{};
    uint64_t serial = 0;
  };

  using ChangedCb = std::function<void()>;

  ExtForeignToplevels() = default;
  ExtForeignToplevels(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels& operator=(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels(ExtForeignToplevels&&) = delete;
  ExtForeignToplevels& operator=(ExtForeignToplevels&&) = delete;
  ~ExtForeignToplevels();

  void bind(ext_foreign_toplevel_list_v1* list, wl_display* display);
  void shutdown();

  void set_changed_cb(ChangedCb cb) {
    changedCbs_.clear();
    add_changed_cb(std::move(cb));
  }

  // Extra observers (ToplevelService bridge). Fired whenever the tracked list
  // may have changed (toplevel added, done, closed, finished). The returned
  // token removes the callback later.
  uint64_t add_changed_cb(std::function<void()> cb);
  void remove_changed_cb(uint64_t token);

  [[nodiscard]] size_t size() const { return toplevels_.size(); }
  [[nodiscard]] const std::vector<Toplevel>& list() const { return toplevels_; }
  [[nodiscard]] std::vector<Toplevel>& list() { return toplevels_; }
  auto begin() { return toplevels_.begin(); }
  auto end() { return toplevels_.end(); }
  auto begin() const { return toplevels_.begin(); }
  auto end() const { return toplevels_.end(); }

  const Toplevel* find(ext_foreign_toplevel_handle_v1* handle) const;

private:
  static void on_toplevel(void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle);
  static void on_finished(void* data, ext_foreign_toplevel_list_v1*);
  static void on_closed(void* data, ext_foreign_toplevel_handle_v1* handle);
  static void on_done(void* data, ext_foreign_toplevel_handle_v1* handle);
  static void on_title(void* data, ext_foreign_toplevel_handle_v1* handle, const char* title);
  static void on_app_id(void* data, ext_foreign_toplevel_handle_v1* handle, const char* app_id);
  static void on_identifier(void* data, ext_foreign_toplevel_handle_v1* handle, const char* identifier);

  const Toplevel* find_mutable(ext_foreign_toplevel_handle_v1* handle, size_t* idx_out);

  ext_foreign_toplevel_list_v1* list_ = nullptr;
  wl_display* display_ = nullptr;
  std::vector<Toplevel> toplevels_{};
  uint64_t nextSerial_ = 1;
  bool initialSyncDone_ = false;

  struct ChangedCbEntry {
    uint64_t token = 0;
    std::function<void()> fn;
  };
  std::vector<ChangedCbEntry> changedCbs_{};
  uint64_t nextChangedCbToken_ = 1;

  void notify_changed() {
    for (const auto& cb : changedCbs_) {
      if (cb.fn) cb.fn();
    }
  }
};

}
