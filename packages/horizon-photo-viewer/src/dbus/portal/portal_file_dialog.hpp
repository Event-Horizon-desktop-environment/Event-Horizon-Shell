#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

typedef struct DBusConnection DBusConnection;
typedef struct DBusWatch DBusWatch;

namespace hpv {

class PortalFileDialog {
public:
    using Callback = std::function<void(const std::string& path)>;

    PortalFileDialog();
    ~PortalFileDialog();

    PortalFileDialog(const PortalFileDialog&) = delete;
    PortalFileDialog& operator=(const PortalFileDialog&) = delete;

    bool init();
    void open_file(const std::string& parent_window_id, Callback callback,
                   const std::vector<std::string>& mime_types = {});
    void save_file(const std::string& parent_window_id,
                   const std::string& suggested_name,
                   const std::string& suggested_folder,
                   Callback callback);
    int get_fd() const;
    bool dispatch();

private:
    DBusConnection* conn_ = nullptr;
    Callback pending_callback_;
    std::string pending_handle_;
    std::string parent_window_id_;
};

}
