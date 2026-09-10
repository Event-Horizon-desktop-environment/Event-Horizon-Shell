#pragma once

#include "services/ipc/client.hpp"
#include "services/windows/toplevel_types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Client-side mirror of the toplevel/window service. Split-out
// components (taskbar, overview, dock app menu) use this instead of borrowing
// the dock's ForeignToplevels/ExtForeignToplevels trackers. It subscribes to
// the toplevel.created/updated/closed topics, fetches the initial snapshot via
// the "toplevel.list" command, and calls the change callback whenever the list
// mutates. It owns the eh::ipc::IpcClient (subscriptions ride its connection).
//
// `find()` returns an iterator into `list` keyed by the id assigned by the
// service (to avoid name comparisons against arbitrary titles).

namespace eh::windows {

class ToplevelClient {
public:
  using ChangeHandler = std::function<void(const ToplevelClient&)>;

  ToplevelClient() = default;
  ~ToplevelClient() = default;
  ToplevelClient(const ToplevelClient&) = delete;
  ToplevelClient& operator=(const ToplevelClient&) = delete;
  ToplevelClient(ToplevelClient&&) = delete;
  ToplevelClient& operator=(ToplevelClient&&) = delete;

  // Connect, subscribe to the toplevel topics and fetch the initial snapshot.
  bool start(const std::string& socket_path);

  void set_change_handler(ChangeHandler h) { onChanged_ = std::move(h); }

  [[nodiscard]] const std::vector<ToplevelRecord>& list() const { return list_; }
  [[nodiscard]] const ToplevelRecord* find(std::uint64_t id) const;

  // Passthrough for the component's poll loop.
  [[nodiscard]] std::vector<int> poll_fds() const { return ipc_->poll_fds(); }
  void on_fd_ready(int fd) { ipc_->on_fd_ready(fd); }

private:
  void handle_event(std::string topic, std::string payload, std::vector<int> fds);
  void apply_record(const ToplevelRecord& rec, bool create);
  void remove_record(std::uint64_t id);
  void notify() {
    if (onChanged_) onChanged_(*this);
  }

  std::unique_ptr<eh::ipc::IpcClient> ipc_;
  std::vector<ToplevelRecord> list_;
  ChangeHandler onChanged_;
};

} // namespace eh::windows
