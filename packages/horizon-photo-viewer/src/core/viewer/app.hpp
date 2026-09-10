#pragma once

#include "common/config/config.hpp"
#include "cache/thumbnail/thumb_cache.hpp"
#include "thread/pool/decode_pool.hpp"
#include "decode/common/svg_doc.hpp"
#include "common/file_utils/trash.hpp"
#include "wayland/core/connection.hpp"
#include "wayland/core/seat.hpp"
#include "wayland/surface/surface_extensions.hpp"
#include "wayland/buffer/shm_buffer.hpp"
#include "render/text/text_renderer.hpp"
#include "dbus/portal/portal_file_dialog.hpp"
#include "decode/core/decoder.hpp"
#include "ui/overlay.hpp"
#include "ui/thumbnail_strip/thumbnail_strip.hpp"
#include "screenshot/app.hpp"
#include "core/screenshot/foreign_toplevels.hpp"
#include "core/screenshot/wlr_foreign_toplevels.hpp"
#include "core/screenshot/icon_cache.hpp"

#include <cairo.h>
#ifdef HAVE_LIBCURL
#include "features/upload/upload.hpp"
#endif
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <list>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <poll.h>

struct wl_surface;
struct xdg_surface;
struct xdg_toplevel;
typedef struct _cairo cairo_t;

namespace hpv {

struct DecodedImage {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    int stride = 0;
};

enum class MarkupTool {
    kPen, kLine, kArrow, kRect, kEllipse,
    kText, kHighlight, kBlur, kNumbered, kEraser
};

struct MarkupElement {
    MarkupTool type = MarkupTool::kPen;

    // All positions in IMAGE pixel coordinates (raw, before pan/zoom)
    std::vector<float> points_x;
    std::vector<float> points_y;

    // Rect/ellipse: x, y, w, h (image coords)
    float rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;

    // Text
    std::string text;
    float text_x = 0, text_y = 0;
    float font_size = 16;

    // Style
    uint32_t color = 0xFF0000FF;  // 0xRRGGBBAA
    float thickness = 3.0f;
    bool filled = false;
};

class App {
public:
    App(int argc, char** argv);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    int run();
    void quit() { running_ = false; }

    // Wayland events
    void set_window_size(int width, int height);
    void on_close();
    void on_key(const KeyEvent& ev);
    void on_pointer(const PointerEvent& ev);
    void on_motion(int x, int y);
    void on_scroll(const ScrollEvent& ev);

    // Buffer release callback
    void on_shm_release();

    // Image loading
    void open_file(std::string path);
    void open_directory(const std::string& path);
    void next_image();
    void prev_image();
    void first_image();
    void last_image();
    void delete_image();

    // View state
    void zoom_in();
    void zoom_out();
    void zoom_fit();
    void zoom_1to1();
    void toggle_fullscreen();
    void toggle_slideshow();
    void toggle_overlay();
    void toggle_settings();
    void toggle_sidebar();
    void toggle_menu();
    void toggle_crop();
    void apply_crop();
    void cancel_crop();
    void rotate_90_cw();
    void rotate_90_ccw();
    void flip_horizontal();
    void flip_vertical();
    void toggle_markup();
    void commit_markup();
    void cancel_markup();
    void undo_markup();
    void set_bg_alpha(float a);
    void set_slideshow_interval(int ms);
    void toggle_color_management();
    void set_default_zoom(float z);
    void toggle_theme();
    void toggle_imgur_direct_link();
    void toggle_imgur_open_browser();
    void toggle_imgur_auto_copy();
    void set_active_settings_tab(int tab);
    void save_image();
    void save_as();
    void save_as_copy();

    // Upload
    void upload_image();

    // Screenshot
    void screenshot_screen();
    void screenshot_window();
    void screenshot_focused();
    void screenshot_selection();
    void screenshot_copy();
    void toggle_screenshot_panel();

    // File dialog
    void open_file_dialog();
    void on_file_dialog_result(const std::string& path);

