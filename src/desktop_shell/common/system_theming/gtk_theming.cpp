#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include "desktop_shell/common/fs/string_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace eh::theming {

using eh::shell::str::file_exists;
using eh::shell::str::trim;
using eh::theming::detail::icon_base_dirs;
using eh::theming::detail::read_ini_kv;
using eh::theming::detail::run_cmd_capture;

namespace {

static bool parse_hex_color(const std::string& hex, double& r, double& g, double& b) {
   
  if (hex.empty()) return false;
  std::string h = hex;
  if (h.front() == '#') h = h.substr(1);
  if (h.size() == 3) {
    h = std::string(1, h[0]) + h[0] + h[1] + h[1] + h[2] + h[2];
  }
  if (h.size() != 6) return false;
  auto hex_val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  r = static_cast<double>(hex_val(h[0]) * 16 + hex_val(h[1])) / 255.0;
  g = static_cast<double>(hex_val(h[2]) * 16 + hex_val(h[3])) / 255.0;
  b = static_cast<double>(hex_val(h[4]) * 16 + hex_val(h[5])) / 255.0;
  return true;
}

static std::unordered_map<std::string, GtkThemeColors> s_gtkColorCache;

static std::unordered_map<std::string, GtkThemeDesign> s_gtkDesignCache;

// Determine if a color value was set (not the default)
static bool is_default_color(double r, double g, double b,
                              double defR, double defG, double defB) {
   
  const double eps = 0.001;
  return std::abs(r - defR) < eps && std::abs(g - defG) < eps && std::abs(b - defB) < eps;
}

static bool dir_has_gtk_css(const std::string& dir) {
   
  for (const char* rel : {"/gtk-3.0/gtk.css", "/gtk-4.0/gtk.css"}) {
    if (file_exists(dir + rel)) return true;
  }
  return false;
}

static int parse_css_px(const std::string& val) {
   
  std::string s;
  for (char c : val) {
    if (c >= '0' && c <= '9') s.push_back(c);
    else if (c == '.') s.push_back(c);
    else break;
  }
  if (s.empty()) return -1;
  return static_cast<int>(std::lround(std::atof(s.c_str())));
}

} // namespace

void clear_gtk_theme_colors_cache() {
   
  s_gtkColorCache.clear();
}

void clear_gtk_theme_design_cache() {
   
  s_gtkDesignCache.clear();
}

