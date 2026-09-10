#pragma once

#include <cairo.h>
#include <cstddef>
#include <cstdint>

struct NSVGimage;

namespace hpv {

// Backend-agnostic handle to a parsed SVG document.
struct SvgDoc {
    bool from_librsvg = false;
    float width = 0;
    float height = 0;
    struct NSVGimage* ns_image = nullptr;
    void* rsvg_handle = nullptr;

    ~SvgDoc();
};

bool svg_have_librsvg();

SvgDoc* svg_doc_parse(const uint8_t* data, size_t size);

void svg_doc_render(cairo_t* cr, const SvgDoc* doc,
                    float target_w, float target_h);

} // namespace hpv
