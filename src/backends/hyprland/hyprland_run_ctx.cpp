#include "backends/hyprland/hyprland_backends.h"

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
#include <unistd.h>

namespace wspace::hyprland {

namespace {

void logWarning(const std::string& msg) {
  std::cerr << "[hyprland] " << msg << "\n";
}

}

HyprlandRuntime::HyprlandRuntime() {
  resolveIfPending();
  readConfig();
}

HyprlandRuntime::~HyprlandRuntime() {
  shutdown();
}

bool HyprlandRuntime::startSession() {
  if (m_sockets.ev.empty()) { return false; }
  shutdown();

  m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (m_fd < 0) {
    logWarning(std::string("failed to create hyprland IPC socket: ") + std::strerror(errno));
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_sockets.ev.size() >= sizeof(addr.sun_path)) {
    logWarning("hyprland IPC socket path too long");
    shutdown();
    return false;
  }
  std::memcpy(addr.sun_path, m_sockets.ev.c_str(), m_sockets.ev.size() + 1);

  if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    logWarning(std::string("failed to connect to hyprland IPC ") + m_sockets.ev + ": " + std::strerror(errno));
    shutdown();
    return false;
  }

  int flags = ::fcntl(m_fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
  }
  return true;
}

void HyprlandRuntime::handlePoll(short revents) {
  if (m_fd < 0) { return; }
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    logWarning("hyprland IPC disconnected");
    shutdown();
    for (auto* h : m_listeners) { h->onStateChange(); }
    return;
  }
  if ((revents & POLLIN) != 0) { drainSocket(); }
}

void HyprlandRuntime::shutdown() {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  m_buffer.clear();
  for (auto* h : m_listeners) { h->onCleanup(); }
}

std::optional<std::string> HyprlandRuntime::sendCommand(std::string_view command) const {
  resolveIfPending();
  if (m_sockets.req.empty() || command.empty()) { return std::nullopt; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return std::nullopt; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_sockets.req.size() >= sizeof(addr.sun_path)) { ::close(fd); return std::nullopt; }
  std::memcpy(addr.sun_path, m_sockets.req.c_str(), m_sockets.req.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(fd); return std::nullopt; }

  std::size_t off = 0;
  while (off < command.size()) {
    auto written = ::send(fd, command.data() + off, command.size() - off, MSG_NOSIGNAL);
    if (written <= 0) {
      if (written < 0 && errno == EINTR) { continue; }
      ::close(fd); return std::nullopt;
    }
    off += static_cast<std::size_t>(written);
  }

  ::shutdown(fd, SHUT_WR);

  std::string result;
  std::array<char, 4096> buf;
  while (true) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 3000) <= 0) { ::close(fd); return std::nullopt; }
    auto n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n > 0) { result.append(buf.data(), buf.data() + n); continue; }
    if (n == 0) { break; }
    if (errno == EINTR) { continue; }
    ::close(fd); return std::nullopt;
  }
  ::close(fd);
  return result;
}

std::optional<nlohmann::json> HyprlandRuntime::sendQuery(std::string_view command) const {
  auto resp = sendCommand(command);
  if (!resp.has_value() || resp->empty()) { return std::nullopt; }
  try {
    return nlohmann::json::parse(*resp);
  } catch (const nlohmann::json::exception& e) {
    logWarning(std::string("failed to parse hyprland response for ") + std::string(command) + ": " + e.what());
    return std::nullopt;
  }
}

void HyprlandRuntime::rescan() {
  m_sockets = {};
  m_pathsKnown = false;
  m_luaMode = false;
  locateEndpoints();
}

void HyprlandRuntime::resolveIfPending() const {
  if (!m_pathsKnown) { locateEndpoints(); }
}

void HyprlandRuntime::locateEndpoints() const {
  m_pathsKnown = true;
  auto* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (sig == nullptr || sig[0] == '\0') { return; }

  std::string dir;
  auto* rtDir = std::getenv("XDG_RUNTIME_DIR");
  if (rtDir != nullptr && rtDir[0] != '\0') { dir = std::string(rtDir) + "/hypr/" + sig; }

  std::error_code ec;
  if (dir.empty() || !std::filesystem::is_directory(dir, ec)) { dir = std::string("/tmp/hypr/") + sig; }

  ec.clear();
  if (!std::filesystem::is_directory(dir, ec)) { return; }

  m_sockets = SocketPaths{
    .req = dir + "/.socket.sock",
    .ev = dir + "/.socket2.sock",
  };
}

