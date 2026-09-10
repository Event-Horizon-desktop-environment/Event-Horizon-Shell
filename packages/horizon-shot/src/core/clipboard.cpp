#include "clipboard.hpp"
#include "core/logging.hpp"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <system_error>

#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"

namespace hs::core {

static void ext_source_send(void* user_data, ext_data_control_source_v1*, const char*, int32_t fd) {
  auto* data = static_cast<std::string*>(user_data);
  const char* ptr = data->data();
  size_t remain = data->size();
  while (remain > 0) {
    ssize_t n = write(fd, ptr, remain);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    ptr += n;
    remain -= static_cast<size_t>(n);
  }
  close(fd);
}

static void ext_source_cancelled(void* user_data, ext_data_control_source_v1* src) {
  ext_data_control_source_v1_destroy(src);
  delete static_cast<std::string*>(user_data);
}

static constexpr ext_data_control_source_v1_listener kExtSourceListener_ = {
    .send = ext_source_send,
    .cancelled = ext_source_cancelled,
};

static void wlr_source_send(void* user_data, zwlr_data_control_source_v1*, const char*, int32_t fd) {
  auto* data = static_cast<std::string*>(user_data);
  const char* ptr = data->data();
  size_t remain = data->size();
  while (remain > 0) {
    ssize_t n = write(fd, ptr, remain);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    ptr += n;
    remain -= static_cast<size_t>(n);
  }
  close(fd);
}

static void wlr_source_cancelled(void* user_data, zwlr_data_control_source_v1* src) {
  zwlr_data_control_source_v1_destroy(src);
  delete static_cast<std::string*>(user_data);
}

static constexpr zwlr_data_control_source_v1_listener kWlrSourceListener_ = {
    .send = wlr_source_send,
    .cancelled = wlr_source_cancelled,
};

ClipboardService::~ClipboardService() { cleanup(); }

bool ClipboardService::bind_ext(void* ext_data_control_mgr, wl_seat* seat, wl_display* display) {
  HS_LOG("ClipboardService::bind_ext mgr=%p seat=%p", ext_data_control_mgr, (void*)seat);
  if (!ext_data_control_mgr || !seat || !display) { HS_LOG("ClipboardService::bind_ext: invalid args"); return false; }
  cleanup();
  manager_ = ext_data_control_mgr;
  seat_ = seat;
  display_ = display;
  is_ext_ = true;

  device_ = ext_data_control_manager_v1_get_data_device(
      static_cast<ext_data_control_manager_v1*>(manager_), seat_);
  available_ = (device_ != nullptr);
  return available_;
}

bool ClipboardService::bind_wlr(void* wlr_data_control_mgr, wl_seat* seat, wl_display* display) {
  HS_LOG("ClipboardService::bind_wlr mgr=%p seat=%p", wlr_data_control_mgr, (void*)seat);
  if (!wlr_data_control_mgr || !seat || !display) { HS_LOG("ClipboardService::bind_wlr: invalid args"); return false; }
  cleanup();
  manager_ = wlr_data_control_mgr;
  seat_ = seat;
  display_ = display;
  is_ext_ = false;

  device_ = zwlr_data_control_manager_v1_get_data_device(
      static_cast<zwlr_data_control_manager_v1*>(manager_), seat_);
  available_ = (device_ != nullptr);
  return available_;
}

void ClipboardService::cleanup() {
  HS_LOG("ClipboardService::cleanup");
  if (source_) {
    if (is_ext_) {
      ext_data_control_source_v1_destroy(static_cast<ext_data_control_source_v1*>(source_));
    } else {
      zwlr_data_control_source_v1_destroy(static_cast<zwlr_data_control_source_v1*>(source_));
    }
    source_ = nullptr;
  }
  if (device_) {
    if (is_ext_) {
      ext_data_control_device_v1_destroy(static_cast<ext_data_control_device_v1*>(device_));
    } else {
      zwlr_data_control_device_v1_destroy(static_cast<zwlr_data_control_device_v1*>(device_));
    }
    device_ = nullptr;
  }
  manager_ = nullptr;
  seat_ = nullptr;
  display_ = nullptr;
  available_ = false;
}

bool ClipboardService::is_available() const noexcept {
  return available_;
}

bool ClipboardService::copy_data(std::string mime_type, std::string data) {
  HS_LOG("ClipboardService::copy_data mime_type='%s' data.size=%zu", mime_type.c_str(), data.size());
  if (!available_ || !device_) { HS_LOG("ClipboardService::copy_data: not available"); return false; }

  if (source_) {
    if (is_ext_) {
      ext_data_control_source_v1_destroy(static_cast<ext_data_control_source_v1*>(source_));
    } else {
      zwlr_data_control_source_v1_destroy(static_cast<zwlr_data_control_source_v1*>(source_));
    }
    source_ = nullptr;
  }

  auto* heap_data = new std::string(std::move(data));

  if (is_ext_) {
    auto* src = ext_data_control_manager_v1_create_data_source(
        static_cast<ext_data_control_manager_v1*>(manager_));
    if (!src) {
      delete heap_data;
      return false;
    }
    ext_data_control_source_v1_offer(src, mime_type.c_str());
    ext_data_control_source_v1_add_listener(src, &kExtSourceListener_, heap_data);
    ext_data_control_device_v1_set_selection(static_cast<ext_data_control_device_v1*>(device_), src);
    source_ = src;
  } else {
    auto* src = zwlr_data_control_manager_v1_create_data_source(
        static_cast<zwlr_data_control_manager_v1*>(manager_));
    if (!src) {
      delete heap_data;
      return false;
    }
    zwlr_data_control_source_v1_offer(src, mime_type.c_str());
    zwlr_data_control_source_v1_add_listener(src, &kWlrSourceListener_, heap_data);
    zwlr_data_control_device_v1_set_selection(static_cast<zwlr_data_control_device_v1*>(device_), src);
    source_ = src;
  }

  if (display_) wl_display_flush(display_);

  return true;
}

}
