#include "app/file_browser/features/drag.hpp"
#include "app/file_browser/features/progress.hpp"
#include "app/file_browser/app.hpp"

#include <unistd.h>
#include <sys/mman.h>

#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <vector>

#include <poll.h>

#include <cairo/cairo.h>

#include "wayland/core/protocols.hpp"
#include "wayland/core/memfd.hpp"

namespace eh::file_browser {

namespace {

// ── URI encoding helpers (mirrors ClipboardService) ────────────

std::string file_uri_for_path(const std::string& abs_path) {
  std::string out = "file://";
  for (unsigned char c : abs_path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else if (c == ' ')
      out += "%20";
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string canonical_abs_path(const std::string& path) {
  std::error_code ec;
  auto c = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !c.empty()) return c.string();
  return std::filesystem::absolute(path, ec).string();
}

// ── wl_data_source listener table ─────────────────────────────

constexpr wl_data_source_listener kDataSourceListener = {
  .target = data_source_target,
  .send = data_source_send,
  .cancelled = data_source_cancelled,
  .dnd_drop_performed = data_source_dnd_drop_performed,
  .dnd_finished = data_source_dnd_finished,
  .action = data_source_action,
};

} // namespace

// ── wl_data_source callbacks ──────────────────────────────────

void data_source_target(void*, wl_data_source*, const char*) {
}

void data_source_send(void* data, wl_data_source*, const char* mime, int32_t fd) {
  auto& app = *static_cast<AppState*>(data);
  if (fd < 0) return;

  // Non-blocking: the compositor may not read the pipe promptly,
  // and a blocking write would freeze the entire event loop.
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

  std::string_view mime_sv(mime ? mime : "");

  if (mime_sv == "text/uri-list") {
    std::string body;
    for (const auto& p : app.drag_paths) {
      std::string canon = canonical_abs_path(p);
      if (canon.empty()) continue;
      body += file_uri_for_path(canon) + "\r\n";
    }
    if (!body.empty()) {
      write(fd, body.data(), body.size());
    }
  } else if (mime_sv == "x-special/gnome-copied-files") {
    std::string body = "copy\n";
    for (const auto& p : app.drag_paths) {
      std::string canon = canonical_abs_path(p);
      if (canon.empty()) continue;
      body += file_uri_for_path(canon) + "\n";
    }
    if (body.size() > 5) {
      write(fd, body.data(), body.size());
    }
  }

  close(fd);
}

void data_source_cancelled(void* data, wl_data_source*) {
  auto& app = *static_cast<AppState*>(data);
  cancel_drag(app);
}

void data_source_dnd_drop_performed(void*, wl_data_source*) {
}

void data_source_dnd_finished(void* data, wl_data_source*) {
  auto& app = *static_cast<AppState*>(data);
  cancel_drag(app);
}

void data_source_action(void*, wl_data_source*, uint32_t) {
}

// ── Start drag ────────────────────────────────────────────────

void start_drag(AppState& app) {
  if (!app.data_device || app.drag_paths.empty()) {
    cancel_drag(app);
    return;
  }

  // Create data source
  auto* mgr = app.wl.data_device_manager();
  if (!mgr) {
    cancel_drag(app);
    return;
  }

  app.drag_source = wl_data_device_manager_create_data_source(mgr);
  if (!app.drag_source) {
    cancel_drag(app);
    return;
  }

  // Offer MIME types
  wl_data_source_offer(app.drag_source, "text/uri-list");
  wl_data_source_offer(app.drag_source, "x-special/gnome-copied-files");

  wl_data_source_add_listener(app.drag_source, &kDataSourceListener, &app);

  // Determine file info for the ghost icon
  FileType drag_ft = FileType::File;
  std::string drag_label;
  if (!app.drag_paths.empty()) {
    drag_label = std::filesystem::path(app.drag_paths[0]).filename().string();
    for (const auto& e : app.cur_tab().entries) {
      if (e.path == app.drag_paths[0]) {
        drag_ft = e.is_dir ? FileType::Folder : e.type;
        break;
      }
    }
  }

  auto icon_name_for_type = [](FileType ft) -> const char* {
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
      default:                   return "text-x-generic";
    }
  };

