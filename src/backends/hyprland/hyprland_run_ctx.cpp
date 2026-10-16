#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/common/log/debug_log.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace {

// Wires an existing AF_UNIX stream socket to `path`. `err` receives a
// short description when the wiring fails, for the caller's log line.
bool connectFdToPath(int fd, std::string const& path, std::string& err) {
  if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
    err = "path exceeds sun_path limit";
    return false;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    err = std::strerror(errno);
    return false;
  }
  return true;
}

// Splits "NAME>>PAYLOAD"; false when the separator is missing.
bool splitEventLine(std::string_view line, std::string_view& name, std::string_view& body) {
  const auto sep = line.find(">>");
  if (sep == std::string_view::npos) { return false; }
  name = line.substr(0, sep);
  body = line.substr(sep + 2);
  return true;
}

// Locates the two IPC sockets of the running compositor instance.
// Fills `req`/`ev` when a live instance directory is found.
bool resolveHyprSocketPaths(std::string& req, std::string& ev) {
  auto* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (sig == nullptr || sig[0] == '\0') { return false; }

  const std::string suffix = std::string("/hypr/") + sig;
  std::error_code ec;

  std::string dir;
  if (auto* rtDir = std::getenv("XDG_RUNTIME_DIR"); rtDir != nullptr && rtDir[0] != '\0') {
    dir = std::string(rtDir) + suffix;
    if (!std::filesystem::is_directory(dir, ec)) { dir.clear(); }
  }
  if (dir.empty()) {
    dir = std::string("/tmp") + suffix;
    ec.clear();
    if (!std::filesystem::is_directory(dir, ec)) { return false; }
  }

  req = dir + "/.socket.sock";
  ev = dir + "/.socket2.sock";
  return true;
}

// One-shot request/response over a Unix socket: connect, send, half-close,
// collect until EOF. Returns empty on any failure. Both the session method
// and the free function below share it so the wire logic lives once.
std::optional<std::string> requestOverSocket(std::string const& path, std::string_view command,
                                             int timeoutMs) {
  if (path.empty() || command.empty()) { return std::nullopt; }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return std::nullopt; }

  std::string err;
  if (!connectFdToPath(fd, path, err)) {
    ::close(fd);
    return std::nullopt;
  }

  std::size_t off = 0;
  while (off < command.size()) {
    auto written = ::send(fd, command.data() + off, command.size() - off, MSG_NOSIGNAL);
    if (written > 0) {
      off += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) { continue; }
    ::close(fd);
    return std::nullopt;
  }

  ::shutdown(fd, SHUT_WR);

  std::string result;
  std::array<char, 4096> buf;
  for (;;) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, timeoutMs) <= 0) {
      ::close(fd);
      return std::nullopt;
    }
    auto n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n < 0) {
      if (errno == EINTR) { continue; }
      ::close(fd);
      return std::nullopt;
    }
    if (n == 0) { break; }
    result.append(buf.data(), buf.data() + n);
  }
  ::close(fd);
  return result;
}

} // namespace

