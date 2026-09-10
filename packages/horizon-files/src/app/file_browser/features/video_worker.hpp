#pragma once

#include <cairo/cairo.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/file_browser/app_types.hpp"

namespace eh::file_browser {

struct VideoThumbResult {
  std::string path;
  cairo_surface_t* surface{nullptr};
};

class VideoThumbWorker {
public:
  VideoThumbWorker();
  ~VideoThumbWorker();

  void enqueue(const std::string& path, int max_px,
               const std::string& cache_path, time_t src_mtime);
  bool poll(VideoThumbResult& out);
  bool busy();
  void stop();

private:
  struct WorkItem {
    std::string path;
    int max_px;
    std::string cache_path;
    time_t src_mtime;
  };

  void thread_main(int thread_id);

  static constexpr int kNumThreads = 6;

  std::vector<std::thread> m_threads;
  std::mutex m_in_mutex;
  std::condition_variable m_in_cv;
  std::queue<WorkItem> m_in;
  std::set<std::string> m_pending;

  std::mutex m_out_mutex;
  std::queue<VideoThumbResult> m_out;

  std::atomic<bool> m_running{true};
};

VideoThumbWorker& video_worker();

void drain_video_thumbnails(AppState& app);

} // namespace eh::file_browser
