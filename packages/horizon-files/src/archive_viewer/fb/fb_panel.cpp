#include "archive_viewer/fb/fb_panel.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <pwd.h>
#include <unistd.h>

#include <cairo/cairo.h>

namespace fs = std::filesystem;

namespace archive_viewer {

// ── Helpers ────────────────────────────────────────────────────────

static std::string home_dir() {
  if (const char* h = getenv("HOME")) return h;
  if (auto* pw = getpwuid(getuid())) return pw->pw_dir;
  return "/";
}

static bool is_hidden(const std::string& name) {
  return !name.empty() && name[0] == '.';
}

// ── Sidebar locations ─────────────────────────────────────────────

struct SideLoc {
  std::string label;
  std::string path;
};

static std::vector<SideLoc> detect_side_locations() {
  std::vector<SideLoc> locs;
  std::string home = home_dir();
  locs.push_back({"Home", home});
  auto try_add = [&](const std::string& name, const std::string& sub) {
    std::string p = home + "/" + sub;
    if (fs::is_directory(p)) locs.push_back({name, p});
  };
  try_add("Desktop", "Desktop");
  try_add("Documents", "Documents");
  try_add("Downloads", "Downloads");
  try_add("Music", "Music");
  try_add("Pictures", "Pictures");
  try_add("Videos", "Videos");
  locs.push_back({"Root", "/"});
  return locs;
}

// ── Draw helpers ───────────────────────────────────────────────────

static void set_color(cairo_t* cr, double r, double g, double b, double a) {
  cairo_set_source_rgba(cr, r, g, b, a);
}

static void draw_rounded_rect(cairo_t* cr, double x, double y, double w,
                               double h, double r) {
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  cairo_move_to(cr, x + r, y);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

// ── File-type detection ────────────────────────────────────────────

static FileType detect_file_type(const std::string& name, bool is_dir) {
  if (is_dir) return FileType::Folder;
  auto dot = name.rfind('.');
  if (dot == std::string::npos || dot == name.size() - 1) return FileType::File;
  std::string ext = name.substr(dot + 1);
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
      ext == "bmp" || ext == "webp" || ext == "svg" || ext == "avif" ||
      ext == "tif" || ext == "tiff" || ext == "ico" || ext == "heic" ||
      ext == "heif" || ext == "psd" || ext == "xcf" || ext == "ai" ||
      ext == "eps" || ext == "raw" || ext == "cr2" || ext == "nef" ||
      ext == "arw" || ext == "dng" || ext == "orf" || ext == "raf" ||
      ext == "pbm" || ext == "pgm" || ext == "ppm" || ext == "xbm" ||
      ext == "xpm")
    return FileType::Image;

  if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg" ||
      ext == "m4a" || ext == "aac" || ext == "opus" || ext == "wma" ||
      ext == "aiff" || ext == "aif" || ext == "alac" || ext == "ac3" ||
      ext == "dts" || ext == "mid" || ext == "midi" || ext == "ape" ||
      ext == "wv" || ext == "tta" || ext == "ra")
    return FileType::Audio;

