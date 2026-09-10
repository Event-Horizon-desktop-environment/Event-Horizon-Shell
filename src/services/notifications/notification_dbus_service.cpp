#include "services/notifications/notification_dbus_service.hpp"

#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <tuple>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/VTableItems.h>

#ifndef EH_SHELL_VERSION
#define EH_SHELL_VERSION "0.9.5-beta1"
#endif

namespace eh::shell::notifications {

namespace {

const sdbus::ServiceName kBusName{"org.freedesktop.Notifications"};
const sdbus::ObjectPath kObjectPath{"/org/freedesktop/Notifications"};
constexpr auto kInterface = "org.freedesktop.Notifications";

constexpr std::size_t kMaxStringLen = 1024;

std::string clamp_str(std::string_view s) {
   
  const auto len = std::min(s.size(), kMaxStringLen);
  return std::string{s.substr(0, len)};
}

bool is_blank_text(std::string_view text) {
   
  return text.empty() ||
         std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
}

void ascii_lower_inplace(std::string& s) {
   
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
}

std::string sanitize_markup(std::string_view s) {
   
  std::string out;
  out.reserve(s.size());

  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '<') {
      const std::size_t close = s.find('>', i + 1);
      if (close != std::string_view::npos) {
        std::string tag{s.substr(i + 1, close - i - 1)};
        ascii_lower_inplace(tag);
        if (tag == "br" || tag == "br/" || tag == "br /") {
          out += '\n';
        }
        i = close + 1;
        continue;
      }
    }

    if (s[i] == '&') {
      const std::string_view rest = s.substr(i);
      if (rest.substr(0, 4) == "&lt;") {
        out += '<';
        i += 4;
      } else if (rest.substr(0, 4) == "&gt;") {
        out += '>';
        i += 4;
      } else if (rest.substr(0, 5) == "&amp;") {
        out += '&';
        i += 5;
      } else if (rest.substr(0, 6) == "&quot;") {
        out += '"';
        i += 6;
      } else if (rest.substr(0, 6) == "&apos;") {
        out += '\'';
        i += 6;
      } else {
        out += s[i];
        ++i;
      }
    } else {
      out += s[i];
      ++i;
    }
  }

  return out;
}

std::vector<std::string> sanitize_actions(const std::vector<std::string>& actions) {
   
  std::vector<std::string> sanitized;
  sanitized.reserve(actions.size() - (actions.size() % 2));

  for (std::size_t i = 0; i + 1 < actions.size(); i += 2) {
    std::string action_key = clamp_str(actions[i]);
    std::string label = clamp_str(actions[i + 1]);

    if (action_key.empty()) {
      continue;
    }

    if (is_blank_text(label)) {
      label = "Action";
    }

    sanitized.push_back(std::move(action_key));
    sanitized.push_back(std::move(label));
  }

  return sanitized;
}

using NotificationImageDataStruct = sdbus::Struct<std::int32_t, std::int32_t, std::int32_t, bool, std::int32_t,
                                                  std::int32_t, std::vector<std::uint8_t>>;

std::optional<NotificationImageData> decode_image_data_variant(const sdbus::Variant& value) {
   
  try {
    const auto data = value.get<NotificationImageDataStruct>();
    NotificationImageData out;
    out.width = std::get<0>(data);
    out.height = std::get<1>(data);
    out.rowStride = std::get<2>(data);
    out.hasAlpha = std::get<3>(data);
    out.bitsPerSample = std::get<4>(data);
    out.channels = std::get<5>(data);
    out.data = std::get<6>(data);
    eh::shell_log::notif_verbose("decode_image_data_variant: ", out.width, "x", out.height, " ch=", out.channels, " alpha=", out.hasAlpha);
    return out;
  } catch (const sdbus::Error& e) {
    eh::shell_log::notif_verbose("decode_image_data_variant: struct decode failed: ", e.what());
  }

  try {
    const auto data = value.get<std::tuple<std::int32_t, std::int32_t, std::int32_t, bool, std::int32_t, std::int32_t,
                                           std::vector<std::uint8_t>>>();
    NotificationImageData out;
    out.width = std::get<0>(data);
    out.height = std::get<1>(data);
    out.rowStride = std::get<2>(data);
    out.hasAlpha = std::get<3>(data);
    out.bitsPerSample = std::get<4>(data);
    out.channels = std::get<5>(data);
    out.data = std::get<6>(data);
    eh::shell_log::notif_verbose("decode_image_data_variant (tuple): ", out.width, "x", out.height, " ch=", out.channels);
    return out;
  } catch (const sdbus::Error& e) {
    eh::shell_log::notif_verbose("decode_image_data_variant: tuple decode failed: ", e.what());
  }

  return std::nullopt;
}

