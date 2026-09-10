#define _GNU_SOURCE 1
#include "services/ipc/ipc_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

namespace eh::ipc {

// Per-connection state.

struct IpcService::QueuedEvent {
  std::vector<uint8_t> bytes;
  std::vector<int>     fds;        // SCM_RIGHTS attachments (delivered on first send)
  bool                 fdsSent = false;
};

struct IpcService::Client {
  int fd = -1;
  std::vector<uint8_t> rbuf;
  std::deque<QueuedEvent> out;
  bool wantPollOut = false;
};

// Socket path helpers.

std::string default_socket_path() {
  if (const char* p = std::getenv("EH_IPC_SOCKET")) {
    return std::string(p);
  }
  if (const char* dir = std::getenv("XDG_RUNTIME_DIR")) {
    return std::string(dir) + "/event-horizon-ipc.sock";
  }
  return "/tmp/event-horizon-ipc.sock";
}

// IpcService.

IpcService::IpcService() = default;

IpcService::~IpcService() {
  stop();
}

bool IpcService::start(const std::string& path) {
  if (listenFd_ >= 0) stop();

  socketPath_ = path;

  // Remove stale socket file
  unlink(socketPath_.c_str());

  listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listenFd_ < 0) {
    std::cerr << "[ipc] socket() failed: " << std::strerror(errno) << "\n";
    return false;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const size_t pathLen = std::min(socketPath_.size(), sizeof(addr.sun_path) - 1);
  std::memcpy(addr.sun_path, socketPath_.data(), pathLen);
  addr.sun_path[pathLen] = '\0';

  if (::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[ipc] bind(\"" << socketPath_ << "\") failed: " << std::strerror(errno) << "\n";
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }

  // Make the socket accessible to all users
  ::chmod(socketPath_.c_str(), 0666);

  if (::listen(listenFd_, 8) < 0) {
    std::cerr << "[ipc] listen() failed: " << std::strerror(errno) << "\n";
    ::close(listenFd_);
    listenFd_ = -1;
    unlink(socketPath_.c_str());
    return false;
  }

  std::cerr << "[ipc] listening on " << socketPath_ << "\n";
  return true;
}

void IpcService::stop() {
  {
    std::lock_guard<std::mutex> lock(busMtx_);
    for (auto& [fd, client] : clients_) {
      (void)fd;
      ::close(client->fd);
    }
    clients_.clear();
    for (int fd : pending_) {
      ::close(fd);
    }
    pending_.clear();
    subscriptions_.clear();
  }
  if (listenFd_ >= 0) {
    ::close(listenFd_);
    listenFd_ = -1;
  }
  if (!socketPath_.empty()) {
    unlink(socketPath_.c_str());
    socketPath_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(handlersMtx_);
    handlers_.clear();
  }
}

void IpcService::register_handler(std::string name, IpcHandler handler) {
  std::lock_guard<std::mutex> lock(handlersMtx_);
  handlers_[std::move(name)] = std::move(handler);
}

void IpcService::unregister_handler(const std::string& name) {
  std::lock_guard<std::mutex> lock(handlersMtx_);
  handlers_.erase(name);
}

std::vector<std::string> IpcService::registered_commands() const {
  std::lock_guard<std::mutex> lock(handlersMtx_);
  std::vector<std::string> cmds;
  cmds.reserve(handlers_.size());
  for (const auto& [name, _] : handlers_) {
    cmds.push_back(name);
  }
  return cmds;
}

// Connection management.

void IpcService::on_accept() {
  while (true) {
    struct sockaddr_un clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd = ::accept4(listenFd_, reinterpret_cast<struct sockaddr*>(&clientAddr),
                             &addrLen, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (clientFd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      if (errno == EINTR) continue;
      std::cerr << "[ipc] accept4() failed: " << std::strerror(errno) << "\n";
      break;
    }

    // A fresh connection may not have sent its first byte yet (connect returns
    // before the client writes). Park it in `pending_`; the poll loop re-peeks it
    // in on_fd_ready once data arrives, choosing the dialect then.
    std::lock_guard<std::mutex> lock(busMtx_);
    pending_.insert(clientFd);
  }
}

void IpcService::dispatch_legacy(int clientFd) {
  // One-shot legacy command: read a plain-text line (<= kMaxLegacyCmd), respond
  // with the handler output followed by a newline, then close.
  char buf[kMaxLegacyCmd + 1];
  ssize_t n = ::read(clientFd, buf, kMaxLegacyCmd);
  if (n <= 0) {
    ::close(clientFd);
    return;
  }
  buf[n] = '\0';

  std::string line(buf);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
    line.pop_back();
  }

  std::string response = process_command(line);
  response += '\n';

  const char* out = response.data();
  size_t remaining = response.size();
  while (remaining > 0) {
    ssize_t written = ::send(clientFd, out, remaining, MSG_NOSIGNAL);
    if (written > 0) {
      out += written;
      remaining -= static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Response is tiny (command output is bounded); the peer is draining it
      // slowly. Pause briefly rather than hot-spinning, then retry.
      ::usleep(1000);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    break;
  }
  ::close(clientFd);
}

void IpcService::close_client(int fd) {
  std::lock_guard<std::mutex> lock(busMtx_);
  auto it = clients_.find(fd);
  if (it != clients_.end()) {
    ::close(it->second->fd);
    for (auto& [topic, members] : subscriptions_) {
      members.erase(fd);
    }
    clients_.erase(it);
  }
}

// Command dispatch (shared by legacy and framed requests).

std::string IpcService::process_command(const std::string& line) {
  if (line.empty()) return "error empty command";

  std::vector<std::string> tokens;
  std::istringstream stream(line);
  std::string token;
  while (stream >> token) {
    tokens.push_back(std::move(token));
  }

  if (tokens.empty()) return "error empty command";

  const std::string& cmd = tokens[0];
  std::vector<std::string> args(tokens.begin() + 1, tokens.end());

  IpcHandler handler;
  {
    std::lock_guard<std::mutex> lock(handlersMtx_);
    auto it = handlers_.find(cmd);
    if (it != handlers_.end())
      handler = it->second;
  }
  if (handler) {
    try {
      return handler(args);
    } catch (const std::exception& e) {
      return "error ipc handler exception: " + std::string(e.what());
    }
  }

  return "error unknown command: " + cmd;
}

// Framed protocol handling.

void IpcService::handle_frame(Client& c, IpcFrame&& frame) {
  if (frame.header.version != kProtocolVersion) return;

  std::string topic;
  switch (frame.header.type) {
    case kTypeRequest: {
      std::string line(frame.body.begin(), frame.body.end());
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
      }
      IpcFrame resp = make_response(frame.header.id, process_command(line));
      std::vector<uint8_t> enc;
      encode_frame(resp, enc);
      std::lock_guard<std::mutex> lock(busMtx_);
      c.out.push_back(QueuedEvent{std::move(enc), {}, false});
      c.wantPollOut = true;
      break;
    }
    case kTypeSubscribe: {
      std::string_view raw(reinterpret_cast<const char*>(frame.body.data()), frame.body.size());
      topic.assign(raw);
      if (const size_t nul = topic.find('\0'); nul != std::string::npos) {
        topic.resize(nul);
      }
      std::vector<uint8_t> enc;
      encode_frame(make_ack(frame.header.id), enc);
      {
        std::lock_guard<std::mutex> lock(busMtx_);
        if (!topic.empty()) {
          subscriptions_[topic].insert(c.fd);
        }
        c.out.push_back(QueuedEvent{std::move(enc), {}, false});
        c.wantPollOut = true;
      }
      break;
    }
    case kTypeUnsubscribe: {
      std::string_view raw(reinterpret_cast<const char*>(frame.body.data()), frame.body.size());
      topic.assign(raw);
      if (const size_t nul = topic.find('\0'); nul != std::string::npos) {
        topic.resize(nul);
      }
      std::vector<uint8_t> enc;
      encode_frame(make_ack(frame.header.id), enc);
      {
        std::lock_guard<std::mutex> lock(busMtx_);
        if (auto it = subscriptions_.find(topic); it != subscriptions_.end()) {
          it->second.erase(c.fd);
        }
        c.out.push_back(QueuedEvent{std::move(enc), {}, false});
        c.wantPollOut = true;
      }
      break;
    }
    default:
      // Events / responses / acks from clients: re-broadcast events to
      // subscribers so any framed client (a split-out component) can publish.
      // Responses/acks from clients are not processed.
      if (frame.header.type == kTypeEvent) {
        const auto nul = std::find(frame.body.begin(), frame.body.end(), '\0');
        std::string_view topic(reinterpret_cast<const char*>(frame.body.data()),
                               static_cast<size_t>(nul - frame.body.begin()));
        std::string_view payload;
        if (nul != frame.body.end()) {
          const size_t off = static_cast<size_t>(nul + 1 - frame.body.begin());
          payload = std::string_view(reinterpret_cast<const char*>(frame.body.data() + off),
                                     frame.body.size() - off);
        }
        publish(std::string(topic), payload);
      }
      break;
  }
}

bool IpcService::flush_client(Client& c) {
  std::lock_guard<std::mutex> lock(busMtx_);
  while (!c.out.empty()) {
    QueuedEvent& q = c.out.front();

    struct msghdr msg{};
    struct iovec iov{};
    iov.iov_base = q.bytes.data();
    iov.iov_len  = q.bytes.size();
    msg.msg_iov  = &iov;
    msg.msg_iovlen = 1;

    std::vector<char> cbuf;
    const bool withFds = !q.fds.empty() && !q.fdsSent;
    if (withFds) {
      cbuf.resize(CMSG_SPACE(sizeof(int) * q.fds.size()));
      msg.msg_control = cbuf.data();
      msg.msg_controllen = cbuf.size();
      struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type  = SCM_RIGHTS;
      cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * q.fds.size());
      std::memcpy(CMSG_DATA(cmsg), q.fds.data(), sizeof(int) * q.fds.size());
    }

    ssize_t written = ::sendmsg(c.fd, &msg, MSG_NOSIGNAL);
    if (written > 0) {
      if (withFds) {
        q.fdsSent = true;
        for (int f : q.fds) ::close(f);
        q.fds.clear();
      }
      q.bytes.erase(q.bytes.begin(), q.bytes.begin() + written);
      if (q.bytes.empty()) {
        c.out.pop_front();
        if (c.out.empty()) {
          // Queue fully drained: drop the POLLOUT interest. Leaving it set
          // makes an idle (always-writable) unix socket report readiness on
          // every ppoll iteration -> 100% CPU busy-loop in the poll thread.
          c.wantPollOut = false;
        }
      }
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      return true;  // keep what remains queued; poll will re-arm POLLOUT
    }
    return false;  // hard error: caller drops the connection
  }
  return true;
}

void IpcService::on_fd_ready(int fd, short revents) {
  if (fd == listenFd_) {
    on_accept();
    return;
  }

  // A connection parked before its first byte arrived: resolve its dialect now.
  int legacyFd = -1;
  {
    std::lock_guard<std::mutex> lock(busMtx_);
    auto pendingIt = pending_.find(fd);
    if (pendingIt != pending_.end()) {
      if (revents & (POLLERR | POLLNVAL | POLLHUP)) {
        pending_.erase(pendingIt);
        ::close(fd);
        return;
      }
      char probe = 0;
      ssize_t got = ::recv(fd, &probe, 1, MSG_PEEK);
      if (got == 0) {
        pending_.erase(pendingIt);
        ::close(fd);
        return;
      }
      if (got < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          return;  // still no data; keep waiting
        }
        pending_.erase(pendingIt);
        ::close(fd);
        return;
      }
      pending_.erase(pendingIt);
      if (static_cast<uint8_t>(probe) == kFrameMagic) {
        auto client = std::make_unique<Client>();
        client->fd = fd;
        clients_[fd] = std::move(client);
        // Fall through to the framed read path below.
      } else {
        legacyFd = fd;
      }
    }
  }
  if (legacyFd >= 0) {
    dispatch_legacy(legacyFd);
    return;
  }

