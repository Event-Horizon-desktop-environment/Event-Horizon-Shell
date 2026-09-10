#pragma once

#include "desktop_shell/desktop/audio/cava_config.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace eh::shell::desktop {

class CavaReader {
public:
  explicit CavaReader(const CavaConfig& cfg);
  ~CavaReader();

  void start();
  void stop();
  void reload(const CavaConfig& cfg);

  [[nodiscard]] bool available() const { return m_available; }

  void read_values(std::vector<int>& out) const;

private:
  void thread_func();

  mutable std::mutex m_mutex;
  std::vector<int> m_values;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_available{false};
  std::thread m_thread;
  int m_pipe_fd = -1;
  pid_t m_pid = 0;
  CavaConfig m_cfg;
};

} // namespace eh::shell::desktop
