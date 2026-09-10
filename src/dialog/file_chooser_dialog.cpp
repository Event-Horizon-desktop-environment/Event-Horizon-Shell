#include "dialog/file_chooser_dialog.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/ui/theme.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace eh::dialog {

// Small helpers for paths and URIs.

static std::string home_dir() {
   
  if (auto* h = std::getenv("HOME")) return h;
  if (auto* pw = getpwuid(getuid())) return pw->pw_dir;
  return "/";
}

static bool is_dir(const std::string& path) {
   
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string uri_from_path(const std::string& path) {
   
  return "file://" + path;
}

// Icon theme lookups: a shared cache loads the configured theme once and
// prewarms the usual search dirs.

static eh::icons::IconCache s_icon_cache;

static void ensure_icon_cache_initialized() {
  static bool done = false;
  if (done) return;
  done = true;
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  if (!sc.dock.iconTheme.empty()) {
    s_icon_cache.set_icon_theme(sc.dock.iconTheme);
  }
  s_icon_cache.prewarm_search_dirs();
}

enum class FileIcon { Folder, Image, Audio, Video, Text, Executable, File };

static FileIcon detect_file_icon(const std::string& name, bool is_dir) {
   
  if (is_dir) return FileIcon::Folder;
  auto dot = name.rfind('.');
  if (dot == std::string::npos || dot == name.size() - 1) return FileIcon::File;
  std::string ext = name.substr(dot + 1);
  for (auto& c : ext) c = std::tolower(c);
  if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
      ext == "bmp" || ext == "webp" || ext == "svg" || ext == "avif")
    return FileIcon::Image;
  if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg" ||
      ext == "m4a" || ext == "aac" || ext == "opus")
    return FileIcon::Audio;
  if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
      ext == "webm" || ext == "m4v")
    return FileIcon::Video;
  if (ext == "txt" || ext == "md" || ext == "conf" || ext == "cfg" ||
      ext == "ini" || ext == "json" || ext == "xml" || ext == "yaml")
    return FileIcon::Text;
  if (ext == "sh" || ext == "bin" || ext == "elf" || ext == "AppImage" ||
      ext == "desktop")
    return FileIcon::Executable;
  return FileIcon::File;
}

static const char* icon_name_for_type(FileIcon fi) {
   
  switch (fi) {
    case FileIcon::Folder:     return "folder";
    case FileIcon::Image:      return "image-x-generic";
    case FileIcon::Audio:      return "audio-x-generic";
    case FileIcon::Video:      return "video-x-generic";
    case FileIcon::Text:       return "text-x-generic";
    case FileIcon::Executable: return "application-x-executable";
    case FileIcon::File:       return "text-x-generic";
  }
  return "unknown";
}

cairo_surface_t* FileChooserDialog::get_thumbnail(const std::string& path, int size) {
   
  auto it = thumb_cache_.find(path);
  if (it != thumb_cache_.end()) return it->second;
  cairo_surface_t* s = eh::wallpaper::load_thumbnail(path, size);
  if (s) thumb_cache_[path] = s;
  return s;
}

static void draw_file_icon(cairo_t* cr, int x, int y, int size, FileIcon fi,
                           bool selected, cairo_surface_t* thumb = nullptr) {
   
  // Image files get a thumbnail instead of a plain icon.
  if (fi == FileIcon::Image && thumb) {
    double iw = static_cast<double>(cairo_image_surface_get_width(thumb));
    double ih = static_cast<double>(cairo_image_surface_get_height(thumb));
    if (iw > 0 && ih > 0) {
      double scale = size / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      // Clip the thumbnail to a rounded rect.
      double rad = 6;
      cairo_new_path(cr);
      cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3*M_PI/2);
      cairo_arc(cr, x + size - rad, y + rad, rad, 3*M_PI/2, 2*M_PI);
      cairo_arc(cr, x + size - rad, y + size - rad, rad, 0, M_PI/2);
      cairo_arc(cr, x + rad, y + size - rad, rad, M_PI/2, M_PI);
      cairo_close_path(cr);
      cairo_clip(cr);
      cairo_translate(cr, x, y);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, thumb,
                               (size/scale - iw) / 2, (size/scale - ih) / 2);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
  }

  const auto* entry = s_icon_cache.tray_icon(icon_name_for_type(fi));
  if (entry && entry->surface) {
    double iw = static_cast<double>(entry->width);
    double ih = static_cast<double>(entry->height);
    double scale = size / std::max(1.0, std::max(iw, ih));
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, entry->surface,
                             (size/scale - iw) / 2, (size/scale - ih) / 2);
    cairo_paint(cr);
    cairo_restore(cr);
    return;
  }

  // No theme icon either, so fall back to a colored shape with a letter.
  double r, g, b;
  switch (fi) {
    case FileIcon::Folder:     r = 0.85; g = 0.65; b = 0.20; break;
    case FileIcon::Image:      r = 0.20; g = 0.75; b = 0.40; break;
    case FileIcon::Audio:      r = 0.30; g = 0.55; b = 0.90; break;
    case FileIcon::Video:      r = 0.70; g = 0.35; b = 0.80; break;
    case FileIcon::Text:       r = 0.50; g = 0.50; b = 0.55; break;
    case FileIcon::Executable: r = 0.60; g = 0.40; b = 0.25; break;
    default:                   r = 0.40; g = 0.40; b = 0.45; break;
  }
  if (selected) { r = r * 1.3; g = g * 1.3; b = b * 1.3; }

  cairo_save(cr);
  double rad = size * 0.2;
  cairo_new_path(cr);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + size - rad, y + rad, rad, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + size - rad, y + size - rad, rad, 0, M_PI / 2);
  cairo_arc(cr, x + rad, y + size - rad, rad, M_PI / 2, M_PI);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, r, g, b, 0.85);
  cairo_fill(cr);

  const char* label = "?";
  switch (fi) {
    case FileIcon::Folder:     label = "F"; break;
    case FileIcon::Image:      label = "I"; break;
    case FileIcon::Audio:      label = "A"; break;
    case FileIcon::Video:      label = "V"; break;
    case FileIcon::Text:       label = "T"; break;
    case FileIcon::Executable: label = "X"; break;
    default:                   label = "?"; break;
  }
  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, size * 0.45);
  cairo_text_extents_t te;
  cairo_text_extents(cr, label, &te);
  cairo_move_to(cr, x + (size - te.width) / 2 - te.x_bearing,
                y + (size + te.height) / 2 - te.y_bearing);
  cairo_show_text(cr, label);
  cairo_restore(cr);
}

