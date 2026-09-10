#include "screenshot/capture/drm_kms.hpp"
#include "screenshot/capture/capture.hpp"

#include "core/screenshot/logging.hpp"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <algorithm>

namespace hpv::sc {

static float pq_to_linear_10000(float pq_val) {
  if (pq_val <= 0.0f) return 0.0f;
  float m1 = 2610.0f / 16384.0f;
  float m2 = 2523.0f / 4096.0f * 128.0f / 256.0f;
  float c1 = 3424.0f / 4096.0f;
  float c2 = 2413.0f / 4096.0f * 32.0f / 256.0f;
  float c3 = 2392.0f / 4096.0f * 32.0f / 256.0f;
  float Lp = powf(pq_val, 1.0f / m2);
  float num = (std::max)(Lp - c1, 0.0f);
  float den = (std::max)(c2 - c3 * Lp, 0.001f);
  float linear_abs = powf(num / den, 1.0f / m1);
  return linear_abs;
}

static int parse_connector_name(const char* name, uint32_t& type, uint32_t& id) {
  if (strncmp(name, "DP-", 3) == 0) {
    type = DRM_MODE_CONNECTOR_DisplayPort;
    id = static_cast<uint32_t>(std::atoi(name + 3));
  } else if (strncmp(name, "HDMI-A-", 7) == 0) {
    type = DRM_MODE_CONNECTOR_HDMIA;
    id = static_cast<uint32_t>(std::atoi(name + 7));
  } else if (strncmp(name, "eDP-", 4) == 0) {
    type = DRM_MODE_CONNECTOR_eDP;
    id = static_cast<uint32_t>(std::atoi(name + 4));
  } else if (strncmp(name, "HDMI-", 5) == 0) {
    type = DRM_MODE_CONNECTOR_HDMIA;
    id = static_cast<uint32_t>(std::atoi(name + 5));
  } else if (strncmp(name, "VGA-", 4) == 0) {
    type = DRM_MODE_CONNECTOR_VGA;
    id = static_cast<uint32_t>(std::atoi(name + 4));
  } else if (strncmp(name, "DVI-", 4) == 0) {
    type = DRM_MODE_CONNECTOR_DVII;
    id = static_cast<uint32_t>(std::atoi(name + 4));
  } else {
    return -1;
  }
  return 0;
}

static bool read_fb_xbgr2101010(const void* map, uint32_t pitch,
                                 int w, int h,
                                 std::vector<float>& out_rgb) {
  out_rgb.resize(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; y++) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(
        static_cast<const char*>(map) + static_cast<size_t>(y) * pitch);
    for (int x = 0; x < w; x++) {
      uint32_t p = row[x];
      float pq_r = static_cast<float>((p >> 20) & 0x3FF) / 1023.0f;
      float pq_g = static_cast<float>((p >> 10) & 0x3FF) / 1023.0f;
      float pq_b = static_cast<float>((p >> 0) & 0x3FF) / 1023.0f;
      size_t idx = (static_cast<size_t>(y) * w + x) * 3;
      out_rgb[idx + 0] = pq_to_linear_10000(pq_r);
      out_rgb[idx + 1] = pq_to_linear_10000(pq_g);
      out_rgb[idx + 2] = pq_to_linear_10000(pq_b);
    }
  }
  return true;
}

static bool read_fb_argb8888(const void* map, uint32_t pitch,
                              int w, int h,
                              std::vector<float>& out_rgb) {
  out_rgb.resize(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; y++) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(
        static_cast<const char*>(map) + static_cast<size_t>(y) * pitch);
    for (int x = 0; x < w; x++) {
      uint32_t p = row[x];
      size_t idx = (static_cast<size_t>(y) * w + x) * 3;
      out_rgb[idx + 0] = static_cast<float>((p >> 16) & 0xFF) / 255.0f;
      out_rgb[idx + 1] = static_cast<float>((p >> 8) & 0xFF) / 255.0f;
      out_rgb[idx + 2] = static_cast<float>(p & 0xFF) / 255.0f;
    }
  }
  return true;
}

