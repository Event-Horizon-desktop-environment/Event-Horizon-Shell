#pragma once

#include <cstdint>
#include <string>
#include <string_view>

enum class RfkillDeviceType : std::uint8_t {
  Bluetooth,
  Wlan,
};

struct RfkillSwitchResult {
  bool success = false;
  bool hardBlocked = false;
  std::string detail;
};

[[nodiscard]] RfkillSwitchResult setRfkillSoftBlocked(RfkillDeviceType type, bool softBlocked);

[[nodiscard]] RfkillSwitchResult setRfkillSoftBlockedForNetInterface(std::string_view ifname, bool softBlocked);

[[nodiscard]] bool isRfkillSoftBlocked(RfkillDeviceType type);

[[nodiscard]] bool isRfkillHardBlocked(RfkillDeviceType type);
