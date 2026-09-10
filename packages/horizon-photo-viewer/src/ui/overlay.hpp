#pragma once

#include "features/theme/theme.hpp"
#include "ui/m3_widgets.hpp"
#include "decode/common/exif.hpp"

#include <cairo.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Material Symbols Rounded codepoints (Private Use Area)
inline constexpr int kIconCodepoints[] = {
    0xE2C7, // folder_open      → Open
    0xE5CB, // chevron_left     → <
    0xE5CC, // chevron_right    → >
    0xE145, // add              → +
    0xE15B, // remove           → -
    0xEA16, // fit_screen       → Fit
    0xE5D0, // fullscreen       → Full
    0xE037, // play_arrow       → Play
    0xE8B8, // settings         → Gear
    0xE88E, // info             → Info
    0xE945, // crop             → Crop
    0xE5D3, // more_vert        → Menu
    0xE746, // draw             → Draw
    0xE419, // rotate_right     → RotR (mirrored from same glyph as RotL)
    0xE419, // rotate_left      → RotL
    0xE5C8, // flip             → Flip
    0,      // screen (SVG)
    0,      // window (SVG)
    0,      // focused (SVG)
    0,      // selection (SVG)
    0,      // copy (SVG)
    0,      // panel (SVG)
    0,      // upload (SVG)
};
inline constexpr const char* kIconLabels[] = {
    "Open", "<", ">", "+", "-", "Fit", "Full", "Play", "Gear", "Info", "Crop", "Menu", "Draw",
    "RotR", "RotL", "Flip",
    "Screen", "Window", "Focused", "Selection", "Copy", "Panel", "Upload",
};
inline constexpr int8_t kToolbarLayout[] = {
    0,      // folder (Open)
    -1,     // spacer
    1,      // chevron_left
    2,      // chevron_right
    -1,     // spacer
    3,      // add
    4,      // remove
    5,      // fit_screen
    -1,     // spacer
    6,      // fullscreen
    7,      // play_arrow
    -1,     // spacer
    10,     // crop
    12,     // draw (markup)
    -1,     // spacer
    13,     // rotate_cw
    14,     // rotate_ccw
    15,     // flip
    -1,     // spacer
    16,     // screen capture
    17,     // window capture
    18,     // focused capture
    19,     // selection capture
};
inline constexpr int kNumToolbarItems = sizeof(kToolbarLayout) / sizeof(kToolbarLayout[0]);
inline constexpr int kSettingsIconIdx = 8;
inline constexpr int kMetadataIconIdx = 9;
inline constexpr int kMenuIconIdx = 11;
inline constexpr int kIconSize = 96;
inline constexpr int kNumIcons = 23;
inline constexpr int kScreenIconIdx = 16;
inline constexpr int kWindowIconIdx = 17;
inline constexpr int kFocusedIconIdx = 18;
inline constexpr int kSelectionIconIdx = 19;
inline constexpr int kCopyIconIdx = 20;
inline constexpr int kPanelIconIdx = 21;
inline constexpr int kUploadIconIdx = 22;

namespace hpv {

struct OverlayState {
    bool show_info = false;
    bool toolbar_visible = true;
    bool fullscreen = false;
    bool slideshow = false;
    bool show_settings = false;
    bool show_sidebar = false;
    bool show_menu = false;
    bool crop_active = false;
    bool markup_active = false;
    float bg_alpha = 1.0f;

    std::string filename;
    int image_width = 0;
    int image_height = 0;
    float zoom = 1.0f;
    int image_index = 0;
    int image_count = 0;

    int slideshow_interval_ms = 5000;
    bool enable_color_management = true;
    float default_zoom = 1.0f;
    std::string theme = "dark";
    std::string imgur_client_id;

    ExifInfo exif;
};

struct OverlayButton {
    int x = 0, y = 0, w = 0, h = 0;
    std::string label;
    std::string tooltip;
    std::function<void()> action;
};

class Overlay {
public:
    ~Overlay();

    void render_overlay(cairo_t* cr, int win_w, int win_h, const OverlayState& state);
    void render_toolbar(cairo_t* cr, int win_w, int win_h,
                        std::vector<OverlayButton>& buttons,
                        int hovered_idx = -1, int pressed_idx = -1,
                        float bg_alpha = 1.0f);
    void render_settings_popup(cairo_t* cr, int win_w, int win_h,
                                OverlayState& state, std::vector<OverlayButton>& buttons,
                                M3Slider& bg_alpha_slider,
                                M3Slider& default_zoom_slider,
                                M3Slider& ss_interval_slider,
                                M3Toggle& theme_toggle,
                                M3Toggle& color_mgmt_toggle,
                                M3Toggle& imgur_direct_toggle,
                                M3Toggle& imgur_open_browser_toggle,
                                M3Toggle& imgur_auto_copy_toggle,
                                int active_settings_tab);
    void render_sidebar(cairo_t* cr, int win_w, int win_h,
                        const OverlayState& state, std::vector<OverlayButton>& buttons);
    void render_crop_overlay(cairo_t* cr, int win_w, int win_h,
                             const OverlayState& state, std::vector<OverlayButton>& buttons);
    void render_markup_overlay(cairo_t* cr, int win_w, int win_h,
                               const OverlayState& state);
    void render_menu_popup(cairo_t* cr, int win_w, int win_h,
                           const OverlayState& state, std::vector<OverlayButton>& buttons);
    void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);

    bool init_icons(const char* font_path, const char* crop_svg_path,
                    const char* flip_svg_path);
    bool init_screenshot_icons(const char* screen_svg, const char* window_svg,
                                const char* focused_svg, const char* selection_svg,
                                const char* copy_svg);
    bool init_panel_icon(const char* panel_svg);
    bool init_upload_icon(const char* upload_svg);
    void destroy_icons();
    bool crop_icon_loaded() const {
        return kNumIcons > 10 && icon_surfaces_.size() > 10 && icon_surfaces_[10] != nullptr;
    }

    static constexpr int kToolbarHeight = 48;
    static constexpr int kToolbarHoverZone = 64;
    static constexpr int kNumIcons = 23;

private:
    void render_placeholder(cairo_t* cr, int win_w, int win_h);
    void render_info(cairo_t* cr, int win_w, int win_h, const OverlayState& state);

    bool ready_icon_from_codepoint(unsigned char* font_data, size_t font_size,
                                   int codepoint, int icon_size);
    bool ready_icon_from_svg(const char* svg_path, int icon_size);

    std::vector<cairo_surface_t*> icon_surfaces_;
    unsigned char* font_data_ = nullptr;
    size_t font_size_ = 0;
};

}