  // word-wrap helper (mirrors the one in ui/draw.cpp)
  auto word_wrap_lines = [](cairo_t* cr, const std::string& text, double max_width) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    cairo_text_extents_t te;
    std::string word;
    std::string cur;

    while (stream >> word) {
      cairo_text_extents(cr, word.c_str(), &te);
      bool fits_anywhere = te.width <= max_width;

      if (fits_anywhere && cur.empty()) {
        cur = word;
      } else if (fits_anywhere) {
        cairo_text_extents(cr, (cur + " " + word).c_str(), &te);
        if (te.width <= max_width) {
          cur += " " + word;
        } else {
          lines.push_back(cur);
          cur = word;
        }
      } else {
        if (!cur.empty()) {
          lines.push_back(cur);
          cur.clear();
        }
        std::vector<std::string> parts;
        size_t seg_start = 0;
        for (size_t i = 0; i < word.size(); i++) {
          if (word[i] == '-') {
            parts.push_back(word.substr(seg_start, i - seg_start + 1));
            seg_start = i + 1;
          }
        }
        if (seg_start < word.size())
          parts.push_back(word.substr(seg_start));
        if (parts.empty())
          parts.push_back(word);
        std::string line;
        for (const auto& part : parts) {
          std::string test = line + part;
          cairo_text_extents(cr, test.c_str(), &te);
          if (te.width > max_width) {
            if (!line.empty()) {
              lines.push_back(line);
              line.clear();
            }
            cairo_text_extents(cr, part.c_str(), &te);
            if (te.width > max_width) {
              std::string frag;
              for (char ch : part) {
                cairo_text_extents(cr, (frag + ch).c_str(), &te);
                if (te.width > max_width) {
                  if (!frag.empty()) {
                    lines.push_back(frag);
                    frag.clear();
                  }
                  frag = ch;
                } else {
                  frag += ch;
                }
              }
              if (!frag.empty()) line = frag;
            } else {
              line = part;
            }
          } else {
            line += part;
          }
        }
        if (!line.empty())
          cur = line;
      }
    }
    if (!cur.empty())
      lines.push_back(cur);

    for (auto& line : lines) {
      cairo_text_extents(cr, line.c_str(), &te);
      if (te.width > max_width) {
        while (!line.empty() && te.width > max_width) {
          line.pop_back();
          cairo_text_extents(cr, (line + "...").c_str(), &te);
        }
        line += "...";
      }
    }
    return lines;
  };

  // Create drag icon surface (ghost image) with word-wrap
  const int icon_size = 48;
  const int label_font_size = 12;
  const int pad_x = 12;
  const int pad_top = 10;
  const int pad_bot = 10;
  const int icon_gap = 6;
  const double line_height = label_font_size * 1.3;
  const int ghost_w = 140;
  const int cursor_off_x = 16;  // shift ghost right of cursor
  const int cursor_off_y = 16;  // shift ghost below cursor

  if (!app.drag_icon_surface) {
    app.drag_icon_surface = wl_compositor_create_surface(app.wl.compositor());
  }

  if (app.drag_icon_surface) {
    // Open a temporary cairo context to compute word-wrap
    auto* tmp_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto* tmp_cr = cairo_create(tmp_surf);
    cairo_select_font_face(tmp_cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(tmp_cr, label_font_size);

    double text_max_w = ghost_w - pad_x * 2;
    auto lines = word_wrap_lines(tmp_cr, drag_label, text_max_w);

    // Limit lines shown (3 max, rest get "...")
    if (lines.size() > 3) {
      lines.resize(3);
      lines.back() = "...";
    }

    double text_block_h = lines.size() * line_height;
    int ghost_h = pad_top + icon_size + icon_gap + static_cast<int>(text_block_h) + pad_bot;

    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp_surf);

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ghost_w);
    int buf_size = stride * ghost_h;

