#define _GNU_SOURCE 1
#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "wl/core/protocols.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/surface/vulkan_wayland.hpp"

#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "ux/Powermenu/power_menu.hpp"

#include "configuration/shell_config.hpp"
#include "configuration/shell_renderer_backend.hpp"
#include "desktop_shell/shared/popup/popup_position.hpp"

#include <toml++/toml.hpp>

#include "m3/core/primitives/box.hpp"

#include <wayland-client.h>

#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>
#include <spawn.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

const char* zone_name(eh::shell::dock::app_drawer::AppDrawerHitZone z) {
   
  using Z = eh::shell::dock::app_drawer::AppDrawerHitZone;
  switch (z) {
    case Z::None: return "None";
    case Z::SearchField: return "SearchField";
    case Z::CategoryTab: return "CategoryTab";
    case Z::AppListRow: return "AppListRow";
    case Z::PinnedApp: return "PinnedApp";
    case Z::PowerButton: return "PowerButton";
    case Z::NightlightButton: return "NightlightButton";
  }
  return "Unknown";
}

std::atomic_int g_close_request{0};
std::atomic_int g_reposition_request{0};
void on_sigusr1(int) { g_close_request.fetch_add(1, std::memory_order_relaxed); }
void on_sigusr2(int) { g_reposition_request.fetch_add(1, std::memory_order_relaxed); }

std::string runtime_dir() {
   
  if (const char* d = std::getenv("XDG_RUNTIME_DIR")) return d;
  return "/tmp";
}

std::string trim(std::string s) {
   
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i) s.erase(0, i);
  return s;
}

static std::string settings_path() { return eh::config::state_component_toml_path("dock"); }

static void enable_debug_log_to_file_if_needed() {
   
  if (eh_app_drawer_debug_level() <= 0) return;
  const std::string dir = settings_path();
  const std::size_t slash = dir.rfind('/');
  const std::string parent = (slash != std::string::npos) ? dir.substr(0, slash) : dir;
  (void)::mkdir(parent.c_str(), 0755);
  const std::string path = parent + "/appdrawer.log";
  const int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) return;
  (void)dup2(fd, STDERR_FILENO);
  close(fd);
  std::cerr.setf(std::ios::unitbuf);
  std::cerr << "[appdrawer] logging enabled path=\"" << path << "\" pid=" << getpid() << "\n";
}

std::vector<std::string> load_drawer_pinned_apps() {
   
  const std::string path = settings_path();
  if (::access(path.c_str(), F_OK) != 0) return {};
  try {
    toml::table tbl = toml::parse_file(path);
    if (auto* dock = tbl.get_as<toml::table>("dock")) {
      if (auto* arr = (*dock)["drawer_pinned_apps"].as_array()) {
        std::vector<std::string> out;
        for (auto& el : *arr) {
          if (auto s = el.value<std::string>()) out.push_back(*s);
        }
        if (!out.empty()) return out;
      }
      // fallback: pinned_apps
      if (auto* arr = (*dock)["pinned_apps"].as_array()) {
        std::vector<std::string> out;
        for (auto& el : *arr) {
          if (auto s = el.value<std::string>()) out.push_back(*s);
        }
        return out;
      }
    }
  } catch (const toml::parse_error&) {}
  return {};
}
}

void utf8_pop_back(std::string& s) {
   
  if (s.empty()) return;
  size_t i = s.size();
  while (i > 0) {
    --i;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0xc0u) != 0x80u) {
      s.resize(i);
      return;
    }
  }
  s.clear();
}

enum class Anchor : uint8_t { Top = 0, Bottom = 1 };

struct Request {
  Anchor anchor = Anchor::Top;
  int margin = 0;
  int x = -1;
  bool toggle = true;
};

std::string request_path() {
   
  return runtime_dir() + "/" + std::string(eh::shell::kAppDrawerNamespace) + ".request";
}
std::string lock_path() {
   
  return runtime_dir() + "/" + std::string(eh::shell::kAppDrawerNamespace) + ".lock";
}

void write_request_file(const Request& r) {
   
  const std::string tmp = request_path() + ".__tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return;
  fprintf(f, "anchor=%s\n", r.anchor == Anchor::Top ? "top" : "bottom");
  fprintf(f, "margin=%d\n", r.margin);
  if (r.x >= 0) fprintf(f, "x=%d\n", r.x);
  fclose(f);
  (void)std::rename(tmp.c_str(), request_path().c_str());
}

Request read_request_file_or_default() {
   
  Request r;
  std::ifstream f(request_path());
  if (!f.is_open()) return r;
  std::string line;
  while (std::getline(f, line)) {
    std::string v = trim(line);
    if (v.rfind("anchor=", 0) == 0) {
      const std::string a = trim(std::string(v.substr(std::strlen("anchor="))));
      r.anchor = (a == "bottom") ? Anchor::Bottom : Anchor::Top;
    } else if (v.rfind("margin=", 0) == 0) {
      const int val = std::atoi(std::string(v.substr(std::strlen("margin="))).c_str());
      r.margin = std::max(0, std::min(200, val));
    } else if (v.rfind("x=", 0) == 0) {
      const int val = std::atoi(std::string(v.substr(std::strlen("x="))).c_str());
      r.x = std::max(-1, std::min(8000, val));
    }
  }
  return r;
}

bool try_take_singleton_lock_or_signal_primary_and_exit(const Request& req) {
   
  const std::string lp = lock_path();
  const int fd = open(lp.c_str(), O_RDWR | O_CREAT, 0600);
  if (fd < 0) return true;
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {

    char buf[64]{};
    lseek(fd, 0, SEEK_SET);
    const ssize_t r = read(fd, buf, sizeof(buf) - 1);
    const pid_t pid = (r > 0) ? static_cast<pid_t>(std::atoi(buf)) : -1;
    if (pid > 0) {
      if (req.toggle) {
        kill(pid, SIGUSR1);
      } else {
        write_request_file(req);
        kill(pid, SIGUSR2);
      }
    }
    close(fd);
    return false;
  }

  ftruncate(fd, 0);
  char pidbuf[64];
  const int n = std::snprintf(pidbuf, sizeof(pidbuf), "%d\n", static_cast<int>(getpid()));
  (void)write(fd, pidbuf, static_cast<size_t>(std::max(0, n)));
  return true;
}