GtkThemeColors extract_gtk_theme_colors(const std::string& themeDir) {
   
  {
    auto it = s_gtkColorCache.find(themeDir);
    if (it != s_gtkColorCache.end()) return it->second;
  }

  GtkThemeColors out;

  auto read_css_file = [&](const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
      std::string s = line;
      size_t start = 0;
      while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
      if (start >= s.size()) continue;
      const char* p = s.c_str() + start;
      if (strncmp(p, "@define-color", 13) != 0) continue;
      p += 13;
      while (*p == ' ' || *p == '\t') ++p;
      if (!*p) continue;
      const char* nameStart = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') ++p;
      std::string name(nameStart, p);
      if (name.empty()) continue;
      while (*p == ' ' || *p == '\t') ++p;
      if (!*p) continue;
      const char* valStart = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != ';') ++p;
      std::string val(valStart, p);
      if (val.empty() || val[0] != '#') continue;

      double r, g, b;
      if (!parse_hex_color(val, r, g, b)) continue;

      for (auto& c : name) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
      }

      auto set_if = [&](double& rr, double& gg, double& bb, const std::string& n) {
        if (name == n) { rr = r; gg = g; bb = b; return true; }
        return false;
      };

      // Modern headerbar-style names first, then legacy names
      set_if(out.headerBgR, out.headerBgG, out.headerBgB, "headerbar_bg_color");
      set_if(out.headerFgR, out.headerFgG, out.headerFgB, "headerbar_fg_color");

      set_if(out.bgR, out.bgG, out.bgB, "window_bg_color")
      || set_if(out.bgR, out.bgG, out.bgB, "theme_bg_color")
      || set_if(out.bgR, out.bgG, out.bgB, "bg_color")
      || set_if(out.bgR, out.bgG, out.bgB, "popover_bg_color");

      set_if(out.fgR, out.fgG, out.fgB, "window_fg_color")
      || set_if(out.fgR, out.fgG, out.fgB, "theme_fg_color")
      || set_if(out.fgR, out.fgG, out.fgB, "fg_color")
      || set_if(out.fgR, out.fgG, out.fgB, "popover_fg_color");

      set_if(out.accentR, out.accentG, out.accentB, "accent_bg_color")
      || set_if(out.accentR, out.accentG, out.accentB, "theme_selected_bg_color")
      || set_if(out.accentR, out.accentG, out.accentB, "selected_bg_color")
      || set_if(out.accentR, out.accentG, out.accentB, "button_bg_color");

      set_if(out.baseR, out.baseG, out.baseB, "view_bg_color")
      || set_if(out.baseR, out.baseG, out.baseB, "theme_base_color")
      || set_if(out.baseR, out.baseG, out.baseB, "base_color")
      || set_if(out.baseR, out.baseG, out.baseB, "card_bg_color");

      set_if(out.textR, out.textG, out.textB, "view_fg_color")
      || set_if(out.textR, out.textG, out.textB, "theme_text_color")
      || set_if(out.textR, out.textG, out.textB, "text_color")
      || set_if(out.textR, out.textG, out.textB, "card_fg_color");
    }
  };

  for (const char* rel : {"/gtk-3.0/gtk.css", "/gtk-4.0/gtk.css"}) {
    read_css_file(themeDir + rel);
  }

  // CSS fallback: if @define-color didn't set enough, scan for selector colors
  if (is_default_color(out.bgR, out.bgG, out.bgB, 1, 1, 1) ||
      is_default_color(out.fgR, out.fgG, out.fgB, 0, 0, 0) ||
      is_default_color(out.accentR, out.accentG, out.accentB, 0.2, 0.5, 0.9)) {

    auto scan_css_selectors = [&](const std::string& cssPath) {
      std::ifstream f(cssPath);
      if (!f.is_open()) return;
      std::string line;

      std::string currentSelector;
      bool inBlock = false;
      int braceDepth = 0;

      while (std::getline(f, line)) {
        std::string s = line;
        bool hasContent = false;
        for (char c : s) {
          if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            hasContent = true; break;
          }
        }
        if (!hasContent) continue;

        if (!inBlock) {
          size_t bc = s.find('{');
          if (bc != std::string::npos) {
            currentSelector = s.substr(0, bc);
            // strip trailing whitespace from selector
            while (!currentSelector.empty() && (currentSelector.back() == ' ' || currentSelector.back() == '\t'))
              currentSelector.pop_back();
            inBlock = true;
            braceDepth = 1;
            // process properties after { on same line
            std::string after = s.substr(bc + 1);
            size_t cc = after.find('}');
            if (cc != std::string::npos) {
              inBlock = false;
              braceDepth = 0;
            }
          }
        } else {
          for (char c : s) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
              braceDepth--;
              if (braceDepth <= 0) {
                inBlock = false;
                currentSelector.clear();
                braceDepth = 0;
                break;
              }
            }
          }
        }

        if (!inBlock && !currentSelector.empty()) {
          currentSelector.clear();
          continue;
        }
        if (!inBlock || currentSelector.empty()) continue;

        // Inside a block, look for color properties
        auto extract_color = [](const std::string& str, size_t pos) -> std::string {
          size_t start = str.find_first_not_of(" \t", pos);
          if (start == std::string::npos) return {};
          if (start >= str.size() || str[start] != '#') return {};
          size_t end = start + 1;
          while (end < str.size() && (
            (str[end] >= '0' && str[end] <= '9') ||
            (str[end] >= 'a' && str[end] <= 'f') ||
            (str[end] >= 'A' && str[end] <= 'F')
          )) end++;
          return str.substr(start, end - start);
        };

        // Check for background-color
        auto bgPos = s.find("background-color");
        if (bgPos != std::string::npos) {
          auto colon = s.find(':', bgPos);
          if (colon != std::string::npos) {
            std::string hex = extract_color(s, colon + 1);
            if (!hex.empty()) {
              double cr, cg, cb;
              if (parse_hex_color(hex, cr, cg, cb)) {
                bool isWindow = currentSelector.find("window") != std::string::npos ||
                                currentSelector.find("WINDOW") != std::string::npos;
                bool isButton = currentSelector.find("button") != std::string::npos ||
                                currentSelector.find("BUTTON") != std::string::npos;
                bool isEntry = currentSelector.find("entry") != std::string::npos ||
                               currentSelector.find("textview") != std::string::npos ||
                               currentSelector.find("text") != std::string::npos;
                bool isHeader = currentSelector.find("headerbar") != std::string::npos ||
                                currentSelector.find("titlebar") != std::string::npos;
                bool isGlobal = currentSelector == "*";

                if (isHeader && is_default_color(out.headerBgR, out.headerBgG, out.headerBgB, 0.9, 0.9, 0.9)) {
                  out.headerBgR = cr; out.headerBgG = cg; out.headerBgB = cb;
                }
                if (isWindow && is_default_color(out.bgR, out.bgG, out.bgB, 1, 1, 1)) {
                  out.bgR = cr; out.bgG = cg; out.bgB = cb;
                }
                if (isButton && is_default_color(out.accentR, out.accentG, out.accentB, 0.2, 0.5, 0.9)) {
                  out.accentR = cr; out.accentG = cg; out.accentB = cb;
                }
                if (isEntry && is_default_color(out.baseR, out.baseG, out.baseB, 1, 1, 1)) {
                  out.baseR = cr; out.baseG = cg; out.baseB = cb;
                }
                if (isGlobal && is_default_color(out.bgR, out.bgG, out.bgB, 1, 1, 1)) {
                  out.bgR = cr; out.bgG = cg; out.bgB = cb;
                }
              }
            }
          }
        }

        // Check for color property
        auto colPos = s.find("color:");
        if (colPos != std::string::npos) {
          // Make sure it's not "background-color"
          bool isBgColor = false;
          if (colPos >= 16) {
            std::string before = s.substr(colPos - 16, 16);
            if (before.find("background") != std::string::npos) isBgColor = true;
          }
              if (!isBgColor) {
            auto colon = s.find(':', colPos);
            if (colon != std::string::npos) {
              std::string hex = extract_color(s, colon + 1);
              if (!hex.empty()) {
                double cr, cg, cb;
                if (parse_hex_color(hex, cr, cg, cb)) {
                  bool isWindow = currentSelector.find("window") != std::string::npos ||
                                  currentSelector.find("WINDOW") != std::string::npos;
                  bool isLabel = currentSelector.find("label") != std::string::npos ||
                                 currentSelector.find("text") != std::string::npos;
                  bool isHeader = currentSelector.find("headerbar") != std::string::npos ||
                                  currentSelector.find("titlebar") != std::string::npos;
                  bool isGlobal = currentSelector == "*";

                  if (isHeader && is_default_color(out.headerFgR, out.headerFgG, out.headerFgB, 0, 0, 0)) {
                    out.headerFgR = cr; out.headerFgG = cg; out.headerFgB = cb;
                  }
                  if (isWindow && is_default_color(out.fgR, out.fgG, out.fgB, 0, 0, 0)) {
                    out.fgR = cr; out.fgG = cg; out.fgB = cb;
                  }
                  if (isLabel && is_default_color(out.textR, out.textG, out.textB, 0, 0, 0)) {
                    out.textR = cr; out.textG = cg; out.textB = cb;
                  }
                  if (isGlobal && is_default_color(out.fgR, out.fgG, out.fgB, 0, 0, 0)) {
                    out.fgR = cr; out.fgG = cg; out.fgB = cb;
                  }
                  if (isGlobal && is_default_color(out.textR, out.textG, out.textB, 0, 0, 0)) {
                    out.textR = cr; out.textG = cg; out.textB = cb;
                  }
                }
              }
            }
          }
        }
      }
    };

    for (const char* rel : {"/gtk-3.0/gtk.css", "/gtk-4.0/gtk.css"}) {
      scan_css_selectors(themeDir + rel);
    }
  }

  // Ensure headerbar colors are derived if not set
  if (is_default_color(out.headerBgR, out.headerBgG, out.headerBgB, 0.9, 0.9, 0.9) &&
      !is_default_color(out.bgR, out.bgG, out.bgB, 1, 1, 1)) {
    out.headerBgR = out.bgR * 0.92;
    out.headerBgG = out.bgG * 0.92;
    out.headerBgB = out.bgB * 0.92;
  }
  if (is_default_color(out.headerFgR, out.headerFgG, out.headerFgB, 0, 0, 0) &&
      !is_default_color(out.fgR, out.fgG, out.fgB, 0, 0, 0)) {
    out.headerFgR = out.fgR;
    out.headerFgG = out.fgG;
    out.headerFgB = out.fgB;
  }
  if (is_default_color(out.baseR, out.baseG, out.baseB, 1, 1, 1) &&
      !is_default_color(out.bgR, out.bgG, out.bgB, 1, 1, 1)) {
    out.baseR = out.bgR;
    out.baseG = out.bgG;
    out.baseB = out.bgB;
  }
  if (is_default_color(out.textR, out.textG, out.textB, 0, 0, 0) &&
      !is_default_color(out.fgR, out.fgG, out.fgB, 0, 0, 0)) {
    out.textR = out.fgR;
    out.textG = out.fgG;
    out.textB = out.fgB;
  }

  out.valid = true;
  s_gtkColorCache[themeDir] = out;
  return out;
}

