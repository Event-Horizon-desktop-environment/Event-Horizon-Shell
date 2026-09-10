#include "screenshot/app.hpp"
#include "screenshot/capture/capture.hpp"
#include "screenshot/wayland_seat.hpp"
#include "screenshot/shm_buffer.hpp"
#include "core/screenshot/clipboard.hpp"
#include "core/screenshot/color_info.hpp"
#include "core/screenshot/logging.hpp"
#include "core/screenshot/foreign_toplevels.hpp"
#include "core/screenshot/wlr_foreign_toplevels.hpp"
#include <wayland/core/connection.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace hpv::sc {

int run_screenshot_capture(const CaptureOptions& opts) {
    WaylandConnection wl;
    if (!wl.connect()) {
        std::cerr << "Failed to connect to Wayland display\n";
        return 1;
    }
    wl_display_roundtrip(wl.display());

    ClipboardService clipboard;
    if (opts.copy) {
        if (auto* ext = wl.ext_data_control_manager()) {
            clipboard.bind_ext(ext, wl.seat(), wl.display());
        } else if (auto* wlr = wl.wlr_data_control_manager()) {
            clipboard.bind_wlr(wlr, wl.seat(), wl.display());
        }
    }

    if (opts.list_windows) {
        wl_display_roundtrip(wl.display());
        wl_display_roundtrip(wl.display());
        wl_display_roundtrip(wl.display());

        if (wl.has_ext_foreign_toplevel_list()) {
            const auto& list = wl.ext_foreign_toplevels().list();
            if (list.empty()) {
                std::cout << "No windows detected.\n";
            } else {
                for (size_t i = 0; i < list.size(); ++i) {
                    const auto& tl = list[i];
                    std::cout << i << ": app_id='" << tl.appId << "' title='" << tl.title << "'"
                              << " identifier='" << tl.identifier << "'\n";
                }
            }
        } else if (wl.has_wlr_foreign_toplevel_manager()) {
            const auto& list = wl.wlr_foreign_toplevels().list();
            if (list.empty()) {
                std::cout << "No windows detected.\n";
            } else {
                for (size_t i = 0; i < list.size(); ++i) {
                    const auto& tl = list[i];
                    std::cout << i << ": app_id='" << tl.appId << "' title='" << tl.title << "'"
                              << " activated=" << tl.activated << "\n";
                }
            }
        } else {
            std::cerr << "No toplevel list available from compositor.\n";
        }
        return 0;
    }

    std::string out_path = opts.output_path.empty()
        ? "/tmp/horizon-screenshot.png"
        : opts.output_path;

    bool ok = false;
    HdrData hdr;

    switch (opts.mode) {
    case CaptureOptions::Select: {
        wl.refresh_logical_outputs();
        auto bounds = wl.logical_output_bounds();
        ok = capture_selection_interactive(wl, bounds, out_path);
        break;
    }
    case CaptureOptions::Focused:
        ok = capture_focused_window(wl, out_path);
        break;
    case CaptureOptions::Screen:
        if (!opts.output_name.empty()) {
            wl.refresh_logical_outputs();
            auto outputs = list_outputs(wl);
            wl_output* target = nullptr;
            for (const auto& o : outputs) {
                if (o.name == opts.output_name) { target = o.output; break; }
            }
            if (!target) {
                std::cerr << "Output not found: " << opts.output_name << "\n";
                return 1;
            }
            ok = capture_output(wl, target, out_path, opts.capture_hdr ? &hdr : nullptr);
        } else {
            ok = capture_all_screens(wl, out_path, opts.capture_hdr ? &hdr : nullptr);
        }
        break;
    case CaptureOptions::Window: {
        wl_display_roundtrip(wl.display());
        if (!wl.has_ext_foreign_toplevel_list() && !wl.has_wlr_foreign_toplevel_manager()) {
            std::cerr << "No toplevel list available from compositor.\n";
            return 1;
        }
        wl_display_roundtrip(wl.display());
        wl_display_roundtrip(wl.display());

        if (opts.window_selector.empty()) {
            std::cerr << "Use --window <index>, --window <title>, or --window <app_id>.\n";
            std::cerr << "Use --list-windows to see available windows.\n";
            return 1;
        }

        ext_foreign_toplevel_handle_v1* handle = nullptr;
        bool found = false;

        if (wl.has_ext_foreign_toplevel_list()) {
            const auto& toplevels = wl.ext_foreign_toplevels().list();
            char* end = nullptr;
            long idx = std::strtol(opts.window_selector.c_str(), &end, 10);
            if (*end == '\0' && idx >= 0 && static_cast<size_t>(idx) < toplevels.size()) {
                handle = toplevels[idx].handle;
                found = true;
            } else {
                for (size_t i = 0; i < toplevels.size(); ++i) {
                    const auto& tl = toplevels[i];
                    if (tl.appId.find(opts.window_selector) != std::string::npos ||
                        tl.title.find(opts.window_selector) != std::string::npos) {
                        handle = tl.handle;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found && wl.has_wlr_foreign_toplevel_manager()) {
            const auto& toplevels = wl.wlr_foreign_toplevels().list();
            char* end = nullptr;
            long idx = std::strtol(opts.window_selector.c_str(), &end, 10);
            if (*end == '\0' && idx >= 0 && static_cast<size_t>(idx) < toplevels.size()) {
                handle = nullptr;
                found = true;
            } else {
                for (size_t i = 0; i < toplevels.size(); ++i) {
                    const auto& tl = toplevels[i];
                    if (tl.appId.find(opts.window_selector) != std::string::npos ||
                        tl.title.find(opts.window_selector) != std::string::npos) {
                        handle = nullptr;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            std::cerr << "No window matching '" << opts.window_selector << "' found.\n";
            return 1;
        }

        ok = capture_window_by_handle(wl, handle, out_path);
        break;
    }
    }

    if (!ok) {
        std::cerr << "Capture failed\n";
        return 1;
    }

    wl_display_roundtrip(wl.display());

    if (opts.copy && clipboard.is_available()) {
        FILE* f = fopen(out_path.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            rewind(f);
            std::string png_data(static_cast<std::string::size_type>(fsize), '\0');
            size_t nread = fread(png_data.data(), 1, static_cast<size_t>(fsize), f);
            fclose(f);
            if (static_cast<long>(nread) == fsize) {
                clipboard.copy_data("image/png", std::move(png_data));
                if (opts.output_path.empty()) unlink(out_path.c_str());
            }
        }
    }

    if (opts.output_path.empty() && !opts.copy) {
        std::cout << "Saved to " << out_path << "\n";
    }

    return 0;
}

}
