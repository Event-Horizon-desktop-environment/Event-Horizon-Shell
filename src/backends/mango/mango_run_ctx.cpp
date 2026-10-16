#include "backends/mango/mango_backends.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace wspace::mango {

namespace {

[[nodiscard]] bool isUsableSocket(std::string_view path) {
  if (path.empty()) { return false; }
  struct stat st{};
  if (::stat(path.data(), &st) != 0) { return false; }
  return S_ISSOCK(st.st_mode);
}

// Connected stream socket, or -1. The caller owns the fd.
[[nodiscard]] int openSessionSocket(std::string const& endpoint) {
  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    MANGOWM_ERROR("mango ipc: socket() errno=%d", errno);
    return -1;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (endpoint.size() >= sizeof(addr.sun_path)) {
    MANGOWM_ERROR("mango ipc: path too long (%zu)", endpoint.size());
    ::close(fd);
    return -1;
  }
  std::memcpy(addr.sun_path, endpoint.c_str(), endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    MANGOWM_ERROR("mango ipc: connect to '%s' failed errno=%d", endpoint.c_str(), errno);
    ::close(fd);
    return -1;
  }
  return fd;
}

[[nodiscard]] bool sendAll(int fd, std::string const& payload) {
  std::size_t off = 0;
  while (off < payload.size()) {
    ssize_t n = ::send(fd, payload.data() + off, payload.size() - off, MSG_NOSIGNAL);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) { continue; }
    return false;
  }
  return true;
}

// First line off the socket, without the newline. Empty when the peer
// closed early, errored, or sent nothing readable.
[[nodiscard]] std::string readLine(int fd) {
  std::string resp;
  std::array<char, 4096> buf;
  for (;;) {
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR) { continue; }
      return {};
    }
    if (n == 0) { break; }
    resp.append(buf.data(), static_cast<std::size_t>(n));
    if (resp.find('\n') != std::string::npos) { break; }
  }

  auto nl = resp.find('\n');
  if (nl != std::string::npos) { resp.resize(nl); }
  return resp;
}

} // namespace

void MangoSession::locateEndpoint() const {
  m_resolved = true;
  m_endpoint.clear();
  auto* sig = std::getenv("MANGO_INSTANCE_SIGNATURE");
  if (sig == nullptr || sig[0] == '\0') { return; }
  if (!isUsableSocket(sig)) { return; }
  m_endpoint = sig;
}

void MangoSession::resolveIfPending() const {
  if (!m_resolved) { locateEndpoint(); }
}

void MangoSession::refresh() {
  m_endpoint.clear();
  m_resolved = false;
  locateEndpoint();
}

bool MangoSession::available() const {
  resolveIfPending();
  return !m_endpoint.empty();
}

const std::string& MangoSession::socketPath() const {
  resolveIfPending();
  return m_endpoint;
}

std::optional<nlohmann::json> MangoSession::ipcQuery(std::string_view cmd) const {
  MANGOWM_DEBUG("mango ipc: '%.*s'", (int)cmd.size(), cmd.data());
  resolveIfPending();
  if (m_endpoint.empty() || cmd.empty()) { return std::nullopt; }

  int fd = openSessionSocket(m_endpoint);
  if (fd < 0) { return std::nullopt; }

  std::string payload(cmd);
  payload.push_back('\n');
  if (!sendAll(fd, payload)) {
    ::close(fd);
    return std::nullopt;
  }

  std::string resp = readLine(fd);
  ::close(fd);
  if (resp.empty()) { return std::nullopt; }

  try { return nlohmann::json::parse(resp); }
  catch (nlohmann::json::exception const&) { return std::nullopt; }
}

bool MangoSession::runCommand(std::string_view cmd) const {
  const auto resp = ipcQuery("dispatch " + std::string{cmd});
  if (!resp || !resp->is_object()) { return false; }

  const auto reported = resp->find("success");
  if (reported == resp->end() || !reported->is_boolean()) { return false; }
  return reported->get<bool>();
}

} // namespace wspace::mango
