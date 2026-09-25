#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"

#include <cairo/cairo.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace eh::settings::default_apps {

constexpr int kNumCategories = 9;

struct DefaultAppsLayout {
  int header_x = 0;
  int header_y = 0;
  int header_w = 0;
  int header_h = 0;
  int apps_x = 0;
  int apps_y = 0;
  int apps_w = 0;
  int apps_h = 0;
  int first_row_y = 0;

  int row_h = 42;
  int dropdown_h = 32;
  int cat_icon_px = 24;
  int card_inset = 8;
  int inner_pad = 16;
  int top_pad = 8;
  int gap_cards = 24;

  // Visible category indices (in display order) after applying the search filter.
  std::array<int, kNumCategories> vis{};
  int n_vis = kNumCategories;

  double ui_scale = 1.0;
};

void compute_default_apps_layout(int content_x, int content_w, int content_top_y, int viewport_h,
                                 DefaultAppsLayout* out);

void default_apps_dropdown_rect(const DefaultAppsLayout& layout, int visual_row, int* dx, int* dy, int* dw, int* dh);

[[nodiscard]] int visual_index_of_category(const DefaultAppsLayout& layout, int category_index);

[[nodiscard]] const char* category_material_icon(int row_index);

void set_stored_desktop_id(eh::config::DefaultAppsSettings& d, int category_index, std::string desktop_basename);

void clear_stored_desktop_id(eh::config::DefaultAppsSettings& d, int category_index);

[[nodiscard]] const char* category_label(int index);

[[nodiscard]] const char* const* category_mime_list(int index);

[[nodiscard]] std::string query_effective_desktop_id(int category_index);

[[nodiscard]] std::string friendly_line_for_desktop_id(const std::string& desktop_id);

void hit_row_control(double lx, double ly, const DefaultAppsLayout& layout, int* out_row, int* out_control);

void filter_picker_entries_for_category(int category_index, std::vector<eh::app_drawer::DesktopEntry>* entries);

void filter_picker_entries_for_search(std::string_view needle_lower, std::vector<eh::app_drawer::DesktopEntry>* entries);

[[nodiscard]] bool desktop_entry_matches_search(const eh::app_drawer::DesktopEntry& e, std::string_view needle_lower);

}
