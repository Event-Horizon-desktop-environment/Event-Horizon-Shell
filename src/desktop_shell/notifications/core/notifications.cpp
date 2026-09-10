#include "desktop_shell/notifications/core/notifications.hpp"

#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <iterator>

namespace eh::shell::notifications {

namespace {

constexpr auto kImplicitDuplicateWindow = std::chrono::seconds(1);

std::optional<TimePoint> schedule_expiry(TimePoint now, std::int32_t timeout_ms) noexcept {
   
  if (timeout_ms > 0) {
    return now + std::chrono::milliseconds(timeout_ms);
  }
  return std::nullopt;
}

bool has_same_content(const Notification& notification, const std::string& appName, const std::string& summary,
                      const std::string& body) {
   
  return notification.appName == appName && notification.summary == summary && notification.body == body;
}

}

void NotificationManager::rebuildHistoryIndex() {
   
  history_index_.clear();
  for (std::size_t i = 0; i < history_.size(); ++i) {
    history_index_[history_[i].notification.id] = i;
  }
}

void NotificationManager::upsertHistory(const Notification& notification, bool active,
                                        std::optional<CloseReason> close_reason) {
   
  if (const auto it = history_index_.find(notification.id); it != history_index_.end()) {
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(it->second));
  }

  Notification stripped = notification;
  stripped.imageData.reset();

  history_.push_back(NotificationHistoryEntry{
      .notification = std::move(stripped),
      .active = active,
      .closeReason = close_reason,
      .eventSerial = ++change_serial_,
  });

  constexpr std::size_t kMaxHistoryEntries = 100;
  while (history_.size() > kMaxHistoryEntries) {
    history_.pop_front();
  }

  rebuildHistoryIndex();
}

int NotificationManager::addEventCallback(EventCallback callback) {
   
  const int token = next_callback_token_++;
  event_callbacks_.emplace_back(token, std::move(callback));
  return token;
}

void NotificationManager::removeEventCallback(int token) {
   
  std::erase_if(event_callbacks_, [token](const auto& pair) { return pair.first == token; });
}

