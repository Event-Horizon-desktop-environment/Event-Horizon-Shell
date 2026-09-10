#include "wl/core/gpu_page_trim.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>

#include <sys/mman.h>
#include <link.h>

namespace eh::gpu {
namespace {

uint64_t now_ms_steady() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::atomic<uint64_t> g_last_activity_ms{now_ms_steady()};
std::atomic<uint64_t> g_last_trim_attempt_ms{0};
std::atomic<uint64_t> g_rate_window_start_ms{now_ms_steady()};
std::atomic<uint32_t> g_sig_frames{0};

bool name_is_nvidia_userspace(const char* name) {
  if (!name || !*name) return false;
  // libnvidia-gpucomp / -glcore / -glvkspirv / -glsi / -tls / -allocator,
  // plus the GLVND vendor libs libGLX_nvidia / libEGL_nvidia.
  return ::strstr(name, "libnvidia-") == name ||
         ::strstr(name, "nvidia/current/lib") != nullptr ||
         ::strstr(name, "libGLX_nvidia") == name ||
         ::strstr(name, "libEGL_nvidia") == name;
}

struct TrimCtx {
  uint64_t dropped = 0;
  uint32_t libs_matched = 0;
  uint32_t ranges_advised = 0;
  int first_madvise_errno = 0;
};

int phdr_callback(struct dl_phdr_info* info, size_t, void* data) {
  auto* ctx = static_cast<TrimCtx*>(data);
  const char* base = info->dlpi_name;
  if (!base) return 0;
  // Keep only the basename for prefix checks.
  const char* slash = std::strrchr(base, '/');
  const char* leaf = slash ? slash + 1 : base;
  if (!name_is_nvidia_userspace(leaf)) return 0;
  ++ctx->libs_matched;

  for (int j = 0; j < info->dlpi_phnum; ++j) {
    const ElfW(Phdr)& ph = info->dlpi_phdr[j];
    if (ph.p_type != PT_LOAD) continue;
    if (ph.p_flags & PF_W) continue;          // never touch writable segments
    if (ph.p_memsz == 0) continue;

    const uintptr_t start =
        (static_cast<uintptr_t>(info->dlpi_addr) + ph.p_vaddr) & ~uintptr_t(0xFFF);
    const uintptr_t end =
        ((static_cast<uintptr_t>(info->dlpi_addr) + ph.p_vaddr + ph.p_memsz) +
         0xFFF) & ~uintptr_t(0xFFF);
    if (end <= start) continue;
    if (::madvise(reinterpret_cast<void*>(start), end - start, MADV_DONTNEED) == 0) {
      ctx->dropped += end - start;
      ++ctx->ranges_advised;
    } else if (ctx->first_madvise_errno == 0) {
      ctx->first_madvise_errno = errno;
    }
  }
  return 0;
}

} // namespace

void touch_frame_activity() { g_last_activity_ms.store(now_ms_steady(), std::memory_order_relaxed); }

void touch_frame_activity_if_significant(uint32_t total_px, uint32_t damaged_px) {
  if (total_px == 0) return;
  // Full damage or >2% of the buffer counts as real activity.
  const uint64_t limit = (static_cast<uint64_t>(total_px) * 2ULL) / 100ULL;
  if (damaged_px == 0 || damaged_px >= limit) {
    touch_frame_activity();
    g_sig_frames.fetch_add(1, std::memory_order_relaxed);
  }
}

uint64_t trim_idle_gpu_pages(uint64_t idle_ms) {
  const uint64_t now = now_ms_steady();

  // Cheap short-circuit: this function is called every loop tick, so bail
  // out on a few atomics unless an attempt is even possible.
  if (now - g_last_trim_attempt_ms.load(std::memory_order_relaxed) < 3000) return 0;

  // Qualify as idle via any of:
  //  1. No significant frame for `idle_ms` (full quiet), or
  //  2. No significant frame for ~2.5s (micro-quiet: a burst just ended —
  //     the next burst re-faults a few MB in single-digit milliseconds, so
  //     dropping immediately beats pinning ~150MB through every pause), or
  //  3. Sparse background traffic: rate under 240 significant frames/min
  //     (~4 Hz; interactive bursts run an order of magnitude hotter).
  const auto last_activity = g_last_activity_ms.load(std::memory_order_relaxed);
  const bool quiet = now - last_activity >= idle_ms;
  const bool micro_quiet = now - last_activity >= 2500;

  constexpr uint32_t kMaxSparseFpm = 240;
  constexpr uint64_t kMinRateWindowMs = 5000;
  const uint64_t window_start = g_rate_window_start_ms.load(std::memory_order_relaxed);
  const uint64_t window_ms = now - window_start;
  uint64_t fpm = UINT64_MAX;
  if (window_ms >= kMinRateWindowMs) {
    const uint32_t frames = g_sig_frames.exchange(0, std::memory_order_relaxed);
    g_rate_window_start_ms.store(now, std::memory_order_relaxed);
    fpm = frames * 60000ULL / window_ms;
  }
  if (!quiet && !micro_quiet && fpm > kMaxSparseFpm) return 0;

  g_last_trim_attempt_ms.store(now, std::memory_order_relaxed);

  TrimCtx ctx{};
  ::dl_iterate_phdr(phdr_callback, &ctx);
  static const bool trace = ::getenv("EH_GPU_TRACE") != nullptr;
  if (trace) {
    std::fprintf(stderr,
                 "[gpu-trim] attempt(%s): libs=%u ranges=%u dropped=%lluKB errno0=%d\n",
                 quiet ? "quiet" : micro_quiet ? "micro-quiet" : "sparse",
                 ctx.libs_matched, ctx.ranges_advised,
                 static_cast<unsigned long long>(ctx.dropped / 1024), ctx.first_madvise_errno);
  }
  return ctx.dropped;
}

uint64_t force_trim_gpu_pages() {
  g_last_trim_attempt_ms.store(now_ms_steady(), std::memory_order_relaxed);
  TrimCtx ctx{};
  ::dl_iterate_phdr(phdr_callback, &ctx);
  static const bool trace = ::getenv("EH_GPU_TRACE") != nullptr;
  if (trace) {
    std::fprintf(stderr, "[gpu-trim] forced: libs=%u ranges=%u dropped=%lluKB errno0=%d\n",
                 ctx.libs_matched, ctx.ranges_advised,
                 static_cast<unsigned long long>(ctx.dropped / 1024), ctx.first_madvise_errno);
  }
  return ctx.dropped;
}

} // namespace eh::gpu