// Constructor.

FileChooserDialog::FileChooserDialog(Mode mode, const char* title,
                                     const std::string& accept_type,
                                     bool multiple)
  : DialogBase(720, 520, title && title[0] ? title :
                 (mode == Mode::Open ? "Open File" :
                  mode == Mode::Directory ? "Select Folder" : "Save File")),
    mode_(mode), accept_type_(accept_type), multiple_(multiple) {
  ensure_icon_cache_initialized();
  load_dir(home_dir());

  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    auto now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "screenshot-%Y%m%d-%H%M%S.png", std::localtime(&now));
    filename_text_ = buf;
    filename_cursor_ = static_cast<int>(filename_text_.size());
  }
}

FileChooserDialog::~FileChooserDialog() {
   
  MANGOWM_INFO("{}", __func__);
  for (auto& [path, surf] : thumb_cache_) {
    (void)path;
    if (surf) cairo_surface_destroy(surf);
  }
  thumb_cache_.clear();
}

// Re-read a directory into the entry list, sorted, folders first.

void FileChooserDialog::load_dir(const std::string& path) {
   
  current_path_ = path;
  entries_.clear();
  scroll_offset_ = 0;
  hover_idx_ = -1;
  selected_idx_ = -1;
  multi_selected_.clear();

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    std::cerr << "[file-chooser] opendir(" << path << "): " << strerror(errno) << '\n';
    return;
  }

  std::vector<FileEntry> dirs, files;
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    std::string name(ent->d_name);
    if (name == ".") continue;

    bool d = (ent->d_type == DT_DIR) ||
             (ent->d_type == DT_UNKNOWN && is_dir(path + "/" + name));

    if (d && name == "..") continue; // Up navigation is handled separately.

    if (d) {
      dirs.push_back({name, true});
    } else {
      files.push_back({name, false});
    }
  }
  closedir(dir);

  std::sort(dirs.begin(), dirs.end(),
            [](auto& a, auto& b) { return a.name < b.name; });
  std::sort(files.begin(), files.end(),
            [](auto& a, auto& b) { return a.name < b.name; });

  // In Directory mode image files are listed too, as selectable previews.
  std::vector<FileEntry> image_files;
  if (mode_ == Mode::Directory) {
    for (auto& f : files)
      if (detect_file_icon(f.name, false) == FileIcon::Image)
        image_files.push_back(std::move(f));
  }
  entries_ = std::move(dirs);
  if (mode_ == Mode::Directory) {
    entries_.insert(entries_.end(), image_files.begin(), image_files.end());
  } else {
    entries_.insert(entries_.end(), files.begin(), files.end());
  }
  rebuild_visible();
  request_redraw();
}

// go_up and open_selected.

void FileChooserDialog::go_up() {
   
  fs::path p(current_path_);
  auto parent = p.parent_path();
  if (parent != p) {
    load_dir(parent.string());
  }
}

void FileChooserDialog::open_selected() {
    
  if (mode_ == Mode::Directory) {
    selected_uris_.clear();
    selected_uris_.push_back(uri_from_path(current_path_));
    Result res;
    res.response = 0;
    res.uris = selected_uris_;
    finish(std::move(res));
    return;
  }

  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    std::string name = filename_text_;
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    if (name.empty() || name.find('/') != std::string::npos) return;
    std::string full = current_path_ + "/" + name;
    selected_uris_.clear();
    selected_uris_.push_back(uri_from_path(full));
    Result res;
    res.response = 0;
    res.uris = selected_uris_;
    finish(std::move(res));
    return;
  }

  if (selected_idx_ < 0 || selected_idx_ >= static_cast<int>(entries_.size())) return;
  auto& entry = entries_[selected_idx_];
  std::string full = current_path_ + "/" + entry.name;

  if (entry.is_dir) {
    load_dir(full);
  } else {
    selected_uris_.clear();
    selected_uris_.push_back(uri_from_path(full));
    Result res;
    res.response = 0;
    res.uris = selected_uris_;
    finish(std::move(res));
  }
}