struct App {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  wl_output* output = nullptr;
  int outputW = 0;
  int outputH = 0;
  int outputScale = 1;
  zwlr_layer_shell_v1* layerShell = nullptr;

  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layerSurface = nullptr;

  bool configured = false;
  int configuredW = 0;
  int configuredH = 0;

  eh::wayland::ShmBuffer buf{};
  eh::wayland::CairoCpuBuffer glRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> vkLayer{};
  std::shared_ptr<eh::wayland::VulkanDisplayContext> vkDisplay{};
  bool vkFailed = false;
  bool deferRedraw = false;

  eh::icons::IconCache icons{};

  eh::shell::dock::app_drawer::AppDrawerState drawer{};

  double pointerX = 0.0;
  double pointerY = 0.0;

  xkb_context* xkbCtx = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;

  Request req{};

  bool ctxOpen = false;
  double ctxX = 0.0;
  double ctxY = 0.0;
  std::string ctxKey{};
  bool ctxIsPinned = false;

  bool running = true;
  bool pendingRedraw = false;

  uint32_t lastButtonSerial = 0;

  CompositorKind compositorKind = CompositorKind::Unknown;

  eh::ui::powermenu::PowerMenu powerMenu{};

  wl_callback* caretFrameCb = nullptr;
  uint64_t caretBlinkHalf = static_cast<uint64_t>(-1);
  uint64_t lastDrawMs = 0;
  int outputRefreshHz = 60;
};

void draw(App& app);
void appdrawer_destroy_caret_frame(App& app);
void appdrawer_queue_caret_frame(App& app);
void appdrawer_caret_frame_done(void* data, wl_callback* cb, uint32_t serial);

void on_buf_release(void* user) {
   
  auto& app = *static_cast<App*>(user);
  if (!app.pendingRedraw) return;
  app.pendingRedraw = false;
  draw(app);
}

bool appdrawer_renderer_env_is_cairo() {
   
  const char* e = std::getenv("EH_SHELL_RENDERER");
  if (!e || !*e) return false;
  std::string low(e);
  for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return low == "cairo";
}

bool appdrawer_want_vk(const App& app, const eh::config::ShellConfig& sc) {
   
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)sc;
  return false;
#else
  if (appdrawer_renderer_env_is_cairo()) return false;
  if (app.vkFailed) return false;
  return sc.renderer == eh::config::ShellRendererBackend::Vulkan;
#endif
}

void appdrawer_clear_all_gpu_surfaces(App& app) {
   
  app.vkLayer.reset();
  app.glRaster.destroy();
  app.vkDisplay.reset();
}

void appdrawer_clear_vk_present_state(App& app) {
   
  app.vkLayer.reset();
  app.glRaster.destroy();
  app.vkDisplay.reset();
}

void appdrawer_abort_vk(App& app) {
   
  app.vkFailed = true;
  appdrawer_clear_all_gpu_surfaces(app);
}

bool appdrawer_ensure_vk_raster(App& app, int buf_w, int buf_h) {
   
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)buf_w;
  (void)buf_h;
  return false;
#else
  if (!app.display || !app.surface) return false;
  if (!app.vkDisplay) app.vkDisplay = std::make_shared<eh::wayland::VulkanDisplayContext>();
  if (!app.vkDisplay->valid()) {
    if (!app.vkDisplay->init(app.display)) {
      appdrawer_abort_vk(app);
      std::cerr << "[appdrawer-vk] Vulkan display init failed; using SHM+Cairo\n";
      return false;
    }
  }
  if (!app.vkLayer || !app.vkLayer->valid()) {
    if (!app.vkLayer) app.vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
    if (!app.vkLayer->create(*app.vkDisplay, app.display, app.surface, buf_w, buf_h)) {
      appdrawer_abort_vk(app);
      std::cerr << "[appdrawer-vk] Vulkan surface/swapchain failed; using SHM+Cairo\n";
      return false;
    }
    if (app.compositor) wl_surface_set_opaque_region(app.surface, nullptr);
  } else {
    app.vkLayer->resize(buf_w, buf_h);
  }
  if (!app.glRaster.ensure(buf_w, buf_h)) {
    std::cerr << "[appdrawer-vk] CairoCpuBuffer::ensure failed; using SHM+Cairo\n";
    appdrawer_abort_vk(app);
    return false;
  }
  return true;
#endif
}

bool appdrawer_present_vk_raster(App& app, int buf_w, int buf_h, bool* transient_failure) {
   
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)buf_w;
  (void)buf_h;
  if (transient_failure) *transient_failure = false;
  return false;
#else
  bool transient = false;
  if (!app.vkDisplay || !app.vkLayer ||
      !app.vkLayer->present_cpu_bgra(*app.vkDisplay, app.glRaster.data(), buf_w, buf_h, app.glRaster.stride(), &transient)) {
    if (transient_failure) *transient_failure = transient;
    if (transient) {
      appdrawer_clear_vk_present_state(app);
      return false;
    }
    appdrawer_abort_vk(app);
    return false;
  }
  if (transient_failure) *transient_failure = false;
  return true;
#endif
}

void apply_layer_geometry(App& app) {
   
  if (!app.layerSurface) return;
  const int w = app.drawer.popupW;
  const int h = app.drawer.popupH;
  std::cerr << "[appdrawer] apply_geometry:"
            << " anchor=" << (app.req.anchor == Anchor::Top ? "top" : "bottom")
            << " margin=" << app.req.margin
            << " x=" << app.req.x
            << " size=" << w << "x" << h
            << "\n";
  zwlr_layer_surface_v1_set_size(app.layerSurface, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
  zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, 0);
  zwlr_layer_surface_v1_set_keyboard_interactivity(app.layerSurface,
                                                   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);

  PopupPositionInput in{};
  in.popup_w = w;
  in.popup_h = h;
  in.anchor_x = app.req.x >= 0 ? app.req.x : 0;
  in.bottom_clearance = app.req.margin;
  const PopupPositionOutput pos = compute_popup_position(in);

  if (app.req.anchor == Anchor::Top) {
    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    int margin_left = 0;
    if (app.req.x >= 0) {
      anchors |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
      margin_left = std::max(0, app.req.x);
    }
    std::cerr << "[appdrawer] wl_layer: anchors=TOP" << (app.req.x >= 0 ? "|LEFT" : "") << " top=" << app.req.margin
              << " left=" << margin_left << "\n";
    zwlr_layer_surface_v1_set_anchor(app.layerSurface, anchors);
    zwlr_layer_surface_v1_set_margin(app.layerSurface, app.req.margin, 0, 0, margin_left);
  } else {
    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    int margin_left = 0;
    if (app.req.x >= 0) {
      anchors |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
      margin_left = std::max(0, app.req.x);
    }
    std::cerr << "[appdrawer] wl_layer: anchors=BOTTOM" << (app.req.x >= 0 ? "|LEFT" : "") << " bottom=" << pos.margin_bottom
              << " left=" << margin_left << "\n";
    zwlr_layer_surface_v1_set_anchor(app.layerSurface, anchors);
    zwlr_layer_surface_v1_set_margin(app.layerSurface, 0, 0, pos.margin_bottom, margin_left);
  }
}

void layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
   
  auto& app = *static_cast<App*>(data);
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  app.configured = true;
  app.configuredW = static_cast<int>(width);
  app.configuredH = static_cast<int>(height);
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-wl] configure serial=" << serial << " w=" << app.configuredW << " h=" << app.configuredH << "\n";
  }
  draw(app);
}
void layer_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto& app = *static_cast<App*>(data);
  app.running = false;
}
static const zwlr_layer_surface_v1_listener kLayerListener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

void app_output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                                 int32_t) {}

void app_output_mode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t refresh_mHz) {
    
  if (!(flags & WL_OUTPUT_MODE_CURRENT) || width <= 0 || height <= 0) return;
  auto& app = *static_cast<App*>(data);
  app.outputW = width;
  app.outputH = height;
  if (refresh_mHz > 0) app.outputRefreshHz = (refresh_mHz + 500) / 1000;
}

void app_output_done(void*, wl_output*) {}
void app_output_scale(void* data, wl_output*, int32_t scale) {
   
  auto& app = *static_cast<App*>(data);
  if (scale > 0) app.outputScale = scale;
}
void app_output_name(void*, wl_output*, const char*) {}
void app_output_description(void*, wl_output*, const char*) {}

static const wl_output_listener app_output_listener = {
    .geometry = app_output_geometry,
    .mode = app_output_mode,
    .done = app_output_done,
    .scale = app_output_scale,
    .name = app_output_name,
    .description = app_output_description,
};

void registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
   
  auto& app = *static_cast<App*>(data);
  (void)version;
  if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
    app.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
    app.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
    app.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 7));
  } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
    app.layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4));
  } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
    app.output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, 1));
    wl_output_add_listener(app.output, &app_output_listener, &app);
  }
}
void registry_global_remove(void*, wl_registry*, uint32_t) {}
static const wl_registry_listener kRegistryListener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t sx, wl_fixed_t sy) {
   
  auto& app = *static_cast<App*>(data);
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-input] enter x=" << app.pointerX << " y=" << app.pointerY << "\n";
  }
}
void pointer_leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
   
  auto& app = *static_cast<App*>(data);
  (void)app;
  if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-input] leave\n";
}
void pointer_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
   
  auto& app = *static_cast<App*>(data);
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  app.drawer.pointerX = app.pointerX;
  app.drawer.pointerY = app.pointerY;
  const auto z = eh::shell::dock::app_drawer::app_drawer_hit_zone(app.drawer, app.pointerX, app.pointerY);
  int nh = -1;
  if (z == eh::shell::dock::app_drawer::AppDrawerHitZone::PinnedApp)
    nh = eh::shell::dock::app_drawer::app_drawer_pick_pinned_index(app.drawer, app.pointerX, app.pointerY);
  app.drawer.hoverDrawerPinIdx = nh;
  if (z == eh::shell::dock::app_drawer::AppDrawerHitZone::PowerButton)
    app.drawer.hoverPowerIdx = eh::shell::dock::app_drawer::app_drawer_pick_power_index(app.drawer, app.pointerX, app.pointerY);
  else if (z == eh::shell::dock::app_drawer::AppDrawerHitZone::NightlightButton)
    app.drawer.hoverPowerIdx = 99;
  else
    app.drawer.hoverPowerIdx = -1;
  if (eh_app_drawer_debug_level() >= 2) {
    std::cerr << "[appdrawer-input] motion x=" << app.pointerX << " y=" << app.pointerY << "\n";
  }
  draw(app);
}

static void launch_exec_command(const std::string& execLine) {
   
  if (execLine.empty()) return;

  const pid_t intermediate = ::fork();
  if (intermediate < 0) return;
  if (intermediate > 0) {
    ::waitpid(intermediate, nullptr, 0);
    return;
  }

  if (::setsid() < 0) ::_exit(1);

  const pid_t worker = ::fork();
  if (worker < 0) ::_exit(1);
  if (worker > 0) ::_exit(0);

  const int devnull = ::open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    ::dup2(devnull, STDIN_FILENO);
    ::dup2(devnull, STDOUT_FILENO);
    ::dup2(devnull, STDERR_FILENO);
    ::close(devnull);
  }

  std::vector<char> arg_lc(execLine.begin(), execLine.end());
  arg_lc.push_back('\0');

  // Launch through the per-user systemd manager whenever possible so the app's
  // process materializes in the user-writable (delegated) cgroup subtree —
  // user@1000.service/app.slice. VramBoostManager needs that to protect the
  // app's VRAM via dmem.min without running the shell as root. `--scope` keeps
  // the caller's full environment (games rely on it). As root the DE bypasses
  // this (root can write any cgroup already).
  if (::geteuid() != 0) {
    const std::string unit = "eh-app-" + std::to_string(static_cast<long>(::getpid()));
    std::vector<std::string> sdr = {"systemd-run", "--user", "--scope", "--collect",
                                    "--quiet", "--slice=app.slice", "--unit=" + unit,
                                    "--", "/bin/sh", "-lc", execLine};
    std::vector<char*> argvS;
    argvS.reserve(sdr.size() + 1);
    for (auto& a : sdr) argvS.push_back(a.data());
    argvS.push_back(nullptr);
    ::execvp("systemd-run", argvS.data());
  }

  char argv0[] = "sh";
  char argv1[] = "-lc";
  char* argv[] = {argv0, argv1, arg_lc.data(), nullptr};

  ::execvp("/bin/sh", argv);
  ::_exit(127);
}

