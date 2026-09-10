#include "desktop_shell/desktop/entries/desktop_open_with.hpp"

#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"

#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include <cairo/cairo.h>

namespace fs = std::filesystem;

using namespace eh::shell::shared;

namespace {

eh::icons::IconCache s_icon_cache;

std::string sh_quote(const std::string& s) {
   
  std::string q = "'";
  for (char c : s) {
    if (c == '\'')
      q += "'\\''";
    else
      q += c;
  }
  q += '\'';
  return q;
}

std::string guess_mime_by_extension(const std::string& path);

std::string detect_mime_type(const std::string& file_path) {
   
  const std::string quoted = sh_quote(file_path);
  const std::string cmd = "xdg-mime query filetype " + quoted + " 2>/dev/null || "
                          "file -b --mime-type " + quoted + " 2>/dev/null";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return {};
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), f)) out += buf;
  pclose(f);
  // Trim all trailing whitespace
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                          out.back() == ' ' || out.back() == '\t'))
    out.pop_back();
  // Trim leading whitespace
  while (!out.empty() && (out.front() == ' ' || out.front() == '\t'))
    out.erase(out.begin());
  // Lowercase for matching
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // If the detected type is ambiguous (empty file, generic binary), guess from extension
  if (out == "inode/x-empty" || out == "application/octet-stream") {
    std::string guessed = guess_mime_by_extension(file_path);
    if (!guessed.empty()) return guessed;
  }

  return out;
}