    int memfd = eh::wayland::memfd_create_compat("eh-dnd-icon", 0);
    if (memfd >= 0 && ftruncate(memfd, buf_size) == 0) {
      auto* pool = wl_shm_create_pool(app.shm, memfd, buf_size);
      if (pool) {
        auto* buf = wl_shm_pool_create_buffer(pool, 0, ghost_w, ghost_h, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);

        auto* data = static_cast<unsigned char*>(
            mmap(nullptr, static_cast<size_t>(buf_size), PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0));
        if (data && data != MAP_FAILED) {
          std::memset(data, 0, static_cast<size_t>(buf_size));
          cairo_surface_t* cairo_surf =
              cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, ghost_w, ghost_h, stride);
          cairo_t* cr = cairo_create(cairo_surf);

          cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr, label_font_size);

          // Draw the file icon from icon cache
          int icon_cy = pad_top;
          int icon_cx = (ghost_w - icon_size) / 2;
          const auto* icon_entry = app.icons.tray_icon(icon_name_for_type(drag_ft));
          if (icon_entry && icon_entry->surface) {
            double iw = static_cast<double>(icon_entry->width);
            double ih = static_cast<double>(icon_entry->height);
            if (iw > 0 && ih > 0) {
              double scale = icon_size / std::max(1.0, std::max(iw, ih));
              cairo_save(cr);
              cairo_translate(cr, icon_cx, icon_cy);
              cairo_scale(cr, scale, scale);
              cairo_set_source_surface(cr, icon_entry->surface,
                                       (icon_size / scale - iw) / 2,
                                       (icon_size / scale - ih) / 2);
              cairo_paint(cr);
              cairo_restore(cr);
            }
          }

          // Draw label text (word-wrapped, centered)
          int text_y = pad_top + icon_size + icon_gap + static_cast<int>(line_height);

          // Vertically center the text block in the label area
          double label_area_h = static_cast<double>(ghost_h) - pad_top - icon_size - icon_gap - pad_bot;
          double text_block_offset = (label_area_h - lines.size() * line_height) / 2.0;

          cairo_set_source_rgba(cr, 0.92, 0.92, 0.95, 0.9);
          for (size_t i = 0; i < lines.size(); i++) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, lines[i].c_str(), &te);
            double lx = (ghost_w - te.width) / 2.0;
            double ly = text_y + i * line_height + text_block_offset;
            cairo_move_to(cr, lx, ly);
            cairo_show_text(cr, lines[i].c_str());
          }

          // Draw count badge if multiple files
          cairo_text_extents_t te;
          if (app.drag_paths.size() > 1) {
            std::string count = std::to_string(app.drag_paths.size());
            cairo_set_source_rgba(cr, 0.4, 0.7, 0.95, 0.9);
            cairo_arc(cr, ghost_w - 12, 12, 10, 0, 2 * 3.14159);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 11);
            cairo_text_extents(cr, count.c_str(), &te);
            cairo_move_to(cr, ghost_w - 12 - te.width / 2, 12 + te.height / 2);
            cairo_show_text(cr, count.c_str());
          }

          cairo_destroy(cr);
          cairo_surface_destroy(cairo_surf);
          munmap(data, static_cast<size_t>(buf_size));
        }
        close(memfd);
        wl_surface_attach(app.drag_icon_surface, buf, -cursor_off_x, -cursor_off_y);
        wl_surface_commit(app.drag_icon_surface);
        app.drag_icon_attached = true;
      } else {
        close(memfd);
      }
    } else if (memfd >= 0) {
      close(memfd);
    }
  }

  // Start the drag session
  wl_data_device_start_drag(
      app.data_device,
      app.drag_source,
      app.surface,
      app.drag_icon_surface,
      app.drag_button_serial);

  app.drag_potential = false;
}

// ── Cancel drag ───────────────────────────────────────────────