void save_drawer_pinned_apps(const std::vector<std::string>& pins) {
   
  const std::string path = settings_path();
  const std::size_t slash = path.rfind('/');
  const std::string parent = (slash != std::string::npos) ? path.substr(0, slash) : path;
  (void)::mkdir(parent.c_str(), 0755);

  toml::table root;
  if (::access(path.c_str(), F_OK) == 0) {
    try { root = toml::parse_file(path); } catch (const toml::parse_error&) {}
  }

  toml::array arr;
  for (const auto& s : pins) arr.push_back(s);
  if (auto* dock = root["dock"].as_table()) {
    dock->insert_or_assign("drawer_pinned_apps", std::move(arr));
  } else {
    toml::table d;
    d.insert("drawer_pinned_apps", std::move(arr));
    root.insert("dock", std::move(d));
  }

  const std::string tmp = path + ".__ehtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << root;
    out.flush();
    if (!out.good()) { out.close(); (void)::remove(tmp.c_str()); return; }
    out.close();
  }
  (void)::rename(tmp.c_str(), path.c_str());
}

void pointer_button(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
   
  auto& app = *static_cast<App*>(data);
  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;

  app.lastButtonSerial = serial;
  if (app.layerSurface) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        app.layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    wl_surface_commit(app.surface);
    if (app.display) wl_display_flush(app.display);
  }
  const auto zone = eh::shell::dock::app_drawer::app_drawer_hit_zone(app.drawer, app.pointerX, app.pointerY);
  const int rowIdx = (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::AppListRow)
                         ? eh::shell::dock::app_drawer::app_drawer_pick_row_index(app.drawer, app.pointerX, app.pointerY)
                         : -1;
  const int pinIdx = (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::PinnedApp)
                         ? eh::shell::dock::app_drawer::app_drawer_pick_pinned_index(app.drawer, app.pointerX, app.pointerY)
                         : -1;
  const int pwrIdx = (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::PowerButton)
                         ? eh::shell::dock::app_drawer::app_drawer_pick_power_index(app.drawer, app.pointerX, app.pointerY)
                         : -1;
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-input] button btn=0x" << std::hex << button << std::dec
              << " x=" << app.pointerX << " y=" << app.pointerY
              << " zone=" << zone_name(zone)
              << " row=" << rowIdx << " pin=" << pinIdx << " power=" << pwrIdx
              << " ctxOpen=" << (app.ctxOpen ? 1 : 0)
              << "\n";
  }

  if (button == 0x111  ) {
    if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::AppListRow) {
      const int idx = eh::shell::dock::app_drawer::app_drawer_pick_row_index(app.drawer, app.pointerX, app.pointerY);
      if (idx >= 0 && idx < static_cast<int>(app.drawer.hits.size())) {
        app.ctxKey = app.drawer.hits[static_cast<size_t>(idx)].iconKey;
        app.ctxIsPinned =
            (std::find(app.drawer.pinnedApps.begin(), app.drawer.pinnedApps.end(), app.ctxKey) != app.drawer.pinnedApps.end());
        app.ctxOpen = true;
        app.ctxX = app.pointerX;
        app.ctxY = app.pointerY;
        if (eh_app_drawer_debug_level() >= 1) {
          std::cerr << "[appdrawer-input] ctx_open row idx=" << idx << " key=\"" << app.ctxKey
                    << "\" pinned=" << (app.ctxIsPinned ? 1 : 0) << "\n";
        }
        draw(app);
      }
    } else if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::PinnedApp) {
      const int p = eh::shell::dock::app_drawer::app_drawer_pick_pinned_index(app.drawer, app.pointerX, app.pointerY);
      if (p >= 0 && p < static_cast<int>(app.drawer.pinnedApps.size())) {
        app.ctxKey = app.drawer.pinnedApps[static_cast<size_t>(p)];
        app.ctxIsPinned = true;
        app.ctxOpen = true;
        app.ctxX = app.pointerX;
        app.ctxY = app.pointerY;
        if (eh_app_drawer_debug_level() >= 1) {
          std::cerr << "[appdrawer-input] ctx_open pinned idx=" << p << " key=\"" << app.ctxKey << "\"\n";
        }
        draw(app);
      }
    }
    return;
  }

  if (button != 0x110  ) return;

  if (app.ctxOpen) {
    const double mw = 220.0;
    const double mh = 44.0;
    double mx = app.ctxX;
    double my = app.ctxY;
    if (mx + mw > app.drawer.popupW - 8) mx = app.drawer.popupW - 8 - mw;
    if (my + mh > app.drawer.popupH - 8) my = app.drawer.popupH - 8 - mh;
    if (mx < 8) mx = 8;
    if (my < 8) my = 8;
    const bool in = (app.pointerX >= mx && app.pointerX < mx + mw && app.pointerY >= my && app.pointerY < my + mh);
    if (!in) {
      app.ctxOpen = false;
      if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-input] ctx_close outside\n";
      draw(app);
      return;
    }
    std::vector<std::string> pins = app.drawer.pinnedApps;
    auto it = std::find(pins.begin(), pins.end(), app.ctxKey);
    if (it != pins.end()) pins.erase(it);
    else if (!app.ctxKey.empty()) pins.push_back(app.ctxKey);
    save_drawer_pinned_apps(pins);
    app.drawer.pinnedApps = std::move(pins);
    app.ctxOpen = false;
    if (eh_app_drawer_debug_level() >= 1) {
      const bool nowPinned =
          (std::find(app.drawer.pinnedApps.begin(), app.drawer.pinnedApps.end(), app.ctxKey) != app.drawer.pinnedApps.end());
      std::cerr << "[appdrawer-input] ctx_toggle_pin key=\"" << app.ctxKey << "\" nowPinned=" << (nowPinned ? 1 : 0) << "\n";
    }
    draw(app);
    return;
  }

  if (app.powerMenu.open) {
    const auto res = eh::ui::powermenu::handle_click(app.powerMenu, app.pointerX, app.pointerY);
    if (res == eh::ui::powermenu::ClickResult::Confirm) {
      const int idx = app.powerMenu.actionIdx;
      eh::ui::powermenu::close(app.powerMenu);
      eh::shell::dock::app_drawer::app_drawer_power_exec(app.compositorKind, idx);
      app.running = false;
    } else if (res == eh::ui::powermenu::ClickResult::Cancel || res == eh::ui::powermenu::ClickResult::Close) {
      eh::ui::powermenu::close(app.powerMenu);
      if (app.layerSurface) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            app.layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
      }
      if (app.surface) {
        wl_surface_commit(app.surface);
        if (app.display) wl_display_flush(app.display);
      }
    }
    return;
  }

  if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::SearchField) {
    app.drawer.searchFieldFocused = true;
    draw(app);
    return;
  }

  if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::AppListRow) {
    app.drawer.searchFieldFocused = false;
    const int idx = eh::shell::dock::app_drawer::app_drawer_pick_row_index(app.drawer, app.pointerX, app.pointerY);
    if (idx >= 0 && idx < static_cast<int>(app.drawer.hits.size())) {
      if (eh_app_drawer_debug_level() >= 1) {
        std::cerr << "[appdrawer-input] launch row idx=" << idx << " name=\"" << app.drawer.hits[static_cast<size_t>(idx)].name
                  << "\" iconKey=\"" << app.drawer.hits[static_cast<size_t>(idx)].iconKey << "\"\n";
      }
      launch_exec_command(app.drawer.hits[static_cast<size_t>(idx)].exec);
      app.running = false;
    }
  } else if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::PinnedApp) {
    const int p = eh::shell::dock::app_drawer::app_drawer_pick_pinned_index(app.drawer, app.pointerX, app.pointerY);
    if (p >= 0 && p < static_cast<int>(app.drawer.pinnedApps.size())) {
      const std::string key = app.drawer.pinnedApps[static_cast<size_t>(p)];

      std::vector<SpotlightHit> tmp;
      eh_app_drawer_menu_query("", &tmp);
      for (const auto& h : tmp) {
        auto base = [](std::string s) -> std::string {
          if (const auto sl = s.rfind('/'); sl != std::string::npos) s = s.substr(sl + 1);
          return s;
        };
        const std::string kbase = base(key);
        const std::string hbase = base(h.path);
        if (!kbase.empty() && (kbase == hbase || kbase == hbase + ".desktop")) {
          if (eh_app_drawer_debug_level() >= 1) {
            std::cerr << "[appdrawer-input] launch pinned key=\"" << key << "\" matched path=\"" << h.path << "\"\n";
          }
          launch_exec_command(h.exec);
          app.running = false;
          break;
        }
        if (h.iconKey == key) {
          if (eh_app_drawer_debug_level() >= 1) {
            std::cerr << "[appdrawer-input] launch pinned key=\"" << key << "\" matched iconKey\n";
          }
          launch_exec_command(h.exec);
          app.running = false;
          break;
        }
      }
    }
  } else if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::PowerButton) {
    const int pwr = eh::shell::dock::app_drawer::app_drawer_pick_power_index(app.drawer, app.pointerX, app.pointerY);
    if (pwr == 0) {
      eh::shell::dock::app_drawer::app_drawer_power_exec(app.compositorKind, 0);
      app.running = false;
    } else if (pwr >= 1 && pwr <= 3) {
      eh::ui::powermenu::open(app.powerMenu, app.display, app.compositor, app.shm,
                               app.layerShell, app.output, app.outputW, app.outputH, app.outputScale, pwr);
    }
  } else if (zone == eh::shell::dock::app_drawer::AppDrawerHitZone::NightlightButton) {
    const bool next = !eh::shell::dock::app_drawer::get_nightlight_active();
    eh::shell::dock::app_drawer::set_nightlight_active(next);
    draw(app);
  }
}

