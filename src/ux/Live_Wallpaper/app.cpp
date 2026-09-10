#include "ux/Live_Wallpaper/app.hpp"
#include "ux/Live_Wallpaper/settings.hpp"
#include "ux/Live_Wallpaper/ui/draw.hpp"

#include "libffmpegthumbnailer/videothumbnailer.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "desktop_shell/common/palette/matugen_external_templates.hpp"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
// NOTE: No more ux/settings/* or settings_tab_wallpaper headers (separation complete for gallery logic).
// Needed gallery/ensure/layout helpers are ported locally inside Live_Wallpaper (or use live_ variants defined here).
#include "xdg-shell-client-protocol.h"

namespace eh::live_wallpaper {

AppState::AppState() = default;
AppState::~AppState() {
  for (auto* s : liveWallpaperHoverVideoFrames)
    if (s) cairo_surface_destroy(s);
  for (auto& [_, s] : liveWallpaperBlurredThumbs)
    if (s) cairo_surface_destroy(s);
}

static constexpr int kWpSubTabH = 48;

static constexpr const char* kWallpaperModeLabels[] = {"Fill", "Fit", "Stretch", "Center", "Tile"};

static std::string wallpaper_file_basename(const std::string& p) {
  const size_t s = p.rfind('/');
  return (s == std::string::npos) ? p : p.substr(s + 1);
}

static std::string wallpaper_truncate_visual(const std::string& p, size_t maxC) {
  if (p.size() <= maxC) return p;
  if (maxC < 12) return p.substr(0, maxC);
  const size_t head = maxC / 2 - 2;
  const size_t tail = maxC - head - 3;
  return p.substr(0, head) + "\u2026" + p.substr(p.size() - tail);
}

// Video file helpers (standalone; does not touch the shared wallpaper library).

static bool ends_with_video_ext(const std::string& name) {
  const size_t n = name.size();
  if (n < 4) return false;
  auto ci = [&](const char* suf) -> bool {
    size_t sn = 0;
    while (suf[sn]) ++sn;
    if (n < sn) return false;
    for (size_t i = 0; i < sn; ++i) {
      if (std::tolower(static_cast<unsigned char>(name[n - sn + i])) !=
          std::tolower(static_cast<unsigned char>(suf[i])))
        return false;
    }
    return true;
  };
  return ci(".mp4") || ci(".webm") || ci(".mkv") || ci(".mov") || ci(".avi");
}

FILE* lw_log_file() {
  static FILE* f = nullptr;
  if (!f) f = fopen("/tmp/live", "w");
  return f;
}

int64_t lw_now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int lw_run_cmd_and_read_stdout(const std::vector<const char*>& argv,
                                       std::vector<std::uint8_t>& out,
                                       int maxBytes, int timeoutMs = -1) {
  out.clear();
  std::string cmdStr;
  for (auto& a : argv) { if (a) { if (!cmdStr.empty()) cmdStr += ' '; cmdStr += a; } }
  int64_t t0 = lw_now_us();
  int pipefd[2] = {};
  if (pipe(pipefd) < 0) { LW_LOG("pipe failed: %s", cmdStr.c_str()); return -1; }
  pid_t pid = fork();
  if (pid < 0) { close(pipefd[0]); close(pipefd[1]); LW_LOG("fork failed: %s", cmdStr.c_str()); return -1; }
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(const_cast<char*>(a));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(pipefd[1]);
  int total = 0;
  for (;;) {
    if (total >= maxBytes) break;
    if (timeoutMs >= 0) {
      struct pollfd pfd;
      pfd.fd = pipefd[0];
      pfd.events = POLLIN;
      int ret = poll(&pfd, 1, timeoutMs);
      if (ret <= 0) {
        if (ret == 0) LW_LOG("cmd timeout(%dms): %s  (got %d bytes)", timeoutMs, cmdStr.c_str(), total);
        break;
      }
    }
    int remain = maxBytes - total;
    char buf[4096];
    ssize_t n = read(pipefd[0], buf, std::min((int)sizeof(buf), remain));
    if (n <= 0) break;
    out.insert(out.end(), buf, buf + n);
    total += (int)n;
  }
  close(pipefd[0]);

  // Give child time to exit naturally after closing pipe
  // (ffmpeg exits almost immediately after writing all output)
  int st = 0;
  pid_t waited = 0;
  for (int i = 0; i < 40; ++i) {
    waited = waitpid(pid, &st, WNOHANG);
    if (waited == pid) break;
    usleep(25000); // 25ms × 40 = 1s total grace period
  }
  if (waited != pid) {
    kill(pid, SIGTERM);
    waited = waitpid(pid, &st, 0);
  }
  int64_t dt = lw_now_us() - t0;
  bool ok = (waited == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
  if (dt > 50000 || !ok)
    LW_LOG("cmd %s dt=%lldms ok=%d bytes=%d waited=%d sig=%d",
           cmdStr.c_str(), (long long)(dt/1000), ok, total, (int)waited,
           WIFSIGNALED(st) ? WTERMSIG(st) : 0);
  return ok ? total : -1;
}

static double lw_get_video_duration(const std::string& path) {
  std::vector<const char*> argv = {
    "ffprobe", "-v", "error", "-show_entries", "format=duration",
    "-of", "default=noprint_wrappers=1:nokey=1",
    path.c_str(), nullptr
  };
  std::vector<std::uint8_t> out;
  if (lw_run_cmd_and_read_stdout(argv, out, 256, 2000) <= 0) return 0.0;
  std::string s(out.begin(), out.end());
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  try { return std::stod(s); } catch (...) { return 0.0; }
}

static double lw_get_video_frame_rate(const std::string& path) {
  std::vector<const char*> argv = {
    "ffprobe", "-v", "error",
    "-select_streams", "v:0",
    "-of", "default=noprint_wrappers=1:nokey=1",
    "-show_entries", "stream=r_frame_rate",
    path.c_str(), nullptr
  };
  std::vector<std::uint8_t> out;
  if (lw_run_cmd_and_read_stdout(argv, out, 128, 2000) <= 0) return 30.0;
  std::string s(out.begin(), out.end());
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  size_t slash = s.find('/');
  if (slash == std::string::npos) {
    try { return std::stod(s); } catch (...) { return 30.0; }
  }
  double num = 30.0, den = 1.0;
  try { num = std::stod(s.substr(0, slash)); } catch (...) {}
  try { den = std::stod(s.substr(slash + 1)); } catch (...) {}
  return (den > 0.0 && num > 0.0) ? num / den : 30.0;
}

static std::string lw_video_thumb_cache_dir() {
  const char* cache = std::getenv("XDG_CACHE_HOME");
  std::string dir;
  if (cache && cache[0]) dir = std::string(cache) + "/event-horizon/video-thumbs";
  else if (const char* h = std::getenv("HOME")) dir = std::string(h) + "/.cache/event-horizon/video-thumbs";
  else dir = "/tmp/event-horizon-video-thumbs";
  return dir;
}

static std::string lw_video_thumb_cache_path(const std::string& videoPath) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : videoPath) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%016" PRIx64, h);
  return lw_video_thumb_cache_dir() + "/" + buf + ".png";
}

static cairo_surface_t* lw_load_cached_thumb(const std::string& videoPath) {
  std::string cp = lw_video_thumb_cache_path(videoPath);
  struct stat st{};
  if (::stat(cp.c_str(), &st) != 0) return nullptr;
  struct stat src{};
  if (::stat(videoPath.c_str(), &src) != 0) return nullptr;
  if (st.st_mtime < src.st_mtime) return nullptr;
  return cairo_image_surface_create_from_png(cp.c_str());
}

static void lw_save_thumb_cache(const std::string& videoPath, cairo_surface_t* surf) {
  std::string dir = lw_video_thumb_cache_dir();
  struct stat st{};
  if (::stat(dir.c_str(), &st) != 0) {
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return;
  }
  std::string cp = lw_video_thumb_cache_path(videoPath);
  cairo_surface_write_to_png(surf, cp.c_str());
}

// Blurred thumb disk cache.
static std::string lw_blurred_thumb_cache_dir() {
  const char* cache = std::getenv("XDG_CACHE_HOME");
  std::string dir;
  if (cache && cache[0]) dir = std::string(cache) + "/event-horizon/blurred-thumbs";
  else if (const char* h = std::getenv("HOME")) dir = std::string(h) + "/.cache/event-horizon/blurred-thumbs";
  else dir = "/tmp/event-horizon-blurred-thumbs";
  return dir;
}

static std::string lw_blurred_thumb_cache_path(const std::string& videoPath) {
  // Reuse same hash as the regular thumb cache
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : videoPath) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%016" PRIx64, h);
  return lw_blurred_thumb_cache_dir() + "/" + buf + ".png";
}

static cairo_surface_t* lw_load_cached_blurred_thumb(const std::string& videoPath) {
  std::string cp = lw_blurred_thumb_cache_path(videoPath);
  struct stat st{};
  if (::stat(cp.c_str(), &st) != 0) return nullptr;
  // Also check that the original video hasn't changed
  struct stat src{};
  if (::stat(videoPath.c_str(), &src) != 0) return nullptr;
  if (st.st_mtime < src.st_mtime) return nullptr;
  return cairo_image_surface_create_from_png(cp.c_str());
}