void cancel_drag(AppState& app) {
  if (app.drag_source) {
    wl_data_source_destroy(app.drag_source);
    app.drag_source = nullptr;
  }
  if (app.drag_icon_surface && app.drag_icon_attached) {
    wl_surface_attach(app.drag_icon_surface, nullptr, 0, 0);
    wl_surface_commit(app.drag_icon_surface);
    app.drag_icon_attached = false;
  }
  app.drag_paths.clear();
  app.drag_potential = false;
  app.drag_potential_idx = -1;
  // Clear drop target state
  app.drop_target_path.clear();
  app.drop_target_idx = -1;
  app.drop_target_is_sidebar = false;
  app.drop_target_sidebar_idx = -1;
  app.drop_target_is_valid = false;
  app.drop_x = 0;
  app.drop_y = 0;
}

void update_drag_icon(AppState& app) {
  (void)app;
}

// ── Drop receiver (wl_data_device listener) ─────────────────────

namespace {

struct DropOfferData {
  std::vector<std::string> mime_types;
};

void data_offer_offer(void* data, wl_data_offer*, const char* mime_type) {
  auto& od = *static_cast<DropOfferData*>(data);
  od.mime_types.emplace_back(mime_type);
}

void data_offer_source_actions(void*, wl_data_offer*, uint32_t) {}
void data_offer_action(void*, wl_data_offer*, uint32_t) {}

constexpr wl_data_offer_listener kDataOfferListener = {
  .offer = data_offer_offer,
  .source_actions = data_offer_source_actions,
  .action = data_offer_action,
};

bool has_mime(const DropOfferData& od, const std::string_view target) {
  for (const auto& m : od.mime_types) {
    if (m == target) return true;
  }
  return false;
}

} // namespace

void data_device_data_offer(void* data, wl_data_device*, wl_data_offer* offer) {
  auto& app = *static_cast<AppState*>(data);
  if (app.drop_offer) {
    auto* old = static_cast<DropOfferData*>(wl_data_offer_get_user_data(app.drop_offer));
    delete old;
    wl_data_offer_destroy(app.drop_offer);
  }
  app.drop_offer = offer;
  auto* offer_data = new DropOfferData;
  wl_data_offer_set_user_data(offer, offer_data);
  wl_data_offer_add_listener(offer, &kDataOfferListener, offer_data);
}

void data_device_enter(void*, wl_data_device*, uint32_t serial, wl_surface*,
                       wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
  auto* offer_data = static_cast<DropOfferData*>(wl_data_offer_get_user_data(offer));
  if (offer_data && has_mime(*offer_data, "text/uri-list")) {
    wl_data_offer_accept(offer, serial, "text/uri-list");
  }
  // Clear previous drop target when entering with a new drag
  // (state is set/replaced by data_device_motion)
}

void data_device_leave(void* data, wl_data_device*) {
  auto& app = *static_cast<AppState*>(data);
  app.drop_target_path.clear();
  app.drop_target_idx = -1;
  app.drop_target_is_sidebar = false;
  app.drop_target_sidebar_idx = -1;
  app.drop_target_fav_section = false;
  app.drop_target_is_valid = false;
  app.drop_x = 0;
  app.drop_y = 0;
  app.pendingRedraw = true;
  if (app.drop_offer) {
    auto* offer_data = static_cast<DropOfferData*>(wl_data_offer_get_user_data(app.drop_offer));
    delete offer_data;
    wl_data_offer_destroy(app.drop_offer);
    app.drop_offer = nullptr;
  }
}