// Fallback: guess MIME type from file extension when content-based detection
// returns a generic type (e.g. inode/x-empty for empty files).
std::string guess_mime_by_extension(const std::string& path) {
   
  std::string ext = fs::path(path).extension().string();
  if (ext.empty()) return {};
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // Common extension -> MIME type mapping
  // clang-format off
  struct { const char* ext; const char* mime; } map[] = {
    {".txt", "text/plain"}, {".md", "text/markdown"},
    {".html", "text/html"}, {".htm", "text/html"},
    {".css", "text/css"}, {".js", "text/javascript"},
    {".json", "application/json"}, {".xml", "text/xml"},
    {".csv", "text/csv"}, {".yaml", "text/yaml"}, {".yml", "text/yaml"},
    {".ini", "text/plain"}, {".cfg", "text/plain"}, {".conf", "text/plain"}, {".log", "text/plain"},
    {".sh", "application/x-sh"}, {".py", "text/x-python"},
    {".cpp", "text/x-c++src"}, {".c", "text/x-csrc"},
    {".h", "text/x-chdr"}, {".hpp", "text/x-c++hdr"},
    {".java", "text/x-java"}, {".rs", "text/x-rust"}, {".go", "text/x-go"},
    {".ts", "text/typescript"}, {".tsx", "text/typescript-tsx"},
    {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
    {".gif", "image/gif"}, {".bmp", "image/bmp"}, {".svg", "image/svg+xml"},
    {".webp", "image/webp"}, {".ico", "image/vnd.microsoft.icon"},
    {".pdf", "application/pdf"},
    {".doc", "application/msword"}, {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".xls", "application/vnd.ms-excel"}, {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".ppt", "application/vnd.ms-powerpoint"}, {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".ogg", "audio/ogg"}, {".flac", "audio/flac"},
    {".mp4", "video/mp4"}, {".avi", "video/x-msvideo"}, {".mkv", "video/x-matroska"}, {".mov", "video/quicktime"}, {".webm", "video/webm"},
    {".zip", "application/zip"}, {".tar", "application/x-tar"},
    {".gz", "application/gzip"}, {".bz2", "application/x-bzip2"}, {".xz", "application/x-xz"},
    {".7z", "application/x-7z-compressed"}, {".rar", "application/vnd.rar"},
  };
  // clang-format on
  for (auto& ent : map) {
    if (ext == ent.ext) return ent.mime;
  }
  return {};
}

bool in_rect(double lx, double ly, const double r[4]) {
   
  return lx >= r[0] && lx < r[0] + r[2] && ly >= r[1] && ly < r[1] + r[3];
}

// Collect desktop file IDs associated with a MIME type by reading the
// system's canonical MIME cache and mimeapps.list files. This is the same
// database used by xdg-open and every other desktop environment.
static std::vector<std::string> get_mime_associations(const std::string& mime_type) {
   
  // Read a mimeinfo.cache file, return all IDs after "mime/type="
  auto read_cache = [&](const std::string& path) -> std::string {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return {};
    std::string result;
    char buf[1024];
    const std::string prefix = mime_type + "=";
    while (fgets(buf, sizeof(buf), f)) {
      std::string line = buf;
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
      if (line.size() > prefix.size() &&
          line.compare(0, prefix.size(), prefix) == 0) {
        result = line.substr(prefix.size());
        break;
      }
    }
    fclose(f);
    return result;
  };

  // Read a mimeapps.list file, return all desktop IDs for the given MIME
  // type from both [Default Applications] and [Added Associations] sections.
  auto read_apps = [&](const std::string& path) -> std::string {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return {};
    std::string result;
    char buf[1024];
    bool inDefaults = false;
    bool inAdded = false;
    const std::string prefix = mime_type + "=";
    while (fgets(buf, sizeof(buf), f)) {
      std::string line = buf;
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
      if (line == "[Default Applications]") { inDefaults = true; inAdded = false; continue; }
      if (line == "[Added Associations]") { inAdded = true; inDefaults = false; continue; }
      if (line.empty() || line[0] == '#' || line[0] == '[') {
        if (!line.empty() && line[0] == '[' && line != "[Default Applications]" && line != "[Added Associations]")
          inDefaults = false, inAdded = false;
        continue;
      }
      if ((inDefaults || inAdded) && line.size() > prefix.size() &&
          line.compare(0, prefix.size(), prefix) == 0) {
        if (!result.empty()) result += ';';
        result += line.substr(prefix.size());
      }
    }
    fclose(f);
    return result;
  };

  // Parse a semicolon-separated list into unique IDs
  auto parse_ids = [](const std::string& list) -> std::vector<std::string> {
    std::vector<std::string> ids;
    size_t start = 0;
    while (start < list.size()) {
      size_t semi = list.find(';', start);
      std::string id = (semi == std::string::npos)
          ? list.substr(start) : list.substr(start, semi - start);
      // Trim whitespace
      while (!id.empty() && id.front() == ' ') id.erase(id.begin());
      while (!id.empty() && id.back() == ' ') id.pop_back();
      if (!id.empty()) ids.push_back(id);
      if (semi == std::string::npos) break;
      start = semi + 1;
    }
    return ids;
  };

  // Collect from all standard locations
  std::vector<std::string> all;
  auto add = [&](const std::string& list) {
    auto v = parse_ids(list);
    all.insert(all.end(), v.begin(), v.end());
  };

  const char* home = getenv("HOME");
  if (home) {
    std::string ud = std::string(home) + "/.local/share/applications";
    add(read_cache(ud + "/mimeinfo.cache"));
    add(read_apps(ud + "/mimeapps.list"));
    add(read_apps(std::string(home) + "/.config/mimeapps.list"));
  }
  add(read_cache("/usr/share/applications/mimeinfo.cache"));
  add(read_apps("/usr/share/applications/mimeapps.list"));

  return all;
}

void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
   
  cairo_new_path(cr);
  const double r = std::min({rad, rw * 0.5, rh * 0.5});
  const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

} // namespace

