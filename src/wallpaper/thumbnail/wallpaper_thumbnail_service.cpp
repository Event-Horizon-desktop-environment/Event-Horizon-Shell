#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"

#include "desktop_shell/common/bench/bench_trace.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <sys/eventfd.h>
#include <unistd.h>

#include "wallpaper/wallpaper_log.hpp"

namespace eh::wallpaper {
namespace {

constexpr int kMinWorkers = 2;
constexpr int kMaxWorkers = 4;

constexpr std::size_t kMaxCompletedPending = 64;

constexpr std::size_t kMaxJobQueuePending = 192;

static std::string basename_for_log(const std::string& path) {
  WP_SCOPE();
    
  const auto s = path.rfind('/');
  return (s == std::string::npos) ? path : path.substr(s + 1);
}

}

WallpaperThumbnailService& WallpaperThumbnailService::instance() {
  WP_SCOPE();
    
  static WallpaperThumbnailService g;
  return g;
}

WallpaperThumbnailService::WallpaperThumbnailService() {
  WP_SCOPE();
    
  wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake_fd_ < 0) {
    wake_fd_ = -1;
  }

  const unsigned hc = std::thread::hardware_concurrency();
  const std::size_t n =
      std::clamp<std::size_t>((hc == 0) ? kMinWorkers : std::max<std::size_t>(kMinWorkers, hc / 2), kMinWorkers,
                              static_cast<std::size_t>(kMaxWorkers));
  workers_.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
  if (wallpaper_thumbnail_pipeline_debug_enabled()) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] service: wake_fd=" << wake_fd_ << " workers=" << workers_.size()
              << (wake_fd_ < 0 ? " (no eventfd — completions merge on draw only)" : "")
              << " RGBA decode (Cairo only on merge)\n";
  }
}

WallpaperThumbnailService::~WallpaperThumbnailService() {
  WP_SCOPE();
    
  MANGOWM_INFO("{}", __func__);
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    shutdown_.store(true);
  }
  queue_cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  if (wake_fd_ >= 0) {
    ::close(wake_fd_);
    wake_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> rlock(result_mu_);
    completed_.clear();
  }
  {
    std::lock_guard<std::mutex> qlock(queue_mu_);
    in_flight_.clear();
    job_queue_.clear();
    canceled_.clear();
  }
}

void WallpaperThumbnailService::request(const std::string& path, int max_px) {
  WP_SCOPE();
  WP_LOG("path=%s decodePx=%d", path.c_str(), max_px);
  if (path.empty() || max_px < 24) return;
  std::lock_guard<std::mutex> lock(queue_mu_);

  for (auto& j : job_queue_) {
    if (j.path == path) {
      if (max_px > j.max_px) j.max_px = max_px;
      return;
    }
  }
  if (in_flight_.contains(path)) return;
  while (job_queue_.size() >= kMaxJobQueuePending) {
    const Job victim = std::move(job_queue_.front());
    job_queue_.pop_front();
    canceled_.insert(victim.path);
    in_flight_.erase(victim.path);
  }
  canceled_.erase(path);
  in_flight_.insert(path);
  job_queue_.emplace_back(path, max_px);
  if (wallpaper_thumbnail_pipeline_debug_enabled()) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] enqueue file=\"" << basename_for_log(path) << "\" max_px=" << max_px
              << " q=" << job_queue_.size() << " in_flight=" << in_flight_.size() << "\n";
  }
  queue_cv_.notify_one();
}

void WallpaperThumbnailService::release_all() {
  WP_SCOPE();
    
  if (wallpaper_thumbnail_pipeline_debug_enabled()) {
    std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
    std::cerr << "[wallpaper-thumb] release_all\n";
  }

  std::vector<std::string> dropped_merge_paths;
  {
    std::lock_guard<std::mutex> rlock(result_mu_);
    dropped_merge_paths.reserve(completed_.size());
    for (const auto& c : completed_) dropped_merge_paths.push_back(c.path);
    completed_.clear();
  }
  {
    std::lock_guard<std::mutex> qlock(queue_mu_);
    for (const auto& p : dropped_merge_paths) {
      in_flight_.erase(p);
    }
    for (const auto& j : job_queue_) {
      canceled_.insert(j.path);
      in_flight_.erase(j.path);
    }
    job_queue_.clear();
    for (const auto& p : in_flight_) {
      canceled_.insert(p);
    }
    queue_cv_.notify_all();
  }
}

std::pair<std::size_t, std::size_t> WallpaperThumbnailService::queue_snapshot() const {
  WP_SCOPE();
    
  std::lock_guard<std::mutex> lock(queue_mu_);
  return {job_queue_.size(), in_flight_.size()};
}