  if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
      ext == "webm" || ext == "m4v" || ext == "wmv" || ext == "flv" ||
      ext == "f4v" || ext == "3gp" || ext == "3g2" || ext == "ogv" ||
      ext == "mpg" || ext == "mpeg" || ext == "mpe" || ext == "ts" ||
      ext == "mts" || ext == "m2ts" || ext == "vob")
    return FileType::Video;

  if (ext == "html" || ext == "htm" || ext == "xhtml" || ext == "css" ||
      ext == "scss" || ext == "sass" || ext == "less" || ext == "php" ||
      ext == "asp" || ext == "aspx" || ext == "jsp" || ext == "wasm")
    return FileType::Web;

  if (ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mdwn" ||
      ext == "mkd" || ext == "mkdn")
    return FileType::Markdown;

  if (ext == "c" || ext == "cpp" || ext == "cxx" || ext == "cc" ||
      ext == "h" || ext == "hpp" || ext == "hxx" || ext == "hh" ||
      ext == "py" || ext == "pyw" || ext == "rs" || ext == "go" ||
      ext == "java" || ext == "js" || ext == "ts" || ext == "jsx" ||
      ext == "tsx" || ext == "rb" || ext == "php" || ext == "pl" ||
      ext == "pm" || ext == "lua" || ext == "swift" || ext == "kt" ||
      ext == "kts" || ext == "scala" || ext == "clj" || ext == "cljs" ||
      ext == "elm" || ext == "hs" || ext == "dart" || ext == "r" ||
      ext == "m" || ext == "mm" || ext == "cs" || ext == "fs" ||
      ext == "vb" || ext == "sql" || ext == "svelte" || ext == "vue" ||
      ext == "coffee" || ext == "groovy" || ext == "jl" || ext == "nim" ||
      ext == "cob" || ext == "cbl" || ext == "asm" || ext == "s" ||
      ext == "cr" || ext == "zig" || ext == "ex" || ext == "exs" ||
      ext == "erl" || ext == "hrl" || ext == "ml" || ext == "mli" ||
      ext == "re" || ext == "rei" || ext == "tcl" || ext == "d" ||
      ext == "makefile" || ext == "cmake" || ext == "cmakelists")
    return FileType::Code;

  if (ext == "txt" || ext == "conf" || ext == "cfg" ||
      ext == "ini" || ext == "json" || ext == "xml" || ext == "yaml" ||
      ext == "yml" || ext == "log" || ext == "csv" || ext == "tsv" ||
      ext == "toml" || ext == "nfo" || ext == "info" || ext == "tex" ||
      ext == "sty" || ext == "bst")
    return FileType::Text;

  if (ext == "pdf" || ext == "doc" || ext == "docx" || ext == "xls" ||
      ext == "xlsx" || ext == "ppt" || ext == "pptx" || ext == "odt" ||
      ext == "ods" || ext == "odp" || ext == "odg" || ext == "odf" ||
      ext == "rtf" || ext == "djvu" || ext == "epub" || ext == "mobi" ||
      ext == "azw" || ext == "azw3" || ext == "cbr" || ext == "cbz" ||
      ext == "pages" || ext == "numbers" || ext == "keynote" ||
      ext == "pub" || ext == "indd")
    return FileType::Document;

  if (ext == "ttf" || ext == "otf" || ext == "woff" || ext == "woff2" ||
      ext == "eot" || ext == "pfa" || ext == "pfb" || ext == "ttc" ||
      ext == "dfont" || ext == "sfd")
    return FileType::Font;

  if (ext == "zip" || ext == "tar" || ext == "gz" || ext == "bz2" ||
      ext == "xz" || ext == "7z" || ext == "rar" || ext == "zst" ||
      ext == "zstd" || ext == "lz" || ext == "lz4" || ext == "lzma" ||
      ext == "lzo" || ext == "ar" || ext == "cpio" || ext == "iso" ||
      ext == "cab" || ext == "dmg" || ext == "tgz" || ext == "tbz2" ||
      ext == "txz" || ext == "zoo" || ext == "hqx" || ext == "sit")
    return FileType::Archive;

  if (ext == "sh" || ext == "bin" || ext == "elf" || ext == "exe" ||
      ext == "msi" || ext == "out" || ext == "app" || ext == "run" ||
      ext == "com" || ext == "bat" || ext == "cmd" || ext == "ps1" ||
      ext == "appimage" || ext == "desktop" || ext == "deb" ||
      ext == "rpm" || ext == "appdir" || ext == "flatpak" || ext == "snap")
    return FileType::Executable;

  return FileType::File;
}

// ── Icon drawing (system theme → colored rect + letter) ────────────

