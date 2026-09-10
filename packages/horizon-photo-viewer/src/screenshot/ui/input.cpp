#include "screenshot/ui/input.hpp"
#include "screenshot/ui/layout.hpp"
#include "screenshot/app.hpp"
#include "screenshot/capture/capture.hpp"
#include "screenshot/screencopy.hpp"
#include "core/screenshot/file_chooser_dialog.hpp"

#include "core/screenshot/logging.hpp"

#include <wlr-foreign-toplevel-management-unstable-v1-client-protocol.h>

#include <algorithm>
#include <cstdarg>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <poll.h>

namespace hpv::sc {

static int hit_grid_cell(const Layout& l, int x, int y)
{
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < 2; ++col) {
      int cx = l.sidebar_content_x + col * (l.grid_cell_w + 8);
      int cy = l.grid_y + row * (l.grid_cell_h + 8);
      if (x >= cx && x < cx + l.grid_cell_w &&
          y >= cy && y < cy + l.grid_cell_h) {
        return row * 2 + col;
      }
    }
  }
  return -1;
}

static int hit_window_list(const AppState& app, const Layout& l, int x, int y)
{
  if (app.source != Source::Window || app.window_list.empty()) return -1;

  int header_h = 20;
  int list_section_y = l.list_y;
  int item_y = list_section_y + header_h + 4;

  if (x < l.sidebar_content_x || x > l.sidebar_content_x + l.sidebar_content_w) return -1;

  int max_items = (l.export_y - item_y - 8) / l.list_item_h;
  if (max_items < 1) max_items = 1;

  for (int i = 0; i < max_items && i < static_cast<int>(app.window_list.size()); ++i) {
    int iy = item_y + i * l.list_item_h;
    if (y >= iy && y < iy + l.list_item_h) return i;
  }
  return -1;
}

static int hit_output_list(const AppState& app, const Layout& l, int x, int y)
{
  if (app.source != Source::Screen || app.output_list.empty()) return -1;

  int header_h = 20;
  int list_section_y = l.list_y;
  int item_y = list_section_y + header_h + 4;

  if (x < l.sidebar_content_x || x > l.sidebar_content_x + l.sidebar_content_w) return -1;

  int n = static_cast<int>(app.output_list.size());
  int visual_count = n + 1;
  int max_items = (l.export_y - item_y - 8) / l.list_item_h;
  if (max_items < 1) max_items = 1;

  for (int vi = 0; vi < max_items && vi < visual_count; ++vi) {
    int iy = item_y + vi * l.list_item_h;
    if (y >= iy && y < iy + l.list_item_h) return vi;
  }
  return -1;
}

static int hit_export_btn(const Layout& l, int x, int y)
{
  int export_content_y = l.export_y + (l.export_h - 34) / 2;
  int gap = 12;

  for (int i = 0; i < 3; ++i) {
    int bx = l.sidebar_content_x + i * (l.export_btn_w + gap);
    if (x >= bx && x < bx + l.export_btn_w &&
        y >= export_content_y && y < export_content_y + 34) {
      return i;
    }
  }

  return -1;
}

static void do_export_png(AppState& app);
static void do_save_as(AppState& app);
static void do_copy_png(AppState& app);

