#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "helpers/timing.h"

struct Rect { int x, y, w, h; };

static std::vector<Rect> merge_damage(std::vector<Rect>& dirty, const Rect& new_damage) {
    dirty.push_back(new_damage);
    if (dirty.size() <= 1) return dirty;
    if (dirty.size() > 8) {
        int min_x = dirty[0].x, min_y = dirty[0].y;
        int max_x = dirty[0].x + dirty[0].w, max_y = dirty[0].y + dirty[0].h;
        for (auto& r : dirty) {
            min_x = std::min(min_x, r.x);
            min_y = std::min(min_y, r.y);
            max_x = std::max(max_x, r.x + r.w);
            max_y = std::max(max_y, r.y + r.h);
        }
        dirty.clear();
        dirty.push_back({min_x, min_y, max_x - min_x, max_y - min_y});
        return dirty;
    }
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < dirty.size() && !merged; ++i) {
            for (size_t j = i + 1; j < dirty.size() && !merged; ++j) {
                auto& a = dirty[i];
                auto& b = dirty[j];
                if (a.x <= b.x + b.w + 1 && a.x + a.w + 1 >= b.x &&
                    a.y <= b.y + b.h + 1 && a.y + a.h + 1 >= b.y) {
                    int nx = std::min(a.x, b.x);
                    int ny = std::min(a.y, b.y);
                    int nw = std::max(a.x + a.w, b.x + b.w) - nx;
                    int nh = std::max(a.y + a.h, b.y + b.h) - ny;
                    a = {nx, ny, nw, nh};
                    dirty.erase(dirty.begin() + j);
                    merged = true;
                }
            }
        }
    }
    return dirty;
}

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== Damage Region Benchmarks ===\n\n");

    bench("single_rect_merge", 100000, []() {
        std::vector<Rect> dirty;
        for (int i = 0; i < 100; ++i)
            merge_damage(dirty, {i * 20, 0, 19, 1080});
    });

    bench("clustered_merge", 100000, []() {
        std::vector<Rect> dirty;
        for (int i = 0; i < 100; ++i)
            merge_damage(dirty, {100 + (i % 10), i / 10 * 108, 50, 100});
    });

    bench("scattered_merge", 50000, []() {
        std::vector<Rect> dirty;
        for (int i = 0; i < 50; ++i)
            merge_damage(dirty, {(i * 37) % 1920, (i * 53) % 1080, 20, 20});
    });

    bench("fullscreen_merge", 200000, []() {
        std::vector<Rect> dirty;
        merge_damage(dirty, {0, 0, 1920, 1080});
        merge_damage(dirty, {0, 0, 1920, 1080});
        merge_damage(dirty, {0, 0, 1920, 1080});
    });

    printf("\nDone.\n");
    return 0;
}
