#pragma once
#include <functional>
#include <memory>

class ThreadPool {
public:
  static ThreadPool& instance();
  void enqueue(std::function<void()> task);
  void enqueue(std::function<void()> task, std::function<void()> on_main_finish);

private:
  ThreadPool();
  ~ThreadPool();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