static constexpr double kWheelStepPx = 48.0;
void appdrawer_destroy_caret_frame(App& app) {
   
  if (app.caretFrameCb) {
    wl_callback_destroy(app.caretFrameCb);
    app.caretFrameCb = nullptr;
  }
}

void appdrawer_queue_caret_frame(App& app) {
   
  if (!app.surface || app.caretFrameCb) return;
  const bool scrollAnimActive = std::abs(app.drawer.scrollPxCurrent - app.drawer.scrollPx) > 0.5;
  if (!scrollAnimActive && (!app.drawer.searchFieldFocused || app.ctxOpen)) return;
  const wl_callback_listener kAppDrawerCaretListener = {.done = appdrawer_caret_frame_done};
  app.caretFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.caretFrameCb, &kAppDrawerCaretListener, &app);
}

void appdrawer_caret_frame_done(void* data, wl_callback* cb, uint32_t  ) {
    
  auto& app = *static_cast<App*>(data);
  wl_callback_destroy(cb);
  app.caretFrameCb = nullptr;
  if (!app.surface) return;
  const bool scrollAnimActive = std::abs(app.drawer.scrollPxCurrent - app.drawer.scrollPx) > 0.5;
  if (!scrollAnimActive && (app.ctxOpen || !app.drawer.searchFieldFocused)) return;
  const uint64_t ms = eh::shell::monotonic_ms();
  constexpr uint64_t kHalfMs = 530;
  const uint64_t half = ms / kHalfMs;
  if (scrollAnimActive || half != app.caretBlinkHalf) {
    app.caretBlinkHalf = half;
    const uint64_t minIntervalMs = scrollAnimActive ? (1000u / std::max(1, app.outputRefreshHz)) : 0u;
    if (!scrollAnimActive || (ms - app.lastDrawMs) >= minIntervalMs) {
      draw(app);
    } else if (app.surface) {
      app.caretFrameCb = wl_surface_frame(app.surface);
      static const wl_callback_listener kAppDrawerCaretListener = {.done = appdrawer_caret_frame_done};
      wl_callback_add_listener(app.caretFrameCb, &kAppDrawerCaretListener, &app);
      wl_surface_commit(app.surface);
    }
  } else {
    appdrawer_queue_caret_frame(app);
  }
}