// Drawing.

int FileChooserDialog::grid_cols() const {
   
  int n = std::max(1, (list_w_ - 24) / grid_cell_w_);
  if (n > 1) --n; // One column less, for breathing room.
  return n;
}

int FileChooserDialog::grid_rows() const {
   
  if (visible_.empty()) return 0;
  return (int(visible_.size()) + grid_cols() - 1) / grid_cols();
}

int FileChooserDialog::grid_cell_h() const {
   
  // Aim for 8-10 rows of cells; the cell height then shrinks or grows
  // with the window.
  int rows = std::clamp(list_h_ / 80, 6, 15);
  return list_h_ / rows;
}

void FileChooserDialog::draw_grid(cairo_t* cr, int w, int h) {
    
  (void)w; (void)h;
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  eh::config::ShellAppearance ap = sc.appearance;
  eh::matugen::refresh_wallpaper_derived_palette(ap, sc.wallpaperImage);
  const auto mc = eh::config::derived_chrome_colors(ap);
  double kAccR = mc.accentR, kAccG = mc.accentG, kAccB = mc.accentB;
  double kTxtR = mc.textR, kTxtG = mc.textG, kTxtB = mc.textB;

  int cols = grid_cols();
  if (cols < 1) cols = 1;

  int cell_w = (list_w_ - 24) / cols;
  int cell_h = grid_cell_h();
  int icon_size = std::min(64, cell_h - 30);
  int pad = 16;

  int start_row = scroll_offset_;
  int visible_rows = list_h_ / cell_h + 1;
  int end_row = std::min(start_row + visible_rows, grid_rows());

  for (int row = start_row; row < end_row; ++row) {
    for (int col = 0; col < cols; ++col) {
      int vi = row * cols + col;
      if (vi >= static_cast<int>(visible_.size())) break;
      int ei = visible_[vi];

      int cx = list_x_ + pad + col * cell_w;
      int cy = list_y_ + (row - start_row) * cell_h + 8;

      bool selected = (ei == selected_idx_);
      bool hovered = (ei == hover_idx_);
      bool hidden = entry_hidden(ei);

      // Selection/hover tint behind the cell icon.
      int hl_w = icon_size + 12;
      int hl_h = icon_size + 12;
      int hl_x = cx + (cell_w - hl_w) / 2;
      int hl_y = cy + 8;
      if (selected) {
        cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.20);
        double rr = 12;
        cairo_new_path(cr);
        cairo_arc(cr, hl_x + rr, hl_y + rr, rr, M_PI, 3*M_PI/2);
        cairo_arc(cr, hl_x + hl_w - rr, hl_y + rr, rr, 3*M_PI/2, 2*M_PI);
        cairo_arc(cr, hl_x + hl_w - rr, hl_y + hl_h - rr, rr, 0, M_PI/2);
        cairo_arc(cr, hl_x + rr, hl_y + hl_h - rr, rr, M_PI/2, M_PI);
        cairo_close_path(cr);
        cairo_fill(cr);
      } else if (hovered) {
        cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.10);
        double rr = 12;
        cairo_new_path(cr);
        cairo_arc(cr, hl_x + rr, hl_y + rr, rr, M_PI, 3*M_PI/2);
        cairo_arc(cr, hl_x + hl_w - rr, hl_y + rr, rr, 3*M_PI/2, 2*M_PI);
        cairo_arc(cr, hl_x + hl_w - rr, hl_y + hl_h - rr, rr, 0, M_PI/2);
        cairo_arc(cr, hl_x + rr, hl_y + hl_h - rr, rr, M_PI/2, M_PI);
        cairo_close_path(cr);
        cairo_fill(cr);
      }

      auto& entry = entries_[ei];
      FileIcon fi = detect_file_icon(entry.name, entry.is_dir);

      cairo_surface_t* thumb = nullptr;
      if (fi == FileIcon::Image && !entry.is_dir)
        thumb = get_thumbnail(current_path_ + "/" + entry.name, icon_size);

      if (hidden) {
        cairo_save(cr);
        cairo_rectangle(cr, cx, cy, cell_w, cell_h);
        cairo_clip(cr);
        cairo_push_group(cr);
      }

      int icon_x = cx + (cell_w - icon_size) / 2;
      int icon_y = cy + std::max(4, (cell_h - icon_size - 24) / 2);
      draw_file_icon(cr, icon_x, icon_y, icon_size, fi, selected, thumb);

      int label_y = icon_y + icon_size + 4;
      int label_w = cell_w - 8;
      int font_size = std::min(11, std::max(8, cell_h / 14));
      cairo_save(cr);
      cairo_rectangle(cr, cx + 4, label_y, label_w, cell_h - (label_y - cy) - 4);
      cairo_clip(cr);
      PangoLayout* lay = pango_cairo_create_layout(cr);
      PangoFontDescription* fd = pango_font_description_from_string("Sans");
      pango_font_description_set_size(fd, font_size * PANGO_SCALE);
      pango_layout_set_font_description(lay, fd);
      pango_font_description_free(fd);
      pango_layout_set_text(lay, entry.name.c_str(), -1);
      pango_layout_set_alignment(lay, PANGO_ALIGN_CENTER);
      pango_layout_set_width(lay, label_w * PANGO_SCALE);
      pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
      cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.9);
      cairo_move_to(cr, cx + 4, label_y);
      pango_cairo_show_layout(cr, lay);
      g_object_unref(lay);
      cairo_restore(cr);

      if (hidden) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.40);
        cairo_restore(cr);
      }
    }
  }
}

