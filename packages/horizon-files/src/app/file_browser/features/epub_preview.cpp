#include "app/file_browser/features/epub_preview.hpp"

#ifdef EH_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stb/stb_image.h"

namespace eh::file_browser {

bool is_epub_extension(const std::string& path) {
  const char suf[] = ".epub";
  if (path.size() < 5) return false;
  for (int i = 0; i < 5; ++i) {
    char a = path[path.size() - 5 + i];
    char b = suf[i];
    if ((a >= 'A' && a <= 'Z') ? (a - 'A' + 'a') != b
        : (a >= 'a' && a <= 'z') ? a != b
        : a != b) return false;
  }
  return true;
}

static bool has_suffix_ic(const std::string& s, const char* suf) {
  size_t slen = s.size();
  size_t suflen = std::strlen(suf);
  if (slen < suflen) return false;
  for (size_t i = 0; i < suflen; ++i) {
    char ca = s[slen - suflen + i];
    char cb = suf[i];
    if ((ca >= 'A' && ca <= 'Z') ? (ca - 'A' + 'a') != cb
        : (ca >= 'a' && ca <= 'z') ? ca != cb
        : ca != cb) return false;
  }
  return true;
}

static bool is_image_ext(const std::string& name) {
  return has_suffix_ic(name, ".jpg") || has_suffix_ic(name, ".jpeg")
      || has_suffix_ic(name, ".png") || has_suffix_ic(name, ".gif")
      || has_suffix_ic(name, ".webp") || has_suffix_ic(name, ".bmp");
}

static cairo_surface_t* create_cairo_surface_from_rgba(
    const unsigned char* data, int w, int h) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    return nullptr;
  }

  int stride = cairo_image_surface_get_stride(surf);
  auto* dst = cairo_image_surface_get_data(surf);

  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * stride);
    const unsigned char* src = data + static_cast<size_t>(y * w * 4);
    for (int x = 0; x < w; ++x) {
      unsigned char r = src[0];
      unsigned char g = src[1];
      unsigned char b = src[2];
      unsigned char a = src[3];
      unsigned char pr = static_cast<unsigned char>((static_cast<unsigned>(r) * a + 127) / 255);
      unsigned char pg = static_cast<unsigned char>((static_cast<unsigned>(g) * a + 127) / 255);
      unsigned char pb = static_cast<unsigned char>((static_cast<unsigned>(b) * a + 127) / 255);
      row[x] = (static_cast<uint32_t>(a) << 24)
             | (static_cast<uint32_t>(pr) << 16)
             | (static_cast<uint32_t>(pg) << 8)
             | static_cast<uint32_t>(pb);
      src += 4;
    }
  }

  cairo_surface_mark_dirty_rectangle(surf, 0, 0, w, h);
  cairo_surface_flush(surf);
  return surf;
}

#ifdef EH_HAVE_LIBARCHIVE

static int extract_epub_cover(const std::string& path,
                               std::vector<unsigned char>& out_data) {
  struct archive* a = archive_read_new();
  archive_read_support_filter_none(a);
  archive_read_support_format_zip(a);

  if (archive_read_open_filename(a, path.c_str(), 10240) != ARCHIVE_OK) {
    archive_read_free(a);
    return -1;
  }

  struct archive_entry* entry;
  std::vector<unsigned char> best_data;
  int best_priority = 1000;

  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char* name_cstr = archive_entry_pathname(entry);
    if (!name_cstr) {
      archive_read_data_skip(a);
      continue;
    }
    std::string name(name_cstr);

    std::string lower = name;
    for (auto& ch : lower)
      if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';

    if (!is_image_ext(name)) {
      archive_read_data_skip(a);
      continue;
    }

    int priority = 999;
    if (lower.find("cover") != std::string::npos) {
      priority = 0; // highest — explicit cover
    } else if (lower.find("front") != std::string::npos
               || lower.find("thumbnail") != std::string::npos) {
      priority = 1;
    } else if (lower.find("image") != std::string::npos
               || lower.find("oebps") != std::string::npos) {
      priority = 2;
    } else {
      priority = 3;
    }

    if (priority >= best_priority) {
      archive_read_data_skip(a);
      continue;
    }

    la_int64_t size = archive_entry_size(entry);
    if (size <= 0) {
      archive_read_data_skip(a);
      continue;
    }

    std::vector<unsigned char> data(static_cast<size_t>(size));
    la_ssize_t total = 0;
    while (total < size) {
      la_ssize_t r = archive_read_data(a, data.data() + total,
                                        static_cast<size_t>(size - total));
      if (r < 0) break;
      if (r == 0) break;
      total += r;
    }

    if (total == size) {
      best_data = std::move(data);
      best_priority = priority;
    } else {
      archive_read_data_skip(a);
    }
  }

  archive_read_close(a);
  archive_read_free(a);

  if (best_data.empty()) return -1;
  out_data = std::move(best_data);
  return 0;
}

#endif

cairo_surface_t* load_epub_thumbnail(const std::string& path, int /*max_px*/) {
#ifdef EH_HAVE_LIBARCHIVE
  std::vector<unsigned char> cover_data;

  if (extract_epub_cover(path, cover_data) != 0)
    return nullptr;
  if (cover_data.empty())
    return nullptr;

  int w = 0, h = 0, channels = 0;
  unsigned char* img = stbi_load_from_memory(
      cover_data.data(), static_cast<int>(cover_data.size()), &w, &h, &channels, 4);
  if (!img) return nullptr;
  if (w <= 0 || h <= 0) {
    stbi_image_free(img);
    return nullptr;
  }

  cairo_surface_t* surf = create_cairo_surface_from_rgba(img, w, h);
  stbi_image_free(img);
  return surf;

#else
  (void)path;
  return nullptr;
#endif
}

} // namespace eh::file_browser