void drawer_scroll(App& app, double deltaPx) {
   
  if (deltaPx == 0.0) return;
  const double before = app.drawer.scrollPx;
  eh::shell::dock::app_drawer::app_drawer_scroll_pixels(app.drawer, deltaPx);
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-input] scroll deltaPx=" << deltaPx
              << " before=" << before << " after=" << app.drawer.scrollPx
              << " hits=" << app.drawer.hits.size() << "\n";
  }
  draw(app);
}
void pointer_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
   
  auto& app = *static_cast<App*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  const double dy = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dy * 20.0));
  if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-input] axis dy=" << dy << " deltaPx=" << deltaPx << "\n";
  drawer_scroll(app, deltaPx);
}
void pointer_frame(void*, wl_pointer*) {}
void pointer_axis_source(void*, wl_pointer*, uint32_t) {}
void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
void pointer_axis_discrete(void* data, wl_pointer*, uint32_t axis, int32_t discrete) {
   
  auto& app = *static_cast<App*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-input] axis_discrete discrete=" << discrete << "\n";
  drawer_scroll(app, static_cast<double>(discrete) * kWheelStepPx);
}
void pointer_axis_value120(void* data, wl_pointer*, uint32_t axis, int32_t value120) {
   
  auto& app = *static_cast<App*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-input] axis_value120 value120=" << value120 << "\n";
  drawer_scroll(app, (static_cast<double>(value120) / 120.0) * kWheelStepPx);
}
void pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}
#ifdef EH_HAVE_POINTER_WARP
void pointer_warp(void* data, wl_pointer* p, wl_fixed_t sx, wl_fixed_t sy) {
  pointer_motion(data, p, 0, sx, sy);
}
#endif

static const wl_pointer_listener kPointerListener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
    .warp = pointer_warp,
#endif
};

void keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
   
  auto& app = *static_cast<App*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !app.xkbCtx) {
    if (fd >= 0) close(fd);
    return;
  }
  char* map_str = static_cast<char*>(mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;

  if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
  if (app.xkbState) xkb_state_unref(app.xkbState);
  const size_t map_len = size > 0 ? static_cast<size_t>(size) - 1 : 0;
  app.xkbKeymap =
      xkb_keymap_new_from_buffer(app.xkbCtx, map_str, map_len, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, static_cast<size_t>(size));
  app.xkbState = app.xkbKeymap ? xkb_state_new(app.xkbKeymap) : nullptr;
}
void keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t keycode, uint32_t state) {
   
  auto& app = *static_cast<App*>(data);
  if (!app.xkbState) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, keycode + 8);
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-input] key keycode=" << keycode << " sym=0x" << std::hex << sym << std::dec
              << " queryLen=" << app.drawer.query.size()
              << " sel=" << app.drawer.sel << " hits=" << app.drawer.hits.size()
              << " ctxOpen=" << (app.ctxOpen ? 1 : 0)
              << "\n";
  }
  if (sym == XKB_KEY_Escape) {
    if (app.ctxOpen) {
      app.ctxOpen = false;
      draw(app);
      return;
    }
    app.running = false;
    return;
  }
  if (app.drawer.searchFieldFocused) {
    if (sym == XKB_KEY_Down) {
      app.drawer.searchFieldFocused = false;
      if (!app.drawer.hits.empty() && app.drawer.sel < 0) app.drawer.sel = 0;
      eh::shell::dock::app_drawer::app_drawer_ensure_sel_visible(app.drawer);
      draw(app);
      return;
    }
    if (sym == XKB_KEY_BackSpace) {
      utf8_pop_back(app.drawer.query);
      eh::shell::dock::app_drawer::app_drawer_refresh_hits(app.drawer);
      draw(app);
      return;
    }
    char utf8Sf[128]{};
    const int nsf = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8Sf, sizeof(utf8Sf) - 1);
    if (nsf > 0) {
      app.drawer.query.append(utf8Sf, static_cast<size_t>(nsf));
      eh::shell::dock::app_drawer::app_drawer_refresh_hits(app.drawer);
      draw(app);
      return;
    }
  }

  if (sym == XKB_KEY_Up) {
    if (!app.drawer.hits.empty()) {
      if (app.drawer.sel <= 0) app.drawer.sel = static_cast<int>(app.drawer.hits.size()) - 1;
      else app.drawer.sel--;
      eh::shell::dock::app_drawer::app_drawer_ensure_sel_visible(app.drawer);
    }
    draw(app);
    return;
  }
  if (sym == XKB_KEY_Down) {
    if (!app.drawer.hits.empty()) {
      if (app.drawer.sel < 0) app.drawer.sel = 0;
      else if (app.drawer.sel >= static_cast<int>(app.drawer.hits.size()) - 1) app.drawer.sel = 0;
      else app.drawer.sel++;
      eh::shell::dock::app_drawer::app_drawer_ensure_sel_visible(app.drawer);
    }
    draw(app);
    return;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (app.drawer.sel >= 0 && app.drawer.sel < static_cast<int>(app.drawer.hits.size())) {
      launch_exec_command(app.drawer.hits[static_cast<size_t>(app.drawer.sel)].exec);
      app.running = false;
      return;
    }
  }
}

void keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {
   
  auto& app = *static_cast<App*>(data);
  if (!app.xkbState) return;
  const auto gl = static_cast<xkb_layout_index_t>(group);
  xkb_state_update_mask(app.xkbState, depressed, latched, locked, gl, gl, gl);
}
void keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

static const wl_keyboard_listener kKeyboardListener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

