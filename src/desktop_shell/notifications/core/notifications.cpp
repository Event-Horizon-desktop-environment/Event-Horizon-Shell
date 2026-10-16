#include "desktop_shell/notifications/core/notifications.hpp"

#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <ranges>

namespace eh::shell::notifications {

namespace {

constexpr auto kImplicitDuplicateWindow = std::chrono::seconds(1);

std::optional<TimeMark> schedule_expiry(TimeMark now, std::int32_t timeout_ms) noexcept {
   
  if (timeout_ms > 0) {
    return now + std::chrono::milliseconds(timeout_ms);
  }
  return std::nullopt;
}

// Drops the history slot registered for `id`, if one exists.
template <typename Store, typename SlotMap>
void forget_slot(Store& store, SlotMap& slots, std::uint32_t id) {
  const auto found = slots.find(id);
  if (found == slots.end()) { return; }
  store.erase(store.begin() + static_cast<std::ptrdiff_t>(found->second));
}

std::optional<int> ms_until(std::optional<TimeMark> const& deadline, TimeMark now) {
  if (!deadline.has_value()) { return std::nullopt; }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
  return static_cast<int>(std::max<long long>(0, ms));
}

bool has_same_content(const Notification& notification, const std::string& appName, const std::string& summary,
                      const std::string& body) {
   
  return notification.appName == appName && notification.summary == summary && notification.body == body;
}

}

void NotificationHub::reindexHistory() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  history_index_.clear();
  for (std::size_t position = 0; auto const& entry : history_) {
    history_index_.insert_or_assign(entry.notification.id, position++);
  }
}

void NotificationHub::recordHistory(const Notification& notification, bool active,
                                        std::optional<DismissReason> close_reason) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  forget_slot(history_, history_index_, notification.id);

  Notification stripped = notification;
  stripped.picture.reset();

  history_.push_back(HistoryRecord{
      .notification = std::move(stripped),
      .active = active,
      .closeReason = close_reason,
      .eventSerial = ++change_serial_,
  });

  constexpr std::size_t kMaxHistoryEntries = 100;
  while (history_.size() > kMaxHistoryEntries) {
    history_.pop_front();
  }

  reindexHistory();
}

int NotificationHub::subscribeEvents(EventHook callback) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  event_callbacks_.emplace_back(next_callback_token_++, std::move(callback));
  return next_callback_token_ - 1;
}

void NotificationHub::removeEventCallback(int token) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto retired = [token](const auto& registered) { return registered.first == token; };
  const auto firstDead = std::remove_if(event_callbacks_.begin(), event_callbacks_.end(), retired);
  event_callbacks_.erase(firstDead, event_callbacks_.end());
}

std::uint32_t NotificationHub::addOrReplace(std::uint32_t replaces_id, std::string app_name, std::string summary,
                                                std::string body, AlertLevel urgency, std::int32_t timeout,
                                                NoticeSource origin, std::vector<std::string> actions,
                                                std::optional<std::string> icon,
                                                std::optional<NotificationPicture> image_data,
                                                std::optional<std::string> category,
                                                std::optional<std::string> desktop_entry, bool silent,
                                                bool transient) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  const auto now = Clock::now();

  if (replaces_id != 0) {
    if (const auto it = id_to_index_.find(replaces_id); it != id_to_index_.end()) {
      auto& n = notifications_[it->second];

      const bool changed =
          (n.appName != app_name || n.summary != summary || n.body != body || n.timeout != timeout ||
           n.urgency != urgency || n.origin != origin || n.actions != actions || n.icon != icon ||
           n.picture != image_data || n.category != category || n.desktopEntry != desktop_entry);

      n.origin = origin;
      n.appName = std::move(app_name);
      n.summary = std::move(summary);
      n.body = std::move(body);
      n.timeout = timeout;
      n.urgency = urgency;
      n.actions = std::move(actions);
      n.icon = std::move(icon);
      n.picture = std::move(image_data);
      n.category = std::move(category);
      n.desktopEntry = std::move(desktop_entry);
      n.silent = silent;
      n.transient = transient;
      // Identical re-pushes (e.g. the same track re-emitted by a second
      // mpris host) are true no-ops: the expiry clock must NOT restart, or
      // the toast's progress bar visibly jumps backwards (flicker).
      if (changed) {
        n.receivedTime = now;
        n.expiryTime = schedule_expiry(now, timeout);
      }

      recordHistory(n, true, std::nullopt);

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
      existing.picture = std::move(image_data);
      existing.category = std::move(category);
      existing.desktopEntry = std::move(desktop_entry);
      existing.receivedTime = now;
      existing.expiryTime = schedule_expiry(now, timeout);

      recordHistory(existing, true, std::nullopt);

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
      .picture = std::move(image_data),
      .category = std::move(category),
      .desktopEntry = std::move(desktop_entry),
      .receivedTime = now,
      .expiryTime = schedule_expiry(now, timeout),
      .silent = silent,
      .transient = transient,
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
  recordHistory(n, true, std::nullopt);

  eh::shell_log::notifications("added id=", n.id, " app=\"", n.appName, "\" summary=\"", n.summary, "\" has_image=", n.picture.has_value(), " timeout=", n.timeout, " urgency=", static_cast<int>(n.urgency));

  for (auto& [token, cb] : event_callbacks_) {
    (void)token;
    cb(n, NotificationEvent::Added);
  }

  return n.id;
}

std::uint32_t NotificationHub::postInternal(std::string app_name, std::string summary, std::string body,
                                                 AlertLevel urgency, std::int32_t timeout,
                                                 std::optional<std::string> icon,
                                                 std::optional<NotificationPicture> image_data,
                                                 std::optional<std::string> category,
                                                 std::optional<std::string> desktop_entry) {
   
  return addOrReplace(0, std::move(app_name), std::move(summary), std::move(body), urgency, timeout,
                      NoticeSource::Internal, {}, std::move(icon), std::move(image_data), std::move(category),
                      std::move(desktop_entry));
}

void NotificationHub::setActionHook(ActionHook callback) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  action_invoke_callback_.swap(callback);
}

