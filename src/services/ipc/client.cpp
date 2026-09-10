#define _GNU_SOURCE 1
#include "services/ipc/client.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <sys/uio.h>

namespace eh::ipc {

using Clock = std::chrono::steady_clock;
namespace {

bool read_available(int fd, std::vector<uint8_t>& buf, std::vector<int>& fds) {
  uint8_t chunk[1u << 16];
  for (;;) {
    struct msghdr msg{};
    struct iovec iov{};
    iov.iov_base = chunk;
    iov.iov_len  = sizeof(chunk);
    msg.msg_iov  = &iov;
    msg.msg_iovlen = 1;
    char cbuf[CMSG_SPACE(16 * sizeof(int))];
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    ssize_t n = ::recvmsg(fd, &msg, MSG_DONTWAIT);
    if (n > 0) {
      buf.insert(buf.end(), chunk, chunk + n);
      for (struct cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr;
           c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
          const size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
          const auto* p = reinterpret_cast<const int*>(CMSG_DATA(c));
          fds.insert(fds.end(), p, p + count);
        }
      }
      continue;
    }
    if (n == 0) return false;  // peer closed
    if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
    if (errno == EINTR) continue;
    return false;
  }
}

int wait_for_io(int fd, short events, int timeoutMs) {
  struct pollfd pfd{};
  pfd.fd = fd;
  pfd.events = events;
  for (;;) {
    int r = ::poll(&pfd, 1, timeoutMs);
    if (r > 0) return r;
    if (r == 0) return 0;
    if (errno != EINTR) return -1;
  }
}

bool write_all(int fd, std::vector<uint8_t>& buf, int timeoutMs) {
  while (!buf.empty()) {
    ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
    if (n > 0) {
      buf.erase(buf.begin(), buf.begin() + n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (wait_for_io(fd, POLLOUT, timeoutMs) <= 0) return false;
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    return false;
  }
  return true;
}

} // namespace

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
  disconnect();
}

int IpcClient::connect(const std::string& path, int attempts) {
  if (fd_ >= 0) disconnect();

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const size_t pathLen = std::min(path.size(), sizeof(addr.sun_path) - 1);
  std::memcpy(addr.sun_path, path.data(), pathLen);
  addr.sun_path[pathLen] = '\0';

  for (int attempt = 0; attempt < attempts; ++attempt) {
    int sock = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) return -1;
    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
      const int flags = ::fcntl(sock, F_GETFL, 0);
      ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
      fd_ = sock;
      rbuf_.clear();
      wbuf_.clear();
      return fd_;
    }
    int saved = errno;
    ::close(sock);
    if (saved != ENOENT && saved != ECONNREFUSED) return -1;
    ::usleep(50 * 1000);
  }
  return -1;
}

void IpcClient::disconnect() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  for (int f : pendingFds_) ::close(f);
  pendingFds_.clear();
  rbuf_.clear();
  wbuf_.clear();
}

bool IpcClient::pump_frame(int timeoutMs, IpcFrame& out) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    size_t consumed = 0;
    if (!rbuf_.empty() && decode_frame(rbuf_.data(), rbuf_.size(), consumed, out)) {
      rbuf_.erase(rbuf_.begin(), rbuf_.begin() + consumed);
      return true;
    }
    if (consumed != 0) return false;  // corrupt buffer

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - Clock::now()).count();
    if (remaining <= 0) return false;
    const int r = wait_for_io(fd_, POLLIN, static_cast<int>(remaining));
    if (r <= 0) return false;
    if (!read_available(fd_, rbuf_, pendingFds_)) return false;
  }
}

bool IpcClient::wait_for_ack(uint64_t id, int timeoutMs) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - Clock::now()).count();
    if (remaining <= 0) return false;
    IpcFrame frame;
    if (!pump_frame(static_cast<int>(remaining), frame)) return false;
    if (frame.header.type == kTypeAck && frame.header.id == id) return true;
  }
}