bool WallpaperThumbnailService::has_pending_completed() const {
  WP_SCOPE();
    
  std::lock_guard<std::mutex> lock(result_mu_);
  return !completed_.empty();
}

void WallpaperThumbnailService::drain_wake() {
  WP_SCOPE();
    
  if (wake_fd_ < 0) return;
  std::uint64_t v = 0;
  while (::read(wake_fd_, &v, sizeof(v)) > 0) {
  }
}

std::vector<ThumbnailDecoded> WallpaperThumbnailService::take_completed() {
  WP_SCOPE();
  WP_LOG("count=%zu", completed_.size());
  std::deque<ThumbnailDecoded> tmp;
  {
    std::lock_guard<std::mutex> lock(result_mu_);
    completed_.swap(tmp);
  }
  std::vector<ThumbnailDecoded> out;
  out.reserve(tmp.size());
  while (!tmp.empty()) {
    out.push_back(std::move(tmp.front()));
    tmp.pop_front();
  }
  if (!out.empty()) {
    std::lock_guard<std::mutex> qlock(queue_mu_);
    for (const auto& d : out) {
      in_flight_.erase(d.path);
    }
  }
  return out;
}

void WallpaperThumbnailService::signal_main() {
  WP_SCOPE();
    
  if (wake_fd_ < 0) return;
  const std::uint64_t one = 1;
  if (::write(wake_fd_, &one, sizeof(one)) < 0 && errno != EAGAIN) {
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] eventfd write failed errno=" << errno << " (" << std::strerror(errno) << ")\n";
    }
  }
}

void WallpaperThumbnailService::push_completed(ThumbnailDecoded job) {
  WP_SCOPE();
  WP_LOG("path=%s", job.path.c_str());
  std::vector<std::string> dropped_paths;
  {
    std::lock_guard<std::mutex> lock(result_mu_);
    while (completed_.size() >= kMaxCompletedPending) {
      ThumbnailDecoded old = std::move(completed_.front());
      completed_.pop_front();
      dropped_paths.push_back(std::move(old.path));
    }
    completed_.push_back(std::move(job));
  }
  if (!dropped_paths.empty()) {
    std::lock_guard<std::mutex> qlock(queue_mu_);
    for (const auto& p : dropped_paths) {
      in_flight_.erase(p);
    }
    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] completed queue cap: dropped " << dropped_paths.size()
                << " oldest pending merge (UI merge slower than decode)\n";
    }
  }
  signal_main();
  WP_LOG("completed count=%zu", completed_.size());
}

void WallpaperThumbnailService::worker_loop() {
  WP_SCOPE();
  WP_LOG("worker start");
  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(queue_mu_);
      queue_cv_.wait(lock, [this]() { return shutdown_.load() || !job_queue_.empty(); });
      if (shutdown_.load() && job_queue_.empty()) return;
      if (job_queue_.empty()) continue;
      job = std::move(job_queue_.front());
      job_queue_.pop_front();
    }

    const auto t_dec0 = std::chrono::steady_clock::now();
    ThumbnailDecoded decoded = decode_thumbnail_to_rgba(job.path, job.max_px);
    if (eh::bench::enabled()) {
      const int64_t us =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_dec0).count();
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      std::cerr << "[eh-bench] wallpaper_thumb_decode file=\"" << basename_for_log(job.path) << "\" us=" << us
                << " ok=" << (!decoded.failed && decoded.width > 0 ? 1 : 0)
                << " cache_hit=" << (decoded.from_disk_cache_hit ? 1 : 0) << " " << decoded.width << "x" << decoded.height
                << "\n";
    }

    bool drop = false;
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (canceled_.erase(job.path)) drop = true;
      if (drop) in_flight_.erase(job.path);
    }
    if (drop) {
      if (wallpaper_thumbnail_pipeline_debug_enabled()) {
        std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] dropped (canceled) file=\"" << basename_for_log(job.path) << "\"\n";
      }
      continue;
    }

    if (wallpaper_thumbnail_pipeline_debug_enabled()) {
      std::lock_guard<std::mutex> lk(wallpaper_thumbnail_log_mutex());
      if (!decoded.failed && decoded.width > 0 && decoded.height > 0) {
        std::cerr << "[wallpaper-thumb] decode ok file=\"" << basename_for_log(job.path) << "\" " << decoded.width
                  << "x" << decoded.height << " disk_cache_hit=" << (decoded.from_disk_cache_hit ? 1 : 0) << "\n";
      } else {
        std::cerr << "[wallpaper-thumb] decode fail file=\"" << basename_for_log(job.path) << "\" rgba_bytes="
                  << decoded.rgba.size() << "\n";
      }
    }

    write_thumbnail_disk_cache_maybe(decoded);
    push_completed(std::move(decoded));
  }
}

}