static const char* icon_name_for_file_type(FileType ft) {
  switch (ft) {
    case FileType::Folder:     return "folder";
    case FileType::Image:      return "image-x-generic";
    case FileType::Audio:      return "audio-x-generic";
    case FileType::Video:      return "video-x-generic";
    case FileType::Text:       return "text-x-generic";
    case FileType::Markdown:   return "text-x-markdown";
    case FileType::Code:       return "text-x-code";
    case FileType::Document:   return "x-office-document";
    case FileType::Font:       return "font-x-generic";
    case FileType::Archive:    return "application-x-archive";
    case FileType::Executable: return "application-x-executable";
    case FileType::Web:        return "text-html";
    default:                   return "text-x-generic";
  }
}

static void draw_file_icon(IconLoader& icons, cairo_t* cr, double x, double y,
                            double sz, FileType ft) {
  // Try system icon theme first
  const char* icon_name = icon_name_for_file_type(ft);
  const auto* entry = icons.load(icon_name);
  if (entry && entry->surface) {
    double iw = static_cast<double>(entry->width);
    double ih = static_cast<double>(entry->height);
    if (iw > 0 && ih > 0) {
      double scale = sz / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      cairo_translate(cr, x, y);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, entry->surface,
                               (sz / scale - iw) / 2,
                               (sz / scale - ih) / 2);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
  }

  // Fallback: colored rounded rect + letter
  double r = 0, g = 0, b = 0;
  char letter = '?';
  switch (ft) {
    case FileType::Folder:     r=0.85; g=0.65; b=0.20; letter='F'; break;
    case FileType::Image:      r=0.20; g=0.75; b=0.40; letter='I'; break;
    case FileType::Audio:      r=0.30; g=0.55; b=0.90; letter='A'; break;
    case FileType::Video:      r=0.70; g=0.35; b=0.80; letter='V'; break;
    case FileType::Text:       r=0.50; g=0.50; b=0.55; letter='T'; break;
    case FileType::Markdown:   r=0.25; g=0.50; b=0.70; letter='M'; break;
    case FileType::Code:       r=0.30; g=0.60; b=0.55; letter='C'; break;
    case FileType::Document:   r=0.20; g=0.45; b=0.75; letter='D'; break;
    case FileType::Font:       r=0.55; g=0.35; b=0.70; letter='f'; break;
    case FileType::Archive:    r=0.70; g=0.50; b=0.20; letter='Z'; break;
    case FileType::Executable: r=0.60; g=0.40; b=0.25; letter='X'; break;
    case FileType::Web:        r=0.30; g=0.55; b=0.85; letter='W'; break;
    default:                   r=0.40; g=0.40; b=0.45; letter='?'; break;
  }

  double t = sz * 0.08, rr = sz * 0.12;

  cairo_set_source_rgb(cr, r, g, b);
  draw_rounded_rect(cr, x + t, y + t, sz - t * 2, sz - t * 2, rr);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, sz * 0.55);
  cairo_text_extents_t te;
  char letter_str[2] = {letter, 0};
  cairo_text_extents(cr, letter_str, &te);
  cairo_move_to(cr, x + (sz - te.x_advance) / 2, y + (sz + te.height) / 2 - te.y_bearing * 0.3);
  cairo_show_text(cr, letter_str);
}

// ── Scrolling ──────────────────────────────────────────────────────

static int calc_content_h(const std::vector<int>& visible, int row_h) {
  return static_cast<int>(visible.size()) * row_h;
}

static void clamp_scroll(FbPanel& p) {
  int max_s = std::max(0, p.content_h - (p.h - p.top_bar_h - p.status_h -
                        (p.pick_mode ? p.pick_bar_h : 0)));
  if (p.scroll_px > max_s) p.scroll_px = max_s;
  if (p.scroll_px < 0) p.scroll_px = 0;
}

// ── Implementation ─────────────────────────────────────────────────

