#pragma once

#include <cairo.h>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hpv {

struct ThumbnailEntry {
    int x = 0, y = 0, w = 0, h = 0;
    int index = 0;
    bool loaded = false;
    bool current = false;
    std::function<void()> action;
};

struct ThumbnailStripState {
    int image_index = -1;
    int image_count = 0;
    int win_w = 0;
    int win_h = 0;
    int scroll_offset = 0;
    // Cache lookups: if index exists in cache, use its BGRA data
    std::map<int, std::vector<uint8_t>>* cache = nullptr;
    std::map<int, int>* cache_w = nullptr;
    std::map<int, int>* cache_h = nullptr;
};

class ThumbnailStrip {
public:
    void render(cairo_t* cr, const ThumbnailStripState& state,
                std::vector<ThumbnailEntry>& entries);

    static constexpr int kHeight = 72;
    static constexpr int kThumbW = 80;
    static constexpr int kThumbH = 60;
    static constexpr int kMargin = 6;
    static constexpr int kGap = 4;

private:
    static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);
    void draw_entry(cairo_t* cr, int x, int y, int w, int h,
                    bool current, cairo_surface_t* thumb_surf);
};

}
