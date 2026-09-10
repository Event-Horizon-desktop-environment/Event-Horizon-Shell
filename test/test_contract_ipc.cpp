#include "services/ipc/client.hpp"
#include "services/ipc/ipc_protocol.hpp"
#include "services/ipc/ipc_server.hpp"
#include "services/windows/toplevel_client.hpp"
#include "services/windows/toplevel_service.hpp"
#include "services/windows/toplevel_types.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

namespace {

int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";                   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

std::string socket_path() {
  return "/tmp/eh-test-ipc-" + std::to_string(::getpid()) + ".sock";
}

// Mimic the production poll loop (PollMuxLoop build_handlers + on_fd_ready).
void drive_server(eh::ipc::IpcService& srv, int timeoutMs) {
  auto interests = srv.poll_interests();
  std::vector<struct pollfd> pfds;
  pfds.reserve(interests.size());
  for (const auto& i : interests) {
    pfds.push_back({i.fd, i.events, 0});
  }
  int r = ::poll(pfds.data(), pfds.size(), timeoutMs);
  if (r <= 0) return;
  for (size_t i = 0; i < pfds.size(); ++i) {
    if (pfds[i].revents != 0) {
      srv.on_fd_ready(pfds[i].fd, pfds[i].revents);
    }
  }
}

std::string read_until_eof(int fd) {
  std::string out;
  char buf[256];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

void test_protocol_encoding() {
  CHECK(sizeof(eh::ipc::IpcHeader) == 14);

  eh::ipc::IpcFrame req = eh::ipc::make_request(7, "ping");
  CHECK(req.header.type == eh::ipc::kTypeRequest);
  CHECK(req.header.id == 7);
  CHECK(std::string(req.body.begin(), req.body.end()) == "ping");

  std::vector<uint8_t> enc;
  eh::ipc::encode_frame(req, enc);
  CHECK(enc.size() == 5 + 14 + 4);

  eh::ipc::IpcFrame parsed;
  size_t consumed = 0;
  CHECK(eh::ipc::decode_frame(enc.data(), enc.size(), consumed, parsed));
  CHECK(consumed == enc.size());
  CHECK(parsed.header.type == eh::ipc::kTypeRequest);
  CHECK(parsed.header.id == 7);
  CHECK(std::string(parsed.body.begin(), parsed.body.end()) == "ping");

  eh::ipc::IpcFrame evt = eh::ipc::make_event("config.applied", "theme=dark");
  CHECK(evt.header.type == eh::ipc::kTypeEvent);
  const std::string evtBody(evt.body.begin(), evt.body.end());
  CHECK(evtBody == std::string("config.applied") + '\0' + "theme=dark");

  std::vector<uint8_t> enc2;
  eh::ipc::encode_frame(evt, enc2);
  // Incomplete frame: only magic + partial length must not decode.
  CHECK(!eh::ipc::decode_frame(enc2.data(), 3, consumed, parsed));
}

void test_legacy_shim(const std::string& path) {
  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  CHECK(::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
  const std::string cmd = "ping\n";
  CHECK(::write(fd, cmd.data(), cmd.size()) == static_cast<ssize_t>(cmd.size()));
  CHECK(read_until_eof(fd) == "pong\n");
  ::close(fd);

  fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  CHECK(::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
  const std::string bad = "nope-command\n";
  CHECK(::write(fd, bad.data(), bad.size()) == static_cast<ssize_t>(bad.size()));
  CHECK(read_until_eof(fd) == "error unknown command: nope-command\n");
  ::close(fd);
}

} // namespace

void test_toplevel_codec() {
  using namespace eh::windows;

  ToplevelRecord r;
  r.id = 42;
  r.flags = kActivated | kMaximized;
  r.appId = "org.gnome.Nautilus";
  r.title = std::string("Files (with\todd\n chars) ;") + '\0';

  std::string enc;
  toplevel_encode_record(r, enc);
  ToplevelRecord back;
  size_t consumed = 0;
  CHECK(toplevel_decode_record(enc, 0, back, consumed));
  CHECK(consumed == enc.size());
  CHECK(back.id == 42);
  CHECK(back.flags == (kActivated | kMaximized));
  CHECK(back.appId == r.appId);
  CHECK(back.title == r.title);
  CHECK(back.key.empty());

  std::vector<ToplevelRecord> list;
  list.push_back(r);
  ToplevelRecord r2;
  r2.id = 7;
  r2.flags = kMinimized | kFullscreen;
  r2.appId = "kitty";
  r2.title = "zsh";
  list.push_back(r2);

  std::string lenc;
  toplevel_encode_list(list, lenc);
  std::vector<ToplevelRecord> lback;
  CHECK(toplevel_decode_list(lenc, lback));
  CHECK(lback.size() == 2);
  CHECK(lback[0].id == 42);
  CHECK(lback[0].appId == r.appId);
  CHECK(lback[0].title == r.title);
  CHECK(lback[1].id == 7);
  CHECK(lback[1].flags == (kMinimized | kFullscreen));

  // Malformed list: declared count but truncated record must fail cleanly.
  std::string bad = "1\n7;1;3;ab";  // title length 3 but only "ab" present
  std::vector<ToplevelRecord> badOut;
  CHECK(!toplevel_decode_list(bad, badOut));
  CHECK(badOut.empty());

  // Empty snapshot round trips.
  std::string empty;
  toplevel_encode_list({}, empty);
  CHECK(toplevel_decode_list(empty, badOut));
  CHECK(badOut.empty());
}

void test_toplevel_service_roundtrip(eh::ipc::IpcService& srv, const std::string& path) {
  using namespace eh::windows;

  ToplevelService service(srv);
  service.register_handlers();

  ToplevelClient tc;
  int changeCount = 0;
  tc.set_change_handler([&](const ToplevelClient&) { ++changeCount; });
  CHECK(tc.start(path));
  CHECK(tc.list().empty());

  // Pump the client's socket until `predicate` holds or ~4s elapse.
  auto pump_until = [&](auto predicate) {
    for (int i = 0; i < 200; ++i) {
      if (predicate()) return true;
      const auto fds = tc.poll_fds();
      struct pollfd pfd{fds.empty() ? -1 : fds[0], POLLIN, 0};
      if (::poll(&pfd, 1, 20) > 0 && pfd.revents != 0) tc.on_fd_ready(pfd.fd);
    }
    return predicate();
  };

  // Publish a snapshot with two windows; the client mirrors them.
  std::vector<ToplevelRecord> snap;
  {
    ToplevelRecord r;
    r.key = "w:0x1";
    r.appId = "firefox";
    r.title = "Mozilla";
    snap.push_back(std::move(r));
  }
  {
    ToplevelRecord r;
    r.key = "w:0x2";
    r.appId = "kitty";
    r.title = "zsh";
    r.flags = kActivated;
    snap.push_back(std::move(r));
  }
  service.set_snapshot(std::move(snap));

  CHECK(pump_until([&] { return tc.list().size() == 2; }));
  CHECK(changeCount >= 1);

  const ToplevelRecord* firefox = tc.find(1);
  const ToplevelRecord* kitty = tc.find(2);
  CHECK(firefox != nullptr);
  CHECK(kitty != nullptr);
  if (firefox) {
    CHECK(firefox->appId == "firefox");
    CHECK(firefox->title == "Mozilla");
    CHECK(firefox->flags == 0);
  }
  if (kitty) {
    CHECK(kitty->appId == "kitty");
    CHECK((kitty->flags & kActivated) != 0);
  }
  CHECK(tc.find(99) == nullptr);

  // Update: kitty's title changes and firefox is minimized.
  {
    std::vector<ToplevelRecord> snap2;
    ToplevelRecord r;
    r.key = "w:0x1";
    r.appId = "firefox";
    r.title = "Mozilla";
    r.flags = kMinimized;
    snap2.push_back(std::move(r));
    r.key = "w:0x2";
    r.appId = "kitty";
    r.title = "bash";
    r.flags = kActivated | kMaximized;
    snap2.push_back(std::move(r));
    service.set_snapshot(std::move(snap2));
  }
  CHECK(pump_until([&] { return tc.find(2) && tc.find(2)->title == "bash"; }));
  CHECK(tc.list().size() == 2);
  if (tc.find(2)) {
    CHECK((tc.find(2)->flags & kMaximized) != 0);
  }
  if (tc.find(1)) {
    CHECK((tc.find(1)->flags & kMinimized) != 0);
  }

  // Close: firefox disappears; ids stay stable for kitty.
  {
    std::vector<ToplevelRecord> snap3;
    ToplevelRecord r;
    r.key = "w:0x2";
    r.appId = "kitty";
    r.title = "bash";
    r.flags = kActivated | kMaximized;
    snap3.push_back(std::move(r));
    service.set_snapshot(std::move(snap3));
  }
  CHECK(pump_until([&] { return tc.list().size() == 1; }));
  CHECK(tc.find(1) == nullptr);
  CHECK(tc.find(2) != nullptr);

  // clear() closes everything.
  const int beforeClear = static_cast<int>(tc.list().size());
  service.clear();
  CHECK(pump_until([&] { return tc.list().empty(); }));
  CHECK(beforeClear == 1);
  CHECK(tc.list().empty());
}

int main() {
  test_protocol_encoding();
  test_toplevel_codec();

  eh::ipc::IpcService srv;
  const std::string path = socket_path();
  CHECK(srv.start(path));
  srv.register_handler("ping", [](const std::vector<std::string>&) { return std::string("pong"); });
  srv.register_handler("echo", [](const std::vector<std::string>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i) out += ' ';
      out += args[i];
    }
    return out;
  });

  auto cmds = srv.registered_commands();
  CHECK(std::find(cmds.begin(), cmds.end(), "ping") != cmds.end());

  // The server runs on its own poll loop, exactly like the supervisor does.
  std::atomic<bool> stop{false};
  std::thread serverLoop([&] {
    while (!stop.load()) drive_server(srv, 10);
  });

  test_legacy_shim(path);

  {
    eh::ipc::IpcClient client;
    CHECK(client.connect(path) >= 0);

    auto resp = client.request("ping");
    CHECK(resp.has_value());
    CHECK(resp && *resp == "pong");

    resp = client.request("echo alpha beta");
    CHECK(resp && *resp == "alpha beta");

    // Pub/sub round trip.
    std::string gotTopic, gotPayload;
    std::vector<int> gotFds;
    client.set_event_handler([&](std::string t, std::string p, std::vector<int> fds) {
      gotTopic = std::move(t);
      gotPayload = std::move(p);
      gotFds = std::move(fds);
    });
    CHECK(client.subscribe("config.applied"));
    CHECK(client.subscribe("fd.topic"));

    // Publish happens on the "supervisor" side; the loop flushes it.
    srv.publish("config.applied", "theme=dark");
    struct pollfd pfd{client.fd(), POLLIN, 0};
    CHECK(::poll(&pfd, 1, 2000) > 0);
    client.on_fd_ready(client.fd());
    CHECK(gotTopic == "config.applied");
    CHECK(gotPayload == "theme=dark");
    CHECK(gotFds.empty());

    // fd-passing via SCM_RIGHTS. Publish a dup so the server's post-send close of
    // "its copy" cannot hit the same descriptor we still hold (in-process test
    // shares one fd table; production uses separate processes).
    int devNull = ::open("/dev/null", O_RDONLY);
    CHECK(devNull >= 0);
    const int queuedFd = ::dup(devNull);
    CHECK(queuedFd >= 0);
    gotTopic.clear();
    gotPayload.clear();
    gotFds.clear();
    srv.publish("fd.topic", "hand me a fd", {queuedFd});
    CHECK(::poll(&pfd, 1, 2000) > 0);
    client.on_fd_ready(client.fd());
    CHECK(gotTopic == "fd.topic");
    CHECK(gotPayload == "hand me a fd");
    CHECK(gotFds.size() == 1);
    if (gotFds.size() == 1) {
      // Received fd must be a valid, independent descriptor: closing the sender's
      // original must not affect it.
      CHECK(gotFds[0] != devNull);
      ::close(devNull);
      CHECK(::fcntl(gotFds[0], F_GETFD) != -1);
    }

    // Unsubscribe stops delivery.
    CHECK(client.unsubscribe("config.applied"));
    gotTopic.clear();
    srv.publish("config.applied", "should be dropped");
    srv.publish("fd.topic", "still delivered");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client.on_fd_ready(client.fd());
    CHECK(gotTopic == "fd.topic");
    CHECK(gotPayload == "still delivered");
  }

  test_toplevel_service_roundtrip(srv, path);

  stop.store(true);
  serverLoop.join();

  CHECK(srv.client_count() == 0);
  srv.stop();
  if (failures == 0) {
    std::cout << "ipc_fabric: all checks passed\n";
    return 0;
  }
  std::cerr << "ipc_fabric: " << failures << " check(s) failed\n";
  return 1;
}