std::optional<NotificationImageData> decode_image_hint(const std::map<std::string, sdbus::Variant>& hints) {
   
  for (const char* key : {"image-data", "image_data", "icon_data"}) {
    const auto it = hints.find(key);
    if (it == hints.end()) {
      continue;
    }

    if (auto decoded = decode_image_data_variant(it->second)) {
      return decoded;
    }
  }

  return std::nullopt;
}

}

NotificationDbusService::NotificationDbusService(NotificationManager& manager) : manager_(manager) {
    
  bus_ = sdbus::createSessionBusConnection();

  // Check if another notification daemon already owns the bus name.
  // Claiming it would break notifications for all other applications.
  {
    auto daemonProxy = sdbus::createProxy(*bus_,
        sdbus::ServiceName{"org.freedesktop.DBus"},
        sdbus::ObjectPath{"/org/freedesktop/DBus"});

    bool alreadyRunning = false;
    try {
      daemonProxy->callMethod("NameHasOwner")
          .onInterface("org.freedesktop.DBus")
          .withArguments(std::string{"org.freedesktop.Notifications"})
          .storeResultsTo(alreadyRunning);
    } catch (const sdbus::Error&) {
    }

    if (alreadyRunning) {
      std::cerr << "[notifications] Notification daemon already running — deferring to existing provider\n";
      bus_.reset();
      return;
    }
  }

  bus_->requestName(kBusName);
  object_ = sdbus::createObject(*bus_, kObjectPath);

  object_
      ->addVTable(
          sdbus::registerMethod("Notify")
              .withInputParamNames("app_name", "replaces_id", "app_icon", "summary", "body", "actions", "hints",
                                   "expire_timeout")
              .withOutputParamNames("id")
              .implementedAs(
                  [this](const std::string& app_name, std::uint32_t replaces_id, const std::string& app_icon,
                         const std::string& summary, const std::string& body, const std::vector<std::string>& actions,
                         const std::map<std::string, sdbus::Variant>& hints, std::int32_t expire_timeout) {
                     
                    return onNotify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout);
                  }),

          sdbus::registerMethod("GetCapabilities").withOutputParamNames("capabilities").implementedAs([this]() {
            return onGetCapabilities();
          }),

          sdbus::registerMethod("GetNotifications").withOutputParamNames("notifications").implementedAs([this]() {
            return onGetNotifications();
          }),

          sdbus::registerMethod("GetServerInformation")
              .withOutputParamNames("name", "vendor", "version", "spec_version")
              .implementedAs([this]() { return onGetServerInformation(); }),

          sdbus::registerMethod("CloseNotification").withInputParamNames("id").implementedAs([this](std::uint32_t id) {
            onCloseNotification(id);
          }),

          sdbus::registerMethod("InvokeAction")
              .withInputParamNames("id", "action_key")
              .implementedAs(
                  [this](std::uint32_t id, const std::string& action_key) { onInvokeAction(id, action_key); }),

          sdbus::registerSignal("NotificationClosed").withParameters<std::uint32_t, std::uint32_t>("id", "reason"),

          sdbus::registerSignal("ActionInvoked").withParameters<std::uint32_t, std::string>("id", "action_key"))
      .forInterface(kInterface);

  manager_.setActionInvokeCallback(
      [this](std::uint32_t id, const std::string& action_key) { emitActionInvoked(id, action_key); });

  bus_->enterEventLoopAsync();
}

NotificationDbusService::~NotificationDbusService() {
   
  MANGOWM_INFO("{}", __func__);
  manager_.setActionInvokeCallback(nullptr);
}

void NotificationDbusService::shellDismiss(std::uint32_t id) {
   
  if (!manager_.close(id, CloseReason::Dismissed)) {
    return;
  }
  emitClose(id, CloseReason::Dismissed);
}

void NotificationDbusService::shellDismissAll() {
   
  const auto snapshot = manager_.all();
  std::vector<std::uint32_t> ids;
  ids.reserve(snapshot.size());
  for (const auto& n : snapshot) {
    ids.push_back(n.id);
  }
  for (const std::uint32_t id : ids) {
    shellDismiss(id);
  }
}

void NotificationDbusService::processExpired() {
   
  for (const std::uint32_t id : manager_.expiredIds()) {
    emitClose(id, CloseReason::Expired);
    (void)manager_.close(id, CloseReason::Expired);
  }
}

