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

[[nodiscard]] bool isValidSocket(std::string_view path) {
  struct stat st{};
  return !path.empty() && ::stat(path.data(), &st) == 0 && S_ISSOCK(st.st_mode);
}

} // namespace

bool MangoRuntime::available() const {
  resolveIfPending();
  return !m_endpoint.empty();
}

const std::string& MangoRuntime::socketPath() const {
  resolveIfPending();
  return m_endpoint;
}

std::optional<nlohmann::json> MangoRuntime::request(std::string_view cmd) const {
  MANGOWM_DEBUG("mango request: '%.*s'", (int)cmd.size(), cmd.data());
  resolveIfPending();
  if (m_endpoint.empty() || cmd.empty()) { return std::nullopt; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { MANGOWM_ERROR("mango request: socket() errno=%d", errno); return std::nullopt; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_endpoint.size() >= sizeof(addr.sun_path)) {
    MANGOWM_ERROR("mango request: path too long (%zu)", m_endpoint.size());
    ::close(fd);
    return std::nullopt;
  }
  std::memcpy(addr.sun_path, m_endpoint.c_str(), m_endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    MANGOWM_ERROR("mango request: connect to '%s' failed errno=%d", m_endpoint.c_str(), errno);
    ::close(fd);
    return std::nullopt;
  }

  std::string payload(cmd);
  payload.push_back('\n');
  std::size_t off = 0;
  while (off < payload.size()) {
    ssize_t n = ::send(fd, payload.data() + off, payload.size() - off, MSG_NOSIGNAL);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      ::close(fd);
      return std::nullopt;
    }
    off += static_cast<std::size_t>(n);
  }

  std::string resp;
  std::array<char, 4096> buf;
  for (;;) {
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n > 0) {
      resp.append(buf.data(), static_cast<std::size_t>(n));
      if (resp.find('\n') != std::string::npos) { break; }
      continue;
    }
    if (n == 0) { break; }
    if (errno == EINTR) { continue; }
    ::close(fd);
    return std::nullopt;
  }

  ::close(fd);

  auto nl = resp.find('\n');
  if (nl != std::string::npos) { resp.resize(nl); }
  if (resp.empty()) { return std::nullopt; }

  try { return nlohmann::json::parse(resp); }
  catch (nlohmann::json::exception const&) { return std::nullopt; }
}

bool MangoRuntime::dispatch(std::string_view cmd) const {
  std::string text = "dispatch ";
  text += cmd;
  auto resp = request(text);
  if (!resp.has_value() || !resp->is_object()) { return false; }
  auto it = resp->find("success");
  return it != resp->end() && it->is_boolean() && it->get<bool>();
}

void MangoRuntime::refresh() {
  m_endpoint.clear();
  m_resolved = false;
  locateEndpoint();
}

void MangoRuntime::resolveIfPending() const {
  if (!m_resolved) { locateEndpoint(); }
}

void MangoRuntime::locateEndpoint() const {
  m_resolved = true;
  auto* sig = std::getenv("MANGO_INSTANCE_SIGNATURE");
  if (sig != nullptr && sig[0] != '\0' && isValidSocket(sig)) {
    m_endpoint = sig;
  }
}

} // namespace wspace::mango