void FileChooserDialog::draw(cairo_t* cr, int w, int h) {
   
  constexpr int kTopBarH = 44;
  constexpr int kBottomBarH = 56;
  constexpr int kPad = 12;
  constexpr double kCardRad = 14.0;

  // Pull in the palette colors for this backdrop.
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  eh::config::ShellAppearance ap = sc.appearance;
  eh::matugen::refresh_wallpaper_derived_palette(ap, sc.wallpaperImage);
  const auto mc = eh::config::derived_chrome_colors(ap);
  double kAccR = mc.accentR, kAccG = mc.accentG, kAccB = mc.accentB;
  double kSurfR = mc.dockFillR, kSurfG = mc.dockFillG, kSurfB = mc.dockFillB;
  double kTxtR = mc.textR, kTxtG = mc.textG, kTxtB = mc.textB;
  double kOutR = mc.outlineR, kOutG = mc.outlineG, kOutB = mc.outlineB;

  // Dim everything behind the dialog with the surface color.
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.55);
  cairo_paint(cr);

  // Top bar: path text on the left, tools on the right.
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.9);
  cairo_rectangle(cr, 0, 0, w, kTopBarH);
  cairo_fill(cr);

  // Hairline glass separator under the top bar.
  cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.08);
  cairo_move_to(cr, 0, kTopBarH);
  cairo_line_to(cr, w, kTopBarH);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  draw_text_left(cr, current_path_.c_str(), kPad, kTopBarH / 2, kTxtR, kTxtG, kTxtB, 13);

  // Toolbar buttons: icon at 24px centered in the button, text as fallback.
  auto draw_tool = [&](int bx, int by, int bw, int bh,
                        const char* icon_name, const char* fallback,
                        int hid, bool pressed) {
     
    const auto* ic = s_icon_cache.tray_icon(icon_name);
    if (ic && ic->surface) {
      draw_button(cr, bx, by, bw, bh, "",
                  hover_idx_ == hid, pressed, kSurfR, kSurfG, kSurfB);
      double iw = static_cast<double>(ic->width), ih = static_cast<double>(ic->height);
      double scale = 20.0 / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      cairo_translate(cr, bx + (bw - iw * scale) / 2, by + (bh - ih * scale) / 2);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, ic->surface, 0, 0);
      double tint = pressed ? 0.7 : (hover_idx_ == hid ? 0.9 : 1.0);
      cairo_paint_with_alpha(cr, tint);
      cairo_restore(cr);
    } else {
      draw_button(cr, bx, by, bw, bh, fallback,
                  hover_idx_ == hid, pressed, kSurfR, kSurfG, kSurfB);
    }
  };

// Hidden-files toggle (top right).
  draw_tool(w - 126, 6, 28, kTopBarH - 12,
            "visibility-symbolic", ".H", -6, show_hidden_);

// List/grid view toggle.
  draw_tool(w - 96, 6, 28, kTopBarH - 12,
            grid_ ? "view-list-details-symbolic" : "view-grid-symbolic",
            "G", -5, grid_);

// Up to the parent directory.
  draw_tool(w - 64, 6, 56, kTopBarH - 12,
            "go-up-symbolic", "Up", -2, false);