void FbPanel::reset() {
  auto saved_bg = bg_opacity;
  auto saved_panel = panel_opacity;
  *this = FbPanel{};
  bg_opacity = saved_bg;
  panel_opacity = saved_panel;
  current_path = home_dir();
  pick_mode = false;
  reload();
}

void FbPanel::init() {
  current_path = home_dir();
  pick_mode = false;
  reload();
}

void FbPanel::navigate_to(const std::string& path) {
  if (!fs::is_directory(path)) return;
  current_path = path;
  scroll_px = 0;
  hover_idx = -1;
  selected_idx = -1;
  reload();
}

void FbPanel::navigate_up() {
  fs::path p(current_path);
  auto parent = p.parent_path();
  if (parent != p && fs::is_directory(parent)) {
    navigate_to(parent.string());
  }
}

void FbPanel::reload() {
  entries.clear();
  visible.clear();
  std::vector<FbEntry> dirs, files;
  auto iter = fs::directory_iterator(current_path,
      fs::directory_options::skip_permission_denied);
  for (const auto& de : iter) {
    FbEntry e;
    e.name = de.path().filename().string();
    e.path = de.path().string();
    e.is_dir = de.is_directory();
    e.is_hidden = is_hidden(e.name);
    e.type = detect_file_type(e.name, e.is_dir);
    if (e.is_dir) {
      // skip
    } else     if (de.is_regular_file()) {
      e.size = static_cast<uint64_t>(de.file_size());
    }
    e.mtime = 0;
    if (e.is_dir) dirs.push_back(std::move(e));
    else files.push_back(std::move(e));
  }
  auto sort_by_name = [](auto& v) {
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
      auto ca = a.name, cb = b.name;
      for (auto& c : ca) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (auto& c : cb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return ca < cb;
    });
  };
  sort_by_name(dirs);
  sort_by_name(files);
  int idx = 0;
  for (auto& e : dirs) { entries.push_back(std::move(e)); visible.push_back(idx++); }
  for (auto& e : files) { entries.push_back(std::move(e)); visible.push_back(idx++); }
  content_h = calc_content_h(visible, row_h);
  clamp_scroll(*this);
}