namespace eh::shell::desktop {

void open_with_open(DesktopApp& app, const std::string& abs_path) {
   
  auto& st = app.openWith;
  if (st.open) {
    eh_desktop_log("open_with_open: already open path=%s layerIdx=%zu layers=%zu",
                   abs_path.c_str(), st.layerIdx, app.layers.size());
    return;
  }

  const std::string mime_type = detect_mime_type(abs_path);
  const auto& entries = dock::app_drawer::get_cached_entries();

  // Query system MIME association database for recommended apps
  auto recommended_ids = get_mime_associations(mime_type);

  // Build a set for fast lookup
  std::unordered_set<std::string> rec_set;
  for (const auto& id : recommended_ids) rec_set.insert(id);

  // Also scan desktop file MimeType fields for the exact target MIME type.
  // This catches apps that declare support but aren't in the system cache yet.
  if (!mime_type.empty()) {
    for (const auto& ent : entries) {
      if (ent.noDisplay || ent.hidden) continue;
      // Check if mime_type appears as a delimited entry in the semicolon list
      // Wrap both in semicolons so that "text/plain" doesn't match "text/plain-something"
      const bool found = (";" + ent.mimeTypesLower + ";")
                             .find(";" + mime_type + ";") != std::string::npos;
      if (found) {
        std::string id = fs::path(ent.path).stem().string();
        rec_set.insert(id);
        rec_set.insert(id + ".desktop");
      }
    }
  }

  std::vector<OpenWithEntry> recommended;
  std::vector<OpenWithEntry> other;
  recommended.reserve(entries.size());
  other.reserve(entries.size());

  for (const auto& ent : entries) {
    if (ent.noDisplay || ent.hidden) continue;
    OpenWithEntry ae;
    ae.desktop_id = fs::path(ent.path).stem().string();
    ae.name = ent.name;
    // Check if this app's desktop ID is in the recommendation set
    // (system MIME associations + desktop file MimeType field)
    const bool isRec = rec_set.count(ae.desktop_id) ||
                       rec_set.count(ae.desktop_id + ".desktop");
    if (isRec)
      recommended.push_back(std::move(ae));
    else
      other.push_back(std::move(ae));
  }

  st.exactCount = static_cast<int>(recommended.size());
  st.familyCount = 0;
  st.apps = std::move(recommended);
  st.apps.insert(st.apps.end(), std::make_move_iterator(other.begin()),
                 std::make_move_iterator(other.end()));

  st.open = true;
  st.filePath = abs_path;
  st.mimeType = mime_type;
  st.layerIdx = app.pointerLayerIdx;
  st.hoverIdx = -1;
  st.selectedIdx = -1;
  st.scrollOffset = 0;
  st.setDefault = false;
  st.x = 0; st.y = 0; st.w = 0; st.h = 0;
  eh_desktop_log("open_with_open: OK path=%s mime=%s layerIdx=%zu layers=%zu apps=%zu",
                 abs_path.c_str(), mime_type.c_str(), st.layerIdx, app.layers.size(), st.apps.size());
}

void open_with_close(DesktopApp& app) {
   
  auto& st = app.openWith;
  eh_desktop_log("open_with_close: wasOpen=%d file=%s layerIdx=%zu->0 layers=%zu",
                 st.open, st.filePath.c_str(), st.layerIdx, app.layers.size());
  st.open = false;
  st.apps.clear();
  st.filePath.clear();
  st.mimeType.clear();
  st.hoverIdx = -1;
  st.selectedIdx = -1;
  st.scrollOffset = 0;
  st.exactCount = 0;
  st.familyCount = 0;
  st.setDefault = false;
  st.layerIdx = 0;
}

void paint_open_with(DesktopApp& app, cairo_t* cr) {
   
  auto& st = app.openWith;
  if (!st.open) return;
  if (st.layerIdx >= app.layers.size() || !app.layers[st.layerIdx]) {
    eh_desktop_log("paint_open_with: BAD layerIdx=%zu layers=%zu", st.layerIdx, app.layers.size());
    return;
  }
  const DesktopLayer& L = *app.layers[st.layerIdx];

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const auto mc = eh::config::derived_chrome_colors(sc.appearance);
  const double us = dock_ui_scale(sc.dock);
  const double kAccR = mc.accentR, kAccG = mc.accentG, kAccB = mc.accentB;
  const double dimR = mc.drawerDimR, dimG = mc.drawerDimG, dimB = mc.drawerDimB;
  const double surfR = mc.dockFillR, surfG = mc.dockFillG, surfB = mc.dockFillB;
  const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;

  const double W = static_cast<double>(L.configuredWidth);
  const double H = static_cast<double>(L.configuredHeight);

  const double kPad = 16.0 * us;
  const double kPadIn = 12.0 * us;
  const double kTopBarH = 44.0 * us;
  const double kEntryH = 40.0 * us;
  const double kBottomBarH = 52.0 * us;
  const double kMaxListH = 320.0 * us;
  const double kCardRad = 16.0 * us;

  const double cardW = std::min(480.0 * us, W - 32.0 * us);
  const int totalEntries = static_cast<int>(st.apps.size());
  const int maxVisible = std::max(1, static_cast<int>(kMaxListH / kEntryH));
  const int visibleEntries = std::min(totalEntries, maxVisible);
  const double listH = static_cast<double>(visibleEntries) * kEntryH;
  const double cardH = kPad + kTopBarH + kPadIn + listH + kPadIn + kBottomBarH + kPad;

  const double cx = (W - cardW) * 0.5;
  const double cy = (H - cardH) * 0.5;

  st.w = cardW;
  st.h = cardH;
  st.x = cx;
  st.y = cy;

  // Card shadow
  rr(cr, cx + 2.0 * us, cy + 3.0 * us, cardW, cardH, kCardRad);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
  cairo_fill(cr);

  // Card background
  rr(cr, cx, cy, cardW, cardH, kCardRad);
  cairo_set_source_rgba(cr, surfR, surfG, surfB, 0.98);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Close button
  const double closeSz = 28.0 * us;
  st.hitClose[0] = cx + cardW - kPad - closeSz;
  st.hitClose[1] = cy + kPad - 4.0 * us;
  st.hitClose[2] = closeSz;
  st.hitClose[3] = closeSz;
  {
    const int cHov = st.hoverIdx == -2 ? 1 : 0;
    rr(cr, st.hitClose[0], st.hitClose[1], closeSz, closeSz, 8.0 * us);
    cairo_set_source_rgba(cr, dimR, dimG, dimB, cHov ? 0.55 : 0.40);
    cairo_fill(cr);
    draw_material_glyph(cr, st.hitClose[0] + closeSz * 0.5,
                         st.hitClose[1] + closeSz * 0.5,
                         18.0 * us, "close", 0.88, 0.92, 0.95, 1.0);
  }

  // Title
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16.0 * us);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 1.0);
  cairo_move_to(cr, cx + kPad, cy + kPad + 18.0 * us);
  cairo_show_text(cr, "Open With");

  // Filename subtitle
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12.0 * us);
  cairo_set_source_rgba(cr, outR + 0.15, outG + 0.12, outB + 0.12, 0.75);
  std::string fname = fs::path(st.filePath).filename().string();
  cairo_move_to(cr, cx + kPad, cy + kPad + 36.0 * us);
  cairo_show_text(cr, fname.c_str());

  // Separator under top bar
  const double sep1Y = cy + kPad + kTopBarH;
  cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
  cairo_rectangle(cr, cx, sep1Y, cardW, 1.0);
  cairo_fill(cr);

  // Clipped list area
  const double listX = cx + kPadIn;
  const double listY = cy + kPad + kTopBarH + kPadIn;
  const double listW = cardW - 2.0 * kPadIn;
  const double scrollbarW = 6.0 * us;

  cairo_save(cr);
  cairo_rectangle(cr, listX, listY, listW, listH);
  cairo_clip(cr);

  const int start = st.scrollOffset;
  const int end = std::min(start + visibleEntries, totalEntries);
  const int recommendedEnd = st.exactCount + st.familyCount;
  const bool hasDivider = (recommendedEnd > 0 && recommendedEnd < totalEntries);

  for (int i = start; i < end; ++i) {
    const double ey = listY + static_cast<double>(i - start) * kEntryH;

    // Clean section divider between recommended and other apps
    if (hasDivider && i == recommendedEnd) {
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.15);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, listX + kPadIn, ey - 0.5);
      cairo_line_to(cr, listX + listW - kPadIn, ey - 0.5);
      cairo_stroke(cr);
    }

    const bool sel = (i == st.selectedIdx);
    const bool hov = (i == st.hoverIdx);

    if (sel) {
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.20);
      cairo_rectangle(cr, listX, ey, listW, kEntryH);
      cairo_fill(cr);
    } else if (hov) {
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.10);
      cairo_rectangle(cr, listX, ey, listW, kEntryH);
      cairo_fill(cr);
    }

    // Separator line between entries
    cairo_set_source_rgba(cr, outR, outG, outB, 0.08);
    cairo_move_to(cr, listX + kPadIn, ey + kEntryH - 0.5);
    cairo_line_to(cr, listX + listW - kPadIn, ey + kEntryH - 0.5);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);

    // App icon
    const double iconSize = 24.0 * us;
    const double iconX = listX + 6.0 * us;
    const double iconY = ey + (kEntryH - iconSize) * 0.5;
    const auto* iconEntry = s_icon_cache.app_icon(st.apps[i].desktop_id);
    if (iconEntry && iconEntry->surface) {
      double iw = static_cast<double>(iconEntry->width);
      double ih = static_cast<double>(iconEntry->height);
      double scale = iconSize / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      cairo_translate(cr, iconX, iconY);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, iconEntry->surface,
                               ((iconSize / scale) - iw) * 0.5,
                               ((iconSize / scale) - ih) * 0.5);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      cairo_arc(cr, iconX + iconSize * 0.5, iconY + iconSize * 0.5,
                iconSize * 0.5, 0, 2.0 * M_PI);
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.5);
      cairo_fill(cr);
      char letter[2] = {st.apps[i].name.empty() ? '?' : st.apps[i].name[0], '\0'};
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, iconSize * 0.5);
      cairo_text_extents_t te;
      cairo_text_extents(cr, letter, &te);
      cairo_move_to(cr, iconX + (iconSize - te.width) * 0.5 - te.x_bearing,
                    iconY + (iconSize + te.height) * 0.5 - te.y_bearing);
      cairo_show_text(cr, letter);
    }

    // App name
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 0.95);
    cairo_move_to(cr, iconX + iconSize + 8.0 * us, ey + kEntryH * 0.5 + 5.0 * us);
    cairo_show_text(cr, st.apps[i].name.c_str());
  }

  cairo_restore(cr); // remove clip

  // Scrollbar
  if (totalEntries > visibleEntries) {
    const double sbTrackH = listH;
    const double sbH = std::max(scrollbarW * 2.0,
                                sbTrackH * static_cast<double>(visibleEntries) /
                                    static_cast<double>(totalEntries));
    const double sbMax = sbTrackH - sbH;
    const double frac = sbMax > 0
        ? static_cast<double>(st.scrollOffset) /
              static_cast<double>(totalEntries - visibleEntries)
        : 0.0;
    const double sbY = listY + frac * sbMax;
    const double sx = listX + listW - scrollbarW - 2.0 * us;
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.55, 0.3);
    rr(cr, sx, sbY, scrollbarW, sbH, scrollbarW * 0.5);
    cairo_fill(cr);
  }

  // Separator above bottom bar
  const double sep2Y = cy + kPad + kTopBarH + kPadIn + listH + kPadIn;
  cairo_set_source_rgba(cr, outR, outG, outB, 0.15);
  cairo_rectangle(cr, cx, sep2Y, cardW, 1.0);
  cairo_fill(cr);

  // Bottom bar layout
  const double bottomY = sep2Y + 1.0;
  const double bottomClientH = kBottomBarH;

  // "Set as default" checkbox
  const double defAreaY = bottomY + (bottomClientH - 24.0 * us) * 0.5;
  const double checkSize = 20.0 * us;
  st.hitDefault[0] = listX;
  st.hitDefault[1] = defAreaY;
  st.hitDefault[2] = 200.0 * us;
  st.hitDefault[3] = 24.0 * us;

  const double checkBoxX = st.hitDefault[0];
  const double checkBoxY = defAreaY + (24.0 * us - checkSize) * 0.5;
  const bool hDef = (st.hoverIdx == -5);

  if (st.setDefault) {
    cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.85);
  } else {
    cairo_set_source_rgba(cr, dimR, dimG, dimB, hDef ? 0.55 : 0.40);
  }
  rr(cr, checkBoxX, checkBoxY, checkSize, checkSize, 4.0 * us);
  cairo_fill(cr);

  if (st.setDefault) {
    // Checkmark
    cairo_set_source_rgba(cr, 0.08, 0.1, 0.12, 0.95);
    cairo_set_line_width(cr, 2.5 * us);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, checkBoxX + 4.0 * us, checkBoxY + checkSize * 0.55);
    cairo_line_to(cr, checkBoxX + checkSize * 0.40, checkBoxY + checkSize * 0.80);
    cairo_line_to(cr, checkBoxX + checkSize * 0.80, checkBoxY + checkSize * 0.25);
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  }

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12.0 * us);
  cairo_set_source_rgba(cr, 0.80, 0.82, 0.85, 0.9);
  cairo_move_to(cr, checkBoxX + checkSize + 8.0 * us, defAreaY + 17.0 * us);
  cairo_show_text(cr, "Set as default");

  // Cancel / Open buttons
  const double btnH = 34.0 * us;
  const double btnW = 90.0 * us;
  const double btnGap = 10.0 * us;
  const double btnY = bottomY + (bottomClientH - btnH) * 0.5;
  const double btnRight = cx + cardW - kPad;

  st.hitCancel[0] = btnRight - btnW * 2 - btnGap;
  st.hitCancel[1] = btnY;
  st.hitCancel[2] = btnW;
  st.hitCancel[3] = btnH;

  st.hitOpen[0] = btnRight - btnW;
  st.hitOpen[1] = btnY;
  st.hitOpen[2] = btnW;
  st.hitOpen[3] = btnH;

  // Cancel button
  {
    const bool hCancel = (st.hoverIdx == -3);
    rr(cr, st.hitCancel[0], btnY, btnW, btnH, btnH * 0.5);
    cairo_set_source_rgba(cr, dimR, dimG, dimB, hCancel ? 0.52 : 0.42);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, outR, outG, outB, 0.45);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_set_source_rgba(cr, 0.90, 0.94, 0.96, 0.95);
    cairo_text_extents_t te;
    cairo_text_extents(cr, "Cancel", &te);
    cairo_move_to(cr, st.hitCancel[0] + (btnW - te.x_advance) * 0.5,
                  btnY + btnH * 0.5 + te.height * 0.35);
    cairo_show_text(cr, "Cancel");
  }

  // Open button
  {
    const bool hOpen = (st.hoverIdx == -4);
    rr(cr, st.hitOpen[0], btnY, btnW, btnH, btnH * 0.5);
    cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, hOpen ? 0.22 : 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_set_source_rgba(cr, kAccR + 0.05, kAccG + 0.35, kAccB + 0.35, 0.98);
    cairo_text_extents_t te;
    cairo_text_extents(cr, "Open", &te);
    cairo_move_to(cr, st.hitOpen[0] + (btnW - te.x_advance) * 0.5,
                  btnY + btnH * 0.5 + te.height * 0.35);
    cairo_show_text(cr, "Open");
  }
}