std::uint32_t NotificationManager::addOrReplace(std::uint32_t replaces_id, std::string app_name, std::string summary,
                                                std::string body, Urgency urgency, std::int32_t timeout,
                                                NotificationOrigin origin, std::vector<std::string> actions,
                                                std::optional<std::string> icon,
                                                std::optional<NotificationImageData> image_data,
                                                std::optional<std::string> category,
                                                std::optional<std::string> desktop_entry) {
  const auto now = Clock::now();

  if (replaces_id != 0) {
    if (const auto it = id_to_index_.find(replaces_id); it != id_to_index_.end()) {
      auto& n = notifications_[it->second];

      const bool changed =
          (n.appName != app_name || n.summary != summary || n.body != body || n.timeout != timeout ||
           n.urgency != urgency || n.origin != origin || n.actions != actions || n.icon != icon ||
           n.imageData != image_data || n.category != category || n.desktopEntry != desktop_entry);

      n.origin = origin;
      n.appName = std::move(app_name);
      n.summary = std::move(summary);
      n.body = std::move(body);
      n.timeout = timeout;
      n.urgency = urgency;
      n.actions = std::move(actions);
      n.icon = std::move(icon);
      n.imageData = std::move(image_data);
      n.category = std::move(category);
      n.desktopEntry = std::move(desktop_entry);
      n.receivedTime = now;
      n.expiryTime = schedule_expiry(now, timeout);

      upsertHistory(n, true, std::nullopt);

      if (changed) {
        eh::shell_log::notif_verbose("addOrReplace: updated id=", replaces_id, " app=\"", n.appName, "\"");
        for (auto& [token, cb] : event_callbacks_) {
          (void)token;
          cb(n, NotificationEvent::Updated);
        }
      } else {
        eh::shell_log::notif_verbose("addOrReplace: replaced id=", replaces_id, " (unchanged)");
      }

      return n.id;
    }
  }

  for (auto it = notifications_.rbegin(); it != notifications_.rend(); ++it) {
    const auto& existing = *it;
    if (has_same_content(existing, app_name, summary, body) && now - existing.receivedTime < kImplicitDuplicateWindow) {
      eh::shell_log::notif_verbose("addOrReplace: duplicate suppressed id=", existing.id, " app=\"", app_name, "\"");
      return existing.id;
    }
  }

  // Same-app merge: if the same app already has an active notification,
  // update it in-place instead of stacking a new card.  Catches chat
  // apps (Discord, etc.) that send multiple Notify calls per message
  // without a proper replaces_id.  Uses case-insensitive appName
  // comparison and desktop-entry matching as fallback.
  for (auto it = notifications_.rbegin(); it != notifications_.rend(); ++it) {
    auto& existing = *it;
    const bool same_app = existing.appName.size() == app_name.size() &&
                          std::equal(existing.appName.begin(), existing.appName.end(),
                                     app_name.begin(), app_name.end(),
                                     [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    const bool same_entry = desktop_entry.has_value() && existing.desktopEntry.has_value() &&
                            *existing.desktopEntry == *desktop_entry;
    if (same_app || same_entry) {
      existing.origin = origin;
      existing.summary = std::move(summary);
      existing.body = std::move(body);
      existing.timeout = timeout;
      existing.urgency = urgency;
      existing.actions = std::move(actions);
      existing.icon = std::move(icon);
      existing.imageData = std::move(image_data);
      existing.category = std::move(category);
      existing.desktopEntry = std::move(desktop_entry);
      existing.receivedTime = now;
      existing.expiryTime = schedule_expiry(now, timeout);

      upsertHistory(existing, true, std::nullopt);

      eh::shell_log::notif_verbose("addOrReplace: same-app merge id=", existing.id, " app=\"", existing.appName, "\"");
      for (auto& [token, cb] : event_callbacks_) {
        (void)token;
        cb(existing, NotificationEvent::Updated);
      }
      return existing.id;
    }
  }

  const std::uint32_t id = next_id_++;
  notifications_.push_back(Notification{
      .id = id,
      .origin = origin,
      .appName = std::move(app_name),
      .summary = std::move(summary),
      .body = std::move(body),
      .timeout = timeout,
      .urgency = urgency,
      .actions = std::move(actions),
      .icon = std::move(icon),
      .imageData = std::move(image_data),
      .category = std::move(category),
      .desktopEntry = std::move(desktop_entry),
      .receivedTime = now,
      .expiryTime = schedule_expiry(now, timeout),
  });
  id_to_index_.emplace(id, notifications_.size() - 1);

  // Cap active notifications at 50 to prevent unbounded growth.
  while (notifications_.size() > 50) {
    id_to_index_.erase(notifications_.front().id);
    notifications_.pop_front();
    for (std::size_t i = 0; i < notifications_.size(); ++i)
      id_to_index_[notifications_[i].id] = i;
  }

  const auto& n = notifications_.back();
  upsertHistory(n, true, std::nullopt);

  eh::shell_log::notifications("added id=", n.id, " app=\"", n.appName, "\" summary=\"", n.summary, "\" has_image=", n.imageData.has_value(), " timeout=", n.timeout, " urgency=", static_cast<int>(n.urgency));

  for (auto& [token, cb] : event_callbacks_) {
    (void)token;
    cb(n, NotificationEvent::Added);
  }

  return n.id;
}

std::uint32_t NotificationManager::addInternal(std::string app_name, std::string summary, std::string body,
                                                 Urgency urgency, std::int32_t timeout,
                                                 std::optional<std::string> icon,
                                                 std::optional<NotificationImageData> image_data,
                                                 std::optional<std::string> category,
                                                 std::optional<std::string> desktop_entry) {
   
  return addOrReplace(0, std::move(app_name), std::move(summary), std::move(body), urgency, timeout,
                      NotificationOrigin::Internal, {}, std::move(icon), std::move(image_data), std::move(category),
                      std::move(desktop_entry));
}

void NotificationManager::setActionInvokeCallback(ActionInvokeCallback callback) {
   
  action_invoke_callback_ = std::move(callback);
}

bool NotificationManager::invokeAction(std::uint32_t id, const std::string& actionKey, bool close_after_invoke) {
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end() || actionKey.empty()) {
    return false;
  }

  const Notification& notification = notifications_[it->second];
  bool action_found = false;
  for (std::size_t i = 0; i + 1 < notification.actions.size(); i += 2) {
    if (notification.actions[i] == actionKey) {
      action_found = true;
      break;
    }
  }
  if (!action_found) {
    return false;
  }

  if (action_invoke_callback_) {
    action_invoke_callback_(id, actionKey);
  }

  if (close_after_invoke) {
    (void)close(id, CloseReason::Dismissed);
  }
  return true;
}

bool NotificationManager::close(std::uint32_t id, CloseReason reason) {
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    eh::shell_log::notif_verbose("close: id=", id, " not found");
    return false;
  }

  const std::size_t index = it->second;
  Notification& in_slot = notifications_[index];
  in_slot.imageData.reset();
  const Notification closed = std::move(in_slot);

  eh::shell_log::notifications("closed id=", id, " app=\"", closed.appName, "\" reason=", static_cast<int>(reason));

  upsertHistory(closed, false, reason);

  notifications_.erase(notifications_.begin() + static_cast<std::ptrdiff_t>(index));
  id_to_index_.erase(it);

  for (std::size_t i = index; i < notifications_.size(); ++i) {
    id_to_index_[notifications_[i].id] = i;
  }

  for (auto& [token, cb] : event_callbacks_) {
    (void)token;
    cb(closed, NotificationEvent::Closed);
  }

  return true;
}