static void lw_save_blurred_thumb_cache(const std::string& videoPath, cairo_surface_t* surf) {
  std::string dir = lw_blurred_thumb_cache_dir();
  struct stat st{};
  if (::stat(dir.c_str(), &st) != 0) {
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return;
  }
  std::string cp = lw_blurred_thumb_cache_path(videoPath);
  cairo_surface_write_to_png(surf, cp.c_str());
}

void lw_remove_blurred_thumb_cache(const std::string& videoPath) {
  std::string cp = lw_blurred_thumb_cache_path(videoPath);
  std::error_code ec;
  std::filesystem::remove(cp, ec);
}

// Extract video frames at their native frame rate.
// If the full video has ≤200 frames, extracts the entire duration.
// Otherwise extracts a short segment (up to 3 s) at native fps,
// starting from seekPos (pass ≥0 to seek, or <0 for start).
// If knownDuration/knownFps are >0, they are used instead of probing.
static std::vector<cairo_surface_t*> lw_extract_video_frames(const std::string& path,
                                                               int tw, int th,
                                                               double& outFrameDurationMs,
                                                               double seekPos = -1.0,
                                                               double knownDuration = -1.0,
                                                               double knownFps = -1.0) {
  std::vector<cairo_surface_t*> frames;
  outFrameDurationMs = 33.0;
  if (tw <= 0 || th <= 0 || path.empty()) return frames;

  double duration = knownDuration > 0.0 ? knownDuration : lw_get_video_duration(path);
  if (duration <= 0.0) duration = 1.5;
  double fps = knownFps > 0.0 ? knownFps : lw_get_video_frame_rate(path);
  if (fps <= 0.0 || fps > 120.0) fps = 30.0;

  const int MAX_FRAMES = 200;
  double nativeTotal = duration * fps;
  bool fullExtract = nativeTotal <= static_cast<double>(MAX_FRAMES) + 0.5;
  double segmentDuration = fullExtract ? duration : std::min(3.0, duration);
  if (segmentDuration <= 0.0) segmentDuration = 1.5;

  if (seekPos < 0.0) seekPos = duration * 0.3 - segmentDuration * 0.5;
  seekPos = std::max(0.0, std::min(seekPos, duration - segmentDuration));

  outFrameDurationMs = 1000.0 / fps;
  int expectedFrames = std::max(1, static_cast<int>(segmentDuration * fps + 0.5));
  if (expectedFrames > MAX_FRAMES) expectedFrames = MAX_FRAMES;

  std::string sizeStr = std::to_string(tw) + ":" + std::to_string(th);
  std::string fpsStr = std::to_string(fps);
  std::string vfStr = "fps=" + fpsStr + ",scale=" + sizeStr
    + ":force_original_aspect_ratio=decrease,pad=" + sizeStr + ":(ow-iw)/2:(oh-ih)/2";
  std::string seekStr = fullExtract ? "" : std::to_string(seekPos);
  std::string durStr = fullExtract ? "" : std::to_string(segmentDuration);

  std::vector<const char*> argv;
  argv.reserve(24);
  auto add = [&](const char* s) { argv.push_back(s); };
  add("nice"); add("-n"); add("19"); add("ffmpeg");
  if (!fullExtract) {
    add("-ss"); add(seekStr.c_str());
  }
  add("-i"); add(path.c_str());
  if (!fullExtract) {
    add("-t"); add(durStr.c_str());
  }
  add("-vf"); add(vfStr.c_str());
  add("-f"); add("rawvideo");
  add("-pix_fmt"); add("rgba");
  add("-an"); add("-loglevel"); add("quiet");
  add("-y"); add("-nostdin");
  add("pipe:1");
  add(nullptr);

  int frameBytes = tw * th * 4;
  int maxBytes = frameBytes * expectedFrames;
  std::vector<std::uint8_t> raw;
  if (lw_run_cmd_and_read_stdout(argv, raw, maxBytes, 5000) <= 0) return frames;
  int nFrames = (int)raw.size() / frameBytes;
  if (nFrames <= 0) return frames;
  if (nFrames > expectedFrames) nFrames = expectedFrames;
  frames.reserve(static_cast<size_t>(nFrames));
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tw);
  int cairoStride = 0;
  for (int i = 0; i < nFrames; ++i) {
    const uint8_t* src = raw.data() + i * frameBytes;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(s);
      continue;
    }
    unsigned char* dst = cairo_image_surface_get_data(s);
    for (int y = 0; y < th; ++y) {
      const uint8_t* sr = src + y * tw * 4;
      uint32_t* dl = reinterpret_cast<uint32_t*>(dst + y * stride);
      for (int x = 0; x < tw; ++x) {
        uint32_t r = sr[0], g = sr[1], b = sr[2], a = sr[3];
        uint32_t pr = (r * a + 127) / 255;
        uint32_t pg = (g * a + 127) / 255;
        uint32_t pb = (b * a + 127) / 255;
        dl[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
        sr += 4;
      }
    }
    cairo_surface_mark_dirty(s);
    frames.push_back(s);

    if (i == 0) {
      cairoStride = cairo_image_surface_get_stride(s);
      uint32_t sum = 0;
      for (int k = 0; k < tw * 4 && k < 128; ++k) sum += src[k];
      uint32_t dsum = 0;
      for (int k = 0; k < cairoStride * th && k < 4096; ++k) dsum += dst[k];
      LW_LOG("vid_frame[0] stride=%d cairoStride=%d nFrames=%d rawBytes=%zu frameBytes=%d srcSum=%u dstSum=%u",
             stride, cairoStride, nFrames, raw.size(), frameBytes, sum, dsum);
    }
  }
  return frames;
}

void live_wallpaper_start_hover_video_preview(AppState& app, const std::string& path, size_t galleryIndex) {
  live_wallpaper_stop_hover_video_preview(app);
  if (path.empty() || !ends_with_video_ext(path)) return;
  app.liveWallpaperHoverVideoPath = path;
  app.liveWallpaperHoverVideoFrameIndex = 0;

  double duration = lw_get_video_duration(path);
  if (duration <= 0.0) duration = 1.5;
  double fps = lw_get_video_frame_rate(path);
  if (fps <= 0.0 || fps > 120.0) fps = 30.0;

  double seekPos = 0.0;
  double segment = std::min(3.0, duration);
  if (duration > segment && !app.liveWallpaperGalleryPaths.empty()) {
    double t = static_cast<double>(galleryIndex) / app.liveWallpaperGalleryPaths.size();
    seekPos = (duration - segment) * t;
  }

  double frameDurationMs = 1000.0 / fps;
  app.liveWallpaperHoverVideoFrames = lw_extract_video_frames(path, 320, 180, frameDurationMs, seekPos, duration, fps);
  app.liveWallpaperHoverVideoFrameDuration = app.liveWallpaperHoverVideoFrames.size() > 0
    ? std::chrono::milliseconds(static_cast<int>(frameDurationMs + 0.5))
    : std::chrono::milliseconds(33);
  app.liveWallpaperHoverVideoLastFrameTime = std::chrono::steady_clock::now();
}

void live_wallpaper_stop_hover_video_preview(AppState& app) {
  app.liveWallpaperHoverVideoPath.clear();
  app.liveWallpaperHoverVideoFrameIndex = 0;
  for (auto* s : app.liveWallpaperHoverVideoFrames)
    if (s) cairo_surface_destroy(s);
  app.liveWallpaperHoverVideoFrames.clear();
}

static bool lw_extract_video_frame(const std::string& videoPath,
                                    std::vector<std::uint8_t>& rgba,
                                    int& outW, int& outH,
                                    int maxPx) {
  rgba.clear();
  outW = outH = 0;
  try {
    ffmpegthumbnailer::VideoThumbnailer vt;
    vt.setThumbnailSize(maxPx);
    vt.setMaintainAspectRatio(true);
    vt.setSeekTime("00:00:01");
    std::vector<std::uint8_t> rawRgb;
    ffmpegthumbnailer::VideoFrameInfo info = vt.generateThumbnail(videoPath, Rgb, rawRgb);
    int w = info.width;
    int h = info.height;
    if (w <= 0 || h <= 0 || rawRgb.empty()) return false;
    rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (size_t i = 0, n = rawRgb.size() / 3; i < n; ++i) {
      rgba[i * 4 + 0] = rawRgb[i * 3 + 0];
      rgba[i * 4 + 1] = rawRgb[i * 3 + 1];
      rgba[i * 4 + 2] = rawRgb[i * 3 + 2];
      rgba[i * 4 + 3] = 255;
    }
    outW = w;
    outH = h;
    return true;
  } catch (const std::exception& e) {
    LW_LOG("ffmpegthumbnailer extraction failed for %s: %s", videoPath.c_str(), e.what());
    return false;
  }
}

static bool lw_gallery_recursive() noexcept {
  const char* e = std::getenv("EH_WALLPAPER_GALLERY_RECURSIVE");
  return e != nullptr && e[0] != '\0' && e[0] != '0';
}

