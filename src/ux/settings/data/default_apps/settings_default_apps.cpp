#include "ux/settings/data/default_apps/settings_default_apps.hpp"

#include <algorithm>

#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"

#include <cairo/cairo.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace eh::settings::default_apps {
namespace {

constexpr int kMinRowH = 34;
constexpr int kMaxRowH = 96;
constexpr int kRefViewportH = 640;

struct CatMeta {
  const char* label;
  const char* detail;
  const char* const* mimes;
};

static const char* kMimeWeb[] = {"x-scheme-handler/http", "x-scheme-handler/https", nullptr};
static const char* kMimeMail[] = {"x-scheme-handler/mailto", nullptr};
static const char* kMimeCal[] = {"text/calendar", "x-scheme-handler/webcal", nullptr};
static const char* kMimeFm[] = {"inode/directory", nullptr};
static const char* kMimeTerm[] = {"application/x-terminal-emulator", nullptr};
static const char* kMimeMusic[] = {"audio/mpeg", "audio/flac", "audio/ogg", nullptr};
static const char* kMimeVideo[] = {"video/mp4", "video/webm", "video/x-matroska", nullptr};
static const char* kMimeImg[] = {"image/png", "image/jpeg", "image/webp", nullptr};
static const char* kMimePdf[] = {"application/pdf", nullptr};

static const char* kQueryMime[] = {
    "x-scheme-handler/https", "x-scheme-handler/mailto", "text/calendar", "inode/directory",
    "application/x-terminal-emulator", "audio/flac", "video/mp4", "image/png", "application/pdf",
};

static const CatMeta kCat[] = {
    {"Web browser", "HTTP and HTTPS links", kMimeWeb},
    {"Email", "mailto links", kMimeMail},
    {"Calendar", "Calendar files and webcal", kMimeCal},
    {"File manager", "Folders and file browser", kMimeFm},
    {"Terminal", "Terminal emulator", kMimeTerm},
    {"Audio", "Music and other audio types", kMimeMusic},
    {"Video", "Video playback", kMimeVideo},
    {"Images", "Image viewing (raster / photo)", kMimeImg},
    {"PDF", "PDF and document viewing", kMimePdf},
};

[[nodiscard]] bool semicolon_token_equals(std::string_view cats_lower, std::string_view token) {
   
  size_t start = 0;
  while (start < cats_lower.size()) {
    const size_t semi = cats_lower.find(';', start);
    std::string_view seg =
        semi == std::string_view::npos ? cats_lower.substr(start) : cats_lower.substr(start, semi - start);
    while (!seg.empty() && seg.front() == ' ') seg.remove_prefix(1);
    while (!seg.empty() && seg.back() == ' ') seg.remove_suffix(1);
    if (seg == token) return true;
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return false;
}

[[nodiscard]] bool desktop_entry_is_calendar_app(const eh::app_drawer::DesktopEntry& e) {
  std::string catsLower;
  catsLower.resize(e.categories.size());
  std::transform(e.categories.begin(), e.categories.end(), catsLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string& c = catsLower;
  const std::string& m = e.mimeTypesLower;
  if (!c.empty() && semicolon_token_equals(c, "calendar")) return true;
  if (m.empty()) return false;
  const bool mimeCal = semicolon_token_equals(m, "text/calendar") || semicolon_token_equals(m, "text/x-vcalendar") ||
                       semicolon_token_equals(m, "application/ics") || semicolon_token_equals(m, "text/x-ical") ||
                       semicolon_token_equals(m, "model/calendar") || semicolon_token_equals(m, "x-scheme-handler/webcal");
  if (!mimeCal) return false;
  if (!c.empty() && semicolon_token_equals(c, "webbrowser")) return false;
  return true;
}

[[nodiscard]] bool desktop_entry_matches_picker_category(int category_index, const eh::app_drawer::DesktopEntry& e) {
  std::string catsLower;
  catsLower.resize(e.categories.size());
  std::transform(e.categories.begin(), e.categories.end(), catsLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string& c = catsLower;

  if (category_index != 2 && c.empty()) return false;
  switch (category_index) {
    case 0: return semicolon_token_equals(c, "webbrowser");
    case 1: return semicolon_token_equals(c, "email");
    case 2: return desktop_entry_is_calendar_app(e);
    case 3: return semicolon_token_equals(c, "filemanager");
    case 4: return semicolon_token_equals(c, "terminalemulator");
    case 5:
      return semicolon_token_equals(c, "audio") || semicolon_token_equals(c, "audioplayer") ||
             semicolon_token_equals(c, "music") || semicolon_token_equals(c, "sequencer");
    case 6:
      return semicolon_token_equals(c, "video") || semicolon_token_equals(c, "videoplayer") ||
             semicolon_token_equals(c, "tv") ||
             (semicolon_token_equals(c, "player") && semicolon_token_equals(c, "audiovideo"));
    case 7:
      return semicolon_token_equals(c, "rastergraphics") || semicolon_token_equals(c, "2dgraphics") ||
             semicolon_token_equals(c, "photography") || semicolon_token_equals(c, "scanning") ||
             (semicolon_token_equals(c, "viewer") && semicolon_token_equals(c, "graphics"));
    case 8:
      return semicolon_token_equals(c, "pdf") ||
             (semicolon_token_equals(c, "viewer") && semicolon_token_equals(c, "office"));
    default: return false;
  }
}

[[nodiscard]] std::string shell_quote_single(const std::string& s) {
  std::string o = "'";
  for (char c : s) {
    if (c == '\'')
      o += "'\\''";
    else
      o += c;
  }
  return o + "'";
}

[[nodiscard]] std::string trim_ws(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
  return s.substr(a);
}

[[nodiscard]] std::string popen_line(const char* cmd) {
  FILE* p = popen(cmd, "r");
  if (!p) return {};
  std::string out;
  char buf[512];
  if (fgets(buf, sizeof(buf), p)) out = trim_ws(std::string(buf));
  int st = pclose(p);
  if (st != 0) std::cerr << "[default-apps] pclose status=" << st << '\n';
  return out;
}

[[nodiscard]] std::string& field_ref(eh::config::DefaultAppsSettings& d, int i) {
  switch (i) {
    case 0: return d.web;
    case 1: return d.mail;
    case 2: return d.calendar;
    case 3: return d.fileManager;
    case 4: return d.terminal;
    case 5: return d.music;
    case 6: return d.video;
    case 7: return d.images;
    case 8: return d.pdf;
    default: {
      static std::string dummy;
      return dummy;
    }
  }
}

}

const char* category_label(int index) {
   
  if (index < 0 || index >= kNumCategories) return "";
  return kCat[static_cast<size_t>(index)].label;
}

const char* const* category_mime_list(int index) {
  if (index < 0 || index >= kNumCategories) return nullptr;
  return kCat[static_cast<size_t>(index)].mimes;
}

std::string query_effective_desktop_id(int category_index) {
   
  if (category_index < 0 || category_index >= kNumCategories) return {};
  const char* mime = kQueryMime[static_cast<size_t>(category_index)];
  const std::string inner =
      std::string("command -v xdg-mime >/dev/null 2>&1 && xdg-mime query default ") + mime;
  const std::string cmd = "/bin/sh -c " + shell_quote_single(inner);
  return popen_line(cmd.c_str());
}

void set_stored_desktop_id(eh::config::DefaultAppsSettings& d, int category_index, std::string desktop_basename) {
   
  if (category_index < 0 || category_index >= kNumCategories) return;
  field_ref(d, category_index) = std::move(desktop_basename);
}

void clear_stored_desktop_id(eh::config::DefaultAppsSettings& d, int category_index) {
   
  if (category_index < 0 || category_index >= kNumCategories) return;
  field_ref(d, category_index).clear();
}

namespace {

std::string strip_desktop_file_suffix(std::string_view id) {
  std::string s(id);
  constexpr std::string_view suf = ".desktop";
  if (s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0) s.resize(s.size() - suf.size());
  return s;
}

}

std::string friendly_line_for_desktop_id(const std::string& desktop_id) {
   
  if (desktop_id.empty()) return {};
  const auto& apps = eh::app_drawer::get_cached_entries();
  for (const auto& e : apps) {
    fs::path p(e.path);
    if (p.filename().string() == desktop_id) {
      if (!e.name.empty()) return e.name;
      return strip_desktop_file_suffix(desktop_id);
    }
  }
  return strip_desktop_file_suffix(desktop_id);
}

void compute_default_apps_layout(int content_x, int content_w, int content_top_y, int viewport_h, DefaultAppsLayout* out) {
   
  if (!out) return;
  const int vh = std::max(180, viewport_h);
  const double sf = std::clamp(static_cast<double>(vh) / static_cast<double>(kRefViewportH), 0.72, 2.4);
  out->ui_scale = sf;
  out->top_pad = std::max(4, static_cast<int>(std::lround(8.0 * sf)));
  out->card_inset = std::max(6, static_cast<int>(std::lround(8.0 * sf)));
  out->inner_pad = std::max(8, static_cast<int>(std::lround(16.0 * sf)));
  out->gap_cards = std::max(8, static_cast<int>(std::lround(24.0 * sf)));
  out->header_h = std::clamp(static_cast<int>(std::lround(88.0 * sf)), 56, 132);
  out->header_x = content_x + out->card_inset;
  out->header_w = std::max(80, content_w - 2 * out->card_inset);
  out->header_y = content_top_y + out->top_pad;
  out->apps_x = out->header_x;
  out->apps_w = out->header_w;
  out->apps_y = out->header_y + out->header_h + out->gap_cards;
  out->first_row_y = out->apps_y + out->inner_pad;

  const int reserved = out->top_pad + out->header_h + out->gap_cards + out->inner_pad * 2;
  const int avail = std::max(0, vh - reserved);
  out->row_h = std::clamp(avail / kNumCategories, kMinRowH, kMaxRowH);
  out->dropdown_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(out->row_h) * 0.82)), 24, out->row_h - 2);
  out->apps_h = out->inner_pad * 2 + kNumCategories * out->row_h;
}

