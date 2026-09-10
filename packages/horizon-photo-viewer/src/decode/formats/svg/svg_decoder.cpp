#include "decode/formats/svg/decoder.hpp"
#include "decode/common/svg_render.hpp"

#include <nanosvg/nanosvg.h>

#include <iostream>
#include <string>
#include <vector>


namespace hpv {

// ---- SvgDecoder (raster fallback) ----

bool SvgDecoder::can_decode(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    if (data[0] == '<' && data[1] == 's' && data[2] == 'v' && data[3] == 'g') return true;
    if (data[0] == '<' && data[1] == '?' && data[2] == 'x' && data[3] == 'm') return true;
    if (data[0] == '<' && data[1] == 'S' && data[2] == 'V' && data[3] == 'G') return true;
    return false;
}

DecodeResult SvgDecoder::decode(const uint8_t*, size_t,
                                 int, int) {
    DecodeResult result;
    // Vector rendering is handled by render_svg_cairo — just return a
    // sentinel so load_image knows the file is SVG and continues to
    // the nsgParse-based vector path.
    result.pixels = std::vector<uint8_t>(4, 0);
    result.width = 1;
    result.height = 1;
    result.format_name = "SVG";
    return result;
}

// ---- Vector rendering (with fill-batch optimisation) ----

void render_svg_cairo(cairo_t* cr, const NSVGimage* img,
                       float target_w, float target_h) {
    if (!img || img->width <= 0 || img->height <= 0) {
        std::cerr << "[svg] nanosvg render skipped: img=" << (void*)img
                  << " w=" << (img ? img->width : 0)
                  << " h=" << (img ? img->height : 0) << "\n";
        return;
    }

    float scale = target_w / img->width;
    float sy = target_h / img->height;
    if (sy < scale) scale = sy;

    cairo_save(cr);
    cairo_scale(cr, scale, scale);

    // Batch state for consecutive fill-only solid-colour shapes
    bool batch_active = false;
    unsigned int batch_color = 0;
    int batch_fill_rule = 0;
    float batch_opacity = 1.0f;

    for (const NSVGshape* shape = img->shapes; shape; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;

        bool do_fill = shape->fill.type == NSVG_PAINT_COLOR ||
                       shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT ||
                       shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT;
        bool do_stroke = (shape->stroke.type == NSVG_PAINT_COLOR ||
                          shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT ||
                          shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) &&
                         shape->strokeWidth > 0.0f;

        if (!do_fill && !do_stroke) continue;

        // Can this shape be merged into the current fill batch?
        bool can_batch = do_fill && !do_stroke &&
                         shape->fill.type == NSVG_PAINT_COLOR;

        if (can_batch) {
            unsigned int c = shape->fill.color;
            int fr = shape->fillRule;
            float op = shape->opacity;

            if (batch_active &&
                (c != batch_color || fr != batch_fill_rule || op != batch_opacity)) {
                cairo_fill(cr);
                batch_active = false;
            }

            if (!batch_active) {
                batch_active = true;
                batch_color = c;
                batch_fill_rule = fr;
                batch_opacity = op;
                float fa = ((c >> 24) & 0xff) / 255.0f * op;
                cairo_set_source_rgba(cr,
                    (c & 0xff) / 255.0,
                    ((c >> 8) & 0xff) / 255.0,
                    ((c >> 16) & 0xff) / 255.0,
                    fa);
                cairo_set_fill_rule(cr, fr == NSVG_FILLRULE_EVENODD
                    ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
                cairo_new_path(cr);
            }

            for (const NSVGpath* path = shape->paths; path; path = path->next) {
                cairo_move_to(cr, path->pts[0], path->pts[1]);
                for (int i = 0; i < path->npts - 1; i += 3) {
                    const float* p = &path->pts[i * 2];
                    cairo_curve_to(cr, p[2], p[3], p[4], p[5], p[6], p[7]);
                }
                if (path->closed) cairo_close_path(cr);
            }
            continue;
        }

        // Flush any active batch before a non-batchable shape
        if (batch_active) {
            cairo_fill(cr);
            batch_active = false;
        }

        // Build path
        for (const NSVGpath* path = shape->paths; path; path = path->next) {
            cairo_move_to(cr, path->pts[0], path->pts[1]);
            for (int i = 0; i < path->npts - 1; i += 3) {
                const float* p = &path->pts[i * 2];
                cairo_curve_to(cr, p[2], p[3], p[4], p[5], p[6], p[7]);
            }
            if (path->closed) cairo_close_path(cr);
        }

        // Fill
        if (do_fill) {
            if (shape->fill.type == NSVG_PAINT_COLOR) {
                unsigned int c = shape->fill.color;
                double fa = ((c >> 24) & 0xff) / 255.0 * shape->opacity;
                cairo_set_source_rgba(cr,
                    (c & 0xff) / 255.0,
                    ((c >> 8) & 0xff) / 255.0,
                    ((c >> 16) & 0xff) / 255.0,
                    fa);
            } else if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT) {
                const NSVGgradient* g = shape->fill.gradient;
                if (g && g->nstops > 0) {
                    cairo_pattern_t* pat = cairo_pattern_create_linear(0, 0, 0, 1);
                    cairo_matrix_t mat;
                    cairo_matrix_init(&mat, g->xform[0], g->xform[1],
                                            g->xform[2], g->xform[3],
                                            g->xform[4], g->xform[5]);
                    cairo_pattern_set_matrix(pat, &mat);
                    for (int i = 0; i < g->nstops; i++) {
                        unsigned int sc = g->stops[i].color;
                        cairo_pattern_add_color_stop_rgba(pat, g->stops[i].offset,
                            (sc & 0xff) / 255.0,
                            ((sc >> 8) & 0xff) / 255.0,
                            ((sc >> 16) & 0xff) / 255.0,
                            ((sc >> 24) & 0xff) / 255.0);
                    }
                    cairo_set_source(cr, pat);
                    cairo_pattern_destroy(pat);
                } else {
                    do_fill = false;
                }
            } else if (shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
                const NSVGgradient* g = shape->fill.gradient;
                if (g && g->nstops > 0) {
                    cairo_pattern_t* pat = cairo_pattern_create_radial(
                        g->fx, g->fy, 0, 0, 0, 1);
                    cairo_matrix_t mat;
                    cairo_matrix_init(&mat, g->xform[0], g->xform[1],
                                            g->xform[2], g->xform[3],
                                            g->xform[4], g->xform[5]);
                    cairo_pattern_set_matrix(pat, &mat);
                    for (int i = 0; i < g->nstops; i++) {
                        unsigned int sc = g->stops[i].color;
                        cairo_pattern_add_color_stop_rgba(pat, g->stops[i].offset,
                            (sc & 0xff) / 255.0,
                            ((sc >> 8) & 0xff) / 255.0,
                            ((sc >> 16) & 0xff) / 255.0,
                            ((sc >> 24) & 0xff) / 255.0);
                    }
                    cairo_set_source(cr, pat);
                    cairo_pattern_destroy(pat);
                } else {
                    do_fill = false;
                }
            }

            if (do_fill) {
                cairo_set_fill_rule(cr, shape->fillRule == NSVG_FILLRULE_EVENODD
                    ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
                cairo_fill_preserve(cr);
            }
        }

        // Stroke
        if (do_stroke) {
            if (shape->stroke.type == NSVG_PAINT_COLOR) {
                unsigned int c = shape->stroke.color;
                double sa = ((c >> 24) & 0xff) / 255.0 * shape->opacity;
                cairo_set_source_rgba(cr,
                    (c & 0xff) / 255.0,
                    ((c >> 8) & 0xff) / 255.0,
                    ((c >> 16) & 0xff) / 255.0,
                    sa);
                cairo_set_line_width(cr, shape->strokeWidth);
                switch (shape->strokeLineCap) {
                    case NSVG_CAP_BUTT:   cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT); break;
                    case NSVG_CAP_ROUND:  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); break;
                    case NSVG_CAP_SQUARE: cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE); break;
                }
                switch (shape->strokeLineJoin) {
                    case NSVG_JOIN_MITER: cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER); break;
                    case NSVG_JOIN_ROUND: cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); break;
                    case NSVG_JOIN_BEVEL: cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL); break;
                }
                cairo_set_miter_limit(cr, shape->miterLimit);
                if (shape->strokeDashCount > 0) {
                    double dashes[8];
                    for (int i = 0; i < shape->strokeDashCount; i++)
                        dashes[i] = shape->strokeDashArray[i];
                    cairo_set_dash(cr, dashes, shape->strokeDashCount, shape->strokeDashOffset);
                }
                cairo_stroke(cr);
            } else {
                do_stroke = false;
            }
        }

        if (!do_stroke && do_fill)
            cairo_new_path(cr);
    }

    // Flush any remaining batch
    if (batch_active)
        cairo_fill(cr);

    cairo_restore(cr);

    // Debug: count shapes and total paths in this image
    int shape_count = 0, path_count = 0, fill_batch_count = 0;
    for (const NSVGshape* shape = img->shapes; shape; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;
        shape_count++;
        bool can_batch = (shape->fill.type == NSVG_PAINT_COLOR) &&
                         !((shape->stroke.type == NSVG_PAINT_COLOR ||
                            shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT ||
                            shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) &&
                           shape->strokeWidth > 0.0f);
        if (can_batch) fill_batch_count++;
        for (const NSVGpath* p = shape->paths; p; p = p->next)
            path_count++;
    }
    std::cerr << "[svg] shapes=" << shape_count
              << " paths=" << path_count
              << " batchable=" << fill_batch_count
              << "\n";
}

} // namespace hpv
