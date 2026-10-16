#include "backends/niri/niri_backends.h"
#include "desktop_shell/common/log/debug_log.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// Opens a stream socket wired to `path`; -1 when any step refuses.
int dialUnix(std::string_view path) {
  sockaddr_un addr{};
  if (path.size() >= sizeof(addr.sun_path)) { return -1; }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return -1; }

  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.data(), path.size());

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Streams `data` out in full; false on a hard error (errno left live),
// true once every byte is written or the stream stalled on EINTR.
bool pumpOut(int fd, std::string_view data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n > 0) { off += static_cast<std::size_t>(n); continue; }
    if (n < 0 && errno == EINTR) { continue; }
    return false;
  }
  return true;
}

// How pumpIn() ended: a complete line, an orderly close, or a hard error.
enum class Drain { Line, PeerGone, Fault };

Drain pumpIn(int fd, std::string& out) {
  std::array<char, 4096> buf;
  for (;;) {
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n > 0) {
      out.append(buf.data(), static_cast<std::size_t>(n));
      if (out.find('\n') != std::string::npos) { return Drain::Line; }
      continue;
    }
    if (n == 0) { return Drain::PeerGone; }
    if (errno == EINTR) { continue; }
    return Drain::Fault;
  }
}

} // namespace

struct NiriSession::ReplyInfo {
  enum class Tag { None, WriteFail, ReadFail, NoContent, BadJson, Ok };
  Tag t = Tag::None;
  std::optional<nlohmann::json> data;
};

bool NiriSession::canConnect() const {
  resolveIfPending();
  return !m_endpoint.empty();
}

const std::string& NiriSession::queryEndpoint() const {
  resolveIfPending();
  return m_endpoint;
}

std::optional<nlohmann::json> NiriSession::sendQuery(std::string_view req) const {
  return transmit(req).data;
}

bool NiriSession::sendCheck(std::string_view req, bool allowNoResponse) const {
  const auto reply = transmit(req);
  if (reply.t != ReplyInfo::Tag::Ok) {
    return reply.t == ReplyInfo::Tag::NoContent && allowNoResponse;
  }
  return reply.data.has_value() && reply.data->is_object() && reply.data->contains("Ok");
}

bool NiriSession::dispatchAction(const nlohmann::json& action, bool allowNoResponse) const {
  nlohmann::json req;
  req.emplace("Action", action);
  return sendCheck(req.dump() + '\n', allowNoResponse);
}

NiriSession::ReplyInfo NiriSession::transmit(std::string_view req) const {
  resolveIfPending();
  if (m_endpoint.empty() || req.empty()) { return {ReplyInfo::Tag::None, std::nullopt}; }

  const int fd = dialUnix(m_endpoint);
  if (fd < 0) { return {ReplyInfo::Tag::None, std::nullopt}; }

  if (!pumpOut(fd, req)) {
    const int e = errno;
    ::close(fd);
    debug_log("niri", "tx write failed: %s", std::strerror(e));
    return {ReplyInfo::Tag::WriteFail, std::nullopt};
  }

  std::string resp;
  const auto drain = pumpIn(fd, resp);
  if (drain == Drain::Fault) {
    const int e = errno;
    ::close(fd);
    debug_log("niri", "tx read failed: %s", std::strerror(e));
    return {ReplyInfo::Tag::ReadFail, std::nullopt};
  }
  ::close(fd);

  auto nl = resp.find('\n');
  if (nl != std::string::npos) { resp.resize(nl); }
  if (resp.empty()) { return {ReplyInfo::Tag::NoContent, std::nullopt}; }

  auto parsed = nlohmann::json::parse(resp, nullptr, false);
  if (parsed.is_discarded()) {
    debug_log("niri", "reply JSON parse failed: %s", resp.substr(0, 160).c_str());
    return {ReplyInfo::Tag::BadJson, std::nullopt};
  }
  return {ReplyInfo::Tag::Ok, std::move(parsed)};
}

void NiriSession::rescan() {
  m_endpoint.clear();
  m_resolved = false;
  locateEndpoint();
}

void NiriSession::resolveIfPending() const {
  if (!m_resolved) { locateEndpoint(); }
}

void NiriSession::locateEndpoint() const {
  auto* env = std::getenv("NIRI_SOCKET");
  if (env != nullptr) {
    const std::string_view candidate = env;
    if (!candidate.empty()) { m_endpoint = candidate; }
  }
  m_resolved = true;
}
