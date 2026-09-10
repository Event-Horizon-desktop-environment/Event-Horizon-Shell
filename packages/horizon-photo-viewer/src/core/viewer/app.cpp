#include "core/viewer/app.hpp"
#include "common/math/math.hpp"
#include "decode/common/svg_doc.hpp"
#include "screenshot/app.hpp"
#include "core/screenshot/logging.hpp"
#ifdef HAVE_LIBCURL
#include "features/upload/upload.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cairo.h>

#include "psimpl/psimpl.h"
#include "tinyspline/tinysplinecxx.h"
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

using namespace hpv::math;
namespace fs = std::filesystem;

// Listener trampolines
namespace {

const xdg_surface_listener kXdgSurfaceListener = {
    .configure = [](void*, xdg_surface* surf, uint32_t serial) {
        xdg_surface_ack_configure(surf, serial);
    },
};

const xdg_toplevel_listener kXdgToplevelListener = {
    .configure = [](void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
        if (w > 0 && h > 0) {
            static_cast<hpv::App*>(data)->set_window_size(w, h);
        }
    },
    .close = [](void* data, xdg_toplevel*) {
        static_cast<hpv::App*>(data)->on_close();
    },
    .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
};

}

namespace hpv {

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

App::App(int argc, char** argv) {
#ifdef HAVE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    config_ = Config::load();
    bg_alpha_ = config_.bg_alpha;
    if (argc > 1) {
        current_path_ = argv[1];
    }
}

App::~App() {
#ifdef HAVE_LIBCURL
    if (upload_thread_.joinable()) {
        upload_thread_.detach();
    }
    upload_state_.reset();
    curl_global_cleanup();
#endif
    if (data_device_) wl_data_device_release(data_device_);
    if (xdg_toplevel_) xdg_toplevel_destroy(xdg_toplevel_);
    if (xdg_surface_) xdg_surface_destroy(xdg_surface_);
    if (surface_) wl_surface_destroy(surface_);
    destroy_cached_strip();
    if (svg_vector_cache_) { cairo_surface_destroy(svg_vector_cache_); svg_vector_cache_ = nullptr; }
}

bool App::init() {
    if (!init_wayland()) return false;
    if (!create_window()) return false;
    init_font();

    // Load Material Symbols icons for the toolbar
    {
        const char* icon_paths[] = {
            "assets/fonts/MF/MaterialSymbolsRounded.ttf",
            "../assets/fonts/MF/MaterialSymbolsRounded.ttf",
#ifdef ICON_FONT_PATH_SYSTEM
            ICON_FONT_PATH_SYSTEM,
#endif
            nullptr,
        };
        bool icons_loaded = false;
        for (const char** ip = icon_paths; *ip; ip++) {
#ifdef CROP_SVG_PATH_SYSTEM
            if (overlay_.init_icons(*ip, CROP_SVG_PATH_SYSTEM,
                                   FLIP_SVG_PATH_SYSTEM)
                && overlay_.crop_icon_loaded()) {
                icons_loaded = true;
                break;
            }
#endif
            const char* base_paths[] = { "assets/", "../assets/", nullptr };
            for (const char** bp = base_paths; *bp; bp++) {
                std::string base = *bp;
                std::string crop_path = base + "crop.svg";
                std::string flip_path = base + "flip.svg";
                if (overlay_.init_icons(*ip, crop_path.c_str(), flip_path.c_str())
                    && overlay_.crop_icon_loaded()) {
                    icons_loaded = true;
                    break;
                }
            }
            if (icons_loaded) break;
        }
        if (!icons_loaded)
            std::cerr << "overlay: icons (font + svg) not found\n";

        // Load screenshot SVG icons
        {
            const char* svg_paths[] = { "assets/screen.svg", "assets/window.svg",
                                        "assets/focused.svg", "assets/selection.svg",
                                        "assets/copy.svg" };
            const char* alt_paths[] = { "../assets/screen.svg", "../assets/window.svg",
                                        "../assets/focused.svg", "../assets/selection.svg",
                                        "../assets/copy.svg" };
    #ifdef SCREEN_SVG_PATH_SYSTEM
            const char* sys_paths[] = { SCREEN_SVG_PATH_SYSTEM, WINDOW_SVG_PATH_SYSTEM,
                                        FOCUSED_SVG_PATH_SYSTEM, SELECTION_SVG_PATH_SYSTEM,
                                        COPY_SVG_PATH_SYSTEM };
    #endif
            bool svg_ok = false;
            for (auto* base : { svg_paths, alt_paths
    #ifdef SCREEN_SVG_PATH_SYSTEM
                    , sys_paths
    #endif
                }) {
                if (overlay_.init_screenshot_icons(base[0], base[1], base[2], base[3], base[4])) {
                    svg_ok = true;
                    break;
                }
            }
            if (!svg_ok)
                std::cerr << "overlay: screenshot SVG icons not found\n";
        }

        // Load panel SVG icon
        {
            const char* panel_paths[] = { "assets/panel.svg", "../assets/panel.svg" };
    #ifdef COPY_SVG_PATH_SYSTEM
            // Derive panel path from system copy SVG path
            std::string panel_sys;
            {
                std::string cp = COPY_SVG_PATH_SYSTEM;
                auto pos = cp.rfind('/');
                if (pos != std::string::npos) {
                    panel_sys = cp.substr(0, pos + 1) + "panel.svg";
                }
            }
            const char* panel_sys_path = panel_sys.empty() ? nullptr : panel_sys.c_str();
    #endif
            bool panel_ok = false;
            for (const char* p : panel_paths) {
                if (overlay_.init_panel_icon(p)) {
                    panel_ok = true;
                    break;
                }
            }
    #ifdef COPY_SVG_PATH_SYSTEM
            if (!panel_ok && panel_sys_path) {
                panel_ok = overlay_.init_panel_icon(panel_sys_path);
            }
    #endif
            if (!panel_ok)
                std::cerr << "overlay: panel SVG icon not found\n";
        }

        // Load upload SVG icon
        {
            const char* upload_paths[] = { "assets/upload.svg", "../assets/upload.svg" };
    #ifdef UPLOAD_SVG_PATH_SYSTEM
            const char* upload_sys_path = UPLOAD_SVG_PATH_SYSTEM;
    #endif
            bool upload_ok = false;
            for (const char* p : upload_paths) {
                if (overlay_.init_upload_icon(p)) {
                    upload_ok = true;
                    break;
                }
            }
    #ifdef UPLOAD_SVG_PATH_SYSTEM
            if (!upload_ok && upload_sys_path) {
                upload_ok = overlay_.init_upload_icon(upload_sys_path);
            }
    #endif
            if (!upload_ok)
                std::cerr << "overlay: upload SVG icon not found\n";
        }
    }

    // Register decoders up front so that if set_window_size triggers a
    // render during the roundtrips below, load_image can actually decode.
    decoders_.register_decoder(std::make_unique<WuffsDecoder>());
#ifdef HAVE_LIBJPEG
    decoders_.register_decoder(std::make_unique<JpegDecoder>());
#endif
#ifdef HAVE_LIBWEBP
    decoders_.register_decoder(std::make_unique<WebPDecoder>());
#endif
#ifdef HAVE_LIBJXL
    decoders_.register_decoder(std::make_unique<JxlDecoder>());
#endif
    decoders_.register_decoder(std::make_unique<HeifDecoder>());
    decoders_.register_decoder(std::make_unique<AvifDecoder>());
    decoders_.register_decoder(std::make_unique<RawDecoder>());
    decoders_.register_decoder(std::make_unique<SvgDecoder>());
    decoders_.register_decoder(std::make_unique<StbDecoder>());

    // Init portal file dialog
    portal_dialog_.init();

    // First commit tells the compositor to configure the surface.
    // Without this, the roundtrips below get no configure events and
    // window_width_/height_ stay at the default (1280x720) — wrong.
    wl_surface_commit(surface_);

    // Roundtrips to process initial configure events.
    // xdg_surface_configure gives the serial (acked in the listener).
    // xdg_toplevel_configure gives the actual window size.
    wl_display_roundtrip(conn_.display());
    wl_display_roundtrip(conn_.display());

    // Create both buffers at the configured size
    for (auto& buf : bufs_) {
        buf.init(conn_.shm(), window_width_, window_height_);
        buf.set_release_callback([this]() { on_shm_release(); });
    }

    // Open initial file if provided
    if (!current_path_.empty()) {
        open_file(current_path_);
    }

    // Render the first frame and commit so the compositor maps the surface
    render();

    return true;
}

bool App::init_wayland() {
    if (!conn_.connect()) {
        std::cerr << "Failed to connect to Wayland display\n";
        return false;
    }

    if (!conn_.compositor()) {
        std::cerr << "Compositor not available\n";
        return false;
    }

    // Init seat for keyboard + pointer input
    if (conn_.seat()) {
        seat_.init(conn_.seat(), conn_.display());
        seat_.set_key_callback([this](const KeyEvent& ev) {
            on_key(ev);
        });
        seat_.set_pointer_callback([this](const PointerEvent& ev) {
            on_pointer(ev);
        });
        seat_.set_scroll_callback([this](const ScrollEvent& ev) {
            on_scroll(ev);
        });
        seat_.set_motion_callback([this](int x, int y) {
            on_motion(x, y);
        });
    }

    // Init data device for drag-and-drop
    if (conn_.data_device_manager() && conn_.seat()) {
        data_device_ = wl_data_device_manager_get_data_device(
            conn_.data_device_manager(), conn_.seat());
        static constexpr wl_data_device_listener listener = {
            .data_offer = handle_data_offer,
            .enter = handle_data_enter,
            .leave = handle_data_leave,
            .motion = handle_data_motion,
            .drop = handle_data_drop,
            .selection = handle_data_selection,
        };
        wl_data_device_add_listener(data_device_, &listener, this);
    }

    return true;
}

bool App::create_window() {
    surface_ = wl_compositor_create_surface(conn_.compositor());
    if (!surface_) {
        std::cerr << "Failed to create wl_surface\n";
        return false;
    }

    xdg_surface_ = xdg_wm_base_get_xdg_surface(conn_.xdg_base(), surface_);
    if (!xdg_surface_) {
        std::cerr << "Failed to create xdg_surface\n";
        return false;
    }
    xdg_surface_add_listener(xdg_surface_, &kXdgSurfaceListener, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    if (!xdg_toplevel_) {
        std::cerr << "Failed to create xdg_toplevel\n";
        return false;
    }
    xdg_toplevel_add_listener(xdg_toplevel_, &kXdgToplevelListener, this);

    xdg_toplevel_set_title(xdg_toplevel_, "Horizon Photo Viewer");
    xdg_toplevel_set_app_id(xdg_toplevel_, "com.horizon.photo-viewer");
    xdg_toplevel_set_min_size(xdg_toplevel_, 320, 200);

    // Init surface extensions (viewporter, fractional-scale, tearing)
    surface_extensions_.init(conn_, surface_);

    return true;
}

void App::init_font() {
    const char* font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        nullptr,
    };
    for (const char** fp = font_paths; *fp; fp++) {
        if (text_renderer_.init(*fp, 13.0f)) return;
    }
    std::cerr << "text: no font found\n";
}

void App::render() {
    if (loading_) return;
    if (decoded_image_.rgba.empty() && !current_path_.empty()) {
        load_image(current_path_);
        return;
    }
    present();
}

void App::present() {
    pending_redraw_ = false;

    int bi = paint_buf_;
    int win_w = std::max(1, window_width_);
    int win_h = std::max(1, window_height_);

    // If the current buffer is busy (compositor still reading it),
    // try the other buffer instead. If both are busy, process any
    // already-queued release events. If still both busy, defer redraw
    // rather than drawing into a buffer the compositor is still using,
    // which causes visible flickering.
    if (bufs_[bi].busy()) {
        int other = 1 - bi;
        if (!bufs_[other].busy()) {
            bi = other;
        } else {
            wl_display_dispatch_pending(conn_.display());
            if (!bufs_[bi].busy()) {
                // freed by pending release event
            } else if (!bufs_[other].busy()) {
                bi = other;
            } else {
                // Both still busy — defer redraw until a buffer is released
                pending_redraw_ = true;
                return;
            }
        }
    }

    // Ensure buffer at correct size (only if buffer already exists)
    if (bufs_[bi].buffer() && (win_w != bufs_[bi].width() || win_h != bufs_[bi].height())) {
        bufs_[bi].init(conn_.shm(), win_w, win_h);
    }

    if (!bufs_[bi].buffer()) return;

    // Wrap the SHM buffer in a Cairo surface
    cairo_surface_t* cs = cairo_image_surface_create_for_data(
        bufs_[bi].data(),
        CAIRO_FORMAT_ARGB32,
        win_w, win_h,
        bufs_[bi].stride()
    );
    cairo_t* cr = cairo_create(cs);

    // --- Apply theme before drawing chrome ---
    hpv::m3::apply_theme(config_.theme == "light");

    // --- Background (use SOURCE to overwrite stale buffer content) ---
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, hpv::m3::surface_r, hpv::m3::surface_g,
                          hpv::m3::surface_b, bg_alpha_);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // --- Content area (shrunk when sidebar or thumbnail strip is visible) ---
    int content_w = win_w;
    int sidebar_w = 0;
    if (show_sidebar_) {
        sidebar_w = 320;
        content_w = win_w - sidebar_w;
    }
    bool hide_strip = fullscreen_ || slideshow_;
    bool strip_visible = show_thumbnails_ && (!hide_strip || show_thumbnails_hover_);
    int strip_h = (strip_visible && decoded_image_.width > 0 && dir_images_.size() > 1)
                      ? ThumbnailStrip::kHeight : 0;