  Client* c = nullptr;
  {
    std::lock_guard<std::mutex> lock(busMtx_);
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    c = it->second.get();
  }
  // Client lifetime is owned by this poll thread: only on_fd_ready/stop() remove
  // entries, so the raw pointer stays valid while we work.

  bool dead = (revents & (POLLERR | POLLNVAL | POLLHUP)) != 0;

  if (!dead && (revents & POLLIN)) {
    uint8_t chunk[1u << 16];
    ssize_t n = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (n > 0) {
      c->rbuf.insert(c->rbuf.end(), chunk, chunk + n);
    } else if (n == 0) {
      dead = true;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      dead = true;
    }

    if (!dead) {
      size_t off = 0;
      while (off < c->rbuf.size()) {
        size_t consumed = 0;
        IpcFrame frame;
        if (!decode_frame(c->rbuf.data() + off, c->rbuf.size() - off, consumed, frame)) {
          if (consumed == 0) break;  // incomplete frame: wait for more data
          dead = true;               // corrupt stream
          break;
        }
        handle_frame(*c, std::move(frame));
        off += consumed;
      }
      if (off > 0) {
        c->rbuf.erase(c->rbuf.begin(), c->rbuf.begin() + off);
      }
    }
  }

  if (!dead && (revents & POLLOUT) && !flush_client(*c)) {
    dead = true;
  }

