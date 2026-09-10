#include "bootstrap/session/unified_shell.hpp"

#include "bootstrap/session/detail/unified_shell_session.hpp"
#include "bootstrap/entry/screenshot_mode.hpp"
#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "services/ipc/ipc_server.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"

#include <cstring>
#include <fstream>
#include <iostream>

#include <sys/socket.h>
#include <sys/un.h>

namespace {

void ipc_log(const std::string& msg) {
  std::ofstream f("/tmp/eh-keyboard.log", std::ios::app);
  f << msg << std::endl;
}

} // namespace

namespace eh::app {

namespace {

UnifiedShell* g_unified_shell_active = nullptr;

static int run_ipc_client(const std::string& command) {
  ipc_log("IPC CLIENT: run_ipc_client called with command='" + command + "'");
  const std::string path = eh::ipc::default_socket_path();
  ipc_log("IPC CLIENT: socket path = " + path);
  std::cerr << "ipc: connecting to " << path << "\n";

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const size_t pathLen = std::min(path.size(), sizeof(addr.sun_path) - 1);
  std::memcpy(addr.sun_path, path.data(), pathLen);
  addr.sun_path[pathLen] = '\0';

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    ipc_log("IPC CLIENT: socket() failed: " + std::string(std::strerror(errno)));
    std::cerr << "ipc: failed to create socket: " << std::strerror(errno) << "\n";
    return 1;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ipc_log("IPC CLIENT: connect() failed: " + std::string(std::strerror(errno)));
    std::cerr << "ipc: failed to connect to " << path << ": " << std::strerror(errno) << "\n";
    ::close(fd);
    return 1;
  }
  ipc_log("IPC CLIENT: connected OK");

  const std::string cmd = command + "\n";
  ssize_t written = ::write(fd, cmd.data(), cmd.size());
  ipc_log("IPC CLIENT: wrote " + std::to_string(written) + " bytes");

  char buf[4096];
  ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
  ::close(fd);

  if (n > 0) {
    buf[n] = '\0';
    ipc_log("IPC CLIENT: received response: '" + std::string(buf) + "'");
    std::cout << buf;
  } else {
    ipc_log("IPC CLIENT: no response (n=" + std::to_string(n) + ")");
  }
  return 0;
}

}

int UnifiedShell::run(int argc, char** argv) {
   
  MANGOWM_INFO("UnifiedShell::run argc=%d", argc);
  struct ActiveGuard {
    UnifiedShell* const self;
    explicit ActiveGuard(UnifiedShell* s) : self(s) {
       
      g_unified_shell_active = s;
    }
    ~ActiveGuard() {
       
      if (g_unified_shell_active == self) g_unified_shell_active = nullptr;
    }
  } guard{this};

  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: begin");
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::strcmp(argv[i], "--version") == 0) {
      std::cerr << "Event Horizon " << EH_SHELL_VERSION << "\n";
      return 0;
    }
    if (argv[i] && std::strcmp(argv[i], "--eh-screenshot") == 0) return eh::app::run_screenshot_mode(argc, argv);
    if (argv[i] && std::strcmp(argv[i], "--eh-settings") == 0) return eh::settings::run_standalone();
    if (argv[i] && std::strcmp(argv[i], "--ipc") == 0) {
      if (i + 1 < argc) return run_ipc_client(argv[i + 1]);
      std::cerr << "Usage: EventHorizon --ipc <command> [args...]\n";
      return 1;
    }
  }

  const detail::ShellRunMode mode = detail::parse_shell_run_mode(argc, argv);

  detail::UnifiedShellSession session(*this);
  return session.run(mode);
}

}
