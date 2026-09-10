#include "app/file_browser/features/video_worker.hpp"
#include "app/file_browser/app.hpp"

#include <cairo/cairo.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace eh::file_browser {
namespace {

cairo_surface_t* scale_surface(cairo_surface_t* src, int max_px) {
  int w = cairo_image_surface_get_width(src);
  int h = cairo_image_surface_get_height(src);
  if (w <= 0 || h <= 0) return nullptr;
  if (std::max(w, h) <= max_px) {
    cairo_surface_reference(src);
    return src;
  }
  double scale = static_cast<double>(max_px) / std::max(w, h);
  int nw = std::max(1, static_cast<int>(std::lround(w * scale)));
  int nh = std::max(1, static_cast<int>(std::lround(h * scale)));
  cairo_surface_t* scaled =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
  if (!scaled || cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(scaled);
    return nullptr;
  }
  cairo_t* cr = cairo_create(scaled);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(scaled);
  return scaled;
}

} // namespace

// ── VideoThumbWorker ───────────────────────────────────────

VideoThumbWorker::VideoThumbWorker() {
  for (int i = 0; i < kNumThreads; ++i)
    m_threads.emplace_back(&VideoThumbWorker::thread_main, this, i);
}

VideoThumbWorker::~VideoThumbWorker() {
  stop();
  m_in_cv.notify_all();
  for (auto& t : m_threads)
    if (t.joinable()) t.join();
}

void VideoThumbWorker::enqueue(const std::string& path, int max_px,
                                const std::string& cache_path,
                                time_t src_mtime) {
  {
    std::lock_guard<std::mutex> lock(m_in_mutex);
    if (m_pending.count(path)) return;
    m_in.push({path, max_px, cache_path, src_mtime});
    m_pending.insert(path);
  }
  m_in_cv.notify_one();
}

bool VideoThumbWorker::poll(VideoThumbResult& out) {
  std::lock_guard<std::mutex> lock(m_out_mutex);
  if (m_out.empty()) return false;
  out = std::move(m_out.front());
  m_out.pop();
  return true;
}

bool VideoThumbWorker::busy() {
  std::lock_guard<std::mutex> lock(m_in_mutex);
  return !m_in.empty();
}

void VideoThumbWorker::stop() { m_running.store(false); }

void VideoThumbWorker::thread_main(int /*thread_id*/) {
  while (m_running.load()) {
    WorkItem item;
    {
      std::unique_lock<std::mutex> lock(m_in_mutex);
      m_in_cv.wait(lock, [this] {
        return !m_running.load() || !m_in.empty();
      });
      if (!m_running.load()) return;
      if (m_in.empty()) continue;

      item = std::move(m_in.front());
      m_in.pop();
      m_pending.erase(item.path);
    }

    // Generate cache file if needed
    bool cache_ok = (access(item.cache_path.c_str(), R_OK) == 0);
    if (cache_ok) {
      struct stat cache_st{};
      if (stat(item.cache_path.c_str(), &cache_st) == 0 &&
          cache_st.st_mtime == item.src_mtime) {
        // Cache valid
      } else {
        cache_ok = false;
      }
    }

    if (!cache_ok) {
      std::error_code ec;
      auto dir = std::filesystem::path(item.cache_path).parent_path();
      std::filesystem::create_directories(dir, ec);

#ifdef FFMPEGTHUMBNAILER_BIN
      std::string cmd = "\"" FFMPEGTHUMBNAILER_BIN "\"";
#else
      std::string cmd = "ffmpegthumbnailer";
#endif
      cmd += " -s 512 -q 1 -i \"";
      cmd += item.path;
      cmd += "\" -o \"";
      cmd += item.cache_path;
      cmd += "\" 2>/dev/null";

      FILE* fp = popen(cmd.c_str(), "r");
      if (!fp) continue;
      std::array<char, 4096> buf{};
      while (fgets(buf.data(), static_cast<int>(buf.size()), fp))
        ;
      int rc = pclose(fp);

      if (rc != 0 || access(item.cache_path.c_str(), R_OK) != 0) {
        std::fprintf(stderr,
                     "[video-worker] ffmpegthumbnailer rc=%d for \"%s\"\n",
                     rc, item.path.c_str());
        continue;
      }

      // Set mtime to match source
      struct stat src_st{};
      if (stat(item.path.c_str(), &src_st) == 0) {
        struct timeval times[2];
        times[0].tv_sec = src_st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec = src_st.st_mtime;
        times[1].tv_usec = 0;
        utimes(item.cache_path.c_str(), times);
      }
    }

    // Load PNG with cairo
    cairo_surface_t* raw =
        cairo_image_surface_create_from_png(item.cache_path.c_str());
    if (!raw || cairo_surface_status(raw) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(raw);
      continue;
    }

    cairo_surface_t* scaled = scale_surface(raw, item.max_px);
    cairo_surface_destroy(raw);
    if (!scaled) continue;

    {
      std::lock_guard<std::mutex> lock(m_out_mutex);
      m_out.push({item.path, scaled});
    }
  }
}

// ── Singleton access ────────────────────────────────────────

VideoThumbWorker& video_worker() {
  static VideoThumbWorker instance;
  return instance;
}

// ── Drain completed thumbnails into AppState cache ──────────

void drain_video_thumbnails(AppState& app) {
  auto& worker = video_worker();
  VideoThumbResult res;
  bool had_any = false;
  while (worker.poll(res)) {
    had_any = true;
    if (!res.surface) continue;

    if (app.thumb_cache.count(res.path)) {
      cairo_surface_destroy(res.surface);
      continue;
    }

    while (app.thumb_cache_bytes >= AppState::kThumbCacheMaxBytes &&
           !app.thumb_lru.empty()) {
      auto evict = app.thumb_lru.back();
      auto ev = app.thumb_cache.find(evict);
      if (ev != app.thumb_cache.end()) {
        int eh = cairo_image_surface_get_height(ev->second);
        int estr = cairo_image_surface_get_stride(ev->second);
        app.thumb_cache_bytes -= static_cast<std::size_t>(eh * estr);
        cairo_surface_destroy(ev->second);
        app.thumb_cache.erase(ev);
      }
      app.thumb_lru.pop_back();
    }

    int h = cairo_image_surface_get_height(res.surface);
    int stride = cairo_image_surface_get_stride(res.surface);
    app.thumb_cache_bytes += static_cast<std::size_t>(h * stride);
    app.thumb_cache[res.path] = res.surface;
    app.thumb_lru.push_front(res.path);
  }

  if (had_any) {
    schedule_frame(app);
  }
}

} // namespace eh::file_browser
