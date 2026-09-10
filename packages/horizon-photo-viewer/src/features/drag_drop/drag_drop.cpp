#include "core/viewer/app.hpp"

#include <cstdlib>
#include <string_view>
#include <unistd.h>

namespace {

void data_offer_handle_offer(void* data, wl_data_offer* /*offer*/, const char* mime_type) {
    auto* mime_types = static_cast<std::vector<std::string>*>(data);
    mime_types->emplace_back(mime_type);
}

void data_offer_handle_source_actions(void* /*data*/, wl_data_offer* /*offer*/,
                                       uint32_t /*source_actions*/) {}

void data_offer_handle_action(void* /*data*/, wl_data_offer* /*offer*/,
                               uint32_t /*dnd_action*/) {}

constexpr wl_data_offer_listener data_offer_listener_ = {
    .offer = data_offer_handle_offer,
    .source_actions = data_offer_handle_source_actions,
    .action = data_offer_handle_action,
};

}

namespace hpv {

void App::handle_data_offer(void* data, wl_data_device* /*device*/, wl_data_offer* offer) {
    auto& self = *static_cast<App*>(data);
    self.drag_offer_ = offer;
    self.drag_mime_types_.clear();
    wl_data_offer_add_listener(offer, &data_offer_listener_, &self.drag_mime_types_);
}

void App::handle_data_enter(void* data, wl_data_device* /*device*/, uint32_t serial,
                             wl_surface* /*surface*/, wl_fixed_t /*x*/, wl_fixed_t /*y*/,
                             wl_data_offer* /*offer*/) {
    auto& self = *static_cast<App*>(data);
    bool has_uris = false;
    for (auto& mt : self.drag_mime_types_) {
        if (mt == "text/uri-list") { has_uris = true; break; }
    }
    if (has_uris && self.drag_offer_) {
        wl_data_offer_accept(self.drag_offer_, serial, "text/uri-list");
    }
}

void App::handle_data_leave(void* /*data*/, wl_data_device* /*device*/) {}

void App::handle_data_motion(void* data, wl_data_device* /*device*/,
                              uint32_t /*time*/, wl_fixed_t /*x*/, wl_fixed_t /*y*/) {
    auto& self = *static_cast<App*>(data);
    if (self.drag_offer_) {
        wl_data_offer_set_actions(self.drag_offer_,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
}

void App::handle_data_drop(void* data, wl_data_device* /*device*/) {
    auto& self = *static_cast<App*>(data);
    if (!self.drag_offer_) return;
    // Accept the drop
    int fds[2];
    if (pipe(fds) < 0) return;
    wl_data_offer_receive(self.drag_offer_, "text/uri-list", fds[1]);
    close(fds[1]);
    wl_data_offer_finish(self.drag_offer_);
    wl_data_offer_destroy(self.drag_offer_);
    self.drag_offer_ = nullptr;
    wl_display_flush(self.conn_.display());

    // Read URIs from the pipe
    char buf[4096];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (n <= 0) return;
    buf[n] = '\0';
    // Parse first file:// URI
    std::string uri;
    char* p = buf;
    while (*p == '\r' || *p == '\n') p++;
    if (std::string_view(p).starts_with("file://")) {
        p += 7;
        while (*p && *p != '\r' && *p != '\n') uri += *p++;
        // URL-decode
        std::string path;
        for (size_t i = 0; i < uri.size(); i++) {
            if (uri[i] == '%' && i + 2 < uri.size()) {
                char hex[3] = {uri[i+1], uri[i+2], '\0'};
                path += (char)std::strtoul(hex, nullptr, 16);
                i += 2;
            } else if (uri[i] == '+') {
                path += ' ';
            } else {
                path += uri[i];
            }
        }
        if (!path.empty()) self.open_file(path);
    }
}

void App::handle_data_selection(void* /*data*/, wl_data_device* /*device*/,
                                 wl_data_offer* /*offer*/) {}

void App::on_data_drop(uint32_t /*serial*/, wl_data_offer* /*offer*/) {}

}
