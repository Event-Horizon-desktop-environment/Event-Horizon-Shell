// frame_bench — Wayland frame-pacing & presentation-latency benchmark.
//
// Opens a tiny xdg-toplevel window and commits one frame per compositor
// frame callback, measuring:
//
//   callback interval      time between successive wl_callback.done events
//                          (should equal the refresh period; drift = jitter)
//   callback latency       commit → callback dispatch (compositor round-trip)
//   presentation latency   wp_presentation presented-time − commit time
//   missed vblanks         sequence-number gaps in feedback events
//   p95 drift              spread of windowed p95 callback intervals
//
// Usage:
//   frame_bench [--frames N] [--interval-ms M] [--size WxH] [--json PATH]
//
// Exit code 0 on success (even if the compositor lacks wp_presentation —
// those sections are skipped).

#include "../helpers/timing.h"

#include <wayland-client.h>
#include "presentation-time-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <cmath>

namespace {

uint64_t now_mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

struct Stats {
    TimingStats ms;
};

double stddev_ms(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double acc = 0.0;
    for (const double x : v) {
        const double d = x - m;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(v.size() - 1));
}

struct FrameBench {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wp_presentation* pres = nullptr;

    wl_surface* surface = nullptr;
    xdg_surface* xdg_surf = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wl_callback* pending_cb = nullptr;
    wl_buffer* buffer = nullptr;
    void* shm_data = nullptr;
    uint32_t buf_w = 320, buf_h = 80;

    bool configured = false;
    bool running = true;
    bool wayland_error = false;

    long target_frames = 300;
    int throttle_ms = -1; // -1 = uncapped

    // samples
    std::vector<double> cb_interval_ms;   // callback → callback
    std::vector<double> cb_latency_ms;    // commit → callback dispatch
    std::vector<double> pres_latency_ms;  // presented − commit
    std::deque<uint64_t> commit_ns_cb_q; // pairs commits with frame callbacks
    std::deque<uint64_t> commit_ns_fb_q; // pairs commits with presentation feedbacks
    uint64_t last_cb_ns = 0;
    uint64_t first_refresh_hz_num = 0; // refresh ns reported by compositor
    long frames_done = 0;
    long missed_vblanks = 0;
    long discarded = 0;
    uint64_t prev_seq = 0;
    bool have_prev_seq = false;
    uint32_t clock_id = CLOCK_MONOTONIC;
};

FrameBench g;

// ── shm buffer ──────────────────────────────────────────────────────────────

bool make_buffer(FrameBench& b) {
    const int stride = static_cast<int>(b.buf_w) * 4;
    const size_t size = static_cast<size_t>(stride) * b.buf_h;
    char name[] = "/eh-frame-bench";
    const int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        close(fd);
        return false;
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    auto* px = static_cast<uint32_t*>(data);
    for (uint32_t y = 0; y < b.buf_h; ++y) {
        for (uint32_t x = 0; x < b.buf_w; ++x) {
            const uint32_t r = (x * 255u) / b.buf_w;
            const uint32_t gr = (y * 255u) / b.buf_h;
            px[static_cast<size_t>(y) * b.buf_w + x] = 0xFF000000u | (r << 16) | (gr << 8);
        }
    }
    wl_shm_pool* pool = wl_shm_create_pool(b.shm, fd, static_cast<int32_t>(size));
    b.buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(b.buf_w), static_cast<int32_t>(b.buf_h),
                                         stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd); // buffer keeps its own ref
    b.shm_data = data;
    return b.buffer != nullptr;
}

// ── listeners ───────────────────────────────────────────────────────────────

void request_frame(FrameBench& b);