std::vector<std::string> live_wallpaper_scan_video_files(const std::string& dir_path) {
  std::vector<std::string> out;
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root(dir_path);
  const fs::file_status st = fs::status(root, ec);
  if (ec || !fs::is_directory(st)) return out;
  constexpr size_t kMaxEntries = 4000;
  if (lw_gallery_recursive()) {
    try {
      for (const fs::directory_entry& ent :
           fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (out.size() >= kMaxEntries) break;
        if (!ent.is_regular_file(ec) || ec) continue;
        const fs::path& p = ent.path();
        const std::string fname = p.filename().string();
        if (!fname.empty() && fname[0] == '.') continue;
        if (!ends_with_video_ext(fname)) continue;
        const std::string full = ::eh::wallpaper::normalize_wallpaper_path(p.string());
        if (::access(full.c_str(), R_OK) == 0) out.push_back(full);
      }
    } catch (const fs::filesystem_error&) {}
  } else {
    DIR* d = opendir(dir_path.c_str());
    if (!d) return out;
    errno = 0;
    while (dirent* ent = readdir(d)) {
      if (out.size() >= kMaxEntries) break;
      if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
        continue;
      const std::string name = ent->d_name;
      if (!ends_with_video_ext(name)) continue;
      std::string full = dir_path;
      if (!full.empty() && full.back() != '/') full.push_back('/');
      full += name;
      if (::access(full.c_str(), R_OK) == 0) out.push_back(full);
    }
    closedir(d);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  if (out.size() > kMaxEntries) out.resize(kMaxEntries);
  return out;
}

void live_wallpaper_process_video_thumbnails(AppState& app) {
  if (app.liveWallpaperPendingVideoThumbs.empty()) return;
  LW_LOG("process_thumb start queue_len=%zu", app.liveWallpaperPendingVideoThumbs.size());

  // Batch ALL cached thumbnail loads first, then do at most one extraction.
  // This prevents N separate draw() calls for N cached files.
  while (!app.liveWallpaperPendingVideoThumbs.empty()) {
    const std::string path = app.liveWallpaperPendingVideoThumbs.front();
    app.liveWallpaperPendingVideoThumbs.erase(app.liveWallpaperPendingVideoThumbs.begin());

    if (app.liveWallpaperThumbs.find(path) != app.liveWallpaperThumbs.end()) continue;
    if (!ends_with_video_ext(path)) continue;

    // Try disk cache first
    cairo_surface_t* surf = lw_load_cached_thumb(path);
    if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
      app.liveWallpaperThumbs[path] = surf;
      { auto bit = app.liveWallpaperBlurredThumbs.find(path); if (bit != app.liveWallpaperBlurredThumbs.end()) { cairo_surface_destroy(bit->second); app.liveWallpaperBlurredThumbs.erase(bit); } }
      app.liveWallpaperThumbLru.remove(path);
      app.liveWallpaperThumbLru.push_back(path);
      if (path == app.liveWallpaperHeroRequest && path == app.config.image) {
        if (app.liveWallpaperHeroSurf) cairo_surface_destroy(app.liveWallpaperHeroSurf);
        app.liveWallpaperHeroSurf = surf;
        app.liveWallpaperHeroPath = path;
        app.liveWallpaperHeroRequest.clear();
        cairo_surface_reference(surf);
      }
      continue;  // cached load is fast — grab the next one
    }
    if (surf) cairo_surface_destroy(surf);

    // Cache miss — extract a single frame (expensive: fork/exec ffmpeg)
    {
      std::vector<std::uint8_t> rgba;
      int vw = 0, vh = 0;
      if (!lw_extract_video_frame(path, rgba, vw, vh, 512) || vw <= 0 || vh <= 0 || rgba.empty())
        return;
      ::eh::wallpaper::ThumbnailDecoded d;
      d.path = path; // fixed in previous step
      d.max_px = 512;
      d.width = vw;
      d.height = vh;
      d.rgba = std::move(rgba);
      surf = ::eh::wallpaper::thumbnail_decoded_to_surface(d);
      if (!surf) return;
      lw_save_thumb_cache(path, surf);
      LW_LOG("process_thumb extracted path=%s", path.c_str());
    }

    app.liveWallpaperThumbs[path] = surf;
    { auto bit = app.liveWallpaperBlurredThumbs.find(path); if (bit != app.liveWallpaperBlurredThumbs.end()) { cairo_surface_destroy(bit->second); app.liveWallpaperBlurredThumbs.erase(bit); } }
    app.liveWallpaperThumbLru.remove(path);
    app.liveWallpaperThumbLru.push_back(path);
    if (path == app.liveWallpaperHeroRequest && path == app.config.image) {
      if (app.liveWallpaperHeroSurf) cairo_surface_destroy(app.liveWallpaperHeroSurf);
      app.liveWallpaperHeroSurf = surf;
      app.liveWallpaperHeroPath = path;
      app.liveWallpaperHeroRequest.clear();
      cairo_surface_reference(surf);
    }
    break;  // one extraction per event-loop iteration
  }
}

// Video wallpaper application via mpvpaper.

static std::string lw_state_base() {
  if (const char* d = std::getenv("XDG_STATE_HOME")) return std::string(d);
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

static std::string lw_wallpaper_pid_path() {
  return lw_state_base() + "/event-horizon/wallpaper.pid";
}

static void lw_kill_previous_wallpaper() {
  FILE* f = fopen(lw_wallpaper_pid_path().c_str(), "rb");
  if (!f) return;
  char buf[64]{};
  if (fgets(buf, sizeof(buf), f)) {
    pid_t old = static_cast<pid_t>(std::strtol(buf, nullptr, 10));
    if (old > 1) {
      kill(old, SIGTERM);
      for (int i = 0; i < 50; ++i) {
        if (kill(old, 0) != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      if (kill(old, 0) == 0) kill(old, SIGKILL);
    }
  }
  fclose(f);
  unlink(lw_wallpaper_pid_path().c_str());
}

static void lw_write_pid(pid_t p) {
  FILE* f = fopen(lw_wallpaper_pid_path().c_str(), "wb");
  if (!f) return;
  fprintf(f, "%d\n", static_cast<int>(p));
  fclose(f);
}

void live_wallpaper_apply_video(const std::string& videoPath) {
  lw_kill_previous_wallpaper();

  std::vector<const char*> argv = {
    "mpvpaper", "-o", "--loop --no-audio --hwdec=auto", "*", videoPath.c_str(), nullptr
  };

  pid_t pid = fork();
  if (pid < 0) return;
  if (pid == 0) {
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(const_cast<char*>(a));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }

  int st = 0;
  waitpid(pid, &st, WNOHANG);
  if (WIFEXITED(st) && WEXITSTATUS(st) == 127) return;
  lw_write_pid(pid);
}

void live_wallpaper_clear_video_wallpaper() {
  lw_kill_previous_wallpaper();
}

static std::string lw_video_frame_png_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  if (!state || !state[0]) state = std::getenv("HOME");
  if (state && state[0]) {
    std::string base(state);
    base += std::getenv("XDG_STATE_HOME") ? "/event-horizon/video-frame.png" : "/.local/state/event-horizon/video-frame.png";
    return base;
  }
  return "/tmp/event-horizon/video-frame.png";
}

static void lw_save_video_frame_as_png(const std::string& videoPath, const std::string& pngPath) {
  try {
    ffmpegthumbnailer::VideoThumbnailer vt;
    vt.setThumbnailSize(512);
    vt.setMaintainAspectRatio(true);
    vt.setSeekTime("00:00:01");
    vt.generateThumbnail(videoPath, Png, pngPath);
  } catch (const std::exception& e) {
    LW_LOG("ffmpegthumbnailer PNG save failed for %s: %s", videoPath.c_str(), e.what());
  }
}

void live_wallpaper_apply_video_with_matugen(AppState& app, const std::string& videoPath) {
  live_wallpaper_apply_video(videoPath);

  // Extract a still frame for dynamic color generation (local to this standalone app)
  std::string framePath = lw_video_frame_png_path();
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(framePath).parent_path(), ec);
  lw_save_video_frame_as_png(videoPath, framePath);

  // Run palette generation for *this app's* chrome only. We no longer write main shell state
  // toml from here (separation). If the user wants the frame as system wallpaper,
  // the apply path will handle it.
  ::eh::config::ShellAppearance ap{};
  ::eh::matugen::refresh_wallpaper_derived_palette(ap, framePath);

  app.drawChrome = ::eh::config::derived_chrome_colors(ap);
  app.drawChromeMatugen = ap.anyPaletteActive();
}

static void wallpaper_sort_paths_inplace(std::vector<std::string>& paths, int sortMode,
                                          std::unordered_map<std::string, time_t>& mtimeCache) {
  if (paths.empty()) return;
  if (sortMode == 0) {
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
      const std::string ba = wallpaper_file_basename(a);
      const std::string bb = wallpaper_file_basename(b);
      if (ba != bb) return ba < bb;
      return a < b;
    });
    return;
  }
  struct Entry { std::string path; time_t mt = 0; };
  std::vector<Entry> tmp;
  tmp.reserve(paths.size());
  for (const auto& p : paths) {
    auto it = mtimeCache.find(p);
    if (it != mtimeCache.end()) {
      tmp.push_back({p, it->second});
    } else {
      struct stat st {};
      time_t mt = 0;
      if (::stat(p.c_str(), &st) == 0) mt = st.st_mtime;
      mtimeCache[p] = mt;
      tmp.push_back({p, mt});
    }
  }
  if (sortMode == 1) {
    std::sort(tmp.begin(), tmp.end(), [](const Entry& a, const Entry& b) {
      if (a.mt != b.mt) return a.mt < b.mt;
      return a.path < b.path;
    });
  } else {
    std::sort(tmp.begin(), tmp.end(), [](const Entry& a, const Entry& b) {
      if (a.mt != b.mt) return a.mt > b.mt;
      return a.path < b.path;
    });
  }
  paths.clear();
  for (auto& e : tmp) paths.push_back(std::move(e.path));
}

