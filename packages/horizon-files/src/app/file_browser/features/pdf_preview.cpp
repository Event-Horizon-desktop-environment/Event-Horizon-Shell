#include "app/file_browser/features/pdf_preview.hpp"

#ifdef EH_HAVE_POPPLER
#include <poppler.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace eh::file_browser {
namespace {

void pdf_log(const char* fmt, ...) {
  FILE* f = fopen("/tmp/horizon-files.log", "a");
  if (!f) return;
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm* t = localtime(&ts.tv_sec);
  fprintf(f, "%02d:%02d:%02d.%03ld [pdf] ",
          t->tm_hour, t->tm_min, t->tm_sec, ts.tv_nsec / 1000000);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fprintf(f, "\n");
  fclose(f);
}

} // anonymous namespace

bool is_pdf_extension(const std::string& path) {
  const char suf[] = ".pdf";
  if (path.size() < 4) return false;
  for (int i = 0; i < 4; ++i) {
    char a = path[path.size() - 4 + i];
    char b = suf[i];
    if ((a >= 'A' && a <= 'Z') ? (a - 'A' + 'a') != b
        : (a >= 'a' && a <= 'z') ? a != b
        : a != b) return false;
  }
  return true;
}

cairo_surface_t* load_pdf_thumbnail(const std::string& path, int max_px) {
  pdf_log("load_pdf_thumbnail called with path=%s max_px=%d", path.c_str(), max_px);

#ifdef EH_HAVE_POPPLER
  pdf_log("EH_HAVE_POPPLER is defined, attempting poppler load");

  GError* error = nullptr;
  gchar* uri_cstr = g_filename_to_uri(path.c_str(), nullptr, &error);
  if (!uri_cstr) {
    pdf_log("g_filename_to_uri FAILED: %s",
            error && error->message ? error->message : "no error message");
    if (error) g_error_free(error);
    return nullptr;
  }
  std::string uri(uri_cstr);
  g_free(uri_cstr);
  pdf_log("URI=%s", uri.c_str());

  PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, &error);
  if (!doc) {
    pdf_log("poppler_document_new_from_file FAILED: %s",
            error && error->message ? error->message : "no error message");
    if (error) g_error_free(error);
    return nullptr;
  }
  pdf_log("poppler_document_new_from_file OK");

  int n_pages = poppler_document_get_n_pages(doc);
  pdf_log("n_pages=%d", n_pages);
  if (n_pages < 1) {
    pdf_log("FAIL: no pages in document");
    g_object_unref(doc);
    return nullptr;
  }

  PopplerPage* page = poppler_document_get_page(doc, 0);
  if (!page) {
    pdf_log("FAIL: poppler_document_get_page returned null for page 0");
    g_object_unref(doc);
    return nullptr;
  }
  pdf_log("poppler_document_get_page OK");

  double pw, ph;
  poppler_page_get_size(page, &pw, &ph);
  pdf_log("page size=%fx%f", pw, ph);
  if (pw <= 0 || ph <= 0) {
    pdf_log("FAIL: page size invalid");
    g_object_unref(page);
    g_object_unref(doc);
    return nullptr;
  }

  double scale = 1.0;
  if (std::max(pw, ph) > static_cast<double>(max_px))
    scale = static_cast<double>(max_px) / std::max(pw, ph);
  pdf_log("render scale=%f", scale);

  int rw = std::max(1, static_cast<int>(std::lround(pw * scale)));
  int rh = std::max(1, static_cast<int>(std::lround(ph * scale)));
  pdf_log("render size=%dx%d", rw, rh);

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    pdf_log("FAIL: cairo_image_surface_create returned %s",
            surf ? cairo_status_to_string(cairo_surface_status(surf)) : "null");
    cairo_surface_destroy(surf);
    g_object_unref(page);
    g_object_unref(doc);
    return nullptr;
  }
  pdf_log("cairo surface created OK");

  cairo_t* cr = cairo_create(surf);
  cairo_scale(cr, scale, scale);
  poppler_page_render(page, cr);
  cairo_destroy(cr);
  cairo_surface_flush(surf);
  pdf_log("poppler_page_render done");

  g_object_unref(page);
  g_object_unref(doc);
  pdf_log("SUCCESS, returning surface");
  return surf;
#else
  pdf_log("FAIL: EH_HAVE_POPPLER NOT defined — poppler not compiled in");
  (void)path;
  (void)max_px;
  return nullptr;
#endif
}

} // namespace eh::file_browser