// The file list card spanning the middle of the window.
  list_x_ = kPad;
  list_y_ = kTopBarH + kPad / 2;
  list_w_ = w - 2 * kPad;
  list_h_ = h - kTopBarH - kBottomBarH - kPad;
  entry_h_ = 36;

  // Card background, rounded corners.
  cairo_save(cr);
  {
    double x0 = list_x_, y0 = list_y_, x1 = list_x_ + list_w_, y1 = list_y_ + list_h_;
    cairo_new_path(cr);
    cairo_arc(cr, x0 + kCardRad, y0 + kCardRad, kCardRad, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, x1 - kCardRad, y0 + kCardRad, kCardRad, 3 * M_PI / 2, 2 * M_PI);
    cairo_arc(cr, x1 - kCardRad, y1 - kCardRad, kCardRad, 0, M_PI / 2);
    cairo_arc(cr, x0 + kCardRad, y1 - kCardRad, kCardRad, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.9);
    cairo_fill_preserve(cr);
  }
  // A faint glass border around it.
  cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.06);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  // Everything stays inside the card.
  cairo_rectangle(cr, list_x_, list_y_, list_w_, list_h_);
  cairo_clip(cr);

  if (grid_) {
    draw_grid(cr, w, h);
  } else {
    int visible_rows = list_h_ / entry_h_;
    int start = scroll_offset_;
    int end = std::min(start + visible_rows, static_cast<int>(visible_.size()));

    for (int vi = start; vi < end; ++vi) {
      int ei = visible_[vi];
      int ey = list_y_ + (vi - start) * entry_h_;
      bool selected = (ei == selected_idx_);
      bool hovered = (ei == hover_idx_);
      bool hidden = entry_hidden(ei);

      if (hidden) {
        cairo_save(cr);
        cairo_rectangle(cr, list_x_, ey, list_w_, entry_h_);
        cairo_clip(cr);
        cairo_push_group(cr);
      }

      if (selected) {
        cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.20);
        cairo_rectangle(cr, list_x_, ey, list_w_, entry_h_);
        cairo_fill(cr);
      } else if (hovered) {
        cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.12);
        cairo_rectangle(cr, list_x_, ey, list_w_, entry_h_);
        cairo_fill(cr);
      }

      // Hairline separator between rows.
      cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.06);
      cairo_move_to(cr, list_x_, ey + entry_h_ - 1);
      cairo_line_to(cr, list_x_ + list_w_, ey + entry_h_ - 1);
      cairo_set_line_width(cr, 1);
      cairo_stroke(cr);

      auto& entry = entries_[ei];
      FileIcon icon_type = detect_file_icon(entry.name, entry.is_dir);
      int icon_size = 22;
      cairo_surface_t* thumb = nullptr;
      if (icon_type == FileIcon::Image && !entry.is_dir)
        thumb = get_thumbnail(current_path_ + "/" + entry.name, icon_size);
      draw_file_icon(cr, list_x_ + kPad, ey + (entry_h_ - icon_size) / 2,
                     icon_size, icon_type, selected, thumb);
      int text_x = list_x_ + kPad + icon_size + kPad;
      draw_text_left(cr, entry.name.c_str(), text_x, ey + entry_h_ / 2,
                     kTxtR, kTxtG, kTxtB, 13);

      if (hidden) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, 0.40);
        cairo_restore(cr);
      }
    }
  }

  cairo_restore(cr);

  // Thin scrollbar on the card's right edge when content overflows.
  int total_items = grid_ ? grid_rows() : static_cast<int>(visible_.size());
  int visible_items = grid_ ? (list_h_ / grid_cell_h()) : (list_h_ / entry_h_);
  if (total_items > visible_items) {
    double sb_h = static_cast<double>(visible_items) / total_items * list_h_;
    double sb_y = list_y_ + static_cast<double>(scroll_offset_) / total_items * list_h_;
    cairo_set_source_rgba(cr, kOutR, kOutG, kOutB, 0.5);
    double sx = list_x_ + list_w_ - 6, sr = 2;
    cairo_new_path(cr);
    cairo_arc(cr, sx + sr, sb_y + sr, sr, M_PI, 3*M_PI/2);
    cairo_arc(cr, sx + 4 - sr, sb_y + sr, sr, 3*M_PI/2, 2*M_PI);
    cairo_arc(cr, sx + 4 - sr, sb_y + sb_h - sr, sr, 0, M_PI/2);
    cairo_arc(cr, sx + sr, sb_y + sb_h - sr, sr, M_PI/2, M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);
  }

  // Bottom bar with the action buttons.
  int by = h - kBottomBarH;
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.9);
  cairo_rectangle(cr, 0, by, w, kBottomBarH);
  cairo_fill(cr);

  // Hairline glass separator above the bottom bar.
  cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.08);
  cairo_move_to(cr, 0, by);
  cairo_line_to(cr, w, by);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  int btn_w = 90, btn_h = 36, btn_y = by + (kBottomBarH - btn_h) / 2;

// Cancel, in the tonal (outline) style.
  draw_button(cr, w - btn_w * 2 - 16, btn_y, btn_w, btn_h,
              "Cancel", hover_idx_ == -3, false, kOutR, kOutG, kOutB);

