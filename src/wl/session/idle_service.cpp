#include "wl/session/idle_service.hpp"

#include <algorithm>
#include <utility>

namespace {

constexpr ext_idle_notification_v1_listener kIdleNotificationListener = {
    .idled = &eh::wayland::IdleService::on_idled,
    .resumed = &eh::wayland::IdleService::on_resumed,
};

}

namespace eh::wayland {

IdleService::IdleService(ext_idle_notifier_v1* notifier, wl_seat* seat)
    : notifier_(notifier), seat_(seat) {}

IdleService::~IdleService() { clear(); }

void IdleService::reload(std::vector<IdleBehavior> behaviors) {
   
  clear();
  behaviors_.reserve(behaviors.size());
  for (auto& b : behaviors) {
    if (!b.enabled || b.timeout_ms == 0) continue;
    auto bs = std::make_unique<BehaviorState>();
    bs->config = std::move(b);
    create_notification(*bs);
    behaviors_.push_back(std::move(bs));
  }
}

void IdleService::clear() {
   
  for (auto& bs : behaviors_) {
    if (bs->notification) {
      ext_idle_notification_v1_destroy(bs->notification);
      bs->notification = nullptr;
    }
  }
  behaviors_.clear();
}

void IdleService::create_notification(BehaviorState& bs) {
   
  if (!notifier_ || !seat_) return;
  bs.notification = ext_idle_notifier_v1_get_idle_notification(notifier_, bs.config.timeout_ms, seat_);
  if (bs.notification) {
    ext_idle_notification_v1_add_listener(bs.notification, &kIdleNotificationListener, &bs);
  }
}

void IdleService::on_idled(void* data, ext_idle_notification_v1*) {
   
  auto& bs = *static_cast<BehaviorState*>(data);
  if (bs.config.on_idle) bs.config.on_idle();
}

void IdleService::on_resumed(void* data, ext_idle_notification_v1*) {
   
  auto& bs = *static_cast<BehaviorState*>(data);
  if (bs.config.on_resumed) bs.config.on_resumed();
}

}
