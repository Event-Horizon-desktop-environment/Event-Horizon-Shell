#include "services/notifications/notification_dbus_service.hpp"

#include "configuration/shell_config.hpp"
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
#define EH_SHELL_VERSION "0.9.7.3-beta-3"
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

using PictureTuple = sdbus::Struct<std::int32_t, std::int32_t, std::int32_t, bool, std::int32_t,
                                                  std::int32_t, std::vector<std::uint8_t>>;

std::optional<NotificationPicture> decode_image_data_variant(const sdbus::Variant& value) {
  try {
    const auto data = value.get<PictureTuple>();
    NotificationPicture out;
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
    NotificationPicture out;
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

std::optional<NotificationPicture> decode_image_hint(const std::map<std::string, sdbus::Variant>& hints) {
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

NotificationDbusService::NotificationDbusService(NotificationHub& manager) : manager_(manager) {
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

  try {
    bus_->requestName(kBusName);
  } catch (const sdbus::Error& e) {
    std::cerr << "[notifications] requestName failed (another provider won the race) — deferring\n";
    bus_.reset();
    return;
  }
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

          sdbus::registerMethod("ListNotifications").withOutputParamNames("notifications").implementedAs([this]() {
            return readNotifications();
          }),

          sdbus::registerMethod("GetServerInformation")
              .withOutputParamNames("name", "vendor", "version", "spec_version")
              .implementedAs([this]() { return onGetServerInformation(); }),

          sdbus::registerMethod("CloseNotification").withInputParamNames("id").implementedAs([this](std::uint32_t id) {
            closeNotificationById(id);
          }),

          sdbus::registerMethod("TriggerAction")
              .withInputParamNames("id", "action_key")
              .implementedAs(
                  [this](std::uint32_t id, const std::string& action_key) { fireAction(id, action_key); }),

          sdbus::registerSignal("NotificationClosed").withParameters<std::uint32_t, std::uint32_t>("id", "reason"),

          sdbus::registerSignal("ActionInvoked").withParameters<std::uint32_t, std::string>("id", "action_key"))
      .forInterface(kInterface);

  manager_.setActionHook(
      [this](std::uint32_t id, const std::string& action_key) { emitActionInvoked(id, action_key); });

  bus_->enterEventLoopAsync();
}

NotificationDbusService::~NotificationDbusService() {
  MANGOWM_INFO("{}", __func__);
  manager_.setActionHook(nullptr);
}

void NotificationDbusService::shellDismiss(std::uint32_t id) {
  if (!manager_.close(id, DismissReason::Dismissed)) {
    return;
  }
  sendClosedSignal(id, DismissReason::Dismissed);
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

void NotificationDbusService::sweepExpired() {
  for (const std::uint32_t id : manager_.expiredList()) {
    if (manager_.close(id, DismissReason::Expired)) {
      sendClosedSignal(id, DismissReason::Expired);
    }
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

  AlertLevel urgency = AlertLevel::Normal;
  if (auto it = hints.find("urgency"); it != hints.end()) {
    try {
      const std::uint8_t raw = it->second.get<std::uint8_t>();
      if (raw <= static_cast<std::uint8_t>(AlertLevel::Critical)) {
        urgency = static_cast<AlertLevel>(raw);
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

  std::optional<NotificationPicture> image_data = decode_image_hint(hints);

  const eh::config::NotificationRule* rule = nullptr;
  {
    const auto& rules = eh::config::notification_rules();
    std::string appLower = app_name;
    for (char& c : appLower) {
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    std::string entryLower;
    if (desktop_entry) {
      entryLower = *desktop_entry;
      for (char& c : entryLower) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      }
    }
    for (const auto& r : rules) {
      if (r.match.empty()) continue;
      std::string m = r.match;
      for (char& c : m) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      }
      if (appLower.find(m) != std::string::npos ||
          (!entryLower.empty() && entryLower.find(m) != std::string::npos)) {
        rule = &r;
        break;
      }
    }
  }
  bool silent = false;
  bool transient = false;
  if (rule) {
    bool urgencyOk = false;
    if (urgency == AlertLevel::Low) urgencyOk = rule->allowLow;
    else if (urgency == AlertLevel::Critical) urgencyOk = rule->allowCritical;
    else urgencyOk = rule->allowNormal;
    if (!urgencyOk) silent = true;
    if (!rule->showToast) silent = true;
    if (!rule->saveHistory) transient = true;
  }

  if (manager_.doNotDisturb() && !(rule && rule->bypassDnd)) {
    eh::shell_log::notifications("dnd active — silenced id=", replaces_id ? replaces_id : 0, " app=\"", app_name,
                                 "\" urgency=", static_cast<int>(urgency));
    return 0;
  }

  eh::shell_log::notif_verbose("onNotify: app=\"", app_name, "\" summary=\"", summary, "\" timeout=", timeout,
                               " replaces=", replaces_id, " has_image=", image_data.has_value());

  return manager_.addOrReplace(replaces_id, clamp_str(app_name), sanitize_markup(clamp_str(summary)),
                               sanitize_markup(clamp_str(body)), urgency, timeout, NoticeSource::External,
                               sanitized_actions, icon, image_data, category, desktop_entry, silent, transient);
}

std::vector<std::string> NotificationDbusService::onGetCapabilities() { return {"body", "actions"}; }

std::vector<std::map<std::string, sdbus::Variant>> NotificationDbusService::readNotifications() {
  const auto put = [](auto& dict, const char* key, auto&& value) {
    dict[key] = sdbus::Variant{std::forward<decltype(value)>(value)};
  };

  std::vector<std::map<std::string, sdbus::Variant>> result;
  for (const auto& n : manager_.all()) {
    std::map<std::string, sdbus::Variant> notif;
    put(notif, "id", n.id);
    put(notif, "app_name", n.appName);
    put(notif, "summary", n.summary);
    put(notif, "body", n.body);
    put(notif, "timeout", n.timeout);
    put(notif, "urgency", static_cast<std::uint8_t>(n.urgency));
    put(notif, "actions", n.actions);
    put(notif, "icon", n.icon.value_or(""));
    put(notif, "category", n.category.value_or(""));
    put(notif, "desktop_entry", n.desktopEntry.value_or(""));
    result.push_back(std::move(notif));
  }
  return result;
}

void NotificationDbusService::closeNotificationById(std::uint32_t id) {
  eh::shell_log::notif_verbose("closeNotificationById: id=", id);
  if (!manager_.close(id, DismissReason::ClosedByCall)) {
    eh::shell_log::notifications("closeNotificationById: id=", id, " not found");
    throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Notifications.Error.NotFound"},
                       "no notification with that id");
  }
  sendClosedSignal(id, DismissReason::ClosedByCall);
}

void NotificationDbusService::sendClosedSignal(std::uint32_t id, DismissReason reason) {
  if (!object_) return;
  object_->emitSignal("NotificationClosed")
      .onInterface(kInterface)
      .withArguments(id, static_cast<std::uint32_t>(reason));
}

void NotificationDbusService::fireAction(std::uint32_t id, const std::string& action_key) {
  const auto require = [](bool satisfied, const char* why) {
    if (!satisfied) {
      throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Notifications.Error.InvalidAction"}, why);
    }
  };

  const std::string sanitized_key = clamp_str(action_key);
  require(!sanitized_key.empty(), "an action key is required");
  require(manager_.triggerAction(id, sanitized_key, false), "this notification offers no such action");
}

void NotificationDbusService::emitActionInvoked(std::uint32_t id, const std::string& action_key) {
  if (!object_) return;
  object_->emitSignal("ActionInvoked").onInterface(kInterface).withArguments(id, action_key);
}

std::tuple<std::string, std::string, std::string, std::string> NotificationDbusService::onGetServerInformation() {
  const std::string product = "EventHorizon";
  const std::string vendor = "Event Horizon";
  const std::string spec = "1.2";
  return std::make_tuple(product, vendor, EH_SHELL_VERSION, spec);
}

}
