#include "../app.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <string>

namespace eh::file_browser {

void draw_rename_ui(AppState& app, cairo_t* cr) {
  int w = app.width;
  int h = app.height;
  int dlg_w = 400;
  int dlg_h = 190;
  int dlg_x = (w - dlg_w) / 2;
  int dlg_y = (h - dlg_h) / 2;
  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b, 1.0);
  draw_rounded_rect(cr, dlg_x, dlg_y, dlg_w, dlg_h, 12);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b, 0.2);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, dlg_x + 0.5, dlg_y + 0.5, dlg_w - 1, dlg_h - 1, 11.5);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
  cairo_move_to(cr, dlg_x + 24, dlg_y + 32);
  cairo_show_text(cr, "Rename");

  auto slash = app.rename_ui_entry_path.rfind('/');
  std::string dir_str = (slash != std::string::npos)
    ? app.rename_ui_entry_path.substr(0, slash)
    : "";
  if (dir_str.size() > 50)
    dir_str = "..." + dir_str.substr(dir_str.size() - 47);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.45);
  cairo_move_to(cr, dlg_x + 24, dlg_y + 50);
  cairo_show_text(cr, dir_str.c_str());

  int input_x = dlg_x + 24;
  int input_y = dlg_y + 64;
  int input_w = dlg_w - 48;
  int input_h = 36;
  cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, 0.5);
  draw_rounded_rect(cr, input_x, input_y, input_w, input_h, 8);
  cairo_fill(cr);

  cairo_set_font_size(cr, 14);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
  cairo_move_to(cr, input_x + 12, input_y + input_h / 2 + 5);
  cairo_show_text(cr, app.rename_ui_buf.c_str());

  if (app.rename_ui_buf.empty()) {
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.35);
    cairo_move_to(cr, input_x + 12, input_y + input_h / 2 + 5);
    cairo_show_text(cr, "New name");
  } else {
    cairo_text_extents_t te;
    cairo_text_extents(cr, app.rename_ui_buf.substr(0, app.rename_ui_cursor_pos).c_str(), &te);
    int cx = input_x + 12 + static_cast<int>(te.width);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.65);
    cairo_rectangle(cr, cx, input_y + 8, 1, input_h - 16);
    cairo_fill(cr);
  }

  int btn_y = dlg_y + dlg_h - 52;
  int btn_w = 90;
  int btn_h = 34;
  int cancel_x = dlg_x + dlg_w - 230;
  int rename_x = dlg_x + dlg_w - 120;

  cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b, 0.55);
  draw_rounded_rect(cr, cancel_x, btn_y, btn_w, btn_h, 8);
  cairo_fill(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.9);
  cairo_text_extents_t te;
  cairo_text_extents(cr, "Cancel", &te);
  cairo_move_to(cr, cancel_x + (btn_w - te.x_advance) / 2, btn_y + btn_h / 2 + te.height * 0.35);
  cairo_show_text(cr, "Cancel");

  cairo_set_source_rgba(cr, app.accent_r, app.accent_g, app.accent_b, 0.9);
  draw_rounded_rect(cr, rename_x, btn_y, btn_w, btn_h, 8);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
  cairo_text_extents(cr, "Rename", &te);
  cairo_move_to(cr, rename_x + (btn_w - te.x_advance) / 2, btn_y + btn_h / 2 + te.height * 0.35);
  cairo_show_text(cr, "Rename");
}