void HyprlandRuntime::readConfig() {
  auto json = sendQuery("j/status");
  if (!json || !json->is_object()) { return; }
  if (json->value("configProvider", "") == "lua") { m_luaMode = true; }
}

void HyprlandRuntime::registerEventHandler(HyprlandEventHandler* handler) {
  m_listeners.push_back(handler);
}

void HyprlandRuntime::unregisterEventHandler(HyprlandEventHandler* handler) {
  m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), handler), m_listeners.end());
}

void HyprlandRuntime::routeEvent(std::string_view line) {
  auto split = line.find(">>");
  if (split == std::string_view::npos) { return; }
  auto ev = line.substr(0, split);
  auto payload = line.substr(split + 2);
  for (auto* h : m_listeners) { h->onEvent(ev, payload); }
}

void HyprlandRuntime::drainSocket() {
  if (m_fd < 0) { return; }
  std::array<char, 4096> buf;
  while (true) {
    auto n = ::recv(m_fd, buf.data(), buf.size(), MSG_DONTWAIT);
    if (n > 0) { m_buffer.insert(m_buffer.end(), buf.data(), buf.data() + n); continue; }
    if (n == 0) { shutdown(); return; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
    if (errno == EINTR) { continue; }
    logWarning(std::string("failed to read from hyprland IPC: ") + std::strerror(errno));
    shutdown();
    return;
  }
  processLines();
}

void HyprlandRuntime::processLines() {
  while (true) {
    auto it = std::find(m_buffer.begin(), m_buffer.end(), '\n');
    if (it == m_buffer.end()) { return; }
    std::string line(m_buffer.begin(), it);
    m_buffer.erase(m_buffer.begin(), it + 1);
    if (!line.empty()) { routeEvent(line); }
  }
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

static bool resolveHyprSocketPaths(std::string& req, std::string& ev) {
  auto* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (sig == nullptr || sig[0] == '\0') { return false; }

  std::string dir;
  auto* rtDir = std::getenv("XDG_RUNTIME_DIR");
  if (rtDir != nullptr && rtDir[0] != '\0') { dir = std::string(rtDir) + "/hypr/" + sig; }

  std::error_code ec;
  if (dir.empty() || !std::filesystem::is_directory(dir, ec)) { dir = std::string("/tmp/hypr/") + sig; }
  ec.clear();
  if (!std::filesystem::is_directory(dir, ec)) { return false; }

  req = dir + "/.socket.sock";
  ev = dir + "/.socket2.sock";
  return true;
}

bool hyprland_ipc_send(std::string_view command) {
  return hyprland_ipc_request(command).has_value();
}

std::optional<std::string> hyprland_ipc_request(std::string_view command) {
  std::string req, ev;
  if (!resolveHyprSocketPaths(req, ev) || req.empty() || command.empty()) { return std::nullopt; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return std::nullopt; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (req.size() >= sizeof(addr.sun_path)) { ::close(fd); return std::nullopt; }
  std::memcpy(addr.sun_path, req.c_str(), req.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(fd); return std::nullopt; }

  std::size_t off = 0;
  while (off < command.size()) {
    auto w = ::send(fd, command.data() + off, command.size() - off, MSG_NOSIGNAL);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) { continue; }
      ::close(fd); return std::nullopt;
    }
    off += static_cast<std::size_t>(w);
  }

  ::shutdown(fd, SHUT_WR);

  std::string result;
  std::array<char, 4096> buf;
  while (true) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 3000) <= 0) { ::close(fd); return std::nullopt; }
    auto n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n > 0) { result.append(buf.data(), buf.data() + n); continue; }
    if (n == 0) { break; }
    if (errno == EINTR) { continue; }
    ::close(fd); return std::nullopt;
  }

  ::close(fd);
  return result;
}
