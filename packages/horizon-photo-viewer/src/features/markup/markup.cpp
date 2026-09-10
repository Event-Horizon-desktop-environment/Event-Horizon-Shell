#include "core/viewer/app.hpp"

#include <algorithm>
#include <cmath>

namespace hpv {

void App::draw_markup_elements(cairo_t* cr) {
    if (markup_elements_.empty() && !markup_current_) return;

    auto set_color = [&](const MarkupElement& el) {
        cairo_set_source_rgba(cr,
            ((el.color >> 24) & 0xFF) / 255.0,
            ((el.color >> 16) & 0xFF) / 255.0,
            ((el.color >> 8) & 0xFF) / 255.0,
            ((el.color >> 0) & 0xFF) / 255.0);
        cairo_set_line_width(cr, el.thickness);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    };

    auto draw_pen = [&](const MarkupElement& el) {
        if (el.points_x.size() < 2) return;
        cairo_move_to(cr, el.points_x[0], el.points_y[0]);
        for (size_t i = 1; i < el.points_x.size(); i++)
            cairo_line_to(cr, el.points_x[i], el.points_y[i]);
        cairo_stroke(cr);
    };

    auto draw_line = [&](const MarkupElement& el) {
        if (el.points_x.size() < 2) return;
        cairo_move_to(cr, el.points_x[0], el.points_y[0]);
        cairo_line_to(cr, el.points_x[1], el.points_y[1]);
        cairo_stroke(cr);
    };

    auto draw_arrow = [&](const MarkupElement& el) {
        if (el.points_x.size() < 2) return;
        float x1 = el.points_x[0], y1 = el.points_y[0];
        float x2 = el.points_x[1], y2 = el.points_y[1];
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) return;
        float ux = dx / len, uy = dy / len;
        // Shaft
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
        // Arrowhead
        float head_len = std::max(10.0f, el.thickness * 3.0f);
        float head_angle = 0.45f;
        float ax1 = x2 - ux * head_len * std::cos(head_angle) + uy * head_len * std::sin(head_angle);
        float ay1 = y2 - uy * head_len * std::cos(head_angle) - ux * head_len * std::sin(head_angle);
        float ax2 = x2 - ux * head_len * std::cos(head_angle) - uy * head_len * std::sin(head_angle);
        float ay2 = y2 - uy * head_len * std::cos(head_angle) + ux * head_len * std::sin(head_angle);
        cairo_set_line_width(cr, std::max(1.5f, el.thickness * 0.5f));
        cairo_move_to(cr, x2, y2);
        cairo_line_to(cr, ax1, ay1);
        cairo_move_to(cr, x2, y2);
        cairo_line_to(cr, ax2, ay2);
        cairo_stroke(cr);
    };

    auto draw_rect = [&](const MarkupElement& el) {
        if (el.rect_w <= 0 || el.rect_h <= 0) return;
        cairo_rectangle(cr, el.rect_x, el.rect_y, el.rect_w, el.rect_h);
        cairo_stroke(cr);
    };

    auto draw_ellipse = [&](const MarkupElement& el) {
        if (el.rect_w <= 0 || el.rect_h <= 0) return;
        float cx = el.rect_x + el.rect_w * 0.5f;
        float cy = el.rect_y + el.rect_h * 0.5f;
        float rx = el.rect_w * 0.5f;
        float ry = el.rect_h * 0.5f;
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_scale(cr, rx, ry);
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_stroke(cr);
    };

    auto draw_numbered = [&](const MarkupElement& el) {
        if (el.points_x.size() < 1) return;
        float cx = el.points_x[0], cy = el.points_y[0];
        float r = std::max(12.0f, el.thickness * 3.0f);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_fill(cr);
        // White border
        cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
        cairo_set_line_width(cr, 1.5);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_stroke(cr);
        // Number text
        if (!el.text.empty()) {
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, r * 0.9f);
            cairo_text_extents_t te;
            cairo_text_extents(cr, el.text.c_str(), &te);
            cairo_move_to(cr, cx - te.x_advance * 0.5f, cy + te.height * 0.35f);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
            cairo_show_text(cr, el.text.c_str());
        }
    };

    auto draw_one = [&](const MarkupElement& el) {
        set_color(el);
        switch (el.type) {
        case MarkupTool::kPen:   draw_pen(el); break;
        case MarkupTool::kLine:  draw_line(el); break;
        case MarkupTool::kArrow: draw_arrow(el); break;
        case MarkupTool::kRect:  draw_rect(el); break;
        case MarkupTool::kEllipse: draw_ellipse(el); break;
        case MarkupTool::kNumbered: draw_numbered(el); break;
        default: draw_pen(el); break;
        }
    };

    for (const auto& el : markup_elements_) draw_one(el);
    if (markup_current_) draw_one(*markup_current_);
}

void App::toggle_markup() {
    if (markup_active_) {
        cancel_markup();
        return;
    }
    if (decoded_image_.width <= 0) return;
    if (crop_active_) cancel_crop();
    markup_active_ = true;
    markup_tool_ = MarkupTool::kPen;
    markup_color_ = 0xFF0000FF;
    markup_thickness_ = 3.0f;
    numbered_count_ = 0;
    markup_current_.reset();
    render();
}

void App::commit_markup() {
    if (!markup_active_) return;
    if (markup_current_) {
        markup_elements_.push_back(std::move(*markup_current_));
        markup_current_.reset();
    }
    markup_active_ = false;
    image_modified_ = true;
    render();
}

void App::cancel_markup() {
    markup_active_ = false;
    markup_current_.reset();
    markup_elements_.clear();
    markup_redo_stack_.clear();
    image_modified_ = false;
    render();
}

void App::undo_markup() {
    if (!markup_elements_.empty()) {
        markup_redo_stack_.push_back(std::move(markup_elements_.back()));
        markup_elements_.pop_back();
        render();
    }
}

}