  if (dead) {
    close_client(fd);
  }
}

// Poll integration.

std::vector<PollInterest> IpcService::poll_interests() const {
  std::vector<PollInterest> interests;
  if (listenFd_ >= 0) {
    interests.push_back({listenFd_, POLLIN});
  }
  std::lock_guard<std::mutex> lock(busMtx_);
  for (int fd : pending_) {
    interests.push_back({fd, POLLIN});
  }
  for (auto& [fd, client] : clients_) {
    short events = POLLIN;
    if (client->wantPollOut || !client->out.empty()) {
      events |= POLLOUT;
    }
    interests.push_back({fd, events});
  }
  return interests;
}

// Pub/sub.

void IpcService::publish(const std::string& topic, std::string_view payload,
                         const std::vector<int>& fds) {
  if (listenFd_ < 0) return;

  IpcFrame evt = make_event(topic, payload);
  if (!fds.empty()) {
    evt.header.flags |= kFlagFd;
  }
  std::vector<uint8_t> enc;
  encode_frame(evt, enc);

  std::lock_guard<std::mutex> lock(busMtx_);
  auto it = subscriptions_.find(topic);
  if (it == subscriptions_.end()) return;
  for (int fd : it->second) {
    auto client = clients_.find(fd);
    if (client == clients_.end()) continue;
    client->second->out.push_back(QueuedEvent{enc, fds, false});
    client->second->wantPollOut = true;
  }
}

size_t IpcService::client_count() const {
  std::lock_guard<std::mutex> lock(busMtx_);
  return clients_.size();
}

} // namespace eh::ipc