// The primary Open/Save/Select action, in accent.
  const char* action = (mode_ == Mode::Open) ? "Open" :
                       (mode_ == Mode::Directory) ? "Select" : "Save";
  draw_button(cr, w - btn_w - 8, btn_y, btn_w, btn_h,
              action, hover_idx_ == -4, false, kAccR, kAccG, kAccB);

  // Filename entry, shown in Save/SaveFiles modes.
  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    int entry_x = 16;
    int entry_w = w - btn_w * 2 - 16 - 12 - btn_w - 24;
    int entry_h = btn_h;
    int entry_y = btn_y;
    int entry_rad = 8;

    cairo_set_source_rgba(cr, kOutR, kOutG, kOutB, 0.3);
    cairo_new_path(cr);
    cairo_arc(cr, entry_x + entry_rad, entry_y + entry_rad, entry_rad, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, entry_x + entry_w - entry_rad, entry_y + entry_rad, entry_rad, 3 * M_PI / 2, 2 * M_PI);
    cairo_arc(cr, entry_x + entry_w - entry_rad, entry_y + entry_h - entry_rad, entry_rad, 0, M_PI / 2);
    cairo_arc(cr, entry_x + entry_rad, entry_y + entry_h - entry_rad, entry_rad, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    if (filename_focused_) {
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.15);
      cairo_new_path(cr);
      cairo_arc(cr, entry_x + entry_rad, entry_y + entry_rad, entry_rad, M_PI, 3 * M_PI / 2);
      cairo_arc(cr, entry_x + entry_w - entry_rad, entry_y + entry_rad, entry_rad, 3 * M_PI / 2, 2 * M_PI);
      cairo_arc(cr, entry_x + entry_w - entry_rad, entry_y + entry_h - entry_rad, entry_rad, 0, M_PI / 2);
      cairo_arc(cr, entry_x + entry_rad, entry_y + entry_h - entry_rad, entry_rad, M_PI / 2, M_PI);
      cairo_close_path(cr);
      cairo_fill(cr);
    }

    std::string display = filename_text_;
    if (display.empty()) {
      cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.3);
      draw_text_left(cr, "filename.png", entry_x + 10, entry_y + entry_h / 2, kTxtR, kTxtG, kTxtB, 13);
    } else {
      cairo_set_source_rgba(cr, kTxtR, kTxtG, kTxtB, 0.9);
      draw_text_left(cr, display.c_str(), entry_x + 10, entry_y + entry_h / 2, kTxtR, kTxtG, kTxtB, 13);
    }

    // The text cursor inside the entry.
    if (filename_focused_) {
      int cursor_x = entry_x + 10;
      if (!filename_text_.empty()) {
        std::string pre = filename_text_.substr(0, filename_cursor_);
        cairo_text_extents_t te;
        cairo_text_extents(cr, pre.c_str(), &te);
        cursor_x += static_cast<int>(te.width);
      }
      cairo_set_source_rgba(cr, kAccR, kAccG, kAccB, 0.8);
      cairo_rectangle(cr, cursor_x, entry_y + 6, 1.5, entry_h - 12);
      cairo_fill(cr);
    }
  }
}

// Grid view support.

int FileChooserDialog::entry_at(int x, int y) const {
   
  if (x < list_x_ || x >= list_x_ + list_w_ || y < list_y_ || y >= list_y_ + list_h_)
    return -1;
  if (grid_) {
    int cols = grid_cols();
    if (cols < 1) return -1;
    int cell_w = (list_w_ - 24) / cols;
    int col = (x - list_x_ - 16) / cell_w;
    if (col < 0 || col >= cols) return -1;
    int row = scroll_offset_ + (y - list_y_ - 6) / grid_cell_h();
    int vi = row * cols + col;
    if (vi >= 0 && vi < static_cast<int>(visible_.size())) return visible_[vi];
    return -1;
  }
  int vi = scroll_offset_ + (y - list_y_) / entry_h_;
  if (vi >= 0 && vi < static_cast<int>(visible_.size())) return visible_[vi];
  return -1;
}

void FileChooserDialog::toggle_view() {
   
  grid_ = !grid_;
  scroll_offset_ = 0;
  request_redraw();
}

// Mouse clicks.

void FileChooserDialog::on_click(int x, int y, int button) {
    
  (void)button;
  int w = width(), h = height();
  constexpr int kTopBarH = 44;
  constexpr int kBottomBarH = 56;
  int btn_w = 90, btn_h = 36;
  int btn_y = h - kBottomBarH + (kBottomBarH - btn_h) / 2;

  // Clicking the filename entry focuses it (Save mode).
  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    int entry_x = 16;
    int entry_w = w - btn_w * 2 - 16 - 12 - btn_w - 24;
    if (x >= entry_x && x <= entry_x + entry_w && y >= btn_y && y <= btn_y + btn_h) {
      filename_focused_ = true;
      request_redraw();
      return;
    }
  }

// Hidden files toggle, top-right corner.
  if (y >= 6 && y <= kTopBarH - 6 && x >= w - 126 && x <= w - 98) {
    toggle_hidden();
    return;
  }

// View-toggle button.
  if (y >= 6 && y <= kTopBarH - 6 && x >= w - 96 && x <= w - 68) {
    toggle_view();
    return;
  }

// Up to the parent directory.
  if (x >= w - 64 && x <= w - 8 && y >= 6 && y <= kTopBarH - 6) {
    go_up();
    return;
  }

// Cancel: close with a failure result.
  if (x >= w - btn_w * 2 - 16 && x <= w - btn_w - 16 &&
      y >= btn_y && y <= btn_y + btn_h) {
    Result r; r.response = 1;
    finish(std::move(r));
    return;
  }

// The Open/Save action.
  if (x >= w - btn_w - 8 && x <= w - 8 &&
      y >= btn_y && y <= btn_y + btn_h) {
    open_selected();
    return;
  }

