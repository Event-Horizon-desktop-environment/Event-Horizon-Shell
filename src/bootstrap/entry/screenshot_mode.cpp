#include "bootstrap/entry/screenshot_mode.hpp"
#include "bootstrap/entry/region_select.hpp"

#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "wl/capture/screencopy_png.hpp"
#include "wl/core/connection.hpp"
#include "wl/input/clipboard.hpp"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <wayland-client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace eh::app {
namespace {

void print_usage(const char* argv0) {
   
  std::cerr << "Usage: " << (argv0 ? argv0 : "event-horizon")
             << " --eh-screenshot [options] [<file.png>]\n"
               "\n"
               "Options:\n"
               "  --cursor              Include cursor in capture\n"
               "  -o, --output NAME     wl_output name (full capture; default: largest logical output)\n"
                "  --geometry WxH+X+Y    Region in compositor-global logical coordinates\n"
                "  -g WxH+X+Y            Same as --geometry\n"
                "  --select              Interactive region selection (built-in, no external tools)\n"
                "  --around-mouse WxH    Capture a W×H region centered on the current mouse cursor\n"
                "  --no-clipboard        Do not copy the PNG to the clipboard\n"
                "  --preview             Show the screenshot preview window (GUI with Copy / Save / Save As)\n"
                "  --list-windows        List available toplevel windows and exit\n"
                "  --toplevel ID         Capture a specific toplevel window by identifier\n"
                "  --window ID           Same as --toplevel\n"
               "\n"
               "After a successful save, the image is copied to the clipboard (image/png) via the Wayland\n"
               "data-control protocol, unless --no-clipboard is set.\n"
               "\n"
                 "Full output: omit --geometry / --select (uses --output or largest output).\n"
                 "             Uses ext-image-copy-capture if available, falls back to zwlr_screencopy.\n"
                 "Area: uses --geometry / --around-mouse / --select.  Captures the full output via\n"
                 "      ext-image-copy-capture and crops to the requested region.  Falls back to\n"
                 "      zwlr_screencopy region capture.\n"
               "Window: uses --toplevel / --window.  Requires ext-foreign-toplevel-list (Hyprland).\n";
}

[[nodiscard]] bool parse_geometry_wh(const std::string& raw, int* gw, int* gh, int* gx, int* gy) {
   
  std::string s = raw;
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  if (s.empty()) return false;
  int rw = 0;
  int rh = 0;
  int rx = 0;
  int ry = 0;
  if (std::sscanf(s.c_str(), "%dx%d+%d+%d", &rw, &rh, &rx, &ry) != 4) return false;
  if (rw < 1 || rh < 1) return false;
  *gw = rw;
  *gh = rh;
  *gx = rx;
  *gy = ry;
  return true;
}

[[nodiscard]] std::optional<std::string> read_slurp_line() {
   
  FILE* p = popen("slurp -f '%wx%h+%x+%y' 2>/dev/null", "r");
  if (!p) return std::nullopt;
  std::array<char, 512> buf{};
  (void)std::fread(buf.data(), 1, buf.size() - 1, p);
  const int st = pclose(p);
  if (st != 0) return std::nullopt;
  std::string line(buf.data());
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  if (line.empty()) return std::nullopt;
  return line;
}

[[nodiscard]] bool crop_png_file(const std::string& src_path, const std::string& dst_path,
                                  int crop_x, int crop_y, int crop_w, int crop_h) {
   
  int w = 0;
  int h = 0;
  unsigned char* pixels = stbi_load(src_path.c_str(), &w, &h, nullptr, 4);
  if (!pixels) {
    std::cerr << "Failed to load intermediate PNG for crop.\n";
    return false;
  }

  if (crop_x < 0) { crop_w += crop_x; crop_x = 0; }
  if (crop_y < 0) { crop_h += crop_y; crop_y = 0; }
  crop_w = std::min(crop_w, w - crop_x);
  crop_h = std::min(crop_h, h - crop_y);

  if (crop_w <= 0 || crop_h <= 0) {
    std::cerr << "Crop region is outside the captured output.\n";
    stbi_image_free(pixels);
    return false;
  }

  std::vector<unsigned char> cropped(static_cast<std::size_t>(crop_w) * crop_h * 4);
  for (int y = 0; y < crop_h; ++y) {
    std::memcpy(&cropped[static_cast<std::size_t>(y) * crop_w * 4],
                &pixels[(static_cast<std::size_t>(crop_y + y) * w + crop_x) * 4],
                static_cast<std::size_t>(crop_w) * 4);
  }
  stbi_image_free(pixels);

  if (!stbi_write_png(dst_path.c_str(), crop_w, crop_h, 4, cropped.data(), crop_w * 4)) {
    std::cerr << "Failed to write cropped PNG.\n";
    return false;
  }
  return true;
}

[[nodiscard]] int get_output_global_x(const std::vector<eh::wayland::LogicalOutputBounds>& bounds,
                                        wl_output* output) {
   
  for (const auto& b : bounds) {
    if (b.output == output) return b.global_x;
  }
  return 0;
}

[[nodiscard]] int get_output_global_y(const std::vector<eh::wayland::LogicalOutputBounds>& bounds,
                                        wl_output* output) {
   
  for (const auto& b : bounds) {
    if (b.output == output) return b.global_y;
  }
  return 0;
}

void spawn_clipboard_daemon(const std::string& daemon_path, const std::string& png_path) {
  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "fork failed: " << std::strerror(errno) << "\n";
    return;
  }
  if (pid > 0) return;  // Parent returns immediately

