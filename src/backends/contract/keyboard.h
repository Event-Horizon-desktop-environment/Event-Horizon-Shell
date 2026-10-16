#pragma once

// Keyboard layout source: querying and switching the active input layout.
//
// Kept separate from desk concerns — a compositor may expose workspaces
// without exposing layout switching, and vice versa.

#include <string>
#include <vector>

// Snapshot of the layouts a compositor currently offers.
struct LayoutSet {
  // Index into `names` for the active layout, or -1 when unknown.
  int activeIndex = -1;
  // Display names, in the compositor's reported order.
  std::vector<std::string> names;
};

#include <functional>
#include <optional>

class ILayoutSource;

#include <poll.h>

// Interface for one compositor's keyboard layout source.
//
// Layout switching is synchronous from the caller's perspective: `nextLayout`
// returns false if the compositor could not be reached. Callers that need
// change notification use `onStateChange`.
class ILayoutSource {
public:
  using Notify = std::function<void()>;
  // One layout's display name as reported by the compositor.
  using LayoutName = std::optional<std::string>;

  virtual ~ILayoutSource() = default;

  virtual void onStateChange(Notify /*cb*/) {}

  [[nodiscard]] virtual LayoutName activeLayoutName() const = 0;
  [[nodiscard]] virtual std::optional<LayoutSet> currentLayout() const = 0;

  // Cycle to the next layout. Returns false when the link is down.
  [[nodiscard]] virtual bool nextLayout() const = 0;

  [[nodiscard]] virtual bool ready() const noexcept = 0;

  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  virtual void onEvent(short /*revents*/) {}
  virtual bool openConnection() { return false; }
  virtual void teardown() {}
};
