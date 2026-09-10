#pragma once

#include "services/ipc/ipc_protocol.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eh::ipc {

class IpcClient {
public:
  // Raw bytes of a received event: body is "topic\0payload". `fds` are any
  // SCM_RIGHTS descriptors attached to the event; the client closes them after
  // the handler returns, so dup() anything the handler wants to keep.
  using EventHandler =
      std::function<void(std::string topic, std::string payload, std::vector<int> fds)>;

  IpcClient();
  ~IpcClient();
  IpcClient(const IpcClient&) = delete;
  IpcClient& operator=(const IpcClient&) = delete;

  // Connect to the server socket (retries up to `attempts` with a small delay).
  // Returns the connection fd on success, or -1 on failure.
  int connect(const std::string& path, int attempts = 8);
  void disconnect();
  [[nodiscard]] int fd() const { return fd_; }
  [[nodiscard]] bool connected() const { return fd_ >= 0; }

  // Framed request/response round trip (blocking).
  std::optional<std::string> request(const std::string& command_line, int timeoutMs = 3000);

  // Subscribe to a topic; delivered via `on_event` on the poll/read path.
  bool subscribe(const std::string& topic, int timeoutMs = 3000);
  // Fire-and-forget subscribe: sends the subscribe frame without waiting for an
  // ACK.  Use this from the supervisor startup path where the IPC server poll
  // loop has not started yet (the ACK cannot arrive until the server processes
  // the frame, which requires the poll loop to run).
  bool subscribe_async(const std::string& topic);
  bool unsubscribe(const std::string& topic, int timeoutMs = 3000);

  // Publish an event to the bus. The server re-broadcasts it to every
  // subscriber of `topic` (this client included, if subscribed). `fds` are
  // attached via SCM_RIGHTS; the server closes them after forwarding.
  bool publish(const std::string& topic, std::string_view payload, const std::vector<int>& fds = {});

  // Nonblocking poll integration: fds to watch, and the read handler for a ready
  // fd. Call from the child's poll loop (same pattern as the server).
  [[nodiscard]] std::vector<int> poll_fds() const;
  void on_fd_ready(int fd);

  void set_event_handler(EventHandler handler) { onEvent_ = std::move(handler); }

private:
  bool wait_for_ack(uint64_t id, int timeoutMs);
  bool pump_frame(int timeoutMs, IpcFrame& out);
  void dispatch_event(const IpcFrame& frame);

  int fd_ = -1;
  uint64_t nextId_ = 1;
  std::vector<uint8_t> rbuf_;
  std::vector<uint8_t> wbuf_;
  std::vector<int> pendingFds_;  // fds attached to the next flagged frame
  EventHandler onEvent_;
};

} // namespace eh::ipc
