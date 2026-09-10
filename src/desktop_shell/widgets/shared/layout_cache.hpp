#pragma once
#include <pango/pangocairo.h>
#include <string>
#include <unordered_map>

namespace eh::widgets {

struct LayoutCache {
  struct Entry {
    PangoLayout* layout = nullptr;
    std::string fontDesc;
    int fontPx = 0;
  };
  std::unordered_map<std::string, Entry> map;
  int lastFontPx = 0;

  PangoLayout* get(cairo_t* cr, const char* font_desc, int font_px, const std::string& key) {
    auto it = map.find(key);
    if (it != map.end() && it->second.fontPx == font_px && it->second.fontDesc == font_desc) {
      pango_cairo_update_layout(it->second.layout, cr);
      return it->second.layout;
    }
    for (auto& [_, e] : map) {
      if (e.layout) g_object_unref(e.layout);
    }
    map.clear();
    Entry e;
    e.layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string(font_desc);
    pango_layout_set_font_description(e.layout, desc);
    pango_font_description_free(desc);
    e.fontDesc = font_desc;
    e.fontPx = font_px;
    map[key] = e;
    return e.layout;
  }

  void clear() {
    for (auto& [_, e] : map) {
      if (e.layout) g_object_unref(e.layout);
    }
    map.clear();
  }

  ~LayoutCache() { clear(); }
};

}
