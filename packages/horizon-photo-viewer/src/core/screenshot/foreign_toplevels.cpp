#include "core/screenshot/foreign_toplevels.hpp"
#include "core/screenshot/logging.hpp"

#include <algorithm>
#include <wayland-client.h>

#include <ext-foreign-toplevel-list-v1-client-protocol.h>

namespace hpv::sc {

ExtForeignToplevels::~ExtForeignToplevels() { shutdown(); }

void ExtForeignToplevels::bind(ext_foreign_toplevel_list_v1* list, wl_display* display)
{
  SC_LOG("ExtForeignToplevels::bind list=%p display=%p", (void*)list, (void*)display);
  if (!list || list_ == list) { SC_LOG("ExtForeignToplevels::bind: already bound or null"); return; }
  list_ = list;
  display_ = display;

  static const ext_foreign_toplevel_list_v1_listener kListListener = {
      .toplevel = on_toplevel,
      .finished = on_finished,
  };
  ext_foreign_toplevel_list_v1_add_listener(list_, &kListListener, this);
}

void ExtForeignToplevels::shutdown()
{
  SC_LOG("ExtForeignToplevels::shutdown toplevels=%zu", toplevels_.size());
  if (display_ && wl_display_get_error(display_)) {
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

void ExtForeignToplevels::on_toplevel(void* data, ext_foreign_toplevel_list_v1*,
                                        ext_foreign_toplevel_handle_v1* handle)
{
  SC_LOG("ExtForeignToplevels::on_toplevel handle=%p", (void*)handle);
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  Toplevel tl;
  tl.handle = handle;
  self.toplevels_.push_back(std::move(tl));
  SC_LOG("ExtForeignToplevels::on_toplevel: now %zu toplevels tracked", self.toplevels_.size());

  static const ext_foreign_toplevel_handle_v1_listener kHandleListener = {
      .closed = on_closed,
      .done = on_done,
      .title = on_title,
      .app_id = on_app_id,
      .identifier = on_identifier,
  };
  ext_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, &self);
}

void ExtForeignToplevels::on_finished(void* data, ext_foreign_toplevel_list_v1* list)
{
  SC_LOG("ExtForeignToplevels::on_finished");
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  (void)list;
  if (self.display_ && !self.initialSyncDone_) {
    (void)wl_display_roundtrip(self.display_);
    self.initialSyncDone_ = true;
  }
}

void ExtForeignToplevels::on_closed(void* data, ext_foreign_toplevel_handle_v1* handle)
{
  SC_LOG("ExtForeignToplevels::on_closed handle=%p", (void*)handle);
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  auto it = std::remove_if(self.toplevels_.begin(), self.toplevels_.end(),
                            [handle](const Toplevel& t) { return t.handle == handle; });
  bool found = (it != self.toplevels_.end());
  if (found) {
    self.toplevels_.erase(it, self.toplevels_.end());
  }
  SC_LOG("ExtForeignToplevels::on_closed: found=%d remaining=%zu", found, self.toplevels_.size());
  if (handle) {
    if (self.display_ && wl_display_get_error(self.display_)) return;
    ext_foreign_toplevel_handle_v1_destroy(handle);
  }
}

void ExtForeignToplevels::on_done(void* data, ext_foreign_toplevel_handle_v1* handle)
{
  SC_LOG("ExtForeignToplevels::on_done handle=%p", (void*)handle);
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  for (const auto& tl : self.toplevels_) {
    if (tl.handle == handle) {
      std::string xw = tl.appId.find("XWayland") != std::string::npos ? " [XWayland]" : "";
      SC_LOG("ExtForeignToplevels::on_done: window ready%s title='%s' app_id='%s' identifier='%s'",
             xw.c_str(), tl.title.c_str(), tl.appId.c_str(), tl.identifier.c_str());
      break;
    }
  }
}

void ExtForeignToplevels::on_title(void* data, ext_foreign_toplevel_handle_v1* handle, const char* title)
{
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.title = title ? title : ""; SC_LOG("ExtForeignToplevels::on_title handle=%p title='%s'", (void*)handle, tl.title.c_str()); return; }
  }
  SC_LOG("ExtForeignToplevels::on_title handle=%p — NOT FOUND in toplevels", (void*)handle);
}

void ExtForeignToplevels::on_app_id(void* data, ext_foreign_toplevel_handle_v1* handle, const char* app_id)
{
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.appId = app_id ? app_id : ""; SC_LOG("ExtForeignToplevels::on_app_id handle=%p app_id='%s'", (void*)handle, tl.appId.c_str()); return; }
  }
  SC_LOG("ExtForeignToplevels::on_app_id handle=%p — NOT FOUND in toplevels", (void*)handle);
}

void ExtForeignToplevels::on_identifier(void* data, ext_foreign_toplevel_handle_v1* handle, const char* identifier)
{
  auto& self = *static_cast<ExtForeignToplevels*>(data);
  for (auto& tl : self.toplevels_) {
    if (tl.handle == handle) { tl.identifier = identifier ? identifier : ""; SC_LOG("ExtForeignToplevels::on_identifier handle=%p identifier='%s'", (void*)handle, tl.identifier.c_str()); return; }
  }
  SC_LOG("ExtForeignToplevels::on_identifier handle=%p — NOT FOUND in toplevels", (void*)handle);
}

}