bool open_with_pointer_motion(DesktopApp& app) {
   
  auto& st = app.openWith;
  if (!st.open) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, st.layerIdx, &lx, &ly)) return false;

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double us = dock_ui_scale(sc.dock);
  const double kPad = 16.0 * us;
  const double kPadIn = 12.0 * us;
  const double kEntryH = 40.0 * us;
  const double kTopBarH = 44.0 * us;
  const double kMaxListH = 320.0 * us;

  const int totalEntries = static_cast<int>(st.apps.size());
  const int maxVisible = std::max(1, static_cast<int>(kMaxListH / kEntryH));
  const int visibleEntries = std::min(totalEntries, maxVisible);
  const double listH = static_cast<double>(visibleEntries) * kEntryH;

  const double listX = st.x + kPadIn;
  const double listY = st.y + kPad + kTopBarH + kPadIn;
  const double listW = st.w - 2.0 * kPadIn;

  int newHover = -1;

  // Check close button
  if (in_rect(lx, ly,     st.hitClose.data())) {
    newHover = -2;
  }
  // Check Cancel button
  else if (in_rect(lx, ly, st.hitCancel.data())) {
    newHover = -3;
  }
  // Check Open button
  else if (in_rect(lx, ly, st.hitOpen.data())) {
    newHover = -4;
  }
  // Check "Set as default" checkbox
  else if (in_rect(lx, ly, st.hitDefault.data())) {
    newHover = -5;
  }
  // Check app list entries
  else if (lx >= listX && lx < listX + listW &&
           ly >= listY && ly < listY + listH) {
    int idx = st.scrollOffset + static_cast<int>((ly - listY) / kEntryH);
    if (idx >= 0 && idx < totalEntries) {
      newHover = idx;
    }
  }

  if (newHover == st.hoverIdx) return true;
  eh_desktop_log("open_with_motion: hover %d->%d layerIdx=%zu layers=%zu", st.hoverIdx, newHover, st.layerIdx, app.layers.size());
  st.hoverIdx = newHover;
  if (st.layerIdx < app.layers.size() && app.layers[st.layerIdx])
    paint_layer(app, *app.layers[st.layerIdx]);
  else
    eh_desktop_log("open_with_motion: paint SKIP bad layerIdx=%zu", st.layerIdx);
  if (app.display) wl_display_flush(app.display);
  return true;
}

