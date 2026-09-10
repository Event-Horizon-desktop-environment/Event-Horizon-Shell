#include "core/viewer/app.hpp"

#include <algorithm>
#include <cstring>

namespace hpv {

void App::gen_thumb_bgra(const std::vector<uint8_t>& rgba, int w, int h,
                         std::vector<uint8_t>& out, int& out_w, int& out_h) {
    int tw = 80, th = 60;
    float scale = std::min((float)tw / w, (float)th / h);
    out_w = std::max(1, (int)(w * scale));
    out_h = std::max(1, (int)(h * scale));
    out.resize((size_t)out_w * out_h * 4);

    int src_step = w * 4;
    for (int ty = 0; ty < out_h; ty++) {
        int sy = std::min((int)(ty / scale), h - 1);
        for (int tx = 0; tx < out_w; tx++) {
            int sx = std::min((int)(tx / scale), w - 1);
            size_t si = (size_t)sy * src_step + (size_t)sx * 4;
            size_t ti = (size_t)ty * out_w * 4 + (size_t)tx * 4;
            out[ti + 0] = rgba[si + 2];
            out[ti + 1] = rgba[si + 1];
            out[ti + 2] = rgba[si + 0];
            out[ti + 3] = rgba[si + 3];
        }
    }
}

void App::evict_thumb_cache() {
    while (thumb_cache_bytes_ > kThumbCacheMaxBytes && !thumb_cache_order_.empty()) {
        int idx = thumb_cache_order_.back();
        thumb_cache_order_.pop_back();
        auto it = thumb_cache_.find(idx);
        if (it != thumb_cache_.end()) {
            thumb_cache_bytes_ -= it->second.size();
            thumb_cache_.erase(it);
            thumb_cache_w_.erase(idx);
            thumb_cache_h_.erase(idx);
        }
    }
}

void App::invalidate_thumb_strip() {
    thumb_dirty_ = true;
}

void App::destroy_cached_strip() {
    if (cached_strip_) {
        cairo_surface_destroy(cached_strip_);
        cached_strip_ = nullptr;
    }
    cached_strip_w_ = 0;
}

void App::process_thumb_batch(int count) {
    if (!show_thumbnails_) return;
    bool any = false;
    int n = std::min(count, (int)thumb_pending_.size());
    for (int i = 0; i < n; i++) {
        int idx = thumb_pending_[i];
        if (thumb_cache_.count(idx) == 0 || thumb_cache_[idx].empty()) {
            load_thumbnail(idx);
            any = true;
        }
    }
    thumb_pending_.erase(thumb_pending_.begin(), thumb_pending_.begin() + n);
    if (any) {
        invalidate_thumb_strip();
        render();
    }
}

void App::queue_thumb_preload(int from, int to) {
    if (from < 0) from = 0;
    if (to > (int)dir_images_.size()) to = (int)dir_images_.size();
    for (int i = from; i < to; i++) {
        if (thumb_cache_.count(i) == 0) {
            if (std::find(thumb_pending_.begin(), thumb_pending_.end(), i) == thumb_pending_.end()) {
                thumb_pending_.push_back(i);
            }
        }
    }
}

void App::load_thumbnail(int index) {
    if (index < 0 || index >= (int)dir_images_.size()) return;
    if (thumb_cache_.count(index)) return; // already cached in memory

    const auto& filepath = dir_images_[index];

    // Try disk cache first
    {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (ThumbCache::load(filepath, rgba, w, h)) {
            // Convert RGBA to BGRA for Cairo
            thumb_cache_w_[index] = w;
            thumb_cache_h_[index] = h;
            thumb_cache_[index].resize(rgba.size());
            auto& bg = thumb_cache_[index];
            for (int i = 0; i < w * h; i++) {
                bg[i * 4 + 0] = rgba[i * 4 + 2];
                bg[i * 4 + 1] = rgba[i * 4 + 1];
                bg[i * 4 + 2] = rgba[i * 4 + 0];
                bg[i * 4 + 3] = rgba[i * 4 + 3];
            }
            thumb_cache_bytes_ += bg.size();
            thumb_cache_order_.push_front(index);
            evict_thumb_cache();
            return;
        }
    }

    // Mark as attempted so we don't retry failed decodes
    thumb_cache_[index] = {};
    thumb_cache_w_[index] = 0;
    thumb_cache_h_[index] = 0;

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size);
    if (fread(buf.data(), 1, size, f) != size) {
        fclose(f);
        return;
    }
    fclose(f);

    auto result = decoders_.decode(buf.data(), size);
    if (result.pixels.empty()) return;

    if (result.format_name == "SVG") {
        // SVG thumbnail: render via vector path at low resolution
        int tw = 80, th = 60;
        SvgDoc* doc = svg_doc_parse(buf.data(), buf.size());
        if (doc) {
            float scale = std::min((float)tw / doc->width, (float)th / doc->height);
            int rw = std::max(1, (int)(doc->width * scale));
            int rh = std::max(1, (int)(doc->height * scale));
            cairo_surface_t* ts = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
            cairo_t* tc = cairo_create(ts);
            svg_doc_render(tc, doc, (float)rw, (float)rh);
            cairo_destroy(tc);
            uint8_t* td = cairo_image_surface_get_data(ts);
            int tstride = cairo_image_surface_get_stride(ts);
            thumb_cache_w_[index] = rw;
            thumb_cache_h_[index] = rh;
            thumb_cache_[index].resize((size_t)rh * tstride);
            std::memcpy(thumb_cache_[index].data(), td, (size_t)rh * tstride);
            cairo_surface_destroy(ts);
            delete doc;
        } else {
            thumb_cache_[index] = {};
            thumb_cache_w_[index] = 0;
            thumb_cache_h_[index] = 0;
        }
    } else {
        // Raster thumbnail
        gen_thumb_bgra(result.pixels, result.width, result.height,
                       thumb_cache_[index], thumb_cache_w_[index], thumb_cache_h_[index]);
    }

    thumb_cache_bytes_ += thumb_cache_[index].size();
    thumb_cache_order_.push_front(index);
    evict_thumb_cache();

    // Save thumbnail to disk cache (convert BGRA to RGBA for canonical storage)
    {
        auto& tb = thumb_cache_[index];
        int tw = thumb_cache_w_[index];
        int th = thumb_cache_h_[index];
        std::vector<uint8_t> rgba(tb.size());
        for (int i = 0; i < tw * th; i++) {
            rgba[i * 4 + 0] = tb[i * 4 + 2];
            rgba[i * 4 + 1] = tb[i * 4 + 1];
            rgba[i * 4 + 2] = tb[i * 4 + 0];
            rgba[i * 4 + 3] = tb[i * 4 + 3];
        }
        ThumbCache::save(filepath, rgba.data(), tw, th);
    }
}

}
