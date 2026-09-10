#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace eh::shell::notifications {

enum class Urgency : std::uint8_t {
  Low = 0,
  Normal = 1,
  Critical = 2,
};

enum class CloseReason : std::uint32_t {
  Expired = 1,
  Dismissed = 2,
  ClosedByCall = 3,
};

enum class NotificationOrigin : std::uint8_t {
  External = 0,
  Internal = 1,
};

enum class NotificationEvent {
  Added,
  Updated,
  Closed,
};

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct NotificationImageData {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t rowStride = 0;
  bool hasAlpha = true;
  std::int32_t bitsPerSample = 8;
  std::int32_t channels = 4;
  std::vector<std::uint8_t> data;

  bool operator==(const NotificationImageData&) const = default;
};

struct Notification {
  std::uint32_t id = 0;
  NotificationOrigin origin = NotificationOrigin::External;
  std::string appName;
  std::string summary;
  std::string body;
  std::int32_t timeout = 0;
  Urgency urgency = Urgency::Normal;
  std::vector<std::string> actions;
  std::optional<std::string> icon;
  std::optional<NotificationImageData> imageData;
  std::optional<std::string> category;
  std::optional<std::string> desktopEntry;
  TimePoint receivedTime{};
  std::optional<TimePoint> expiryTime;
};

struct NotificationHistoryEntry {
  Notification notification;
  bool active = true;
  std::optional<CloseReason> closeReason;
  std::uint64_t eventSerial = 0;
};

constexpr std::int32_t kDefaultNotificationTimeoutMs = 6000;

}
