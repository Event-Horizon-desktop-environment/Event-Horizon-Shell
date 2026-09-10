#include "backends/triad/triad_backends.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace wspace::triad {

static bool pathIsSocket(std::string const& loc) {
  struct stat info;
  return !loc.empty() && ::stat(loc.c_str(), &info) == 0 && S_ISSOCK(info.st_mode);
}

static nlohmann::json buildEnvelope(std::string_view request) {
  return nlohmann::json{{"triad", {{"version", 1}, {"request", request}}}};
}

bool TriadRuntime::canConnect() const {
  resolveOnDemand();
  return !m_endpoint.empty();
}

std::string const& TriadRuntime::queryEndpoint() const {
  resolveOnDemand();
  return m_endpoint;
}

std::optional<nlohmann::json> TriadRuntime::sendQuery(std::string_view request) const {
  return sendPayload(buildEnvelope(request));
}

std::optional<nlohmann::json> TriadRuntime::sendPayload(nlohmann::json body) const {
  auto serial = body.dump();
  serial.push_back('\n');
  auto result = performExchange(serial);
  if (!result.data || !result.data->is_object()) return std::nullopt;
  auto const& reply = *result.data;
  auto const okField = reply.find("ok");
  if (okField != reply.end() && okField->is_boolean() && !okField->get<bool>()) return std::nullopt;
  return reply;
}

bool TriadRuntime::dispatchAction(std::string_view name, nlohmann::json parameters) const {
  if (!parameters.is_object()) parameters = nlohmann::json::object();
  parameters["version"] = 1;
  parameters["request"] = "action";
  parameters["action"] = name;
  return sendPayload(nlohmann::json{{"triad", std::move(parameters)}}).has_value();
}

TriadRuntime::ExchangeResult TriadRuntime::performExchange(std::string_view message) const {
  resolveOnDemand();
  if (m_endpoint.empty() || message.empty()) return {TransferOutcome::SocketMissing, std::nullopt};

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return {TransferOutcome::SocketMissing, std::nullopt};

  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  if (m_endpoint.size() >= sizeof(addr.sun_path)) { ::close(fd); return {TransferOutcome::SocketMissing, std::nullopt}; }
  std::memcpy(addr.sun_path, m_endpoint.c_str(), m_endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return {TransferOutcome::SocketMissing, std::nullopt};
  }

  std::size_t sent = 0;
  while (sent < message.size()) {
    ssize_t n = ::write(fd, message.data() + sent, message.size() - sent);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      ::close(fd);
      return {TransferOutcome::TransmitFailure, std::nullopt};
    }
    sent += static_cast<std::size_t>(n);
  }

  std::string accumulated;
  char chunk[4096];
  for (;;) {
    ssize_t n = ::read(fd, chunk, sizeof(chunk));
    if (n > 0) {
      accumulated.append(chunk, static_cast<std::size_t>(n));
      if (accumulated.find('\n') != std::string::npos) break;
      continue;
    }
    if (n == 0) break;
    if (errno == EINTR) continue;
    ::close(fd);
    return {TransferOutcome::ReceiveFailure, std::nullopt};
  }

  ::close(fd);

  auto nl = accumulated.find('\n');
  if (nl != std::string::npos) accumulated.resize(nl);
  if (accumulated.empty()) return {TransferOutcome::ParseError, std::nullopt};

  try {
    return {TransferOutcome::Success, nlohmann::json::parse(accumulated)};
  } catch (nlohmann::json::exception const&) {
    return {TransferOutcome::ParseError, std::nullopt};
  }
}

void TriadRuntime::rescan() {
  m_endpoint.clear();
  m_initialized = false;
  lookupSocket();
}

void TriadRuntime::resolveOnDemand() const {
  if (!m_initialized) {
    m_initialized = true;
    lookupSocket();
  }
}

void TriadRuntime::lookupSocket() const {
  m_initialized = true;
  if (auto* env = std::getenv("TRIAD_SOCKET"); env && env[0]) {
    m_endpoint = env;
    return;
  }
  auto* dir = std::getenv("XDG_RUNTIME_DIR");
  if (!dir || !dir[0]) return;
  auto candidate = std::string(dir) + "/triad.sock";
  if (pathIsSocket(candidate)) m_endpoint = std::move(candidate);
}

} // namespace wspace::triad
