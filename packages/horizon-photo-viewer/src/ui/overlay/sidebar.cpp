#include "ui/overlay.hpp"

#include <cstdio>
#include <sstream>

namespace hpv {

void Overlay::render_sidebar(cairo_t* cr, int win_w, int win_h,
                              const OverlayState& state, std::vector<OverlayButton>& buttons) {
    int sw = 320;
    int sx = win_w - sw;
    int sh = win_h;
    int top_off = kToolbarHeight;

    // Sidebar background — surface
    cairo_set_source_rgba(cr, m3::surface_r, m3::surface_g, m3::surface_b, 1.0);
    cairo_rectangle(cr, sx, 0, sw, sh);
    cairo_fill(cr);

    // Left outline — outline
    cairo_set_source_rgba(cr, m3::outline_r, m3::outline_g, m3::outline_b, 0.5);
    cairo_move_to(cr, sx, top_off);
    cairo_line_to(cr, sx, sh);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // Header row: title + close icon button
    int header_h = 44;
    int pad = 16;

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);
    cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 1.0);
    cairo_move_to(cr, sx + pad, top_off + header_h - 12);
    cairo_show_text(cr, "Metadata");

    // Close button — M3 standard icon button
    int cb_size = 36;
    int cb_x = sx + sw - pad - cb_size;
    int cb_y = top_off + (header_h - cb_size) / 2;
    // Draw close mark
    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                          m3::on_surface_variant_b, 1.0);
    cairo_set_line_width(cr, 2);
    int cm = 10;
    int c_cx = cb_x + cb_size / 2;
    int c_cy = cb_y + cb_size / 2;
    cairo_move_to(cr, c_cx - cm, c_cy - cm);
    cairo_line_to(cr, c_cx + cm, c_cy + cm);
    cairo_move_to(cr, c_cx + cm, c_cy - cm);
    cairo_line_to(cr, c_cx - cm, c_cy + cm);
    cairo_stroke(cr);
    buttons.push_back({cb_x, cb_y, cb_size, cb_size, "CloseSidebar", {}, {}});

    // Divider
    int div_y = top_off + header_h;
    cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                          m3::outline_variant_b, 0.5);
    cairo_move_to(cr, sx + pad, div_y);
    cairo_line_to(cr, sx + sw - pad, div_y);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    int ly = div_y + 16;
    int left_col = sx + pad;
    int val_col = sx + 128;
    int val_max_w = sw - 144;
    int line_h = 20;

    auto field = [&](const char* label, const std::string& value,
                     bool wrap = false, int wrap_lines = 2) {
        if (value.empty()) return;
        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.9);
        cairo_move_to(cr, left_col, ly);
        cairo_show_text(cr, label);

        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 1.0);
        if (wrap) {
            std::string remain = value;
            int lines_used = 0;
            for (int line = 0; line < wrap_lines && !remain.empty(); line++) {
                int fit = 0;
                for (size_t i = 1; i <= remain.size(); i++) {
                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, remain.substr(0, i).c_str(), &ext);
                    if (ext.width > val_max_w) break;
                    fit = (int)i;
                }
                if (fit == 0) fit = 1;

                std::string display;
                if (line == wrap_lines - 1 && (size_t)fit < remain.size()) {
                    while (fit > 0) {
                        std::string s = remain.substr(0, fit) + "...";
                        cairo_text_extents_t ext;
                        cairo_text_extents(cr, s.c_str(), &ext);
                        if (ext.width <= val_max_w) break;
                        fit--;
                    }
                    display = remain.substr(0, std::max(0, fit)) + "...";
                    remain.clear();
                } else {
                    display = remain.substr(0, fit);
                    remain = remain.substr(fit);
                }
                cairo_move_to(cr, val_col, ly + line * line_h);
                cairo_show_text(cr, display.c_str());
                lines_used = line + 1;
            }
            ly += line_h * lines_used;
        } else {
            cairo_move_to(cr, val_col, ly);
            cairo_show_text(cr, value.c_str());
            ly += line_h;
        }
    };

    auto section_header = [&](const char* text) {
        cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.9);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, left_col, ly);
        cairo_show_text(cr, text);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        ly += line_h;
    };

    // File info
    if (!state.filename.empty()) {
        field("File", state.filename, true, 2);
    }
    if (state.image_width > 0) {
        char dim[32];
        snprintf(dim, sizeof(dim), "%d x %d", state.image_width, state.image_height);
        field("Dimensions", dim);
    }

    if (!state.exif.image_description.empty())
        field("Description", state.exif.image_description, true, 2);
    field("Camera Make", state.exif.make);
    field("Camera Model", state.exif.model);
    field("Lens", state.exif.lens);
    if (!state.exif.focal_length.empty())
        field("Focal Length", state.exif.focal_length + " mm");
    if (!state.exif.aperture.empty())
        field("Aperture", "f/" + state.exif.aperture);
    if (!state.exif.exposure.empty())
        field("Exposure", state.exif.exposure + " s");
    if (!state.exif.exposure_bias.empty())
        field("Exposure Bias", state.exif.exposure_bias + " EV");
    if (!state.exif.iso.empty())
        field("ISO", state.exif.iso);
    if (!state.exif.flash.empty())
        field("Flash", state.exif.flash);
    if (!state.exif.metering_mode.empty())
        field("Metering", state.exif.metering_mode);
    if (!state.exif.exposure_mode.empty())
        field("Exposure Mode", state.exif.exposure_mode);
    if (!state.exif.exposure_program.empty())
        field("Program", state.exif.exposure_program);
    if (!state.exif.white_balance.empty())
        field("White Balance", state.exif.white_balance);
    if (!state.exif.color_space.empty())
        field("Color Space", state.exif.color_space);
    if (!state.exif.subject_distance.empty())
        field("Subject Dist.", state.exif.subject_distance);
    if (!state.exif.digital_zoom.empty())
        field("Digital Zoom", state.exif.digital_zoom);
    if (!state.exif.scene_capture_type.empty())
        field("Scene Type", state.exif.scene_capture_type);
    if (!state.exif.contrast.empty())
        field("Contrast", state.exif.contrast);
    if (!state.exif.saturation.empty())
        field("Saturation", state.exif.saturation);
    if (!state.exif.sharpness.empty())
        field("Sharpness", state.exif.sharpness);
    if (!state.exif.date_time_original.empty() && state.exif.date_time_original != state.exif.date_time)
        field("Date Taken", state.exif.date_time_original);
    if (state.exif.date_time != state.exif.date_time_original)
        field("Date Modified", state.exif.date_time);
    else if (!state.exif.date_time.empty())
        field("Date/Time", state.exif.date_time);
    field("Orientation", std::to_string(state.exif.orientation));
    // File info
    {
        if (state.exif.file_size > 0) {
            char sz[32];
            double bytes = (double)state.exif.file_size;
            if (bytes > 1024 * 1024)
                snprintf(sz, sizeof(sz), "%.1f MiB", bytes / (1024 * 1024));
            else if (bytes > 1024)
                snprintf(sz, sizeof(sz), "%.1f KiB", bytes / 1024);
            else
                snprintf(sz, sizeof(sz), "%zu B", (size_t)bytes);
            field("File Size", sz);
        }
        if (!state.exif.file_modified.empty())
            field("Modified", state.exif.file_modified);
    }

    if (!state.exif.software.empty())
        field("Software", state.exif.software);
    if (!state.exif.artist.empty())
        field("Artist", state.exif.artist, true, 2);
    if (!state.exif.copyright.empty())
        field("Copyright", state.exif.copyright, true, 2);
    if (!state.exif.gps_latitude.empty())
        field("GPS Latitude", state.exif.gps_latitude);
    if (!state.exif.gps_longitude.empty())
        field("GPS Longitude", state.exif.gps_longitude);
    if (!state.exif.gps_altitude.empty())
        field("GPS Altitude", state.exif.gps_altitude);
    if (!state.exif.x_resolution.empty())
        field("X Resolution", state.exif.x_resolution);
    if (!state.exif.y_resolution.empty())
        field("Y Resolution", state.exif.y_resolution);

    // ICC Profile
    if (!state.exif.icc_description.empty())
        field("ICC Profile", state.exif.icc_description, true, 1);
    if (!state.exif.icc_copyright.empty())
        field("ICC Copyright", state.exif.icc_copyright, true, 1);
    if (!state.exif.icc_model.empty())
        field("ICC Model", state.exif.icc_model, true, 1);

    // Additional metadata from all_metadata
    if (!state.exif.all_metadata.empty()) {
        ly += 4;
        section_header("All Tags");
        for (auto& md : state.exif.all_metadata) {
            if (ly + line_h > sh - 16) {
                cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                      m3::on_surface_variant_b, 0.8);
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 11);
                cairo_move_to(cr, left_col, ly);
                cairo_show_text(cr, "... more");
                break;
            }
            field(md.first.c_str(), md.second, true, 1);
        }
    }
}