bool capture_output_drm(const char* drm_path, const char* connector_name,
                         HdrData& out_hdr) {
  SC_LOG("capture_output_drm: enter drm_path=%s connector=%s", drm_path, connector_name);
  int fd = ::open(drm_path, O_RDWR);
  if (fd < 0) { SC_LOG("capture_output_drm: open failed"); return false; }

  uint32_t conn_type = 0, conn_type_id = 0;
  if (parse_connector_name(connector_name, conn_type, conn_type_id) < 0) {
    SC_LOG("capture_output_drm: parse_connector_name failed for '%s'", connector_name);
    ::close(fd);
    return false;
  }

  drmModeRes* res = drmModeGetResources(fd);
  if (!res) { SC_LOG("capture_output_drm: drmModeGetResources failed"); ::close(fd); return false; }

  uint32_t fb_id = 0;
  int w = 0, h = 0;
  bool found = false;

  for (int i = 0; i < res->count_connectors; i++) {
    drmModeConnector* conn = drmModeGetConnectorCurrent(fd, res->connectors[i]);
    if (!conn) continue;

    if (conn->connector_type == conn_type &&
        conn->connector_type_id == conn_type_id &&
        conn->connection == DRM_MODE_CONNECTED &&
        conn->count_modes > 0) {

      if (conn->encoder_id) {
        drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc && enc->crtc_id) {
          drmModeCrtc* crtc = drmModeGetCrtc(fd, enc->crtc_id);
          if (crtc) {
            fb_id = crtc->buffer_id;
            w = crtc->width;
            h = crtc->height;
            drmModeFreeCrtc(crtc);
          }
        }
        if (enc) drmModeFreeEncoder(enc);
      }

      drmModeFreeConnector(conn);
      if (fb_id) { found = true; break; }
    } else {
      drmModeFreeConnector(conn);
    }
  }

  drmModeFreeResources(res);
  if (!found) { SC_LOG("capture_output_drm: no active framebuffer found"); ::close(fd); return false; }

  drmModeFB2* fb = drmModeGetFB2(fd, fb_id);
  if (!fb) { SC_LOG("capture_output_drm: drmModeGetFB2 failed"); ::close(fd); return false; }

  int dmabuf_fd = -1;
  if (drmPrimeHandleToFD(fd, fb->handles[0], DRM_CLOEXEC, &dmabuf_fd) < 0) {
    SC_LOG("capture_output_drm: drmPrimeHandleToFD failed errno=%d", errno);
    drmModeFreeFB2(fb);
    ::close(fd);
    return false;
  }

  size_t map_size = static_cast<size_t>(fb->height) * fb->pitches[0];
  void* map = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
  ::close(dmabuf_fd);

  if (map == MAP_FAILED) {
    SC_LOG("capture_output_drm: mmap failed errno=%d", errno);
    drmModeFreeFB2(fb);
    ::close(fd);
    return false;
  }

  std::vector<float> rgb;
  bool ok = false;

  if (fb->pixel_format == DRM_FORMAT_XBGR2101010 ||
      fb->pixel_format == DRM_FORMAT_ABGR2101010) {
    ok = read_fb_xbgr2101010(map, fb->pitches[0], w, h, rgb);
  } else {
    ok = read_fb_argb8888(map, fb->pitches[0], w, h, rgb);
  }

  munmap(map, map_size);
  drmModeFreeFB2(fb);
  ::close(fd);

  if (!ok) { SC_LOG("capture_output_drm: read_fb failed"); return false; }

  out_hdr.linear_rgb = std::move(rgb);
  out_hdr.width = w;
  out_hdr.height = h;
  out_hdr.valid = true;
  out_hdr.max_lum = 10000;

  SC_LOG("capture_output_drm: success w=%d h=%d", w, h);
  return true;
}

std::string find_drm_card_for_output(const char* connector_name) {
  SC_LOG("find_drm_card_for_output: enter connector=%s", connector_name);
  uint32_t conn_type = 0, conn_type_id = 0;
  if (parse_connector_name(connector_name, conn_type, conn_type_id) < 0)
    return {};

  for (int card = 0; card < 8; card++) {
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/dri/card%d", card);

    int fd = ::open(path, O_RDWR);
    if (fd < 0) continue;

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) { ::close(fd); continue; }

    bool found = false;
    for (int i = 0; i < res->count_connectors; i++) {
      drmModeConnector* conn = drmModeGetConnectorCurrent(fd, res->connectors[i]);
      if (!conn) continue;

      if (conn->connector_type == conn_type &&
          conn->connector_type_id == conn_type_id &&
          conn->connection == DRM_MODE_CONNECTED) {
        found = true;
        drmModeFreeConnector(conn);
        break;
      }
      drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    ::close(fd);

    if (found) { SC_LOG("find_drm_card_for_output: found %s", path); return std::string(path); }
  }

  SC_LOG("find_drm_card_for_output: not found");
  return {};
}

}
