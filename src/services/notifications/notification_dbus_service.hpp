#pragma once

#include "desktop_shell/notifications/core/notifications.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace eh::shell::notifications {

class NotificationDbusService {
public:
  explicit NotificationDbusService(NotificationManager& manager);
  ~NotificationDbusService();

  NotificationDbusService(const NotificationDbusService&) = delete;
  NotificationDbusService& operator=(const NotificationDbusService&) = delete;
  NotificationDbusService(NotificationDbusService&&) = delete;
  NotificationDbusService& operator=(NotificationDbusService&&) = delete;

  void processExpired();

  void shellDismiss(std::uint32_t id);
  void shellDismissAll();

private:
  std::uint32_t onNotify(const std::string& app_name, std::uint32_t replaces_id, const std::string& app_icon,
                         const std::string& summary, const std::string& body, const std::vector<std::string>& actions,
                         const std::map<std::string, sdbus::Variant>& hints, std::int32_t expire_timeout);

  void onCloseNotification(std::uint32_t id);
  void emitClose(std::uint32_t id, CloseReason reason);

  void onInvokeAction(std::uint32_t id, const std::string& action_key);
  void emitActionInvoked(std::uint32_t id, const std::string& action_key);

  std::vector<std::map<std::string, sdbus::Variant>> onGetNotifications();

  std::vector<std::string> onGetCapabilities();

  std::tuple<std::string, std::string, std::string, std::string> onGetServerInformation();

  NotificationManager& manager_;
  std::unique_ptr<sdbus::IConnection> bus_;
  std::unique_ptr<sdbus::IObject> object_;
};

}
