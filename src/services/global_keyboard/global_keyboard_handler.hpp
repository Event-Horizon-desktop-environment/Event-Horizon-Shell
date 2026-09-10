#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace eh::service {

class GlobalKeyboardHandler {
public:
  using ActivateFn = std::function<void()>;

  GlobalKeyboardHandler() = default;
  ~GlobalKeyboardHandler();

  GlobalKeyboardHandler(const GlobalKeyboardHandler&) = delete;
  GlobalKeyboardHandler& operator=(const GlobalKeyboardHandler&) = delete;
  GlobalKeyboardHandler(GlobalKeyboardHandler&&) = delete;
  GlobalKeyboardHandler& operator=(GlobalKeyboardHandler&&) = delete;

  bool init(ActivateFn startMenuFn, ActivateFn settingsFn = nullptr);
  void cleanup();

private:
  void evdev_thread_fn();

  std::atomic<bool> active_{false};
  ActivateFn startMenuFn_;
  ActivateFn settingsFn_;
  std::unique_ptr<std::thread> thread_;
};

} // namespace eh::service
