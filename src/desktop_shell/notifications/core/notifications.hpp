#pragma once

#include "desktop_shell/notifications/types/notification_types.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eh::shell::notifications {

class NotificationManager {
public:
  NotificationManager() = default;

  using EventCallback = std::function<void(const Notification&, NotificationEvent)>;
  using ActionInvokeCallback = std::function<void(std::uint32_t, const std::string&)>;
  using StateCallback = std::function<void()>;

  [[nodiscard]] int addEventCallback(EventCallback callback);
  void removeEventCallback(int token);

  [[nodiscard]] std::uint32_t addOrReplace(std::uint32_t replaces_id, std::string app_name, std::string summary,
                                           std::string body, Urgency urgency, std::int32_t timeout,
                                           NotificationOrigin origin = NotificationOrigin::External,
                                           std::vector<std::string> actions = {},
                                           std::optional<std::string> icon = std::nullopt,
                                           std::optional<NotificationImageData> image_data = std::nullopt,
                                           std::optional<std::string> category = std::nullopt,
                                           std::optional<std::string> desktop_entry = std::nullopt);

  [[nodiscard]] std::uint32_t addInternal(std::string app_name, std::string summary, std::string body,
                                            Urgency urgency = Urgency::Normal,
                                            std::int32_t timeout = kDefaultNotificationTimeoutMs,
                                            std::optional<std::string> icon = std::nullopt,
                                            std::optional<NotificationImageData> image_data = std::nullopt,
                                            std::optional<std::string> category = std::nullopt,
                                            std::optional<std::string> desktop_entry = std::nullopt);

  void setActionInvokeCallback(ActionInvokeCallback callback);
  [[nodiscard]] bool invokeAction(std::uint32_t id, const std::string& actionKey, bool close_after_invoke = true);

  [[nodiscard]] bool close(std::uint32_t id, CloseReason reason = CloseReason::ClosedByCall);

  [[nodiscard]] std::vector<std::uint32_t> expiredIds() const;
  [[nodiscard]] int nextExpiryTimeoutMs() const;
  void processExpired();

  void pauseExpiry(std::uint32_t id);
  void resumeExpiry(std::uint32_t id, std::int32_t remaining_ms);

  [[nodiscard]] const std::deque<Notification>& all() const noexcept;
  [[nodiscard]] const std::deque<NotificationHistoryEntry>& history() const noexcept;
  [[nodiscard]] std::uint64_t changeSerial() const noexcept;
  void removeHistoryEntry(std::uint32_t id);
  void clearHistory();

  void setDoNotDisturb(bool enabled);
  [[nodiscard]] bool doNotDisturb() const noexcept;
  [[nodiscard]] bool toggleDoNotDisturb();

  void setServerDefaultTimeoutMs(std::int32_t ms);
  [[nodiscard]] std::int32_t serverDefaultTimeoutMs() const noexcept;

  void setStateCallback(StateCallback callback);

private:
  void upsertHistory(const Notification& notification, bool active, std::optional<CloseReason> close_reason);
  void rebuildHistoryIndex();

  std::deque<Notification> notifications_;
  std::unordered_map<std::uint32_t, std::size_t> id_to_index_;
  std::deque<NotificationHistoryEntry> history_;
  std::unordered_map<std::uint32_t, std::size_t> history_index_;
  std::vector<std::pair<int, EventCallback>> event_callbacks_;
  ActionInvokeCallback action_invoke_callback_;
  StateCallback state_callback_;
  int next_callback_token_{0};
  std::uint32_t next_id_{1};
  std::uint64_t change_serial_{0};
  bool do_not_disturb_ = false;
  std::int32_t server_default_timeout_ms_{kDefaultNotificationTimeoutMs};
};

}