void FbPanel::paint(cairo_t* cr) {
  if (!active || w <= 0 || h <= 0) return;

  // Save and translate to panel position
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_clip(cr);

  // Background
  set_color(cr, 0.14, 0.14, 0.16, bg_opacity);
  cairo_paint(cr);

  // ── Layout ──
  int top_h = top_bar_h;
  int bar_h = pick_mode ? pick_bar_h : 0;
  int list_y = top_h;
  int list_h = h - top_h - status_h - bar_h;

  auto locs = detect_side_locations();
  int sidebar_w = 160;

  int content_x = sidebar_w;
  int content_w = w - sidebar_w;

  // ── Sidebar ──
  if (sidebar_w > 0) {
    set_color(cr, 0.12, 0.12, 0.14, panel_opacity);
    cairo_rectangle(cr, 0, 0, sidebar_w, h - status_h - bar_h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgb(cr, 0.55, 0.55, 0.58);
    cairo_move_to(cr, 8, 16);
    cairo_show_text(cr, "PLACES");
    cairo_fill(cr);

    int sy = 26;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    for (int i = 0; i < static_cast<int>(locs.size()); ++i) {
      bool hovered = (i == side_hover_idx);
      if (hovered) {
        cairo_set_source_rgb(cr, 0.22, 0.22, 0.24);
        cairo_rectangle(cr, 0, sy, sidebar_w, 28);
        cairo_fill(cr);
      }
      bool active_side = (locs[i].path == current_path);
      if (active_side) {
        cairo_set_source_rgb(cr, 0.20, 0.40, 0.70);
        cairo_rectangle(cr, 0, sy, 3, 28);
        cairo_fill(cr);
      }
      cairo_set_source_rgb(cr, active_side ? 0.90 : (hovered ? 0.90 : 0.72),
                           active_side ? 0.90 : (hovered ? 0.90 : 0.72),
                           active_side ? 0.95 : (hovered ? 0.92 : 0.75));
      cairo_move_to(cr, 12, sy + 19);
      cairo_show_text(cr, locs[i].label.c_str());
      cairo_fill(cr);
      sy += 32;
    }

    // Separator
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.27, 0.3);
    cairo_rectangle(cr, sidebar_w, 0, 1, h - status_h - bar_h);
    cairo_fill(cr);
  }

  // ── Top bar ──
  // Background
  set_color(cr, 0.16, 0.16, 0.18, panel_opacity);
  cairo_rectangle(cr, content_x, 0, content_w, top_h);
  cairo_fill(cr);

  // Up button
  {
    double bx = content_x + 6, by = 6, bw = 28, bh = top_h - 12;
    if (back_hover) {
      cairo_set_source_rgb(cr, 0.30, 0.30, 0.32);
      draw_rounded_rect(cr, bx, by, bw, bh, 4);
      cairo_fill(cr);
    }
    // Up arrow (triangle)
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.88);
    cairo_move_to(cr, bx + bw / 2, by + 8);
    cairo_line_to(cr, bx + bw / 2 - 6, by + bh - 8);
    cairo_line_to(cr, bx + bw / 2 + 6, by + bh - 8);
    cairo_close_path(cr);
    cairo_fill(cr);
  }

  // Path display
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_set_source_rgb(cr, 0.80, 0.80, 0.84);
  {
    // Truncate path if too wide
    std::string disp = current_path;
    cairo_text_extents_t te;
    cairo_text_extents(cr, disp.c_str(), &te);
    if (te.x_advance > content_w - 60) {
      // Show last part
      auto pos = disp.rfind('/');
      if (pos != std::string::npos && pos > 0) {
        pos = disp.rfind('/', pos - 1);
        if (pos != std::string::npos) disp = "..." + disp.substr(pos);
      }
    }
    cairo_move_to(cr, content_x + 42, top_h / 2 + 5);
    cairo_show_text(cr, disp.c_str());
    cairo_fill(cr);
  }

  // Separator under top bar
  cairo_set_source_rgba(cr, 0.25, 0.25, 0.27, 0.3);
  cairo_rectangle(cr, content_x, top_h - 1, content_w, 1);
  cairo_fill(cr);

  // ── File list ──
  cairo_save(cr);
  cairo_rectangle(cr, content_x, list_y, content_w, list_h);
  cairo_clip(cr);

  int yp = list_y - scroll_px;
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);

  for (int vi = 0; vi < static_cast<int>(visible.size()); ++vi) {
    if (yp + row_h < list_y || yp > list_y + list_h) {
      yp += row_h;
      continue;
    }
    int ei = visible[vi];
    auto& e = entries[ei];
    bool hovered = (vi == hover_idx);
    bool selected = (vi == selected_idx);

    // Row background
    if (selected) {
      cairo_set_source_rgb(cr, 0.20, 0.40, 0.70);
      cairo_rectangle(cr, content_x, yp, content_w, row_h);
      cairo_fill(cr);
    } else if (hovered) {
      cairo_set_source_rgb(cr, 0.22, 0.22, 0.24);
      cairo_rectangle(cr, content_x, yp, content_w, row_h);
      cairo_fill(cr);
    }

    // Icon
    draw_file_icon(icons, cr, content_x + 4, yp + 4, row_h - 8, e.type);

    // Name
    cairo_set_source_rgb(cr, selected ? 1.0 : 0.85,
                         selected ? 1.0 : 0.85,
                         selected ? 1.0 : 0.88);
    cairo_move_to(cr, content_x + row_h + 4, yp + row_h / 2 + 5);
    cairo_show_text(cr, e.name.c_str());
    cairo_fill(cr);

    // Size (for files)
    if (!e.is_dir) {
      char sz[32];
      if (e.size > 1073741824)
        std::snprintf(sz, sizeof(sz), "%.1f GiB", e.size / 1073741824.0);
      else if (e.size > 1048576)
        std::snprintf(sz, sizeof(sz), "%.1f MiB", e.size / 1048576.0);
      else if (e.size > 1024)
        std::snprintf(sz, sizeof(sz), "%.1f KiB", e.size / 1024.0);
      else
        std::snprintf(sz, sizeof(sz), "%" PRIu64 " B", e.size);
      cairo_set_source_rgb(cr, selected ? 0.85 : 0.50,
                           selected ? 0.85 : 0.50,
                           selected ? 0.90 : 0.55);
      cairo_text_extents_t te;
      cairo_text_extents(cr, sz, &te);
      cairo_move_to(cr, content_x + content_w - 10 - te.x_advance, yp + row_h / 2 + 5);
      cairo_show_text(cr, sz);
      cairo_fill(cr);
    }

    yp += row_h;
  }

  cairo_restore(cr);

  // ── Scrollbar ──
  if (content_h > list_h) {
    int sb_x = x + w - 8;
    int sb_h = list_h;
    double thumb_h = std::max(20.0, static_cast<double>(sb_h) * sb_h / content_h);
    double thumb_y = list_y + (static_cast<double>(scroll_px) / (content_h - list_h)) * (sb_h - thumb_h);
    cairo_set_source_rgba(cr, 0.35, 0.35, 0.38, 0.5);
    draw_rounded_rect(cr, sb_x, thumb_y, 6, thumb_h, 3);
    cairo_fill(cr);
  }

  // ── Status bar ──
  {
    int sby = h - status_h - bar_h;
    set_color(cr, 0.16, 0.16, 0.18, panel_opacity);
    cairo_rectangle(cr, 0, sby, w, status_h + bar_h);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    cairo_set_source_rgb(cr, 0.60, 0.60, 0.65);

    char buf[64];
    int ndirs = 0, nfiles = 0;
    for (auto& e : entries) {
      if (e.is_dir) ndirs++;
      else nfiles++;
    }
    std::snprintf(buf, sizeof(buf), "%d items (%d dirs, %d files)", ndirs + nfiles, ndirs, nfiles);
    cairo_move_to(cr, 8, sby + status_h / 2 + 4);
    cairo_show_text(cr, buf);
    cairo_fill(cr);
  }

  // ── Picker bar ──
  if (pick_mode) {
    int pby = h - pick_bar_h;
    set_color(cr, 0.12, 0.12, 0.14, panel_opacity);
    cairo_rectangle(cr, 0, pby, w, pick_bar_h);
    cairo_fill(cr);

    // Separator
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.27, 0.3);
    cairo_rectangle(cr, 0, pby, w, 1);
    cairo_fill(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);

    // Select button (right side)
    double sbw = 80, sbh = 28, sbx = w - sbw - 8, sby2 = pby + (pick_bar_h - sbh) / 2;
    bool can_select = pick_file ? selected_idx >= 0 : true;
    if (can_select) {
      if (select_hover) {
        cairo_set_source_rgb(cr, 0.25, 0.50, 0.85);
      } else {
        cairo_set_source_rgb(cr, 0.20, 0.40, 0.75);
      }
    } else {
      cairo_set_source_rgb(cr, 0.25, 0.25, 0.27);
    }
    draw_rounded_rect(cr, sbx, sby2, sbw, sbh, 5);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, can_select ? 1.0 : 0.50, can_select ? 1.0 : 0.50, can_select ? 1.0 : 0.55);
    cairo_move_to(cr, sbx + sbw / 2 - 16, sby2 + sbh / 2 + 5);
    cairo_show_text(cr, "Select");
    cairo_fill(cr);

    // Cancel button (left of Select)
    double cbw = 70, cbx = sbx - cbw - 6;
    if (cancel_hover) {
      cairo_set_source_rgb(cr, 0.35, 0.35, 0.37);
    } else {
      cairo_set_source_rgb(cr, 0.28, 0.28, 0.30);
    }
    draw_rounded_rect(cr, cbx, sby2, cbw, sbh, 5);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.85, 0.85, 0.88);
    cairo_move_to(cr, cbx + cbw / 2 - 16, sby2 + sbh / 2 + 5);
    cairo_show_text(cr, "Cancel");
    cairo_fill(cr);

    // Show currently selected path (for file picker)
    if (pick_file && selected_idx >= 0) {
      int ei = visible[selected_idx];
      std::string sel_name = entries[ei].name;
      cairo_set_source_rgb(cr, 0.60, 0.60, 0.65);
      cairo_move_to(cr, 8, sby2 + sbh / 2 + 5);
      cairo_show_text(cr, sel_name.c_str());
      cairo_fill(cr);
    } else if (!pick_file) {
      cairo_set_source_rgb(cr, 0.60, 0.60, 0.65);
      std::string dir_disp = "Dir: " + current_path;
      cairo_move_to(cr, 8, sby2 + sbh / 2 + 5);
      cairo_show_text(cr, dir_disp.c_str());
      cairo_fill(cr);
    }
  }

  cairo_restore(cr);
}

