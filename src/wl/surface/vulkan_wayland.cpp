#include "wl/surface/vulkan_wayland.hpp"
#include "wl/core/icd_select.hpp"
#include "wl/surface/vulkan_destruction_queue.hpp"

#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#ifndef NDEBUG
static std::atomic<int> g_debug_present_count{0};
static std::atomic<int64_t> g_debug_present_bytes{0};
#endif

namespace {

const char* vk_result_string(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return "UNKNOWN_VK_RESULT";
    }
}

bool check_instance_extension(const char* ext_name) {
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    if (ext_count == 0) return false;

    std::vector<VkExtensionProperties> extensions(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, ext_name) == 0) {
            return true;
        }
    }
    return false;
}

bool check_device_extension(VkPhysicalDevice device, const char* ext_name) {
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr);
    if (ext_count == 0) return false;

    std::vector<VkExtensionProperties> extensions(ext_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &ext_count, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, ext_name) == 0) {
            return true;
        }
    }
    return false;
}

constexpr uint32_t k_u32_invalid = 0xffffffffu;

uint32_t find_mem_type(const VkPhysicalDeviceMemoryProperties& mp, uint32_t type_bits, VkMemoryPropertyFlags props) {
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  return k_u32_invalid;
}

bool pick_surface_format(VkPhysicalDevice pdev, VkSurfaceKHR surf, VkFormat* out_fmt, VkColorSpaceKHR* out_cs) {
  uint32_t nf = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &nf, nullptr);
  if (nf == 0) return false;
  std::vector<VkSurfaceFormatKHR> fmts(nf);
  vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &nf, fmts.data());
  VkFormat want[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM};
  for (VkFormat w : want) {
    for (const auto& f : fmts) {
      if (f.format == w && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        *out_fmt = f.format;
        *out_cs = f.colorSpace;
        return true;
      }
    }
  }
  for (VkFormat w : want) {
    for (const auto& f : fmts) {
      if (f.format == w) {
        *out_fmt = f.format;
        *out_cs = f.colorSpace;
        return true;
      }
    }
  }
  *out_fmt = fmts[0].format;
  *out_cs = fmts[0].colorSpace;
  return true;
}

VkPresentModeKHR pick_present_mode(VkPhysicalDevice pdev, VkSurfaceKHR surf) {
  uint32_t nm = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surf, &nm, nullptr);
  std::vector<VkPresentModeKHR> modes(nm);
  if (nm) vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surf, &nm, modes.data());
  if (eh::debug_profile::env_bool("EH_VK_MAILBOX")) {
    for (VkPresentModeKHR m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
  }
  for (VkPresentModeKHR m : modes) {
    if (m == VK_PRESENT_MODE_FIFO_KHR) return m;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

static bool query_drm_format_modifiers(VkPhysicalDevice pdev, VkFormat format,
                                        VkInstance instance,
                                        std::vector<uint64_t>& out_modifiers) {
  auto func = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2KHR>(
      vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties2KHR"));
  if (!func) {
    func = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2KHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties2"));
  }
  if (!func) return false;

  VkFormatProperties2KHR fmt_props{};
  fmt_props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR;

  VkDrmFormatModifierPropertiesListEXT mod_list{};
  mod_list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
  fmt_props.pNext = &mod_list;

  func(pdev, format, &fmt_props);

  if (mod_list.drmFormatModifierCount == 0) return false;

  std::vector<VkDrmFormatModifierPropertiesEXT> mod_props(mod_list.drmFormatModifierCount);
  mod_list.pDrmFormatModifierProperties = mod_props.data();

  func(pdev, format, &fmt_props);

  for (const auto& m : mod_props) {
    if (m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT) {
      out_modifiers.push_back(m.drmFormatModifier);
    }
  }

  return !out_modifiers.empty();
}

} // namespace

// Vulkan debug messenger (validation layers).
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/) {
  const char* sev = "INFO";
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) sev = "ERROR";
  else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) sev = "WARN";
  else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) sev = "VERB";
  std::fprintf(stderr, "[VK-%s] %s\n", sev, data ? data->pMessage : "(null)");
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    MANGOWM_ERROR("Vulkan validation: %s", data ? data->pMessage : "(null)");
  return VK_FALSE;
}

static VkDebugUtilsMessengerEXT g_debug_messenger = VK_NULL_HANDLE;

static void destroy_debug_messenger(VkInstance inst) {
  if (g_debug_messenger != VK_NULL_HANDLE && inst != VK_NULL_HANDLE) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) func(inst, g_debug_messenger, nullptr);
    g_debug_messenger = VK_NULL_HANDLE;
  }
}