void seat_capabilities(void* data, wl_seat* seat, uint32_t caps) {
   
  auto& app = *static_cast<App*>(data);
  if (eh_app_drawer_debug_level() >= 1) {
    std::cerr << "[appdrawer-input] seat_caps caps=0x" << std::hex << caps << std::dec
              << " ptr=" << (app.pointer ? 1 : 0) << " kbd=" << (app.keyboard ? 1 : 0) << "\n";
  }
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app.pointer) {
    app.pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app.pointer, &kPointerListener, &app);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && app.pointer) {
    wl_pointer_destroy(app.pointer);
    app.pointer = nullptr;
  }

  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app.keyboard) {
    app.keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app.keyboard, &kKeyboardListener, &app);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && app.keyboard) {
    wl_keyboard_destroy(app.keyboard);
    app.keyboard = nullptr;
  }
}
void seat_name(void*, wl_seat*, const char*) {}
static const wl_seat_listener kSeatListener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void draw(App& app) {
   
  if (!app.configured || !app.surface) return;

  using BenchClock = std::chrono::steady_clock;
  const BenchClock::time_point t0 = BenchClock::now();

  const int w = app.configuredW > 0 ? app.configuredW : app.drawer.popupW;
  const int h = app.configuredH > 0 ? app.configuredH : app.drawer.popupH;

  const eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
  app.drawer.viewMode = sc.appearance.launchpadViewMode;
  const bool want_vk = appdrawer_want_vk(app, sc);
  const bool want_gpu = want_vk;
  if (!want_gpu && app.buf.busy()) {
    app.pendingRedraw = true;
    if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-draw] buffer busy\n";
    return;
  }
  const BenchClock::time_point t1 = BenchClock::now();

  bool vk_path = false;
  if (want_vk && appdrawer_ensure_vk_raster(app, w, h)) vk_path = true;
  if (!vk_path) {
    if (!app.buf.ensure(app.shm, eh::shell::kAppDrawerNamespace, w, h)) return;
    if (app.buf.busy()) {
      app.pendingRedraw = true;
      if (eh_app_drawer_debug_level() >= 1) std::cerr << "[appdrawer-draw] buffer busy\n";
      return;
    }
  }
  const BenchClock::time_point t2 = BenchClock::now();

  if (app.compositor) {
    wl_region* input_region = wl_compositor_create_region(app.compositor);
    if (input_region) {
      wl_region_add(input_region, 0, 0, w, h);
      wl_surface_set_input_region(app.surface, input_region);
      wl_region_destroy(input_region);
    }
  }
  if (eh_app_drawer_debug_level() >= 2) std::cerr << "[appdrawer-draw] regions set w=" << w << " h=" << h << "\n";

  cairo_t* const cr = vk_path ? app.glRaster.cairo() : app.buf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  const BenchClock::time_point t3 = BenchClock::now();

  eh::shell::dock::app_drawer::app_drawer_paint(app.drawer, cr, app.icons);

  if (app.ctxOpen) {
    const double mw = 220.0;
    const double mh = 44.0;
    double mx = app.ctxX;
    double my = app.ctxY;
    if (mx + mw > w - 8) mx = w - 8 - mw;
    if (my + mh > h - 8) my = h - 8 - mh;
    if (mx < 8) mx = 8;
    if (my < 8) my = 8;

    {
      m3::Box box;
      box.setColor(0.06f, 0.08f, 0.10f, 0.96f);
      box.setRadius(10.0f);
      box.setGeometry(mx, my, mw, mh);
      box.setGlassy(true);
      box.paint(cr);
    }

    cairo_new_path(cr);
    {
      const double r = 10.0;
      cairo_arc(cr, mx + mw - r, my + r, r, -M_PI_2, 0);
      cairo_arc(cr, mx + mw - r, my + mh - r, r, 0, M_PI_2);
      cairo_arc(cr, mx + r, my + mh - r, r, M_PI_2, M_PI);
      cairo_arc(cr, mx + r, my + r, r, M_PI, 3 * M_PI_2);
      cairo_close_path(cr);
    }
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, 0.92, 0.95, 0.97, 0.95);
    cairo_move_to(cr, mx + 12, my + 27);
    cairo_show_text(cr, app.ctxIsPinned ? "Unpin from dock" : "Pin to dock");
  }
  const BenchClock::time_point t4 = BenchClock::now();

  cairo_restore(cr);
  cairo_surface_flush(vk_path ? app.glRaster.cairo_surface() : app.buf.cairo_surface());

  appdrawer_destroy_caret_frame(app);
  if (vk_path) {
    bool transient = false;
    if (!appdrawer_present_vk_raster(app, w, h, &transient)) {
      if (transient) app.deferRedraw = true;
      return;
    }
  } else {
    wl_surface_attach(app.surface, app.buf.wl(), 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, w, h);
    app.buf.mark_busy();
  }
  app.lastDrawMs = eh::shell::monotonic_ms();
  appdrawer_queue_caret_frame(app);
  wl_surface_commit(app.surface);

  if (app.display) (void)wl_display_flush(app.display);

  if (eh_menu_bench()) {
    static unsigned s_drawN = 0;
    ++s_drawN;
    const auto us_total = std::chrono::duration_cast<std::chrono::microseconds>(BenchClock::now() - t0).count();
    const auto us_snapshot = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto us_buffers = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    const auto us_clear = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    const auto us_body = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
    const long us_commit = us_total - us_snapshot - us_buffers - us_clear - us_body;
    const double fps_inst = us_total > 0 ? 1000000.0 / static_cast<double>(us_total) : 0.0;
    char line[1024];
    snprintf(line, sizeof(line),
      "[menu-bench] draw#%u %dx%d slot=%s snapshot=%ldus buffers=%ldus clear=%ldus body=%ldus commit=%ldus total=%ldus fps_inst=%.1f\n",
      s_drawN, w, h, vk_path ? "vk" : "shm",
      static_cast<long>(us_snapshot), static_cast<long>(us_buffers),
      static_cast<long>(us_clear), static_cast<long>(us_body),
      static_cast<long>(us_commit), static_cast<long>(us_total), fps_inst);
    std::cerr << line;
    static FILE* s_log = nullptr;
    if (!s_log) {
      s_log = fopen("/tmp/eh_menu_bench.log", "w");
      if (s_log) {
        time_t now_t = time(nullptr);
        fprintf(s_log, "# Event Horizon Menu Bench  %s", ctime(&now_t));
        fprintf(s_log, "# draw# WxH slot snapshot buffers clear body commit total fps_inst\n");
      }
    }
    if (s_log) { fputs(line, s_log); fflush(s_log); }
  }
}

void parse_args(int argc, char** argv, Request& out) {
   
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i] ? argv[i] : "";
    if (a == "--anchor=bottom") out.anchor = Anchor::Bottom;
    else if (a == "--anchor=top") out.anchor = Anchor::Top;
    else if (a.rfind("--margin=", 0) == 0) out.margin = std::max(0, std::min(200, std::atoi(a.substr(9).c_str())));
    else if (a.rfind("--x=", 0) == 0) out.x = std::max(0, std::min(8000, std::atoi(a.substr(4).c_str())));
    else if (a == "--toggle") out.toggle = true;
    else if (a == "--reposition") out.toggle = false;
  }
}

