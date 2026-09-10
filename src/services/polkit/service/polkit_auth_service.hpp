#pragma once

#if defined(EH_HAVE_POLKIT_AGENT)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>

struct pollfd;

namespace eh::polkit {

enum class AuthState : std::uint8_t { Idle, Pending, Active, Success, Failed };

struct Snapshot {
  AuthState state = AuthState::Idle;
  std::string action_id;
  std::string message;
  std::string prompt;
  std::string error;
  std::string cookie;
  bool echo_on = true;
  bool has_request = false;
};

class PolkitAuthService {
public:
  using ChangeCallback = std::function<void()>;

  static PolkitAuthService& instance();

  void start();
  void stop();

  void set_change_callback(ChangeCallback cb);

  [[nodiscard]] Snapshot snapshot() const;

  void submit_response(const std::string& password);
  void cancel_request();

  // Called from poll loop integration
  void gather_fds(std::vector<pollfd>& fds);
  int poll_timeout();
  void dispatch_glib(const pollfd* fds, int nfds);

  // Exposed for C bridge (same TU, not part of public API)
  struct Impl;
  Impl* impl() { return impl_.get(); }
  const Impl* impl() const { return impl_.get(); }

private:
  PolkitAuthService();
  ~PolkitAuthService();
  PolkitAuthService(const PolkitAuthService&) = delete;
  PolkitAuthService& operator=(const PolkitAuthService&) = delete;
  PolkitAuthService(PolkitAuthService&&) = delete;
  PolkitAuthService& operator=(PolkitAuthService&&) = delete;

  std::unique_ptr<Impl> impl_;
};

}

#endif