void trigger_capture(AppState& app)
{
  SC_LOG("trigger_capture: enter source=%d selected_window=%d", (int)app.source, app.selected_window_idx);
  char tmp_pattern[] = "/tmp/eh-shot-XXXXXX";
  int fd = mkstemp(tmp_pattern);
  if (fd < 0) {
    app.status = "Failed to create temp file";
    app.pendingRedraw = true;
    return;
  }
  std::string out_path(tmp_pattern);
  close(fd);

  bool ok = false;
  switch (app.source) {
  case Source::Focused:
    SC_LOG("trigger_capture: Focused");
    ok = capture_focused_window(app.wl, out_path);
    break;
  case Source::Window:
    SC_LOG("trigger_capture: Window (idx=%d)", app.selected_window_idx);
    if (app.selected_window_idx >= 0 &&
        app.selected_window_idx < static_cast<int>(app.window_list.size())) {
      auto* handle = app.window_list[app.selected_window_idx].handle;
      if (!handle) {
        SC_LOG("trigger_capture: Window mode handle is null (wlr-foreign-toplevel, no ext-image-copy-capture) - falling back to screen capture");
      }
      ok = capture_window_by_handle(app.wl, handle, out_path);
    } else {
      app.status = "No window selected";
      unlink(out_path.c_str());
      app.pendingRedraw = true;
      return;
    }
    break;
  case Source::Screen:
    if (app.capture_all_screens) {
      SC_LOG("trigger_capture: all screens");
      app.captured_hdr = {};
      ok = capture_all_screens(app.wl, out_path, &app.captured_hdr);
    } else if (app.selected_output_idx >= 0 &&
               app.selected_output_idx < static_cast<int>(app.output_list.size())) {
      SC_LOG("trigger_capture: output idx=%d", app.selected_output_idx);
      app.captured_hdr = {};
      ok = capture_output(app.wl, app.output_list[app.selected_output_idx].output, out_path, &app.captured_hdr);
    } else {
      SC_LOG("trigger_capture: single screen (no output selected)");
      ok = capture_screen(app.wl, out_path);
    }
    break;
  case Source::Selection:
    {
      SC_LOG("trigger_capture: Selection - refreshing outputs");
      app.wl.refresh_logical_outputs();
      auto bounds = app.wl.logical_output_bounds();
      SC_LOG("trigger_capture: Selection - %zu logical bounds", bounds.size());
      ok = capture_selection_interactive(app.wl, bounds, out_path);
      wl_display_roundtrip(app.wl.display());
    }
    break;
  }

  if (!ok) {
    app.status = "Capture failed";
    unlink(out_path.c_str());
    app.pendingRedraw = true;
    return;
  }

  if (app.captured_hdr.valid) {
    std::string preview_path = out_path + ".preview.png";
    if (write_preview_from_hdr(app.captured_hdr, preview_path)) {
      app.captured = load_capture(preview_path);
      unlink(preview_path.c_str());
    } else {
      app.captured = load_capture(out_path);
    }
  } else {
    app.captured = load_capture(out_path);
  }
  app.zoom = 1.0;
  app.pan_x = 0.0;
  app.pan_y = 0.0;
  app.dragging = false;
  if (app.cached_img) { cairo_surface_destroy(app.cached_img); app.cached_img = nullptr; }

  if (!app.last_capture_path.empty()) {
    unlink(app.last_capture_path.c_str());
  }
  app.last_capture_path = out_path;

  if (app.captured.valid) {
    if (app.source == Source::Window && app.selected_window_idx >= 0 &&
        static_cast<size_t>(app.selected_window_idx) < app.window_list.size() &&
        !app.window_list[app.selected_window_idx].handle) {
      app.status = "Full screen captured (window mode unavailable)";
    } else {
      app.status = "Captured";
    }
    do_copy_png(app);
  } else {
    app.status = "Could not load preview";
  }
  app.pendingRedraw = true;
  SC_LOG("trigger_capture: exit captured_valid=%d", app.captured.valid);
}

static void do_export_png(AppState& app)
{
  SC_LOG("do_export_png: enter");
  if (app.last_capture_path.empty() || !app.captured.valid) {
    app.status = "Nothing to export";
    app.pendingRedraw = true;
    return;
  }

  auto now = std::time(nullptr);
  char defname[80];
  std::string save_ext;
  if (app.captured_hdr.valid) {
#ifdef EH_HAVE_LIBJXL
    std::strftime(defname, sizeof(defname), "screenshot-HDR-%Y%m%d-%H%M%S.jxl", std::localtime(&now));
    save_ext = ".jxl";
#else
    std::strftime(defname, sizeof(defname), "screenshot-HDR-%Y%m%d-%H%M%S.png", std::localtime(&now));
    save_ext = ".png";
#endif
  } else {
    std::strftime(defname, sizeof(defname), "screenshot-%Y%m%d-%H%M%S.png", std::localtime(&now));
    save_ext = ".png";
  }

  dialog::FileChooserDialog dlg(
      dialog::FileChooserDialog::Mode::Save,
      "Save Screenshot", "", false);
  dlg.set_suggested_filename(defname);

  std::atomic<bool> dlg_done{false};
  dialog::DialogBase::Result dlg_result;
  std::thread dlg_thread([&]() {
    dlg_result = dlg.run();
    dlg_done = true;
  });

  int dpy_fd = wl_display_get_fd(app.wl.display());
  while (!dlg_done) {
    wl_display_dispatch_pending(app.wl.display());
    wl_display_flush(app.wl.display());

    struct pollfd pfd;
    pfd.fd = dpy_fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 50);
    if (ret < 0) break;
    if (ret > 0 && (pfd.revents & POLLIN)) {
      wl_display_dispatch(app.wl.display());
    }
  }
  dlg_thread.join();

  if (dlg_result.response != 0 || dlg_result.uris.empty()) {
    app.status = "Export cancelled";
    app.pendingRedraw = true;
    return;
  }

  std::string out_path = dialog::file_uri_to_local_path(dlg_result.uris[0]);
  if (out_path.empty()) {
    app.status = "Export cancelled";
    app.pendingRedraw = true;
    return;
  }

  bool ok = false;
  if (app.captured_hdr.valid) {
#ifdef EH_HAVE_LIBJXL
    ok = write_jxl_hdr(
        app.captured_hdr.linear_rgb.data(),
        app.captured_hdr.width, app.captured_hdr.height,
        out_path.c_str(), app.captured_hdr.max_lum);
#elif defined(EH_HAVE_LIBPNG)
    ok = write_png16_from_linear(
        app.captured_hdr.linear_rgb.data(),
        app.captured_hdr.width, app.captured_hdr.height,
        out_path.c_str());
#endif
    if (!ok) {
      ok = save_composed_png(app.captured, app.frame, out_path);
    }
  } else {
    ok = save_composed_png(app.captured, app.frame, out_path);
  }
  if (ok) {
    app.status = "Saved to " + out_path;
  } else {
    app.status = "Export failed";
  }
  app.pendingRedraw = true;
}

