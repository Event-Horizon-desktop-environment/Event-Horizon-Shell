#pragma once

#include "desktop_shell/notifications/core/notifications.hpp"
#include "desktop_shell/notifications/types/notifications_ipc.hpp"

#include <string>
#include <utility>

namespace eh::notify {

inline eh::shell::notifications::NotificationManager*& instancePtr() {
  static eh::shell::notifications::NotificationManager* ptr = nullptr;
  return ptr;
}

inline void setInstance(eh::shell::notifications::NotificationManager* manager) { instancePtr() = manager; }

inline eh::shell::notifications::NotificationManager* instance() { return instancePtr(); }

inline void error(std::string app, std::string title, std::string body) {
  if (auto* m = instancePtr()) {
    (void)m->addInternal(std::move(app), std::move(title), std::move(body), eh::shell::notifications::Urgency::Critical);
    return;
  }
  push_internal(app, title, body, static_cast<int>(eh::shell::notifications::Urgency::Critical));
}

inline void info(std::string app, std::string title, std::string body) {
  if (auto* m = instancePtr()) {
    (void)m->addInternal(std::move(app), std::move(title), std::move(body));
    return;
  }
  push_internal(app, title, body, static_cast<int>(eh::shell::notifications::Urgency::Normal));
}

}
