#pragma once

namespace eh::gpu {

// Restrict Vulkan enumeration to hardware ICDs when they exist, skipping
// software renderers (lavapipe/llvmpipe) and, when the proprietary NVIDIA
// module owns the GPU stack, everything else as well.
//
// The loader dlopens every manifest it scans, which otherwise maps tens of MB
// of dead weight (libLLVM via Mesa's RADV build, lavapipe itself) even though
// a real GPU ends up selected. Idempotent; respects pre-existing user env
// (VK_DRIVER_FILES / VK_ICD_FILENAMES / VK_LOADER_DRIVERS_SELECT).
//
// Runs automatically before main() via a library constructor, so it applies
// to every process linking the shell core — dock, taskbar, supervisor,
// settings — no matter which component touches Vulkan first.
void select_vulkan_icds();

}  // namespace eh::gpu
