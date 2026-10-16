#pragma once

#include "desktop_shell/notifications/core/notifications.hpp"
#include "desktop_shell/notifications/types/notifications_ipc.hpp"

#include <string>
#include <utility>

namespace eh::notify {

inline eh::shell::notifications::NotificationHub*& instancePtr() {
  static eh::shell::notifications::NotificationHub* ptr = nullptr;
  return ptr;
}

inline void setInstance(eh::shell::notifications::NotificationHub* manager) { instancePtr() = manager; }

inline eh::shell::notifications::NotificationHub* instance() { return instancePtr(); }

inline void error(std::string app, std::string title, std::string body) {
  if (auto* m = instancePtr()) {
    (void)m->postInternal(std::move(app), std::move(title), std::move(body), eh::shell::notifications::AlertLevel::Critical);
    return;
  }
  push_internal(app, title, body, static_cast<int>(eh::shell::notifications::AlertLevel::Critical));
}

inline void info(std::string app, std::string title, std::string body) {
  if (auto* m = instancePtr()) {
    (void)m->postInternal(std::move(app), std::move(title), std::move(body));
    return;
  }
  push_internal(app, title, body, static_cast<int>(eh::shell::notifications::AlertLevel::Normal));
}

}