// A click on the file list.
  int idx = entry_at(x, y);
  if (idx >= 0) {
    if (entries_[idx].is_dir) {
      load_dir(current_path_ + "/" + entries_[idx].name);
    } else {
      selected_idx_ = idx;
      selected_uris_.clear();
      selected_uris_.push_back(uri_from_path(current_path_ + "/" + entries_[idx].name));
      // In Save mode, prefill the entry with the clicked file's name.
      if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
        filename_text_ = entries_[idx].name;
        filename_cursor_ = static_cast<int>(filename_text_.size());
      }
      request_redraw();
    }
  }

  // Clicking anywhere else drops focus from the filename entry.
  filename_focused_ = false;
  request_redraw();
}

// Pointer motion.

void FileChooserDialog::on_pointer_move(int x, int y) {
   
  int w = width(), h = height();
  constexpr int kTopBarH = 44;
  constexpr int kBottomBarH = 56;
  int btn_w = 90;

  // Toolbar buttons first (they use negative hover ids).
  if (y < kTopBarH) {
    if (x >= w - 126 && x <= w - 98) { hover_idx_ = -6; request_redraw(); return; }
    if (x >= w - 96 && x <= w - 68)  { hover_idx_ = -5; request_redraw(); return; }
    if (x >= w - 64 && x <= w - 8)   { hover_idx_ = -2; request_redraw(); return; }
    if (hover_idx_ != -1) { hover_idx_ = -1; request_redraw(); }
    return;
  }
  if (y > h - kBottomBarH) {
    if (x >= w - btn_w * 2 - 16 && x <= w - btn_w - 16) { hover_idx_ = -3; request_redraw(); return; }
    if (x >= w - btn_w - 8 && x <= w - 8) { hover_idx_ = -4; request_redraw(); return; }
    if (hover_idx_ < 0) { hover_idx_ = -1; request_redraw(); return; }
    hover_idx_ = -1;
    request_redraw();
    return;
  }

  // Then the file list itself.
  int idx = entry_at(x, y);
  if (idx >= 0) {
    if (idx != hover_idx_) { hover_idx_ = idx; request_redraw(); }
    return;
  }

  if (hover_idx_ != -1) {
    hover_idx_ = -1;
    request_redraw();
  }
}

// Scroll wheel.

void FileChooserDialog::on_scroll(int x, int y, double dx, double dy) {
   
  (void)x; (void)y; (void)dx;
  if (grid_) {
    int vis_rows = list_h_ / grid_cell_h();
    int max_row = std::max(0, grid_rows() - vis_rows);
    scroll_offset_ = std::clamp(scroll_offset_ + (dy > 0 ? 1 : -1), 0, max_row);
  } else {
    int max_offset = std::max(0, static_cast<int>(entries_.size()) - static_cast<int>(list_h_ / entry_h_));
    scroll_offset_ = std::clamp(scroll_offset_ + (dy > 0 ? 3 : -3), 0, max_offset);
  }
  request_redraw();
}

// Keyboard input.