static std::string wallpaper_folder_key(const std::string& folder) {
  if (folder.empty()) return {};
  std::string o = folder;
  while (o.size() > 1 && (o.back() == '/' || o.back() == '\\')) o.pop_back();
  return o;
}

WallpaperTabLayout live_wallpaper_tab_layout(const AppState& app, int cardInsetX, int contentW, int galleryGridTopY) {
  WallpaperTabLayout L{};
  L.galleryTop = galleryGridTopY;
  const int availW = contentW - 8 - 2 * kCardPad;
  const double s = std::min(static_cast<double>(app.width) / 1920.0, static_cast<double>(app.height) / 1080.0);
  L.gap = std::max(4, static_cast<int>(kWpThumbGap * std::clamp(s, 0.4, 2.5)));

  // Compute cols from available width using scale as size target
  {
    const double scale = static_cast<double>(std::clamp(app.liveWallpaperGalleryScalePct, 50, 200)) / 100.0;
    int targetW = static_cast<int>(420.0 * scale);
    targetW = std::clamp(targetW, 60, 800);
    L.cols = std::max(1, (availW + L.gap) / (targetW + L.gap));
    L.thumb = (availW - L.gap * (L.cols - 1)) / L.cols;
    L.thumb = std::max(40, L.thumb);
    L.thumbH = (L.thumb * 9 + 8) / 16;
  }

  // Compute rows from available height
  {
    const int availH = app.height - galleryGridTopY - static_cast<int>(40.0 * std::clamp(s, 0.4, 2.5));
    const int rowStride = L.thumbH + L.gap;
    L.rows = std::max(1, availH / rowStride);
  }

  L.perPage = L.cols * L.rows;
  const size_t n = app.liveWallpaperGalleryPaths.size();
  L.navW = std::max(48, static_cast<int>(80.0 * std::clamp(s, 0.4, 2.5)));
  L.navH = std::max(24, static_cast<int>(32.0 * std::clamp(s, 0.4, 2.5)));
  const int rowStrideNav = L.thumbH + kWpThumbLabelH + L.gap;
  const int gridBottomNav = L.galleryTop + L.rows * rowStrideNav;
  L.navY = gridBottomNav + static_cast<int>(14.0 * std::clamp(s, 0.4, 2.5));
  L.showNav = n > static_cast<size_t>(L.perPage);
  if (L.showNav) {
    L.galleryBottom = L.navY - static_cast<int>(10.0 * std::clamp(s, 0.4, 2.5));
  } else {
    L.galleryBottom = gridBottomNav + static_cast<int>(16.0 * std::clamp(s, 0.4, 2.5));
  }
  L.prevX = cardInsetX + kCardPad;
  L.nextX = cardInsetX + contentW - kCardPad - L.navW;
  return L;
}

void wallpaper_clamp_page_lw(AppState& app, int perPage) {
  if (perPage < 1) perPage = 1;
  const size_t n = app.liveWallpaperGalleryPaths.size();
  const int pages = std::max(1, static_cast<int>((n + static_cast<size_t>(perPage) - 1) / static_cast<size_t>(perPage)));
  if (app.liveWallpaperGalleryPage >= pages) app.liveWallpaperGalleryPage = pages - 1;
  if (app.liveWallpaperGalleryPage < 0) app.liveWallpaperGalleryPage = 0;
}

void ensure_live_wallpaper_gallery(AppState& app) {
  if (app.config.folder.empty()) {
    if (!app.liveWallpaperGalleryPaths.empty()) {
      ::eh::wallpaper::WallpaperThumbnailService::instance().release_all();
      for (auto& e : app.liveWallpaperThumbs) {
        if (e.second) cairo_surface_destroy(e.second);
      }
      app.liveWallpaperThumbs.clear();
      for (auto& [_, s] : app.liveWallpaperBlurredThumbs)
        if (s) cairo_surface_destroy(s);
      app.liveWallpaperBlurredThumbs.clear();
      app.liveWallpaperThumbLru.clear();
      app.liveWallpaperGalleryPaths.clear();
      app.liveWallpaperGalleryMtimeCache.clear();
      app.liveWallpaperGalleryPage = 0;
    }
    app.liveWallpaperGalleryFolderSynced.clear();
    app.liveWallpaperGalleryFolderMtime = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    return;
  }
  const std::string key = wallpaper_folder_key(app.config.folder);
  const auto now = std::chrono::steady_clock::now();
  constexpr auto kStatDebounce = std::chrono::milliseconds(500);
  bool needsRescan = false;
  if (now - app.liveWallpaperGalleryLastStatCheck >= kStatDebounce) {
    app.liveWallpaperGalleryLastStatCheck = now;
    struct stat st {};
    const time_t curMtime = (::stat(key.c_str(), &st) == 0) ? st.st_mtime : 0;
    if (app.liveWallpaperGalleryFolderSynced != key || app.liveWallpaperGalleryFolderMtime != curMtime) {
      needsRescan = true;
      app.liveWallpaperGalleryFolderSynced = key;
      app.liveWallpaperGalleryFolderMtime = curMtime;
    }
  }
  if (needsRescan || app.liveWallpaperGalleryPaths.empty()) {
    ::eh::wallpaper::WallpaperThumbnailService::instance().release_all();
    for (auto& e : app.liveWallpaperThumbs) {
      if (e.second) cairo_surface_destroy(e.second);
    }
    app.liveWallpaperThumbs.clear();
    for (auto& [_, s] : app.liveWallpaperBlurredThumbs)
      if (s) cairo_surface_destroy(s);
    app.liveWallpaperBlurredThumbs.clear();
    app.liveWallpaperThumbLru.clear();
    app.liveWallpaperPendingVideoThumbs.clear();
    auto imgPaths = ::eh::wallpaper::scan_image_files(key.empty() ? app.config.folder : key);
    auto vidPaths = live_wallpaper_scan_video_files(key.empty() ? app.config.folder : key);
    app.liveWallpaperGalleryPaths = std::move(imgPaths);
    app.liveWallpaperGalleryPaths.insert(app.liveWallpaperGalleryPaths.end(), vidPaths.begin(), vidPaths.end());
    app.liveWallpaperGalleryMtimeCache.clear();
    app.liveWallpaperGalleryPage = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    app.liveWallpaperGalleryPrecached = false;
  }
  if (app.liveWallpaperGallerySortMode != app.liveWallpaperGallerySortModeApplied) {
    wallpaper_sort_paths_inplace(app.liveWallpaperGalleryPaths, app.liveWallpaperGallerySortMode, app.liveWallpaperGalleryMtimeCache);
    app.liveWallpaperGallerySortModeApplied = app.liveWallpaperGallerySortMode;
  }
  if (!app.liveWallpaperGalleryPrecached && !app.liveWallpaperGalleryPaths.empty()) {
    app.liveWallpaperGalleryPrecached = true;
    for (const auto& fp : app.liveWallpaperGalleryPaths) {
      if (ends_with_video_ext(fp)) {
        app.liveWallpaperPendingVideoThumbs.push_back(fp);
      } else {
        ::eh::wallpaper::WallpaperThumbnailService::instance().request(fp, 512);
      }
    }
  }
}

void live_wallpaper_invalidate_hero(AppState& app) {
  if (app.liveWallpaperHeroSurf) {
    cairo_surface_destroy(app.liveWallpaperHeroSurf);
    app.liveWallpaperHeroSurf = nullptr;
  }
  app.liveWallpaperHeroPath.clear();
}

static void live_wallpaper_ensure_hero_surface(AppState& app) {
  const std::string& img = app.config.image;
  if (img.empty()) {
    live_wallpaper_invalidate_hero(app);
    return;
  }
  if (app.liveWallpaperHeroPath == img && app.liveWallpaperHeroSurf &&
      cairo_surface_status(app.liveWallpaperHeroSurf) == CAIRO_STATUS_SUCCESS)
    return;
  if (!app.liveWallpaperHeroRequest.empty()) {
    if (app.liveWallpaperHeroRequest == img) return;
    app.liveWallpaperHeroRequest.clear();
  }
  app.liveWallpaperHeroRequest = img;
  if (ends_with_video_ext(img)) {
    if (std::find(app.liveWallpaperPendingVideoThumbs.begin(), app.liveWallpaperPendingVideoThumbs.end(), img) == app.liveWallpaperPendingVideoThumbs.end())
      app.liveWallpaperPendingVideoThumbs.push_front(img);
  } else {
    ::eh::wallpaper::WallpaperThumbnailService::instance().request(img, 512);
  }
}

void live_wallpaper_cycle_selection(AppState& app, int delta) {
  ensure_live_wallpaper_gallery(app);
  if (app.liveWallpaperGalleryPaths.empty()) return;
  const std::string& cur = app.config.image;
  auto it = std::find(app.liveWallpaperGalleryPaths.begin(), app.liveWallpaperGalleryPaths.end(), cur);
  size_t idx = 0;
  if (it != app.liveWallpaperGalleryPaths.end()) idx = static_cast<size_t>(it - app.liveWallpaperGalleryPaths.begin());
  const size_t n = app.liveWallpaperGalleryPaths.size();
  idx = (idx + static_cast<size_t>(delta) + n) % n;
  app.config.image = app.liveWallpaperGalleryPaths[idx];
  app.config.enabled = true;
}

