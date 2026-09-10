#include "desktop_shell/desktop/audio/cava_reader.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

namespace eh::shell::desktop {

CavaReader::CavaReader(const CavaConfig& cfg)
    : m_cfg(cfg) {}

CavaReader::~CavaReader() {
  stop();
}

void CavaReader::start() {
  if (m_running.exchange(true)) return;
  m_thread = std::thread(&CavaReader::thread_func, this);
}

void CavaReader::stop() {
  m_running = false;
  if (m_pid > 0) {
    kill(m_pid, SIGTERM);
    int st = 0;
    waitpid(m_pid, &st, WNOHANG);
    m_pid = 0;
  }
  if (m_pipe_fd >= 0) {
    close(m_pipe_fd);
    m_pipe_fd = -1;
  }
  if (m_thread.joinable()) m_thread.join();
}

void CavaReader::reload(const CavaConfig& cfg) {
  stop();
  m_cfg = cfg;
  start();
}

void CavaReader::read_values(std::vector<int>& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  out = m_values;
}

void CavaReader::thread_func() {
  const int barCount = m_cfg.bars;

  int in_fd[2]{}, out_fd[2]{};
  if (pipe(in_fd) < 0 || pipe(out_fd) < 0) {
    m_available = false;
    m_running = false;
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(in_fd[0]); close(in_fd[1]);
    close(out_fd[0]); close(out_fd[1]);
    m_available = false;
    m_running = false;
    return;
  }

  if (pid == 0) {
    close(in_fd[1]);
    close(out_fd[0]);
    dup2(in_fd[0], STDIN_FILENO);
    dup2(out_fd[1], STDOUT_FILENO);
    dup2(out_fd[1], STDERR_FILENO);
    for (int i = 3; i < 64; ++i) close(i);

    execlp("cava", "cava", "-p", "/dev/stdin", nullptr);
    _exit(127);
  }

  // Parent: write config to input pipe
  close(in_fd[0]);
  {
    const std::string config = m_cfg.build_cava_config();
    write(in_fd[1], config.data(), config.size());
  }
  close(in_fd[1]);
  close(out_fd[1]);
  m_pipe_fd = out_fd[0];
  m_pid = pid;

  fcntl(m_pipe_fd, F_SETFL, O_NONBLOCK);

  std::vector<int> latest(barCount, 0);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values = latest;
  }
  m_available = true;

  std::string buf;
  buf.reserve(4096);

  while (m_running) {
    struct pollfd pfd{m_pipe_fd, POLLIN, 0};
    int pr = poll(&pfd, 1, 200);
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pr == 0) continue;

    char raw[4096];
    ssize_t n = read(m_pipe_fd, raw, sizeof(raw) - 1);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      break;
    }
    raw[n] = '\0';

    buf.append(raw, static_cast<size_t>(n));
    size_t pos = 0;
    while (true) {
      size_t nl = buf.find('\n', pos);
      if (nl == std::string::npos) break;
      std::string_view line(buf.data() + pos, nl - pos);
      pos = nl + 1;

      if (line.empty()) continue;

      size_t start = 0;
      size_t idx = 0;
      while (start < line.size() && idx < static_cast<size_t>(barCount)) {
        size_t semi = line.find(';', start);
        if (semi == std::string::npos) semi = line.size();
        if (semi > start) {
          int val = 0;
          for (size_t ci = start; ci < semi; ++ci) {
            if (line[ci] >= '0' && line[ci] <= '9') {
              val = val * 10 + (line[ci] - '0');
            }
          }
          latest[idx++] = val;
        }
        start = semi + 1;
      }

      std::lock_guard<std::mutex> lock(m_mutex);
      m_values = latest;
    }
    buf.erase(0, pos);
  }

  m_available = false;
  close(m_pipe_fd);
  m_pipe_fd = -1;
  if (m_pid > 0) {
    kill(m_pid, SIGTERM);
    waitpid(m_pid, nullptr, WNOHANG);
    m_pid = 0;
  }
  m_running = false;
}

} // namespace eh::shell::desktop