    // --- Sidebar area background (painted before the image) ---
    if (show_sidebar_) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, hpv::m3::surface_container_r, hpv::m3::surface_container_g,
                              hpv::m3::surface_container_b, bg_alpha_);
        cairo_rectangle(cr, content_w, 0, sidebar_w, win_h);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }

    // --- Opaque region: empty when bg_alpha < 1 so compositor respects alpha ---
    {
        wl_region* reg = wl_compositor_create_region(conn_.compositor());
        if (bg_alpha_ >= 1.0f) {
            wl_region_add(reg, 0, 0, win_w, win_h);
        }
        wl_surface_set_opaque_region(surface_, reg);
        wl_region_destroy(reg);
    }

    // --- Decoded image (constrained to content area) ---
    if (!decoded_image_.rgba.empty()) {
        float img_w = orig_img_w_ > 0 ? orig_img_w_ : (float)decoded_image_.width;
        float img_h = orig_img_h_ > 0 ? orig_img_h_ : (float)decoded_image_.height;

        // Swap dimensions for 90/270 rotation so fit calculation uses the correct aspect
        float layout_w = (rotation_ == 90 || rotation_ == 270) ? img_h : img_w;
        float layout_h = (rotation_ == 90 || rotation_ == 270) ? img_w : img_h;

        int avail_h = win_h - (show_toolbar_ ? Overlay::kToolbarHeight : 0) - strip_h;
        float fit_scale = std::min((float)content_w / layout_w, (float)avail_h / layout_h) * zoom_;
        int draw_w = std::max(1, (int)(layout_w * fit_scale));
        int draw_h = std::max(1, (int)(layout_h * fit_scale));

        int offset_x = (content_w - draw_w) / 2 + (int)pan_x_;
        int offset_y = (show_toolbar_ ? Overlay::kToolbarHeight : 0) + (avail_h - draw_h) / 2 + (int)pan_y_;

        cairo_save(cr);
        cairo_translate(cr, offset_x, offset_y);

        // Rotation / flip around center of draw area
        cairo_translate(cr, draw_w / 2.0, draw_h / 2.0);
        cairo_scale(cr, flip_h_ ? -1.0 : 1.0, flip_v_ ? -1.0 : 1.0);
        cairo_rotate(cr, rotation_ * M_PI / 180.0);
        cairo_translate(cr, -draw_w / 2.0, -draw_h / 2.0);

        std::cerr << "[present] svg_doc=" << (void*)svg_doc_
                  << " cache=" << (void*)svg_vector_cache_
                  << " cache_dims=" << svg_vector_w_ << "x" << svg_vector_h_
                  << " draw=" << draw_w << "x" << draw_h
                  << " native=" << orig_img_w_ << "x" << orig_img_h_
                  << " rgba=" << decoded_image_.rgba.size()
                  << "\n";
        if (svg_doc_) {
            // --- SVG rendering: cache at max(draw, native), only rebuild when cache too small ---
            int nw = std::max(draw_w, (int)orig_img_w_);
            int nh = std::max(draw_h, (int)orig_img_h_);
            if (!svg_vector_cache_ || nw > svg_vector_w_ || nh > svg_vector_h_) {
                if (svg_vector_cache_) cairo_surface_destroy(svg_vector_cache_);
                nw = std::max(1, nw);
                nh = std::max(1, nh);
                svg_vector_cache_ = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, nw, nh);
                svg_vector_w_ = nw;
                svg_vector_h_ = nh;
                cairo_t* vcr = cairo_create(svg_vector_cache_);
                {
                    struct timespec tv0, tv1;
                    clock_gettime(CLOCK_MONOTONIC, &tv0);
                    svg_doc_render(vcr, svg_doc_, (float)nw, (float)nh);
                    clock_gettime(CLOCK_MONOTONIC, &tv1);
                    int64_t v_us = (tv1.tv_sec - tv0.tv_sec) * 1000000 +
                                   (tv1.tv_nsec - tv0.tv_nsec) / 1000;
                    std::cerr << "[svg] build: " << v_us << " us ("
                              << nw << "x" << nh << ")\n";
                }
                cairo_destroy(vcr);

                // Persist render to disk cache for instant full-quality recall
                if (nw >= 64 && nh >= 64 &&
                    ThumbCache::save_svg_surface(current_path_, svg_vector_cache_)) {
                    std::cerr << "[svg] disk cache saved: "
                              << nw << "x" << nh << "\n";
                }
            }

            // Display the cache scaled to fit the display rect (downscales when zoomed out)
            cairo_save(cr);
            cairo_scale(cr, (double)draw_w / svg_vector_w_,
                            (double)draw_h / svg_vector_h_);
            cairo_set_source_surface(cr, svg_vector_cache_, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_restore(cr);

            // Markup elements in image coords
            cairo_save(cr);
            cairo_scale(cr, (double)draw_w / orig_img_w_,
                            (double)draw_h / orig_img_h_);
            draw_markup_elements(cr);
            cairo_restore(cr);
        } else {
            cairo_scale(cr, (double)draw_w / decoded_image_.width,
                            (double)draw_h / decoded_image_.height);
            cairo_surface_t* img_surf = cairo_image_surface_create_for_data(
                decoded_image_.rgba.data(),
                CAIRO_FORMAT_ARGB32,
                decoded_image_.width, decoded_image_.height,
                decoded_image_.width * 4
            );
            cairo_set_source_surface(cr, img_surf, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_surface_destroy(img_surf);

            // Draw markup elements (in original image coords, within the same transform)
            draw_markup_elements(cr);
        }

        cairo_restore(cr);
    }

    // --- Overlay (toolbar only — info/placeholder drawn after thumbnail strip) ---
    OverlayState ov_state;
    ov_state.show_info = show_overlay_;
    ov_state.toolbar_visible = show_toolbar_;
    ov_state.fullscreen = fullscreen_;
    ov_state.slideshow = slideshow_;
    ov_state.show_settings = show_settings_;
    ov_state.show_sidebar = show_sidebar_;
    ov_state.crop_active = crop_active_;
    ov_state.markup_active = markup_active_;
    ov_state.bg_alpha = bg_alpha_;
    ov_state.zoom = zoom_;
    ov_state.image_width = decoded_image_.width;
    ov_state.image_height = decoded_image_.height;
    ov_state.image_index = dir_image_index_;
    ov_state.image_count = (int)dir_images_.size();
    ov_state.slideshow_interval_ms = config_.slideshow_interval_ms;
    ov_state.enable_color_management = config_.enable_color_management;
    ov_state.default_zoom = config_.default_zoom;
    ov_state.theme = config_.theme;
    ov_state.exif = exif_info_;
    if (decoded_image_.width <= 0) {
        ov_state.filename.clear();
    } else {
        ov_state.filename = fs::path(current_path_).filename().string();
    }

    if (show_toolbar_) {
        overlay_.render_toolbar(cr, win_w, win_h, toolbar_buttons_,
                                toolbar_hover_idx_, toolbar_press_idx_, bg_alpha_);
        // Fill in button actions after render_toolbar populates geometries
        for (auto& btn : toolbar_buttons_) {
            if (btn.label == "Open") { btn.action = [this]() {
                show_open_menu_ = !show_open_menu_;
                show_window_menu_ = false;
                show_screen_menu_ = false;
                show_upload_menu_ = false;
                if (show_open_menu_) {
                    show_menu_ = false;
                    show_open_recent_menu_ = false;
                }
                render();
            }; btn.tooltip = "Open file"; }
            else if (btn.label == "<") { btn.action = [this]() { prev_image(); }; btn.tooltip = "Previous image"; }
            else if (btn.label == ">") { btn.action = [this]() { next_image(); }; btn.tooltip = "Next image"; }
            else if (btn.label == "+") { btn.action = [this]() { zoom_in(); }; btn.tooltip = "Zoom in"; }
            else if (btn.label == "-") { btn.action = [this]() { zoom_out(); }; btn.tooltip = "Zoom out"; }
            else if (btn.label == "Fit") { btn.action = [this]() { zoom_fit(); }; btn.tooltip = "Fit to window"; }
            else if (btn.label == "Full") { btn.action = [this]() { toggle_fullscreen(); }; btn.tooltip = "Toggle fullscreen"; }
            else if (btn.label == "Play") { btn.action = [this]() { toggle_slideshow(); }; btn.tooltip = "Start slideshow"; }
            else if (btn.label == "Gear") { btn.action = [this]() { toggle_settings(); }; btn.tooltip = "Settings"; }
            else if (btn.label == "Info") {
                btn.action = [this]() { show_sidebar_ = !show_sidebar_; render(); };
                btn.tooltip = "Toggle sidebar";
            }
            else if (btn.label == "Crop") { btn.action = [this]() { toggle_crop(); }; btn.tooltip = "Crop image"; }
            else if (btn.label == "Draw") { btn.action = [this]() { toggle_markup(); }; btn.tooltip = "Draw on image"; }
            else if (btn.label == "RotR") { btn.action = [this]() { rotate_90_cw(); }; btn.tooltip = "Rotate 90\u00B0 CW"; }
            else if (btn.label == "RotL") { btn.action = [this]() { rotate_90_ccw(); }; btn.tooltip = "Rotate 90\u00B0 CCW"; }
            else if (btn.label == "Flip") { btn.action = [this]() { flip_horizontal(); }; btn.tooltip = "Flip horizontally"; }
            else if (btn.label == "Menu") { btn.action = [this]() {
                show_open_menu_ = false;
                show_open_recent_menu_ = false;
                toggle_menu();
            }; btn.tooltip = "Menu"; }
            else if (btn.label == "Panel") { btn.action = [this]() { toggle_screenshot_panel(); }; btn.tooltip = "Screenshot panel"; }
            else if (btn.label == "Screen") { btn.action = [this]() {
                show_screen_menu_ = !show_screen_menu_;
                show_window_menu_ = false;
                show_upload_menu_ = false;
                show_open_menu_ = false;
                show_open_recent_menu_ = false;
                if (show_screen_menu_) {
                    show_menu_ = false;
                    screenshot_panel_active_ = false;
                    refresh_screenshot_lists();
                }
                render();
            }; btn.tooltip = "Capture screens"; }
            else if (btn.label == "Window") { btn.action = [this]() {
                show_window_menu_ = !show_window_menu_;
                show_screen_menu_ = false;
                show_upload_menu_ = false;
                show_open_menu_ = false;
                show_open_recent_menu_ = false;
                if (show_window_menu_) {
                    show_menu_ = false;
                    screenshot_panel_active_ = false;
                    refresh_screenshot_lists();
                    if (!screenshot_icon_cache_inited_) {
                        screenshot_icon_cache_.set_icon_theme(hpv::sc::detect_system_icon_theme());
                        screenshot_icon_cache_inited_ = true;
                    }
                }
                render();
            }; btn.tooltip = "Capture a window"; }
            else if (btn.label == "Focused") { btn.action = [this]() { screenshot_focused(); }; btn.tooltip = "Capture focused window"; }
            else if (btn.label == "Selection") { btn.action = [this]() { screenshot_selection(); }; btn.tooltip = "Capture a selection"; }
            else if (btn.label == "Copy") { btn.action = [this]() { screenshot_copy(); }; btn.tooltip = "Copy screenshot to clipboard"; }
            else if (btn.label == "Upload") { btn.action = [this]() {
                show_upload_menu_ = !show_upload_menu_;
                show_screen_menu_ = false;
                show_window_menu_ = false;
                show_open_menu_ = false;
                show_open_recent_menu_ = false;
                if (show_upload_menu_) {
                    show_menu_ = false;
                }
                render();
            }; btn.tooltip = "Upload image"; }
        }
    }

    // --- Menu popup ---
    if (show_menu_) {
        std::vector<OverlayButton> menu_buttons;
        overlay_.render_menu_popup(cr, win_w, win_h, ov_state, menu_buttons);
        for (auto& btn : menu_buttons) {
            if (btn.label == "Save") btn.action = [this]() {
                show_menu_ = false;
                save_image();
            };
            else if (btn.label == "Save As") btn.action = [this]() {
                show_menu_ = false;
                save_as();
            };
            else if (btn.label == "Save As Copy") btn.action = [this]() {
                show_menu_ = false;
                save_as_copy();
            };
            else if (btn.label == "Upload\u2026") btn.action = [this]() {
                show_menu_ = false;
                upload_image();
            };
        }
        toolbar_buttons_.insert(toolbar_buttons_.end(),
                                menu_buttons.begin(), menu_buttons.end());
    }

    // --- Window capture dropdown ---
    if (show_window_menu_) {
        int btn_x = 0;
        for (auto& btn : toolbar_buttons_) {
            if (btn.label == "Window") { btn_x = btn.x; break; }
        }
        int item_h = 36;
        int max_items = std::min((int)screenshot_windows_.size(), 10);
        int icon_size = 20;
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        // Compute dynamic width: find the longest label
        int max_label_w = 0;
        for (int i = 0; i < max_items; i++) {
            auto& we = screenshot_windows_[i];
            const char* label = we.title.empty() ? we.appId.c_str() : we.title.c_str();
            cairo_text_extents_t te;
            cairo_text_extents(cr, label, &te);
            if (te.width > max_label_w) max_label_w = (int)te.width;
        }
        int pw = std::clamp(12 + icon_size + 8 + max_label_w + 12, 200, 400);
        int ph = max_items > 0 ? 4 + max_items * item_h + 4 : 40;
        int px = std::min(btn_x, win_w - pw - 8);
        int py = Overlay::kToolbarHeight + 4;
        int text_max_w = pw - 12 - icon_size - 8 - 12;

        window_menu_x_ = px; window_menu_y_ = py;
        window_menu_w_ = pw; window_menu_h_ = ph;

        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.95);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_stroke(cr);

        if (max_items > 0) {
            for (int i = 0; i < max_items; i++) {
                int iy = py + 4 + i * item_h;
                auto& we = screenshot_windows_[i];

                if (i == window_menu_hover_) {
                    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                          m3::on_surface_variant_b, 0.08);
                    cairo_rectangle(cr, px + 4, iy, pw - 8, item_h);
                    cairo_fill(cr);
                }

                int icon_x = px + 12;
                int icon_y = iy + (item_h - icon_size) / 2;
                const auto* icon = screenshot_icon_cache_.app_icon(we.appId);
                if (icon && icon->surface) {
                    cairo_save(cr);
                    double scale = (double)icon_size / icon->width;
                    cairo_translate(cr, icon_x, icon_y);
                    cairo_scale(cr, scale, scale);
                    cairo_set_source_surface(cr, icon->surface, 0, 0);
                    cairo_paint(cr);
                    cairo_restore(cr);
                }

                cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
                const char* label = we.title.empty() ? we.appId.c_str() : we.title.c_str();
                // Truncate with ellipsis if too long
                std::string display;
                cairo_text_extents_t te;
                cairo_text_extents(cr, label, &te);
                if (te.width > text_max_w) {
                    display = label;
                    while (!display.empty()) {
                        cairo_text_extents(cr, (display + "\u2026").c_str(), &te);
                        if (te.width <= text_max_w) break;
                        display.pop_back();
                    }
                    display += "\u2026";
                } else {
                    display = label;
                }
                cairo_move_to(cr, px + 12 + icon_size + 8, iy + item_h / 2 + 5);
                cairo_show_text(cr, display.c_str());
            }
        } else {
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.5);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            const char* msg = screenshot_toplevel_avail_ ? "No windows found" : "Window list not available";
            cairo_text_extents_t te;
            cairo_text_extents(cr, msg, &te);
            cairo_move_to(cr, px + (pw - te.width) / 2, py + 26);
            cairo_show_text(cr, msg);
        }
    }

    // --- Screen capture dropdown ---
    if (show_screen_menu_) {
        int btn_x = 0;
        for (auto& btn : toolbar_buttons_) {
            if (btn.label == "Screen") { btn_x = btn.x; break; }
        }
        int item_h = 36;
        int num_outputs = (int)screenshot_outputs_.size();
        int num_items = num_outputs > 0 ? num_outputs + 1 : 1; // +1 for "All Screens"
        int max_items = std::min(num_items, 10);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        int max_label_w = 0;
        for (int i = 0; i < max_items; i++) {
            const char* label;
            if (i == 0) {
                label = "All Screens";
            } else {
                auto& out = screenshot_outputs_[i - 1];
                char buf[128];
                snprintf(buf, sizeof(buf), "%s  %dx%d", out.name.c_str(), out.width, out.height);
                label = buf;
            }
            cairo_text_extents_t te;
            cairo_text_extents(cr, label, &te);
            if (te.width > max_label_w) max_label_w = (int)te.width;
        }
        int pw = std::clamp(12 + max_label_w + 12, 200, 400);
        int ph = 4 + max_items * item_h + 4;
        int px = std::min(btn_x, win_w - pw - 8);
        int py = Overlay::kToolbarHeight + 4;
        int text_max_w = pw - 24;

        screen_menu_x_ = px; screen_menu_y_ = py;
        screen_menu_w_ = pw; screen_menu_h_ = ph;

        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.95);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_stroke(cr);

        for (int i = 0; i < max_items; i++) {
            int iy = py + 4 + i * item_h;

            if (i == screen_menu_hover_) {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, 0.08);
                cairo_rectangle(cr, px + 4, iy, pw - 8, item_h);
                cairo_fill(cr);
            }

            std::string display;
            if (i == 0) {
                display = "All Screens";
            } else {
                auto& out = screenshot_outputs_[i - 1];
                char buf[128];
                snprintf(buf, sizeof(buf), "%s  %dx%d", out.name.c_str(), out.width, out.height);
                display = buf;
            }

            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
            cairo_text_extents_t te;
            cairo_text_extents(cr, display.c_str(), &te);
            if (te.width > text_max_w) {
                while (!display.empty()) {
                    cairo_text_extents(cr, (display + "\u2026").c_str(), &te);
                    if (te.width <= text_max_w) break;
                    display.pop_back();
                }
                display += "\u2026";
            }
            cairo_move_to(cr, px + 12, iy + item_h / 2 + 5);
            cairo_show_text(cr, display.c_str());
        }
    }

    // --- Upload submenu ---
    if (show_upload_menu_) {
        int btn_x = 0;
        for (auto& btn : toolbar_buttons_) {
            if (btn.label == "Upload") { btn_x = btn.x; break; }
        }
        int item_h = 36;
        int num_items = 1; // Imgur for now
        const char* items[] = { "Imgur" };
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        int max_label_w = 0;
        for (int i = 0; i < num_items; i++) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, items[i], &te);
            if (te.width > max_label_w) max_label_w = (int)te.width;
        }
        int pw = std::clamp(12 + max_label_w + 12, 140, 300);
        int ph = 4 + num_items * item_h + 4;
        int px = std::min(btn_x, win_w - pw - 8);
        int py = Overlay::kToolbarHeight + 4;

        upload_menu_x_ = px; upload_menu_y_ = py;
        upload_menu_w_ = pw; upload_menu_h_ = ph;

        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.95);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_stroke(cr);

        for (int i = 0; i < num_items; i++) {
            int iy = py + 4 + i * item_h;

            if (i == upload_menu_hover_) {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, 0.08);
                cairo_rectangle(cr, px + 4, iy, pw - 8, item_h);
                cairo_fill(cr);
            }

            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
            cairo_move_to(cr, px + 12, iy + item_h / 2 + 5);
            cairo_show_text(cr, items[i]);
        }
    }

    // --- Open submenu ---
    if (show_open_menu_) {
        int btn_x = 0;
        for (auto& btn : toolbar_buttons_) {
            if (btn.label == "Open") { btn_x = btn.x; break; }
        }
        int item_h = 36;
        const int num_items = 2;
        const char* items[] = { "Open New", "Open Recent" };
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        int max_label_w = 0;
        for (int i = 0; i < num_items; i++) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, items[i], &te);
            if (te.width > max_label_w) max_label_w = (int)te.width;
        }
        int pw = std::clamp(12 + max_label_w + 12 + 16, 160, 300);
        int ph = 4 + num_items * item_h + 4;
        int px = std::min(btn_x, win_w - pw - 8);
        int py = Overlay::kToolbarHeight + 4;

        open_menu_x_ = px; open_menu_y_ = py;
        open_menu_w_ = pw; open_menu_h_ = ph;

        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.95);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_stroke(cr);

        for (int i = 0; i < num_items; i++) {
            int iy = py + 4 + i * item_h;

            if (i == open_menu_hover_) {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, 0.08);
                cairo_rectangle(cr, px + 4, iy, pw - 8, item_h);
                cairo_fill(cr);
            }

            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
            cairo_move_to(cr, px + 12, iy + item_h / 2 + 5);
            cairo_show_text(cr, items[i]);

            // Chevron for "Open Recent"
            if (i == 1) {
                cairo_move_to(cr, px + pw - 20, iy + item_h / 2 + 5);
                cairo_show_text(cr, "\u203A");
            }
        }
    }

    // --- Open Recent submenu ---
    if (show_open_recent_menu_) {
        int item_h = 36;
        int max_items = std::min((int)config_.recent_files.size(), 10);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        int max_label_w = 0;
        for (int i = 0; i < max_items; i++) {
            fs::path p(config_.recent_files[i]);
            std::string label = p.filename().string();
            cairo_text_extents_t te;
            cairo_text_extents(cr, label.c_str(), &te);
            if (te.width > max_label_w) max_label_w = (int)te.width;
        }
        int pw = std::clamp(12 + max_label_w + 12, 180, 400);
        int ph = max_items > 0 ? 4 + max_items * item_h + 4 : 40;
        int px = std::min(open_menu_x_ + open_menu_w_, win_w - pw - 8);
        int py = open_menu_y_ + 4 + 1 * item_h; // align with "Open Recent" item

        open_recent_menu_x_ = px; open_recent_menu_y_ = py;
        open_recent_menu_w_ = pw; open_recent_menu_h_ = ph;

        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.95);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, px, py, pw, ph, 10);
        cairo_stroke(cr);

        if (max_items > 0) {
            for (int i = 0; i < max_items; i++) {
                int iy = py + 4 + i * item_h;

                if (i == open_recent_menu_hover_) {
                    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                          m3::on_surface_variant_b, 0.08);
                    cairo_rectangle(cr, px + 4, iy, pw - 8, item_h);
                    cairo_fill(cr);
                }

                fs::path p(config_.recent_files[i]);
                std::string label = p.filename().string();
                cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
                cairo_text_extents_t te;
                cairo_text_extents(cr, label.c_str(), &te);
                if (te.width > pw - 24) {
                    while (!label.empty()) {
                        cairo_text_extents(cr, (label + "\u2026").c_str(), &te);
                        if (te.width <= pw - 24) break;
                        label.pop_back();
                    }
                    label += "\u2026";
                }
                cairo_move_to(cr, px + 12, iy + item_h / 2 + 5);
                cairo_show_text(cr, label.c_str());
            }
        } else {
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.5);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            const char* msg = "No recent files";
            cairo_text_extents_t te;
            cairo_text_extents(cr, msg, &te);
            cairo_move_to(cr, px + (pw - te.width) / 2, py + 26);
            cairo_show_text(cr, msg);
        }
    }

    // --- Crop overlay ---
    if (crop_active_ && decoded_image_.width > 0) {
        std::vector<OverlayButton> crop_buttons;
        overlay_.render_crop_overlay(cr, win_w, win_h, ov_state, crop_buttons);
        // Draw the actual crop rectangle
        draw_crop_rect(cr, win_w, win_h);
        // Apply / Cancel floating buttons — always at the content area bottom so crop rect never obscures them
        int btn_w = 100, btn_h = 36;
        int btn_y = win_h - strip_h - btn_h - 20;
        int cx = (win_w - (btn_w * 2 + 10)) / 2;
        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.9);
        overlay_.draw_rounded_rect(cr, cx, btn_y, btn_w, btn_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_primary_container_r, m3::on_primary_container_g,
                              m3::on_primary_container_b, 1.0);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, cx + 30, btn_y + 24);
        cairo_show_text(cr, "Apply");
        crop_buttons.push_back({cx, btn_y, btn_w, btn_h, "CropApply", {}, {}});

        cx += btn_w + 10;
        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.15);
        overlay_.draw_rounded_rect(cr, cx, btn_y, btn_w, btn_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 0.87);
        cairo_move_to(cr, cx + 26, btn_y + 24);
        cairo_show_text(cr, "Cancel");
        crop_buttons.push_back({cx, btn_y, btn_w, btn_h, "CropCancel", {}, {}});

        for (auto& btn : crop_buttons) {
            if (btn.label == "CropApply") btn.action = [this]() { apply_crop(); };
            else if (btn.label == "CropCancel") btn.action = [this]() { cancel_crop(); };
        }
        toolbar_buttons_.insert(toolbar_buttons_.end(),
                                crop_buttons.begin(), crop_buttons.end());
    }

    // --- Markup overlay ---
    if (markup_active_ && decoded_image_.width > 0) {
        overlay_.render_markup_overlay(cr, win_w, win_h, ov_state);

        std::vector<OverlayButton> markup_buttons;

        // --- Tool submenu ---
        int sub_x = 10;
        int sub_y = Overlay::kToolbarHeight + 10;
        int tool_btn_w = 56, tool_btn_h = 28;
        const char* tool_names[] = {"Pen", "Line", "Arrow", "Rect", "Ellipse"};
        int num_tools = 5;
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        for (int i = 0; i < num_tools; i++) {
            int tx = sub_x + i * (tool_btn_w + 4);
            bool active = (int)markup_tool_ == i;
            if (active) {
                cairo_set_source_rgba(cr, m3::primary_container_r, m3::primary_container_g,
                                      m3::primary_container_b, 1.0);
            } else {
                cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                                      m3::surface_container_high_b, 0.9);
            }
            overlay_.draw_rounded_rect(cr, tx, sub_y, tool_btn_w, tool_btn_h, 6);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, active ? m3::on_primary_container_r : m3::on_surface_r,
                                  active ? m3::on_primary_container_g : m3::on_surface_g,
                                  active ? m3::on_primary_container_b : m3::on_surface_b, 0.87);
            cairo_move_to(cr, tx + 8, sub_y + 18);
            cairo_show_text(cr, tool_names[i]);
            markup_buttons.push_back({tx, sub_y, tool_btn_w, tool_btn_h,
                                      "MTool" + std::to_string(i), {}, {}});
        }

        // --- Color hue bar ---
        hue_bar_x_ = (float)sub_x;
        hue_bar_y_ = (float)(Overlay::kToolbarHeight + 46);
        // Gradient stops: 7 stops across the hue wheel
        struct { float pos; float r, g, b; } stops[] = {
            {0.00f, 1,0,0}, {0.17f, 1,1,0}, {0.33f, 0,1,0},
            {0.50f, 0,1,1}, {0.67f, 0,0,1}, {0.83f, 1,0,1},
            {1.00f, 1,0,0},
        };
        cairo_save(cr);
        overlay_.draw_rounded_rect(cr, hue_bar_x_, hue_bar_y_, hue_bar_w_, hue_bar_h_, 4);
        cairo_clip(cr);
        for (int s = 0; s < 6; s++) {
            float x0 = hue_bar_x_ + stops[s].pos * hue_bar_w_;
            float x1 = hue_bar_x_ + stops[s + 1].pos * hue_bar_w_;
            cairo_pattern_t* pat = cairo_pattern_create_linear(x0, 0, x1, 0);
            cairo_pattern_add_color_stop_rgba(pat, 0, stops[s].r, stops[s].g, stops[s].b, 1);
            cairo_pattern_add_color_stop_rgba(pat, 1, stops[s+1].r, stops[s+1].g, stops[s+1].b, 1);
            cairo_rectangle(cr, x0, hue_bar_y_, x1 - x0, hue_bar_h_);
            cairo_set_source(cr, pat);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);
        }
        cairo_restore(cr);
        // Thumb showing current hue
        {
            float hue = hue_from_rgb(markup_color_);
            float thumb_cx = hue_bar_x_ + hue * hue_bar_w_;
            float thumb_cy = hue_bar_y_ + hue_bar_h_ * 0.5f;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
            cairo_arc(cr, thumb_cx, thumb_cy, 8, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
            cairo_arc(cr, thumb_cx, thumb_cy, 8, 0, 2 * M_PI);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }

        // --- Thickness submenu ---
        sub_y = Overlay::kToolbarHeight + 80;
        int thick_btn_w = 56, thick_btn_h = 28;
        float thicknesses[] = {2.0f, 4.0f, 8.0f};
        const char* thick_labels[] = {"Thin", "Med", "Thick"};
        for (int i = 0; i < 3; i++) {
            int tx = sub_x + i * (thick_btn_w + 4);
            bool active = std::abs(markup_thickness_ - thicknesses[i]) < 0.1f;
            if (active) {
                cairo_set_source_rgba(cr, m3::primary_container_r, m3::primary_container_g,
                                      m3::primary_container_b, 1.0);
            } else {
                cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                                      m3::surface_container_high_b, 0.9);
            }
            overlay_.draw_rounded_rect(cr, tx, sub_y, thick_btn_w, thick_btn_h, 6);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, active ? m3::on_primary_container_r : m3::on_surface_r,
                                  active ? m3::on_primary_container_g : m3::on_surface_g,
                                  active ? m3::on_primary_container_b : m3::on_surface_b, 0.87);
            cairo_move_to(cr, tx + 10, sub_y + 18);
            cairo_show_text(cr, thick_labels[i]);
            markup_buttons.push_back({tx, sub_y, thick_btn_w, thick_btn_h,
                                      "MThick_" + std::to_string(i), {}, {}});
        }

        // --- Undo button ---
        {
            int ux = sub_x + 3 * (thick_btn_w + 4) + 12;
            int uy = Overlay::kToolbarHeight + 80;
            int uw = 56, uh = 28;
            if (!markup_elements_.empty()) {
                cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                                      m3::surface_container_high_b, 0.9);
            } else {
                cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                                      m3::surface_container_b, 0.5);
            }
            overlay_.draw_rounded_rect(cr, ux, uy, uw, uh, 6);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                                  m3::on_surface_b, markup_elements_.empty() ? 0.3 : 0.87);
            cairo_move_to(cr, ux + 14, uy + 18);
            cairo_show_text(cr, "Undo");
            markup_buttons.push_back({ux, uy, uw, uh, "MUndo", {}, {}});
        }

        // Apply / Cancel floating buttons
        int btn_w = 100, btn_h = 36;
        int btn_y = win_h - strip_h - btn_h - 20;
        int cx = (win_w - (btn_w * 2 + 10)) / 2;
        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.9);
        overlay_.draw_rounded_rect(cr, cx, btn_y, btn_w, btn_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_primary_container_r, m3::on_primary_container_g,
                              m3::on_primary_container_b, 1.0);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, cx + 30, btn_y + 24);
        cairo_show_text(cr, "Apply");
        markup_buttons.push_back({cx, btn_y, btn_w, btn_h, "MarkupApply", {}, {}});

        cx += btn_w + 10;
        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.15);
        overlay_.draw_rounded_rect(cr, cx, btn_y, btn_w, btn_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 0.87);
        cairo_move_to(cr, cx + 26, btn_y + 24);
        cairo_show_text(cr, "Cancel");
        markup_buttons.push_back({cx, btn_y, btn_w, btn_h, "MarkupCancel", {}, {}});

        for (auto& btn : markup_buttons) {
            if (btn.label == "MarkupApply") btn.action = [this]() { commit_markup(); };
            else if (btn.label == "MarkupCancel") btn.action = [this]() { cancel_markup(); };
            else if (btn.label == "MTool0") btn.action = [this]() { markup_tool_ = MarkupTool::kPen; render(); };
            else if (btn.label == "MTool1") btn.action = [this]() { markup_tool_ = MarkupTool::kLine; render(); };
            else if (btn.label == "MTool2") btn.action = [this]() { markup_tool_ = MarkupTool::kArrow; render(); };
            else if (btn.label == "MTool3") btn.action = [this]() { markup_tool_ = MarkupTool::kRect; render(); };
            else if (btn.label == "MTool4") btn.action = [this]() { markup_tool_ = MarkupTool::kEllipse; render(); };
            else if (btn.label == "MHueBar") btn.action = {}; // handled in on_pointer
            else if (btn.label == "MThick_0") btn.action = [this]() { markup_thickness_ = 2.0f; render(); };
            else if (btn.label == "MThick_1") btn.action = [this]() { markup_thickness_ = 4.0f; render(); };
            else if (btn.label == "MThick_2") btn.action = [this]() { markup_thickness_ = 8.0f; render(); };
            else if (btn.label == "MUndo") btn.action = [this]() { undo_markup(); };
        }
        toolbar_buttons_.insert(toolbar_buttons_.end(),
                                markup_buttons.begin(), markup_buttons.end());
    }

    // --- Thumbnail strip (pushed below content area) ---
    if (strip_visible && decoded_image_.width > 0 && dir_images_.size() > 1) {
        if (thumb_dirty_ || cached_strip_w_ != content_w) {
            destroy_cached_strip();
            cached_strip_w_ = content_w;
            cached_strip_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, content_w,
                                                       ThumbnailStrip::kHeight);
            cairo_t* strip_cr = cairo_create(cached_strip_);

            // Render with win_h = kHeight so strip_y = 0 within the strip surface
            ThumbnailStripState ts;
            ts.image_index = dir_image_index_;
            ts.image_count = (int)dir_images_.size();
            ts.win_w = content_w;
            ts.win_h = ThumbnailStrip::kHeight;
            ts.scroll_offset = thumb_scroll_;
            ts.cache = &thumb_cache_;
            ts.cache_w = &thumb_cache_w_;
            ts.cache_h = &thumb_cache_h_;
            thumbnail_strip_.render(strip_cr, ts, thumb_entries_);
            // Fill in click actions
            for (auto& e : thumb_entries_) {
                if (e.current) continue;
                int idx = e.index;
                e.action = [this, idx]() {
                    if (idx >= 0 && idx < (int)dir_images_.size()) {
                        dir_image_index_ = idx;
                        open_file(dir_images_[idx]);
                    }
                };
            }

            cairo_destroy(strip_cr);
            // Convert entry coords from strip-relative to window-relative
            // since hit-testing uses window coords
            {
                int strip_y_win = win_h - strip_h;
                for (auto& e : thumb_entries_) e.y += strip_y_win;
            }
            thumb_dirty_ = false;
        }
        // Blit cached strip
        cairo_set_source_surface(cr, cached_strip_, 0, win_h - strip_h);
        cairo_paint(cr);
    }

    // --- Overlay info / placeholder (drawn on top of thumbnail strip) ---
    overlay_.render_overlay(cr, win_w, win_h, ov_state);

    // --- Settings popup (drawn on top of everything) ---
    if (show_settings_) {
        // Sync M3 widget colors with current theme tokens
        bg_alpha_slider_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        bg_alpha_slider_.setSurfaceColor(m3::surface_r, m3::surface_g, m3::surface_b);
        default_zoom_slider_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        default_zoom_slider_.setSurfaceColor(m3::surface_r, m3::surface_g, m3::surface_b);
        ss_interval_slider_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        ss_interval_slider_.setSurfaceColor(m3::surface_r, m3::surface_g, m3::surface_b);
        theme_toggle_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        theme_toggle_.setOutlineColor(m3::outline_r, m3::outline_g, m3::outline_b);
        color_mgmt_toggle_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        color_mgmt_toggle_.setOutlineColor(m3::outline_r, m3::outline_g, m3::outline_b);
        imgur_direct_toggle_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        imgur_direct_toggle_.setOutlineColor(m3::outline_r, m3::outline_g, m3::outline_b);
        imgur_open_browser_toggle_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        imgur_open_browser_toggle_.setOutlineColor(m3::outline_r, m3::outline_g, m3::outline_b);
        imgur_auto_copy_toggle_.setAccentColor(m3::primary_r, m3::primary_g, m3::primary_b);
        imgur_auto_copy_toggle_.setOutlineColor(m3::outline_r, m3::outline_g, m3::outline_b);

        // Sync M3 widget state with config
        bg_alpha_slider_.setValue(ov_state.bg_alpha);
        default_zoom_slider_.setValue(ov_state.default_zoom);
        ss_interval_slider_.setValue((float)ov_state.slideshow_interval_ms);
        theme_toggle_.setOn(ov_state.theme == "dark");
        color_mgmt_toggle_.setOn(ov_state.enable_color_management);
        imgur_direct_toggle_.setOn(config_.imgur_direct_link);
        imgur_open_browser_toggle_.setOn(config_.imgur_open_browser);
        imgur_auto_copy_toggle_.setOn(config_.imgur_auto_copy);

        ov_state.imgur_client_id = config_.imgur_client_id;

        std::vector<OverlayButton> settings_buttons;
        overlay_.render_settings_popup(cr, win_w, win_h, ov_state, settings_buttons,
                                        bg_alpha_slider_, default_zoom_slider_,
                                        ss_interval_slider_, theme_toggle_,
                                        color_mgmt_toggle_, imgur_direct_toggle_,
                                        imgur_open_browser_toggle_,
                                        imgur_auto_copy_toggle_,
                                        active_settings_tab_);
        for (auto& btn : settings_buttons) {
            if (btn.label == "CloseSettings") {
                btn.action = [this]() { toggle_settings(); };
            } else if (btn.label == "SettingsTab0") {
                btn.action = [this]() { set_active_settings_tab(0); };
            } else if (btn.label == "SettingsTab1") {
                btn.action = [this]() { set_active_settings_tab(1); };
            } else if (btn.label == "EditClientId") {
                btn.action = [this]() {
                    toggle_settings();
                    show_upload_setup_ = true;
                    upload_setup_input_ = config_.imgur_client_id;
                    render();
                };
            }
        }
        toolbar_buttons_.insert(toolbar_buttons_.end(),
                                settings_buttons.begin(), settings_buttons.end());
    }

    // --- Sidebar (drawn in its dedicated area — no dim overlay) ---
    if (show_sidebar_) {
        std::vector<OverlayButton> sidebar_buttons;
        overlay_.render_sidebar(cr, win_w, win_h, ov_state, sidebar_buttons);
        for (auto& btn : sidebar_buttons) {
            if (btn.label == "CloseSidebar") {
                btn.action = [this]() { toggle_sidebar(); };
            }
        }
        toolbar_buttons_.insert(toolbar_buttons_.end(),
                                sidebar_buttons.begin(), sidebar_buttons.end());
    }

    // --- Screenshot panel ---
    if (screenshot_panel_active_) {
        screenshot_render_panel(cr, win_w, win_h);
    }

    // --- Upload setup dialog ---
    if (show_upload_setup_) {
        int dw = 500, dh = 340;
        int dx = (window_width_ - dw) / 2;
        int dy = (window_height_ - dh) / 2;

        // Dim overlay
        cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
        cairo_paint(cr);

        // Dialog background
        cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                              m3::surface_container_high_b, 0.98);
        overlay_.draw_rounded_rect(cr, dx, dy, dw, dh, 12);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                              m3::outline_variant_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, dx, dy, dw, dh, 12);
        cairo_stroke(cr);

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
        cairo_move_to(cr, dx + 20, dy + 28);
        cairo_show_text(cr, "Imgur Upload Setup");

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);

        int ty = dy + 50;
        auto draw_text = [&](const char* text, int y) {
            cairo_move_to(cr, dx + 20, y);
            cairo_show_text(cr, text);
        };

        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.87);
        draw_text("To upload images to Imgur, you need a Client ID.", ty);
        draw_text("1. Log into your Imgur account in a browser.", ty + 18);
        draw_text("2. Go to the API registration page (must be logged in):", ty + 36);

        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.87);
        draw_text("https://api.imgur.com/oauth2/addclient", ty + 54);

        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.87);
        draw_text("3. Fill in any app name, leave callback URL blank", ty + 76);
        draw_text("4. Choose \"Anonymous usage without user authorization\"", ty + 94);
        draw_text("5. Submit and copy the Client ID", ty + 112);
        draw_text("6. Paste the Client ID below and click Save", ty + 130);

        // Text input box
        int box_y = ty + 148;
        int box_h = 36;
        cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                              m3::surface_container_b, 1.0);
        overlay_.draw_rounded_rect(cr, dx + 20, box_y, dw - 40, box_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::outline_r, m3::outline_g, m3::outline_b, 0.6);
        cairo_set_line_width(cr, 1);
        overlay_.draw_rounded_rect(cr, dx + 20, box_y, dw - 40, box_h, 8);
        cairo_stroke(cr);

        // Input text
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);

        // Truncate and show text with cursor
        std::string display = upload_setup_input_;
        cairo_text_extents_t te;
        cairo_text_extents(cr, display.c_str(), &te);
        int max_text_w = dw - 40 - 16;
        if (te.width > max_text_w) {
            while (!display.empty()) {
                cairo_text_extents(cr, display.c_str(), &te);
                if (te.width <= max_text_w) break;
                display.erase(0, 1);
            }
        }
        cairo_move_to(cr, dx + 28, box_y + box_h / 2 + 5);
        cairo_show_text(cr, display.c_str());

        // Cursor
        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.87);
        cairo_set_line_width(cr, 1.5);
        double cursor_x = dx + 28 + te.width;
        cairo_move_to(cr, cursor_x, box_y + 8);
        cairo_line_to(cr, cursor_x, box_y + box_h - 8);
        cairo_stroke(cr);

        // Buttons
        int btn_w = 100, btn_h = 36;
        int btn_y = dy + dh - 20 - btn_h;
        int save_x = dx + dw - 20 - btn_w;
        int cancel_x = save_x - btn_w - 10;

        auto draw_btn = [&](int bx, const char* label, bool primary, bool hovered) {
            if (primary) {
                cairo_set_source_rgba(cr, m3::primary_container_r, m3::primary_container_g,
                                      m3::primary_container_b, hovered ? 0.9 : 0.8);
            } else {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, hovered ? 0.15 : 0.1);
            }
            overlay_.draw_rounded_rect(cr, bx, btn_y, btn_w, btn_h, 8);
            cairo_fill(cr);

            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 14);
            cairo_set_source_rgba(cr, primary ? m3::on_primary_container_r : m3::on_surface_r,
                                  primary ? m3::on_primary_container_g : m3::on_surface_g,
                                  primary ? m3::on_primary_container_b : m3::on_surface_b,
                                  primary ? 1.0 : 0.87);
            cairo_text_extents_t te2;
            cairo_text_extents(cr, label, &te2);
            cairo_move_to(cr, bx + (btn_w - te2.width) / 2, btn_y + btn_h / 2 + 5);
            cairo_show_text(cr, label);
        };

        draw_btn(cancel_x, "Cancel", false, upload_setup_hover_btn_ == 0);
        draw_btn(save_x, "Save", true, upload_setup_hover_btn_ == 1);
    }

    // --- Upload progress bar ---