void default_apps_dropdown_rect(const DefaultAppsLayout& L, int row_index, int* dx, int* dy, int* dw, int* dh) {
  if (!dx || !dy || !dw || !dh) return;
  if (row_index < 0 || row_index >= kNumCategories) {
    *dx = *dy = *dw = *dh = 0;
    return;
  }
  const int innerLeft = L.apps_x + L.inner_pad;
  const int innerRight = L.apps_x + L.apps_w - L.inner_pad;
  const int rowW = innerRight - innerLeft;
  const int splitX = innerLeft + rowW / 2;
  const int marginS = std::max(6, static_cast<int>(std::lround(8.0 * L.ui_scale)));
  *dx = splitX;
  *dy = L.first_row_y + row_index * L.row_h + (L.row_h - L.dropdown_h) / 2;
  *dw = std::max(80, innerRight - splitX - marginS);
  *dh = L.dropdown_h;
}

const char* category_material_icon(int row_index) {
  static const char* const kIcons[] = {"language",        "mail",           "calendar_month", "folder",
                                       "terminal",        "library_music",  "movie",          "image",
                                       "picture_as_pdf"};
  if (row_index < 0 || row_index >= kNumCategories) return "apps";
  return kIcons[static_cast<size_t>(row_index)];
}

void hit_row_control(double lx, double ly, const DefaultAppsLayout& L, int* out_row, int* out_control) {
   
  *out_row = -1;
  *out_control = 0;
  for (int row = 0; row < kNumCategories; ++row) {
    int dx = 0;
    int dy = 0;
    int dw = 0;
    int dh = 0;
    default_apps_dropdown_rect(L, row, &dx, &dy, &dw, &dh);
    const int ix = static_cast<int>(lx);
    const int iy = static_cast<int>(ly);
    if (ix >= dx && ix < dx + dw && iy >= dy && iy < dy + dh) {
      *out_row = row;
      *out_control = 1;
      return;
    }
  }
}

void filter_picker_entries_for_category(int category_index, std::vector<eh::app_drawer::DesktopEntry>* entries) {
   
  if (!entries || category_index < 0 || category_index >= kNumCategories) return;
  auto& v = *entries;
  const std::vector<eh::app_drawer::DesktopEntry> backup = v;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const eh::app_drawer::DesktopEntry& e) {
             return !desktop_entry_matches_picker_category(category_index, e);
           }),
           v.end());

  if (v.empty() && !backup.empty() && category_index != 2) v = backup;
}

}