void IpcClient::dispatch_event(const IpcFrame& frame) {
  if (!onEvent_) {
    if (frame.header.flags & kFlagFd) {
      for (int f : pendingFds_) ::close(f);
      pendingFds_.clear();
    }
    return;
  }
  std::vector<int> fds;
  if (frame.header.flags & kFlagFd) {
    fds = std::move(pendingFds_);
    pendingFds_.clear();
  }
  const auto nul = std::find(frame.body.begin(), frame.body.end(), '\0');
  std::string topic(frame.body.begin(), nul);
  std::string payload(nul == frame.body.end() ? frame.body.end() : nul + 1,
                      frame.body.end());
  onEvent_(std::move(topic), std::move(payload), std::move(fds));
}

std::optional<std::string> IpcClient::request(const std::string& command_line, int timeoutMs) {
  if (fd_ < 0) return std::nullopt;

  const uint64_t id = nextId_++;
  std::vector<uint8_t> enc;
  encode_frame(make_request(id, command_line), enc);
  if (!write_all(fd_, enc, timeoutMs)) return std::nullopt;

  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - Clock::now()).count();
    if (remaining <= 0) return std::nullopt;
    IpcFrame frame;
    if (!pump_frame(static_cast<int>(remaining), frame)) return std::nullopt;

    if (frame.header.type == kTypeResponse && frame.header.id == id) {
      return std::string(frame.body.begin(), frame.body.end());
    }
    if (frame.header.type == kTypeEvent) {
      dispatch_event(frame);
    }
  }
}

bool IpcClient::subscribe(const std::string& topic, int timeoutMs) {
  if (fd_ < 0) return false;

  const uint64_t id = nextId_++;
  std::vector<uint8_t> enc;
  IpcFrame sub;
  sub.header.version = kProtocolVersion;
  sub.header.type    = kTypeSubscribe;
  sub.header.id      = id;
  sub.body.assign(topic.begin(), topic.end());
  encode_frame(sub, enc);
  if (!write_all(fd_, enc, timeoutMs)) return false;
  return wait_for_ack(id, timeoutMs);
}

bool IpcClient::subscribe_async(const std::string& topic) {
  if (fd_ < 0) return false;

  const uint64_t id = nextId_++;
  std::vector<uint8_t> enc;
  IpcFrame sub;
  sub.header.version = kProtocolVersion;
  sub.header.type    = kTypeSubscribe;
  sub.header.id      = id;
  sub.body.assign(topic.begin(), topic.end());
  encode_frame(sub, enc);
  return write_all(fd_, enc, 3000);
}

bool IpcClient::unsubscribe(const std::string& topic, int timeoutMs) {
  if (fd_ < 0) return false;

  const uint64_t id = nextId_++;
  std::vector<uint8_t> enc;
  IpcFrame unsub;
  unsub.header.version = kProtocolVersion;
  unsub.header.type    = kTypeUnsubscribe;
  unsub.header.id      = id;
  unsub.body.assign(topic.begin(), topic.end());
  encode_frame(unsub, enc);
  if (!write_all(fd_, enc, timeoutMs)) return false;
  return wait_for_ack(id, timeoutMs);
}

bool IpcClient::publish(const std::string& topic, std::string_view payload, const std::vector<int>& /*fds*/) {
  if (fd_ < 0) return false;

  // Note: fds are accepted for API symmetry with IpcService::publish but the
  // client→server direction does not (yet) carry SCM_RIGHTS — the server's
  // recv path reads plain bytes. Senders that need to move bytes (e.g. album
  // art) should inline them in the payload (capped well under kMaxFrameBody).
  IpcFrame evt = make_event(topic, payload);
  std::vector<uint8_t> enc;
  encode_frame(evt, enc);
  return write_all(fd_, enc, 3000);
}

std::vector<int> IpcClient::poll_fds() const {
  if (fd_ >= 0) return {fd_};
  return {};
}

void IpcClient::on_fd_ready(int fd) {
  if (fd != fd_) return;
  if (!read_available(fd_, rbuf_, pendingFds_)) {
    disconnect();
    return;
  }
  for (;;) {
    size_t consumed = 0;
    IpcFrame frame;
    if (!decode_frame(rbuf_.data(), rbuf_.size(), consumed, frame)) {
      if (consumed != 0) {
        disconnect();  // corrupt stream
      }
      return;
    }
    rbuf_.erase(rbuf_.begin(), rbuf_.begin() + consumed);
    if (frame.header.type == kTypeEvent) {
      dispatch_event(frame);
    }
  }
}

} // namespace eh::ipc