#ifdef HAVE_LIBCURL
    if (upload_state_) {
        float p = upload_state_->progress.load(std::memory_order_relaxed);
        if (p >= 0.0f && p < 2.0f) {
            cairo_save(cr);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 13);

            int bar_w = 260;
            int bar_h = 6;
            int pct = (int)(p * 100.0f);
            std::string label = "Uploading... " + std::to_string(pct) + "%";

            cairo_text_extents_t te;
            cairo_text_extents(cr, label.c_str(), &te);
            int pad = 10;
            int bg_w = bar_w + pad * 2;
            int bg_h = bar_h + pad * 2 + (int)te.height + 6;
            int bg_x = (win_w - bg_w) / 2;
            int bg_y = win_h - bg_h - 40;

            // Background
            cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                                  m3::surface_container_b, 0.95);
            overlay_.draw_rounded_rect(cr, bg_x, bg_y, bg_w, bg_h, 8);
            cairo_fill(cr);

            // Label
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                                  m3::on_surface_b, 0.87);
            cairo_move_to(cr, bg_x + (bg_w - te.width) / 2, bg_y + pad + (int)te.height - 2);
            cairo_show_text(cr, label.c_str());

            // Bar background
            int bar_x = bg_x + pad;
            int bar_y = bg_y + pad + (int)te.height + 4;
            cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
            overlay_.draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, 3);
            cairo_fill(cr);

            // Bar fill
            if (p > 0.0f) {
                int fill_w = (int)(bar_w * p);
                if (fill_w < 4) fill_w = 4;
                cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.9);
                overlay_.draw_rounded_rect(cr, bar_x, bar_y, fill_w, bar_h, 3);
                cairo_fill(cr);
            }

            cairo_restore(cr);
        }
    }