void FileChooserDialog::on_key(xkb_keysym_t sym, uint32_t state,
                                const char* utf8, int utf8_len) {
    
  if (state != 1) return; // Only act on key presses.

  // Filename entry editing (Save mode).
  if ((mode_ == Mode::Save || mode_ == Mode::SaveFiles) && filename_focused_) {
    if (sym == XKB_KEY_BackSpace) {
      if (filename_cursor_ > 0) {
        filename_text_.erase(filename_cursor_ - 1, 1);
        filename_cursor_--;
      }
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_Delete) {
      if (filename_cursor_ < static_cast<int>(filename_text_.size())) {
        filename_text_.erase(filename_cursor_, 1);
      }
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_Left) {
      if (filename_cursor_ > 0) filename_cursor_--;
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_Right) {
      if (filename_cursor_ < static_cast<int>(filename_text_.size())) filename_cursor_++;
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_Home) {
      filename_cursor_ = 0;
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_End) {
      filename_cursor_ = static_cast<int>(filename_text_.size());
      request_redraw();
      return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      open_selected();
      return;
    }
    if (sym == XKB_KEY_Escape) {
      filename_focused_ = false;
      request_redraw();
      return;
    }
    // Everything else that types inserts text.
    if (utf8 && utf8_len > 0) {
      for (int i = 0; i < utf8_len; ++i) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 32) continue;
        if (c == '/') continue; // don't allow path separators
      }
      filename_text_.insert(filename_cursor_, utf8, static_cast<size_t>(utf8_len));
      filename_cursor_ += utf8_len;
      request_redraw();
      return;
    }
    return; // consume all keys while focused
  }

  auto find_visible = [&](int from_idx, int delta) -> int {
    if (visible_.empty()) return -1;
    if (from_idx < 0) return visible_.front();
    if (delta > 0) {
      for (int vi = 0; vi < static_cast<int>(visible_.size()); ++vi)
        if (visible_[vi] > from_idx) return visible_[vi];
    } else {
      for (int vi = static_cast<int>(visible_.size()) - 1; vi >= 0; --vi)
        if (visible_[vi] < from_idx) return visible_[vi];
    }
    return from_idx;
  };
  auto move_sel = [&](int delta) {
    int nxt = find_visible(selected_idx_, delta);
    if (nxt < 0) return;
    selected_idx_ = nxt;
    if (!grid_) {
      int visible_rows = list_h_ / entry_h_;
      if (selected_idx_ < static_cast<int>(visible_.size())) {
        // Locate where the selected entry sits in the visible list.
        for (int vi = 0; vi < static_cast<int>(visible_.size()); ++vi) {
          if (visible_[vi] == selected_idx_) {
            if (vi < scroll_offset_) scroll_offset_ = vi;
            if (vi >= scroll_offset_ + visible_rows) scroll_offset_ = vi - visible_rows + 1;
            break;
          }
        }
      }
    }
    request_redraw();
  };

  switch (sym) {
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter:
    if (mode_ == Mode::Directory && selected_idx_ >= 0 && selected_idx_ < static_cast<int>(entries_.size())) {
      load_dir(current_path_ + "/" + entries_[selected_idx_].name);
      selected_idx_ = -1;
    } else {
      open_selected();
    }
    break;
  case XKB_KEY_Escape:
    { Result r; r.response = 1; finish(std::move(r)); }
    break;
  case XKB_KEY_Up:
    if (grid_) {
      int cols = grid_cols();
      if (cols < 1) cols = 1;
      // Grid navigation jumps a full row of columns.
      int cur_vi = -1;
      for (int vi = 0; vi < static_cast<int>(visible_.size()); ++vi)
        if (visible_[vi] == selected_idx_) { cur_vi = vi; break; }
      int tgt_vi = std::max(0, cur_vi - cols);
      if (tgt_vi < static_cast<int>(visible_.size())) selected_idx_ = visible_[tgt_vi];
      request_redraw();
    } else {
      move_sel(-1);
    }
    break;
  case XKB_KEY_Down:
    if (grid_) {
      int cols = grid_cols();
      if (cols < 1) cols = 1;
      int cur_vi = -1;
      for (int vi = 0; vi < static_cast<int>(visible_.size()); ++vi)
        if (visible_[vi] == selected_idx_) { cur_vi = vi; break; }
      int tgt_vi = std::min(static_cast<int>(visible_.size()) - 1, cur_vi + cols);
      if (tgt_vi >= 0) selected_idx_ = visible_[tgt_vi];
      request_redraw();
    } else {
      move_sel(1);
    }
    break;
  case XKB_KEY_Left:
    if (grid_) move_sel(-1);
    break;
  case XKB_KEY_Right:
    if (grid_) move_sel(1);
    break;
  case XKB_KEY_BackSpace:
    go_up();
    break;
  default:
    break;
  }
}

// Hidden file support.

bool FileChooserDialog::entry_hidden(int idx) const {
   
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return false;
  auto& name = entries_[idx].name;
  return !name.empty() && name[0] == '.';
}

void FileChooserDialog::rebuild_visible() {
   
  visible_.clear();
  visible_.reserve(entries_.size());
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
    if (!show_hidden_ && entry_hidden(i)) continue;
    visible_.push_back(i);
  }
  if (scroll_offset_ >= static_cast<int>(visible_.size())) scroll_offset_ = std::max(0, static_cast<int>(visible_.size()) - 1);
  if (selected_idx_ >= 0 && std::find(visible_.begin(), visible_.end(), selected_idx_) == visible_.end())
    selected_idx_ = -1;
}

void FileChooserDialog::toggle_hidden() {
   
  show_hidden_ = !show_hidden_;
  rebuild_visible();
  request_redraw();
}

// Standalone folder picker for the UI layers.

static std::string percent_decode(std::string_view in) {
   
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    if (in[i] == '%' && i + 2 < in.size()) {
      int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
      if (hi >= 0 && lo >= 0) { out += static_cast<char>((hi << 4) | lo); i += 3; continue; }
    }
    out += in[i];
    ++i;
  }
  return out;
}

std::string file_uri_to_local_path(const std::string& uri) {
   
  constexpr std::string_view kPrefix = "file://";
  std::string_view sv(uri);
  if (sv.size() < kPrefix.size() || sv.compare(0, kPrefix.size(), kPrefix) != 0) return {};
  auto rest = sv.substr(kPrefix.size());
  if (rest.empty()) return {};
  if (rest.front() == '/') return percent_decode(rest);
  auto slash = rest.find('/');
  if (slash == std::string_view::npos) return {};
  return percent_decode(rest.substr(slash));
}

bool show_native_folder_picker(std::string* out_path) {
   
  out_path->clear();
  FileChooserDialog dlg(FileChooserDialog::Mode::Directory, "Select Folder", "", false);
  auto result = dlg.run();
  if (result.response != 0) return false;
  if (result.uris.empty()) return false;
  std::string local = file_uri_to_local_path(result.uris.front());
  struct stat sb;
  if (local.empty() || stat(local.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) return false;
  *out_path = std::move(local);
  return true;
}

} // namespace eh::dialog