std::uint32_t NotificationDbusService::onNotify(const std::string& app_name, std::uint32_t replaces_id,
                                                const std::string& app_icon, const std::string& summary,
                                                const std::string& body, const std::vector<std::string>& actions,
                                                const std::map<std::string, sdbus::Variant>& hints,
                                                std::int32_t expire_timeout) {
   
  std::int32_t timeout = expire_timeout;
  if (timeout < 0) {
    timeout = manager_.serverDefaultTimeoutMs();
  }
  const auto sanitized_actions = sanitize_actions(actions);

  Urgency urgency = Urgency::Normal;
  if (auto it = hints.find("urgency"); it != hints.end()) {
    try {
      const std::uint8_t raw = it->second.get<std::uint8_t>();
      if (raw <= static_cast<std::uint8_t>(Urgency::Critical)) {
        urgency = static_cast<Urgency>(raw);
      }
    } catch (const sdbus::Error&) {
    }
  }

  std::optional<std::string> icon;
  if (!app_icon.empty()) {
    icon = clamp_str(app_icon);
  }
  if (auto it = hints.find("image-path"); it != hints.end()) {
    try {
      icon = clamp_str(it->second.get<std::string>());
    } catch (const sdbus::Error&) {
    }
  }
  if (auto it = hints.find("image_path"); it != hints.end()) {
    try {
      icon = clamp_str(it->second.get<std::string>());
    } catch (const sdbus::Error&) {
    }
  }

  std::optional<std::string> category;
  if (auto it = hints.find("category"); it != hints.end()) {
    try {
      category = clamp_str(it->second.get<std::string>());
    } catch (const sdbus::Error&) {
    }
  }

  std::optional<std::string> desktop_entry;
  if (auto it = hints.find("desktop-entry"); it != hints.end()) {
    try {
      desktop_entry = clamp_str(it->second.get<std::string>());
    } catch (const sdbus::Error&) {
    }
  }

  std::optional<NotificationImageData> image_data = decode_image_hint(hints);

  if (manager_.doNotDisturb()) {
    eh::shell_log::notifications("dnd active — silenced id=", replaces_id ? replaces_id : 0, " app=\"", app_name,
                                 "\" urgency=", static_cast<int>(urgency));
    return 0;
  }

  eh::shell_log::notif_verbose("onNotify: app=\"", app_name, "\" summary=\"", summary, "\" timeout=", timeout,
                               " replaces=", replaces_id, " has_image=", image_data.has_value());

  return manager_.addOrReplace(replaces_id, clamp_str(app_name), sanitize_markup(clamp_str(summary)),
                               sanitize_markup(clamp_str(body)), urgency, timeout, NotificationOrigin::External,
                               sanitized_actions, icon, image_data, category, desktop_entry);
}

std::vector<std::string> NotificationDbusService::onGetCapabilities() { return {"body", "actions"}; }

std::vector<std::map<std::string, sdbus::Variant>> NotificationDbusService::onGetNotifications() {
   
  std::vector<std::map<std::string, sdbus::Variant>> result;
  for (const auto& n : manager_.all()) {
    std::map<std::string, sdbus::Variant> notif;
    notif["id"] = sdbus::Variant(n.id);
    notif["app_name"] = sdbus::Variant(n.appName);
    notif["summary"] = sdbus::Variant(n.summary);
    notif["body"] = sdbus::Variant(n.body);
    notif["timeout"] = sdbus::Variant(n.timeout);
    notif["urgency"] = sdbus::Variant(static_cast<std::uint8_t>(n.urgency));
    notif["actions"] = sdbus::Variant(n.actions);
    notif["icon"] = sdbus::Variant(n.icon.value_or(""));
    notif["category"] = sdbus::Variant(n.category.value_or(""));
    notif["desktop_entry"] = sdbus::Variant(n.desktopEntry.value_or(""));
    result.push_back(std::move(notif));
  }
  return result;
}

void NotificationDbusService::onCloseNotification(std::uint32_t id) {
   
  eh::shell_log::notif_verbose("onCloseNotification: id=", id);
  if (!manager_.close(id, CloseReason::ClosedByCall)) {
    eh::shell_log::notifications("onCloseNotification: id=", id, " not found");
    throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Notifications.Error.NotFound"},
                       "notification id was not found");
  }
  emitClose(id, CloseReason::ClosedByCall);
}

void NotificationDbusService::emitClose(std::uint32_t id, CloseReason reason) {
   
  object_->emitSignal("NotificationClosed")
      .onInterface(kInterface)
      .withArguments(id, static_cast<std::uint32_t>(reason));
}

void NotificationDbusService::onInvokeAction(std::uint32_t id, const std::string& action_key) {
   
  const std::string sanitized_key = clamp_str(action_key);
  if (sanitized_key.empty()) {
    throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Notifications.Error.InvalidAction"},
                       "action_key must not be empty");
  }

  if (!manager_.invokeAction(id, sanitized_key, false)) {
    throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Notifications.Error.InvalidAction"},
                       "action_key is not available for this notification");
  }
}

void NotificationDbusService::emitActionInvoked(std::uint32_t id, const std::string& action_key) {
   
  object_->emitSignal("ActionInvoked").onInterface(kInterface).withArguments(id, action_key);
}

std::tuple<std::string, std::string, std::string, std::string> NotificationDbusService::onGetServerInformation() {
   
  return {"EventHorizon", "Event Horizon", EH_SHELL_VERSION, "1.2"};
}

}
