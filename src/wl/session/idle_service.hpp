#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eh::wayland {

struct IdleBehavior {
  std::string name;
  uint32_t timeout_ms = 0;
  std::function<void()> on_idle;
  std::function<void()> on_resumed;
  bool enabled = true;
};

class IdleService {
public:
  explicit IdleService(ext_idle_notifier_v1* notifier, wl_seat* seat);
  ~IdleService();

  IdleService(const IdleService&) = delete;
  IdleService& operator=(const IdleService&) = delete;
  IdleService(IdleService&&) = delete;
  IdleService& operator=(IdleService&&) = delete;

  void reload(std::vector<IdleBehavior> behaviors);
  void clear();

  [[nodiscard]] bool has_notifier() const { return notifier_ != nullptr; }

  static void on_idled(void* data, ext_idle_notification_v1* notification);
  static void on_resumed(void* data, ext_idle_notification_v1* notification);

private:
  struct BehaviorState {
    IdleBehavior config;
    ext_idle_notification_v1* notification = nullptr;
  };

  void create_notification(BehaviorState& bs);

  ext_idle_notifier_v1* notifier_ = nullptr;
  wl_seat* seat_ = nullptr;
  std::vector<std::unique_ptr<BehaviorState>> behaviors_;
};

}
