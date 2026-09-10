#include "bootstrap/thread/thread_pool.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct ThreadPool::Impl {
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<bool> stop{false};

  Impl() {
    const unsigned n = std::max(2u, std::thread::hardware_concurrency());
    workers.reserve(n);
    for (unsigned i = 0; i < n; ++i)
      workers.emplace_back([this]() { worker_loop(); });
  }

  ~Impl() {
    stop.store(true, std::memory_order_relaxed);
    cv.notify_all();
    for (auto& w : workers)
      if (w.joinable()) w.join();
  }

  void worker_loop() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return stop.load(std::memory_order_relaxed) || !tasks.empty(); });
        if (stop.load(std::memory_order_relaxed) && tasks.empty()) return;
        task = std::move(tasks.front());
        tasks.pop();
      }
      if (task) task();
    }
  }

  void enqueue(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mtx);
    tasks.push(std::move(task));
    cv.notify_one();
  }
};

ThreadPool::ThreadPool() : impl_(std::make_unique<Impl>()) {}

ThreadPool::~ThreadPool() = default;

ThreadPool& ThreadPool::instance() {
  static ThreadPool pool;
  return pool;
}

void ThreadPool::enqueue(std::function<void()> task) {
  impl_->enqueue(std::move(task));
}

void ThreadPool::enqueue(std::function<void()> task, std::function<void()> on_main_finish) {
  impl_->enqueue([task = std::move(task), cb = std::move(on_main_finish)]() {
    if (task) task();
    if (cb) DeferredCall::callLater(std::move(cb));
  });
}