#endif

    // --- Toast notification ---
    if (!toast_message_.empty()) {
        cairo_save(cr);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);
        cairo_text_extents_t te;
        cairo_text_extents(cr, toast_message_.c_str(), &te);
        int pad = 12;
        int tw = (int)te.width + pad * 2;
        int th = (int)te.height + pad * 2;
        int tx = (win_w - tw) / 2;
        int ty = win_h - th - 40;
        if (tx < 4) tx = 4;
        if (tx + tw > win_w - 4) tx = win_w - tw - 4;
        cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                              m3::surface_container_b, 0.95);
        overlay_.draw_rounded_rect(cr, tx, ty, tw, th, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 0.87);
        cairo_move_to(cr, tx + pad, ty + pad + (int)te.height - 3);
        cairo_show_text(cr, toast_message_.c_str());
        cairo_restore(cr);
    }

    // --- Toolbar tooltip ---
    if (show_toolbar_ && toolbar_hover_idx_ >= 0 &&
        toolbar_hover_idx_ < (int)toolbar_buttons_.size()) {
        auto& btn = toolbar_buttons_[toolbar_hover_idx_];
        if (!btn.tooltip.empty()) {
            cairo_save(cr);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 13);
            cairo_text_extents_t te;
            cairo_text_extents(cr, btn.tooltip.c_str(), &te);
            int pad = 8;
            int tw = (int)te.width + pad * 2;
            int th = (int)te.height + pad * 2;
            int tx = btn.x + (btn.w - tw) / 2;
            int ty = btn.y + btn.h + 6;
            if (tx < 4) tx = 4;
            if (tx + tw > win_w - 4) tx = win_w - tw - 4;
            cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                                  m3::surface_container_b, 0.95);
            overlay_.draw_rounded_rect(cr, tx, ty, tw, th, 6);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                                  m3::on_surface_b, 0.87);
            cairo_move_to(cr, tx + pad, ty + pad + (int)te.height - 3);
            cairo_show_text(cr, btn.tooltip.c_str());
            cairo_restore(cr);
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(cs);

    // Mark buffer as busy, attach, damage, commit
    bufs_[bi].mark_busy();
    wl_surface_attach(surface_, bufs_[bi].buffer(), 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, win_w, win_h);
    wl_surface_commit(surface_);
    wl_display_flush(conn_.display());

    // Toggle buffer for next frame
    paint_buf_ = 1 - bi;
}

void App::on_shm_release() {
    if (pending_redraw_) {
        pending_redraw_ = false;
        render();
    }
}


// --- Event handlers ---

void App::set_window_size(int width, int height) {
    if (width > 0 && height > 0 &&
        (width != window_width_ || height != window_height_)) {
        window_width_ = width;
        window_height_ = height;
        render();
    }
}

void App::on_close() {
    running_ = false;
}

void App::on_key(const KeyEvent& ev) {
    if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    // Upload setup dialog keyboard input
    if (show_upload_setup_) {
        if (ev.sym == XKB_KEY_Escape) {
            show_upload_setup_ = false;
            render();
            return;
        }
        if (ev.sym == XKB_KEY_Return || ev.sym == XKB_KEY_KP_Enter) {
            if (!upload_setup_input_.empty()) {
                config_.imgur_client_id = upload_setup_input_;
                Config::save(config_);
                show_upload_setup_ = false;
                upload_image();
            }
            return;
        }
        if (ev.sym == XKB_KEY_BackSpace) {
            if (!upload_setup_input_.empty()) {
                upload_setup_input_.pop_back();
                render();
            }
            return;
        }
        if (ev.utf8_len > 0 && ev.utf8[0] >= 0x20 && ev.utf8[0] < 0x7F) {
            upload_setup_input_ += std::string(ev.utf8, ev.utf8_len);
            render();
            return;
        }
        return; // consume all keys while dialog is active
    }

    switch (ev.sym) {
        case XKB_KEY_q:
            if (ev.ctrl) quit();
            break;
        case XKB_KEY_z:
            if (ev.ctrl && !markup_elements_.empty()) { undo_markup(); break; }
            break;
        case XKB_KEY_o:
            if (ev.ctrl) open_file_dialog();
            break;
        case XKB_KEY_f:
        case XKB_KEY_F11:
            toggle_fullscreen();
            break;
        case XKB_KEY_Escape:
            if (show_upload_setup_) { show_upload_setup_ = false; render(); break; }
            if (markup_active_) { cancel_markup(); break; }
            if (crop_active_) { cancel_crop(); break; }
            if (show_upload_menu_) { show_upload_menu_ = false; render(); break; }
            if (show_menu_) { show_menu_ = false; render(); break; }
            if (show_sidebar_) { toggle_sidebar(); break; }
            if (show_settings_) { toggle_settings(); break; }
            if (fullscreen_) toggle_fullscreen();
            else quit();
            break;
        case XKB_KEY_Right:
        case XKB_KEY_j:
            next_image();
            break;
        case XKB_KEY_Left:
        case XKB_KEY_k:
            prev_image();
            break;
        case XKB_KEY_Home:
            first_image();
            break;
        case XKB_KEY_End:
            last_image();
            break;
        case XKB_KEY_plus:
        case XKB_KEY_equal:
            if (ev.ctrl) zoom_in();
            break;
        case XKB_KEY_minus:
            if (ev.ctrl) zoom_out();
            break;
        case XKB_KEY_0:
            if (ev.ctrl) zoom_fit();
            break;
        case XKB_KEY_1:
            if (ev.ctrl) zoom_1to1();
            break;
        case XKB_KEY_space:
            toggle_slideshow();
            break;
        case XKB_KEY_i:
            toggle_overlay();
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            if (markup_active_) { commit_markup(); break; }
            if (crop_active_) { apply_crop(); break; }
            break;
        case XKB_KEY_r:
            if (ev.shift) rotate_90_ccw();
            else rotate_90_cw();
            break;
        case XKB_KEY_h:
            flip_horizontal();
            break;
        case XKB_KEY_v:
            flip_vertical();
            break;
        case XKB_KEY_Delete:
        case XKB_KEY_KP_Delete:
            delete_image();
            break;
        default:
            break;
    }
}