namespace eh::wayland {
struct VulkanDisplayContextImpl {
  wl_display* wayland_dpy = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice pdev = VK_NULL_HANDLE;
  VkDevice dev = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  std::vector<uint32_t> present_qfams;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mp{};
  bool have_drm_mod_ext = false;
};

struct VulkanLayerSurfaceImpl {
  VulkanDisplayContext* ctx_reg = nullptr;
  wl_display* wayland_dpy = nullptr;
  wl_surface* wayland_surf = nullptr;
  VkSurfaceKHR surf = VK_NULL_HANDLE;
  VkSwapchainKHR swap = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkExtent2D extent{};
  struct Frame {
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore sem_img = VK_NULL_HANDLE;
    VkSemaphore sem_done = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkDeviceSize staging_cap = 0;
    void* staging_map = nullptr;
    bool staging_coherent = false;
  };
  Frame frames[kMaxFramesInFlight];
  int frame_idx = 0;
  std::vector<VkImage> imgs;
  std::vector<DamageRegion> img_damage;
  int last_w = 0;
  int last_h = 0;
  std::vector<uint64_t> drm_modifiers;
};

VulkanDisplayContext::VulkanDisplayContext() {
  MANGOWM_DEBUG("VulkanDisplayContext ctor this=%p", (void*)this);
}
VulkanDisplayContext::~VulkanDisplayContext() {
  MANGOWM_DEBUG("VulkanDisplayContext dtor this=%p", (void*)this);
  shutdown();
}

bool VulkanDisplayContext::init(wl_display* display) {

  debug_log("vulkan", "VulkanDisplayContext::init: display=%p", (void*)display);
  MANGOWM_INFO("VulkanDisplayContext init display=%p", (void*)display);
  const bool vk_debug = eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG");

  if (!impl_) impl_ = std::make_unique<VulkanDisplayContextImpl>();

  // Hardware-only ICD selection (shared; see wl/core/icd_select.hpp).
  eh::gpu::select_vulkan_icds();

  std::vector<const char*> inst_ext{VK_KHR_SURFACE_EXTENSION_NAME,
                                    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
  if (check_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
    inst_ext.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  }
  std::vector<const char*> inst_layers;

  if (check_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
    inst_ext.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (vk_debug) std::cerr << "[dock-vk] " << VK_EXT_DEBUG_UTILS_EXTENSION_NAME << ": OK\n";
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count > 0 ? layer_count : 0);
    if (layer_count > 0) (void)vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    for (const auto& lp : layers) {
      if (vk_debug && (std::strstr(lp.layerName, "KHRONOS_validation") != nullptr ||
                       std::strstr(lp.layerName, "validation") != nullptr)) {
        inst_layers.push_back(lp.layerName);
        break;
      }
    }
  }

  uint32_t ver = 0;
  vkEnumerateInstanceVersion(&ver);
  if (vk_debug) std::cerr << "[dock-vk] Vulkan instance version: " << VK_API_VERSION_MAJOR(ver)
                          << "."
                          << VK_API_VERSION_MINOR(ver) << "." << VK_API_VERSION_PATCH(ver) << "\n";

  VkApplicationInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = "EventHorizon";
  ai.applicationVersion = 1;
  ai.pEngineName = "eh";
  ai.engineVersion = 1;
  ai.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = static_cast<uint32_t>(inst_ext.size());
  ici.ppEnabledExtensionNames = inst_ext.data();
  if (!inst_layers.empty()) {
    ici.enabledLayerCount = static_cast<uint32_t>(inst_layers.size());
    ici.ppEnabledLayerNames = inst_layers.data();
  }

  VkInstance inst = VK_NULL_HANDLE;
  VkResult vr = vkCreateInstance(&ici, nullptr, &inst);
  if (vr != VK_SUCCESS) {
    debug_log("vulkan", "vkCreateInstance failed: %s", vk_result_string(vr));
    std::cerr << "[dock-vk] vkCreateInstance failed: " << vk_result_string(vr) << "\n";
    shutdown();
    return false;
  }
  impl_->instance = inst;
  debug_log("vulkan", "Vulkan instance created successfully");

  if (!inst_layers.empty()) {
    VkDebugUtilsMessengerCreateInfoEXT dmci{};
    dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dmci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    dmci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dmci.pfnUserCallback = debug_messenger_callback;
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT"));
    if (func) func(inst, &dmci, nullptr, &g_debug_messenger);
    MANGOWM_INFO("Vulkan validation layers enabled (messenger=%p)", (void*)(void*)g_debug_messenger);
  }

  uint32_t np = 0;
  vr = vkEnumeratePhysicalDevices(inst, &np, nullptr);
  if (vr != VK_SUCCESS) {
    debug_log("vulkan", "vkEnumeratePhysicalDevices failed: %s", vk_result_string(vr));
    std::cerr << "[dock-vk] vkEnumeratePhysicalDevices failed: " << vk_result_string(vr) << "\n";
    shutdown();
    return false;
  }

  if (vk_debug) std::cerr << "[dock-vk] found " << np << " physical device(s)\n";

  if (np == 0) {
    debug_log("vulkan", "no physical devices found");
    std::cerr << "[dock-vk] no physical devices\n";
    shutdown();
    return false;
  }

  std::vector<VkPhysicalDevice> pdevs(np);
  vr = vkEnumeratePhysicalDevices(inst, &np, pdevs.data());
  if (vr != VK_SUCCESS) {
    debug_log("vulkan", "vkEnumeratePhysicalDevices (2nd) failed: %s", vk_result_string(vr));
    std::cerr << "[dock-vk] vkEnumeratePhysicalDevices (2nd call) failed: " << vk_result_string(vr) << "\n";
    shutdown();
    return false;
  }

  VkPhysicalDevice chosen = VK_NULL_HANDLE;
  uint32_t chosen_q = k_u32_invalid;
  std::vector<uint32_t> present_qfams;

  if (vk_debug) std::cerr << "[dock-vk] scanning devices for Wayland presentation support:\n";
  for (VkPhysicalDevice p : pdevs) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(p, &props);

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(p, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qfp(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(p, &nq, qfp.data());

    if (vk_debug) std::cerr << "  Device \"" << props.deviceName << "\" (API "
              << VK_API_VERSION_MAJOR(props.apiVersion) << "."
              << VK_API_VERSION_MINOR(props.apiVersion) << "):\n";

    std::vector<uint32_t> pres_qs;
    uint32_t gfx_q = k_u32_invalid;

    for (uint32_t qi = 0; qi < nq; ++qi) {
      bool graphics = (qfp[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
      bool present = vkGetPhysicalDeviceWaylandPresentationSupportKHR(p, qi, display);
      if (graphics || present) {
        if (vk_debug) std::cerr << "    Queue family " << qi
                  << ": graphics=" << (graphics ? "yes" : "no")
                  << ", present(Wayland)=" << (present ? "yes" : "no") << "\n";
      }
      if (graphics) gfx_q = qi;
      if (present) pres_qs.push_back(qi);
    }

    if (gfx_q != k_u32_invalid && !pres_qs.empty()) {
      chosen = p;
      chosen_q = gfx_q;
      present_qfams = std::move(pres_qs);
      break;
    }
  }

  if (chosen == VK_NULL_HANDLE) {
    debug_log("vulkan", "no Wayland-present-capable queue family found");
    std::cerr << "[dock-vk] no Wayland-present-capable queue family found\n";
    shutdown();
    return false;
  }

  VkPhysicalDeviceProperties chosen_props{};
  vkGetPhysicalDeviceProperties(chosen, &chosen_props);
  if (vk_debug) std::cerr << "[dock-vk] selected device: \"" << chosen_props.deviceName
            << "\" graphics qfam=" << chosen_q
            << " present qfams=";
  for (auto q : present_qfams) if (vk_debug) std::cerr << q << " ";
  if (vk_debug) std::cerr << "\n";

  impl_->pdev = chosen;
  vkGetPhysicalDeviceMemoryProperties(chosen, &impl_->mp);
  impl_->qfam = chosen_q;

  if (vk_debug) std::cerr << "[dock-vk] checking device extensions:\n";
  if (!check_device_extension(chosen, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
    debug_log("vulkan", "device does not support VK_KHR_swapchain");
    std::cerr << "  " << VK_KHR_SWAPCHAIN_EXTENSION_NAME << ": MISSING\n";
    std::cerr << "[dock-vk] device does not support VK_KHR_swapchain extension\n";
    shutdown();
    return false;
  }
  if (vk_debug) std::cerr << "  " << VK_KHR_SWAPCHAIN_EXTENSION_NAME << ": OK\n";

  if (check_device_extension(chosen, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME)) {
    impl_->have_drm_mod_ext = true;
    if (vk_debug) std::cerr << "  " << VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME << ": OK\n";
  } else {
    if (vk_debug) std::cerr << "  " << VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME << ": not available\n";
  }

  std::vector<uint32_t> unique_qfams = {chosen_q};
  for (auto q : present_qfams) {
    if (std::find(unique_qfams.begin(), unique_qfams.end(), q) == unique_qfams.end()) {
      unique_qfams.push_back(q);
    }
  }

  float qp = 1.f;
  std::vector<VkDeviceQueueCreateInfo> dqcis;
  for (auto qfam : unique_qfams) {
    VkDeviceQueueCreateInfo dqci{};
    dqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqci.queueFamilyIndex = qfam;
    dqci.queueCount = 1;
    dqci.pQueuePriorities = &qp;
    dqcis.push_back(dqci);
  }

  impl_->present_qfams = present_qfams;

  std::vector<const char*> dev_ext = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  if (impl_->have_drm_mod_ext) dev_ext.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = static_cast<uint32_t>(dqcis.size());
  dci.pQueueCreateInfos = dqcis.data();
  dci.enabledExtensionCount = static_cast<uint32_t>(dev_ext.size());
  dci.ppEnabledExtensionNames = dev_ext.data();

  VkDevice dev = VK_NULL_HANDLE;
  vr = vkCreateDevice(chosen, &dci, nullptr, &dev);
  if (vr != VK_SUCCESS) {
    debug_log("vulkan", "vkCreateDevice failed: %s", vk_result_string(vr));
    std::cerr << "[dock-vk] vkCreateDevice failed: " << vk_result_string(vr) << "\n";
    shutdown();
    return false;
  }
  impl_->dev = dev;
  vkGetDeviceQueue(dev, chosen_q, 0, &impl_->queue);

  VkCommandPoolCreateInfo cpci{};
  cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpci.queueFamilyIndex = chosen_q;
  if (vkCreateCommandPool(dev, &cpci, nullptr, &impl_->cmd_pool) != VK_SUCCESS) {
    debug_log("vulkan", "vkCreateCommandPool failed");
    std::cerr << "[dock-vk] vkCreateCommandPool failed\n";
    shutdown();
    return false;
  }

  if (eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG")) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(impl_->pdev, &props);
    std::cerr << "[dock-vk] EH_DOCK_VK_DEBUG device=\"" << props.deviceName << "\" apiVersion=0x" << std::hex
              << props.apiVersion << std::dec << " driverVersion=0x" << std::hex << props.driverVersion << std::dec
              << "\n[dock-vk] (set EH_DOCK_VK_DEBUG=0 to silence)\n";
  }

  debug_log("vulkan", "VulkanDisplayContext::init OK device=%p", (void*)impl_->pdev);
  return true;
}

void VulkanDisplayContext::shutdown() {
    
  debug_log("vulkan", "VulkanDisplayContext::shutdown this=%p", (void*)this);
  MANGOWM_DEBUG("VulkanDisplayContext shutdown this=%p had_impl=%d", (void*)this, impl_ ? 1 : 0);
  if (!impl_) return;
  eh::vk::VulkanDestructionQueue::instance().drain();
  if (impl_->cmd_pool != VK_NULL_HANDLE && impl_->dev != VK_NULL_HANDLE) {
    vkDestroyCommandPool(impl_->dev, impl_->cmd_pool, nullptr);
    impl_->cmd_pool = VK_NULL_HANDLE;
  }
  if (impl_->dev != VK_NULL_HANDLE) {
    vkDestroyDevice(impl_->dev, nullptr);
    impl_->dev = VK_NULL_HANDLE;
    impl_->queue = VK_NULL_HANDLE;
  }
  if (impl_->instance != VK_NULL_HANDLE) {
    destroy_debug_messenger(impl_->instance);
    vkDestroyInstance(impl_->instance, nullptr);
    impl_->instance = VK_NULL_HANDLE;
  }
  impl_->pdev = VK_NULL_HANDLE;
  impl_->wayland_dpy = nullptr;
  impl_.reset();
  debug_log("vulkan", "VulkanDisplayContext::shutdown done");
}

bool VulkanDisplayContext::valid() const noexcept { return impl_ && impl_->dev != VK_NULL_HANDLE; }
wl_display* VulkanDisplayContext::wayland_display() const noexcept { return impl_ ? impl_->wayland_dpy : nullptr; }

VulkanLayerSurface::VulkanLayerSurface() {
  debug_log("vulkan", "VulkanLayerSurface ctor this=%p", (void*)this);
}
VulkanLayerSurface::~VulkanLayerSurface() {
  debug_log("vulkan", "VulkanLayerSurface dtor this=%p", (void*)this);
  destroy();
}

void VulkanLayerSurface::destroy() {
    
  debug_log("vulkan", "VulkanLayerSurface::destroy this=%p impl=%p", (void*)this, (void*)impl_.get());
  MANGOWM_DEBUG("this=%p impl=%p surf=%p swap=%p",
    (void*)this, (void*)impl_.get(), (void*)(impl_ ? impl_->surf : VK_NULL_HANDLE),
    (void*)(impl_ ? impl_->swap : VK_NULL_HANDLE));
  if (!impl_) {
    MANGOWM_DEBUG("no impl_, returning");
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  MANGOWM_DEBUG("ctx_reg=%p", (void*)(impl_ ? impl_->ctx_reg : nullptr));
  VulkanDisplayContextImpl* d = impl_->ctx_reg ? impl_->ctx_reg->impl() : nullptr;
  MANGOWM_DEBUG("d=%p", (void*)d);
  if (!d) {
    MANGOWM_DEBUG("no device context, resetting impl_");
    impl_.reset();
    return;
  }
  MANGOWM_DEBUG("dev=%p", (void*)(void*)d->dev);
  VkDevice dev = d->dev;
  if (dev == VK_NULL_HANDLE) {
    MANGOWM_DEBUG("dev is null, resetting impl_");
    impl_.reset();
    return;
  }

  for (int fi = 0; fi < kMaxFramesInFlight; ++fi) {
    auto& f = impl_->frames[fi];
    if (f.fence != VK_NULL_HANDLE) {
      (void)vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
      vkDestroyFence(dev, f.fence, nullptr);
      f.fence = VK_NULL_HANDLE;
    }
    if (f.sem_img != VK_NULL_HANDLE) {
      vkDestroySemaphore(dev, f.sem_img, nullptr);
      f.sem_img = VK_NULL_HANDLE;
    }
    if (f.sem_done != VK_NULL_HANDLE) {
      vkDestroySemaphore(dev, f.sem_done, nullptr);
      f.sem_done = VK_NULL_HANDLE;
    }
    if (f.cmd != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(dev, d->cmd_pool, 1, &f.cmd);
      f.cmd = VK_NULL_HANDLE;
    }
    if (f.staging != VK_NULL_HANDLE) {
      if (f.staging_map) vkUnmapMemory(dev, f.staging_mem);
      vkDestroyBuffer(dev, f.staging, nullptr);
      f.staging = VK_NULL_HANDLE;
    }
    if (f.staging_mem != VK_NULL_HANDLE) {
      vkFreeMemory(dev, f.staging_mem, nullptr);
      f.staging_mem = VK_NULL_HANDLE;
    }
    f.staging_map = nullptr;
    f.staging_cap = 0;
  }
  if (impl_->swap != VK_NULL_HANDLE) {
    MANGOWM_DEBUG("destroying swapchain %p", (void*)(void*)impl_->swap);
    vkDestroySwapchainKHR(dev, impl_->swap, nullptr);
    impl_->swap = VK_NULL_HANDLE;
    if (impl_->wayland_dpy) {
      MANGOWM_DEBUG("wl_display_roundtrip");
      wl_display_roundtrip(impl_->wayland_dpy);
    }
  }
  if (impl_->surf != VK_NULL_HANDLE) {
    MANGOWM_DEBUG("destroying surface %p (instance=%p)", (void*)(void*)impl_->surf, (void*)(void*)d->instance);
    if (d->instance != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(d->instance, impl_->surf, nullptr);
    impl_->surf = VK_NULL_HANDLE;
  }
  impl_->imgs.clear();
  impl_->ctx_reg = nullptr;
  impl_->wayland_dpy = nullptr;
  impl_->wayland_surf = nullptr;
  MANGOWM_DEBUG("resetting impl_");
  impl_.reset();
  MANGOWM_DEBUG("exit had_swap=%d had_surf=%d", had_swap, had_surf);
  debug_log("vulkan", "VulkanLayerSurface::destroy done");
}

void VulkanLayerSurface::detach() noexcept {
  debug_log("vulkan", "VulkanLayerSurface::detach this=%p", (void*)this);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (impl_) {
    impl_->ctx_reg = nullptr;
  }
}

bool VulkanLayerSurface::valid() const noexcept {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  bool ok = impl_ && impl_->swap != VK_NULL_HANDLE;
  const bool vk_debug = eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG");
  if (vk_debug) std::cerr << "[vk-valid] impl_=" << static_cast<void*>(impl_.get())
              << " swap=" << static_cast<void*>(impl_ ? impl_->swap : VK_NULL_HANDLE)
              << " surf=" << static_cast<void*>(impl_ ? impl_->surf : VK_NULL_HANDLE)
              << " ctx_reg=" << static_cast<void*>(impl_ ? impl_->ctx_reg : nullptr)
              << " result=" << (ok ? "true" : "false") << "\n";
  return ok;
}

bool VulkanLayerSurface::create(VulkanDisplayContext& ctx, wl_display* display, wl_surface* surface, int width, int height) {
    
  debug_log("vulkan", "VulkanLayerSurface::create this=%p ctx=%p display=%p surface=%p %dx%d",
    (void*)this, (void*)&ctx, (void*)display, (void*)surface, width, height);
  MANGOWM_INFO("VulkanLayerSurface::create this=%p ctx=%p display=%p surface=%p %dx%d",
    (void*)this, (void*)&ctx, (void*)display, (void*)surface, width, height);

  VkSurfaceKHR saved_surf = VK_NULL_HANDLE;
  VkSwapchainKHR saved_swap = VK_NULL_HANDLE;
  struct wl_surface* saved_wl_surf = nullptr;
  VkInstance saved_instance = VK_NULL_HANDLE;
  VkDevice saved_device = VK_NULL_HANDLE;
  if (impl_) {
    saved_wl_surf = impl_->wayland_surf;
    if (impl_->surf != VK_NULL_HANDLE) {
      saved_surf = impl_->surf;
      impl_->surf = VK_NULL_HANDLE;
    }
    if (impl_->swap != VK_NULL_HANDLE) {
      saved_swap = impl_->swap;
      impl_->swap = VK_NULL_HANDLE;
    }
    if (impl_->ctx_reg && impl_->ctx_reg->impl()) {
      saved_instance = impl_->ctx_reg->impl()->instance;
      saved_device = impl_->ctx_reg->impl()->dev;
    }
  }

  const bool vk_debug = eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG");

  destroy();
  if (vk_debug) std::cerr << "[vk-create] after destroy, checking args: ctx.valid=" << ctx.valid()
            << " display=" << static_cast<void*>(display) << " surface=" << static_cast<void*>(surface)
            << " " << width << "x" << height << "\n";
  if (!ctx.valid() || !display || !surface || width <=0 || height <=0) {
    debug_log("vulkan", "create: invalid args (ctx.valid=%d display=%p surface=%p %dx%d)",
      ctx.valid(), (void*)display, (void*)surface, width, height);
    std::cerr << "[dock-vk] create: invalid args (ctx.valid=" << ctx.valid()
              << " display=" << static_cast<void*>(display) << " surface=" << static_cast<void*>(surface)
              << " " << width << "x" << height << ")\n";
    return false;
  }

  wl_display_flush(display);

  eh::vk::VulkanDestructionQueue::instance().drain();
  if (vk_debug) std::cerr << "[vk-create] after drain, allocating new impl_\n";
  impl_ = std::make_unique<VulkanLayerSurfaceImpl>();
  impl_->ctx_reg = &ctx;
  impl_->wayland_dpy = display;
  impl_->wayland_surf = surface;
  if (vk_debug) std::cerr << "[dock-vk] create: surface=" << static_cast<void*>(surface) << " display=" << static_cast<void*>(display)
            << " call#" << (++create_call_count_) << "\n";

  VulkanDisplayContextImpl* d = ctx.impl();

  if (saved_surf != VK_NULL_HANDLE && saved_wl_surf == surface && saved_instance == d->instance) {
    if (vk_debug) std::cerr << "[dock-vk] reusing old VkSurfaceKHR " << static_cast<void*>(saved_surf)
              << " (wl_surf " << static_cast<void*>(surface) << " unchanged)\n";
    impl_->surf = saved_surf;

    if (saved_swap != VK_NULL_HANDLE && saved_device == d->dev) {
      if (vk_debug) std::cerr << "[dock-vk] reusing old swapchain " << static_cast<void*>(saved_swap) << "\n";
      impl_->swap = saved_swap;
    }
  } else {
    if (vk_debug) std::cerr << "[dock-vk] creating fresh VkSurfaceKHR ("
              << "saved_surf=" << static_cast<void*>(saved_surf)
              << " saved_wl=" << static_cast<void*>(saved_wl_surf)
              << " new_wl=" << static_cast<void*>(surface)
              << " same_instance=" << (saved_instance == d->instance ? "yes" : "no")
              << ")\n";

    if (saved_surf != VK_NULL_HANDLE && saved_instance != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(saved_instance, saved_surf, nullptr);
    if (saved_swap != VK_NULL_HANDLE && saved_device != VK_NULL_HANDLE)
      vkDestroySwapchainKHR(saved_device, saved_swap, nullptr);
  }
  if (impl_->surf == VK_NULL_HANDLE) {
    VkWaylandSurfaceCreateInfoKHR wsci{};
    wsci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    wsci.display = display;
    wsci.surface = surface;
    VkResult vr_surf = vkCreateWaylandSurfaceKHR(d->instance, &wsci, nullptr, &impl_->surf);
    if (vr_surf != VK_SUCCESS) {
      debug_log("vulkan", "vkCreateWaylandSurfaceKHR failed: %s", vk_result_string(vr_surf));
      std::cerr << "[dock-vk] vkCreateWaylandSurfaceKHR failed: " << vk_result_string(vr_surf) << "\n";
      destroy();
      return false;
    }
  }

  VkBool32 sup = VK_FALSE;
  uint32_t nq = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(d->pdev, &nq, nullptr);

  bool found = false;
  for (uint32_t qi = 0; qi < nq; ++qi) {
    VkResult vr_sup = vkGetPhysicalDeviceSurfaceSupportKHR(d->pdev, qi, impl_->surf, &sup);
    if (vk_debug) std::cerr << "[dock-vk] vkGetPhysicalDeviceSurfaceSupportKHR(qfam=" << qi
              << ") result=" << vk_result_string(vr_sup)
              << " supported=" << (sup == VK_TRUE) << "\n";
    if (vr_sup == VK_SUCCESS && sup == VK_TRUE) {
      if (qi != d->qfam) {
        if (vk_debug) std::cerr << "[dock-vk] switching to queue family " << qi
                  << " (was " << d->qfam << ")\n";
        d->qfam = qi;
      }
      found = true;
      break;
    }
  }

  if (!found) {
    debug_log("vulkan", "surface not presentable on any queue family");
    std::cerr << "[dock-vk] surface not presentable on any queue family\n";
    std::cerr << "[dock-vk]   device=";
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(d->pdev, &props);
    std::cerr << props.deviceName << "\n";
    std::cerr << "[dock-vk]   queue families tested=" << nq
              << " chosen_qfam=" << d->qfam
              << " present_qfams=[";
    for (size_t pqi = 0; pqi < d->present_qfams.size(); ++pqi) {
      if (pqi) std::cerr << ",";
      std::cerr << d->present_qfams[pqi];
    }
    std::cerr << "]\n";
    for (uint32_t qi = 0; qi < nq; ++qi) {
      VkBool32 sup = VK_FALSE;
      VkResult vr_sup = vkGetPhysicalDeviceSurfaceSupportKHR(d->pdev, qi, impl_->surf, &sup);
      std::cerr << "[dock-vk]   qfam[" << qi << "]: vkGetPhysicalDeviceSurfaceSupportKHR="
                << vk_result_string(vr_sup) << " supported=" << (sup == VK_TRUE) << "\n";
    }
    destroy();
    return false;
  }

  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d->pdev, impl_->surf, &caps) != VK_SUCCESS) {
    debug_log("vulkan", "surface lost during init");
    std::cerr << "[dock-vk] surface lost during init\n";
    destroy();
    return false;
  }

  if (!pick_surface_format(d->pdev, impl_->surf, &impl_->format, &impl_->color_space)) {
    debug_log("vulkan", "no surface formats");
    std::cerr << "[dock-vk] no surface formats\n";
    destroy();
    return false;
  }

  VkSemaphoreCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkCommandBufferAllocateInfo cbai{};
  cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.commandPool = d->cmd_pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  for (int fi = 0; fi < kMaxFramesInFlight; ++fi) {
    auto& f = impl_->frames[fi];
    if (vkCreateSemaphore(d->dev, &sci, nullptr, &f.sem_img) != VK_SUCCESS ||
        vkCreateSemaphore(d->dev, &sci, nullptr, &f.sem_done) != VK_SUCCESS) {
      debug_log("vulkan", "vkCreateSemaphore failed");
      std::cerr << "[dock-vk] vkCreateSemaphore failed\n";
      destroy();
      return false;
    }
    if (vkCreateFence(d->dev, &fci, nullptr, &f.fence) != VK_SUCCESS) {
      debug_log("vulkan", "vkCreateFence failed");
      std::cerr << "[dock-vk] vkCreateFence failed\n";
      destroy();
      return false;
    }
    if (vkAllocateCommandBuffers(d->dev, &cbai, &f.cmd) != VK_SUCCESS) {
      debug_log("vulkan", "vkAllocateCommandBuffers failed");
      std::cerr << "[dock-vk] vkAllocateCommandBuffers failed\n";
      destroy();
      return false;
    }
  }

  if (!create_swapchain(ctx, width, height)) {
    debug_log("vulkan", "create_swapchain failed during VulkanLayerSurface::create");
    destroy();
    return false;
  }

  debug_log("vulkan", "VulkanLayerSurface::create OK");
  return true;
}

void VulkanLayerSurface::resize(int width, int height) {
    
  debug_log("vulkan", "VulkanLayerSurface::resize this=%p %dx%d", (void*)this, width, height);
  MANGOWM_DEBUG("VulkanLayerSurface::resize this=%p %dx%d", (void*)this, width, height);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const bool vk_debug = eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG");
  if (!impl_ || !impl_->ctx_reg || !impl_->ctx_reg->valid()) {
    debug_log("vulkan", "resize: bail no impl/ctx");
    if (vk_debug) std::cerr << "[vk-resize] bail: no impl/ctx\n";
    return;
  }
  if (width <= 0 || height <= 0) {
    debug_log("vulkan", "resize: bail invalid dims %dx%d", width, height);
    return;
  }
  if (width == impl_->last_w && height == impl_->last_h && impl_->swap != VK_NULL_HANDLE) {
    if (vk_debug) std::cerr << "[vk-resize] no change " << width << "x" << height << "\n";
    return;
  }
  debug_log("vulkan", "resize: proceeding %dx%d old_swap=%p", width, height, (void*)(impl_ ? impl_->swap : VK_NULL_HANDLE));
  if (vk_debug) std::cerr << "[vk-resize] enter " << width << "x" << height << " old_swap=" << static_cast<void*>(impl_->swap) << "\n";
  VulkanDisplayContext& c = *impl_->ctx_reg;
  VulkanDisplayContextImpl* d = c.impl();
  if (!d || d->dev == VK_NULL_HANDLE) {
    debug_log("vulkan", "resize: no device");
    if (vk_debug) std::cerr << "[vk-resize] no device\n";
    return;
  }
  VkDevice dev = d->dev;
  vkDeviceWaitIdle(dev);
  for (int fi = 0; fi < kMaxFramesInFlight; ++fi) {
    auto& f = impl_->frames[fi];
    if (f.fence != VK_NULL_HANDLE) {
      (void)vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
      vkDestroyFence(dev, f.fence, nullptr);
      f.fence = VK_NULL_HANDLE;
    }
    if (f.sem_img != VK_NULL_HANDLE) {
      vkDestroySemaphore(dev, f.sem_img, nullptr);
      f.sem_img = VK_NULL_HANDLE;
    }
    if (f.sem_done != VK_NULL_HANDLE) {
      vkDestroySemaphore(dev, f.sem_done, nullptr);
      f.sem_done = VK_NULL_HANDLE;
    }
    if (f.cmd != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(dev, d->cmd_pool, 1, &f.cmd);
      f.cmd = VK_NULL_HANDLE;
    }
  }
  VkSemaphoreCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkCommandBufferAllocateInfo cbai{};
  cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.commandPool = d->cmd_pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  for (int fi = 0; fi < kMaxFramesInFlight; ++fi) {
    auto& f = impl_->frames[fi];
    if (vkCreateSemaphore(dev, &sci, nullptr, &f.sem_img) != VK_SUCCESS ||
        vkCreateSemaphore(dev, &sci, nullptr, &f.sem_done) != VK_SUCCESS ||
        vkCreateFence(dev, &fci, nullptr, &f.fence) != VK_SUCCESS ||
        vkAllocateCommandBuffers(dev, &cbai, &f.cmd) != VK_SUCCESS) {
      debug_log("vulkan", "resize: failed to create frame %d sync objects", fi);
      if (vk_debug) std::cerr << "[vk-resize] failed to create frame " << fi << " sync objects\n";
      destroy();
      return;
    }
  }
  debug_log("vulkan", "resize: calling create_swapchain");
  if (vk_debug) std::cerr << "[vk-resize] calling create_swapchain old_swap=" << static_cast<void*>(impl_->swap) << "\n";
  if (!create_swapchain(c, width, height)) {
    debug_log("vulkan", "resize: create_swapchain failed");
    if (vk_debug) std::cerr << "[vk-resize] create_swapchain failed, calling destroy()\n";
    destroy();
  }
  debug_log("vulkan", "resize: done %dx%d", width, height);
}

bool VulkanLayerSurface::create_swapchain(VulkanDisplayContext& ctx, int width, int height) {
    
  debug_log("vulkan", "create_swapchain this=%p %dx%d swap=%p surf=%p",
    (void*)this, width, height, (void*)(impl_ ? impl_->swap : VK_NULL_HANDLE),
    (void*)(impl_ ? impl_->surf : VK_NULL_HANDLE));
  MANGOWM_INFO("create_swapchain this=%p %dx%d swap=%p surf=%p",
    (void*)this, width, height, (void*)(impl_ ? impl_->swap : VK_NULL_HANDLE),
    (void*)(impl_ ? impl_->surf : VK_NULL_HANDLE));
  VulkanDisplayContextImpl* d = ctx.impl();
  const bool vk_debug = eh::debug_profile::env_bool("EH_DOCK_VK_DEBUG");

  if (vk_debug) std::cerr << "[dock-vk] create_swapchain: surf=" << static_cast<void*>(impl_->surf)
              << " wl_surf=" << static_cast<void*>(impl_->wayland_surf) << "\n";

  VkSurfaceCapabilitiesKHR caps{};
  VkResult caps_res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d->pdev, impl_->surf, &caps);
  if (caps_res == VK_ERROR_SURFACE_LOST_KHR) {
    debug_log("vulkan", "surface lost during create_swapchain, recreating");
    MANGOWM_WARN("surface lost during create_swapchain, recreating");
    if (vk_debug) std::cerr << "[dock-vk] surface lost, recreating Vulkan surface...\n";

    if (impl_->wayland_surf && impl_->wayland_dpy) {
      vkDestroySurfaceKHR(d->instance, impl_->surf, nullptr);
      impl_->surf = VK_NULL_HANDLE;

      wl_display_roundtrip(impl_->wayland_dpy);
      VkWaylandSurfaceCreateInfoKHR wsci{};
      wsci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
      wsci.display = impl_->wayland_dpy;
      wsci.surface = impl_->wayland_surf;
      if (vkCreateWaylandSurfaceKHR(d->instance, &wsci, nullptr, &impl_->surf) != VK_SUCCESS) {
        debug_log("vulkan", "failed to recreate Vulkan surface");
        if (vk_debug) std::cerr << "[dock-vk] failed to recreate Vulkan surface\n";
        return false;
      }
      caps_res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d->pdev, impl_->surf, &caps);
    } else {
      debug_log("vulkan", "cannot recreate surface: wl_surface not available");
      if (vk_debug) std::cerr << "[dock-vk] cannot recreate surface: wl_surface not available\n";
      return false;
    }
  }
  if (caps_res != VK_SUCCESS) return false;

  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
    debug_log("vulkan", "swapchain does not support TRANSFER_DST");
    if (vk_debug) std::cerr << "[dock-vk] swapchain does not support TRANSFER_DST\n";
    return false;
  }

  VkExtent2D ext{caps.currentExtent.width, caps.currentExtent.height};
  if (ext.width == 0xffffffffu || caps.currentExtent.width == 0) {
    ext.width = static_cast<uint32_t>(width);
    ext.height = static_cast<uint32_t>(height);
    ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }

  uint32_t nic = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) nic = std::min(nic, caps.maxImageCount);

  impl_->drm_modifiers.clear();
  VkImageDrmFormatModifierListCreateInfoEXT mod_list_info{};
  if (d->have_drm_mod_ext && impl_->format != VK_FORMAT_UNDEFINED) {
    if (query_drm_format_modifiers(d->pdev, impl_->format, d->instance, impl_->drm_modifiers)) {
      mod_list_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
      mod_list_info.drmFormatModifierCount = static_cast<uint32_t>(impl_->drm_modifiers.size());
      mod_list_info.pDrmFormatModifiers = impl_->drm_modifiers.data();
    }
  }

  VkSwapchainCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  if (!impl_->drm_modifiers.empty()) sci.pNext = &mod_list_info;
  sci.surface = impl_->surf;
  sci.minImageCount = nic;
  sci.imageFormat = impl_->format;
  sci.imageColorSpace = impl_->color_space;
  sci.imageExtent = ext;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)) {
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) sci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }
  sci.presentMode = pick_present_mode(d->pdev, impl_->surf);
  sci.clipped = VK_TRUE;
  VkSwapchainKHR old_swap = impl_->swap;
  sci.oldSwapchain = old_swap;
  if (vk_debug) std::cerr << "[vk-create-swapchain] calling vkCreateSwapchainKHR oldSwapchain=" << static_cast<void*>(old_swap)
              << " surfcaps=" << caps.minImageCount << "-" << caps.maxImageCount
              << " extent=" << ext.width << "x" << ext.height << "\n";

  VkResult scr = vkCreateSwapchainKHR(d->dev, &sci, nullptr, &impl_->swap);
  if (scr != VK_SUCCESS) {
    debug_log("vulkan", "vkCreateSwapchainKHR failed: %s (retrying)", vk_result_string(scr));
    impl_->swap = old_swap;
    if (vk_debug) std::cerr << "[dock-vk] vkCreateSwapchainKHR failed: " << vk_result_string(scr)
                << " (retrying after device idle)\n";
    vkDeviceWaitIdle(d->dev);

    if (scr == VK_ERROR_SURFACE_LOST_KHR && impl_->wayland_surf && impl_->wayland_dpy) {
      debug_log("vulkan", "surface lost during vkCreateSwapchainKHR, recreating");
      if (vk_debug) std::cerr << "[dock-vk] recreating lost surface...\n";

      if (old_swap != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(d->dev, old_swap, nullptr);
        old_swap = VK_NULL_HANDLE;
      }
      vkDestroySurfaceKHR(d->instance, impl_->surf, nullptr);
      impl_->surf = VK_NULL_HANDLE;

      wl_display_roundtrip(impl_->wayland_dpy);
      VkWaylandSurfaceCreateInfoKHR wsci{};
      wsci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
      wsci.display = impl_->wayland_dpy;
      wsci.surface = impl_->wayland_surf;
      if (vkCreateWaylandSurfaceKHR(d->instance, &wsci, nullptr, &impl_->surf) != VK_SUCCESS) {
        debug_log("vulkan", "failed to recreate surface during swapchain retry");
        if (vk_debug) std::cerr << "[dock-vk] failed to recreate surface\n";
        return false;
      }
      sci.surface = impl_->surf;
      sci.oldSwapchain = VK_NULL_HANDLE;
      if (vk_debug) std::cerr << "[vk-create-swapchain] retrying with new surface, oldSwapchain=NULL\n";
    }

    scr = vkCreateSwapchainKHR(d->dev, &sci, nullptr, &impl_->swap);
    if (scr != VK_SUCCESS) {
      debug_log("vulkan", "swapchain retry failed: %s", vk_result_string(scr));
      std::cerr << "[vk-create-swapchain] retry failed: " << vk_result_string(scr) << "\n";
      return false;
    }
    if (vk_debug) std::cerr << "[vk-create-swapchain] retry succeeded new_swap=" << static_cast<void*>(impl_->swap) << "\n";
  }

  if (old_swap != VK_NULL_HANDLE && old_swap != impl_->swap) {
    if (vk_debug) std::cerr << "[vk-create-swapchain] destroying old swapchain " << static_cast<void*>(old_swap) << "\n";
    vkDestroySwapchainKHR(d->dev, old_swap, nullptr);
  }

  if (vk_debug) std::cerr << "[vk-create-swapchain] success new_swap=" << static_cast<void*>(impl_->swap) << "\n";

  uint32_t ni = 0;
  if (vkGetSwapchainImagesKHR(d->dev, impl_->swap, &ni, nullptr) != VK_SUCCESS) {
    if (vk_debug) std::cerr << "[vk-create-swapchain] vkGetSwapchainImagesKHR (count) failed\n";
    return false;
  }
  impl_->imgs.resize(ni);
  if (ni && vkGetSwapchainImagesKHR(d->dev, impl_->swap, &ni, impl_->imgs.data()) != VK_SUCCESS) {
    if (vk_debug) std::cerr << "[vk-create-swapchain] vkGetSwapchainImagesKHR (images) failed\n";
    impl_->imgs.clear();
    return false;
  }

  impl_->extent = ext;
  impl_->last_w = width;
  impl_->last_h = height;
  impl_->img_damage.assign(impl_->imgs.size(), DamageRegion{});
  for (auto& dmg : impl_->img_damage) dmg.mark_full();
  debug_log("vulkan", "create_swapchain OK: %d images, %dx%d", (int)impl_->imgs.size(), width, height);
  return !impl_->imgs.empty();
}