const wl_callback_listener cb_listener = {
    [](void* data, wl_callback* cb, uint32_t /*serial*/) {
        auto* b = static_cast<FrameBench*>(data);
        wl_callback_destroy(cb);
        b->pending_cb = nullptr;

        const uint64_t now = now_mono_ns();
        if (!b->commit_ns_cb_q.empty()) {
            const uint64_t commit = b->commit_ns_cb_q.front();
            b->commit_ns_cb_q.pop_front();
            b->cb_latency_ms.push_back(static_cast<double>(now - commit) / 1e6);
        }
        if (b->last_cb_ns != 0) {
            b->cb_interval_ms.push_back(static_cast<double>(now - b->last_cb_ns) / 1e6);
        }
        b->last_cb_ns = now;
        ++b->frames_done;

        if (b->throttle_ms > 0 && !b->cb_interval_ms.empty()) {
            usleep(static_cast<useconds_t>(b->throttle_ms) * 1000u);
        }
        if (b->frames_done < b->target_frames) {
            request_frame(*b);
        } else {
            b->running = false;
        }
    }};

const wp_presentation_feedback_listener fb_listener = {
    [](void* /*data*/, struct wp_presentation_feedback* /*fb*/, struct wl_output* /*output*/) {
        // advisory: arrives before `presented`; object is destroyed on
        // presented/discarded which are the terminal events.
    },
    [](void* data, struct wp_presentation_feedback* fb, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec,
       uint32_t refresh_nsec, uint32_t seq_hi, uint32_t seq_lo, uint32_t /*flags*/) {
        auto* b = static_cast<FrameBench*>(data);
        wp_presentation_feedback_destroy(fb);
        const uint64_t presented =
            ((static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo) * 1000000000ull + tv_nsec;
        const uint64_t seq = (static_cast<uint64_t>(seq_hi) << 32) | seq_lo;

        if (b->first_refresh_hz_num == 0 && refresh_nsec > 0) b->first_refresh_hz_num = refresh_nsec;
        if (b->have_prev_seq && seq > b->prev_seq + 1) {
            b->missed_vblanks += static_cast<long>(seq - b->prev_seq - 1);
        }
        b->prev_seq = seq;
        b->have_prev_seq = true;

        if (!b->commit_ns_fb_q.empty()) {
            const uint64_t commit = b->commit_ns_fb_q.front();
            b->commit_ns_fb_q.pop_front();
            if (presented > commit) {
                b->pres_latency_ms.push_back(static_cast<double>(presented - commit) / 1e6);
            }
        }
    },
    [](void* data, struct wp_presentation_feedback* fb) {
        auto* b = static_cast<FrameBench*>(data);
        wp_presentation_feedback_destroy(fb);
        if (!b->commit_ns_fb_q.empty()) b->commit_ns_fb_q.pop_front();
        ++b->discarded;
    }};

void request_frame(FrameBench& b) {
    wl_callback* cb = wl_surface_frame(b.surface);
    wl_callback_add_listener(cb, &cb_listener, &b);
    b.pending_cb = cb;

    wl_surface_attach(b.surface, b.buffer, 0, 0);
    wl_surface_damage_buffer(b.surface, 0, 0, static_cast<int32_t>(b.buf_w), static_cast<int32_t>(b.buf_h));

    if (b.pres) {
        struct wp_presentation_feedback* fb = wp_presentation_feedback(b.pres, b.surface);
        wp_presentation_feedback_add_listener(fb, &fb_listener, &b);
    }

    wl_surface_commit(b.surface);
    b.commit_ns_cb_q.push_back(now_mono_ns());
    b.commit_ns_fb_q.push_back(b.commit_ns_cb_q.back());
}

void handle_xdg_configure(void* /*data*/, xdg_surface* surf, uint32_t serial) {
    xdg_surface_ack_configure(surf, serial);
    g.configured = true;
}

void registry_add(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    auto* b = static_cast<FrameBench*>(data);
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        b->compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        b->shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        b->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, std::min(version, 5u)));
    } else if (strcmp(iface, wp_presentation_interface.name) == 0) {
        b->pres = static_cast<wp_presentation*>(wl_registry_bind(reg, name, &wp_presentation_interface, std::min(version, 2u)));
    }
}
void registry_remove(void*, wl_registry*, uint32_t) {}

