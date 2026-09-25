#pragma once

#include <string>
#include <string_view>

// Horizon radio-block probe / control.
//
// Talks to the kernel rfkill sysfs tree (/sys/class/rfkill) for state and to
// /dev/rfkill for change requests. Deliberately Horizon-shaped API: callers
// deal in RadioKind + aggregated status, never in raw rfkill indexes.
namespace eh::platform {

enum class RadioKind {
  Bluetooth,
  Wifi,
};

struct RadioBlockStatus {
  bool present = false;
  bool softBlocked = false;
  bool hardBlocked = false;
};

struct RadioSetOutcome {
  bool ok = false;
  bool hardBlocked = false;
  std::string detail;
};

// Aggregated block state for every switch of this kind. Never throws;
// reports present=false when the machine has no such radio.
[[nodiscard]] RadioBlockStatus radioBlockStatus(RadioKind kind) noexcept;

// Drive every switch of this kind to the requested soft-block state.
[[nodiscard]] RadioSetOutcome setRadioSoftBlocked(RadioKind kind, bool blocked) noexcept;

// Drive the WLAN switch backing a network interface (e.g. "wlan0").
[[nodiscard]] RadioSetOutcome setInterfaceRadioSoftBlocked(std::string_view ifname, bool blocked) noexcept;

} // namespace eh::platform