void draw_batch_rename(AppState& app, cairo_t* cr) {
  int w = app.width;
  int h = app.height;
  int n = static_cast<int>(app.batch_rename_entries.size());

  // ── Compute preview names ──
  bool is_template = (app.batch_rename_mode == 0);
  if (is_template) {
    int counter = 1;
    for (auto& e : app.batch_rename_entries) {
      std::string base = e.old_name;
      if (!e.ext.empty() && e.old_name.size() > e.ext.size())
        base = e.old_name.substr(0, e.old_name.size() - e.ext.size());
      std::string result = app.batch_rename_template;
      // Replace [Original filename] with base
      for (auto p = result.find("[Original filename]"); p != std::string::npos;
           p = result.find("[Original filename]", p + base.size()))
        result.replace(p, 19, base);
      // Replace [1] with counter (1-digit)
      std::string c1 = std::to_string(counter);
      for (auto p = result.find("[1]"); p != std::string::npos;
           p = result.find("[1]", p + c1.size()))
        result.replace(p, 3, c1);
      // Replace [01] with 2-digit counter
      char buf2[16];
      std::snprintf(buf2, sizeof(buf2), "%02d", counter);
      std::string c2(buf2);
      for (auto p = result.find("[01]"); p != std::string::npos;
           p = result.find("[01]", p + c2.size()))
        result.replace(p, 4, c2);
      // Replace [001] with 3-digit counter
      char buf3[16];
      std::snprintf(buf3, sizeof(buf3), "%03d", counter);
      std::string c3(buf3);
      for (auto p = result.find("[001]"); p != std::string::npos;
           p = result.find("[001]", p + c3.size()))
        result.replace(p, 5, c3);
      e.new_name = result + e.ext;
      ++counter;
    }
  } else {
    for (auto& e : app.batch_rename_entries) {
      std::string n = e.old_name;
      if (!app.batch_rename_find.empty()) {
        std::size_t pos = 0;
        while ((pos = n.find(app.batch_rename_find, pos)) != std::string::npos) {
          n.replace(pos, app.batch_rename_find.size(), app.batch_rename_replace);
          pos += app.batch_rename_replace.size();
        }
      }
      e.new_name = n;
    }
  }

  // ── Layout ──
  int dlg_w = 540;
  int list_h = std::min(n * 28 + 4, 280) + 4;
  int input_area_h = is_template ? 70 : 80;
  int dlg_h = 24 + 28 + input_area_h + list_h + 56;
  int dlg_x = (w - dlg_w) / 2;
  int dlg_y = (h - dlg_h) / 2;
  double sa = app.surface_opacity_pct / 100.0;

  // Backdrop
  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  // Card bg
  cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b, 1.0);
  draw_rounded_rect(cr, dlg_x, dlg_y, dlg_w, dlg_h, 12);
  cairo_fill(cr);

  // Card outline
  cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b, 0.2);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, dlg_x + 0.5, dlg_y + 0.5, dlg_w - 1, dlg_h - 1, 11.5);
  cairo_stroke(cr);

  int cx = dlg_x + 20;
  int cy = dlg_y + 14;

  // ── Title ──
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
  cairo_move_to(cr, cx, cy + 14);
  char title[64];
  std::snprintf(title, sizeof(title), "Rename %d File%s", n, n == 1 ? "" : "s");
  cairo_show_text(cr, title);

  // ── Mode tabs ──
  int tab_y = cy + 28;
  int tab_h = 26;
  int tab_w = 210;
  cairo_set_font_size(cr, 12);

  auto draw_tab = [&](int tbx, bool active, const char* label) {
    if (active) {
      cairo_set_source_rgba(cr, app.accent_r, app.accent_g, app.accent_b, 0.2);
      draw_rounded_rect(cr, tbx, tab_y, tab_w, tab_h, 6);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
    } else {
      bool hovered = (tbx == cx && app.batch_rename_hover_mode == 0) ||
                     (tbx == cx + tab_w + 8 && app.batch_rename_hover_mode == 1);
      if (hovered) {
        cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.12);
        draw_rounded_rect(cr, tbx, tab_y, tab_w, tab_h, 6);
        cairo_fill(cr);
      }
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.5);
    }
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    cairo_move_to(cr, tbx + (tab_w - static_cast<int>(te.x_advance)) / 2,
                  tab_y + tab_h / 2 + static_cast<int>(te.height * 0.35));
    cairo_show_text(cr, label);
  };

  draw_tab(cx, app.batch_rename_mode == 0, "Rename using a template");
  draw_tab(cx + tab_w + 8, app.batch_rename_mode == 1, "Find and replace text");

  int input_y = tab_y + tab_h + 10;
  int field_w = is_template ? 360 : 240;
  int field_h = 30;

  if (is_template) {
    // ── Template mode: single text field + Add button ──
    int tf_x = cx;
    int tf_y = input_y;

    // Input bg
    cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, 0.5);
    draw_rounded_rect(cr, tf_x, tf_y, field_w, field_h, 6);
    cairo_fill(cr);

    // Input text
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
    cairo_move_to(cr, tf_x + 8, tf_y + field_h / 2 + 5);
    cairo_show_text(cr, app.batch_rename_template.c_str());

    // Cursor
    cairo_text_extents_t te;
    cairo_text_extents(cr, app.batch_rename_template.substr(0, app.batch_rename_template_cursor).c_str(), &te);
    int cur_x = tf_x + 8 + static_cast<int>(te.width);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.65);
    cairo_rectangle(cr, cur_x, tf_y + 5, 1, field_h - 10);
    cairo_fill(cr);

    // [+ Add] button
    int add_x = tf_x + field_w + 8;
    int add_btn_w = 70;
    bool add_hover = (app.batch_rename_hover_btn == 3);
    cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b,
                          add_hover ? 0.7 : 0.5);
    draw_rounded_rect(cr, add_x, tf_y, add_btn_w, field_h, 6);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.8);
    cairo_set_font_size(cr, 12);
    cairo_text_extents(cr, "+ Add", &te);
    cairo_move_to(cr, add_x + (add_btn_w - static_cast<int>(te.x_advance)) / 2,
                  tf_y + field_h / 2 + static_cast<int>(te.height * 0.35));
    cairo_show_text(cr, "+ Add");

    // Add dropdown list
    if (app.batch_rename_show_add) {
      int dd_x = add_x;
      int dd_y = tf_y + field_h + 2;
      int dd_w = add_btn_w;
      int dd_item_h = 26;
      static const char* add_options[] = {"1, 2, 3...", "01, 02, 03...", "001, 002, 003..."};
      int dd_h = 3 * dd_item_h + 4;

      cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b, sa);
      draw_rounded_rect(cr, dd_x, dd_y, dd_w, dd_h, 6);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b, 0.3);
      cairo_set_line_width(cr, 1);
      draw_rounded_rect(cr, dd_x + 0.5, dd_y + 0.5, dd_w - 1, dd_h - 1, 5.5);
      cairo_stroke(cr);

      cairo_set_font_size(cr, 11);
      for (int i = 0; i < 3; ++i) {
        int iy = dd_y + 2 + i * dd_item_h;
        if (app.batch_rename_add_hover == i) {
          cairo_set_source_rgba(cr, app.accent_r, app.accent_g, app.accent_b, 0.2);
          cairo_rectangle(cr, dd_x + 2, iy, dd_w - 4, dd_item_h);
          cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.85);
        cairo_move_to(cr, dd_x + 8, iy + dd_item_h / 2 + 5);
        cairo_show_text(cr, add_options[i]);
      }
    }
  } else {
    // ── Find & Replace mode ──
    int label_w = 100;
    int fld_x = cx + label_w;

    // Existing text label
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.6);
    cairo_move_to(cr, cx, input_y + field_h / 2 + 5);
    cairo_show_text(cr, "Existing text:");

    // Find field bg
    cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, 0.5);
    draw_rounded_rect(cr, fld_x, input_y, field_w, field_h, 6);
    cairo_fill(cr);

    // Find field text
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
    cairo_move_to(cr, fld_x + 8, input_y + field_h / 2 + 5);
    cairo_show_text(cr, app.batch_rename_find.c_str());

    // Find field cursor
    if (app.batch_rename_edit_focus == 0) {
      cairo_text_extents_t te;
      cairo_text_extents(cr, app.batch_rename_find.substr(0, app.batch_rename_find_cursor).c_str(), &te);
      int cx2 = fld_x + 8 + static_cast<int>(te.width);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.65);
      cairo_rectangle(cr, cx2, input_y + 5, 1, field_h - 10);
      cairo_fill(cr);
    }

    // Replace with label + field
    int rl_y = input_y + field_h + 6;
    int rl_w = label_w;
    int rf_x = cx + rl_w;

    // Label
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.6);
    cairo_move_to(cr, cx, rl_y + field_h / 2 + 5);
    cairo_show_text(cr, "Replace with:");

    // Replace field bg
    cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, 0.5);
    draw_rounded_rect(cr, rf_x, rl_y, field_w, field_h, 6);
    cairo_fill(cr);

    // Replace field text
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
    cairo_move_to(cr, rf_x + 8, rl_y + field_h / 2 + 5);
    cairo_show_text(cr, app.batch_rename_replace.c_str());

    // Replace field cursor
    if (app.batch_rename_edit_focus == 1) {
      cairo_text_extents_t te;
      cairo_text_extents(cr, app.batch_rename_replace.substr(0, app.batch_rename_replace_cursor).c_str(), &te);
      int cx2 = rf_x + 8 + static_cast<int>(te.width);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.65);
      cairo_rectangle(cr, cx2, rl_y + 5, 1, field_h - 10);
      cairo_fill(cr);
    }
  }

  // ── Preview list ──
  int list_y = input_y + (is_template ? field_h + 10 : field_h * 2 + 16);
  int list_x = cx;
  int list_w = dlg_w - 40;

  // Column headers
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.45);
  cairo_move_to(cr, list_x + 8, list_y - 4);
  cairo_show_text(cr, "Current Name");
  cairo_text_extents_t te;
  cairo_text_extents(cr, "New Name", &te);
  cairo_move_to(cr, list_x + list_w - 8 - static_cast<int>(te.width), list_y - 4);
  cairo_show_text(cr, "New Name");

  // List bg
  cairo_set_source_rgba(cr, app.bg_r, app.bg_g, app.bg_b, 0.3);
  draw_rounded_rect(cr, list_x, list_y, list_w, list_h - 4, 6);
  cairo_fill(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  int row_y = list_y + 6;

  for (int i = 0; i < n; ++i) {
    const auto& e = app.batch_rename_entries[i];
    int mid_x = list_x + list_w / 2;

    // Old name (left side)
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.55);
    cairo_move_to(cr, list_x + 8, row_y + 14);
    std::string display = e.old_name;
    int max_chars = (list_w / 2 - 20) / 7;
    if (static_cast<int>(display.size()) > max_chars)
      display = display.substr(0, max_chars - 3) + "...";
    cairo_show_text(cr, display.c_str());

    // Arrow in the middle
    cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.2);
    cairo_move_to(cr, mid_x - 6, row_y + 14);
    cairo_show_text(cr, "\u2192");

    // New name (right side)
    bool changed = (e.new_name != e.old_name);
    cairo_set_source_rgba(cr, changed ? app.accent_r : app.text_r,
                          changed ? app.accent_g : app.text_g,
                          changed ? app.accent_b : app.text_b,
                          changed ? 0.9 : 0.45);
    std::string ndisplay = e.new_name;
    if (static_cast<int>(ndisplay.size()) > max_chars)
      ndisplay = ndisplay.substr(0, max_chars - 3) + "...";
    cairo_text_extents_t te2;
    cairo_text_extents(cr, ndisplay.c_str(), &te2);
    cairo_move_to(cr, list_x + list_w - 8 - static_cast<int>(te2.width), row_y + 14);
    cairo_show_text(cr, ndisplay.c_str());

    row_y += 26;
  }

  // ── Buttons ──
  int btn_y = dlg_y + dlg_h - 44;
  int btn_w = 90;
  int btn_h = 32;
  int cancel_x = dlg_x + dlg_w - 230;
  int rename_x = dlg_x + dlg_w - 120;

  // Cancel button
  double cancel_alpha = (app.batch_rename_hover_btn == 1) ? 0.75 : 0.55;
  cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b, cancel_alpha);
  draw_rounded_rect(cr, cancel_x, btn_y, btn_w, btn_h, 8);
  cairo_fill(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13);
  cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.9);
  cairo_text_extents(cr, "Cancel", &te);
  cairo_move_to(cr, cancel_x + (btn_w - static_cast<int>(te.x_advance)) / 2,
                btn_y + btn_h / 2 + static_cast<int>(te.height * 0.35));
  cairo_show_text(cr, "Cancel");

  // Rename button
  int n_changed = 0;
  for (const auto& e : app.batch_rename_entries)
    if (e.new_name != e.old_name) ++n_changed;
  bool can_rename = n_changed > 0;
  double rename_alpha = (app.batch_rename_hover_btn == 0) ? 1.0 : 0.9;
  cairo_set_source_rgba(cr, app.accent_r, app.accent_g, app.accent_b,
                        can_rename ? rename_alpha : 0.35);
  draw_rounded_rect(cr, rename_x, btn_y, btn_w, btn_h, 8);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, can_rename ? 1.0 : 0.5);
  cairo_text_extents(cr, "Rename", &te);
  cairo_move_to(cr, rename_x + (btn_w - static_cast<int>(te.x_advance)) / 2,
                btn_y + btn_h / 2 + static_cast<int>(te.height * 0.35));
  cairo_show_text(cr, "Rename");
}

} // namespace eh::file_browser