namespace wspace::hyprland {

namespace {

void logWarning(const std::string& msg) {
  debug_log("hyprland", "%s", msg.c_str());
}

}

HyprlandSession::HyprlandSession() {
  resolveIfPending();
  readConfig();
}

HyprlandSession::~HyprlandSession() {
  shutdown();
}

bool HyprlandSession::startSession() {
  if (m_sockets.ev.empty()) { return false; }
  shutdown();

  m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (m_fd < 0) {
    logWarning(std::string("failed to create hyprland IPC socket: ") + std::strerror(errno));
    return false;
  }

  std::string err;
  if (!connectFdToPath(m_fd, m_sockets.ev, err)) {
    logWarning("hyprland event socket connect failed (" + err + "): " + m_sockets.ev);
    shutdown();
    return false;
  }

  const int flags = ::fcntl(m_fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
  }
  return true;
}

void HyprlandSession::handlePoll(short revents) {
  if (m_fd < 0) { return; }
  constexpr short kFatal = POLLERR | POLLHUP | POLLNVAL;
  if ((revents & kFatal) == 0) {
    if ((revents & POLLIN) != 0) { drainSocket(); }
    return;
  }
  logWarning("hyprland event stream dropped");
  shutdown();
  for (auto* sink : m_listeners) { sink->onStateChange(); }
}

void HyprlandSession::shutdown() {
  const int stale = std::exchange(m_fd, -1);
  if (stale >= 0) { ::close(stale); }
  decltype(m_buffer){}.swap(m_buffer);
  std::ranges::for_each(m_listeners, [](HyprlandEventSink* sink) { sink->onCleanup(); });
}

std::optional<std::string> HyprlandSession::sendCommand(std::string_view command) const {
  resolveIfPending();
  return requestOverSocket(m_sockets.req, command, 3000);
}

std::optional<nlohmann::json> HyprlandSession::sendQuery(std::string_view command) const {
  const auto raw = sendCommand(command);
  if (!raw || raw->empty()) { return std::nullopt; }
  auto parsed = nlohmann::json::parse(*raw, nullptr, false);
  if (parsed.is_discarded()) {
    logWarning(std::string("hyprland query \"") + std::string(command) + "\" returned bad JSON");
    return std::nullopt;
  }
  return parsed;
}

void HyprlandSession::rescan() {
  m_sockets = {};
  m_pathsKnown = false;
  m_luaMode = false;
  locateEndpoints();
}

void HyprlandSession::resolveIfPending() const {
  if (!m_pathsKnown) { locateEndpoints(); }
}

void HyprlandSession::locateEndpoints() const {
  m_pathsKnown = true;
  std::string req, ev;
  if (!resolveHyprSocketPaths(req, ev)) { return; }
  m_sockets = SocketPaths{.req = std::move(req), .ev = std::move(ev)};
}

void HyprlandSession::readConfig() {
  const auto status = sendQuery("j/status");
  if (status && status->is_object()) {
    m_luaMode = status->value("configProvider", "") == "lua";
  }
}

void HyprlandSession::addEventSink(HyprlandEventSink* handler) {
  m_listeners.push_back(handler);
}

void HyprlandSession::removeEventSink(HyprlandEventSink* handler) {
  std::erase(m_listeners, handler);
}

void HyprlandSession::routeEvent(std::string_view line) {
  // Events arrive as "<name>><><payload>"; anything without the separator
  // is malformed and dropped.
  std::string_view name, body;
  if (!splitEventLine(line, name, body)) { return; }
  for (auto* sink : m_listeners) { sink->onEvent(name, body); }
}

void HyprlandSession::drainSocket() {
  if (m_fd < 0) { return; }
  std::array<char, 4096> chunk;
  bool peerGone = false;
  while (!peerGone) {
    const auto got = ::recv(m_fd, chunk.data(), chunk.size(), MSG_DONTWAIT);
    if (got > 0) {
      m_buffer.insert(m_buffer.end(), chunk.begin(), chunk.begin() + got);
      continue;
    }
    if (got == 0) { peerGone = true; break; }
    if (errno == EINTR) { continue; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
    logWarning(std::string("hyprland IPC read error: ") + std::strerror(errno));
    shutdown();
    return;
  }
  if (peerGone) { shutdown(); return; }
  processLines();
}

void HyprlandSession::processLines() {
  auto consumed = m_buffer.begin();
  for (;;) {
    auto nl = std::find(consumed, m_buffer.end(), '\n');
    if (nl == m_buffer.end()) { break; }
    if (nl > consumed) { routeEvent(std::string_view(consumed.base(), static_cast<std::size_t>(nl - consumed))); }
    consumed = std::next(nl);
  }
  if (consumed != m_buffer.begin()) { m_buffer.erase(m_buffer.begin(), consumed); }
}

bool hyprland_config_is_lua() {
  auto resp = ::hyprland_ipc_request("j/status");
  if (!resp) { return false; }
  try {
    auto json = nlohmann::json::parse(*resp);
    return json.is_object() && json.value("configProvider", "") == "lua";
  } catch (...) { return false; }
}

}

bool hyprland_ipc_send(std::string_view command) {
  return hyprland_ipc_request(command).has_value();
}

std::optional<std::string> hyprland_ipc_request(std::string_view command) {
  std::string req, ev;
  if (!resolveHyprSocketPaths(req, ev)) { return std::nullopt; }
  return requestOverSocket(req, command, 3000);
}
