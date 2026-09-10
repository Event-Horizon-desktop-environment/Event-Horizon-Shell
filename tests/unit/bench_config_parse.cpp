#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include "helpers/timing.h"

struct Config {
    int dock_height = 48;
    int taskbar_height = 32;
    double opacity = 0.95;
    bool animations = true;
    int animation_duration = 300;
    std::string wallpaper_mode = "fill";
    int corner_radius = 12;
    int blur_radius = 8;
    bool show_clock = true;
    bool show_battery = true;
    std::string clock_format = "%H:%M";
    int notification_timeout = 5000;
    int max_notifications = 10;
    std::string font_family = "Sans";
    int font_size = 14;
};

static Config parse_toml_basic(const char* path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n')) s.pop_back();
        };
        trim(key);
        trim(val);
        if (key == "dock_height") cfg.dock_height = atoi(val.c_str());
        else if (key == "taskbar_height") cfg.taskbar_height = atoi(val.c_str());
        else if (key == "opacity") cfg.opacity = atof(val.c_str());
        else if (key == "animations") cfg.animations = (val == "true");
        else if (key == "animation_duration") cfg.animation_duration = atoi(val.c_str());
        else if (key == "wallpaper_mode") cfg.wallpaper_mode = val;
        else if (key == "corner_radius") cfg.corner_radius = atoi(val.c_str());
        else if (key == "blur_radius") cfg.blur_radius = atoi(val.c_str());
        else if (key == "show_clock") cfg.show_clock = (val == "true");
        else if (key == "show_battery") cfg.show_battery = (val == "true");
        else if (key == "clock_format") cfg.clock_format = val;
        else if (key == "notification_timeout") cfg.notification_timeout = atoi(val.c_str());
        else if (key == "max_notifications") cfg.max_notifications = atoi(val.c_str());
        else if (key == "font_family") cfg.font_family = val;
        else if (key == "font_size") cfg.font_size = atoi(val.c_str());
    }
    return cfg;
}

static void write_test_config(const std::string& path, int lines) {
    std::ofstream f(path);
    f << "# Test configuration\n\n[appearance]\n";
    f << "dock_height = 48\ntaskbar_height = 32\nopacity = 0.95\nanimations = true\n";
    f << "animation_duration = 300\nwallpaper_mode = \"fill\"\ncorner_radius = 12\nblur_radius = 8\n\n";
    f << "[clock]\nshow_clock = true\nshow_battery = true\nclock_format = \"%H:%M\"\n\n";
    f << "[notification]\nnotification_timeout = 5000\nmax_notifications = 10\n\n";
    f << "[font]\nfont_family = \"Sans\"\nfont_size = 14\n";
    for (int i = 17; i < lines; ++i)
        f << "extra_key_" << i << " = " << i << "\n";
}

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== Config Parse Benchmarks ===\n\n");
    const char* cfg_path = "/tmp/eh-bench-config.toml";

    write_test_config(cfg_path, 17);
    bench("parse_17_lines", 50000, [&]() {
        auto cfg = parse_toml_basic(cfg_path);
        asm volatile("" ::"r"(&cfg));
    });

    write_test_config(cfg_path, 500);
    bench("parse_500_lines", 10000, [&]() {
        auto cfg = parse_toml_basic(cfg_path);
        asm volatile("" ::"r"(&cfg));
    });

    write_test_config(cfg_path, 2000);
    bench("parse_2000_lines", 5000, [&]() {
        auto cfg = parse_toml_basic(cfg_path);
        asm volatile("" ::"r"(&cfg));
    });

    std::remove(cfg_path);
    printf("\nDone.\n");
    return 0;
}