std::vector<GtkThemeInfo> list_installed_gtk_themes() {
   
  const auto bases = icon_base_dirs();
  std::unordered_map<std::string, GtkThemeInfo> byId;

  for (const auto& base : bases) {
    DIR* d = opendir(base.c_str());
    if (!d) continue;
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      const std::string id(ent->d_name);
      if (id.empty() || id == "." || id == "..") continue;
      if (byId.contains(id)) continue;
      const std::string themeDir = base + "/" + id;
      if (!dir_has_gtk_css(themeDir)) continue;
      GtkThemeInfo info{};
      info.id = id;
      info.path = themeDir;
      const std::string idx = themeDir + "/index.theme";
      info.name = file_exists(idx) ? read_ini_kv(idx, "[Desktop Entry]", "Name") : std::string{};
      if (info.name.empty()) info.name = id;
      byId[id] = info;
    }
    closedir(d);
  }

  std::vector<GtkThemeInfo> out;
  out.reserve(byId.size());
  for (auto& [_, v] : byId) out.push_back(v);
  std::sort(out.begin(), out.end(), [](const GtkThemeInfo& a, const GtkThemeInfo& b) { return a.name < b.name; });
  return out;
}

std::string detect_system_gtk_theme() {
   
  const std::string raw = run_cmd_capture("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null");
  if (!raw.empty()) {
    std::string s = trim(raw);
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
      s = s.substr(1, s.size() - 2);
      if (!s.empty()) return s;
    }
  }
  const char* home = std::getenv("HOME");
  if (home) {
    for (const char* rel : {"/.config/gtk-4.0/settings.ini", "/.config/gtk-3.0/settings.ini"}) {
      const std::string v = read_ini_kv(std::string(home) + rel, "[Settings]", "gtk-theme-name");
      if (!v.empty()) return v;
    }
  }
  return {};
}