void data_device_motion(void* data, wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
  auto& app = *static_cast<AppState*>(data);
  int sx = wl_fixed_to_int(x);
  int sy = wl_fixed_to_int(y);
  app.drop_x = sx;
  app.drop_y = sy;

  // Check sidebar first (higher priority)
  int sb_idx = hit_test_sidebar(app, sx, sy);
  if (sb_idx >= 0 && sb_idx < static_cast<int>(app.sidebar_locations.size())) {
    // If over a sidebar item, clear the favorites section indicator
    if (app.drop_target_fav_section) {
      app.drop_target_fav_section = false;
      app.pendingRedraw = true;
    }
    const auto& loc = app.sidebar_locations[sb_idx];
    std::error_code ec;
    if (std::filesystem::is_directory(loc.path, ec)) {
      if (!app.drop_target_is_sidebar || app.drop_target_sidebar_idx != sb_idx) {
        app.drop_target_fav_section = false;
        app.drop_target_path = loc.path;
        app.drop_target_idx = -1;
        app.drop_target_is_sidebar = true;
        app.drop_target_sidebar_idx = sb_idx;
        app.drop_target_is_valid = true;
        app.pendingRedraw = true;
      }
      return;
    }
  }

  // Check if dragging over the Favorites section (to add a new favorite)
  if (hit_test_fav_section(app, sx, sy)) {
    if (!app.drop_target_fav_section) {
      app.drop_target_fav_section = true;
      app.drop_target_is_sidebar = false;
      app.drop_target_sidebar_idx = -1;
      app.drop_target_path.clear();
      app.drop_target_is_valid = true;
      app.pendingRedraw = true;
    }
    return;
  }

  // Check visible entries (only folders are valid targets)
  int idx = -1;
  if (app.cur_tab().view_mode == ViewMode::List) {
    idx = hit_test_list(app, sx, sy);
  } else {
    idx = hit_test_grid(app, sx, sy);
  }

  if (idx >= 0 && idx < static_cast<int>(app.cur_tab().visible_entries.size())) {
    int real_idx = app.cur_tab().visible_entries[idx];
    if (real_idx >= 0 && real_idx < static_cast<int>(app.cur_tab().entries.size()) &&
        app.cur_tab().entries[real_idx].is_dir) {
      if (app.drop_target_idx != idx || app.drop_target_is_sidebar) {
        app.drop_target_path = app.cur_tab().entries[real_idx].path;
        app.drop_target_idx = idx;
        app.drop_target_is_sidebar = false;
        app.drop_target_sidebar_idx = -1;
        app.drop_target_is_valid = true;
        app.pendingRedraw = true;
      }
      return;
    }
  }

  // No valid target under cursor
  if (app.drop_target_is_valid || app.drop_target_fav_section) {
    app.drop_target_path.clear();
    app.drop_target_idx = -1;
    app.drop_target_is_sidebar = false;
    app.drop_target_sidebar_idx = -1;
    app.drop_target_fav_section = false;
    app.drop_target_is_valid = false;
    app.pendingRedraw = true;
  }
}

void data_device_selection(void*, wl_data_device*, wl_data_offer*) {}

