#pragma once

// Desk source: the read/navigate side of a compositor backend.
//
// Split from the old catch-all interface header so a backend implements only
// what it actually is. The default bodies below are the contract's way of
// saying "not supported by this compositor" — a backend overrides what its
// protocol can express and inherits the rest.

#include "backends/contract/desk_types.h"

#include <poll.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct wl_output;

// Interface for one compositor's desk source.
//
// Life cycle is poll-driven: the owner asks for the fd and timeout, waits,
// then hands back the readiness flags. Backends that need no polling inherit
// the defaults and simply never wake the owner.
class IDeskSource {
public:
  using Notify = std::function<void()>;

  virtual ~IDeskSource() = default;

  // --- polling ----------------------------------------------------------
  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int eventTimeout() const noexcept { return -1; }
  virtual void onEvent(short /*revents*/) {}

  // --- desk queries -----------------------------------------------------
  [[nodiscard]] virtual std::vector<DeskSlot> allRegions() const = 0;
  [[nodiscard]] virtual std::vector<DeskSlot> regionsOnOutput(wl_output* output) const = 0;

  // Applications present on each desk, keyed by desk key.
  [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* /*output*/) const {
    return {};
  }

  // --- navigation -------------------------------------------------------
  // Focus a desk by key. Keys the backend did not produce are ignored.
  virtual void jumpTo(const std::string& key) = 0;
  virtual void jumpToOnOutput(wl_output* output, const std::string& key) = 0;
  virtual void jumpToOnOutput(wl_output* output, const DeskSlot& desk) = 0;

  // --- life cycle -------------------------------------------------------
  virtual void teardown() = 0;
  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual bool ready() const noexcept = 0;

  // --- change notification ---------------------------------------------
  virtual void onStateChange(Notify cb) = 0;

  // --- bar integration --------------------------------------------------
  [[nodiscard]] virtual BarMatchPolicy taskbarMode() const noexcept {
    return BarMatchPolicy::Uniform;
  }

  // Resolve bar rows against the live desk set. Returns row handle -> window.
  [[nodiscard]] virtual std::unordered_map<std::uintptr_t, ToplevelRef>
  matchEntries(const std::vector<BarItem>& /*rows*/, wl_output* /*output*/) const {
    return {};
  }

  [[nodiscard]] virtual std::vector<ToplevelRef> windowsOnDesk(wl_output* /*output*/) const {
    return {};
  }

  virtual void bringToFront(const std::string& /*handle*/) {}
};
