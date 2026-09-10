#include "dbus/portal/portal_file_dialog.hpp"

#include <dbus/dbus.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace hpv {

PortalFileDialog::PortalFileDialog() = default;

PortalFileDialog::~PortalFileDialog() {
    pending_callback_ = nullptr;
    if (conn_) {
        dbus_connection_flush(conn_);
        // Don't close shared connections from dbus_bus_get()
        dbus_connection_unref(conn_);
    }
}

bool PortalFileDialog::init() {
    DBusError err;
    dbus_error_init(&err);

    conn_ = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn_) {
        std::cerr << "dbus: failed to connect to session bus: " << err.message << "\n";
        dbus_error_free(&err);
        return false;
    }

    // Request a well-known name so we can receive signals
    dbus_bus_request_name(conn_, "com.horizon.PhotoViewer",
                          DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        // Non-fatal: we can still communicate
        dbus_error_free(&err);
    }

    std::cout << "dbus: connected to session bus\n";
    return true;
}

void PortalFileDialog::save_file(const std::string& parent_window_id,
                                  const std::string& suggested_name,
                                  const std::string& suggested_folder,
                                  Callback callback) {
    if (!conn_) {
        if (callback) callback("");
        return;
    }

    pending_callback_ = nullptr;
    pending_handle_.clear();

    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser",
        "SaveFile"
    );
    if (!msg) return;

    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window_id);

    const char* title = "Save Image";
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);

    // Options dict
    DBusMessageIter dict_iter;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

    // current_name
    if (!suggested_name.empty()) {
        DBusMessageIter entry_iter, variant_iter;
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        const char* key = "current_name";
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
        const char* name = suggested_name.c_str();
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &name);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
    }

    // current_folder
    if (!suggested_folder.empty()) {
        DBusMessageIter entry_iter, variant_iter;
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        const char* key = "current_folder";
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
        const char* folder = suggested_folder.c_str();
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &folder);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
    }

    dbus_message_iter_close_container(&iter, &dict_iter);

    DBusPendingCall* pending = nullptr;
    if (!dbus_connection_send_with_reply(conn_, msg, &pending, -1)) {
        dbus_message_unref(msg);
        return;
    }
    dbus_message_unref(msg);
    if (!pending) return;

    dbus_pending_call_block(pending);

    DBusMessage* reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    if (!reply) {
        if (callback) callback("");
        return;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        DBusError err;
        dbus_error_init(&err);
        dbus_set_error_from_message(&err, reply);
        std::cerr << "portal: SaveFile error: " << err.message << "\n";
        dbus_error_free(&err);
        dbus_message_unref(reply);
        if (callback) callback("");
        return;
    }

    const char* handle_path = nullptr;
    DBusMessageIter reply_iter;
    dbus_message_iter_init(reply, &reply_iter);
    dbus_message_iter_get_basic(&reply_iter, &handle_path);
    dbus_message_unref(reply);

    if (!handle_path) {
        if (callback) callback("");
        return;
    }

    pending_handle_ = handle_path;
    pending_callback_ = std::move(callback);
    parent_window_id_ = parent_window_id;

    std::cout << "portal: save dialog opened, handle=" << handle_path << "\n";

    std::string match_rule = "interface='org.freedesktop.portal.Request',"
                             "member='Response',"
                             "path='" + pending_handle_ + "'";
    DBusError match_err;
    dbus_error_init(&match_err);
    dbus_bus_add_match(conn_, match_rule.c_str(), &match_err);
    if (dbus_error_is_set(&match_err)) {
        std::cerr << "portal: add_match error: " << match_err.message << "\n";
        dbus_error_free(&match_err);
    }
}

