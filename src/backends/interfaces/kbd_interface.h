#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <poll.h>
#include <string>
#include <vector>

struct LayoutInfo {
  std::vector<std::string> names;
  int activeIndex = -1;
};

class IKeyboardManager {
public:
  using Notify = std::function<void()>;

  virtual ~IKeyboardManager() = default;

  [[nodiscard]] virtual bool ready() const noexcept = 0;
  [[nodiscard]] virtual std::optional<LayoutInfo> currentLayout() const = 0;
  [[nodiscard]] virtual std::optional<std::string> activeLayoutName() const = 0;
  [[nodiscard]] virtual bool nextLayout() const = 0;

  virtual void onStateChange(Notify) {}

  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }

  virtual bool openConnection() { return false; }
  virtual void teardown() {}
  virtual void onEvent(short) {}
};