static void do_save_as(AppState& app)
{
  SC_LOG("do_save_as: enter");
  if (app.last_capture_path.empty() || !app.captured.valid) {
    app.status = "Nothing to export";
    app.pendingRedraw = true;
    return;
  }

  auto now = std::time(nullptr);
  char defname[80];
  if (app.captured_hdr.valid) {
    std::strftime(defname, sizeof(defname), "screenshot-HDR-%Y%m%d-%H%M%S.jxl",
                  std::localtime(&now));
  } else {
    std::strftime(defname, sizeof(defname), "screenshot-%Y%m%d-%H%M%S.jxl",
                  std::localtime(&now));
  }

  dialog::FileChooserDialog dlg(
      dialog::FileChooserDialog::Mode::Save,
      "Save Screenshot", "", false);
  dlg.set_suggested_filename(defname);

  std::atomic<bool> dlg_done{false};
  dialog::DialogBase::Result dlg_result;
  std::thread dlg_thread([&]() {
    dlg_result = dlg.run();
    dlg_done = true;
  });

  int dpy_fd = wl_display_get_fd(app.wl.display());
  while (!dlg_done) {
    wl_display_dispatch_pending(app.wl.display());
    wl_display_flush(app.wl.display());

    struct pollfd pfd;
    pfd.fd = dpy_fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 50);
    if (ret < 0) break;
    if (ret > 0 && (pfd.revents & POLLIN)) {
      wl_display_dispatch(app.wl.display());
    }
  }
  dlg_thread.join();

  if (dlg_result.response != 0 || dlg_result.uris.empty()) {
    app.status = "Export cancelled";
    app.pendingRedraw = true;
    return;
  }

  std::string out_path = dialog::file_uri_to_local_path(dlg_result.uris[0]);
  if (out_path.empty()) {
    app.status = "Export cancelled";
    app.pendingRedraw = true;
    return;
  }

  bool ok = false;
  if (app.captured_hdr.valid) {
#ifdef EH_HAVE_LIBJXL
    ok = write_jxl_hdr(
        app.captured_hdr.linear_rgb.data(),
        app.captured_hdr.width, app.captured_hdr.height,
        out_path.c_str(), app.captured_hdr.max_lum);
#elif defined(EH_HAVE_LIBPNG)
    ok = write_png16_from_linear(
        app.captured_hdr.linear_rgb.data(),
        app.captured_hdr.width, app.captured_hdr.height,
        out_path.c_str());
#endif
    if (!ok) {
      ok = save_composed_png(app.captured, app.frame, out_path);
    }
  } else {
    ok = save_composed_png(app.captured, app.frame, out_path);
  }
  if (ok) {
    app.status = "Saved to " + out_path;
  } else {
    app.status = "Export failed";
  }
  app.pendingRedraw = true;
  SC_LOG("do_save_as: exit ok=%d", ok);
}

