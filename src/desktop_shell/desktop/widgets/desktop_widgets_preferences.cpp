#include "desktop/desktop_widgets_preferences.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace eh::shell::desktop {

static const char* kFileName = "desktop_widgets_layout.txt";

static std::string cfg_dir() {
   
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) return std::string(xdg) + "/event-horizon/";
  const char* home = std::getenv("HOME");
  if (home) return std::string(home) + "/.config/event-horizon/";
  return {};
}

void desktop_widgets_prefs_save(const std::vector<DesktopWidgetConfig>& configs) {
   
  const std::string dir = cfg_dir();
  if (dir.empty()) return;
  const std::string path = dir + kFileName;
  const std::string tmp = path + ".tmp";

  if (access(dir.c_str(), F_OK) != 0) (void)mkdir(dir.c_str(), 0755);
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) {
    std::cerr << "[widgets-prefs] fopen failed path=\"" << tmp << "\" errno=" << errno << "\n";
    return;
  }

  for (size_t i = 0; i < configs.size(); ++i) {
    const auto& wc = configs[i];
    fprintf(f, "desktop_widget_%zu_type=%d\n", i, static_cast<int>(wc.type));
    fprintf(f, "desktop_widget_%zu_id=%s\n", i, wc.id.c_str());
    fprintf(f, "desktop_widget_%zu_posX=%d\n", i, wc.posX);
    fprintf(f, "desktop_widget_%zu_posY=%d\n", i, wc.posY);
    fprintf(f, "desktop_widget_%zu_scale=%.2f\n", i, wc.scale);
    fprintf(f, "desktop_widget_%zu_timeFormat=%s\n", i, wc.timeFormat.c_str());
    fprintf(f, "desktop_widget_%zu_showSeconds=%d\n", i, wc.showSeconds ? 1 : 0);
    fprintf(f, "desktop_widget_%zu_showDate=%d\n", i, wc.showDate ? 1 : 0);
    fprintf(f, "desktop_widget_%zu_fontSize=%d\n", i, wc.fontSize);
    if (!wc.outputName.empty())
      fprintf(f, "desktop_widget_%zu_output=%s\n", i, wc.outputName.c_str());
  }

  if (fflush(f) != 0) {
    const int e = errno;
    fclose(f);
    std::cerr << "[widgets-prefs] fflush failed errno=" << e << "\n";
    return;
  }
  if (const int sfd = fileno(f); sfd >= 0) (void)fsync(sfd);

  if (fclose(f) != 0) {
    std::cerr << "[widgets-prefs] fclose failed\n";
    return;
  }

  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::cerr << "[widgets-prefs] rename failed\n";
  }
}

std::vector<DesktopWidgetConfig> desktop_widgets_prefs_load() {
   
  std::vector<DesktopWidgetConfig> configs;

  const std::string dir = cfg_dir();
  if (dir.empty()) return configs;
  const std::string path = dir + kFileName;

  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return configs;

  std::string line;
  line.reserve(512);
  while (true) {
    const int c = fgetc(f);
    if (c == EOF) break;
    if (c == '\n') {
      if (line.rfind("desktop_widget_", 0) == 0) {
        const std::string kDwPrefix = "desktop_widget_";
        size_t p = kDwPrefix.size();
        size_t e = p;
        while (e < line.size() && line[e] >= '0' && line[e] <= '9') e++;
        if (e > p && e < line.size() && line[e] == '_') {
          const int idx = std::stoi(line.substr(p, e - p));
          const size_t fs = e + 1;
          const size_t eq = line.find('=', fs);
          if (eq != std::string::npos && fs < eq) {
            const std::string field = line.substr(fs, eq - fs);
            const std::string val = line.substr(eq + 1);
            while (idx >= static_cast<int>(configs.size()))
              configs.push_back({});
            auto& wc = configs[idx];
            if (field == "type") wc.type = static_cast<DesktopWidgetType>(std::stoi(val));
            else if (field == "id") wc.id = val;
            else if (field == "posX") wc.posX = std::stoi(val);
            else if (field == "posY") wc.posY = std::stoi(val);
            else if (field == "scale") wc.scale = std::stod(val);
            else if (field == "timeFormat") wc.timeFormat = val;
            else if (field == "showSeconds") wc.showSeconds = (val == "1");
            else if (field == "showDate") wc.showDate = (val == "1");
            else if (field == "fontSize") wc.fontSize = std::stoi(val);
            else if (field == "output") wc.outputName = val;
          }
        }
      }
      line.clear();
    } else {
      line += static_cast<char>(c);
    }
  }

  fclose(f);
  return configs;
}

}

