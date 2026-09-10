#include "common/config/config.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace hpv {

static std::string state_dir() {
    const char* state = getenv("XDG_STATE_HOME");
    if (state && state[0]) return state;
    const char* home = getenv("HOME");
    if (!home) return "/tmp";
    return std::string(home) + "/.local/state";
}

static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static void ensure_dir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return;
    size_t pos = 0;
    while ((pos = dir.find_first_of('/', pos + 1)) != std::string::npos) {
        std::string sub = dir.substr(0, pos);
        mkdir(sub.c_str(), 0755);
    }
    mkdir(dir.c_str(), 0755);
}

std::string Config::config_path() {
    return state_dir() + "/event-horizon/Horizon-photo/config.toml";
}

static std::string trim(std::string s) {
    auto f = s.find_first_not_of(" \t\r\n");
    if (f == std::string::npos) return {};
    auto l = s.find_last_not_of(" \t\r\n");
    return s.substr(f, l - f + 1);
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

Config Config::load() {
    Config c{};
    std::string path = config_path();

    if (!file_exists(path)) {
        ensure_dir(path.substr(0, path.rfind('/')));
        std::ofstream out(path);
        if (out) {
            out << "# Horizon Photo Viewer configuration\n";
            out << "\n";
            out << "slideshow_interval_ms = " << c.slideshow_interval_ms << "\n";
            out << "show_overlay = " << (c.show_overlay ? "true" : "false") << "\n";
            out << "enable_color_management = " << (c.enable_color_management ? "true" : "false") << "\n";
            out << "default_zoom = " << c.default_zoom << "\n";
            out << "bg_alpha = " << c.bg_alpha << "\n";
            out << "theme = \"" << c.theme << "\"\n";
            out << "imgur_client_id = \"" << c.imgur_client_id << "\"\n";
            out << "imgur_direct_link = " << (c.imgur_direct_link ? "true" : "false") << "\n";
            out << "imgur_open_browser = " << (c.imgur_open_browser ? "true" : "false") << "\n";
            out << "imgur_auto_copy = " << (c.imgur_auto_copy ? "true" : "false") << "\n";
            out << "recent_files = []\n";
        }
        std::cout << "config: created default at " << path << "\n";
        return c;
    }

    std::ifstream in(path);
    if (!in) {
        std::cerr << "config: failed to open " << path << "\n";
        return c;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "slideshow_interval_ms") {
            int v = 0;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc{})
                c.slideshow_interval_ms = v;
        } else if (key == "show_overlay") {
            c.show_overlay = (val == "true");
        } else if (key == "enable_color_management") {
            c.enable_color_management = (val == "true");
        } else if (key == "default_zoom") {
            float v = 0;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc{})
                c.default_zoom = v;
        } else if (key == "bg_alpha") {
            float v = 0;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc{})
                c.bg_alpha = std::max(0.0f, std::min(1.0f, v));
        } else if (key == "theme") {
            c.theme = unquote(val);
        } else if (key == "imgur_client_id") {
            c.imgur_client_id = unquote(val);
        } else if (key == "imgur_direct_link") {
            c.imgur_direct_link = (val == "true");
        } else if (key == "imgur_open_browser") {
            c.imgur_open_browser = (val == "true");
        } else if (key == "imgur_auto_copy") {
            c.imgur_auto_copy = (val == "true");
        } else if (key == "recent_files") {
            // Parse TOML array: ["path1","path2",...]
            if (val.size() >= 2 && val.front() == '[' && val.back() == ']') {
                std::string inner = val.substr(1, val.size() - 2);
                size_t pos = 0;
                while ((pos = inner.find_first_of('"', pos)) != std::string::npos) {
                    size_t end = inner.find_first_of('"', pos + 1);
                    if (end == std::string::npos) break;
                    std::string p = inner.substr(pos + 1, end - pos - 1);
                    if (!p.empty()) c.recent_files.push_back(p);
                    pos = end + 1;
                }
            }
        }
    }

    std::cout << "config: loaded from " << path << "\n";
    return c;
}

bool Config::save(const Config& c) {
    std::string path = config_path();
    ensure_dir(path.substr(0, path.rfind('/')));
    std::ofstream out(path);
    if (!out) {
        std::cerr << "config: failed to write " << path << "\n";
        return false;
    }
    out << "# Horizon Photo Viewer configuration\n";
    out << "\n";
    out << "slideshow_interval_ms = " << c.slideshow_interval_ms << "\n";
    out << "show_overlay = " << (c.show_overlay ? "true" : "false") << "\n";
    out << "enable_color_management = " << (c.enable_color_management ? "true" : "false") << "\n";
    out << "default_zoom = " << c.default_zoom << "\n";
    out << "bg_alpha = " << c.bg_alpha << "\n";
    out << "theme = \"" << c.theme << "\"\n";
    if (!c.imgur_client_id.empty()) {
        out << "imgur_client_id = \"" << c.imgur_client_id << "\"\n";
    }
    out << "imgur_direct_link = " << (c.imgur_direct_link ? "true" : "false") << "\n";
    out << "imgur_open_browser = " << (c.imgur_open_browser ? "true" : "false") << "\n";
    out << "imgur_auto_copy = " << (c.imgur_auto_copy ? "true" : "false") << "\n";
    out << "recent_files = [";
    for (size_t i = 0; i < c.recent_files.size(); i++) {
        if (i > 0) out << ", ";
        out << "\"" << c.recent_files[i] << "\"";
    }
    out << "]\n";
    return true;
}

}