void App::on_pointer(const PointerEvent& ev) {
    if (ev.button != 0x110) return; // BTN_LEFT

    SC_LOG("on_pointer: state=%d x=%d y=%d panel_active=%d",
           ev.state, ev.x, ev.y, (int)screenshot_panel_active_);

    if (ev.state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // Dismiss toast on any click
        if (!toast_message_.empty()) {
            toast_message_.clear();
            pending_redraw_ = true;
        }

        // Upload setup dialog takes full priority
        if (show_upload_setup_) {
            int dw = 500, dh = 340;
            int dx = (window_width_ - dw) / 2;
            int dy = (window_height_ - dh) / 2;
            // Outside click closes
            if (!(ev.x >= dx && ev.x < dx + dw && ev.y >= dy && ev.y < dy + dh)) {
                show_upload_setup_ = false;
                render();
                return;
            }
            // Save button
            int btn_w = 100, btn_h = 36;
            int btn_y = dy + dh - 20 - btn_h;
            int save_x = dx + dw - 20 - btn_w;
            int cancel_x = save_x - btn_w - 10;
            if (ev.x >= save_x && ev.x < save_x + btn_w && ev.y >= btn_y && ev.y < btn_y + btn_h) {
                if (!upload_setup_input_.empty()) {
                    config_.imgur_client_id = upload_setup_input_;
                    Config::save(config_);
                    show_upload_setup_ = false;
                    upload_image();
                }
                return;
            }
            if (ev.x >= cancel_x && ev.x < cancel_x + btn_w && ev.y >= btn_y && ev.y < btn_y + btn_h) {
                show_upload_setup_ = false;
                render();
                return;
            }
            return; // consume click inside dialog
        }

        // Screenshot panel takes full priority when active
        if (screenshot_panel_active_) {
            screenshot_handle_click(ev.x, ev.y);
            return;
        }

        // Sidebar hit-test — consume clicks below toolbar area
        if (show_sidebar_ && ev.y >= Overlay::kToolbarHeight) {
            int sw = 320;
            int sx = window_width_ - sw;
            if (ev.x >= sx) {
                for (auto& btn : toolbar_buttons_) {
                    if (ev.x >= btn.x && ev.x < btn.x + btn.w &&
                        ev.y >= btn.y && ev.y < btn.y + btn.h && btn.action) {
                        if (btn.label == "CloseSidebar") {
                            toggle_sidebar();
                            return;
                        }
                    }
                }
                return; // consume click inside sidebar
            }
            // Click outside sidebar — let it pass through to other handlers
        }

        // Settings popup hit-test takes priority
        if (show_settings_) {
            int pw = 480, ph = 660;
            int px = (window_width_ - pw) / 2;
            int py = (window_height_ - ph) / 2;

            // Click outside popup closes it
            if (!(ev.x >= px && ev.x < px + pw && ev.y >= py && ev.y < py + ph)) {
                toggle_settings();
                return;
            }

            // 1. M3 slider dispatch
            if (bg_alpha_slider_.handlePointerDown(ev.x, ev.y)) {
                active_slider_ = &bg_alpha_slider_;
                set_bg_alpha(bg_alpha_slider_.value());
                return;
            }
            if (default_zoom_slider_.handlePointerDown(ev.x, ev.y)) {
                active_slider_ = &default_zoom_slider_;
                set_default_zoom(default_zoom_slider_.value());
                return;
            }
            if (ss_interval_slider_.handlePointerDown(ev.x, ev.y)) {
                active_slider_ = &ss_interval_slider_;
                set_slideshow_interval((int)ss_interval_slider_.value());
                return;
            }

            // 2. M3 toggle dispatch
            if (theme_toggle_.containsPoint(ev.x, ev.y)) {
                toggle_theme();
                return;
            }
            if (color_mgmt_toggle_.containsPoint(ev.x, ev.y)) {
                toggle_color_management();
                return;
            }
            if (imgur_direct_toggle_.containsPoint(ev.x, ev.y)) {
                toggle_imgur_direct_link();
                return;
            }
            if (imgur_open_browser_toggle_.containsPoint(ev.x, ev.y)) {
                toggle_imgur_open_browser();
                return;
            }
            if (imgur_auto_copy_toggle_.containsPoint(ev.x, ev.y)) {
                toggle_imgur_auto_copy();
                return;
            }

            // 3. Regular buttons (close, etc.)
            for (auto& btn : toolbar_buttons_) {
                if (ev.x >= btn.x && ev.x < btn.x + btn.w &&
                    ev.y >= btn.y && ev.y < btn.y + btn.h && btn.action) {
                    btn.action();
                    return;
                }
            }

            return; // consume click inside popup even if not on a button
        }

        // Menu popup: close on outside click
        if (show_menu_ && ev.y > Overlay::kToolbarHeight) {
            int pw = 200, ph = 160;
            int px = window_width_ - pw - 8;
            int py = Overlay::kToolbarHeight + 4;
            if (!(ev.x >= px && ev.x < px + pw && ev.y >= py && ev.y < py + ph)) {
                show_menu_ = false;
                render();
                return;
            }
        }

        // Window capture dropdown
        if (show_window_menu_) {
            if (!(ev.x >= window_menu_x_ && ev.x < window_menu_x_ + window_menu_w_ &&
                  ev.y >= window_menu_y_ && ev.y < window_menu_y_ + window_menu_h_)) {
                show_window_menu_ = false;
                render();
                return;
            }
            int item_h = 36;
            int rel_y = ev.y - (window_menu_y_ + 4);
            if (rel_y >= 0) {
                int idx = rel_y / item_h;
                if (idx >= 0 && idx < (int)screenshot_windows_.size()) {
                    show_window_menu_ = false;
                    screenshot_source_ = hpv::sc::Source::Window;
                    screenshot_sel_window_ = idx;
                    screenshot_trigger_capture();
                    return;
                }
            }
            render();
            return;
        }

        // Screen capture dropdown
        if (show_screen_menu_) {
            if (!(ev.x >= screen_menu_x_ && ev.x < screen_menu_x_ + screen_menu_w_ &&
                  ev.y >= screen_menu_y_ && ev.y < screen_menu_y_ + screen_menu_h_)) {
                show_screen_menu_ = false;
                render();
                return;
            }
            int item_h = 36;
            int rel_y = ev.y - (screen_menu_y_ + 4);
            if (rel_y >= 0) {
                int idx = rel_y / item_h;
                if (idx >= 0) {
                    show_screen_menu_ = false;
                    screenshot_source_ = hpv::sc::Source::Screen;
                    if (idx == 0) {
                        screenshot_capture_all_ = true;
                        screenshot_sel_output_ = -1;
                    } else {
                        screenshot_capture_all_ = false;
                        screenshot_sel_output_ = idx - 1;
                    }
                    screenshot_trigger_capture();
                    return;
                }
            }
            render();
            return;
        }

        // Upload submenu
        if (show_upload_menu_) {
            if (!(ev.x >= upload_menu_x_ && ev.x < upload_menu_x_ + upload_menu_w_ &&
                  ev.y >= upload_menu_y_ && ev.y < upload_menu_y_ + upload_menu_h_)) {
                show_upload_menu_ = false;
                render();
                return;
            }
            int item_h = 36;
            int rel_y = ev.y - (upload_menu_y_ + 4);
            if (rel_y >= 0) {
                int idx = rel_y / item_h;
                if (idx == 0) {
                    show_upload_menu_ = false;
                    upload_image();
                    return;
                }
            }
            render();
            return;
        }

        // Open submenu
        if (show_open_menu_) {
            bool in_open = ev.x >= open_menu_x_ && ev.x < open_menu_x_ + open_menu_w_ &&
                           ev.y >= open_menu_y_ && ev.y < open_menu_y_ + open_menu_h_;
            bool in_recent = show_open_recent_menu_ &&
                             ev.x >= open_recent_menu_x_ && ev.x < open_recent_menu_x_ + open_recent_menu_w_ &&
                             ev.y >= open_recent_menu_y_ && ev.y < open_recent_menu_y_ + open_recent_menu_h_;

            if (!in_open && !in_recent) {
                show_open_menu_ = false;
                show_open_recent_menu_ = false;
                render();
                return;
            }

            if (in_recent) {
                int item_h = 36;
                int rel_y = ev.y - (open_recent_menu_y_ + 4);
                if (rel_y >= 0) {
                    int idx = rel_y / item_h;
                    if (idx >= 0 && idx < (int)config_.recent_files.size()) {
                        show_open_menu_ = false;
                        show_open_recent_menu_ = false;
                        open_file(config_.recent_files[idx]);
                        return;
                    }
                }
                render();
                return;
            }

            if (in_open) {
                int item_h = 36;
                int rel_y = ev.y - (open_menu_y_ + 4);
                if (rel_y >= 0) {
                    int idx = rel_y / item_h;
                    if (idx == 0) {
                        show_open_menu_ = false;
                        show_open_recent_menu_ = false;
                        open_file_dialog();
                        return;
                    } else if (idx == 1) {
                        show_open_recent_menu_ = !show_open_recent_menu_;
                        render();
                        return;
                    }
                }
                render();
                return;
            }
        }

        // Crop mode: drag rectangle or handles
        if (crop_active_) {
            if (ev.y > Overlay::kToolbarHeight && decoded_image_.width > 0) {
                int img_x, img_y;
                win_to_img(ev.x, ev.y, img_x, img_y);

                // Check corner handles (in image coords)
                int hl = 10;
                auto near_handle = [&](int hx, int hy) -> bool {
                    int wx, wy;
                    img_to_win(hx, hy, wx, wy);
                    return abs(ev.x - wx) < hl * 2 && abs(ev.y - wy) < hl * 2;
                };

                crop_drag_handle_ = CropNone;
                if (near_handle(crop_x_, crop_y_)) crop_drag_handle_ = CropTL;
                else if (near_handle(crop_x_ + crop_w_, crop_y_)) crop_drag_handle_ = CropTR;
                else if (near_handle(crop_x_, crop_y_ + crop_h_)) crop_drag_handle_ = CropBL;
                else if (near_handle(crop_x_ + crop_w_, crop_y_ + crop_h_)) crop_drag_handle_ = CropBR;
                else if (img_x >= crop_x_ && img_x < crop_x_ + crop_w_ &&
                         img_y >= crop_y_ && img_y < crop_y_ + crop_h_)
                    crop_drag_handle_ = CropMove;

                if (crop_drag_handle_ != CropNone) {
                    crop_dragging_ = true;
                    crop_drag_start_x_ = img_x;
                    crop_drag_start_y_ = img_y;
                    crop_drag_orig_x_ = crop_x_;
                    crop_drag_orig_y_ = crop_y_;
                    crop_drag_orig_w_ = crop_w_;
                    crop_drag_orig_h_ = crop_h_;
                    return;
                }
            }
            // If click was on toolbar buttons (Apply/Cancel), let them handle it
        }

        // Hit-test toolbar buttons first (covers markup submenus, apply/cancel)
        if (show_toolbar_) {
            for (int i = 0; i < (int)toolbar_buttons_.size(); i++) {
                auto& btn = toolbar_buttons_[i];
                if (ev.x >= btn.x && ev.x < btn.x + btn.w &&
                    ev.y >= btn.y && ev.y < btn.y + btn.h && btn.action) {
                    toolbar_press_idx_ = i;
                    btn.action();  // triggers render(), showing press state
                    return;
                }
            }
        }

        // Markup mode: hue bar drag (before drawing starts)
        if (markup_active_ && !crop_active_ && decoded_image_.width > 0 &&
            ev.x >= hue_bar_x_ && ev.x < hue_bar_x_ + hue_bar_w_ &&
            ev.y >= hue_bar_y_ && ev.y < hue_bar_y_ + hue_bar_h_) {
            hue_bar_dragging_ = true;
            float t = (ev.x - hue_bar_x_) / hue_bar_w_;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            markup_color_ = hsv_to_rgb(t * 360.0f, 1.0f, 1.0f);
            render();
            return;
        }

        // Markup mode: start drawing (after button hit-test and hue bar so submenus take priority)
        if (markup_active_ && !crop_active_) {
            if (ev.y > Overlay::kToolbarHeight && decoded_image_.width > 0) {
                int img_x, img_y;
                win_to_img(ev.x, ev.y, img_x, img_y);
                markup_drawing_ = true;
                markup_drag_start_x_ = (float)img_x;
                markup_drag_start_y_ = (float)img_y;
                auto el = std::make_unique<MarkupElement>();
                el->type = markup_tool_;
                el->color = markup_color_;
                el->thickness = markup_thickness_;
                el->points_x.push_back((float)img_x);
                el->points_y.push_back((float)img_y);
                markup_current_ = std::move(el);
                return;
            }
        }

        // Hit-test thumbnail strip
        bool strip_visible = show_thumbnails_ && (!(fullscreen_ || slideshow_) || show_thumbnails_hover_);
        int strip_h = (strip_visible && decoded_image_.width > 0 && dir_images_.size() > 1)
                          ? ThumbnailStrip::kHeight : 0;
        int strip_y = window_height_ - strip_h;
        if (strip_visible && ev.y >= strip_y && !thumb_entries_.empty()) {
            for (auto& e : thumb_entries_) {
                if (ev.x >= e.x && ev.x < e.x + e.w &&
                    ev.y >= e.y && ev.y < e.y + e.h && e.action) {
                    // Double-click detection: same thumb within 500ms
                    uint32_t now = ev.time;
                    if (e.index == last_thumb_click_index_ &&
                        now - last_thumb_click_time_ < 500) {
                        // Double-click — already navigated on first click,
                        // just open sidebar for the now-current image
                        last_thumb_click_index_ = -1;
                        if (!show_sidebar_) {
                            show_sidebar_ = true;
                            render();
                        }
                        return;
                    }
                    last_thumb_click_index_ = e.index;
                    last_thumb_click_time_ = now;
                    e.action(); // single-click navigate
                    return;
                }
            }
        }

        // Start drag-to-pan immediately
        if (!show_toolbar_ || ev.y > Overlay::kToolbarHeight) {
            dragging_ = true;
            drag_start_x_ = ev.x;
            drag_start_y_ = ev.y;
            pan_start_x_ = pan_x_;
            pan_start_y_ = pan_y_;
        }
    } else if (ev.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (active_slider_) {
            active_slider_->handlePointerUp(ev.x, ev.y);
            active_slider_ = nullptr;
            return;
        }
        if (toolbar_press_idx_ >= 0) {
            toolbar_press_idx_ = -1;
            pending_redraw_ = true;
        }
        if (dragging_) {
            dragging_ = false;
        }
        if (hue_bar_dragging_) {
            hue_bar_dragging_ = false;
        }
        if (crop_dragging_) {
            crop_dragging_ = false;
        }
        if (markup_drawing_) {
            markup_drawing_ = false;
            if (markup_current_) {
                auto tool = markup_current_->type;

                //--- Pen: smooth with Catmull-Rom ---
                if (tool == MarkupTool::kPen &&
                    markup_current_->points_x.size() > 3) {
                    auto& px = markup_current_->points_x;
                    auto& py = markup_current_->points_y;
                    size_t n = px.size();

                    std::vector<double> sim_flat;
                    sim_flat.reserve(n * 2);
                    std::vector<double> raw_flat;
                    raw_flat.reserve(n * 2);
                    for (size_t i = 0; i < n; i++) {
                        raw_flat.push_back((double)px[i]);
                        raw_flat.push_back((double)py[i]);
                    }
                    psimpl::simplify_radial_distance<2>(raw_flat.begin(), raw_flat.end(),
                                                        1.0, std::back_inserter(sim_flat));
                    size_t sim_n = sim_flat.size() / 2;

                    if (sim_n >= 4) {
                        try {
                            auto spline = tinyspline::BSpline::interpolateCatmullRom(
                                sim_flat, 2, 0.5f);
                            int num_samples = std::max(64, (int)sim_n * 4);
                            auto sampled = spline.sample(num_samples);
                            px.clear();
                            py.clear();
                            px.reserve(sampled.size() / 2);
                            py.reserve(sampled.size() / 2);
                            for (size_t i = 0; i < sampled.size(); i += 2) {
                                px.push_back((float)sampled[i]);
                                py.push_back((float)sampled[i + 1]);
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "Catmull-Rom smoothing failed: "
                                      << e.what() << "\n";
                        }
                    }
                }

                //--- Rect/Ellipse: no point list needed, use rect_x/y/w/h ---
                if (tool == MarkupTool::kRect || tool == MarkupTool::kEllipse) {
                    markup_current_->points_x.clear();
                    markup_current_->points_y.clear();
                }

                //--- Numbered marker: store number text ---
                if (tool == MarkupTool::kNumbered) {
                    numbered_count_++;
                    markup_current_->text = std::to_string(numbered_count_);
                }

                markup_elements_.push_back(std::move(*markup_current_));
                markup_current_.reset();
            }
            render();
        }
    }
}

void App::on_motion(int x, int y) {
    pointer_x_ = x;
    pointer_y_ = y;

    // Screenshot panel hover tracking
    if (screenshot_panel_active_) {
        screenshot_handle_motion(x, y);
        // Don't return — allow toolbar hover tracking too
    }

    // Upload setup dialog hover tracking
    if (show_upload_setup_) {
        int new_hover = -1;
        int dw = 480, dh = 280;
        int dx = (window_width_ - dw) / 2;
        int dy = (window_height_ - dh) / 2;
        int btn_w = 100, btn_h = 36;
        int btn_y = dy + dh - 20 - btn_h;
        int save_x = dx + dw - 20 - btn_w;
        int cancel_x = save_x - btn_w - 10;
        if (x >= cancel_x && x < cancel_x + btn_w && y >= btn_y && y < btn_y + btn_h)
            new_hover = 0;
        else if (x >= save_x && x < save_x + btn_w && y >= btn_y && y < btn_y + btn_h)
            new_hover = 1;
        if (new_hover != upload_setup_hover_btn_) {
            upload_setup_hover_btn_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 1. Active settings slider drag (highest priority)
    if (active_slider_) {
        if (!show_settings_) {
            active_slider_->handlePointerUp(0, 0);
            active_slider_ = nullptr;
            return;
        }
        active_slider_->handlePointerMove(x, y);
        if (active_slider_ == &bg_alpha_slider_) {
            set_bg_alpha(bg_alpha_slider_.value());
        } else if (active_slider_ == &default_zoom_slider_) {
            set_default_zoom(default_zoom_slider_.value());
        } else if (active_slider_ == &ss_interval_slider_) {
            set_slideshow_interval((int)ss_interval_slider_.value());
        }
        return;
    }

    // 1.5 Hue bar drag
    if (hue_bar_dragging_) {
        float t = (x - hue_bar_x_) / hue_bar_w_;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        markup_color_ = hsv_to_rgb(t * 360.0f, 1.0f, 1.0f);
        render();
        return;
    }

    // 2. Crop drag
    if (crop_active_ && crop_dragging_) {
        if (decoded_image_.width <= 0) { crop_dragging_ = false; return; }
        int img_x, img_y;
        win_to_img(x, y, img_x, img_y);
        int dx = img_x - crop_drag_start_x_;
        int dy = img_y - crop_drag_start_y_;

        int min_dim = 16;
        switch (crop_drag_handle_) {
        case CropMove:
            crop_x_ = std::clamp(crop_drag_orig_x_ + dx, 0, decoded_image_.width - crop_w_);
            crop_y_ = std::clamp(crop_drag_orig_y_ + dy, 0, decoded_image_.height - crop_h_);
            break;
        case CropTL:
            crop_x_ = std::clamp(crop_drag_orig_x_ + dx, 0, crop_drag_orig_x_ + crop_drag_orig_w_ - min_dim);
            crop_y_ = std::clamp(crop_drag_orig_y_ + dy, 0, crop_drag_orig_y_ + crop_drag_orig_h_ - min_dim);
            crop_w_ = crop_drag_orig_w_ + (crop_drag_orig_x_ - crop_x_);
            crop_h_ = crop_drag_orig_h_ + (crop_drag_orig_y_ - crop_y_);
            break;
        case CropTR:
            crop_y_ = std::clamp(crop_drag_orig_y_ + dy, 0, crop_drag_orig_y_ + crop_drag_orig_h_ - min_dim);
            crop_w_ = std::max(min_dim, crop_drag_orig_w_ + dx);
            crop_h_ = crop_drag_orig_h_ + (crop_drag_orig_y_ - crop_y_);
            break;
        case CropBL:
            crop_x_ = std::clamp(crop_drag_orig_x_ + dx, 0, crop_drag_orig_x_ + crop_drag_orig_w_ - min_dim);
            crop_w_ = crop_drag_orig_w_ + (crop_drag_orig_x_ - crop_x_);
            crop_h_ = std::max(min_dim, crop_drag_orig_h_ + dy);
            break;
        case CropBR:
            crop_w_ = std::max(min_dim, crop_drag_orig_w_ + dx);
            crop_h_ = std::max(min_dim, crop_drag_orig_h_ + dy);
            break;
        default: break;
        }
        // Clamp to image bounds
        int max_x = decoded_image_.width - crop_w_;
        if (crop_x_ < 0) crop_x_ = 0;
        else if (crop_x_ > max_x) crop_x_ = max_x;
        int max_y = decoded_image_.height - crop_h_;
        if (crop_y_ < 0) crop_y_ = 0;
        else if (crop_y_ > max_y) crop_y_ = max_y;
        if (crop_w_ > decoded_image_.width) crop_w_ = decoded_image_.width;
        if (crop_h_ > decoded_image_.height) crop_h_ = decoded_image_.height;

        render();
        return;
    }

    // 2.5 Markup draw
    if (markup_active_ && markup_drawing_ && markup_current_) {
        int img_x, img_y;
        win_to_img(x, y, img_x, img_y);
        auto tool = markup_current_->type;
        if (tool == MarkupTool::kPen) {
            markup_current_->points_x.push_back((float)img_x);
            markup_current_->points_y.push_back((float)img_y);
        } else if (tool == MarkupTool::kLine || tool == MarkupTool::kArrow) {
            markup_current_->points_x.resize(2);
            markup_current_->points_y.resize(2);
            markup_current_->points_x[1] = (float)img_x;
            markup_current_->points_y[1] = (float)img_y;
        } else if (tool == MarkupTool::kRect || tool == MarkupTool::kEllipse) {
            float dx = (float)img_x - markup_drag_start_x_;
            float dy = (float)img_y - markup_drag_start_y_;
            if (dx >= 0) { markup_current_->rect_x = markup_drag_start_x_; markup_current_->rect_w = dx; }
            else { markup_current_->rect_x = (float)img_x; markup_current_->rect_w = -dx; }
            if (dy >= 0) { markup_current_->rect_y = markup_drag_start_y_; markup_current_->rect_h = dy; }
            else { markup_current_->rect_y = (float)img_y; markup_current_->rect_h = -dy; }
        }
        render();
        return;
    }

    // 2.75 Window menu hover tracking
    if (show_window_menu_) {
        int new_hover = -1;
        if (x >= window_menu_x_ && x < window_menu_x_ + window_menu_w_ &&
            y >= window_menu_y_ && y < window_menu_y_ + window_menu_h_) {
            int item_h = 36;
            int rel_y = y - (window_menu_y_ + 4);
            if (rel_y >= 0) new_hover = rel_y / item_h;
        }
        if (new_hover != window_menu_hover_) {
            window_menu_hover_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 2.8 Screen menu hover tracking
    if (show_screen_menu_) {
        int new_hover = -1;
        if (x >= screen_menu_x_ && x < screen_menu_x_ + screen_menu_w_ &&
            y >= screen_menu_y_ && y < screen_menu_y_ + screen_menu_h_) {
            int item_h = 36;
            int rel_y = y - (screen_menu_y_ + 4);
            if (rel_y >= 0) new_hover = rel_y / item_h;
        }
        if (new_hover != screen_menu_hover_) {
            screen_menu_hover_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 2.9 Upload menu hover tracking
    if (show_upload_menu_) {
        int new_hover = -1;
        if (x >= upload_menu_x_ && x < upload_menu_x_ + upload_menu_w_ &&
            y >= upload_menu_y_ && y < upload_menu_y_ + upload_menu_h_) {
            int item_h = 36;
            int rel_y = y - (upload_menu_y_ + 4);
            if (rel_y >= 0) new_hover = rel_y / item_h;
        }
        if (new_hover != upload_menu_hover_) {
            upload_menu_hover_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 2.10 Open menu hover tracking
    if (show_open_menu_) {
        int new_hover = -1;
        bool in_open = x >= open_menu_x_ && x < open_menu_x_ + open_menu_w_ &&
                       y >= open_menu_y_ && y < open_menu_y_ + open_menu_h_;
        if (in_open) {
            int item_h = 36;
            int rel_y = y - (open_menu_y_ + 4);
            if (rel_y >= 0) new_hover = rel_y / item_h;
        }
        if (new_hover != open_menu_hover_) {
            open_menu_hover_ = new_hover;
            pending_redraw_ = true;
        }

        bool in_recent = show_open_recent_menu_ &&
                         x >= open_recent_menu_x_ && x < open_recent_menu_x_ + open_recent_menu_w_ &&
                         y >= open_recent_menu_y_ && y < open_recent_menu_y_ + open_recent_menu_h_;

        if (new_hover == 1 && !show_open_recent_menu_) {
            show_open_recent_menu_ = true;
            pending_redraw_ = true;
        } else if (new_hover != 1 && show_open_recent_menu_ && !in_recent) {
            show_open_recent_menu_ = false;
            pending_redraw_ = true;
        }
    }

    // 2.11 Open Recent menu hover tracking
    if (show_open_recent_menu_) {
        int new_hover = -1;
        bool in_recent = x >= open_recent_menu_x_ && x < open_recent_menu_x_ + open_recent_menu_w_ &&
                         y >= open_recent_menu_y_ && y < open_recent_menu_y_ + open_recent_menu_h_;
        if (in_recent) {
            int item_h = 36;
            int rel_y = y - (open_recent_menu_y_ + 4);
            if (rel_y >= 0) new_hover = rel_y / item_h;
        }
        if (new_hover != open_recent_menu_hover_) {
            open_recent_menu_hover_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 3. Toolbar button hover tracking
    {
        int new_hover = -1;
        if (show_toolbar_) {
            for (int i = 0; i < (int)toolbar_buttons_.size(); i++) {
                auto& btn = toolbar_buttons_[i];
                if (x >= btn.x && x < btn.x + btn.w &&
                    y >= btn.y && y < btn.y + btn.h) {
                    new_hover = i;
                    break;
                }
            }
        }
        // Clear press if pointer leaves the pressed button
        if (toolbar_press_idx_ >= 0 && new_hover != toolbar_press_idx_) {
            toolbar_press_idx_ = -1;
            pending_redraw_ = true;
        }
        if (new_hover != toolbar_hover_idx_) {
            toolbar_hover_idx_ = new_hover;
            pending_redraw_ = true;
        }
    }

    // 3. Fullscreen/slideshow toolbar hover zone
    if (fullscreen_ || slideshow_) {
        bool in_zone = y < Overlay::kToolbarHoverZone;
        if (in_zone) {
            if (!show_toolbar_) {
                show_toolbar_ = true;
                pending_redraw_ = true;
            }
            toolbar_hide_time_ = 0;
            return;
        }
        if (show_toolbar_ && toolbar_hide_time_ == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            toolbar_hide_time_ = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        }

        bool in_strip_zone = y >= window_height_ - ThumbnailStrip::kHeight;
        if (in_strip_zone != show_thumbnails_hover_) {
            show_thumbnails_hover_ = in_strip_zone;
            pending_redraw_ = true;
            return;
        }
        if (show_thumbnails_hover_) return;
    }

    // 3. Image drag-to-pan
    if (decoded_image_.width <= 0 || !dragging_) return;

    pan_x_ = pan_start_x_ + (float)(x - drag_start_x_);
    pan_y_ = pan_start_y_ + (float)(y - drag_start_y_);
    pending_redraw_ = true;
}

void App::on_scroll(const ScrollEvent& ev) {
    // Thumbnail strip horizontal scroll
    bool strip_active = show_thumbnails_ && (!(fullscreen_ || slideshow_) || show_thumbnails_hover_);
    int strip_h_scroll = (strip_active && decoded_image_.width > 0 && dir_images_.size() > 1)
                             ? ThumbnailStrip::kHeight : 0;
    if (strip_active && !dir_images_.empty() &&
        pointer_y_ >= window_height_ - strip_h_scroll) {
        int entry_w = ThumbnailStrip::kThumbW + ThumbnailStrip::kGap;
        int max_scroll = std::max(0, (int)dir_images_.size() * entry_w + ThumbnailStrip::kMargin - window_width_);
        thumb_scroll_ += ev.discrete_x * entry_w;
        if (ev.discrete_y != 0) thumb_scroll_ += ev.discrete_y * entry_w;
        thumb_scroll_ = std::max(0, std::min(thumb_scroll_, max_scroll));
        invalidate_thumb_strip();
        render();
        return;
    }

    if (decoded_image_.width <= 0) return;
    if (ev.discrete_y > 0) zoom_out();
    else if (ev.discrete_y < 0) zoom_in();
}

// --- Menu ---

void App::toggle_menu() {
    show_menu_ = !show_menu_;
    show_open_menu_ = false;
    show_open_recent_menu_ = false;
    if (show_menu_) {
        show_settings_ = false;
    }
    render();
}

// --- View state ---

void App::toggle_fullscreen() {
    if (!xdg_toplevel_) return;
    fullscreen_ = !fullscreen_;
    show_toolbar_ = !fullscreen_;
    toolbar_hide_time_ = 0;
    if (fullscreen_) {
        xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(xdg_toplevel_);
    }
    render();
}

void App::toggle_slideshow() {
    slideshow_ = !slideshow_;
    if (slideshow_) {
        std::cout << "Slideshow started\n";
    } else {
        std::cout << "Slideshow stopped\n";
    }
}

void App::toggle_overlay() {
    show_overlay_ = !show_overlay_;
    render();
}

void App::toggle_settings() {
    show_settings_ = !show_settings_;
    if (!show_settings_ && active_slider_) {
        active_slider_->handlePointerUp(0, 0);
        active_slider_ = nullptr;
    }
    render();
}

void App::toggle_sidebar() {
    show_sidebar_ = !show_sidebar_;
    render();
}

void App::set_bg_alpha(float a) {
    bg_alpha_ = std::max(0.0f, std::min(1.0f, a));
    config_.bg_alpha = bg_alpha_;
    Config::save(config_);
    render();
}

void App::set_slideshow_interval(int ms) {
    ms = std::max(1000, std::min(30000, ms));
    config_.slideshow_interval_ms = ms;
    Config::save(config_);
    render();
}

void App::toggle_color_management() {
    config_.enable_color_management = !config_.enable_color_management;
    Config::save(config_);
    render();
}

void App::set_default_zoom(float z) {
    z = std::max(0.5f, std::min(5.0f, z));
    config_.default_zoom = z;
    Config::save(config_);
    render();
}

void App::toggle_theme() {
    config_.theme = (config_.theme == "dark") ? "light" : "dark";
    Config::save(config_);
    render();
}

void App::toggle_imgur_direct_link() {
    config_.imgur_direct_link = !config_.imgur_direct_link;
    Config::save(config_);
    render();
}

void App::toggle_imgur_open_browser() {
    config_.imgur_open_browser = !config_.imgur_open_browser;
    Config::save(config_);
    render();
}

void App::toggle_imgur_auto_copy() {
    config_.imgur_auto_copy = !config_.imgur_auto_copy;
    Config::save(config_);
    render();
}

void App::set_active_settings_tab(int tab) {
    active_settings_tab_ = tab;
    render();
}

// --- Rotation / Flip ---

void App::rotate_90_cw() {
    rotation_ = (rotation_ + 90) % 360;
    image_modified_ = true;
    render();
}

void App::rotate_90_ccw() {
    rotation_ = (rotation_ - 90 + 360) % 360;
    image_modified_ = true;
    render();
}

void App::flip_horizontal() {
    flip_h_ = !flip_h_;
    image_modified_ = true;
    render();
}

void App::flip_vertical() {
    flip_v_ = !flip_v_;
    image_modified_ = true;
    render();
}

int App::run() {
    struct pollfd fds[2];
    fds[0].fd = wl_display_get_fd(conn_.display());
    fds[0].events = POLLIN;

    int dbus_fd = portal_dialog_.get_fd();
    fds[1].fd = dbus_fd;
    fds[1].events = POLLIN;

    int nfds = dbus_fd >= 0 ? 2 : 1;

    std::cout << "Horizon Photo Viewer started\n";

    while (running_) {
        wl_display_flush(conn_.display());

        int timeout = slideshow_ ? config_.slideshow_interval_ms : 100;

#ifdef HAVE_LIBCURL
        // Poll more frequently during upload for smoother progress updates
        if (upload_state_) {
            float p = upload_state_->progress.load(std::memory_order_relaxed);
            if (p >= 0.0f && p < 2.0f) {
                timeout = std::min(timeout, 50);
            }
        }
#endif
        int ret = poll(fds, nfds, timeout);
        if (ret < 0) break;

        if (ret > 0) {
            if (nfds > 1 && (fds[1].revents & POLLIN)) {
                portal_dialog_.dispatch();
            }
            if (fds[0].revents & POLLIN) {
                wl_display_dispatch(conn_.display());
            }
        } else if (ret == 0 && slideshow_) {
            next_image();
        }

        // Toolbar auto-hide: check if 2s have elapsed since cursor left zone
        if (toolbar_hide_time_ > 0 && (fullscreen_ || slideshow_)) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            static constexpr int64_t kToolbarHideDelayMs = 2000;
            if (now - toolbar_hide_time_ >= kToolbarHideDelayMs) {
                toolbar_hide_time_ = 0;
                show_toolbar_ = false;
                pending_redraw_ = true;
            }
        }

        process_thumb_batch();

        if (screenshot_capture_pending_.exchange(false)) {
            if (screenshot_preview_) {
                cairo_surface_destroy(screenshot_preview_);
                screenshot_preview_ = nullptr;
            }
            pending_redraw_ = true;
        }

        if (screenshot_open_pending_.exchange(false)) {
            screenshot_panel_active_ = false;
            open_file(screenshot_open_path_);
            pending_redraw_ = true;
        }

#ifdef HAVE_LIBCURL
        // Upload completion check
        if (upload_state_ && upload_thread_.joinable()) {
            float p = upload_state_->progress.load(std::memory_order_acquire);
            if (p == 2.0f) {
                upload_thread_.join();
                auto r = std::move(upload_state_->result);
                upload_state_.reset();

                if (r.success) {
                    std::string copy_url = config_.imgur_direct_link ? r.url : r.page_url;

                    if (config_.imgur_auto_copy) {
                        ensure_screenshot_clipboard();
                        if (screenshot_clipboard_.is_available()) {
                            screenshot_clipboard_.copy_data("text/plain", copy_url);
                        }
                    }

                    if (config_.imgur_open_browser) {
                        pid_t pid = fork();
                        if (pid == 0) {
                            execlp("xdg-open", "xdg-open", r.url.c_str(), nullptr);
                            _exit(1);
                        }
                    }

                    toast_message_ = "Uploaded: " + copy_url;
                } else {
                    toast_message_ = "Upload failed: " + r.error;
                }

                pending_redraw_ = true;
            } else if (p >= 0.0f) {
                // Still uploading — keep redrawing for progress bar updates
                pending_redraw_ = true;
            }
        }
#endif

        if (pending_redraw_) {
            render();
        }
    }

    return 0;
}

// --- Upload ---

#ifdef HAVE_LIBCURL
void App::upload_image() {
    if (decoded_image_.rgba.empty()) return;
    if (upload_state_) return; // already uploading

    const auto& client_id = config_.imgur_client_id.empty()
        ? Config::kDefaultImgurClientId
        : config_.imgur_client_id;

    std::string png = render_current_image_to_png();
    if (png.empty()) {
        toast_message_ = "Upload: failed to render image";
        render();
        return;
    }

    auto state = std::make_shared<UploadState>();
    upload_state_ = state;

    upload_thread_ = std::thread([state, png, client_id]() {
        state->result = upload_to_imgur(png, client_id, &state->progress);
        state->progress.store(2.0f, std::memory_order_release);
    });

    render();
}
#else
void App::upload_image() {
    toast_message_ = "Upload: libcurl not available. Rebuild with -Dcurl=enabled";
    render();
}
#endif

// --- Screenshot methods ---

void App::screenshot_screen() {
    std::string out_path = "/tmp/horizon-capture.png";
    bool ok = hpv::sc::capture_screen(conn_, out_path);
    if (ok) open_file(out_path);
}

void App::screenshot_window() {
    screenshot_source_ = hpv::sc::Source::Window;
    screenshot_sel_output_ = -1;
    screenshot_sel_window_ = -1;
    screenshot_capture_all_ = false;
    if (!screenshot_panel_active_) {
        screenshot_panel_active_ = true;
    }
    refresh_screenshot_lists();
    render();
}

void App::screenshot_focused() {
    std::string out_path = "/tmp/horizon-capture.png";
    bool ok = hpv::sc::capture_focused_window(conn_, out_path);
    if (ok) open_file(out_path);
}

void App::screenshot_selection() {
    std::string out_path = "/tmp/horizon-capture.png";
    conn_.refresh_logical_outputs();
    auto bounds = conn_.logical_output_bounds();
    bool ok = hpv::sc::capture_selection_interactive(conn_, bounds, out_path);
    if (ok) {
        wl_display_roundtrip(conn_.display());
        open_file(out_path);
    }
}

struct PngWriteState {
    std::string data;
};

static cairo_status_t png_write_callback(void* closure, const unsigned char* data, unsigned int length) {
    auto* state = static_cast<PngWriteState*>(closure);
    state->data.append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

std::string App::render_current_image_to_png() {
    if (decoded_image_.rgba.empty()) return {};

    int w = decoded_image_.width;
    int h = decoded_image_.height;

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return {};

    uint8_t* dst = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            int di = y * stride + x * 4;
            // decoded_image_.rgba is now BGRA (swizzled in-place after thumbnail gen)
            uint8_t b = decoded_image_.rgba[si + 0];
            uint8_t g = decoded_image_.rgba[si + 1];
            uint8_t r = decoded_image_.rgba[si + 2];
            uint8_t a = decoded_image_.rgba[si + 3];
            uint16_t alpha = a;
            dst[di + 0] = (uint8_t)((uint16_t)b * alpha / 255);
            dst[di + 1] = (uint8_t)((uint16_t)g * alpha / 255);
            dst[di + 2] = (uint8_t)((uint16_t)r * alpha / 255);
            dst[di + 3] = a;
        }
    }
    cairo_surface_mark_dirty(surf);

    if (!markup_elements_.empty()) {
        cairo_t* cr = cairo_create(surf);
        cairo_scale(cr, (double)w / orig_img_w_, (double)h / orig_img_h_);
        draw_markup_elements(cr);
        cairo_destroy(cr);
    }

    PngWriteState state;
    cairo_surface_write_to_png_stream(surf, png_write_callback, &state);
    cairo_surface_destroy(surf);

    return state.data;
}

void App::ensure_screenshot_clipboard() {
    if (!screenshot_clipboard_inited_) {
        screenshot_clipboard_inited_ = true;
        if (auto* ext = conn_.ext_data_control_manager()) {
            screenshot_clipboard_.bind_ext(ext, conn_.seat(), conn_.display());
        } else if (auto* wlr = conn_.wlr_data_control_manager()) {
            screenshot_clipboard_.bind_wlr(wlr, conn_.seat(), conn_.display());
        }
    }
}

void App::screenshot_copy() {
    ensure_screenshot_clipboard();
    if (!screenshot_clipboard_.is_available()) return;

    if (!decoded_image_.rgba.empty()) {
        std::string png = render_current_image_to_png();
        if (!png.empty()) {
            screenshot_clipboard_.copy_data("image/png", std::move(png));
            return;
        }
    }

    // Fallback: capture fresh screenshot
    std::string out_path = "/tmp/horizon-capture.png";
    bool ok = hpv::sc::capture_screen(conn_, out_path);
    if (ok) {
        FILE* f = fopen(out_path.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            rewind(f);
            std::string png_data(static_cast<std::string::size_type>(fsize), '\0');
            size_t nread = fread(png_data.data(), 1, static_cast<size_t>(fsize), f);
            fclose(f);
            if (static_cast<long>(nread) == fsize) {
                screenshot_clipboard_.copy_data("image/png", std::move(png_data));
            }
        }
        unlink(out_path.c_str());
    }
}

// --- Screenshot panel ---

void App::toggle_screenshot_panel() {
    screenshot_panel_active_ = !screenshot_panel_active_;
    SC_LOG("toggle_screenshot_panel: active=%d", (int)screenshot_panel_active_);
    show_window_menu_ = false;
    show_screen_menu_ = false;
    if (screenshot_panel_active_) {
        if (!screenshot_icon_cache_inited_) {
            screenshot_icon_cache_.set_icon_theme(hpv::sc::detect_system_icon_theme());
            screenshot_icon_cache_inited_ = true;
        }
        refresh_screenshot_lists();
    }
    render();
}

void App::refresh_screenshot_lists() {
    // Refresh output list from Wayland connection
    auto outputs = hpv::sc::list_outputs(conn_);
    screenshot_outputs_ = std::move(outputs);

    // Check if any toplevel protocol is available (already bound during registry_global)
    screenshot_toplevel_avail_ = conn_.has_any_toplevel_list();

    screenshot_windows_.clear();

    // Give the compositor a chance to send pending toplevel events
    wl_display_roundtrip(conn_.display());
    wl_display_roundtrip(conn_.display());

    if (conn_.has_ext_foreign_toplevel_list()) {
        for (const auto& tl : conn_.ext_foreign_toplevels().list()) {
            hpv::sc::WindowEntry entry;
            entry.handle = tl.handle;
            entry.appId = tl.appId;
            entry.title = tl.title;
            entry.identifier = tl.identifier;
            screenshot_windows_.push_back(std::move(entry));
        }
    } else if (conn_.has_wlr_foreign_toplevel_manager()) {
        for (const auto& tl : conn_.wlr_foreign_toplevels().list()) {
            hpv::sc::WindowEntry entry;
            entry.handle = nullptr;
            entry.wlr_handle = tl.handle;
            entry.appId = tl.appId;
            entry.title = tl.title;
            screenshot_windows_.push_back(std::move(entry));
        }
    }

    if (screenshot_sel_window_ >= static_cast<int>(screenshot_windows_.size())) {
        screenshot_sel_window_ = screenshot_windows_.empty() ? -1 : 0;
    }
}

void App::screenshot_trigger_capture() {
    SC_LOG("screenshot_trigger_capture: source=%d capture_all=%d sel_output=%d sel_window=%d",
           (int)screenshot_source_, (int)screenshot_capture_all_,
           screenshot_sel_output_, screenshot_sel_window_);

    auto capture_and_load = [this](const std::string& path, bool ok) {
        if (screenshot_preview_) {
            cairo_surface_destroy(screenshot_preview_);
            screenshot_preview_ = nullptr;
        }
        if (ok) {
            auto img = hpv::sc::load_capture(path);
            {
                std::lock_guard<std::mutex> lock(screenshot_captured_mutex_);
                screenshot_captured_ = std::move(img);
            }
            screenshot_zoom_ = 1.0;
            screenshot_pan_x_ = 0.0;
            screenshot_pan_y_ = 0.0;
            screenshot_last_path_ = path;
        } else {
            screenshot_status_ = "Capture failed";
        }
    };

    std::string out_path = "/tmp/horizon-panel-capture.png";

    // Use the viewer's own connection for all captures (matching Horizon-shot's UI pattern)
    switch (screenshot_source_) {
    case hpv::sc::Source::Focused: {
        bool ok = hpv::sc::capture_focused_window(conn_, out_path);
        capture_and_load(out_path, ok);
        if (ok) screenshot_open_result(out_path);
        return;
    }
    case hpv::sc::Source::Window: {
        if (screenshot_sel_window_ >= 0 &&
            screenshot_sel_window_ < (int)screenshot_windows_.size()) {
            auto* handle = screenshot_windows_[screenshot_sel_window_].handle;
            bool ok = hpv::sc::capture_window_by_handle(conn_, handle, out_path);
            capture_and_load(out_path, ok);
            if (ok) screenshot_open_result(out_path);
        } else {
            screenshot_status_ = "No window selected";
        }
        return;
    }
    case hpv::sc::Source::Screen: {
        bool ok;
        if (screenshot_capture_all_) {
            ok = hpv::sc::capture_all_screens(conn_, out_path);
        } else if (screenshot_sel_output_ >= 0 &&
                   screenshot_sel_output_ < (int)screenshot_outputs_.size()) {
            ok = hpv::sc::capture_output(conn_, screenshot_outputs_[screenshot_sel_output_].output, out_path);
        } else {
            ok = hpv::sc::capture_screen(conn_, out_path);
        }
        capture_and_load(out_path, ok);
        if (ok) screenshot_open_result(out_path);
        return;
    }
    case hpv::sc::Source::Selection: {
        conn_.refresh_logical_outputs();
        auto bounds = conn_.logical_output_bounds();
        bool ok = hpv::sc::capture_selection_interactive(conn_, bounds, out_path);
        if (ok) wl_display_roundtrip(conn_.display());
        capture_and_load(out_path, ok);
        if (ok) screenshot_open_result(out_path);
        return;
    }
    default:
        screenshot_status_ = "Unknown capture source";
        return;
    }
}

void App::screenshot_open_result(const std::string& path) {
    open_file(path);
}

static int screenshot_panel_compute_width(int win_w) {
    return std::min(360, win_w - 100);
}

void App::screenshot_render_panel(cairo_t* cr, int win_w, int win_h) {
    int pw = screenshot_panel_compute_width(win_w);
    int ph = win_h - Overlay::kToolbarHeight;
    int px = win_w - pw;
    int py = Overlay::kToolbarHeight;

    // Store panel geometry for hit-testing
    screenshot_panel_x_ = px;
    screenshot_panel_y_ = py;
    screenshot_panel_w_ = pw;
    screenshot_panel_h_ = ph;

    // Panel background
    cairo_set_source_rgba(cr, m3::surface_r, m3::surface_g, m3::surface_b, 0.95);
    cairo_rectangle(cr, px, py, pw, ph);
    cairo_fill(cr);

    // Title bar
    int title_h = 40;
    cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);
    cairo_move_to(cr, px + 12, py + 26);
    cairo_show_text(cr, "Screenshot");

    // Close button (X)
    int close_x = px + pw - 36;
    int close_y = py + 4;
    cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.6);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, close_x + 10, close_y + 24);
    cairo_show_text(cr, "\u2715");

    // Separator
    cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                          m3::outline_variant_b, 0.5);
    cairo_move_to(cr, px + 8, py + title_h);
    cairo_line_to(cr, px + pw - 8, py + title_h);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    int cx = px + 12;
    int content_y = py + title_h + 12;
    int content_w = pw - 24;

    // --- Source selection (2x2 grid) ---
    int grid_gap = 6;
    int cell_w = (content_w - grid_gap) / 2;
    int cell_h = 34;
    const char* source_names[] = { "Focused", "Window", "Screen", "Selection" };
    hpv::sc::Source source_vals[] = {
        hpv::sc::Source::Focused, hpv::sc::Source::Window,
        hpv::sc::Source::Screen, hpv::sc::Source::Selection
    };
    int grid_y = content_y;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    for (int i = 0; i < 4; i++) {
        int gx = cx + (i % 2) * (cell_w + grid_gap);
        int gy = grid_y + (i / 2) * (cell_h + grid_gap);
        bool selected = (source_vals[i] == screenshot_source_);
        if (selected) {
            cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.15);
        } else if (i == screenshot_hovered_item_ && screenshot_hovered_area_ == 0) {
            cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                  m3::on_surface_variant_b, 0.08);
        } else {
            cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                  m3::on_surface_variant_b, 0.06);
        }
        overlay_.draw_rounded_rect(cr, gx, gy, cell_w, cell_h, 6);
        cairo_fill(cr);
        if (selected) {
            cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 1.0);
        } else {
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
        }
        double tw = 0;
        cairo_text_extents_t te;
        cairo_text_extents(cr, source_names[i], &te);
        tw = te.width;
        cairo_move_to(cr, gx + (cell_w - tw) / 2, gy + cell_h / 2 + 5);
        cairo_show_text(cr, source_names[i]);
    }

    int list_y = grid_y + 2 * (cell_h + grid_gap) + 12;

    // --- List (outputs or windows) ---
    int list_h = ph - (list_y - py) - 60; // leave room for export bar
    if (list_h < 40) list_h = 40;
    int list_item_h = 32;

    if (screenshot_source_ == hpv::sc::Source::Screen) {
        // Output list
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.6);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, cx, list_y - 2);
        cairo_show_text(cr, "Outputs");

        list_y += 14;
        int avail = list_h - 14;
        int max_items = avail / list_item_h;
        int start = 0;
        int count = std::min(max_items, (int)screenshot_outputs_.size() + 1);
        for (int i = start; i < count; i++) {
            int iy = list_y + i * list_item_h;
            bool is_all = (i == 0);
            bool sel = is_all ? screenshot_capture_all_
                              : (i - 1 == screenshot_sel_output_);
            int list_idx = is_all ? -1 : i - 1;

            // Background
            if (sel) {
                cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.12);
            } else if (screenshot_hovered_area_ == 1 && screenshot_hovered_item_ == i) {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, 0.08);
            }
            if (sel || (screenshot_hovered_area_ == 1 && screenshot_hovered_item_ == i)) {
                cairo_rectangle(cr, cx, iy, content_w, list_item_h);
                cairo_fill(cr);
            }

            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
            cairo_set_font_size(cr, 13);
            const char* label = is_all ? "All Screens" : screenshot_outputs_[list_idx].name.c_str();
            cairo_move_to(cr, cx + 8, iy + 20);
            cairo_show_text(cr, label);

            // Resolution text
            if (!is_all) {
                char res[64];
                snprintf(res, sizeof(res), "%dx%d",
                         screenshot_outputs_[list_idx].width,
                         screenshot_outputs_[list_idx].height);
                cairo_set_font_size(cr, 10);
                cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                                      m3::on_surface_b, 0.5);
                cairo_move_to(cr, cx + content_w - 80, iy + 20);
                cairo_show_text(cr, res);
            }
        }
    } else if (screenshot_source_ == hpv::sc::Source::Window) {
        // Window list
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.6);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, cx, list_y - 2);
        cairo_show_text(cr, "Windows");

        list_y += 14;
        int avail = list_h - 14;
        int max_items = avail / list_item_h;
        if (screenshot_windows_.empty()) {
            cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.4);
            cairo_set_font_size(cr, 12);
            const char* msg = screenshot_toplevel_avail_
                ? "No windows found" : "Window list not available";
            cairo_move_to(cr, cx + 8, list_y + 20);
            cairo_show_text(cr, msg);
        } else {
            int count = std::min(max_items, (int)screenshot_windows_.size());
            int icon_size = 20;
            for (int i = 0; i < count; i++) {
                int iy = list_y + i * list_item_h;
                bool sel = (i == screenshot_sel_window_);

                if (sel) {
                    cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.12);
                } else if (screenshot_hovered_area_ == 1 && screenshot_hovered_item_ == i) {
                    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                          m3::on_surface_variant_b, 0.08);
                }
                if (sel || (screenshot_hovered_area_ == 1 && screenshot_hovered_item_ == i)) {
                    cairo_rectangle(cr, cx, iy, content_w, list_item_h);
                    cairo_fill(cr);
                }

                auto& we = screenshot_windows_[i];
                int icon_x = cx + 8;
                int icon_y = iy + (list_item_h - icon_size) / 2;

                const auto* icon = screenshot_icon_cache_.app_icon(we.appId);
                if (icon && icon->surface) {
                    cairo_save(cr);
                    double scale = static_cast<double>(icon_size) / icon->width;
                    cairo_translate(cr, icon_x, icon_y);
                    cairo_scale(cr, scale, scale);
                    cairo_set_source_surface(cr, icon->surface, 0, 0);
                    cairo_paint(cr);
                    cairo_restore(cr);
                } else {
                    cairo_set_source_rgba(cr, 0.25, 0.28, 0.35, 1.0);
                    cairo_arc(cr, icon_x + icon_size / 2, icon_y + icon_size / 2,
                              icon_size / 2, 0, 2 * M_PI);
                    cairo_fill(cr);

                    std::string label = we.appId.empty() ? we.title : we.appId;
                    if (label.empty()) label = "(unnamed)";
                    char initial[2] = {static_cast<char>(std::toupper((unsigned char)label[0])), 0};
                    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                          m3::on_surface_variant_b, 0.7);
                    cairo_set_font_size(cr, 10);
                    cairo_text_extents_t te;
                    cairo_text_extents(cr, initial, &te);
                    cairo_move_to(cr, icon_x + icon_size / 2 - te.width / 2,
                                  icon_y + icon_size / 2 + te.height / 2);
                    cairo_show_text(cr, initial);
                }

                cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 0.87);
                cairo_set_font_size(cr, 13);
                cairo_move_to(cr, cx + 8 + icon_size + 8, iy + 20);
                cairo_show_text(cr, we.title.empty() ? we.appId.c_str() : we.title.c_str());
            }
        }
    }

    // --- Export bar ---
    int export_y = ph - 52;
    int btn_w = (content_w - 8) / 3;
    int btn_h = 36;
    const char* export_labels[] = { "Save", "Copy", "Save As" };
    for (int i = 0; i < 3; i++) {
        int bx = cx + i * (btn_w + 4);
        int by = py + export_y;
        bool hovered = (screenshot_hovered_area_ == 2 && screenshot_hovered_item_ == i);
        if (hovered) {
            cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.15);
        } else {
            cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.1);
        }
        overlay_.draw_rounded_rect(cr, bx, by, btn_w, btn_h, 8);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 1.0);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13);
        double tw2 = 0;
        cairo_text_extents_t te2;
        cairo_text_extents(cr, export_labels[i], &te2);
        tw2 = te2.width;
        cairo_move_to(cr, bx + (btn_w - tw2) / 2, by + btn_h / 2 + 5);
        cairo_show_text(cr, export_labels[i]);
    }
}