static void do_copy_png(AppState& app)
{
  SC_LOG("do_copy_png: enter");
  if (app.last_capture_path.empty() || !app.captured.valid) {
    app.status = "Nothing to copy";
    app.pendingRedraw = true;
    return;
  }

  if (!app.clipboard.is_available()) {
    app.status = "Clipboard unavailable";
    app.pendingRedraw = true;
    return;
  }

  char tmp_pattern[] = "/tmp/eh-shot-copy-XXXXXX";
  int fd = mkstemp(tmp_pattern);
  if (fd < 0) {
    app.status = "Copy failed";
    app.pendingRedraw = true;
    return;
  }
  close(fd);
  std::string tmp_path(tmp_pattern);

  bool ok = save_composed_png(app.captured, app.frame, tmp_path);
  if (!ok) {
    app.status = "Copy composition failed";
    unlink(tmp_path.c_str());
    app.pendingRedraw = true;
    return;
  }

  FILE* f = fopen(tmp_path.c_str(), "rb");
  if (!f) {
    unlink(tmp_path.c_str());
    app.status = "Copy failed";
    app.pendingRedraw = true;
    return;
  }
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  rewind(f);
  std::string png_data(static_cast<std::string::size_type>(fsize), '\0');
  size_t nread = fread(png_data.data(), 1, static_cast<size_t>(fsize), f);
  fclose(f);
  unlink(tmp_path.c_str());

  if (static_cast<long>(nread) != fsize) {
    app.status = "Copy failed";
    app.pendingRedraw = true;
    return;
  }

  if (app.clipboard.copy_data("image/png", std::move(png_data))) {
    app.status = "Copied to clipboard";
  } else {
    app.status = "Clipboard failed";
  }
  app.pendingRedraw = true;
  SC_LOG("do_copy_png: exit");
}

void handle_click(AppState& app, int x, int y)
{
  SC_LOG("handle_click: enter x=%d y=%d", x, y);
  auto l = compute_layout(app.width, app.height);

  int cell = hit_grid_cell(l, x, y);
  if (cell >= 0) {
    app.pressed_area = 2;
    app.pressed_item = cell;
    app.source = static_cast<Source>(cell);
    if (app.source == Source::Window) {
      if (app.selected_window_idx < 0 && !app.window_list.empty()) {
        app.selected_window_idx = 0;
      }
      app.status = "Select a window";
    } else if (app.source == Source::Screen) {
      app.wl.refresh_logical_outputs();
      wl_display_roundtrip(app.wl.display());
      app.output_list = list_outputs(app.wl);
      app.capture_all_screens = true;
      app.selected_output_idx = -1;
      app.status = "All Screens (click to pick)";
    } else {
      trigger_capture(app);
    }
    app.pendingRedraw = true;
    return;
  }

  if (app.source == Source::Window) {
    int wi = hit_window_list(app, l, x, y);
    if (wi >= 0) {
      app.pressed_area = 1;
      app.pressed_item = wi;
      app.selected_window_idx = wi;
      app.pendingRedraw = true;
      auto* wlr_handle = static_cast<zwlr_foreign_toplevel_handle_v1*>(app.window_list[wi].wlr_handle);
      if (wlr_handle) {
        SC_LOG("handle_click: activating wlr toplevel handle=%p seat=%p", (void*)wlr_handle, (void*)app.wl.seat());
        zwlr_foreign_toplevel_handle_v1_activate(wlr_handle, app.wl.seat());
        wl_display_flush(app.wl.display());
        wl_display_roundtrip(app.wl.display());
      }
      trigger_capture(app);
      return;
    }
  }

  if (app.source == Source::Screen) {
    int vi = hit_output_list(app, l, x, y);
    if (vi == 0) {
      app.pressed_area = 3;
      app.pressed_item = 0;
      app.capture_all_screens = true;
      app.selected_output_idx = -1;
      app.pendingRedraw = true;
      trigger_capture(app);
      return;
    }
    if (vi > 0) {
      int oi = vi - 1;
      if (oi < static_cast<int>(app.output_list.size())) {
        app.pressed_area = 3;
        app.pressed_item = vi;
        app.selected_output_idx = oi;
        app.capture_all_screens = false;
        app.pendingRedraw = true;
        trigger_capture(app);
        return;
      }
    }
  }

  int exp = hit_export_btn(l, x, y);
  if (exp == 0) {
    app.pressed_area = 4;
    app.pressed_item = 0;
    do_export_png(app);
    return;
  }
  if (exp == 1) {
    app.pressed_area = 4;
    app.pressed_item = 1;
    do_copy_png(app);
    return;
  }
  if (exp == 2) {
    app.pressed_area = 4;
    app.pressed_item = 2;
    do_save_as(app);
    return;
  }

  if (x >= l.preview_x && x < l.preview_x + l.preview_w &&
      y >= l.preview_y && y < l.preview_y + l.preview_h &&
      app.captured.valid) {
    app.dragging = true;
    app.drag_start_x = static_cast<double>(x);
    app.drag_start_y = static_cast<double>(y);
    app.drag_pan_x = app.pan_x;
    app.drag_pan_y = app.pan_y;
  }
}