// ── Event helpers ──────────────────────────────────────────────────

static int hit_test_list(FbPanel& p, int mx, int my) {
  int top_h = p.top_bar_h;
  int bar_h = p.pick_mode ? p.pick_bar_h : 0;
  int list_y = top_h;
  int list_h = p.h - top_h - p.status_h - bar_h;
  auto locs = detect_side_locations();
  int sidebar_w = 160;
  int content_x = sidebar_w;

  if (mx < content_x || mx >= p.w) return -1;
  if (my < list_y || my >= list_y + list_h) return -1;
  int idx = (my - list_y + p.scroll_px) / p.row_h;
  if (idx < 0 || idx >= static_cast<int>(p.visible.size())) return -1;
  return idx;
}

// ── Events ─────────────────────────────────────────────────────────

void FbPanel::on_motion(int mx, int my) {
  mouse_x = mx;
  mouse_y = my;

  int top_h = top_bar_h;
  int bar_h = pick_mode ? pick_bar_h : 0;

  // Back button
  back_hover = (mx >= 6 && mx < 34 && my >= 6 && my < top_h - 6);

  // Sidebar hover — always active with fixed 160px width
  auto locs = detect_side_locations();
  side_hover_idx = -1;
  if (mx >= 0 && mx < 160) {
    int sy = 26;
    for (int i = 0; i < static_cast<int>(locs.size()); ++i) {
      if (my >= sy && my < sy + 28) {
        side_hover_idx = i;
        break;
      }
      sy += 32;
    }
  }

  // File list hover
  hover_idx = hit_test_list(*this, mx, my);

  // Picker buttons
  select_hover = false;
  cancel_hover = false;
  if (pick_mode) {
    int pby = h - pick_bar_h;
    double sbw = 80, sbh = 28;
    double sbx = w - sbw - 8, sby2 = pby + (pick_bar_h - sbh) / 2;
    if (mx >= sbx && mx < sbx + sbw && my >= sby2 && my < sby2 + sbh) {
      select_hover = true;
    }
    double cbw = 70;
    double cbx = sbx - cbw - 6;
    if (mx >= cbx && mx < cbx + cbw && my >= sby2 && my < sby2 + sbh) {
      cancel_hover = true;
    }
  }
}

