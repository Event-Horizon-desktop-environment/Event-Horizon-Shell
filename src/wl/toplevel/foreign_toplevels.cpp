#include "wl/toplevel/foreign_toplevels.hpp"

#include <algorithm>
#include <iostream>

namespace eh::wayland {

ForeignToplevels::~ForeignToplevels() { shutdown(); }

void ForeignToplevels::attach(zwlr_foreign_toplevel_manager_v1* mgr, wl_display* display) {
   
  if (!mgr || mgr_ == mgr) return;
  mgr_ = mgr;
  display_ = display;

  static const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
      .title = on_title,
      .app_id = on_app_id,
      .output_enter = on_output_enter,
      .output_leave = on_output_leave,
      .state = on_state,
      .done = on_done,
      .closed = on_closed,
      .parent = on_parent,
  };

  static const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {
      .toplevel = on_manager_toplevel,
      .finished = on_manager_finished,
  };

  zwlr_foreign_toplevel_manager_v1_add_listener(mgr_, &kManagerListener, this);

  (void)kHandleListener;
}

void ForeignToplevels::shutdown() {
   
  handleDestroyedHook_ = nullptr;
  handleDoneHook_ = nullptr;
  visualDirtyHook_ = nullptr;
  snapshotCbs_.clear();

  // Any proxy operation will segfault once the display has errored.
  if (!display_ || !wl_display_get_error(display_)) {
    for (auto& tl : toplevels_) {
      if (tl.handle) {
        zwlr_foreign_toplevel_handle_v1_destroy(tl.handle);
        tl.handle = nullptr;
      }
    }
    if (mgr_) {
      zwlr_foreign_toplevel_manager_v1_stop(mgr_);
      zwlr_foreign_toplevel_manager_v1_destroy(mgr_);
      mgr_ = nullptr;
    }
  }

  toplevels_.clear();
}

const ForeignToplevels::Toplevel* ForeignToplevels::find(zwlr_foreign_toplevel_handle_v1* handle) const {
   
  for (const auto& tl : toplevels_) {
    if (tl.handle == handle) return &tl;
  }
  return nullptr;
}

const ForeignToplevels::Toplevel* ForeignToplevels::find_mutable(zwlr_foreign_toplevel_handle_v1* handle, size_t* idx_out) {
   
  for (size_t i = 0; i < toplevels_.size(); i++) {
    if (toplevels_[i].handle == handle) {
      if (idx_out) *idx_out = i;
      return &toplevels_[i];
    }
  }
  return nullptr;
}

uint64_t ForeignToplevels::add_snapshot_cb(std::function<void()> cb) {
  const uint64_t token = nextSnapshotCbToken_++;
  snapshotCbs_.push_back({token, std::move(cb)});
  return token;
}

void ForeignToplevels::remove_snapshot_cb(uint64_t token) {
  snapshotCbs_.erase(std::remove_if(snapshotCbs_.begin(), snapshotCbs_.end(),
                                    [token](const SnapshotCb& cb) { return cb.token == token; }),
                     snapshotCbs_.end());
}

void ForeignToplevels::notify_snapshot_cbs() {
  for (const auto& cb : snapshotCbs_) {
    if (cb.fn) cb.fn();
  }
}

void ForeignToplevels::prune_closed() {
   
  if (pruningClosed_) return;
  pruningClosed_ = true;
  toplevels_.erase(std::remove_if(toplevels_.begin(), toplevels_.end(), [](const Toplevel& t) { return t.closed; }),
                   toplevels_.end());
  pruningClosed_ = false;
  notify_snapshot_cbs();
}

void ForeignToplevels::on_title(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;
  self.toplevels_[idx].title = title ? title : "";
  if (self.debug_) std::cout << "[toplevel] title: app_id='" << self.toplevels_[idx].appId << "' title='" << self.toplevels_[idx].title << "'\n";
}