void Overlay::render_placeholder(cairo_t* cr, int win_w, int win_h) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 20);

    cairo_text_extents_t ext;
    const char* msg = "No Image Open";
    cairo_text_extents(cr, msg, &ext);
    cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g, m3::on_surface_b, 1.0);
    cairo_move_to(cr, (win_w - ext.width) / 2 - ext.x_bearing,
                      win_h / 2 - 20 - ext.y_bearing);
    cairo_show_text(cr, msg);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    const char* sub = "Ctrl+O  Open File";
    cairo_text_extents(cr, sub, &ext);
    cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                          m3::on_surface_variant_b, 1.0);
    cairo_move_to(cr, (win_w - ext.width) / 2 - ext.x_bearing,
                      win_h / 2 + 10 - ext.y_bearing);
    cairo_show_text(cr, sub);

    const char* sub2 = "Ctrl+Q  Quit";
    cairo_text_extents(cr, sub2, &ext);
    cairo_move_to(cr, (win_w - ext.width) / 2 - ext.x_bearing,
                      win_h / 2 + 32 - ext.y_bearing);
    cairo_show_text(cr, sub2);
}

void Overlay::render_info(cairo_t* cr, int win_w, int win_h, const OverlayState& state) {
    std::ostringstream info;
    info << state.filename;
    if (state.image_width > 0) {
        info << "  |  " << state.image_width << "x" << state.image_height;
    }
    info << "  |  Zoom " << (int)(state.zoom * 100) << "%";
    if (state.image_count > 1) {
        info << "  |  " << (state.image_index + 1) << "/" << state.image_count;
    }

    // EXIF data
    std::string exif_str;
    if (!state.exif.make.empty() || !state.exif.model.empty()) {
        exif_str += state.exif.make;
        if (!state.exif.make.empty() && !state.exif.model.empty()) exif_str += " ";
        exif_str += state.exif.model;
    }
    if (!state.exif.focal_length.empty() || !state.exif.aperture.empty() ||
        !state.exif.exposure.empty() || !state.exif.iso.empty()) {
        if (!exif_str.empty()) exif_str += "  |  ";
        auto append = [&](const std::string& val) {
            if (val.empty()) return;
            if (!exif_str.empty()) exif_str += "  ";
            exif_str += val;
        };
        if (!state.exif.focal_length.empty())
            append(state.exif.focal_length + "mm");
        if (!state.exif.aperture.empty())
            append("f/" + state.exif.aperture);
        if (!state.exif.exposure.empty())
            append(state.exif.exposure + "s");
        if (!state.exif.iso.empty())
            append("ISO " + state.exif.iso);
    }
    if (!state.exif.date_time.empty()) {
        if (!exif_str.empty()) exif_str += "  |  ";
        exif_str += state.exif.date_time;
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    // Info text at bottom center — M3 chip-style
    {
        std::string info_str = info.str();
        cairo_text_extents_t ext;
        cairo_text_extents(cr, info_str.c_str(), &ext);
        int pad_h = 12, pad_v = 6;
        int px = (win_w - (int)ext.width - pad_h * 2) / 2;
        int py = win_h - 30;
        int chip_w = (int)ext.width + pad_h * 2;
        int chip_h = (int)ext.height + pad_v * 2;
        cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                              m3::surface_container_b, 0.95 * state.bg_alpha);
        draw_rounded_rect(cr, px, py - pad_v, chip_w, chip_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 0.95 * state.bg_alpha);
        cairo_move_to(cr, px + pad_h - ext.x_bearing, py - ext.y_bearing);
        cairo_show_text(cr, info_str.c_str());
    }

    // EXIF line below info — M3 chip-style
    if (!exif_str.empty()) {
        cairo_set_font_size(cr, 11);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, exif_str.c_str(), &ext);
        int pad_h = 10, pad_v = 5;
        int px = (win_w - (int)ext.width - pad_h * 2) / 2;
        int py = win_h - 10;
        int chip_w = (int)ext.width + pad_h * 2;
        int chip_h = (int)ext.height + pad_v * 2;
        cairo_set_source_rgba(cr, m3::surface_container_r, m3::surface_container_g,
                              m3::surface_container_b, 0.95 * state.bg_alpha);
        draw_rounded_rect(cr, px, py - pad_v, chip_w, chip_h, 8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 0.95 * state.bg_alpha);
        cairo_move_to(cr, px + pad_h - ext.x_bearing, py - ext.y_bearing);
        cairo_show_text(cr, exif_str.c_str());
    }
}

} // namespace hpv