void FbPanel::on_click(int mx, int my, int button) {
  if (button != 0x110) return; // left button only

  int top_h = top_bar_h;

  // Back button
  if (mx >= 6 && mx < 34 && my >= 6 && my < top_h - 6) {
    navigate_up();
    return;
  }

  // Sidebar click
  {
    auto locs = detect_side_locations();
    if (mx >= 0 && mx < 160) {
      int sy = 26;
      for (int i = 0; i < static_cast<int>(locs.size()); ++i) {
        if (my >= sy && my < sy + 28) {
          if (locs[i].path != current_path)
            navigate_to(locs[i].path);
          return;
        }
        sy += 32;
      }
    }
  }

  // File list click
  int idx = hit_test_list(*this, mx, my);
  if (idx >= 0) {
    int ei = visible[idx];
    if (entries[ei].is_dir) {
      navigate_to(entries[ei].path);
    } else {
      selected_idx = idx;
    }
    return;
  }

  // Picker buttons
  if (pick_mode) {
    int pby = h - pick_bar_h;
    double sbw = 80, sbh = 28;
    double sbx = w - sbw - 8, sby2 = pby + (pick_bar_h - sbh) / 2;
    // Select
    if (mx >= sbx && mx < sbx + sbw && my >= sby2 && my < sby2 + sbh) {
      if (pick_file && selected_idx >= 0) {
        result = entries[visible[selected_idx]].path;
      } else if (!pick_file) {
        result = current_path;
      }
      active = false;
      return;
    }
    // Cancel
    double cbw = 70;
    double cbx = sbx - cbw - 6;
    if (mx >= cbx && mx < cbx + cbw && my >= sby2 && my < sby2 + sbh) {
      result.clear();
      active = false;
      return;
    }
  }
}