void ForeignToplevels::on_app_id(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* app_id) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;
  self.toplevels_[idx].appId = app_id ? app_id : "";
  if (self.debug_) std::cout << "[toplevel] app_id: '" << self.toplevels_[idx].appId << "'\n";
}

void ForeignToplevels::on_state(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_array* states) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;

  bool activated = false;
  bool minimized = false;
  bool maximized = false;
  bool fullscreen = false;

  if (!states || (states->size > 0 && !states->data)) return;
  if ((states->size % sizeof(uint32_t)) != 0) return;

  const auto* begin = states->size > 0 ? static_cast<const uint32_t*>(states->data) : nullptr;
  const auto* end =
      begin ? reinterpret_cast<const uint32_t*>(static_cast<const char*>(states->data) + states->size) : nullptr;
  for (const uint32_t* s = begin; s < end; s++) {
    if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) activated = true;
    if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) minimized = true;
    if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) maximized = true;
    if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) fullscreen = true;
  }

  auto& tl = self.toplevels_[idx];
  tl.activated = activated;
  tl.minimized = minimized;
  tl.maximized = maximized;
  tl.fullscreen = fullscreen;
  if (self.debug_) {
    std::cout << "[toplevel] state: app_id='" << tl.appId << "' activated=" << (tl.activated ? 1 : 0)
              << " minimized=" << (tl.minimized ? 1 : 0) << "\n";
  }
}

void ForeignToplevels::on_done(void* data, zwlr_foreign_toplevel_handle_v1* handle) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  if (self.handleDoneHook_) self.handleDoneHook_(handle);
  self.notify_visual_dirty();
  self.notify_snapshot_cbs();
}

void ForeignToplevels::on_closed(void* data, zwlr_foreign_toplevel_handle_v1* handle) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) {
      tl.closed = true;
      if (self.debug_) std::cout << "[toplevel] closed: app_id='" << tl.appId << "' title='" << tl.title << "'\n";
      if (self.handleDestroyedHook_) self.handleDestroyedHook_(handle);
      if (tl.handle && (!self.display_ || !wl_display_get_error(self.display_)))
        zwlr_foreign_toplevel_handle_v1_destroy(tl.handle);
      tl.handle = nullptr;
      self.notify_visual_dirty();
      self.notify_snapshot_cbs();
      break;
    }
  }

}

void ForeignToplevels::on_parent(void*  , zwlr_foreign_toplevel_handle_v1*  ,
                                zwlr_foreign_toplevel_handle_v1*  ) {}
void ForeignToplevels::on_output_enter(void*  , zwlr_foreign_toplevel_handle_v1*  , wl_output*  ) {}
void ForeignToplevels::on_output_leave(void*  , zwlr_foreign_toplevel_handle_v1*  , wl_output*  ) {}

void ForeignToplevels::on_manager_toplevel(void* data, zwlr_foreign_toplevel_manager_v1*  ,
                                           zwlr_foreign_toplevel_handle_v1* toplevel) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  Toplevel tl;
  tl.handle = toplevel;
  tl.serial = self.nextSerial_++;
  self.toplevels_.push_back(std::move(tl));
  if (self.debug_) std::cout << "[toplevel] created: serial=" << self.toplevels_.back().serial << "\n";

  static const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
      .title = on_title,
      .app_id = on_app_id,
      .output_enter = on_output_enter,
      .output_leave = on_output_leave,
      .state = on_state,
      .done = on_done,
      .closed = on_closed,
      .parent = on_parent,
  };
  zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &kHandleListener, &self);
  self.notify_visual_dirty();
  self.notify_snapshot_cbs();
}

void ForeignToplevels::on_manager_finished(void* data, zwlr_foreign_toplevel_manager_v1*  ) {
   
  auto& self = *static_cast<ForeignToplevels*>(data);
  self.mgr_ = nullptr;
  self.notify_visual_dirty();
  self.notify_snapshot_cbs();
}

}
