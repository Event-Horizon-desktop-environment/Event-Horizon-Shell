#pragma once

// Desk catalog: the per-output, compositor-specific desk data that feeds the
// overview.
//
// Distinct from IDeskSource on purpose. A backend can expose desk reads
// through IDeskSource without necessarily being able to track overview state
// (only some compositors report an overview/expo mode), so this capability
// is a separate interface rather than a pile of optional methods.

#include "backends/contract/desk_types.h"

#include <poll.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wspace {

class IDeskCatalog {
public:
  using Notify = std::function<void()>;

  virtual ~IDeskCatalog() = default;

  // --- overview capability ---------------------------------------------
  [[nodiscard]] virtual bool supportsOverview() const noexcept { return false; }
  [[nodiscard]] virtual bool hasOverview() const noexcept { return false; }
  [[nodiscard]] virtual bool overviewActive() const noexcept { return true; }

  // --- change notification ---------------------------------------------
  virtual void onStateChange(Notify cb) = 0;
  virtual void onOverviewChange(Notify cb) { (void)cb; }

  // --- polling ----------------------------------------------------------
  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int eventTimeout() const noexcept { return -1; }
  virtual void onEvent(short /*revents*/) {}

  // --- reads ------------------------------------------------------------
  // Reconcile caller-supplied desks against what the compositor reports.
  // `outputName` narrows the search to one output; empty means all.
  virtual void sync(std::vector<DeskSlot>& /*desks*/, const std::string& /*outputName*/) const {}

  [[nodiscard]] virtual std::vector<std::string> deskKeys(const std::string& /*outputName*/) const {
    return {};
  }

  [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(const std::string& /*outputName*/) const {
    return {};
  }

  [[nodiscard]] virtual std::vector<ToplevelRef> windowsOnDesk(const std::string& /*outputName*/) const {
    return {};
  }

  virtual void teardown() = 0;
};

} // namespace wspace