void data_device_drop(void* data, wl_data_device*) {
  auto& app = *static_cast<AppState*>(data);
  if (!app.drop_offer) return;

  // ── Within-app drop ─────────────────────────────────────────────
  if (app.drag_source && !app.drag_paths.empty()) {
    auto* offer_data = static_cast<DropOfferData*>(wl_data_offer_get_user_data(app.drop_offer));
    delete offer_data;
    wl_data_offer_destroy(app.drop_offer);
    app.drop_offer = nullptr;

    // Drop on Favorites section — add dragged folders as favorites
    if (app.drop_target_fav_section) {
      for (const auto& p : app.drag_paths) {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
          if (std::find(app.favorites.begin(), app.favorites.end(), p) == app.favorites.end()) {
            app.favorites.push_back(p);
          }
        }
      }
      save_file_browser_settings(app);
      refresh_sidebar(app);
      app.drop_target_fav_section = false;
      app.drop_target_is_valid = false;
      app.drop_target_path.clear();
      app.pendingRedraw = true;
      return;
    }

    // Use drop target if valid, otherwise current directory
    std::string target = app.drop_target_is_valid ? app.drop_target_path : app.cur_tab().current_path;

    // Copy or move based on Ctrl state
    bool ctrl = false;
    if (auto* xkb = app.seat.xkb_state_ptr()) {
      ctrl = xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CTRL,
                                          XKB_STATE_MODS_EFFECTIVE) != 0;
    }

    // Resolve target paths and push undo record (synchronous)
    std::vector<std::string> src_paths;
    for (const auto& src : app.drag_paths) {
      std::string dest = (std::filesystem::path(target) / std::filesystem::path(src).filename()).string();
      src_paths.push_back(dest);
    }
    {
      AppState::UndoRecord rec{ctrl ? AppState::UndoRecord::Type::PasteCopy
                                    : AppState::UndoRecord::Type::PasteCut, {}, {}};
      if (ctrl) {
        rec.paths_b = src_paths;
      } else {
        rec.paths_a = app.drag_paths;
        rec.paths_b = src_paths;
      }
      app.redo_stack.clear();
      app.undo_stack.push_back(std::move(rec));
      if (app.undo_stack.size() > app.kMaxUndo)
        app.undo_stack.erase(app.undo_stack.begin());
    }

    // Start async copy/move
    {
      auto prog = std::make_shared<OperationProgress>();
      prog->type = ctrl ? OperationType::Copy : OperationType::Move;
      std::vector<std::string> drag_src = app.drag_paths;
      start_async_op(drag_src, target, !ctrl, prog,
          [&app, ctrl](bool cancelled) {
            if (!cancelled) {
              app.operation_status = ctrl ? "Copied" : "Moved";
              app.operation_status_expires_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count() + 3000;
            }
            app.op_progress.reset();
            // Clear drop target state
            app.drop_target_path.clear();
            app.drop_target_idx = -1;
            app.drop_target_is_sidebar = false;
            app.drop_target_sidebar_idx = -1;
            app.drop_target_fav_section = false;
            app.drop_target_is_valid = false;
            app.drop_x = 0;
            app.drop_y = 0;
            reload_dir(app);
            app.pendingRedraw = true;
          });
      app.op_progress = prog;
    }

    return;
  }

  // ── External drop: read URI data through the compositor ─────────
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) < 0) return;

  wl_data_offer_receive(app.drop_offer, "text/uri-list", fds[1]);
  close(fds[1]);

  // The receive request must be flushed before the compositor will
  // send the data.  After flushing, poll both the Wayland display fd
  // and the pipe fd so we can dispatch the incoming events (e.g. the
  // source client's data-source.send) while waiting for the pipe.
  wl_display_flush(app.wl.display());

  int dpy_fd = wl_display_get_fd(app.wl.display());
  std::string buf;
  char tmp[4096];

  for (;;) {
    struct pollfd pf[2];
    pf[0].fd = dpy_fd;
    pf[0].events = POLLIN;
    pf[1].fd = fds[0];
    pf[1].events = POLLIN;

    int pr = poll(pf, 2, -1);
    if (pr < 0) break;

    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(app.wl.display()) < 0) break;
    }

    if (pf[1].revents & (POLLIN | POLLHUP)) {
      ssize_t n = read(fds[0], tmp, sizeof(tmp));
      if (n > 0) {
        buf.append(tmp, static_cast<size_t>(n));
      } else {
        break;
      }
    }
  }

  close(fds[0]);

  // Parse the first file:// URI from the received data
  std::string path;
  size_t pos = 0;
  while (pos < buf.size()) {
    auto end = buf.find_first_of("\r\n", pos);
    if (end == std::string::npos) end = buf.size();
    std::string line = buf.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line.starts_with("file://")) {
      std::string raw = line.substr(7);
      std::string decoded;
      for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
          char hex[3] = {raw[i + 1], raw[i + 2], 0};
          decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
          i += 2;
        } else {
          decoded += raw[i];
        }
      }
      path = decoded;
      break;
    }
    if (end >= buf.size()) break;
    pos = end + 1;
  }

  {
    auto* offer_data = static_cast<DropOfferData*>(wl_data_offer_get_user_data(app.drop_offer));
    delete offer_data;
  }
  wl_data_offer_destroy(app.drop_offer);
  app.drop_offer = nullptr;

  if (!path.empty()) {
    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      navigate_to(app, parent.string());
    }
  }
}

void setup_drop_receiver(AppState& app) {
  if (!app.data_device) return;

  static constexpr wl_data_device_listener kDataDeviceListener = {
    .data_offer = data_device_data_offer,
    .enter = data_device_enter,
    .leave = data_device_leave,
    .motion = data_device_motion,
    .drop = data_device_drop,
    .selection = data_device_selection,
  };
  wl_data_device_add_listener(app.data_device, &kDataDeviceListener, &app);
}

} // namespace eh::file_browser