bool open_with_handle_left_press(DesktopApp& app) {
   
  auto& st = app.openWith;
  if (!st.open) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, st.layerIdx, &lx, &ly)) {
    eh_desktop_log("open_with_left_press: no pointer_xy -> close");
    open_with_close(app);
    return true;
  }

  // Check if click is outside the card
  if (!(lx >= st.x && ly >= st.y && lx <= st.x + st.w && ly <= st.y + st.h)) {
    eh_desktop_log("open_with_left_press: click outside card (%.0f,%.0f) card(%.0f,%.0f %.0fx%.0f) -> close",
                   lx, ly, st.x, st.y, st.w, st.h);
    open_with_close(app);
    return true;
  }

  // Close button
  if (in_rect(lx, ly,     st.hitClose.data())) {
    eh_desktop_log("open_with_left_press: close button");
    open_with_close(app);
    return true;
  }

  // Cancel button
  if (in_rect(lx, ly, st.hitCancel.data())) {
    eh_desktop_log("open_with_left_press: cancel button");
    open_with_close(app);
    return true;
  }

  // Open button
  if (in_rect(lx, ly, st.hitOpen.data())) {
    eh_desktop_log("open_with_left_press: open button selectedIdx=%d", st.selectedIdx);
    if (st.selectedIdx >= 0 && st.selectedIdx < static_cast<int>(st.apps.size())) {
      const auto& entry = st.apps[st.selectedIdx];
      if (st.setDefault && !st.mimeType.empty()) {
        const std::string cmd = "xdg-mime default " + sh_quote(entry.desktop_id) + ".desktop " +
                                sh_quote(st.mimeType) + " 2>/dev/null";
        std::system(cmd.c_str());
      }
      const std::string uri = xdg::file_uri_for_path(st.filePath);
      const std::string launch = "gtk-launch " + sh_quote(entry.desktop_id) + " " +
                                 sh_quote(uri) + " &";
      std::system(launch.c_str());
    }
    open_with_close(app);
    return true;
  }

  // "Set as default" checkbox toggle
  if (in_rect(lx, ly, st.hitDefault.data())) {
    st.setDefault = !st.setDefault;
    eh_desktop_log("open_with_left_press: toggle default=%d", st.setDefault);
    if (st.layerIdx < app.layers.size() && app.layers[st.layerIdx])
      paint_layer(app, *app.layers[st.layerIdx]);
    else
      eh_desktop_log("open_with_left_press: paint SKIP bad layerIdx=%zu", st.layerIdx);
    if (app.display) wl_display_flush(app.display);
    return true;
  }

  // App list
  {
    const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
    const double us = dock_ui_scale(sc.dock);
    const double kPad = 16.0 * us;
    const double kPadIn = 12.0 * us;
    const double kEntryH = 40.0 * us;
    const double kTopBarH = 44.0 * us;
    const double kMaxListH = 320.0 * us;

    const int totalEntries = static_cast<int>(st.apps.size());
    const int maxVisible = std::max(1, static_cast<int>(kMaxListH / kEntryH));
    const int visibleEntries = std::min(totalEntries, maxVisible);
    const double listH = static_cast<double>(visibleEntries) * kEntryH;

    const double listX = st.x + kPadIn;
    const double listY = st.y + kPad + kTopBarH + kPadIn;
    const double listW = st.w - 2.0 * kPadIn;

    if (lx >= listX && lx < listX + listW &&
        ly >= listY && ly < listY + listH) {
      int idx = st.scrollOffset + static_cast<int>((ly - listY) / kEntryH);
      if (idx >= 0 && idx < totalEntries) {
        st.selectedIdx = idx;
        eh_desktop_log("open_with_left_press: launch app idx=%d id=%s", idx, st.apps[idx].desktop_id.c_str());
        // Launch immediately on single click
        const auto& entry = st.apps[idx];
        const std::string uri = xdg::file_uri_for_path(st.filePath);
        const std::string launch = "gtk-launch " + sh_quote(entry.desktop_id) + " " +
                                   sh_quote(uri) + " &";
        std::system(launch.c_str());
        open_with_close(app);
        return true;
      }
    }
  }

  return true;
}