bool App::screenshot_handle_click(int x, int y) {
    if (!screenshot_panel_active_) return false;
    SC_LOG("screenshot_handle_click: x=%d y=%d panel=(%d,%d %dx%d)",
           x, y, screenshot_panel_x_, screenshot_panel_y_,
           screenshot_panel_w_, screenshot_panel_h_);

    int cx = screenshot_panel_x_ + 12;
    int pw = screenshot_panel_w_ - 24;

    // Close button (X) hit test
    int close_x = screenshot_panel_x_ + screenshot_panel_w_ - 36;
    int close_y = screenshot_panel_y_ + 4;
    if (x >= close_x && x < close_x + 32 && y >= close_y && y < close_y + 32) {
        SC_LOG("screenshot_handle_click: close button");
        screenshot_panel_active_ = false;
        render();
        return true;
    }

    // Outside panel - close
    if (x < screenshot_panel_x_ || x >= screenshot_panel_x_ + screenshot_panel_w_ ||
        y < screenshot_panel_y_ || y >= screenshot_panel_y_ + screenshot_panel_h_) {
        SC_LOG("screenshot_handle_click: outside panel");
        screenshot_panel_active_ = false;
        render();
        return true;
    }

    // Source grid hit test
    int title_h = 40;
    int grid_gap = 6;
    int cell_w = (pw - grid_gap) / 2;
    int cell_h = 34;
    int grid_y = screenshot_panel_y_ + title_h + 12;
    for (int i = 0; i < 4; i++) {
        int gx = screenshot_panel_x_ + 12 + (i % 2) * (cell_w + grid_gap);
        int gy = grid_y + (i / 2) * (cell_h + grid_gap);
        hpv::sc::Source sv = (hpv::sc::Source)i;
        if (x >= gx && x < gx + cell_w && y >= gy && y < gy + cell_h) {
            SC_LOG("screenshot_handle_click: source grid idx=%d val=%d", i, (int)sv);
            screenshot_source_ = sv;
            screenshot_sel_output_ = -1;
            screenshot_sel_window_ = -1;
            screenshot_capture_all_ = false;
            // Refresh window list if needed
            if (sv == hpv::sc::Source::Window && screenshot_toplevel_avail_ &&
                screenshot_windows_.empty()) {
                refresh_screenshot_lists();
            }
            // Trigger capture immediately for non-window sources
            if (sv != hpv::sc::Source::Window) {
                screenshot_trigger_capture();
            }
            render();
            return true;
        }
    }

    int list_y = grid_y + 2 * (cell_h + grid_gap) + 12 + 14; // after "Outputs"/"Windows" label

    // Output/window list hit test
    int list_item_h = 32;
    int max_items;

    if (screenshot_source_ == hpv::sc::Source::Screen) {
        max_items = std::min(32, (int)screenshot_outputs_.size() + 1);
        for (int i = 0; i < max_items; i++) {
            int iy = list_y + i * list_item_h;
            if (x >= cx && x < cx + pw && y >= iy && y < iy + list_item_h) {
                if (i == 0) {
                    screenshot_capture_all_ = true;
                    screenshot_sel_output_ = -1;
                } else {
                    screenshot_capture_all_ = false;
                    screenshot_sel_output_ = i - 1;
                }
                SC_LOG("screenshot_handle_click: output list i=%d cap_all=%d", i, (int)screenshot_capture_all_);
                screenshot_trigger_capture();
                render();
                return true;
            }
        }
    } else if (screenshot_source_ == hpv::sc::Source::Window) {
        max_items = std::min(32, (int)screenshot_windows_.size());
        for (int i = 0; i < max_items; i++) {
            int iy = list_y + i * list_item_h;
            if (x >= cx && x < cx + pw && y >= iy && y < iy + list_item_h) {
                screenshot_sel_window_ = i;
                SC_LOG("screenshot_handle_click: window list i=%d", i);
                screenshot_trigger_capture();
                render();
                return true;
            }
        }
    }

    // Export buttons hit test
    int export_y = screenshot_panel_y_ + screenshot_panel_h_ - 52;
    int btn_w = (pw - 8) / 3;
    int btn_h = 36;
    for (int i = 0; i < 3; i++) {
        int bx = screenshot_panel_x_ + 12 + i * (btn_w + 4);
        if (x >= bx && x < bx + btn_w && y >= export_y && y < export_y + btn_h) {
            SC_LOG("screenshot_handle_click: export btn i=%d", i);
            if (i == 0) {
                // Save - open the result
                if (!screenshot_last_path_.empty()) {
                    screenshot_open_result(screenshot_last_path_);
                }
            } else if (i == 1) {
                // Copy to clipboard
                bool have_capture;
                {
                    std::lock_guard<std::mutex> lock(screenshot_captured_mutex_);
                    have_capture = screenshot_captured_.valid;
                }
                if (have_capture) {
                    ensure_screenshot_clipboard();
                    if (screenshot_clipboard_.is_available()) {
                        std::string png = render_current_image_to_png();
                        if (!png.empty()) {
                            screenshot_clipboard_.copy_data("image/png", std::move(png));
                        }
                    }
                }
            } else if (i == 2) {
                // Save As
                if (!screenshot_last_path_.empty()) {
                    screenshot_open_result(screenshot_last_path_);
                }
            }
            render();
            return true;
        }
    }

    return true; // consumed by panel
}

