#include "wl/session/session_lock_service.hpp"

namespace eh::wayland {

bool SessionLockService::lock() {
   
  if (!manager_ || lock_) return false;
  lock_ = ext_session_lock_manager_v1_lock(manager_);
  return lock_ != nullptr;
}

void SessionLockService::unlock() {
   
  if (!lock_) return;
  ext_session_lock_v1_destroy(lock_);
  lock_ = nullptr;
}

}