    // Called by listener trampolines (namespace scope, needs public access)
    void render();
    void present();

private:
    bool init_wayland();
    bool create_window();
    void init_font();
    void update_title();
    void load_image(std::string path);
    void load_directory(const std::string& path);
    std::vector<std::string> image_files_in_dir(const std::string& dir);

    // State
    bool running_ = true;
    bool pending_redraw_ = false;
    Config config_;

    // Wayland
    WaylandConnection conn_;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;
    SurfaceExtensions surface_extensions_;

    // Input
    Seat seat_;
    wl_data_device* data_device_ = nullptr;
    wl_data_offer* drag_offer_ = nullptr;
    std::vector<std::string> drag_mime_types_;

    // Rendering — double-buffered
    ShmBuffer bufs_[2];
    int paint_buf_ = 0;
    TextRenderer text_renderer_;

    // File dialog
    PortalFileDialog portal_dialog_;

    // Window state
    int window_width_ = 1280;
    int window_height_ = 720;
    bool fullscreen_ = false;
    bool slideshow_ = false;
    bool show_overlay_ = false;
    bool show_thumbnails_ = true;

    // Settings
    bool show_settings_ = false;
    float bg_alpha_ = 1.0f;

    // Menu
    bool show_menu_ = false;

    // Window capture dropdown
    bool show_window_menu_ = false;
    int window_menu_hover_ = -1;
    int window_menu_x_ = 0, window_menu_y_ = 0;
    int window_menu_w_ = 0, window_menu_h_ = 0;

    // Screen capture dropdown
    bool show_screen_menu_ = false;
    int screen_menu_hover_ = -1;
    int screen_menu_x_ = 0, screen_menu_y_ = 0;
    int screen_menu_w_ = 0, screen_menu_h_ = 0;

    // Upload submenu
    bool show_upload_menu_ = false;
    int upload_menu_hover_ = -1;
    int upload_menu_x_ = 0, upload_menu_y_ = 0;
    int upload_menu_w_ = 0, upload_menu_h_ = 0;

    // Open submenu (Open New / Open Recent)
    bool show_open_menu_ = false;
    int open_menu_hover_ = -1;
    int open_menu_x_ = 0, open_menu_y_ = 0;
    int open_menu_w_ = 0, open_menu_h_ = 0;

    // Open Recent sub-submenu
    bool show_open_recent_menu_ = false;
    int open_recent_menu_hover_ = -1;
    int open_recent_menu_x_ = 0, open_recent_menu_y_ = 0;
    int open_recent_menu_w_ = 0, open_recent_menu_h_ = 0;

    // Upload setup dialog
    bool show_upload_setup_ = false;
    std::string upload_setup_input_;
    int upload_setup_hover_btn_ = -1; // -1 = none, 0 = cancel, 1 = save

#ifdef HAVE_LIBCURL
    struct UploadState {
        std::atomic<float> progress{-1.0f};
        ImgurUploadResult result;
    };
    std::shared_ptr<UploadState> upload_state_;
    std::thread upload_thread_;
#endif

    // Toast notification
    std::string toast_message_;
    uint64_t toast_start_ms_ = 0;

    // Crop state
    bool crop_active_ = false;
    int crop_x_ = 0, crop_y_ = 0;
    int crop_w_ = 0, crop_h_ = 0;
    bool crop_dragging_ = false;
    int crop_drag_start_x_ = 0, crop_drag_start_y_ = 0;
    int crop_drag_orig_x_ = 0, crop_drag_orig_y_ = 0;
    int crop_drag_orig_w_ = 0, crop_drag_orig_h_ = 0;
    enum CropHandle { CropNone, CropMove, CropTL, CropTR, CropBL, CropBR };
    CropHandle crop_drag_handle_ = CropNone;
    bool image_modified_ = false;

    // Rotation / flip state
    int rotation_ = 0;  // 0, 90, 180, 270
    bool flip_h_ = false;
    bool flip_v_ = false;

