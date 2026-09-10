#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

namespace eh::vk {

class VulkanDestructionQueue {
 public:
  using Task = std::function<void()>;

  VulkanDestructionQueue();
  ~VulkanDestructionQueue();

  VulkanDestructionQueue(const VulkanDestructionQueue&) = delete;
  VulkanDestructionQueue& operator=(const VulkanDestructionQueue&) = delete;
  VulkanDestructionQueue(VulkanDestructionQueue&&) = delete;
  VulkanDestructionQueue& operator=(VulkanDestructionQueue&&) = delete;

  void post(Task&& task);

  void shutdown();

  void drain();

  static VulkanDestructionQueue& instance();

 private:
  void thread_main();

  std::thread worker_;
  std::vector<Task> queue_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool running_ = true;
};

}
