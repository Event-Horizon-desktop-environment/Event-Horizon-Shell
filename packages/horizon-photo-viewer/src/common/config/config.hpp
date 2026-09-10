#pragma once

#include <string>
#include <vector>

namespace hpv {

struct Config {
    static Config load();
    static bool save(const Config& c);
    static std::string config_path();

    static constexpr int kMaxRecentFiles = 10;

    int slideshow_interval_ms = 5000;
    bool show_overlay = false;
    bool enable_color_management = true;
    float default_zoom = 1.0f;
    float bg_alpha = 1.0f;
    std::string theme = "dark";
    static constexpr auto kDefaultImgurClientId = "a0691805609650e";
    std::string imgur_client_id = kDefaultImgurClientId;
    bool imgur_direct_link = true;
    bool imgur_open_browser = true;
    bool imgur_auto_copy = true;
    std::vector<std::string> recent_files;
};

}