  // Child side: hand the PNG off to the clipboard daemon.
  execlp(daemon_path.c_str(), daemon_path.c_str(), png_path.c_str(), nullptr);

  // If the absolute path failed, retry with just the binary name.
  const char* basename = strrchr(daemon_path.c_str(), '/');
  if (basename) {
    ++basename;
    execlp(basename, basename, png_path.c_str(), nullptr);
  }
  std::cerr << "Failed to launch clipboard daemon: " << std::strerror(errno) << "\n";
  _exit(1);
}

} // namespace

int run_screenshot_mode(int argc, char** argv) {
  std::string out_path;
  std::string output_name;
  std::string geometry_arg;
  std::string toplevel_id;
  std::string around_mouse_geometry;
  bool cursor = false;
  bool use_select = false;
  bool use_around_mouse = false;
  bool use_slurp = false;
  bool clipboard = false;
  bool preview = false;
  bool list_windows = false;
  std::string clipboard_daemon_binary;

  // Resolve the daemon path relative to our own executable.
  if (argc > 0 && argv[0]) {
    std::string self = argv[0];
    auto slash = self.rfind('/');
    std::string dir = (slash != std::string::npos) ? self.substr(0, slash + 1) : "";
    clipboard_daemon_binary = dir + "horizonClipboardDaemon";
  } else {
    clipboard_daemon_binary = "horizonClipboardDaemon";
  }

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (!a) continue;
    if (std::strcmp(a, "--eh-screenshot") == 0) continue;
    if (std::strcmp(a, "--cursor") == 0) {
      cursor = true;
      continue;
    }
    if (std::strcmp(a, "--select") == 0) {
      use_select = true;
      continue;
    }
    if (std::strcmp(a, "--around-mouse") == 0) {
      use_around_mouse = true;
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      around_mouse_geometry = argv[++i];
      continue;
    }
    if (std::strcmp(a, "--slurp") == 0) {
      use_slurp = true;
      continue;
    }
    if (std::strcmp(a, "--clipboard") == 0) {
      clipboard = true;
      continue;
    }
    if (std::strcmp(a, "--no-clipboard") == 0) {
      clipboard = false;
      continue;
    }
    if (std::strcmp(a, "--preview") == 0) {
      preview = true;
      continue;
    }
    if (std::strcmp(a, "--list-windows") == 0) {
      list_windows = true;
      continue;
    }
    if (std::strcmp(a, "--toplevel") == 0 || std::strcmp(a, "--window") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      toplevel_id = argv[++i];
      continue;
    }
    if (std::strcmp(a, "--geometry") == 0 || std::strcmp(a, "-g") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      geometry_arg = argv[++i];
      continue;
    }
    if (std::strcmp(a, "--output") == 0 || std::strcmp(a, "-o") == 0) {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      output_name = argv[++i];
      continue;
    }
    if (a[0] == '-') {
      std::cerr << "Unknown option: " << a << '\n';
      print_usage(argv[0]);
      return 2;
    }
    out_path = a;
  }

  if (list_windows && !out_path.empty()) {
    std::cerr << "--list-windows takes no output file argument.\n";
    return 2;
  }
  if (!list_windows && out_path.empty() && !(preview || clipboard || use_select || use_around_mouse)) {
    print_usage(argv[0]);
    return 2;
  }

  // No output path given but one is required — capture to a temp file instead.
  if (out_path.empty() && (preview || clipboard || use_select || use_around_mouse)) {
    char tmp_pattern[] = "/tmp/eh-screenshot-XXXXXX";
    const int tmp_fd = mkstemp(tmp_pattern);
    if (tmp_fd < 0) {
      std::cerr << "Failed to create temp file for screenshot.\n";
      return 1;
    }
    out_path = tmp_pattern;
    close(tmp_fd);
  }
  if (use_select && !geometry_arg.empty()) {
    std::cerr << "Use either --select or --geometry, not both.\n";
    return 2;
  }
  if (use_slurp && !geometry_arg.empty()) {
    std::cerr << "Use either --slurp or --geometry, not both.\n";
    return 2;
  }
  if (use_around_mouse && !geometry_arg.empty()) {
    std::cerr << "Use either --around-mouse or --geometry, not both.\n";
    return 2;
  }
  if ((!toplevel_id.empty()) + (use_select || use_slurp || use_around_mouse || !geometry_arg.empty()) > 1) {
    std::cerr << "Use only one of --select, --toplevel/--window, --slurp, --around-mouse, or --geometry.\n";
    return 2;
  }

  eh::wayland::WaylandConnection wl;
  if (!wl.connect()) {
    std::cerr << "Wayland connection failed (WAYLAND_DISPLAY?)\n";
    return 1;
  }
  wl.refresh_logical_outputs();

  if (!wl.shm()) {
    std::cerr << "wl_shm not available.\n";
    return 1;
  }

  // Handle --list-windows.
  if (list_windows) {
    if (!wl.has_ext_foreign_toplevel_list()) {
      std::cerr << "ext-foreign-toplevel-list not available (not Hyprland?).\n";
      return 1;
    }
    wl_display_roundtrip(wl.display());
    const auto& toplevels = wl.ext_foreign_toplevels().list();
    for (const auto& tl : toplevels) {
      std::cout << tl.identifier << "  " << tl.appId << "  " << tl.title << "\n";
    }
    return 0;
  }

  // Handle --toplevel / --window capture.
  if (!toplevel_id.empty()) {
    if (!wl.has_ext_foreign_toplevel_list()) {
      std::cerr << "Window capture requires ext-foreign-toplevel-list (not available).\n";
      return 1;
    }
    if (!wl.ext_foreign_toplevel_image_capture_source_manager()) {
      std::cerr << "Window capture requires ext-foreign-toplevel-image-capture-source (not available).\n";
      return 1;
    }
    if (!wl.ext_image_copy_capture_manager()) {
      std::cerr << "Window capture requires ext-image-copy-capture (not available).\n";
      return 1;
    }

    wl_display_roundtrip(wl.display());
    const auto& toplevels = wl.ext_foreign_toplevels().list();

    const eh::wayland::ExtForeignToplevels::Toplevel* found = nullptr;
    for (const auto& tl : toplevels) {
      if (tl.identifier == toplevel_id) {
        found = &tl;
        break;
      }
    }
    if (!found) {
      std::cerr << "No toplevel found with identifier: " << toplevel_id << "\n";
      return 1;
    }

    bool capture_ok = eh::wayland::ext_capture_toplevel_to_png(
        wl.display(), wl.ext_image_copy_capture_manager(),
        wl.ext_foreign_toplevel_image_capture_source_manager(),
        found->handle, wl.shm(), cursor, out_path);

    if (!capture_ok) std::cerr << "Toplevel capture failed.\n";
    if (!capture_ok) return 1;
    if (clipboard || preview) {
      spawn_clipboard_daemon(clipboard_daemon_binary, out_path);
    }
    return 0;
  }

  // Everything else is a full-screen or area capture.
  const bool area_mode = use_select || use_slurp || use_around_mouse || !geometry_arg.empty();
  int gx = 0;
  int gy = 0;
  int gw = 0;
  int gh = 0;

  if (area_mode && !use_select) {
    if (use_slurp) {
      const auto line = read_slurp_line();
      if (!line) {
        std::cerr << "slurp failed or was cancelled (is `slurp` installed?).\n";
        return 2;
      }
      geometry_arg = *line;
    }
    if (use_around_mouse) {
      // Only width x height come from the WxH argument.
      if (std::sscanf(around_mouse_geometry.c_str(), "%dx%d", &gw, &gh) != 2 || gw < 1 || gh < 1) {
        std::cerr << "Bad --around-mouse value (expected WxH, e.g. 400x300).\n";
        return 2;
      }
      // Resolve the cursor's global position.
      wl.refresh_logical_outputs();
      const auto bounds = wl.logical_output_bounds();
      if (!wl.has_layer_shell()) {
        std::cerr << "--around-mouse requires layer-shell (not available).\n";
        return 1;
      }
      auto cursor_pos = get_cursor_global_position(
          wl.display(), wl.compositor(), wl.shm(),
          wl.layer_shell(), wl.seat(), bounds);
      if (!cursor_pos) {
        std::cerr << "Failed to get cursor position.\n";
        return 1;
      }
      gx = cursor_pos->first - gw / 2;
      gy = cursor_pos->second - gh / 2;
    } else if (!parse_geometry_wh(geometry_arg, &gw, &gh, &gx, &gy)) {
      std::cerr << "Bad --geometry / slurp value (expected WxH+X+Y, e.g. 400x300+10+20).\n";
      return 2;
    }
  }

  wl_output* out = nullptr;
  if (use_around_mouse) {
    // The output under the cursor is the one we crop from.
    const auto bounds = wl.logical_output_bounds();
    for (const auto& b : bounds) {
      if (gx + gw > b.global_x && gx < b.global_x + b.width &&
          gy + gh > b.global_y && gy < b.global_y + b.height) {
        out = b.output;
        break;
      }
    }
    if (!out) {
      std::cerr << "Cursor position is not within any output.\n";
      return 1;
    }
  } else if (!output_name.empty()) {
    out = wl.output_by_name(output_name);
  } else {
    // Default: capture the output under the mouse.
    const auto bounds = wl.logical_output_bounds();
    if (wl.has_layer_shell()) {
      auto cursor_pos = get_cursor_global_position(
          wl.display(), wl.compositor(), wl.shm(),
          wl.layer_shell(), wl.seat(), bounds);
      if (cursor_pos) {
        for (const auto& b : bounds) {
          if (cursor_pos->first >= b.global_x &&
              cursor_pos->first < b.global_x + b.width &&
              cursor_pos->second >= b.global_y &&
              cursor_pos->second < b.global_y + b.height) {
            out = b.output;
            break;
          }
        }
      }
    }
    if (!out) {
      out = wl.pick_largest_logical_output().output;
    }
  }
  if (!out) {
    std::cerr << "No matching wl_output";
    if (!output_name.empty()) std::cerr << " (--output " << output_name << ")";
    std::cerr << ".\n";
    return 1;
  }

  bool capture_ok = false;
  auto* wlr_mgr = wl.screencopy_manager();
  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* ext_src_mgr = wl.ext_output_image_capture_source_manager();
  const bool have_ext = ext_mgr && ext_src_mgr;

  if (use_select) {
    if (!wl.has_layer_shell()) {
      std::cerr << "Interactive selection requires layer-shell (not available).\n";
      return 1;
    }

    // Scratch file for the full-output capture.
    char tmp_pattern[] = "/tmp/eh-scratch-XXXXXX";
    const int tmp_fd = mkstemp(tmp_pattern);
    if (tmp_fd < 0) {
      std::cerr << "Failed to create temp file for full capture.\n";
      return 1;
    }
    std::string tmp_path(tmp_pattern);
    close(tmp_fd);

    // Grab the full output with whichever capture backend is present.
    bool full_ok = false;
    if (have_ext) {
      full_ok = eh::wayland::ext_capture_output_to_png(
          wl.display(), ext_mgr, ext_src_mgr, wl.shm(), out,
          cursor, wl.color_manager(), tmp_path);
    } else if (wlr_mgr) {
      full_ok = eh::wayland::screencopy_output_to_png(
          wl.display(), wlr_mgr, wl.shm(), out, cursor, tmp_path);
    }
    if (!full_ok) {
      std::cerr << "Full output capture for interactive selection failed.\n";
      unlink(tmp_path.c_str());
      return 1;
    }

    // Let the user pick a region over the captured output.
    const auto bounds = wl.logical_output_bounds();
    auto geo = interactive_region_select(
        wl.display(), wl.compositor(), wl.shm(),
        wl.layer_shell(), wl.seat(),
        bounds, out, tmp_path);
    if (!geo) {
      unlink(tmp_path.c_str());
      return 2;
    }

    if (!parse_geometry_wh(*geo, &gw, &gh, &gx, &gy)) {
      std::cerr << "Bad geometry from region select.\n";
      unlink(tmp_path.c_str());
      return 1;
    }

    const int out_ox = get_output_global_x(bounds, out);
    const int out_oy = get_output_global_y(bounds, out);
    const int local_x = gx - out_ox;
    const int local_y = gy - out_oy;

    capture_ok = crop_png_file(tmp_path, out_path, local_x, local_y, gw, gh);
    unlink(tmp_path.c_str());
    if (!capture_ok) std::cerr << "Crop of captured output failed.\n";
  } else if (area_mode) {
    if (have_ext) {
      // Capture the full output via ext, then crop it down with stb_image.
      const auto bounds = wl.logical_output_bounds();
      const int out_ox = get_output_global_x(bounds, out);
      const int out_oy = get_output_global_y(bounds, out);
      const int local_x = gx - out_ox;
      const int local_y = gy - out_oy;

      // Scratch file to hold the full-output frame while we crop.
      char tmp_pattern[] = "/tmp/eh-scratch-XXXXXX";
      const int tmp_fd = mkstemp(tmp_pattern);
      if (tmp_fd < 0) {
        std::cerr << "Failed to create temp file for intermediate capture.\n";
        return 1;
      }
      std::string tmp_path(tmp_pattern);
      close(tmp_fd);

      bool full_ok = eh::wayland::ext_capture_output_to_png(
          wl.display(), ext_mgr, ext_src_mgr, wl.shm(), out, cursor, wl.color_manager(), tmp_path);
      if (!full_ok) {
        std::cerr << "Intermediate full-output capture failed.\n";
        unlink(tmp_path.c_str());
        return 1;
      }

      capture_ok = crop_png_file(tmp_path, out_path, local_x, local_y, gw, gh);
      unlink(tmp_path.c_str());
      if (!capture_ok) std::cerr << "Crop of captured output failed.\n";
    } else if (wlr_mgr) {
      capture_ok = eh::wayland::screencopy_output_region_to_png(wl.display(), wlr_mgr, wl.shm(), out,
                                                                  gx, gy, gw, gh, cursor, out_path);
      if (!capture_ok) std::cerr << "Region capture via zwlr_screencopy failed.\n";
    } else {
      std::cerr << "No screenshot backend available for area capture.\n";
      return 1;
    }
  } else if (wlr_mgr) {
    capture_ok = eh::wayland::screencopy_output_to_png(wl.display(), wlr_mgr, wl.shm(), out, cursor, out_path);
    if (!capture_ok) std::cerr << "Full-output capture via zwlr_screencopy failed.\n";
  } else if (have_ext) {
    capture_ok = eh::wayland::ext_capture_output_to_png(wl.display(), ext_mgr, ext_src_mgr, wl.shm(), out, cursor,
                                                         wl.color_manager(), out_path);
    if (!capture_ok) std::cerr << "Full-output capture via ext-image-copy-capture failed.\n";
  } else {
    std::cerr << "No screenshot backend available.\n";
    return 1;
  }

  if (!capture_ok) return 1;
  if (clipboard || preview) {
    spawn_clipboard_daemon(clipboard_daemon_binary, out_path);
  }
  return 0;
}

}
