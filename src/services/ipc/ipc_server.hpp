#pragma once

#include "services/ipc/ipc_protocol.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct pollfd;

namespace eh::ipc {

using IpcHandler = std::function<std::string(const std::vector<std::string>&)>;

std::string default_socket_path();

// Poll interest for one fd owned by the IPC bus.
struct PollInterest {
  int   fd = -1;
  short events = 0;
};

class IpcService {
public:
  IpcService();
  ~IpcService();

  IpcService(const IpcService&) = delete;
  IpcService& operator=(const IpcService&) = delete;
  IpcService(IpcService&&) = delete;
  IpcService& operator=(IpcService&&) = delete;

  bool start(const std::string& path);
  void stop();
  [[nodiscard]] bool running() const { return listenFd_ >= 0; }
  [[nodiscard]] int listen_fd() const { return listenFd_; }

  void register_handler(std::string name, IpcHandler handler);
  void unregister_handler(const std::string& name);
  [[nodiscard]] std::vector<std::string> registered_commands() const;

  // Poll-loop integration. Call from build_handlers(): every fd that needs
  // attention this iteration (listen fd + persistent clients, with POLLOUT when
  // a client has queued writes). Then hand each returned fd to on_fd_ready().
  [[nodiscard]] std::vector<PollInterest> poll_interests() const;
  void on_accept();                     // listen fd readable
  void on_fd_ready(int fd, short revents);

  // Broadcast a framed event to every connected client subscribed to `topic`.
  // Payload is arbitrary bytes; the wire body is "topic\0payload". Optional
  // `fds` are passed via SCM_RIGHTS attached to the event frame (the event's
  // kFlagFd is set). The server closes each fd after delivery.
  void publish(const std::string& topic, std::string_view payload,
               const std::vector<int>& fds = {});

  // Test hook: number of currently connected framed clients.
  [[nodiscard]] size_t client_count() const;

private:
  struct Client;
  struct QueuedEvent;
  void dispatch_legacy(int clientFd);
  std::string process_command(const std::string& line);
  void handle_frame(Client& c, IpcFrame&& frame);
  bool flush_client(Client& c);
  void close_client(int fd);
  void drop_client(Client& c);

  int listenFd_ = -1;
  std::string socketPath_;
  mutable std::mutex handlersMtx_;
  std::unordered_map<std::string, IpcHandler> handlers_;

  mutable std::mutex busMtx_;
  std::unordered_set<int> pending_;  // accepted, awaiting first byte
  std::unordered_map<int, std::unique_ptr<Client>> clients_;
  std::unordered_map<std::string, std::unordered_set<int>> subscriptions_;
  uint64_t nextRequestId_ = 1;
};

} // namespace eh::ipc
