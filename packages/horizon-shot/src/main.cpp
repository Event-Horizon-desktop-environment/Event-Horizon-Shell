#include "screenshot/app.hpp"
#include "core/logging.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>

static void print_help(const char* prog)
{
  std::printf(
    "Usage: %s [OPTIONS]\n"
    "\n"
    "Capture modes (default: interactive GUI):\n"
    "  -s, --select             Interactive region selection\n"
    "  -f, --focused            Capture focused window\n"
    "  -w, --window [SELECTOR]  Capture a window by index, title, or app_id\n"
    "  -o, --output <name>      Capture a specific output by name\n"
    "  -a, --all                Capture all screens (default in GUI)\n"
    "\n"
    "Frame options:\n"
    "  --cursor             Include cursor in capture\n"
    "  --border             Show window border\n"
    "  --no-shadow          Disable drop shadow\n"
    "  --no-frame           Hide decoration frame\n"
    "  --inset <px>         Padding around captured image (default: 32)\n"
    "  --corner-radius <px> Corner radius (default: 24)\n"
    "\n"
    "Output:\n"
    "  -c, --copy           Copy to clipboard instead of saving\n"
    "  -O, --output-file <path>  Save to specific path\n"
    "  --hdr                Capture HDR data (requires JXL output or preview)\n"
    "\n"
    "Utility:\n"
    "  --list-outputs       List available outputs and exit\n"
    "  --list-windows       List available windows and exit\n"
    "  -h, --help           Show this help and exit\n",
    prog);
}

int main(int argc, char* argv[])
{
  hs::core::init_logging();
  HS_LOG("main: argc=%d argv[0]=%s", argc, argc > 0 ? argv[0] : "?");

  hs::screenshot::AppOptions opts;

  for (int i = 1; i < argc; ++i) {
    HS_LOG("main: arg[%d]=%s", i, argv[i]);
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      print_help(argv[0]);
      HS_LOG("main: exiting after --help");
      return 0;
    }
    if (std::strcmp(argv[i], "--list-outputs") == 0) {
      hs::core::WaylandConnection wl;
      if (!wl.connect()) {
        std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
        return 1;
      }
      wl_display_roundtrip(wl.display());
      wl.refresh_logical_outputs();
      wl_display_roundtrip(wl.display());
      auto outputs = hs::screenshot::list_outputs(wl);
      for (const auto& o : outputs) {
        std::printf("%s: %dx%d @(%d,%d)%s\n",
          o.name.c_str(), o.width, o.height,
          o.global_x, o.global_y,
          o.is_hdr ? " HDR" : "");
      }
      wl.disconnect();
      HS_LOG("main: exiting after --list-outputs");
      return 0;
    }
    if (std::strcmp(argv[i], "-s") == 0 || std::strcmp(argv[i], "--select") == 0) {
      opts.mode = hs::screenshot::AppOptions::Select;
    } else if (std::strcmp(argv[i], "-f") == 0 || std::strcmp(argv[i], "--focused") == 0) {
      opts.mode = hs::screenshot::AppOptions::Focused;
    } else if (std::strcmp(argv[i], "-a") == 0 || std::strcmp(argv[i], "--all") == 0) {
      opts.mode = hs::screenshot::AppOptions::Screen;
    } else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0) {
      opts.mode = hs::screenshot::AppOptions::Screen;
      if (i + 1 < argc) opts.output_name = argv[++i];
      else { std::cerr << "--output requires a name argument\n"; HS_LOG("main: --output missing name"); return 1; }
    } else if (std::strcmp(argv[i], "-O") == 0 || std::strcmp(argv[i], "--output-file") == 0) {
      if (i + 1 < argc) opts.output_path = argv[++i];
      else { std::cerr << "--output-file requires a path argument\n"; HS_LOG("main: --output-file missing path"); return 1; }
    } else if (std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--copy") == 0) {
      opts.copy = true;
    } else if (std::strcmp(argv[i], "--cursor") == 0) {
      opts.frame.includeCursor = true;
    } else if (std::strcmp(argv[i], "--border") == 0) {
      opts.frame.showBorder = true;
    } else if (std::strcmp(argv[i], "--no-shadow") == 0) {
      opts.frame.shadow = 0;
    } else if (std::strcmp(argv[i], "--no-frame") == 0) {
      opts.frame.hideChrome = true;
    } else if (std::strcmp(argv[i], "-w") == 0 || std::strcmp(argv[i], "--window") == 0) {
      opts.mode = hs::screenshot::AppOptions::Window;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        opts.window_selector = argv[++i];
      }
    } else if (std::strcmp(argv[i], "--list-windows") == 0) {
      opts.list_windows = true;
    } else if (std::strcmp(argv[i], "--hdr") == 0) {
      opts.capture_hdr = true;
    } else if (std::strcmp(argv[i], "--inset") == 0) {
      if (i + 1 < argc) opts.frame.inset = std::atoi(argv[++i]);
      else { std::cerr << "--inset requires a value\n"; HS_LOG("main: --inset missing value"); return 1; }
    } else if (std::strcmp(argv[i], "--corner-radius") == 0) {
      if (i + 1 < argc) opts.frame.cornerRadius = std::atoi(argv[++i]);
      else { std::cerr << "--corner-radius requires a value\n"; HS_LOG("main: --corner-radius missing value"); return 1; }
    } else {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      HS_LOG("main: unknown option '%s'", argv[i]);
      print_help(argv[0]);
      return 1;
    }
  }

  HS_LOG("main: calling run_screenshot_cli with mode=%d", (int)opts.mode);
  int ret = hs::screenshot::run_screenshot_cli(opts);
  HS_LOG("main: run_screenshot_cli returned %d", ret);
  return ret;
}
