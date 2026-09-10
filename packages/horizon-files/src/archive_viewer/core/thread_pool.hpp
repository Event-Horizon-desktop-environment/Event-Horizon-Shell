#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace archive_viewer {

class ThreadPool {
public:
  ThreadPool() : ThreadPool(std::thread::hardware_concurrency()) {}

  explicit ThreadPool(size_t n) : stop_(false) {
    for (size_t i = 0; i < n; ++i) {
      workers_.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock lock(queue_mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock lock(queue_mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
  }

  template<typename F, typename... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    auto result = task->get_future();
    {
      std::unique_lock lock(queue_mutex_);
      tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return result;
  }

  size_t thread_count() const { return workers_.size(); }
  size_t pending() const {
    std::unique_lock lock(queue_mutex_);
    return tasks_.size();
  }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  mutable std::mutex queue_mutex_;
  std::condition_variable cv_;
  bool stop_;
};

} // namespace archive_viewer