const std::deque<Notification>& NotificationManager::all() const noexcept { return notifications_; }

const std::deque<NotificationHistoryEntry>& NotificationManager::history() const noexcept { return history_; }

std::uint64_t NotificationManager::changeSerial() const noexcept { return change_serial_; }

void NotificationManager::removeHistoryEntry(std::uint32_t id) {
   
  const auto it = history_index_.find(id);
  if (it == history_index_.end()) {
    return;
  }

  history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(it->second));
  ++change_serial_;
  rebuildHistoryIndex();
}

void NotificationManager::clearHistory() {
   
  if (history_.empty()) {
    return;
  }

  history_.clear();
  history_index_.clear();
  ++change_serial_;
}

std::vector<std::uint32_t> NotificationManager::expiredIds() const {
   
  const auto now = Clock::now();
  std::vector<std::uint32_t> ids;
  for (const auto& n : notifications_) {
    if (n.expiryTime && now >= *n.expiryTime) {
      ids.push_back(n.id);
    }
  }
  return ids;
}

int NotificationManager::nextExpiryTimeoutMs() const {
   
  int expiry_ms = -1;
  const auto now = Clock::now();
  for (const auto& n : notifications_) {
    if (n.expiryTime) {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*n.expiryTime - now).count();
      const int clamped = static_cast<int>(std::max<long long>(0, ms));
      if (expiry_ms < 0 || clamped < expiry_ms) {
        expiry_ms = clamped;
      }
    }
  }
  return expiry_ms;
}

void NotificationManager::processExpired() {
   
  const auto ids = expiredIds();
  if (!ids.empty()) {
    eh::shell_log::notif_verbose("processExpired: ", ids.size(), " expired");
  }
  for (const std::uint32_t id : ids) {
    (void)close(id, CloseReason::Expired);
  }
}

void NotificationManager::pauseExpiry(std::uint32_t id) {
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return;
  }
  notifications_[it->second].expiryTime.reset();
}

void NotificationManager::resumeExpiry(std::uint32_t id, std::int32_t remaining_ms) {
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return;
  }
  if (remaining_ms <= 0) {
    notifications_[it->second].expiryTime = Clock::now();
    return;
  }
  notifications_[it->second].expiryTime = Clock::now() + std::chrono::milliseconds(remaining_ms);
}

void NotificationManager::setDoNotDisturb(bool enabled) {
   
  if (do_not_disturb_ == enabled) {
    return;
  }
  do_not_disturb_ = enabled;
  if (state_callback_) {
    state_callback_();
  }
}

bool NotificationManager::doNotDisturb() const noexcept { return do_not_disturb_; }

bool NotificationManager::toggleDoNotDisturb() {
   
  setDoNotDisturb(!do_not_disturb_);
  return doNotDisturb();
}

void NotificationManager::setStateCallback(StateCallback callback) { state_callback_ = std::move(callback); }

void NotificationManager::setServerDefaultTimeoutMs(std::int32_t ms) {
   
  if (ms < 1000) {
    ms = kDefaultNotificationTimeoutMs;
  }
  server_default_timeout_ms_ = std::clamp(ms, static_cast<std::int32_t>(1000), static_cast<std::int32_t>(600000));
}

std::int32_t NotificationManager::serverDefaultTimeoutMs() const noexcept { return server_default_timeout_ms_; }

}