bool NotificationHub::triggerAction(std::uint32_t id, const std::string& actionKey, bool close_after_invoke) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
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
    (void)close(id, DismissReason::Dismissed);
  }
  return true;
}

bool NotificationHub::close(std::uint32_t id, DismissReason reason) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    eh::shell_log::notif_verbose("close: id=", id, " not found");
    return false;
  }

  const std::size_t index = it->second;
  Notification& in_slot = notifications_[index];
  in_slot.picture.reset();
  const Notification closed = std::move(in_slot);

  eh::shell_log::notifications("closed id=", id, " app=\"", closed.appName, "\" reason=", static_cast<int>(reason));

  recordHistory(closed, false, reason);

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

std::deque<Notification> NotificationHub::all() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return notifications_;
}

std::deque<HistoryRecord> NotificationHub::history() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return history_;
}

std::uint64_t NotificationHub::changeSerial() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return change_serial_;
}

void NotificationHub::dropHistoryEntry(std::uint32_t id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto it = history_index_.find(id);
  if (it == history_index_.end()) {
    return;
  }

  history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(it->second));
  ++change_serial_;
  reindexHistory();
}

void NotificationHub::wipeHistory() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  if (history_.empty()) {
    return;
  }

  history_.clear();
  history_index_.clear();
  ++change_serial_;
}

std::vector<std::uint32_t> NotificationHub::expiredList() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto now = Clock::now();
  auto due = notifications_ |
                   std::views::filter([&](const auto& n) { return n.expiryTime && now >= *n.expiryTime; }) |
                   std::views::transform([](const auto& n) { return n.id; });
  return std::vector<std::uint32_t>(due.begin(), due.end());
}

int NotificationHub::nextExpiryDelayMs() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  int soonest = -1;
  const auto now = Clock::now();
  for (const auto& n : notifications_) {
    const auto candidate = ms_until(n.expiryTime, now);
    if (candidate && (soonest < 0 || *candidate < soonest)) {
      soonest = *candidate;
    }
  }
  return soonest;
}

void NotificationHub::sweepExpired() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto ids = expiredList();
  if (!ids.empty()) {
    eh::shell_log::notif_verbose("sweepExpired: ", ids.size(), " expired");
  }
  for (const std::uint32_t id : ids) {
    (void)close(id, DismissReason::Expired);
  }
}

void NotificationHub::freezeExpiry(std::uint32_t id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return;
  }
  notifications_[it->second].expiryTime.reset();
}

void NotificationHub::thawExpiry(std::uint32_t id, std::int32_t remaining_ms) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
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

void NotificationHub::setQuietMode(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  const bool flipped = do_not_disturb_ != enabled;
  do_not_disturb_ = enabled;
  if (flipped && state_callback_) {
    state_callback_();
  }
}

bool NotificationHub::doNotDisturb() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return do_not_disturb_;
}

bool NotificationHub::flipQuietMode() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  setQuietMode(!do_not_disturb_);
  return doNotDisturb();
}

void NotificationHub::setStateHandler(StateHandler callback) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  state_callback_.swap(callback);
}

void NotificationHub::setServerDefaultTimeoutMs(std::int32_t ms) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
   
  if (ms < 1000) {
    ms = kDefaultNotificationTimeoutMs;
  }
  server_default_timeout_ms_ = std::clamp(ms, static_cast<std::int32_t>(1000), static_cast<std::int32_t>(600000));
}

std::int32_t NotificationHub::serverDefaultTimeoutMs() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return server_default_timeout_ms_;
}

}
