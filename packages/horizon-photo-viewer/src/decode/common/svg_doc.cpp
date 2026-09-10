#include "decode/common/svg_doc.hpp"
#include "decode/common/svg_render.hpp"

#include <nanosvg/nanosvg.h>

#include <cstring>
#include <iostream>
#include <string>

#ifdef HAVE_LIBRSVG
#  include <librsvg/rsvg.h>
#endif

namespace hpv {

SvgDoc::~SvgDoc() {
#ifdef HAVE_LIBRSVG
    if (rsvg_handle) {
        g_object_unref(rsvg_handle);
        rsvg_handle = nullptr;
    }
#endif
    if (ns_image) {
        nsvgDelete(ns_image);
        ns_image = nullptr;
    }
}

bool svg_have_librsvg() {
#ifdef HAVE_LIBRSVG
    return true;
#else
    return false;
#endif
}

SvgDoc* svg_doc_parse(const uint8_t* data, size_t size) {
    if (!data || size == 0) return nullptr;

#ifdef HAVE_LIBRSVG
    {
        // Try librsvg first — handles embedded images, CSS, etc.
        GError* err = nullptr;
        RsvgHandle* handle = rsvg_handle_new_from_data(data, size, &err);
        if (handle) {
            RsvgRectangle rect = { 0, 0, 1, 1 };
            rsvg_handle_get_intrinsic_size_in_pixels(handle, &rect.width, &rect.height);
            auto* doc = new SvgDoc();
            doc->from_librsvg = true;
            doc->rsvg_handle = handle;
            if (rect.width <= 0 || rect.height <= 0) {
                gboolean has_vb = FALSE;
                RsvgRectangle vb = { 0, 0, 0, 0 };
                rsvg_handle_get_intrinsic_dimensions(handle, nullptr, nullptr, nullptr, nullptr, &has_vb, &vb);
                if (has_vb && vb.width > 0 && vb.height > 0) {
                    rect.width = vb.width;
                    rect.height = vb.height;
                } else {
                    std::cerr << "[svg] librsvg: no intrinsic size or viewBox, falling back\n";
                    g_object_unref(handle);
                    delete doc;
                    goto nanosvg_fallback;
                }
            }
            doc->width = (float)rect.width;
            doc->height = (float)rect.height;
            std::cerr << "[svg] librsvg: " << rect.width << "x" << rect.height << "\n";
            return doc;
        }
        if (err) {
            std::cerr << "[svg] librsvg failed: " << (err->message ? err->message : "?") << "\n";
            g_error_free(err);
        }
        // Fall through to nanosvg
    }
#endif

nanosvg_fallback:
    // Nanosvg fallback
    std::string svg_str(reinterpret_cast<const char*>(data), size);
    NSVGimage* img = nsvgParse(const_cast<char*>(svg_str.c_str()), "px", 96);
    if (!img) {
        std::cerr << "[svg] nanosvg parse failed\n";
        return nullptr;
    }
    auto* doc = new SvgDoc();
    doc->ns_image = img;
    doc->width = img->width > 0 ? img->width : 1.0f;
    doc->height = img->height > 0 ? img->height : 1.0f;
    return doc;
}

void svg_doc_render(cairo_t* cr, const SvgDoc* doc,
                    float target_w, float target_h) {
    if (!doc) {
        std::cerr << "[svg] render called with null doc\n";
        return;
    }

#ifdef HAVE_LIBRSVG
    if (doc->rsvg_handle) {
        RsvgRectangle viewport = { 0, 0, (double)target_w, (double)target_h };
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, target_w, target_h);
        cairo_clip(cr);
        GError* err = nullptr;
        if (!rsvg_handle_render_document((RsvgHandle*)doc->rsvg_handle, cr, &viewport, &err)) {
            std::cerr << "[svg] librsvg render failed: "
                      << (err ? err->message : "?") << "\n";
            if (err) g_error_free(err);
        }
        cairo_restore(cr);
        std::cerr << "[svg] librsvg render: " << target_w << "x" << target_h << "\n";
        return;
    }
#endif

    if (doc->ns_image) {
        render_svg_cairo(cr, doc->ns_image, target_w, target_h);
    } else {
        std::cerr << "[svg] render: no backend available\n";
    }
}

} // namespace hpv