void handle_release(AppState& app)
{
  SC_LOG("handle_release: enter");
  app.dragging = false;
  app.pressed_item = -1;
  app.pressed_area = 0;
  app.pendingRedraw = true;
}

void handle_motion(AppState& app, int x, int y)
{
  SC_LOG("handle_motion: enter x=%d y=%d", x, y);
  auto l = compute_layout(app.width, app.height);

  int old_item = app.hovered_item;
  int old_area = app.hovered_area;

  app.hovered_item = -1;
  app.hovered_area = 0;

  int exp = hit_export_btn(l, x, y);
  if (exp >= 0) {
    app.hovered_item = exp;
    app.hovered_area = 4;
  }

  if (app.source == Source::Screen && app.hovered_area == 0) {
    int oi = hit_output_list(app, l, x, y);
    if (oi >= 0) {
      app.hovered_item = oi;
      app.hovered_area = 3;
    }
  }

  if (app.source == Source::Window && app.hovered_area == 0) {
    int wi = hit_window_list(app, l, x, y);
    if (wi >= 0) {
      app.hovered_item = wi;
      app.hovered_area = 1;
    }
  }

  if (app.hovered_area == 0) {
    int cell = hit_grid_cell(l, x, y);
    if (cell >= 0) {
      app.hovered_item = cell;
      app.hovered_area = 2;
    }
  }

  if (app.hovered_item != old_item || app.hovered_area != old_area) {
    app.pendingRedraw = true;
  }

  if (app.dragging) {
    app.pan_x = app.drag_pan_x + (static_cast<double>(x) - app.drag_start_x);
    app.pan_y = app.drag_pan_y + (static_cast<double>(y) - app.drag_start_y);
    app.pendingRedraw = true;
  }
}

void handle_scroll(AppState& app, int mx, int my, double dy)
{
  SC_LOG("handle_scroll: enter mx=%d my=%d dy=%.1f", mx, my, dy);
  if (!app.captured.valid || app.captured.width < 1 || app.captured.height < 1) return;

  int pad = 20;
  int sidebar_w = 320;
  int avail_w = (app.width - sidebar_w) - pad * 2;
  int avail_h = app.height - pad * 2;
  double fit_scale = (std::min)(
      static_cast<double>(avail_w) / app.captured.width,
      static_cast<double>(avail_h) / app.captured.height);

  double old_zoom = app.zoom;
  if (dy > 0) {
    app.zoom *= 1.15;
  } else if (dy < 0) {
    app.zoom /= 1.15;
  }
  app.zoom = (std::max)(0.05, (std::min)(app.zoom, 20.0));

  double ratio = (fit_scale * app.zoom) / (fit_scale * old_zoom);

  int prev_cx = (app.width - sidebar_w) / 2;
  int prev_cy = app.height / 2;

  double old_scale = fit_scale * old_zoom;
  int old_img_w = static_cast<int>(app.captured.width * old_scale);
  int old_img_h = static_cast<int>(app.captured.height * old_scale);
  double old_img_x = prev_cx - old_img_w / 2.0 + app.pan_x;
  double old_img_y = prev_cy - old_img_h / 2.0 + app.pan_y;

  double new_img_x = mx - (mx - old_img_x) * ratio;
  double new_img_y = my - (my - old_img_y) * ratio;

  double new_scale = fit_scale * app.zoom;
  int new_img_w = static_cast<int>(app.captured.width * new_scale);

  app.pan_x = new_img_x - prev_cx + new_img_w / 2.0;
  app.pan_y = new_img_y - prev_cy + static_cast<int>(app.captured.height * new_scale) / 2.0;

  app.pendingRedraw = true;
}

}
