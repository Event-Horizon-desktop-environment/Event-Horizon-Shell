#include "platform/radio_block.h"

#include <linux/rfkill.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace eh::platform {
namespace detail {

constexpr const char* kRfkillSysfsRoot = "/sys/class/rfkill";
constexpr const char* kRfkillControlNode = "/dev/rfkill";

[[nodiscard]] std::string_view sysfsNameFor(RadioKind kind) noexcept {
  switch (kind) {
  case RadioKind::Bluetooth:
    return "bluetooth";
  case RadioKind::Wifi:
    return "wlan";
  }
  return "";
}

[[nodiscard]] bool startsWithRfkill(const std::string& name) {
  return name.size() >= 6 && name.compare(0, 6, "rfkill") == 0;
}

// First whitespace-delimited token of a sysfs attribute, "" on any failure.
[[nodiscard]] std::string readAttributeToken(const std::filesystem::path& path) noexcept {
  std::ifstream in(path);
  if (!in) return "";
  std::string token;
  in >> token;
  return in ? token : "";
}

[[nodiscard]] bool readAttributeFlag(const std::filesystem::path& path, bool& out) noexcept {
  const std::string token = readAttributeToken(path);
  if (token.empty()) return false;
  if (token[0] == '1') {
    out = true;
    return true;
  }
  if (token[0] == '0') {
    out = false;
    return true;
  }
  return false;
}

[[nodiscard]] bool readAttributeIndex(const std::filesystem::path& path, unsigned& out) noexcept {
  const std::string token = readAttributeToken(path);
  if (token.empty()) return false;
  try {
    std::size_t pos = 0;
    const unsigned long v = std::stoul(token, &pos);
    if (pos == 0) return false;
    out = static_cast<unsigned>(v);
    return true;
  } catch (...) {
    return false;
  }
}

struct SwitchRow {
  unsigned index = 0;
  bool soft = false;
  bool hard = false;
};

// Collect every sysfs switch whose type label matches `kind`.
// Aggregates OR-semantics: a radio counts as blocked when any of its
// switches (driver + platform module, ...) reports blocked.
[[nodiscard]] std::vector<SwitchRow> collectSwitches(RadioKind kind) noexcept {
  std::vector<SwitchRow> rows;
  const std::string_view want = sysfsNameFor(kind);

  std::error_code ec;
  std::filesystem::directory_iterator it(kRfkillSysfsRoot, ec);
  if (ec) return rows;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    const std::string name = it->path().filename().string();
    if (!startsWithRfkill(name)) continue;
    const auto base = it->path();
    if (readAttributeToken(base / "type") != want) continue;
    SwitchRow row{};
    if (!readAttributeIndex(base / "index", row.index)) continue;
    // Missing soft/hard attributes mean "not blocked", not "skip row".
    bool flag = false;
    if (readAttributeFlag(base / "soft", flag)) row.soft = flag;
    if (readAttributeFlag(base / "hard", flag)) row.hard = flag;
    rows.push_back(row);
  }
  return rows;
}

// Index of the rfkill switch glued to a wireless PHY for `ifname`.
[[nodiscard]] bool indexForInterface(std::string_view ifname, unsigned& out) noexcept {
  if (ifname.empty() || ifname.find('/') != std::string_view::npos) return false;
  const std::filesystem::path phyDir = std::filesystem::path("/sys/class/net") / std::string(ifname) / "phy80211";

  std::error_code ec;
  std::filesystem::directory_iterator it(phyDir, ec);
  if (ec) return false;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!startsWithRfkill(it->path().filename().string())) continue;
    if (readAttributeIndex(it->path() / "index", out)) return true;
  }
  return false;
}

// sysfs row for a raw kernel index, if it is still visible.
[[nodiscard]] bool rowForIndex(unsigned index, SwitchRow& out) noexcept {
  std::error_code ec;
  std::filesystem::directory_iterator it(kRfkillSysfsRoot, ec);
  if (ec) return false;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    const std::string name = it->path().filename().string();
    if (!startsWithRfkill(name)) continue;
    const auto base = it->path();
    unsigned seen = 0;
    if (!readAttributeIndex(base / "index", seen) || seen != index) continue;
    out.index = index;
    bool flag = false;
    if (readAttributeFlag(base / "soft", flag)) out.soft = flag;
    if (readAttributeFlag(base / "hard", flag)) out.hard = flag;
    return true;
  }
  return false;
}

[[nodiscard]] RadioSetOutcome emitChange(unsigned index, bool blocked) noexcept {
  const int fd = ::open(kRfkillControlNode, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return {.ok = false, .hardBlocked = false, .detail = std::string("cannot open control node: ") + std::strerror(errno)};
  }

  rfkill_event request{};
  request.idx = index;
  request.op = RFKILL_OP_CHANGE;
  request.soft = blocked ? 1 : 0;

  ssize_t done = 0;
  do {
    done = ::write(fd, &request, sizeof(request));
  } while (done < 0 && errno == EINTR);
  const int saved = errno;
  ::close(fd);

  if (done != static_cast<ssize_t>(sizeof(request))) {
    const std::string why = done < 0 ? std::strerror(saved) : "truncated write to control node";
    return {.ok = false, .hardBlocked = false, .detail = why};
  }
  return {.ok = true, .hardBlocked = false, .detail = {}};
}

} // namespace detail

RadioBlockStatus radioBlockStatus(RadioKind kind) noexcept {
  RadioBlockStatus status{};
  for (const auto& row : detail::collectSwitches(kind)) {
    status.present = true;
    status.softBlocked = status.softBlocked || row.soft;
    status.hardBlocked = status.hardBlocked || row.hard;
  }
  return status;
}

RadioSetOutcome setRadioSoftBlocked(RadioKind kind, bool blocked) noexcept {
  const std::vector<detail::SwitchRow> targets = detail::collectSwitches(kind);
  if (targets.empty()) {
    return {.ok = false, .hardBlocked = false, .detail = "no radio switch present"};
  }
  for (const auto& row : targets) {
    if (row.hard) {
      return {.ok = false, .hardBlocked = true, .detail = "hardware switch is engaged"};
    }
  }
  // Drive each switch individually so multi-switch radios (driver plus
  // vendor module) all settle; skip ones already in the desired state.
  RadioSetOutcome outcome{.ok = true, .hardBlocked = false, .detail = {}};
  bool touched = false;
  for (const auto& row : targets) {
    if (row.soft == blocked) continue;
    outcome = detail::emitChange(row.index, blocked);
    if (!outcome.ok) return outcome;
    touched = true;
  }
  if (!touched) return {.ok = true, .hardBlocked = false, .detail = {}};
  return outcome;
}

RadioSetOutcome setInterfaceRadioSoftBlocked(std::string_view ifname, bool blocked) noexcept {
  unsigned index = 0;
  if (!detail::indexForInterface(ifname, index)) {
    return {.ok = false, .hardBlocked = false, .detail = "no radio switch for interface"};
  }
  detail::SwitchRow row{};
  if (detail::rowForIndex(index, row)) {
    if (row.hard) {
      return {.ok = false, .hardBlocked = true, .detail = "hardware switch is engaged"};
    }
    if (row.soft == blocked) return {.ok = true, .hardBlocked = false, .detail = {}};
  }
  return detail::emitChange(index, blocked);
}

} // namespace eh::platform
