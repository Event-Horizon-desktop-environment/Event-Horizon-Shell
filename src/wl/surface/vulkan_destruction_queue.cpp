#include "wl/surface/vulkan_destruction_queue.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <utility>

namespace {

thread_local bool tls_in_queue_task = false;

} // namespace

namespace eh::vk {

VulkanDestructionQueue::VulkanDestructionQueue() {
   
  worker_ = std::thread(&VulkanDestructionQueue::thread_main, this);
}

VulkanDestructionQueue::~VulkanDestructionQueue() {
   
  shutdown();
}

void VulkanDestructionQueue::post(Task&& task) {
   
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!running_) return;
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void VulkanDestructionQueue::drain() {
   
  if (tls_in_queue_task) {
    MANGOWM_DEBUG("drain called from inside a queue task — skipping (re-entrant)");
    return;
  }
  std::atomic<int> count{0};
  post([&] { count.fetch_add(1, std::memory_order_relaxed); });

  while (count.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}

void VulkanDestructionQueue::shutdown() {
   
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_one();
  if (worker_.joinable()) worker_.join();
}

VulkanDestructionQueue& VulkanDestructionQueue::instance() {
  static VulkanDestructionQueue q;
  return q;
}

void VulkanDestructionQueue::thread_main() {
   
  std::vector<Task> local_queue;
  while (true) {
    {
      std::unique_lock<std::mutex> lock(mtx_);
      cv_.wait(lock, [&] { return !queue_.empty() || !running_; });

      if (!running_ && queue_.empty()) break;

      if (!queue_.empty()) {
        local_queue.swap(queue_);
        queue_.clear();
      }
    }

    for (auto& task : local_queue) {
      tls_in_queue_task = true;
      try {
        task();
      } catch (const std::exception& e) {
        std::cerr << "[vk-destruct] exception: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "[vk-destruct] unknown exception\n";
      }
      tls_in_queue_task = false;
    }
    local_queue.clear();
  }
}

}
