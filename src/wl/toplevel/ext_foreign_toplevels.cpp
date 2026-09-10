#include "wl/toplevel/ext_foreign_toplevels.hpp"

#include "ext-foreign-toplevel-list-v1-client-protocol.h"

#include <algorithm>
#include <wayland-client.h>

namespace eh::wayland {

ExtForeignToplevels::~ExtForeignToplevels() { shutdown(); }

uint64_t ExtForeignToplevels::add_changed_cb(std::function<void()> cb) {
  const uint64_t token = nextChangedCbToken_++;
  changedCbs_.push_back({token, std::move(cb)});
  return token;
}

void ExtForeignToplevels::remove_changed_cb(uint64_t token) {
  changedCbs_.erase(std::remove_if(changedCbs_.begin(), changedCbs_.end(),
                                   [token](const ChangedCbEntry& cb) { return cb.token == token; }),
                    changedCbs_.end());
}

void ExtForeignToplevels::bind(ext_foreign_toplevel_list_v1* list, wl_display* display) {
   
  if (!list || list_ == list) return;
  list_ = list;
  display_ = display;

  static const ext_foreign_toplevel_list_v1_listener kListListener = {
      .toplevel = on_toplevel,
      .finished = on_finished,
  };

  static const ext_foreign_toplevel_handle_v1_listener kHandleListener = {
      .closed = on_closed,
      .done = on_done,
      .title = on_title,
      .app_id = on_app_id,
      .identifier = on_identifier,
  };

  ext_foreign_toplevel_list_v1_add_listener(list_, &kListListener, this);
  (void)kHandleListener;
}

void ExtForeignToplevels::shutdown() {
   
  changedCbs_.clear();
  if (display_ && wl_display_get_error(display_)) {
    // The display has errored fatally, so any proxy operation would crash.
    // Just drop the references without touching any Wayland objects.
    display_ = nullptr;
    for (auto& tl : toplevels_) tl.handle = nullptr;
    toplevels_.clear();
    list_ = nullptr;
    initialSyncDone_ = false;
    return;
  }

  for (auto& tl : toplevels_) {
    if (tl.handle) {
      ext_foreign_toplevel_handle_v1_destroy(tl.handle);
      tl.handle = nullptr;
    }
  }
  toplevels_.clear();

  if (list_) {
    ext_foreign_toplevel_list_v1_destroy(list_);
    list_ = nullptr;
  }
  display_ = nullptr;
  initialSyncDone_ = false;
}

const ExtForeignToplevels::Toplevel* ExtForeignToplevels::find(ext_foreign_toplevel_handle_v1* handle) const {
   
  for (const auto& tl : toplevels_) {
    if (tl.handle == handle) return &tl;
  }
  return nullptr;
}

const ExtForeignToplevels::Toplevel* ExtForeignToplevels::find_mutable(
    ext_foreign_toplevel_handle_v1* handle, size_t* idx_out) {
   
  for (size_t i = 0; i < toplevels_.size(); i++) {
    if (toplevels_[i].handle == handle) {
      if (idx_out) *idx_out = i;
      return &toplevels_[i];
    }
  }
  return nullptr;
}

void ExtForeignToplevels::on_toplevel(void* data, ext_foreign_toplevel_list_v1*,
                                       ext_foreign_toplevel_handle_v1* handle) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  Toplevel tl;
  tl.handle = handle;
  tl.serial = self.nextSerial_++;
  self.toplevels_.push_back(std::move(tl));

  static const ext_foreign_toplevel_handle_v1_listener kHandleListener = {
      .closed = on_closed,
      .done = on_done,
      .title = on_title,
      .app_id = on_app_id,
      .identifier = on_identifier,
  };
  ext_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, &self);
  self.notify_changed();
}

void ExtForeignToplevels::on_finished(void* data, ext_foreign_toplevel_list_v1* list) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  (void)list;
  if (self.display_ && !self.initialSyncDone_) {
    (void)wl_display_roundtrip(self.display_);
    self.initialSyncDone_ = true;
  }
  self.notify_changed();
}

void ExtForeignToplevels::on_closed(void* data, ext_foreign_toplevel_handle_v1* handle) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  auto it = std::remove_if(self.toplevels_.begin(), self.toplevels_.end(),
                            [handle](const Toplevel& t) { return t.handle == handle; });
  if (it != self.toplevels_.end()) {
    self.toplevels_.erase(it, self.toplevels_.end());
  }
  if (handle) {
    // If the display already errored, destroying proxies would crash.
    if (self.display_ && wl_display_get_error(self.display_)) return;
    ext_foreign_toplevel_handle_v1_destroy(handle);
  }
  self.notify_changed();
}

void ExtForeignToplevels::on_done(void* data, ext_foreign_toplevel_handle_v1*) {
   
  static_cast<ExtForeignToplevels*>(data)->notify_changed();
}

void ExtForeignToplevels::on_title(void* data, ext_foreign_toplevel_handle_v1* handle, const char* title) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;
  self.toplevels_[idx].title = title ? title : "";
}

void ExtForeignToplevels::on_app_id(void* data, ext_foreign_toplevel_handle_v1* handle, const char* app_id) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;
  self.toplevels_[idx].appId = app_id ? app_id : "";
}

void ExtForeignToplevels::on_identifier(void* data, ext_foreign_toplevel_handle_v1* handle, const char* identifier) {
   
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  size_t idx = 0;
  if (!self.find_mutable(handle, &idx)) return;
  self.toplevels_[idx].identifier = identifier ? identifier : "";
}

}