void PortalFileDialog::open_file(const std::string& parent_window_id, Callback callback,
                                  const std::vector<std::string>& mime_types) {
    if (!conn_) {
        if (callback) callback("");
        return;
    }

    // Cancel any pending request
    pending_callback_ = nullptr;
    pending_handle_.clear();

    // Build the OpenFile method call
    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser",
        "OpenFile"
    );
    if (!msg) return;

    // Parent window (Wayland handle: "wayland:wl_surface@NN")
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window_id);

    // Title
    const char* title = "Open Image";
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);

    // Options dict
    DBusMessageIter dict_iter;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);

    // filters — a(sa(us)): each filter is (display_name, [(type, pattern)...])
    if (!mime_types.empty()) {
        DBusMessageIter entry_iter, variant_iter, filter_array_iter;
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        const char* key = "filters";
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);

        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "a(sa(us))", &variant_iter);
        dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY, "(sa(us))", &filter_array_iter);

        // Single filter group: "Images"
        DBusMessageIter group_struct_iter, filter_list_iter, filter_entry_iter;
        dbus_message_iter_open_container(&filter_array_iter, DBUS_TYPE_STRUCT, NULL, &group_struct_iter);
        const char* filter_name = "Images";
        dbus_message_iter_append_basic(&group_struct_iter, DBUS_TYPE_STRING, &filter_name);

        // Array of (uint32 type, string pattern) — type 1 = MIME type
        dbus_message_iter_open_container(&group_struct_iter, DBUS_TYPE_ARRAY, "(us)", &filter_list_iter);
        for (const auto& mt : mime_types) {
            dbus_message_iter_open_container(&filter_list_iter, DBUS_TYPE_STRUCT, NULL, &filter_entry_iter);
            uint32_t mime_type_tag = 1;
            dbus_message_iter_append_basic(&filter_entry_iter, DBUS_TYPE_UINT32, &mime_type_tag);
            const char* mime = mt.c_str();
            dbus_message_iter_append_basic(&filter_entry_iter, DBUS_TYPE_STRING, &mime);
            dbus_message_iter_close_container(&filter_list_iter, &filter_entry_iter);
        }
        dbus_message_iter_close_container(&group_struct_iter, &filter_list_iter);

        dbus_message_iter_close_container(&filter_array_iter, &group_struct_iter);
        dbus_message_iter_close_container(&variant_iter, &filter_array_iter);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
    }

    dbus_message_iter_close_container(&iter, &dict_iter);

    // Send the method call
    DBusPendingCall* pending = nullptr;
    if (!dbus_connection_send_with_reply(conn_, msg, &pending, -1)) {
        dbus_message_unref(msg);
        return;
    }
    dbus_message_unref(msg);
    if (!pending) return;

    // Wait for reply (we use a timeout since we're in the poll loop)
    dbus_pending_call_block(pending);

    // Process the reply
    DBusMessage* reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    if (!reply) {
        std::cerr << "portal: no reply from OpenFile\n";
        if (callback) callback("");
        return;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        DBusError err;
        dbus_error_init(&err);
        dbus_set_error_from_message(&err, reply);
        std::cerr << "portal: OpenFile error: " << err.message << "\n";
        dbus_error_free(&err);
        dbus_message_unref(reply);
        if (callback) callback("");
        return;
    }

    // Parse the reply — we get a request handle path
    const char* handle_path = nullptr;
    DBusMessageIter reply_iter;
    dbus_message_iter_init(reply, &reply_iter);
    // The reply is a tuple (request_handle)
    dbus_message_iter_get_basic(&reply_iter, &handle_path);
    dbus_message_unref(reply);

    if (!handle_path) {
        std::cerr << "portal: no handle path in reply\n";
        if (callback) callback("");
        return;
    }

    pending_handle_ = handle_path;
    pending_callback_ = std::move(callback);
    parent_window_id_ = parent_window_id;

    std::cout << "portal: file dialog opened, handle=" << handle_path << "\n";

    // Register match rule for the Response signal
    // The portal will send a Response signal on the request path
    // Format: org.freedesktop.portal.Request.Response
    std::string match_rule = "interface='org.freedesktop.portal.Request',"
                             "member='Response',"
                             "path='" + pending_handle_ + "'";
    DBusError match_err;
    dbus_error_init(&match_err);
    dbus_bus_add_match(conn_, match_rule.c_str(), &match_err);
    if (dbus_error_is_set(&match_err)) {
        std::cerr << "portal: add_match error: " << match_err.message << "\n";
        dbus_error_free(&match_err);
    }
}

int PortalFileDialog::get_fd() const {
    if (!conn_) return -1;
    int fd = -1;
    if (!dbus_connection_get_socket(conn_, &fd)) return -1;
    return fd;
}

bool PortalFileDialog::dispatch() {
    if (!conn_) return false;

    // Read any pending data (non-blocking)
    dbus_connection_read_write(conn_, 0);

    // Dispatch pending messages
    while (dbus_connection_get_dispatch_status(conn_) == DBUS_DISPATCH_DATA_REMAINS) {
        DBusMessage* msg = dbus_connection_pop_message(conn_);
        if (!msg) continue;

        // Check if this is a Response signal for our pending request
        if (pending_callback_ && !pending_handle_.empty() &&
            dbus_message_is_signal(msg, "org.freedesktop.portal.Request", "Response")) {

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);

            uint32_t response_code = 0;
            dbus_message_iter_get_basic(&iter, &response_code);
            dbus_message_iter_next(&iter);

            // Results array (dict)
            std::string selected_uri;
            DBusMessageIter dict_iter, variant_iter, array_iter;
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
                dbus_message_iter_recurse(&iter, &dict_iter);
                while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter entry_iter;
                    dbus_message_iter_recurse(&dict_iter, &entry_iter);

                    const char* key = nullptr;
                    dbus_message_iter_get_basic(&entry_iter, &key);
                    dbus_message_iter_next(&entry_iter);

                    if (strcmp(key, "uris") == 0) {
                        // Variant containing array of strings
                        if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_VARIANT) {
                            dbus_message_iter_recurse(&entry_iter, &variant_iter);
                            if (dbus_message_iter_get_arg_type(&variant_iter) == DBUS_TYPE_ARRAY) {
                                dbus_message_iter_recurse(&variant_iter, &array_iter);
                                while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
                                    const char* uri = nullptr;
                                    dbus_message_iter_get_basic(&array_iter, &uri);
                                    selected_uri = uri;
                                    // We take the first URI
                                    break;
                                }
                            }
                        }
                    }
                    dbus_message_iter_next(&dict_iter);
                }
            }

            // Convert file:// URI to path
            std::string file_path;
            if (!selected_uri.empty()) {
                if (selected_uri.compare(0, 7, "file://") == 0) {
                    file_path = selected_uri.substr(7);
                    // URL-decode %20 etc.
                } else {
                    file_path = selected_uri;
                }
            }

            if (response_code == 0 && !file_path.empty()) {
                std::cout << "portal: selected file: " << file_path << "\n";
                auto cb = std::move(pending_callback_);
                pending_callback_ = nullptr;
                pending_handle_.clear();
                if (cb) cb(file_path);
            } else if (response_code != 0) {
                std::cout << "portal: dialog cancelled\n";
                auto cb = std::move(pending_callback_);
                pending_callback_ = nullptr;
                pending_handle_.clear();
                if (cb) cb("");
            }
        }

        dbus_message_unref(msg);
    }

    return true;
}

}