int main(int argc, char** argv) {
   
  Request req;
  parse_args(argc, argv, req);
  write_request_file(req);

  if (!try_take_singleton_lock_or_signal_primary_and_exit(req)) return 0;

  signal(SIGUSR1, on_sigusr1);
  signal(SIGUSR2, on_sigusr2);

  enable_debug_log_to_file_if_needed();

  (void)FcInit();
  eh::shell::ensure_material_symbols_font_registered();

  App app;
  app.req = read_request_file_or_default();
  app.buf.set_release_hook(on_buf_release, &app);
  app.drawer.popupW = eh::shell::dock::app_drawer::app_drawer_popup_width();
  app.drawer.popupH = eh::shell::dock::app_drawer::app_drawer_popup_height();
  app.drawer.pinnedApps = load_drawer_pinned_apps();
  eh::shell::dock::app_drawer::app_drawer_refresh_hits(app.drawer);

  app.display = wl_display_connect(nullptr);
  if (!app.display) {
    const char* wd = std::getenv("WAYLAND_DISPLAY");
    const char* xr = std::getenv("XDG_RUNTIME_DIR");
    std::cerr << "[appdrawer] wl_display_connect failed"
              << " WAYLAND_DISPLAY=\"" << (wd ? wd : "") << "\""
              << " XDG_RUNTIME_DIR=\"" << (xr ? xr : "") << "\"\n";
    return 1;
  }
  app.registry = wl_display_get_registry(app.display);
  wl_registry_add_listener(app.registry, &kRegistryListener, &app);
  wl_display_roundtrip(app.display);
  wl_display_roundtrip(app.display);

  app.compositorKind = detect_compositor_kind();

  if (!app.compositor || !app.shm || !app.layerShell) {
    std::cerr << "[appdrawer] missing globals (compositor/shm/layer-shell)\n";
    return 1;
  }

  if (app.seat) {
    wl_seat_add_listener(app.seat, &kSeatListener, &app);
    wl_display_roundtrip(app.display);
  }
  app.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

  {
    eh::wayland::LayerSurfaceConfig cfg{};
    cfg.nameSpace = eh::shell::kAppDrawerNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    cfg.anchor = 0;
    cfg.width = static_cast<uint32_t>(app.drawer.popupW);
    cfg.height = static_cast<uint32_t>(app.drawer.popupH);
    cfg.exclusiveZone = 0;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = 0;
    cfg.marginLeft = 0;
    cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
    if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, nullptr, cfg,
                                          &kLayerListener, &app, &app.surface, &app.layerSurface)) {
      std::cerr << "[appdrawer] failed to create layer surface\n";
      return 1;
    }
    wl_surface_set_buffer_scale(app.surface, 1);
  }
  apply_layer_geometry(app);
  wl_surface_commit(app.surface);
  if (app.display) wl_display_roundtrip(app.display);

  const int fd = wl_display_get_fd(app.display);
  time_t lastIconThemeCheck = 0;
  while (app.running) {
    if (g_close_request.exchange(0, std::memory_order_relaxed) > 0) break;
    if (g_reposition_request.exchange(0, std::memory_order_relaxed) > 0) {
      app.req = read_request_file_or_default();
      apply_layer_geometry(app);
      app.drawer.pinnedApps = load_drawer_pinned_apps();
      wl_surface_commit(app.surface);
      wl_display_flush(app.display);
    }

    {
      const time_t now = time(nullptr);
      if (now != (time_t)-1 && now - lastIconThemeCheck >= 1) {
        lastIconThemeCheck = now;
        if (app.icons.refresh_auto_theme_if_needed()) draw(app);
      }
    }

    (void)wl_display_flush(app.display);
    (void)wl_display_dispatch_pending(app.display);
    if (app.deferRedraw) {
      app.deferRedraw = false;
      draw(app);
    }
    if (app.powerMenu.open) {
      if (eh::ui::powermenu::tick(app.powerMenu)) {
        const int idx = app.powerMenu.actionIdx;
        eh::ui::powermenu::close(app.powerMenu);
        eh::shell::dock::app_drawer::app_drawer_power_exec(app.compositorKind, idx);
        app.running = false;
        break;
      }
      app.powerMenu.pointerX = app.pointerX;
      app.powerMenu.pointerY = app.pointerY;
      if (app.powerMenu.configured) {
        eh::ui::powermenu::draw(app.powerMenu, app.pointerX, app.pointerY);
      }
    }

    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    const int pr = poll(&pfd, 1, 50);
    if (pr > 0 && (pfd.revents & POLLIN)) {
      if (wl_display_dispatch(app.display) < 0) break;
      if (app.deferRedraw) {
        app.deferRedraw = false;
        draw(app);
      }
      if (app.powerMenu.open && app.powerMenu.configured) {
        app.powerMenu.pointerX = app.pointerX;
        app.powerMenu.pointerY = app.pointerY;
        eh::ui::powermenu::draw(app.powerMenu, app.pointerX, app.pointerY);
      }
    }
  }

  eh::ui::powermenu::close(app.powerMenu);
  appdrawer_clear_all_gpu_surfaces(app);
  app.buf.destroy();
  if (app.pointer) wl_pointer_destroy(app.pointer);
  if (app.keyboard) wl_keyboard_destroy(app.keyboard);
  if (app.seat) wl_seat_destroy(app.seat);
  if (app.layerSurface) zwlr_layer_surface_v1_destroy(app.layerSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  if (app.layerShell) zwlr_layer_shell_v1_destroy(app.layerShell);
  if (app.shm) wl_shm_destroy(app.shm);
  if (app.compositor) wl_compositor_destroy(app.compositor);
  if (app.registry) wl_registry_destroy(app.registry);
  if (app.xkbState) xkb_state_unref(app.xkbState);
  if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
  if (app.xkbCtx) xkb_context_unref(app.xkbCtx);
  if (app.display) wl_display_disconnect(app.display);
  return 0;
}
