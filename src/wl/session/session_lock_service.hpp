#pragma once

#include "wl/core/protocols.hpp"

namespace eh::wayland {

class SessionLockService {
public:
  void bind(ext_session_lock_manager_v1* manager) { manager_ = manager; }
  [[nodiscard]] bool is_available() const noexcept { return manager_ != nullptr; }
  [[nodiscard]] bool is_locked() const noexcept { return lock_ != nullptr; }

  [[nodiscard]] bool lock();
  void unlock();

private:
  ext_session_lock_manager_v1* manager_ = nullptr;
  ext_session_lock_v1* lock_ = nullptr;
};

}
