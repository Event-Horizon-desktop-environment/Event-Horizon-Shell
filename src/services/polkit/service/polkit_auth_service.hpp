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
  std::string action_id;   // polkit action, e.g. org.freedesktop.policykit.exec
  std::string message;     // polkit's "Authentication is required to ..." line
  std::string identity;    // who is being asked, e.g. "user" / "Full Name (user)"
  std::string prompt;      // session request, usually "Password:" (field label)
  std::string icon_name;   // themed icon name polkit supplied for the action
  std::string details;     // serialized PolkitDetails ("key=value, ...")
  std::string info;        // show-info text (session notes)
  std::string error;       // show-error text
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

  // Registration runs on its own thread; the standalone main waits for it
  // and exits non-zero on failure so the supervisor respawns the child.
  [[nodiscard]] bool registration_done() const;
  [[nodiscard]] bool registration_ok() const;

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
