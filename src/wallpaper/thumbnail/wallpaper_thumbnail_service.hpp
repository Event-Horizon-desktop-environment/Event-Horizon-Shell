#pragma once

#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eh::wallpaper {

class WallpaperThumbnailService {
public:
  static WallpaperThumbnailService& instance();

  WallpaperThumbnailService(const WallpaperThumbnailService&) = delete;
  WallpaperThumbnailService& operator=(const WallpaperThumbnailService&) = delete;
  WallpaperThumbnailService(WallpaperThumbnailService&&) = delete;
  WallpaperThumbnailService& operator=(WallpaperThumbnailService&&) = delete;

  [[nodiscard]] int wake_fd() const { return wake_fd_; }

  void request(const std::string& path, int max_px);
  void release_all();
  void drain_wake();

  [[nodiscard]] std::pair<std::size_t, std::size_t> queue_snapshot() const;

  [[nodiscard]] bool has_pending_completed() const;

  [[nodiscard]] std::vector<ThumbnailDecoded> take_completed();

  ~WallpaperThumbnailService();

private:
  WallpaperThumbnailService();
  void worker_loop();
  void push_completed(ThumbnailDecoded job);
  void signal_main();

  int wake_fd_ = -1;
  std::vector<std::thread> workers_;
  std::atomic<bool> shutdown_{false};

  mutable std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  struct Job {
    std::string path;
    int max_px = 0;
  };
  std::deque<Job> job_queue_;
  std::unordered_set<std::string> in_flight_;
  std::unordered_set<std::string> canceled_;

  mutable std::mutex result_mu_;
  std::deque<ThumbnailDecoded> completed_;
};

}