void wm_ping(void* /*data*/, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); }

} // namespace

static void print_stats_line(const char* label, const TimingStats& s) {
    printf("  %-34s mean=%6.2fms median=%6.2fms p95=%6.2fms p99=%6.2fms min=%5.2f max=%7.2f n=%zu\n",
           label, s.mean(), s.median(), s.p95(), s.p99(), s.min(), s.max(), s.samples.size());
}

int main(int argc, char** argv) {
    long frames = 300;
    int throttle = -1;
    const char* json_path = nullptr;
    uint32_t w = 320, h = 80;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next_long = [&]() { return i + 1 < argc ? strtol(argv[++i], nullptr, 10) : 0L; };
        if (a == "--frames") frames = next_long();
        else if (a == "--interval-ms") throttle = static_cast<int>(next_long());
        else if (a == "--json") json_path = (i + 1 < argc) ? argv[++i] : nullptr;
        else if (a == "--size" && i + 1 < argc) sscanf(argv[++i], "%ux%u", &w, &h);
    }
    if (frames < 30) frames = 30;
    g.target_frames = frames;
    g.throttle_ms = throttle;
    g.buf_w = w ? w : 320;
    g.buf_h = h ? h : 80;

    const char* sock = getenv("WAYLAND_DISPLAY");
    if (!sock || !sock[0]) {
        fprintf(stderr, "frame_bench: WAYLAND_DISPLAY not set\n");
        return 2;
    }
    g.display = wl_display_connect(sock);
    if (!g.display) {
        fprintf(stderr, "frame_bench: cannot connect to '%s': %s\n", sock, strerror(errno));
        return 2;
    }

    static const wl_registry_listener reg_listener = {registry_add, registry_remove};
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &reg_listener, &g);
    static const xdg_wm_base_listener wm_listener = {wm_ping};
    // two-phase: get globals
    wl_display_roundtrip(g.display);
    if (!g.compositor || !g.shm || !g.wm_base) {
        fprintf(stderr, "frame_bench: missing required globals (compositor/shm/xdg_wm_base)\n");
        return 2;
    }
    // answer pings for the lifetime of the run
    xdg_wm_base_add_listener(g.wm_base, &wm_listener, nullptr);
    // second pass in case xdg arrived late
    wl_display_roundtrip(g.display);

    if (g.pres) {
        // clock_id event tells us which clock presentation timestamps use
        // (we assume CLOCK_MONOTONIC; the event is advisory here)
        (void)g.clock_id;
    } else {
        printf("  [warn] wp_presentation not advertised — presentation metrics skipped\n");
    }

    g.surface = wl_compositor_create_surface(g.compositor);
    g.xdg_surf = xdg_wm_base_get_xdg_surface(g.wm_base, g.surface);
    static const xdg_surface_listener xdg_listener = {handle_xdg_configure};
    xdg_surface_add_listener(g.xdg_surf, &xdg_listener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xdg_surf);
    xdg_toplevel_set_title(g.toplevel, "eh-frame-bench");
    xdg_toplevel_set_app_id(g.toplevel, "eh-frame-bench");
    wl_surface_commit(g.surface);

    while (!g.configured) {
        if (wl_display_dispatch(g.display) < 0) {
            fprintf(stderr, "frame_bench: dispatch error during configure\n");
            return 2;
        }
    }

    if (!make_buffer(g)) {
        fprintf(stderr, "frame_bench: failed to create shm buffer\n");
        return 2;
    }

    printf("=== frame_bench ===\n");
    printf("  surface %ux%u, target=%ld frames, throttle=%dms, presentation=%s\n\n", g.buf_w, g.buf_h,
           g.target_frames, g.throttle_ms, g.pres ? "yes" : "no");

    request_frame(g);
    while (g.running) {
        if (wl_display_dispatch(g.display) < 0) {
            g.wayland_error = true;
            break;
        }
    }
    // drain trailing presentation feedbacks so pairing completes
    if (g.pres) {
        wl_display_roundtrip(g.display);
    }

    printf("  frames=%ld discarded_feedbacks=%ld missed_vblanks=%ld\n", g.frames_done, g.discarded,
           g.missed_vblanks);
    if (g.first_refresh_hz_num > 0) {
        printf("  compositor refresh=%.2f Hz (%" PRIu64 " ns)\n", 1e9 / static_cast<double>(g.first_refresh_hz_num),
               g.first_refresh_hz_num);
    }
    printf("\n");

    print_stats_line("callback_interval (pacing)", [&] {
        Stats s;
        for (const double v : g.cb_interval_ms) s.ms.add(v);
        return s.ms;
    }());

    // windowed p95 drift
    if (g.cb_interval_ms.size() >= 20) {
        constexpr size_t kWindows = 5;
        double win_p95[kWindows] = {};
        const size_t chunk = g.cb_interval_ms.size() / kWindows;
        printf("  windowed_p95 (drift):");
        double lo = 1e9, hi = 0.0;
        for (size_t wi = 0; wi < kWindows; ++wi) {
            Stats ws;
            for (size_t j = wi * chunk; j < (wi == kWindows - 1 ? g.cb_interval_ms.size() : (wi + 1) * chunk); ++j)
                ws.ms.add(g.cb_interval_ms[j]);
            win_p95[wi] = ws.ms.p95();
            lo = std::min(lo, win_p95[wi]);
            hi = std::max(hi, win_p95[wi]);
            printf(" %.2f", win_p95[wi]);
        }
        printf(" ms → drift=%.3fms stddev=%.3fms\n", hi - lo, stddev_ms(g.cb_interval_ms));
    }

    print_stats_line("callback_latency (commit→cb)", [&] {
        Stats s;
        for (const double v : g.cb_latency_ms) s.ms.add(v);
        return s.ms;
    }());

    if (!g.pres_latency_ms.empty()) {
        print_stats_line("presentation_latency (shown−commit)", [&] {
            Stats s;
            for (const double v : g.pres_latency_ms) s.ms.add(v);
            return s.ms;
        }());
    }

    if (json_path) {
        FILE* f = fopen(json_path, "w");
        if (f) {
            Stats ci, cl, pl;
            for (const double v : g.cb_interval_ms) ci.ms.add(v);
            for (const double v : g.cb_latency_ms) cl.ms.add(v);
            for (const double v : g.pres_latency_ms) pl.ms.add(v);
            fprintf(f, "{\"frames\":%ld,\"refresh_hz\":%.3f,\"missed_vblanks\":%ld,", g.frames_done,
                    g.first_refresh_hz_num > 0 ? 1e9 / static_cast<double>(g.first_refresh_hz_num) : 0.0,
                    g.missed_vblanks);
            fprintf(f, "\"callback_interval\":%s,", ci.ms.to_json("stats").c_str());
            fprintf(f, "\"callback_latency\":%s,", cl.ms.to_json("stats").c_str());
            fprintf(f, "\"presentation_latency\":%s}\n", pl.ms.to_json("stats").c_str());
            fclose(f);
            printf("\nJSON saved to: %s\n", json_path);
        }
    }

    if (g.pending_cb) wl_callback_destroy(g.pending_cb);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xdg_surf) xdg_surface_destroy(g.xdg_surf);
    if (g.buffer) wl_buffer_destroy(g.buffer);
    if (g.shm_data) munmap(g.shm_data, static_cast<size_t>(g.buf_w) * 4 * g.buf_h);
    if (g.surface) wl_surface_destroy(g.surface);
    if (g.pres) wp_presentation_destroy(g.pres);
    if (g.wm_base) xdg_wm_base_destroy(g.wm_base);
    if (g.registry) wl_registry_destroy(g.registry);
    if (g.display) wl_display_disconnect(g.display);

    printf("\n%s\n", g.wayland_error ? "RESULT: wayland protocol error" : "RESULT: ok");
    return g.wayland_error ? 1 : 0;
}