    // Markup state
    bool markup_active_ = false;
    MarkupTool markup_tool_ = MarkupTool::kPen;
    uint32_t markup_color_ = 0xFF0000FF; // default red, 0xRRGGBBAA
    float markup_thickness_ = 3.0f;
    std::vector<MarkupElement> markup_elements_;
    std::unique_ptr<MarkupElement> markup_current_;
    std::vector<MarkupElement> markup_undo_stack_;
    std::vector<MarkupElement> markup_redo_stack_;
    bool markup_drawing_ = false;
    float markup_drag_start_x_ = 0;
    float markup_drag_start_y_ = 0;
    bool hue_bar_dragging_ = false;
    float hue_bar_x_ = 0, hue_bar_y_ = 0;
    float hue_bar_w_ = 280, hue_bar_h_ = 24;
    int numbered_count_ = 0;

    // Sidebar
    bool show_sidebar_ = false;

    // Double-click tracking
    int last_thumb_click_index_ = -1;
    uint32_t last_thumb_click_time_ = 0;

    // Thumbnail strip hover (fullscreen/slideshow)
    bool show_thumbnails_hover_ = false;

    // Image data
    std::string current_path_;
    std::string current_dir_;
    std::vector<std::string> dir_images_;
    int dir_image_index_ = -1;
    DecodedImage decoded_image_;
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f;
    float pan_y_ = 0.0f;
    ExifInfo exif_info_;

    // Decoder
    DecoderRegistry decoders_;

    // Background prefetch
    DecodePool decode_pool_;

    // SVG source data and cached parse for vector rendering
    std::vector<uint8_t> svg_source_data_;
    struct SvgDoc* svg_doc_ = nullptr;
    bool loading_ = false; // guard against re-entrant load_image from frame callbacks

    // Vector cache (Cairo path rendering at display resolution, rebuilt on zoom)
    cairo_surface_t* svg_vector_cache_ = nullptr;
    int svg_vector_w_ = 0, svg_vector_h_ = 0;
    float orig_img_w_ = 0;
    float orig_img_h_ = 0;

    // Drag-to-pan state
    bool dragging_ = false;
    int drag_start_x_ = 0;
    int drag_start_y_ = 0;
    float pan_start_x_ = 0.0f;
    float pan_start_y_ = 0.0f;

    // Toolbar button hover / press tracking
    int toolbar_hover_idx_ = -1;
    int toolbar_press_idx_ = -1;

    // Pointer tracking (for scroll-to-position decisions)
    int pointer_x_ = 0;
    int pointer_y_ = 0;

    // Toolbar (populated during render for hit-testing)
    bool show_toolbar_ = true;
    int64_t toolbar_hide_time_ = 0;  // timestamp (ms) when cursor left hover zone; 0 = no pending hide
    std::vector<OverlayButton> toolbar_buttons_;
    Overlay overlay_;

    // Screenshot panel state
    bool screenshot_panel_active_ = false;
    hpv::sc::Source screenshot_source_ = hpv::sc::Source::Screen;
    int screenshot_sel_output_ = -1;
    int screenshot_sel_window_ = -1;
    bool screenshot_capture_all_ = false;
    std::vector<hpv::sc::OutputInfo> screenshot_outputs_;
    std::vector<hpv::sc::WindowEntry> screenshot_windows_;
    bool screenshot_toplevel_avail_ = false;
    hpv::sc::CapturedImage screenshot_captured_;
    cairo_surface_t* screenshot_preview_ = nullptr;
    double screenshot_zoom_ = 1.0;
    double screenshot_pan_x_ = 0.0;
    double screenshot_pan_y_ = 0.0;
    bool screenshot_dragging_ = false;
    int screenshot_drag_start_x_ = 0;
    int screenshot_drag_start_y_ = 0;
    double screenshot_drag_pan_x_ = 0.0;
    double screenshot_drag_pan_y_ = 0.0;
    int screenshot_hovered_item_ = -1;
    int screenshot_hovered_area_ = 0;
    int screenshot_pressed_item_ = -1;
    int screenshot_pressed_area_ = 0;
    std::string screenshot_status_;
    std::string screenshot_last_path_;
    int screenshot_panel_x_ = 0, screenshot_panel_y_ = 0;
    int screenshot_panel_w_ = 0, screenshot_panel_h_ = 0;

