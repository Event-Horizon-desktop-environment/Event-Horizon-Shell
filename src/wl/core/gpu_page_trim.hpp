#pragma once

#include <cstdint>

namespace eh::gpu {

// Shell components should call this from every Wayland frame callback (and
// any animation tick) so trim_idle_gpu_pages() can detect idleness.
void touch_frame_activity();

// Same, but ignores insignificant repaints (second-clocks, progress pulses
// repainting <2% of the buffer) so sparse cosmetic ticks don't pin ~90MB of
// NVIDIA userspace text pages forever.
void touch_frame_activity_if_significant(uint32_t total_px, uint32_t damaged_px);

// Drops read-only file-backed pages of loaded NVIDIA userspace libraries
// (gpucomp/glcore/glvkspirv/GLX/EGL) via MADV_DONTNEED once the shell has
// been idle for `idle_ms`. These are clean text/rodata pages: the kernel
// simply unpins them from the process; they re-fault transparently from the
// page cache on the next activity burst. Returns bytes unmapped (best-effort
// estimate of RSS reduction).
[[nodiscard]] uint64_t trim_idle_gpu_pages(uint64_t idle_ms);

// Unconditional variant used once at startup settle: drops everything the
// initialization burst left resident without waiting for idle detection.
[[nodiscard]] uint64_t force_trim_gpu_pages();

} // namespace eh::gpu