bool open_with_handle_scroll(DesktopApp& app, double dx, double dy) {
   
  (void)dx;
  auto& st = app.openWith;
  if (!st.open) return false;

  if (st.layerIdx >= app.layers.size() || !app.layers[st.layerIdx]) {
    eh_desktop_log("open_with_scroll: BAD layerIdx=%zu layers=%zu", st.layerIdx, app.layers.size());
    return false;
  }

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double us = dock_ui_scale(sc.dock);
  const double kEntryH = 40.0 * us;

  const int totalEntries = static_cast<int>(st.apps.size());
  const int maxVisible = std::max(1, static_cast<int>(320.0 * us / kEntryH));
  const int visibleEntries = std::min(totalEntries, maxVisible);

  if (totalEntries <= visibleEntries) return true;

  // Convert scroll delta to entries (positive = scroll down = increase offset)
  int delta = static_cast<int>(dy / kEntryH);
  if (delta == 0) {
    delta = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
  }

  st.scrollOffset = std::clamp(st.scrollOffset + delta, 0, totalEntries - visibleEntries);
  eh_desktop_log("open_with_scroll: delta=%d offset=%d paint layer=%zu", delta, st.scrollOffset, st.layerIdx);
  paint_layer(app, *app.layers[st.layerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

} // namespace eh::shell::desktop
