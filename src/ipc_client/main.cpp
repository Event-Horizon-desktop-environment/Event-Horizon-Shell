#include "../desktop_shell/common/log/mangowm_logger.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <array>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string socket_path() {
  if (const char* p = std::getenv("EH_IPC_SOCKET")) {
    return std::string(p);
  }
  if (const char* dir = std::getenv("XDG_RUNTIME_DIR")) {
    return std::string(dir) + "/event-horizon-ipc.sock";
  }
  return "/tmp/event-horizon-ipc.sock";
}

} // namespace

int main(int argc, char** argv) {
   
  if (argc < 2) {
    std::cerr << "Usage: eh-ipc <command> [args...]\n";
    return 1;
  }

  // Build the command string from argv[1..].
  std::string cmd;
  for (int i = 1; i < argc; ++i) {
    if (i > 1) cmd += ' ';
    cmd += argv[i];
  }
  cmd += '\n';

  // Connect to the server.
  const std::string path = socket_path();
  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    std::cerr << "eh-ipc: socket() failed: " << std::strerror(errno) << "\n";
    return 1;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const size_t pathLen = std::min(path.size(), sizeof(addr.sun_path) - 1);
  std::memcpy(addr.sun_path, path.data(), pathLen);
  addr.sun_path[pathLen] = '\0';

  if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "eh-ipc: connect(\"" << path << "\") failed: " << std::strerror(errno) << "\n";
    std::cerr << "Is Event Horizon running?\n";
    ::close(fd);
    return 1;
  }

  // Send the command.
  if (::write(fd, cmd.data(), cmd.size()) < 0) {
    std::cerr << "eh-ipc: write() failed: " << std::strerror(errno) << "\n";
    ::close(fd);
    return 1;
  }

  // Shut down the write side so the server sees EOF.
  ::shutdown(fd, SHUT_WR);

  // Read the response.
  std::array<char, 4096> buf;
  ssize_t n = ::read(fd, buf.data(), buf.size() - 1);
  ::close(fd);

  if (n < 0) {
    std::cerr << "eh-ipc: read() failed: " << std::strerror(errno) << "\n";
    return 1;
  }

  buf[n] = '\0';
  std::string response(buf.data());
  // Trim a trailing newline.
  while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
    response.pop_back();
  }

  std::cout << response << "\n";

  // Exit with an error if the response starts with "error".
  if (response.rfind("error", 0) == 0) return 1;
  return 0;
}
