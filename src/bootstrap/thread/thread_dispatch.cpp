#include "bootstrap/thread/thread_dispatch.hpp"

#include "desktop_shell/common/log/mangowm_logger.hpp"
#include <mutex>
#include <vector>

namespace {

std::vector<std::function<void()>> g_queue;
std::mutex g_mutex;

}

void DeferredCall::callLater(std::function<void()> fn) {
   
  if (!fn) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_queue.push_back(std::move(fn));
}

void DeferredCall::drain() {
   
  std::vector<std::function<void()>> local;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    local.swap(g_queue);
  }
  for (auto& fn : local) {
    if (fn) fn();
  }
}