void FbPanel::on_scroll(int mx, int my, double dx, double dy) {
  (void)mx;
  (void)my;
  (void)dx;
  int list_h = h - top_bar_h - status_h - (pick_mode ? pick_bar_h : 0);
  if (content_h <= list_h) return;
  int step = row_h * 3;
  scroll_px += static_cast<int>(-dy * step);
  clamp_scroll(*this);
}

void FbPanel::on_key(uint32_t sym) {
  if (visible.empty()) return;
  // Up/Down arrow
  if (sym == 0xff52 /* Up */ || sym == 0xffff /* also up? */) {
    // Actually let's use standard keycodes
    (void)sym;
  }
  // Simple implementation: use XKB key names
  // Up = XKB_KEY_Up = 0xff52, Down = XKB_KEY_Down = 0xff54
  // Enter = XKB_KEY_Return = 0xff0d
  if (sym == 0xff52) { // Up
    if (selected_idx < 0) selected_idx = static_cast<int>(visible.size()) - 1;
    else if (selected_idx > 0) selected_idx--;
    // Scroll to keep selected visible
    int list_h = h - top_bar_h - status_h - (pick_mode ? pick_bar_h : 0);
    int y = selected_idx * row_h;
    if (y < scroll_px) scroll_px = y;
    if (y + row_h > scroll_px + list_h) scroll_px = y + row_h - list_h;
    clamp_scroll(*this);
  } else if (sym == 0xff54) { // Down
    if (selected_idx < 0) selected_idx = 0;
    else if (selected_idx < static_cast<int>(visible.size()) - 1) selected_idx++;
    int list_h = h - top_bar_h - status_h - (pick_mode ? pick_bar_h : 0);
    int y = selected_idx * row_h;
    if (y < scroll_px) scroll_px = y;
    if (y + row_h > scroll_px + list_h) scroll_px = y + row_h - list_h;
    clamp_scroll(*this);
  } else if (sym == 0xff0d) { // Enter
    if (selected_idx >= 0) {
      int ei = visible[selected_idx];
      if (entries[ei].is_dir) {
        navigate_to(entries[ei].path);
      } else if (pick_file) {
        result = entries[ei].path;
        active = false;
      }
    } else if (!pick_file) {
      result = current_path;
      active = false;
    }
  } else if (sym == 0xff1b) { // Escape
    result.clear();
    active = false;
  }
}

} // namespace archive_viewer