bool VulkanLayerSurface::ensure_staging(VulkanDisplayContext& ctx, size_t bytes, int fi) {
    
  debug_log("vulkan", "ensure_staging this=%p fi=%d bytes=%zu", (void*)this, fi, bytes);
  auto& f = impl_->frames[fi];
  MANGOWM_TRACE("ensure_staging this=%p fi=%d bytes=%zu cap=%zu", (void*)this, fi, bytes,
    static_cast<size_t>(f.staging_cap));
  VulkanDisplayContextImpl* d = ctx.impl();
  if (f.staging_cap >= static_cast<VkDeviceSize>(bytes) && f.staging_map) return true;
  if (f.staging != VK_NULL_HANDLE) {
    if (f.staging_map) {
      vkUnmapMemory(d->dev, f.staging_mem);
      f.staging_map = nullptr;
    }
    vkDestroyBuffer(d->dev, f.staging, nullptr);
    vkFreeMemory(d->dev, f.staging_mem, nullptr);
    f.staging = VK_NULL_HANDLE;
    f.staging_mem = VK_NULL_HANDLE;
    f.staging_cap = 0;
  }
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = static_cast<VkDeviceSize>(bytes);
  bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(d->dev, &bci, nullptr, &f.staging) != VK_SUCCESS) {
    debug_log("vulkan", "ensure_staging: vkCreateBuffer failed fi=%d bytes=%zu", fi, bytes);
    return false;
  }
  VkMemoryRequirements mr{};
  vkGetBufferMemoryRequirements(d->dev, f.staging, &mr);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = find_mem_type(d->mp, mr.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mai.memoryTypeIndex == k_u32_invalid) {
    mai.memoryTypeIndex = find_mem_type(d->mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  }
  if (mai.memoryTypeIndex == k_u32_invalid) {
    debug_log("vulkan", "ensure_staging: no suitable memory type fi=%d", fi);
    vkDestroyBuffer(d->dev, f.staging, nullptr);
    f.staging = VK_NULL_HANDLE;
    return false;
  }
  if (vkAllocateMemory(d->dev, &mai, nullptr, &f.staging_mem) != VK_SUCCESS) {
    debug_log("vulkan", "ensure_staging: vkAllocateMemory failed fi=%d", fi);
    vkDestroyBuffer(d->dev, f.staging, nullptr);
    f.staging = VK_NULL_HANDLE;
    return false;
  }
  if (vkBindBufferMemory(d->dev, f.staging, f.staging_mem, 0) != VK_SUCCESS) {
    debug_log("vulkan", "ensure_staging: vkBindBufferMemory failed fi=%d", fi);
    vkFreeMemory(d->dev, f.staging_mem, nullptr);
    vkDestroyBuffer(d->dev, f.staging, nullptr);
    f.staging = VK_NULL_HANDLE;
    f.staging_mem = VK_NULL_HANDLE;
    return false;
  }
  f.staging_coherent =
      (d->mp.memoryTypes[mai.memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
  if (vkMapMemory(d->dev, f.staging_mem, 0, VK_WHOLE_SIZE, 0, &f.staging_map) != VK_SUCCESS) {
    debug_log("vulkan", "ensure_staging: vkMapMemory failed fi=%d", fi);
    vkFreeMemory(d->dev, f.staging_mem, nullptr);
    vkDestroyBuffer(d->dev, f.staging, nullptr);
    f.staging = VK_NULL_HANDLE;
    f.staging_mem = VK_NULL_HANDLE;
    return false;
  }
  f.staging_cap = bci.size;
  debug_log("vulkan", "ensure_staging: OK fi=%d cap=%zu", fi, static_cast<size_t>(f.staging_cap));
  return true;
}

bool VulkanLayerSurface::present_cpu_bgra(VulkanDisplayContext& ctx, const unsigned char* data, int width, int height, int stride,
                                          bool* transient_recoverable) {
  DamageRegion full;
  full.mark_full();
  return present_cpu_bgra(ctx, data, width, height, stride, transient_recoverable, full, nullptr);
}

bool VulkanLayerSurface::present_cpu_bgra(VulkanDisplayContext& ctx, const unsigned char* data, int width, int height, int stride,
                                          bool* transient_recoverable, const DamageRegion& frame_damage,
                                          DamageRegion* effective_damage_out) {

  debug_log("vulkan", "present_cpu_bgra this=%p %dx%d stride=%d data=%p", (void*)this, width, height, stride, (void*)data);
  MANGOWM_TRACE("present_cpu_bgra this=%p %dx%d stride=%d data=%p", (void*)this, width, height, stride, (void*)data);
  MANGOWM_TRACE("present_cpu_bgra this=%p %dx%d stride=%d data=%p", (void*)this, width, height, stride, (void*)data);
#ifndef NDEBUG
  g_debug_present_count.fetch_add(1, std::memory_order_relaxed);
#endif
  if (transient_recoverable) *transient_recoverable = false;
  if (!ctx.valid() || !data || width <= 0 || height <= 0 || stride < width * 4) {
    debug_log("vulkan", "present: invalid args ctx.valid=%d data=%p %dx%d stride=%d", ctx.valid(), (void*)data, width, height, stride);
    MANGOWM_ERROR("present invalid args");
    std::cerr << "[vk-present] bail: invalid args\n";
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!impl_ || !impl_->swap) {
    debug_log("vulkan", "present: no impl_ or swap");
    MANGOWM_ERROR("present no impl or swap");
    std::cerr << "[vk-present] bail: no impl_ or swap\n";
    return false;
  }

  resize(width, height);
  if (!impl_ || !impl_->swap || impl_->imgs.empty()) {
    debug_log("vulkan", "present: after resize no impl_/swap/imgs");
    MANGOWM_ERROR("present after resize no impl/swap/imgs");
    std::cerr << "[vk-present] bail: after resize no impl_/swap/imgs\n";
    return false;
  }

  VulkanDisplayContextImpl* d = ctx.impl();
  VkDevice dev = d->dev;
  int fi = impl_->frame_idx;
  auto& f = impl_->frames[fi];

  const size_t need = static_cast<size_t>(width) * 4u * static_cast<size_t>(height);

  if (vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkWaitForFences failed fi=%d", fi);
    return false;
  }
  if (vkResetFences(dev, 1, &f.fence) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkResetFences failed fi=%d", fi);
    return false;
  }

  uint32_t idx = 0;
  VkResult ar = vkAcquireNextImageKHR(dev, impl_->swap, UINT64_MAX, f.sem_img, VK_NULL_HANDLE, &idx);
  if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_ERROR_SURFACE_LOST_KHR) {
    debug_log("vulkan", "present: acquire returned %s, resizing", vk_result_string(ar));
    std::cerr << "[vk-present] acquire returned " << vk_result_string(ar) << ", resizing\n";
    if (vkQueueSubmit(d->queue, 0, nullptr, f.fence) != VK_SUCCESS) {
      debug_log("vulkan", "present: vkQueueSubmit (empty) after acquire failure failed");
      return false;
    }
    resize(width, height);
    if (!impl_ || !impl_->swap) {
      debug_log("vulkan", "present: after out-of-date resize, no impl_/swap");
      std::cerr << "[vk-present] after out-of-date resize, no impl_/swap\n";
      return false;
    }
    if (vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
      debug_log("vulkan", "present: vkWaitForFences after resize failed");
      return false;
    }
    if (vkResetFences(dev, 1, &f.fence) != VK_SUCCESS) {
      debug_log("vulkan", "present: vkResetFences after resize failed");
      return false;
    }
    ar = vkAcquireNextImageKHR(dev, impl_->swap, UINT64_MAX, f.sem_img, VK_NULL_HANDLE, &idx);
  }

  if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
    debug_log("vulkan", "present: acquire failed: %s", vk_result_string(ar));
    std::cerr << "[vk-present] acquire failed: " << vk_result_string(ar) << "\n";
    if (vkQueueSubmit(d->queue, 0, nullptr, f.fence) != VK_SUCCESS) {
      debug_log("vulkan", "present: vkQueueSubmit (empty) after acquire fail failed");
      return false;
    }
    return false;
  }

  // Per-image damage history: this image needs everything recorded since its
  // last successful present, plus this frame's damage. The other images just
  // accumulate this frame's damage for when they get acquired next.
  if (impl_->img_damage.size() != impl_->imgs.size()) {
    impl_->img_damage.assign(impl_->imgs.size(), DamageRegion{});
    for (auto& dmg : impl_->img_damage) dmg.mark_full();
  }
  DamageRegion effective = impl_->img_damage[idx];
  effective.union_with(frame_damage);
  impl_->img_damage[idx] = effective;
  for (size_t si = 0; si < impl_->img_damage.size(); ++si) {
    if (si != static_cast<size_t>(idx)) impl_->img_damage[si].union_with(frame_damage);
  }
  const bool upload_all = effective.full();
  const bool skip_upload = effective.empty();

  if (!ensure_staging(ctx, need, fi)) {
    debug_log("vulkan", "present: ensure_staging failed fi=%d need=%zu", fi, need);
    return false;
  }

#ifndef NDEBUG
  int64_t uploaded_bytes = 0;
#endif
  if (!skip_upload) {
    auto* staging_base = static_cast<unsigned char*>(f.staging_map);
    if (upload_all) {
      std::memcpy(staging_base, data, need);
#ifndef NDEBUG
      uploaded_bytes = static_cast<int64_t>(need);
#endif
    } else {
      for (const auto& sp : effective.spans()) {
        const int32_t sx = std::clamp(sp.x, 0, width);
        const int32_t sy = std::clamp(sp.y, 0, height);
        const int32_t sw = std::min(sp.w, width - sx);
        const int32_t sh = std::min(sp.h, height - sy);
        if (sw <= 0 || sh <= 0) continue;
        for (int row = 0; row < sh; ++row) {
          const size_t y = static_cast<size_t>(sy) + static_cast<size_t>(row);
          const size_t src_off = y * static_cast<size_t>(stride) + static_cast<size_t>(sx) * 4u;
          const size_t dst_off = (y * static_cast<size_t>(width) + static_cast<size_t>(sx)) * 4u;
          std::memcpy(staging_base + dst_off, data + src_off, static_cast<size_t>(sw) * 4u);
        }
#ifndef NDEBUG
        uploaded_bytes += static_cast<int64_t>(sw) * 4 * static_cast<int64_t>(sh);
#endif
      }
    }
    if (!f.staging_coherent) {
      VkMappedMemoryRange rng{};
      rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      rng.memory = f.staging_mem;
      rng.size = VK_WHOLE_SIZE;
      (void)vkFlushMappedMemoryRanges(dev, 1, &rng);
    }
  }
#ifndef NDEBUG
  g_debug_present_bytes.fetch_add(uploaded_bytes, std::memory_order_relaxed);
#endif

  if (vkResetCommandBuffer(f.cmd, 0) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkResetCommandBuffer failed fi=%d", fi);
    return false;
  }
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(f.cmd, &bi) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkBeginCommandBuffer failed fi=%d", fi);
    return false;
  }

  VkImage img = impl_->imgs[idx];
  VkImageMemoryBarrier b0{};
  b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b0.image = img;
  b0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b0.subresourceRange.levelCount = 1;
  b0.subresourceRange.layerCount = 1;
  b0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &b0);

  uint32_t region_count = 0;
  VkBufferImageCopy regions[DamageRegion::kMaxSpans];
  const auto push_region = [&](int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    if (rw <= 0 || rh <= 0 || region_count >= DamageRegion::kMaxSpans) return;
    VkBufferImageCopy& r = regions[region_count++];
    r.bufferOffset =
        static_cast<VkDeviceSize>((static_cast<size_t>(ry) * static_cast<size_t>(width) + static_cast<size_t>(rx)) * 4u);
    r.bufferRowLength = static_cast<uint32_t>(width);
    r.bufferImageHeight = static_cast<uint32_t>(height);
    r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    r.imageSubresource.mipLevel = 0;
    r.imageSubresource.baseArrayLayer = 0;
    r.imageSubresource.layerCount = 1;
    r.imageOffset.x = rx;
    r.imageOffset.y = ry;
    r.imageOffset.z = 0;
    r.imageExtent.width = static_cast<uint32_t>(rw);
    r.imageExtent.height = static_cast<uint32_t>(rh);
    r.imageExtent.depth = 1;
  };
  if (upload_all) {
    push_region(0, 0, width, height);
  } else if (!skip_upload) {
    for (const auto& sp : effective.spans()) push_region(sp.x, sp.y, sp.w, sp.h);
  }

  if (region_count > 0)
    vkCmdCopyBufferToImage(f.cmd, f.staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count, regions);

  VkImageMemoryBarrier b1{};
  b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b1.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b1.image = img;
  b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b1.subresourceRange.levelCount = 1;
  b1.subresourceRange.layerCount = 1;
  b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b1.dstAccessMask = 0;

  vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &b1);

  if (vkEndCommandBuffer(f.cmd) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkEndCommandBuffer failed fi=%d", fi);
    return false;
  }

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &f.sem_img;
  si.pWaitDstStageMask = &wait_stage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &f.sem_done;

  if (vkQueueSubmit(d->queue, 1, &si, f.fence) != VK_SUCCESS) {
    debug_log("vulkan", "present: vkQueueSubmit failed fi=%d", fi);
    return false;
  }

  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &f.sem_done;
  pi.swapchainCount = 1;
  pi.pSwapchains = &impl_->swap;
  pi.pImageIndices = &idx;
  VkResult pr = vkQueuePresentKHR(d->queue, &pi);
  impl_->frame_idx = (fi + 1) % kMaxFramesInFlight;
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_ERROR_SURFACE_LOST_KHR) {
    debug_log("vulkan", "present: vkQueuePresentKHR returned %s (transient)", vk_result_string(pr));
    resize(width, height);
    if (transient_recoverable) *transient_recoverable = true;
    return false;
  }
  if (pr == VK_SUBOPTIMAL_KHR) {
    debug_log("vulkan", "present: vkQueuePresentKHR returned VK_SUBOPTIMAL_KHR (resizing)");
    const VkSwapchainKHR pre = impl_->swap;
    resize(width, height);
    // resize() early-outs when the dimensions are unchanged (contents persist),
    // so only clear the slot if the swapchain was not recreated — recreation
    // resets every slot to full inside create_swapchain.
    const bool recreated = !impl_ || impl_->swap != pre;
    if (!recreated && static_cast<size_t>(idx) < impl_->img_damage.size()) impl_->img_damage[idx].clear();
    if (!recreated && effective_damage_out) *effective_damage_out = effective;
    if (recreated && effective_damage_out) {
      *effective_damage_out = DamageRegion{};
      effective_damage_out->mark_full();
    }
    return true;
  }
  if (static_cast<size_t>(idx) < impl_->img_damage.size()) impl_->img_damage[idx].clear();
  if (effective_damage_out) *effective_damage_out = effective;
  debug_log("vulkan", "present: OK fi=%d frame_idx=%d", fi, impl_->frame_idx);
  return pr == VK_SUCCESS;
}

#ifndef NDEBUG
VulkanLayerSurface::FrameDrawStats VulkanLayerSurface::debug_snapshot_and_reset() noexcept {
  return {g_debug_present_count.exchange(0, std::memory_order_relaxed),
          g_debug_present_bytes.exchange(0, std::memory_order_relaxed)};
}
#endif

}