void schedule_frame(AppState& app) {
  if (!app.surface) return;
  if (app.surfaceFrameCb) return;

  static const wl_callback_listener kListener = {
    .done = [](void* data, wl_callback* cb, uint32_t) {
      wl_callback_destroy(cb);
      auto& a = *static_cast<AppState*>(data);
      a.surfaceFrameCb = nullptr;
      draw(a);
    }
  };

  app.surfaceFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.surfaceFrameCb, &kListener, &app);
}

// Draw function.

void draw(AppState& app) {
  if (!app.surface) return;

  // Advance hover animations
  app.liveWallpaperHoverAnim.tick();

  static int64_t lastLog = 0;
  static int frameCount = 0;
  frameCount++;
  int64_t now = lw_now_us();
  int64_t t0 = now;
  bool doStepLog = (now - lastLog > 1000000);
  if (now - lastLog > 2000000) {
    int fps = (frameCount * 1000000) / (now - lastLog + 1);
    LW_LOG("FPS=%d  pendingVidThumbs=%zu  hoverFrames=%zu  thumbMap=%zu",
           fps, app.liveWallpaperPendingVideoThumbs.size(),
           app.liveWallpaperHoverVideoFrames.size(),
           app.liveWallpaperThumbs.size());
    frameCount = 0;
    lastLog = now;
  }

  int paintBi = -1;
  for (int i = 0; i < 2; ++i) {
    if (!app.buf[i].busy()) { paintBi = i; break; }
  }
  if (paintBi < 0) {
    app.pendingRedraw = true;
    return;
  }

  cairo_t* cr = app.buf[paintBi].cairo();
  cairo_save(cr);

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  // Background (at user-chosen opacity; UI elements stay fully opaque)
  {
    const double bg_op = std::clamp(static_cast<double>(app.liveWallpaperUiOpacityPct), 0.0, 100.0) / 100.0;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.drawerDimR;
      g = app.drawChrome.drawerDimG;
      b = app.drawChrome.drawerDimB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    cairo_set_source_rgba(cr, r, g, b, bg_op);
    cairo_rectangle(cr, 0, 0, app.width, app.height);
    cairo_fill(cr);
  }
  if (doStepLog) LW_LOG("step bg_fill dt=%lld", (long long)(lw_now_us() - t0));

  // Compute new layout
  const LiveWallpaperLayout LW = compute_lw_layout(app.width, app.height);
  const int contentW = app.width - 2 * LW.marginX;
  const double pyH = app.pointerY;

  // Load wallpaper gallery
  ensure_live_wallpaper_gallery(app);
  if (doStepLog) LW_LOG("step layout+gallery dt=%lld", (long long)(lw_now_us() - t0));

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  lw_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  // Tab bar (underline style)
  {
    // Bottom border line
    cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, LW.marginX, LW.tabY + LW.tabH - 0.5);
    cairo_line_to(cr, LW.marginX + contentW, LW.tabY + LW.tabH - 0.5);
    cairo_stroke(cr);

    // Tab 0: Wallpaper
    lw_show_text(cr, LW.marginX + 16 * LW.scale, LW.tabY + LW.tabH / 2 + 6 * LW.scale, "Wallpaper",
                 static_cast<float>(LW.tabFont), 500, t_r, t_g, t_b, app.liveWallpaperUiSubTab == 0 ? 0.95f : 0.5f);
    if (app.liveWallpaperUiSubTab == 0) {
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.9);
      cairo_round_rect(cr, LW.marginX + 16, LW.tabY + LW.tabH - 3, LW.tab0W - 32, 3, 1.5);
      cairo_fill(cr);
    }

    // Tab 1: Gallery settings
    const int tab1X = LW.marginX + LW.tab0W;
    lw_show_text(cr, tab1X + 16, LW.tabY + LW.tabH / 2 + 6, "Gallery settings", 16, 500,
                 t_r, t_g, t_b, app.liveWallpaperUiSubTab == 1 ? 0.95f : 0.5f);
    if (app.liveWallpaperUiSubTab == 1) {
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.9);
      cairo_round_rect(cr, tab1X + 16, LW.tabY + LW.tabH - 3, LW.tab1W - 32, 3, 1.5);
      cairo_fill(cr);
    }
  }
  if (doStepLog) LW_LOG("step tabbar dt=%lld", (long long)(lw_now_us() - t0));

  if (app.liveWallpaperUiSubTab == 1) {
    // Gallery Settings tab.

    const int gsX = LW.marginX;
    const int gsW = std::min(640, contentW);
    const int gsY = LW.contentStartY;

    // Glass card background
    {
      m3::Box glassCard;
      glassCard.setColor(1, 1, 1, 0.07f);
      glassCard.setRadius(24.0f);
      glassCard.setGeometry(gsX, gsY, gsW, 480);
      glassCard.paint(cr);

      cairo_round_rect(cr, gsX, gsY, gsW, 480, 24.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Heading
    lw_show_text(cr, gsX + 32, gsY + 36, "Grid Layout", 18, 600, t_r, t_g, t_b, 1.0f);
    lw_show_text(cr, gsX + 32, gsY + 56, "Session only \u2014 not written to shell config.", 12, 400, t_r, t_g, t_b, 0.55f);

    // Folder picker
    const int galPickerY = gsY + 88;
    const int galPickerH = 28;
    {
      const int pbx = gsX + 32;
      const int pbw = gsW - 64;
      const bool phov = point_in_rect(app.pointerX, pyH, pbx, galPickerY, pbw, galPickerH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Folder dialog (native / kdialog / \u2026)");
      pickerBtn.setGeometry(pbx, galPickerY, pbw, galPickerH);
      pickerBtn.setStyle(m3::Button::Style::Outlined);
      pickerBtn.setSize(m3::Button::Size::XS);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    // Settings rows
    const int row0 = galPickerY + galPickerH + 20;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = gsX + gsW - 32 - 2 * btnW - 10;
    const int plusX = gsX + gsW - 32 - btnW;

    auto paint_gs_row = [&](int row, const char* label, const char* valStr) {
      const int ry = row0 + row * rowPitch;
      lw_show_text(cr, gsX + 32, ry + 20, label, 13, 500, t_r, t_g, t_b, 1.0f);
      lw_show_text(cr, gsX + 240, ry + 20, valStr, 13, 500, t_r, t_g, t_b, 1.0f);
      const bool hm = point_in_rect(app.pointerX, pyH, minusX, ry + 4, btnW, btnH);
      const bool hp = point_in_rect(app.pointerX, pyH, plusX, ry + 4, btnW, btnH);
      m3::Button mBtn;
      mBtn.setMinSize(0, 0);
      mBtn.setLabel("\u2212");
      mBtn.setGeometry(minusX, ry + 4, btnW, btnH);
      mBtn.setStyle(m3::Button::Style::Outlined);
      mBtn.setSize(m3::Button::Size::M);
      mBtn.setAccentColor(a_r, a_g, a_b);
      mBtn.setOutlineColor(o_r, o_g, o_b);
      mBtn.setHovered(hm);
      mBtn.paint(cr);
      m3::Button pBtn;
      pBtn.setMinSize(0, 0);
      pBtn.setLabel("+");
      pBtn.setGeometry(plusX, ry + 4, btnW, btnH);
      pBtn.setStyle(m3::Button::Style::Outlined);
      pBtn.setSize(m3::Button::Size::M);
      pBtn.setAccentColor(a_r, a_g, a_b);
      pBtn.setOutlineColor(o_r, o_g, o_b);
      pBtn.setHovered(hp);
      pBtn.paint(cr);
    };
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", app.liveWallpaperGalleryColumns);
    paint_gs_row(0, "Columns", buf);
    std::snprintf(buf, sizeof(buf), "%d", app.liveWallpaperGalleryRows);
    paint_gs_row(1, "Rows", buf);
    std::snprintf(buf, sizeof(buf), "%d%%", app.liveWallpaperGalleryScalePct);
    paint_gs_row(2, "Thumbnail scale", buf);
    std::snprintf(buf, sizeof(buf), "%d px", app.liveWallpaperGalleryThumbRadiusPx);
    paint_gs_row(3, "Corner radius", buf);

    // Opacity slider
    {
      const int sRow = 4;
      const int ry = row0 + sRow * rowPitch;
      const int trackH = 6, thumbR = 10;
      const int trackY = ry + rowPitch / 2 - trackH / 2;
      const int trackX = gsX + 240;
      const int trackW = gsW - 64 - 240;

      lw_show_text(cr, gsX + 32, ry + 20, "Window opacity", 13, 500, t_r, t_g, t_b, 1.0f);

      cairo_round_rect(cr, trackX, trackY, trackW, trackH, 3.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.3);
      cairo_fill(cr);

      const int fillW = static_cast<int>(static_cast<double>(trackW) * static_cast<double>(app.liveWallpaperUiOpacityPct) / 100.0);
      if (fillW > 2) {
        cairo_round_rect(cr, trackX, trackY, fillW, trackH, 3.0);
        cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.85);
        cairo_fill(cr);
      }

      const int thumbCx = trackX + fillW;
      const int thumbCy = trackY + trackH / 2;
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 1.0);
      cairo_arc(cr, thumbCx, thumbCy, thumbR, 0, 2.0 * M_PI);
      cairo_fill(cr);

      char pct_buf[8];
      std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", app.liveWallpaperUiOpacityPct);
      lw_show_text(cr, gsX + gsW - 32 - 40, ry + 20, pct_buf, 13, 500, t_r, t_g, t_b, 1.0f);
    }

    // Separator + folder dialog section
    {
      const int sepY = row0 + 5 * rowPitch + 8;
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.2);
      cairo_move_to(cr, gsX + 32, sepY);
      cairo_line_to(cr, gsX + gsW - 32, sepY);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      const int fdgY = sepY + 24;
      lw_show_text(cr, gsX + 32, fdgY + 14, "Folder dialog", 12, 400, t_r, t_g, t_b, 0.55f);

      const int fdgBoxY = fdgY + 28;
      const int fdgBoxH = 36;
      m3::Box fdgBox;
      fdgBox.setColor(0, 0, 0, 0.25f);
      fdgBox.setRadius(14.0f);
      fdgBox.setGeometry(gsX + 32, fdgBoxY, gsW - 64, fdgBoxH);
      fdgBox.paint(cr);

      cairo_round_rect(cr, gsX + 32, fdgBoxY, gsW - 64, fdgBoxH, 14.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.3);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      lw_show_text(cr, gsX + 48, fdgBoxY + 22, "Folder dialog (native / kdialog / \u2026)", 12, 400, t_r, t_g, t_b, 0.5f);
    }
    if (doStepLog) LW_LOG("step gallery_settings dt=%lld", (long long)(lw_now_us() - t0));
  } else {
    // Wallpaper tab (hero, controls, gallery).
    live_wallpaper_ensure_hero_surface(app);

    const int heroX = LW.marginX;
    const int heroY = LW.contentStartY;
    const int heroW = LW.heroW;
    const int heroH = LW.heroH;
    const int ctrlX = LW.ctrlX;
    const int ctrlW = LW.ctrlW;

    // ═══ HERO AREA (left column) ═══

    cairo_save(cr);
    cairo_round_rect(cr, heroX, heroY, heroW, heroH, 16.0);
    cairo_clip(cr);
    paint_src_bg(app, cr, 0.94);
    cairo_paint(cr);
    if (app.liveWallpaperHeroSurf && cairo_surface_status(app.liveWallpaperHeroSurf) == CAIRO_STATUS_SUCCESS) {
      cairo_surface_flush(app.liveWallpaperHeroSurf);
      const int iw = cairo_image_surface_get_width(app.liveWallpaperHeroSurf);
      const int ih = cairo_image_surface_get_height(app.liveWallpaperHeroSurf);
      if (iw > 0 && ih > 0) {
        const double sc = std::max(heroW / static_cast<double>(iw), heroH / static_cast<double>(ih));
        const double dispW = static_cast<double>(iw) * sc;
        const double dispH = static_cast<double>(ih) * sc;
        const double ox = heroX + (heroW - dispW) * 0.5;
        const double oy = heroY + (heroH - dispH) * 0.5;
        cairo_translate(cr, ox, oy);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.liveWallpaperHeroSurf, 0, 0);
        cairo_paint(cr);
      }
    }
    // Gradient overlays
    cairo_pattern_t* gTop = cairo_pattern_create_linear(heroX, heroY, heroX, heroY + 64);
    cairo_pattern_add_color_stop_rgba(gTop, 0, 0, 0, 0, 0.5);
    cairo_pattern_add_color_stop_rgba(gTop, 1, 0, 0, 0, 0);
    cairo_set_source(cr, gTop);
    cairo_rectangle(cr, heroX, heroY, heroW, 64);
    cairo_fill(cr);
    cairo_pattern_destroy(gTop);

    cairo_pattern_t* gBot = cairo_pattern_create_linear(heroX, heroY + heroH - 72, heroX, heroY + heroH);
    cairo_pattern_add_color_stop_rgba(gBot, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(gBot, 1, 0, 0, 0, 0.55);
    cairo_set_source(cr, gBot);
    cairo_rectangle(cr, heroX, heroY + heroH - 72, heroW, 72);
    cairo_fill(cr);
    cairo_pattern_destroy(gBot);

    cairo_restore(cr);
    if (doStepLog) LW_LOG("step hero_surf dt=%lld", (long long)(lw_now_us() - t0));

    // Hero border
    cairo_round_rect(cr, heroX, heroY, heroW, heroH, 16.0);
    cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Hero nav arrows
    {
      const int navSize = LW.heroNavSize;
      const double nOff = 16.0 * LW.scale;
      const int navY = heroY + heroH - navSize - static_cast<int>(nOff);
      const double nPad = 8.0 * LW.scale;
      const bool hL = point_in_rect(app.pointerX, pyH, heroX + static_cast<int>(nPad), navY, navSize, navSize);
      const bool hR = point_in_rect(app.pointerX, pyH, heroX + heroW - navSize - static_cast<int>(nPad), navY, navSize, navSize);

      cairo_round_rect(cr, heroX + nPad, navY, navSize, navSize, 10.0 * LW.scale);
      cairo_set_source_rgba(cr, 0, 0, 0, hL ? 0.7 : 0.5);
      cairo_fill(cr);
      material_symbols_draw_glyph(cr, heroX + nPad + navSize / 2.0, navY + navSize / 2.0,
                                  static_cast<int>(18.0 * LW.scale), "chevron_left", 1, 1, 1, 0.85);

      cairo_round_rect(cr, heroX + heroW - navSize - nPad, navY, navSize, navSize, 10.0 * LW.scale);
      cairo_set_source_rgba(cr, 0, 0, 0, hR ? 0.7 : 0.5);
      cairo_fill(cr);
      material_symbols_draw_glyph(cr, heroX + heroW - navSize - nPad + navSize / 2.0, navY + navSize / 2.0,
                                  static_cast<int>(18.0 * LW.scale), "chevron_right", 1, 1, 1, 0.85);
    }

    // ═══ CONTROLS PANEL (right column) ═══

    // Fill Mode card
    {
      const int fillCardH = LW.fillCardH;
      const double fRad = 16.0 * LW.scale;
      m3::Box fillCard;
      fillCard.setColor(1, 1, 1, 0.07f);
      fillCard.setRadius(static_cast<float>(fRad));
      fillCard.setGeometry(ctrlX, heroY, ctrlW, fillCardH);
      fillCard.paint(cr);

      cairo_round_rect(cr, ctrlX, heroY, ctrlW, fillCardH, fRad);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      lw_show_text(cr, ctrlX + static_cast<int>(16.0 * LW.scale), heroY + static_cast<int>(22.0 * LW.scale),
                   "Fill Mode", static_cast<float>(LW.galleryFont), 400, t_r, t_g, t_b, 0.55f);

      const int comboX = ctrlX + ctrlW - static_cast<int>(16.0 * LW.scale) - LW.comboW;
      const int comboY = heroY + (fillCardH - LW.comboH) / 2;
      const int mm = std::clamp(app.config.mode, 0, 4);
      lw_paint_combo_closed(app, cr, comboX, comboY, LW.comboW, LW.comboH, kWallpaperModeLabels[mm],
                            app.liveWallpaperModeDropdownOpen);
    }

    // "Open Picker..." button
    {
      const double gap = 12.0 * LW.scale;
      const int btnY = heroY + LW.fillCardH + static_cast<int>(gap);
      const bool phov = point_in_rect(app.pointerX, pyH, ctrlX, btnY, ctrlW, LW.openPickerH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Open Picker...");
      pickerBtn.setGeometry(ctrlX, btnY, ctrlW, LW.openPickerH);
      pickerBtn.setStyle(m3::Button::Style::Filled);
      pickerBtn.setSize(m3::Button::Size::XS);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    // Action buttons (folder, grid_view, delete)
    {
      const double gap = 12.0 * LW.scale;
      const int actY = heroY + LW.fillCardH + static_cast<int>(gap) + LW.openPickerH + static_cast<int>(gap);
      const int actBtnW = (ctrlW - static_cast<int>(8.0 * LW.scale)) / 3;
      static const char* kActGlyphs[] = {"folder", "grid_view", "delete"};
      for (int ai = 0; ai < 3; ++ai) {
        const int ax = ctrlX + ai * (actBtnW + static_cast<int>(4.0 * LW.scale));
        const bool ahov = point_in_rect(app.pointerX, pyH, ax, actY, actBtnW, LW.actBtnH);
        m3::Button actBtn;
        actBtn.setMinSize(0, 0);
        actBtn.setGlyph(kActGlyphs[ai]);
        actBtn.setGeometry(ax, actY, actBtnW, LW.actBtnH);
        actBtn.setStyle(m3::Button::Style::Outlined);
        actBtn.setSize(m3::Button::Size::XS);
        actBtn.setAccentColor(a_r, a_g, a_b);
        actBtn.setOutlineColor(o_r, o_g, o_b);
        actBtn.setHovered(ahov);
        actBtn.paint(cr);
      }
    }

    // ═══ GALLERY SECTION ═══
    if (doStepLog) LW_LOG("step controls dt=%lld", (long long)(lw_now_us() - t0));

    const int galleryY = LW.galleryY;

    // Gallery header: full folder path + count
    {
      std::ostringstream hd;
      if (app.config.folder.empty()) {
        hd << "No folder";
      } else {
        hd << app.config.folder;
      }
      hd << "  \u00b7  " << app.liveWallpaperGalleryPaths.size() << " wallpapers";
      const std::string hds = hd.str();
      lw_show_text(cr, LW.marginX, galleryY + 4 * LW.scale, hds.c_str(),
                   static_cast<float>(LW.galleryFont), 400, t_r, t_g, t_b, 0.55f);
    }

    // Sort pills
    {
      const int pillY = LW.sortPillsY;
      const int pillH = std::max(24, LW.pillH);
      const char* plab[] = {"Name", "Oldest", "Newest"};
      int px = LW.marginX;
      for (int pi = 0; pi < 3; ++pi) {
        const int textW = lw_text_width(plab[pi], 12.0f, 500);
        const int minPw = textW + 36;
        const int pww = std::max(minPw, static_cast<int>((pi == 0 ? 72.0 : 88.0) * LW.scale));
        const bool sel = app.liveWallpaperGallerySortMode == pi;
        const bool ph = point_in_rect(app.pointerX, pyH, px, pillY, pww, pillH);

        {
          m3::Button pillBtn;
          pillBtn.setMinSize(0, 0);
          pillBtn.setLabel(plab[pi]);
          pillBtn.setGeometry(px, pillY, pww, pillH);
          pillBtn.setStyle(sel ? m3::Button::Style::Filled : m3::Button::Style::Outlined);
          pillBtn.setSize(m3::Button::Size::XS);
          pillBtn.setAccentColor(a_r, a_g, a_b);
          pillBtn.setOutlineColor(o_r, o_g, o_b);
          pillBtn.setHovered(ph);
          pillBtn.paint(cr);
        }

        px += pww + static_cast<int>(8.0 * LW.scale);
      }
    }

    // Thumbnail grid.
    if (doStepLog) LW_LOG("step gallery_header+pills dt=%lld", (long long)(lw_now_us() - t0));
    {
      const int cardInsetXi = LW.marginX;
      WallpaperTabLayout WL = live_wallpaper_tab_layout(app, cardInsetXi, contentW, LW.galleryGridY);
      wallpaper_clamp_page_lw(app, WL.perPage);

      const double gx0d = static_cast<double>(cardInsetXi);
      const size_t gallOff = static_cast<size_t>(app.liveWallpaperGalleryPage * WL.perPage);
      const std::string& preferImg = app.config.image;
      const int decodePx = std::clamp(std::max(WL.thumb, WL.thumbH), 64, 1600);

      // Advance video hover preview frame
      if (!app.liveWallpaperHoverVideoFrames.empty() && !app.liveWallpaperHoverVideoPath.empty()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - app.liveWallpaperHoverVideoLastFrameTime;
        auto frameDur = app.liveWallpaperHoverVideoFrameDuration;
        if (elapsed >= frameDur && app.liveWallpaperHoverVideoFrameIndex + 1 < static_cast<int>(app.liveWallpaperHoverVideoFrames.size())) {
          int steps = static_cast<int>(elapsed / frameDur);
          int maxFrame = static_cast<int>(app.liveWallpaperHoverVideoFrames.size()) - 1;
          app.liveWallpaperHoverVideoFrameIndex = std::min(app.liveWallpaperHoverVideoFrameIndex + steps, maxFrame);
          app.liveWallpaperHoverVideoLastFrameTime += frameDur * steps;
        }
      }

      if (!preferImg.empty() && app.liveWallpaperThumbs.find(preferImg) == app.liveWallpaperThumbs.end()) {
        if (ends_with_video_ext(preferImg)) {
          if (std::find(app.liveWallpaperPendingVideoThumbs.begin(), app.liveWallpaperPendingVideoThumbs.end(), preferImg) == app.liveWallpaperPendingVideoThumbs.end())
            app.liveWallpaperPendingVideoThumbs.push_back(preferImg);
        } else {
          ::eh::wallpaper::WallpaperThumbnailService::instance().request(preferImg, decodePx);
        }
      }

      const size_t nSlots =
          (app.liveWallpaperGalleryPaths.size() <= gallOff)
              ? static_cast<size_t>(0)
              : std::min(static_cast<size_t>(WL.perPage), app.liveWallpaperGalleryPaths.size() - gallOff);

      // Pre-load blurred thumbnails from disk cache for visible adult-marked items
      // so the draw loop below never blocks on synchronous PNG decode.
      for (size_t ii = 0; ii < nSlots; ++ii) {
        const std::string& fp = app.liveWallpaperGalleryPaths[gallOff + ii];
        if (!app.liveWallpaperAdultFlags.count(fp) || !app.liveWallpaperAdultFlags.at(fp)) continue;
        if (app.liveWallpaperBlurredThumbs.find(fp) != app.liveWallpaperBlurredThumbs.end()) continue;
        if (app.liveWallpaperThumbs.find(fp) == app.liveWallpaperThumbs.end()) continue;
        cairo_surface_t* blurred = lw_load_cached_blurred_thumb(fp);
        if (blurred) app.liveWallpaperBlurredThumbs[fp] = blurred;
      }

      const int rowStride = WL.thumbH + WL.gap;
      const double tileRad = static_cast<double>(std::clamp(app.liveWallpaperGalleryThumbRadiusPx, 0, 48));
      double hoveredGx = 0, hoveredGy = 0;
      std::string hoveredPath;

      for (size_t ii = 0; ii < nSlots; ++ii) {
        const size_t gi = gallOff + ii;
        const std::string& fp = app.liveWallpaperGalleryPaths[gi];
        const int row = static_cast<int>(ii / static_cast<size_t>(WL.cols));
        const int col = static_cast<int>(ii % static_cast<size_t>(WL.cols));
        const double gx = gx0d + static_cast<double>(col * (WL.thumb + WL.gap));
        const double gy = static_cast<double>(WL.galleryTop) + static_cast<double>(row * rowStride);

        if (static_cast<int>(ii) == app.liveWallpaperHoveredSlot) {
          hoveredGx = gx;
          hoveredGy = gy;
          hoveredPath = fp;
        }

        cairo_surface_t* surf = nullptr;
        const auto thIt = app.liveWallpaperThumbs.find(fp);
        if (thIt != app.liveWallpaperThumbs.end()) surf = thIt->second;
        else if (!ends_with_video_ext(fp))
          ::eh::wallpaper::WallpaperThumbnailService::instance().request(fp, decodePx);

        const bool hovered = static_cast<int>(ii) == app.liveWallpaperHoveredSlot;
        const bool isAdult = app.liveWallpaperAdultFlags.count(fp) && app.liveWallpaperAdultFlags.at(fp);

        cairo_surface_t* renderSurf = surf;
        if (!isAdult && hovered && fp == app.liveWallpaperHoverVideoPath && !app.liveWallpaperHoverVideoFrames.empty()) {
          int fi = app.liveWallpaperHoverVideoFrameIndex;
          if (fi >= 0 && fi < static_cast<int>(app.liveWallpaperHoverVideoFrames.size()))
            renderSurf = app.liveWallpaperHoverVideoFrames[static_cast<size_t>(fi)];
        }

        // Apply blur for adult-marked content
        if (isAdult && renderSurf && cairo_surface_status(renderSurf) == CAIRO_STATUS_SUCCESS) {
          auto bit = app.liveWallpaperBlurredThumbs.find(fp);
          if (bit != app.liveWallpaperBlurredThumbs.end()) {
            renderSurf = bit->second;
          } else {
            // Try disk cache first
            cairo_surface_t* blurred = lw_load_cached_blurred_thumb(fp);
            if (!blurred) {
              blurred = cairo_surface_box_blur(renderSurf, 100);
              lw_save_blurred_thumb_cache(fp, blurred);
            }
            app.liveWallpaperBlurredThumbs[fp] = blurred;
            renderSurf = blurred;
          }
        }

        const double hs = (!isAdult && hovered && !ends_with_video_ext(fp)) ? static_cast<double>(app.liveWallpaperHoverScale) : 1.0;

        cairo_save(cr);
        if (hs > 1.001) {
          cairo_translate(cr, gx + WL.thumb * 0.5, gy + WL.thumbH * 0.5);
          cairo_scale(cr, hs, hs);
          cairo_translate(cr, -gx - WL.thumb * 0.5, -gy - WL.thumbH * 0.5);
        }
        cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
        cairo_clip(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        if (renderSurf && cairo_surface_status(renderSurf) == CAIRO_STATUS_SUCCESS) {
          cairo_surface_flush(renderSurf);
          const int iw = cairo_image_surface_get_width(renderSurf);
          const int ih = cairo_image_surface_get_height(renderSurf);
          if (iw > 0 && ih > 0) {
            const double sc = std::max(static_cast<double>(WL.thumb) / static_cast<double>(iw),
                                       static_cast<double>(WL.thumbH) / static_cast<double>(ih));
            const double dispW = static_cast<double>(iw) * sc;
            const double dispH = static_cast<double>(ih) * sc;
            const double ox = gx + (static_cast<double>(WL.thumb) - dispW) * 0.5;
            const double oy = gy + (static_cast<double>(WL.thumbH) - dispH) * 0.5;
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, renderSurf, 0, 0);
            cairo_paint(cr);
          }
        } else {
          paint_src_bg(app, cr, 0.95);
          cairo_paint(cr);
        }
        cairo_restore(cr);

        // Selection border
        if (fp == app.config.image) {
          cairo_save(cr);
          cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
          cairo_clip(cr);
          paint_src_accent(app, cr, 0.22);
          cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
          cairo_paint(cr);
          cairo_restore(cr);
        }

        cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
        cairo_set_line_width(cr, fp == app.config.image ? 2.3 : 1.0);
        if (fp == app.config.image)
          paint_src_accent(app, cr, 0.72);
        else
          paint_src_glass_hi(app, cr, 0.12);
        cairo_stroke(cr);

        // "In use" badge
        if (fp == app.config.image) {
          m3::Box badgeBox;
          badgeBox.setColor(s_r, s_g, s_b, 0.35f);
          badgeBox.setRadius(6.0f);
          badgeBox.setGeometry(static_cast<float>(gx + 6), static_cast<float>(gy + 6), 54.0f, 18.0f);
          badgeBox.paint(cr);
          lw_show_text(cr, gx + 10, gy + 19, "In use", 9, 400, t_r, t_g, t_b, 1.0f);
        }

        // "18+" badge for adult-marked content
        if (isAdult) {
          m3::Box adultBox;
          adultBox.setColor(0.85f, 0.15f, 0.15f, 0.70f);
          adultBox.setRadius(6.0f);
          adultBox.setGeometry(static_cast<float>(gx + 6), static_cast<float>(gy + 6), 32.0f, 18.0f);
          adultBox.paint(cr);
          lw_show_text(cr, gx + 9, gy + 19, "18+", 9, 700, 1.0f, 1.0f, 1.0f, 0.95f);
        }

        // Hover highlight for images
        if (hovered && !ends_with_video_ext(fp)) {
          cairo_save(cr);
          cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
          cairo_clip(cr);
          paint_src_accent(app, cr, 0.15f);
          cairo_paint(cr);
          cairo_restore(cr);
        }
      }

      // Hover preview popup
      if (!hoveredPath.empty() && app.liveWallpaperHoveredSlot >= 0 && !ends_with_video_ext(hoveredPath)) {
        constexpr int kPopupW = 240;
        constexpr int kPopupH = 160;
        double popX = hoveredGx + WL.thumb + 12;
        double popY = hoveredGy;
        if (popX + kPopupW > app.width) popX = hoveredGx - kPopupW - 12;
        if (popY + kPopupH > app.height) popY = app.height - kPopupH - 8;
        if (popY < 8) popY = 8;

        cairo_save(cr);
        m3::Box popBg;
        popBg.setColor(s_r, s_g, s_b, 0.94f);
        popBg.setRadius(10.0f);
        popBg.setGeometry(popX, popY, kPopupW, kPopupH);
        popBg.paint(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
        cairo_round_rect(cr, popX, popY, kPopupW, kPopupH, 10.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        constexpr int kPrevAreaH = 100;
        cairo_round_rect(cr, popX + 6, popY + 6, kPopupW - 12, kPrevAreaH - 12, 6.0);
        paint_src_bg(app, cr, 0.75);
        cairo_fill(cr);

        auto prevIt = app.liveWallpaperThumbs.find(hoveredPath);
        cairo_surface_t* prevSurf = (prevIt != app.liveWallpaperThumbs.end()) ? prevIt->second : nullptr;
        if (prevSurf && cairo_surface_status(prevSurf) == CAIRO_STATUS_SUCCESS) {
          cairo_surface_flush(prevSurf);
          const int piw = cairo_image_surface_get_width(prevSurf);
          const int pih = cairo_image_surface_get_height(prevSurf);
          if (piw > 0 && pih > 0) {
            const double pvW = kPopupW - 12;
            const double pvH = kPrevAreaH - 12;
            const double psc = std::max(pvW / static_cast<double>(piw), pvH / static_cast<double>(pih));
            const double pdW = static_cast<double>(piw) * psc;
            const double pdH = static_cast<double>(pih) * psc;
            cairo_save(cr);
            cairo_round_rect(cr, popX + 6, popY + 6, pvW, pvH, 6.0);
            cairo_clip(cr);
            cairo_translate(cr, popX + 6 + (pvW - pdW) * 0.5, popY + 6 + (pvH - pdH) * 0.5);
            cairo_scale(cr, psc, psc);
            cairo_set_source_surface(cr, prevSurf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
          }
        }

        std::string pName = wallpaper_file_basename(hoveredPath);
        if (pName.size() > 28) pName = wallpaper_truncate_visual(pName, 28);
        lw_show_text(cr, popX + 10, popY + kPrevAreaH + 22, pName.c_str(), 12, 600, t_r, t_g, t_b, 1.0f);

        struct stat pst{};
        std::string sizeStr;
        if (::stat(hoveredPath.c_str(), &pst) == 0 && pst.st_size > 0) {
          const double kb = static_cast<double>(pst.st_size) / 1024.0;
          char sb[32];
          if (kb > 1024.0) std::snprintf(sb, sizeof(sb), "%.1f MB", kb / 1024.0);
          else std::snprintf(sb, sizeof(sb), "%.0f KB", kb);
          sizeStr = sb;
        } else {
          sizeStr = "? KB";
        }
        lw_show_text(cr, popX + 10, popY + kPrevAreaH + 38, sizeStr.c_str(), 10, 400, t_r, t_g, t_b, 0.7f);
        cairo_restore(cr);
      }

      // Page navigation
      if (WL.showNav) {
        const bool hPrev = point_in_rect(app.pointerX, pyH, WL.prevX, WL.navY, WL.navW, WL.navH);
        const bool hNext = point_in_rect(app.pointerX, pyH, WL.nextX, WL.navY, WL.navW, WL.navH);

        m3::Button prevBtn;
        prevBtn.setMinSize(0, 0);
        prevBtn.setLabel("Prev");
        prevBtn.setGeometry(WL.prevX, WL.navY, WL.navW, WL.navH);
        prevBtn.setStyle(m3::Button::Style::Outlined);
        prevBtn.setSize(m3::Button::Size::XS);
        prevBtn.setAccentColor(a_r, a_g, a_b);
        prevBtn.setOutlineColor(o_r, o_g, o_b);
        prevBtn.setHovered(hPrev);
        prevBtn.paint(cr);

        m3::Button nextBtn;
        nextBtn.setMinSize(0, 0);
        nextBtn.setLabel("Next");
        nextBtn.setGeometry(WL.nextX, WL.navY, WL.navW, WL.navH);
        nextBtn.setStyle(m3::Button::Style::Outlined);
        nextBtn.setSize(m3::Button::Size::XS);
        nextBtn.setAccentColor(a_r, a_g, a_b);
        nextBtn.setOutlineColor(o_r, o_g, o_b);
        nextBtn.setHovered(hNext);
        nextBtn.paint(cr);

        std::ostringstream os;
        const int pages = std::max(1, static_cast<int>((app.liveWallpaperGalleryPaths.size() + static_cast<size_t>(WL.perPage) - 1) / static_cast<size_t>(WL.perPage)));
        os << static_cast<long>(app.liveWallpaperGalleryPage + 1) << " / " << pages;
        const std::string ps = os.str();
        lw_show_text(cr, LW.marginX + contentW / 2.0, WL.navY + static_cast<int>(18.0 * std::clamp(LW.scale, 0.4, 2.5)), ps.c_str(), std::max(8, static_cast<int>(12.0 * LW.scale)), 400, t_r, t_g, t_b, 0.6f);
      }
    }
  }

  // Mode dropdown popup
  if (app.liveWallpaperUiSubTab == 0 && app.liveWallpaperModeDropdownOpen) {
    const int comboW = 110;
    const int comboH = 28;
    const int comboX = LW.ctrlX + LW.ctrlW - 16 - comboW;
    const int comboY = LW.contentStartY + (60 - comboH) / 2;
    const int ddLy = comboY + comboH + 2;
    lw_paint_combo_list_popup(app, cr, comboX, ddLy, comboW, kSettingsDdRowH, 5, kWallpaperModeLabels,
                              app.config.mode, app.liveWallpaperModeDropdownHoverRow, 1.0);
  }

  // Right-click context menu (18+ marking).
  if (app.liveWallpaperContextMenuOpen) {
    constexpr int kCtxW = 180;
    constexpr int kCtxH = 36;
    const int mx = std::min(app.liveWallpaperContextMenuX, app.width - kCtxW - 8);
    const int my = std::min(app.liveWallpaperContextMenuY, app.height - kCtxH - 8);
    const bool isAdult = app.liveWallpaperAdultFlags.count(app.liveWallpaperContextMenuPath) &&
                         app.liveWallpaperAdultFlags.at(app.liveWallpaperContextMenuPath);

    cairo_save(cr);
    // Background
    cairo_round_rect(cr, mx, my, kCtxW, kCtxH, 8.0);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.95);
    cairo_fill(cr);
    // Border
    cairo_round_rect(cr, mx, my, kCtxW, kCtxH, 8.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    // Label
    lw_show_text(cr, mx + 14, my + 24,
                 isAdult ? "Unmark as 18+" : "Mark as 18+",
                 13, 500, t_r, t_g, t_b, 0.92f);
    cairo_restore(cr);
  }

  cairo_restore(cr);
  cairo_surface_flush(app.buf[paintBi].cairo_surface());

  wl_surface_attach(app.surface, app.buf[paintBi].wl(), 0, 0);
  wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
  app.buf[paintBi].mark_busy();
  if (app.liveWallpaperHoverAnim.has_active() || !app.liveWallpaperHoverVideoFrames.empty())
    schedule_frame(app);
  wl_surface_commit(app.surface);
  if (app.wl.display()) wl_display_flush(app.wl.display());
}

} // namespace eh::live_wallpaper
