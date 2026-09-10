#include "core/viewer/app.hpp"
#include "screenshot/app.hpp"
#include "screenshot/capture/capture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

static void print_screenshot_help(const char* prog) {
    std::printf(
        "Screenshot modes:\n"
        "  -s, --select             Interactive region selection\n"
        "  -f, --focused            Capture focused window\n"
        "  -w, --window [SELECTOR]  Capture a window by index, title, or app_id\n"
        "  -o, --output <name>      Capture a specific output by name\n"
        "  -a, --all                Capture all screens\n"
        "  -c, --copy               Copy to clipboard instead of saving\n"
        "  -O, --output-file <path> Save to specific path\n"
        "  --list-windows           List available windows and exit\n"
        "  --list-outputs           List available outputs and exit\n"
        "  --hdr                    Capture HDR data\n"
        "  -h, --help               Show this help and exit\n"
        "\nUsage: %s --screenshot [OPTIONS]\n", prog);
}

static int run_screenshot(int argc, char** argv) {
    hpv::sc::CaptureOptions opts;
    opts.mode = hpv::sc::CaptureOptions::Screen;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_screenshot_help(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--list-outputs") == 0) {
            hpv::WaylandConnection wl;
            if (!wl.connect()) {
                std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
                return 1;
            }
            wl_display_roundtrip(wl.display());
            wl.refresh_logical_outputs();
            wl_display_roundtrip(wl.display());
            auto outputs = hpv::sc::list_outputs(wl);
            for (const auto& o : outputs) {
                std::printf("%s: %dx%d @(%d,%d)%s\n",
                    o.name.c_str(), o.width, o.height,
                    o.global_x, o.global_y,
                    o.is_hdr ? " HDR" : "");
            }
            wl.disconnect();
            return 0;
        }
        if (std::strcmp(argv[i], "-s") == 0 || std::strcmp(argv[i], "--select") == 0) {
            opts.mode = hpv::sc::CaptureOptions::Select;
        } else if (std::strcmp(argv[i], "-f") == 0 || std::strcmp(argv[i], "--focused") == 0) {
            opts.mode = hpv::sc::CaptureOptions::Focused;
        } else if (std::strcmp(argv[i], "-a") == 0 || std::strcmp(argv[i], "--all") == 0) {
            opts.mode = hpv::sc::CaptureOptions::Screen;
        } else if (std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0) {
            opts.mode = hpv::sc::CaptureOptions::Screen;
            if (i + 1 < argc) opts.output_name = argv[++i];
            else { std::cerr << "--output requires a name argument\n"; return 1; }
        } else if (std::strcmp(argv[i], "-O") == 0 || std::strcmp(argv[i], "--output-file") == 0) {
            if (i + 1 < argc) opts.output_path = argv[++i];
            else { std::cerr << "--output-file requires a path argument\n"; return 1; }
        } else if (std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--copy") == 0) {
            opts.copy = true;
        } else if (std::strcmp(argv[i], "-w") == 0 || std::strcmp(argv[i], "--window") == 0) {
            opts.mode = hpv::sc::CaptureOptions::Window;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.window_selector = argv[++i];
            }
        } else if (std::strcmp(argv[i], "--list-windows") == 0) {
            opts.list_windows = true;
        } else if (std::strcmp(argv[i], "--hdr") == 0) {
            opts.capture_hdr = true;
        } else {
            std::cerr << "Unknown screenshot option: " << argv[i] << "\n";
            print_screenshot_help(argv[0]);
            return 1;
        }
    }

    return hpv::sc::run_screenshot_capture(opts);
}

int main(int argc, char** argv) {
    // Check for screenshot mode
    if (argc > 1 && std::strcmp(argv[1], "--screenshot") == 0) {
        return run_screenshot(argc, argv);
    }

    hpv::App app(argc, argv);

    if (!app.init()) {
        std::cerr << "Failed to initialize Horizon Photo Viewer\n";
        return 1;
    }

    return app.run();
}