    std::atomic<bool> screenshot_capture_pending_ = false;
    std::atomic<bool> screenshot_open_pending_ = false;
    std::string screenshot_open_path_;
    std::mutex screenshot_captured_mutex_;
    hpv::sc::ClipboardService screenshot_clipboard_;
    bool screenshot_clipboard_inited_ = false;
    hpv::sc::IconCache screenshot_icon_cache_;
    bool screenshot_icon_cache_inited_ = false;

    std::string render_current_image_to_png();
    void ensure_screenshot_clipboard();
    void refresh_screenshot_lists();
    void screenshot_trigger_capture();
    void screenshot_render_panel(cairo_t* cr, int win_w, int win_h);
    bool screenshot_handle_click(int x, int y);
    bool screenshot_handle_motion(int x, int y);
    void screenshot_open_result(const std::string& path);

    // M3 widgets for settings popup
    M3Slider bg_alpha_slider_;
    M3Slider default_zoom_slider_;
    M3Slider ss_interval_slider_;
    M3Toggle theme_toggle_;
    M3Toggle color_mgmt_toggle_;
    M3Toggle imgur_direct_toggle_;
    M3Toggle imgur_open_browser_toggle_;
    M3Toggle imgur_auto_copy_toggle_;
    M3Slider* active_slider_ = nullptr;
    int active_settings_tab_ = 0;

    // Thumbnail strip
    ThumbnailStrip thumbnail_strip_;
    std::vector<ThumbnailEntry> thumb_entries_;
    int thumb_scroll_ = 0;
    cairo_surface_t* cached_strip_ = nullptr;
    int cached_strip_w_ = 0;
    bool thumb_dirty_ = true;

    // Thumbnail cache: index -> small BGRA pixels
    static constexpr size_t kThumbCacheMaxBytes = 64 * 1024 * 1024; // 64 MB
    std::map<int, std::vector<uint8_t>> thumb_cache_;
    std::map<int, int> thumb_cache_w_, thumb_cache_h_;
    size_t thumb_cache_bytes_ = 0;
    std::list<int> thumb_cache_order_;  // front = most recently used
    std::vector<int> thumb_pending_;  // indices queued for background decoding

    void evict_thumb_cache();

    void load_thumbnail(int index);
    void process_thumb_batch(int count = 3);
    void queue_thumb_preload(int from, int to);
    void img_to_win(int img_x, int img_y, int& win_x, int& win_y) const;
    void win_to_img(int win_x, int win_y, int& img_x, int& img_y) const;
    void draw_crop_rect(cairo_t* cr, int win_w, int win_h);
    void draw_markup_elements(cairo_t* cr);
    void write_png_file(const std::string& path);
    void save_dialog_(bool as_copy);
    void gen_thumb_bgra(const std::vector<uint8_t>& rgba, int w, int h,
                        std::vector<uint8_t>& out, int& out_w, int& out_h);
    void invalidate_thumb_strip();
    void destroy_cached_strip();

    // Data device (drag-and-drop) handlers
    static void handle_data_offer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void handle_data_enter(void* data, wl_data_device* device, uint32_t serial,
                                  wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                                  wl_data_offer* offer);
    static void handle_data_leave(void* data, wl_data_device* device);
    static void handle_data_motion(void* data, wl_data_device* device, uint32_t time,
                                   wl_fixed_t x, wl_fixed_t y);
    static void handle_data_drop(void* data, wl_data_device* device);
    static void handle_data_selection(void* data, wl_data_device* device,
                                      wl_data_offer* offer);

    void on_data_drop(uint32_t serial, wl_data_offer* offer);
};

}
