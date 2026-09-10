#include "wl/core/icd_select.hpp"

#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace eh::gpu {

namespace {

constexpr std::string_view kSoftware[] = {"lvp",   "lavapipe", "llvmpipe", "swiftshader",
                                          "asahi", "dzn",      "virtio",   "gfxstream",
                                          "software"};

void select_vulkan_icds_once() {
  if (std::getenv("VK_LOADER_DRIVERS_SELECT") || std::getenv("VK_DRIVER_FILES") ||
      std::getenv("VK_ICD_FILENAMES"))
    return;

  std::vector<std::string> hw;
  for (const char* dir :
       {"/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"}) {
    if (DIR* d = opendir(dir)) {
      while (dirent* e = readdir(d)) {
        std::string_view n{e->d_name};
        if (!n.ends_with(".json")) continue;
        bool sw = false;
        for (auto kw : kSoftware)
          if (n.find(kw) != std::string_view::npos) {
            sw = true;
            break;
          }
        if (sw) continue;
        std::string name{n};
        if (std::find(hw.begin(), hw.end(), name) == hw.end()) hw.push_back(std::move(name));
      }
      closedir(d);
    }
  }

  // When the proprietary NVIDIA kernel module is loaded, nothing else can
  // enumerate a device — and merely opening the Mesa ICDs costs real memory
  // (Debian/Ubuntu link RADV against libLLVM, which the process then keeps
  // mapped, ~50MB). Restrict to the NVIDIA ICD alone.
  if (::access("/proc/driver/nvidia/version", R_OK) == 0) {
    auto nv = std::find_if(hw.begin(), hw.end(),
                           [](const std::string& n) { return n.find("nvidia") != std::string::npos; });
    if (nv != hw.end()) hw = {*nv};
  }

  if (!hw.empty()) {
    std::string select;
    for (const auto& n : hw) {
      if (!select.empty()) select += ',';
      select += n;
    }
    setenv("VK_LOADER_DRIVERS_SELECT", select.c_str(), 0);
  }
}

}  // namespace

void select_vulkan_icds() {
  static std::once_flag once;
  std::call_once(once, select_vulkan_icds_once);
}

}  // namespace eh::gpu

// Apply before any code in the process can trigger the loader's driver scan.
__attribute__((constructor)) static void icd_select_early_init() {
  eh::gpu::select_vulkan_icds();
}