bool App::screenshot_handle_motion(int x, int y) {
    if (!screenshot_panel_active_) return false;

    // Reset hover state
    screenshot_hovered_item_ = -1;
    screenshot_hovered_area_ = 0;

    int cx = screenshot_panel_x_ + 12;
    int pw = screenshot_panel_w_ - 24;

    // If outside panel, just update cursor tracking
    if (x < screenshot_panel_x_ || x >= screenshot_panel_x_ + screenshot_panel_w_ ||
        y < screenshot_panel_y_ || y >= screenshot_panel_y_ + screenshot_panel_h_) {
        return true;
    }

    int title_h = 40;
    int grid_gap = 6;
    int cell_w = (pw - grid_gap) / 2;
    int cell_h = 34;
    int grid_y = screenshot_panel_y_ + title_h + 12;

    // Source grid hover
    for (int i = 0; i < 4; i++) {
        int gx = screenshot_panel_x_ + 12 + (i % 2) * (cell_w + grid_gap);
        int gy = grid_y + (i / 2) * (cell_h + grid_gap);
        if (x >= gx && x < gx + cell_w && y >= gy && y < gy + cell_h) {
            screenshot_hovered_item_ = i;
            screenshot_hovered_area_ = 0;
            return true;
        }
    }

    int list_y = grid_y + 2 * (cell_h + grid_gap) + 12 + 14;
    int list_item_h = 32;

    if (screenshot_source_ == hpv::sc::Source::Screen) {
        int max_items = std::min(32, (int)screenshot_outputs_.size() + 1);
        for (int i = 0; i < max_items; i++) {
            int iy = list_y + i * list_item_h;
            if (x >= cx && x < cx + pw && y >= iy && y < iy + list_item_h) {
                screenshot_hovered_item_ = i;
                screenshot_hovered_area_ = 1;
                return true;
            }
        }
    } else if (screenshot_source_ == hpv::sc::Source::Window) {
        int max_items = std::min(32, (int)screenshot_windows_.size());
        for (int i = 0; i < max_items; i++) {
            int iy = list_y + i * list_item_h;
            if (x >= cx && x < cx + pw && y >= iy && y < iy + list_item_h) {
                screenshot_hovered_item_ = i;
                screenshot_hovered_area_ = 1;
                return true;
            }
        }
    }

    // Export buttons hover
    int export_y = screenshot_panel_y_ + screenshot_panel_h_ - 52;
    int btn_w = (pw - 8) / 3;
    int btn_h = 36;
    for (int i = 0; i < 3; i++) {
        int bx = screenshot_panel_x_ + 12 + i * (btn_w + 4);
        if (x >= bx && x < bx + btn_w && y >= export_y && y < export_y + btn_h) {
            screenshot_hovered_item_ = i;
            screenshot_hovered_area_ = 2;
            return true;
        }
    }

    return true;
}

}