bool apply_gtk_theme(const std::string& themeId) {
   
  if (themeId.empty()) return false;
  const std::string cmd = "gsettings set org.gnome.desktop.interface gtk-theme '" + themeId + "' 2>/dev/null";
  (void)std::system(cmd.c_str());
  // fallback: write to the toolkit settings.ini
  const char* home = std::getenv("HOME");
  if (!home) return true;
  auto write_ini = [&](const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "[Settings]\ngtk-theme-name=%s\n", themeId.c_str());
    fclose(f);
  };
  for (const char* rel : {"/.config/gtk-4.0/settings.ini", "/.config/gtk-3.0/settings.ini"}) {
    write_ini(std::string(home) + rel);
  }
  return true;
}

GtkThemeDesign extract_gtk_theme_design(const std::string& themeDir) {
   
  {
    auto it = s_gtkDesignCache.find(themeDir);
    if (it != s_gtkDesignCache.end()) return it->second;
  }

  GtkThemeDesign out;

  auto scan_css = [&](const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;

    std::string currentSelector;
    bool inBlock = false;
    int braceDepth = 0;

    while (std::getline(f, line)) {
      std::string s = line;
      bool hasContent = false;
      for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { hasContent = true; break; }
      }
      if (!hasContent) continue;

      if (!inBlock) {
        size_t bc = s.find('{');
        if (bc != std::string::npos) {
          currentSelector = s.substr(0, bc);
          while (!currentSelector.empty() && (currentSelector.back() == ' ' || currentSelector.back() == '\t'))
            currentSelector.pop_back();
          inBlock = true;
          braceDepth = 1;
        }
      } else {
        for (char c : s) {
          if (c == '{') braceDepth++;
          else if (c == '}') {
            braceDepth--;
            if (braceDepth <= 0) { inBlock = false; currentSelector.clear(); braceDepth = 0; break; }
          }
        }
      }
      if (!inBlock) { currentSelector.clear(); continue; }
      if (currentSelector.empty()) continue;

      auto extract_prop = [&](const std::string& prop) -> std::string {
        size_t p = s.find(prop);
        if (p == std::string::npos) return {};
        size_t colon = s.find(':', p + prop.size());
        if (colon == std::string::npos) return {};
        size_t start = s.find_first_not_of(" \t", colon + 1);
        if (start == std::string::npos) return {};
        size_t end = start;
        while (end < s.size() && s[end] != ';' && s[end] != '}' && s[end] != '\n' && s[end] != '\r') end++;
        return s.substr(start, end - start);
      };

      bool isWindow = currentSelector.find("window") != std::string::npos ||
                      currentSelector.find("decoration") != std::string::npos;
      bool isButton = currentSelector.find("button") != std::string::npos;
      bool isEntry = currentSelector.find("entry") != std::string::npos ||
                     currentSelector.find("textview") != std::string::npos;
      bool isHeader = currentSelector.find("headerbar") != std::string::npos ||
                      currentSelector.find("titlebar") != std::string::npos;
      bool isGlobal = (currentSelector == "*");

      // border-radius
      std::string br = extract_prop("border-radius");
      if (!br.empty()) {
        int px = parse_css_px(br);
        if (px >= 0) {
          if (isWindow && px > 0) out.windowRad = px;
          if (isButton && px > 0) out.buttonRad = px;
          if (isEntry && px > 0) out.entryRad = px;
          if (isGlobal && px > 0) {
            if (out.windowRad == 6) out.windowRad = px;
            if (out.buttonRad == 3) out.buttonRad = px;
            if (out.entryRad == 2) out.entryRad = px;
          }
        }
      }

      // border-width
      std::string bw = extract_prop("border-width");
      if (!bw.empty()) {
        int px = parse_css_px(bw);
        if (px >= 0 && isWindow) out.borderW = px;
      }

      // min-height for headerbar
      if (isHeader) {
        std::string mh = extract_prop("min-height");
        if (!mh.empty()) {
          int px = parse_css_px(mh);
          if (px > 0 && px < 200) {
            // Scale down to preview
            out.titlebarH = std::max(12, std::min(40, px / 2));
          }
        }
      }
    }
  };

  for (const char* rel : {"/gtk-3.0/gtk.css", "/gtk-4.0/gtk.css"}) {
    scan_css(themeDir + rel);
  }

  out.valid = true;
  s_gtkDesignCache[themeDir] = out;
  return out;
}

} // namespace eh::theming
