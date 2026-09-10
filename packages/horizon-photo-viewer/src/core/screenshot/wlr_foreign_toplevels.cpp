#include "core/screenshot/wlr_foreign_toplevels.hpp"
#include "core/screenshot/logging.hpp"

#include <algorithm>
#include <cstring>
#include <wayland-client.h>

#include <wlr-foreign-toplevel-management-unstable-v1-client-protocol.h>

namespace hpv::sc {

WlrForeignToplevels::~WlrForeignToplevels() { shutdown(); }

void WlrForeignToplevels::bind(zwlr_foreign_toplevel_manager_v1* manager, wl_display* display)
{
  SC_LOG("WlrForeignToplevels::bind manager=%p display=%p", (void*)manager, (void*)display);
  if (!manager || manager_ == manager) { SC_LOG("WlrForeignToplevels::bind: already bound or null"); return; }
  manager_ = manager;
  display_ = display;

  static const zwlr_foreign_toplevel_manager_v1_listener kMgrListener = {
      .toplevel = on_toplevel,
      .finished = on_finished,
  };
  zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &kMgrListener, this);
}

void WlrForeignToplevels::shutdown()
{
  SC_LOG("WlrForeignToplevels::shutdown toplevels=%zu", toplevels_.size());
  if (display_ && wl_display_get_error(display_)) {
    display_ = nullptr;
    for (auto& tl : toplevels_) tl.handle = nullptr;
    toplevels_.clear();
    manager_ = nullptr;
    initialSyncDone_ = false;
    return;
  }
  for (auto& tl : toplevels_) {
    if (tl.handle) {
      zwlr_foreign_toplevel_handle_v1_destroy(tl.handle);
      tl.handle = nullptr;
    }
  }
  toplevels_.clear();
  if (manager_) {
    zwlr_foreign_toplevel_manager_v1_destroy(manager_);
    manager_ = nullptr;
  }
  display_ = nullptr;
  initialSyncDone_ = false;
}

void WlrForeignToplevels::on_toplevel(void* data, zwlr_foreign_toplevel_manager_v1*,
                                        zwlr_foreign_toplevel_handle_v1* handle)
{
  SC_LOG("WlrForeignToplevels::on_toplevel handle=%p", (void*)handle);
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  Toplevel tl;
  tl.handle = handle;
  self.toplevels_.push_back(std::move(tl));
  SC_LOG("WlrForeignToplevels::on_toplevel: now %zu toplevels tracked", self.toplevels_.size());

  static const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
      .title = on_title,
      .app_id = on_app_id,
      .output_enter = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
      .output_leave = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
      .state = on_state,
      .done = on_done,
      .closed = on_closed,
      .parent = on_parent,
  };
  zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, &self);
  self.dirty_ = true;
}

void WlrForeignToplevels::on_finished(void* data, zwlr_foreign_toplevel_manager_v1* manager)
{
  SC_LOG("WlrForeignToplevels::on_finished");
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  (void)manager;
  if (self.display_ && !self.initialSyncDone_) {
    (void)wl_display_roundtrip(self.display_);
    self.initialSyncDone_ = true;
  }
}

void WlrForeignToplevels::on_title(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title)
{
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.title = title ? title : ""; SC_LOG("WlrForeignToplevels::on_title handle=%p title='%s'", (void*)handle, tl.title.c_str()); return; }
  }
  SC_LOG("WlrForeignToplevels::on_title handle=%p — NOT FOUND in toplevels", (void*)handle);
}

void WlrForeignToplevels::on_app_id(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* app_id)
{
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.appId = app_id ? app_id : ""; SC_LOG("WlrForeignToplevels::on_app_id handle=%p app_id='%s'", (void*)handle, tl.appId.c_str()); return; }
  }
  SC_LOG("WlrForeignToplevels::on_app_id handle=%p — NOT FOUND in toplevels", (void*)handle);
}

void WlrForeignToplevels::on_parent(void* data, zwlr_foreign_toplevel_handle_v1* handle, zwlr_foreign_toplevel_handle_v1* parent)
{
  SC_LOG("WlrForeignToplevels::on_parent handle=%p parent=%p", (void*)handle, (void*)parent);
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.parent = parent; return; }
  }
}

void WlrForeignToplevels::on_state(void* data, zwlr_foreign_toplevel_handle_v1* handle, struct wl_array* state)
{
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  bool activated = false;
  uint32_t* s = static_cast<uint32_t*>(state->data);
  size_t count = state->size / sizeof(uint32_t);
  std::string states;
  for (size_t i = 0; i < count; ++i) {
    if (!states.empty()) states += "|";
    if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
      activated = true;
      states += "ACTIVATED";
    } else if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
      states += "MAXIMIZED";
    } else if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
      states += "MINIMIZED";
    } else if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) {
      states += "FULLSCREEN";
    } else {
      states += std::to_string(s[i]);
    }
  }
  SC_LOG("WlrForeignToplevels::on_state handle=%p activated=%d states=[%s]", (void*)handle, activated, states.c_str());
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.activated = activated; return; }
  }
  SC_LOG("WlrForeignToplevels::on_state handle=%p — NOT FOUND in toplevels", (void*)handle);
}

void WlrForeignToplevels::on_done(void* data, zwlr_foreign_toplevel_handle_v1* handle)
{
  auto& self = *static_cast<WlrForeignToplevels*>(data);
  SC_LOG("WlrForeignToplevels::on_done handle=%p", (void*)handle);
  for (const auto& tl : self.toplevels_) {
    if (tl.handle == handle) {
      std::string xw = tl.appId.find("XWayland") != std::string::npos ? " [XWayland]" : "";
      SC_LOG("WlrForeignToplevels::on_done: window ready%s title='%s' app_id='%s' parent=%p",
             xw.c_str(), tl.title.c_str(), tl.appId.c_str(), (void*)tl.parent);
      break;
    }
  }
  self.dirty_ = true;
}

void WlrForeignToplevels::on_closed(void* data, zwlr_foreign_toplevel_handle_v1* handle)
{
  SC_LOG("WlrForeignToplevels::on_closed handle=%p", (void*)handle);
  auto& self = *static_cast<WlrForeignToplevels*>(data);

  auto it = std::remove_if(self.toplevels_.begin(), self.toplevels_.end(),
                            [handle](const Toplevel& t) { return t.handle == handle; });
  bool found = (it != self.toplevels_.end());
  if (found) {
    self.toplevels_.erase(it, self.toplevels_.end());
  }
  self.dirty_ = true;

  if (found && handle) {
    if (self.display_ && wl_display_get_error(self.display_)) return;
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
  }
}

}
