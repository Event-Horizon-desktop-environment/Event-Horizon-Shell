#include "backends/sway/sway_backends.h"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace wspace::sway {

bool SwayRuntime::canConnect() const {
  resolveIfPending();
  return !m_endpoint.empty();
}

const std::string& SwayRuntime::queryEndpoint() const {
  resolveIfPending();
  return m_endpoint;
}

std::optional<nlohmann::json> SwayRuntime::sendCommand(std::uint32_t type, std::string_view payload) const {
  auto reply = transmit(type, payload);
  if (reply.payload.empty()) { return std::nullopt; }
  try { return nlohmann::json::parse(reply.payload); }
  catch (nlohmann::json::exception const&) { return std::nullopt; }
}

void SwayRuntime::rescan() {
  m_endpoint.clear();
  m_resolved = false;
  locateEndpoint();
}

SwayRuntime::Frame SwayRuntime::transmit(std::uint32_t type, std::string_view payload) const {
  resolveIfPending();
  if (m_endpoint.empty()) { return {}; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return {}; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_endpoint.size() >= sizeof(addr.sun_path)) { ::close(fd); return {}; }
  std::memcpy(addr.sun_path, m_endpoint.c_str(), m_endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return {};
  }

  std::uint32_t plen = static_cast<std::uint32_t>(payload.size());
  std::vector<char> msg;
  msg.reserve(kIpcMagic.size() + sizeof(plen) + sizeof(type) + payload.size());
  msg.insert(msg.end(), kIpcMagic.begin(), kIpcMagic.end());
  msg.insert(msg.end(), reinterpret_cast<char const*>(&plen),
             reinterpret_cast<char const*>(&plen) + sizeof(plen));
  msg.insert(msg.end(), reinterpret_cast<char const*>(&type),
             reinterpret_cast<char const*>(&type) + sizeof(type));
  msg.insert(msg.end(), payload.begin(), payload.end());

  std::size_t off = 0;
  while (off < msg.size()) {
    ssize_t n = ::send(fd, msg.data() + off, msg.size() - off, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) { continue; }
      ::close(fd);
      return {};
    }
    off += static_cast<std::size_t>(n);
  }

  std::vector<char> hdr;
  hdr.resize(kIpcMagic.size() + sizeof(plen) + sizeof(type));
  std::size_t hgot = 0;
  while (hgot < hdr.size()) {
    pollfd pfd{fd, POLLIN, 0};
    int pr = ::poll(&pfd, 1, 3000);
    if (pr <= 0) { ::close(fd); return {}; }
    ssize_t n = ::recv(fd, hdr.data() + hgot, hdr.size() - hgot, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      ::close(fd);
      return {};
    }
    hgot += static_cast<std::size_t>(n);
  }

  if (!std::equal(kIpcMagic.begin(), kIpcMagic.end(), hdr.begin())) { ::close(fd); return {}; }

  std::uint32_t rlen = 0, rtype = 0;
  std::memcpy(&rlen, hdr.data() + kIpcMagic.size(), sizeof(rlen));
  std::memcpy(&rtype, hdr.data() + kIpcMagic.size() + sizeof(rlen), sizeof(rtype));

  std::string rpay;
  rpay.resize(rlen);
  std::size_t pg = 0;
  while (pg < rlen) {
    pollfd pfd{fd, POLLIN, 0};
    int pr = ::poll(&pfd, 1, 3000);
    if (pr <= 0) { ::close(fd); return {}; }
    ssize_t n = ::recv(fd, rpay.data() + pg, rlen - pg, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      ::close(fd);
      return {};
    }
    pg += static_cast<std::size_t>(n);
  }

  ::close(fd);
  return {rtype, std::move(rpay)};
}

void SwayRuntime::resolveIfPending() const {
  if (!m_resolved) { locateEndpoint(); }
}

void SwayRuntime::locateEndpoint() const {
  m_resolved = true;
  if (auto* s = std::getenv("SWAYSOCK"); s != nullptr && s[0] != '\0') { m_endpoint = s; return; }
  if (auto* s = std::getenv("I3SOCK"); s != nullptr && s[0] != '\0') { m_endpoint = s; }
}

}
