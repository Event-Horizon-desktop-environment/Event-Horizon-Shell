#include "ux/settings/utils/monitors/settings_monitors_drm_probe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef EH_HAVE_LIBDRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

namespace eh::settings_monitors {

#ifndef EH_HAVE_LIBDRM

void drm_probe_merge_caps_into(std::unordered_map<std::string, OutputCaps>*  ) {}

#else

namespace {

inline double drm_mode_refresh_hz(const drmModeModeInfo& m) {
     
    double pixclk_khz = static_cast<double>(m.clock);
    double htotal = static_cast<double>(m.htotal);
    double vtotal = static_cast<double>(m.vtotal);
    if (htotal <= 0.0 || vtotal <= 0.0)
        return 0.0;
    double hz = (pixclk_khz * 1000.0) / (htotal * vtotal);
    if (m.flags & DRM_MODE_FLAG_INTERLACE)
        hz *= 2.0;
    return hz;
}

inline std::string connector_to_name(uint32_t type, uint32_t type_id) {
    const char* prefix = nullptr;
    switch (type) {
        case DRM_MODE_CONNECTOR_VGA:
            prefix = "VGA";
            break;
        case DRM_MODE_CONNECTOR_DVII:
        case DRM_MODE_CONNECTOR_DVID:
        case DRM_MODE_CONNECTOR_DVIA:
            prefix = "DVI";
            break;
        case DRM_MODE_CONNECTOR_DisplayPort:
            prefix = "DP";
            break;
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:
            prefix = "HDMI-A";
            break;
        case DRM_MODE_CONNECTOR_LVDS:
        case DRM_MODE_CONNECTOR_eDP:
            prefix = "eDP";
            break;
        default:
            prefix = "Unknown";
            break;
    }
    return std::string(prefix) + "-" + std::to_string(type_id);
}

bool edid_hdr_hint_from_blob(const uint8_t* edid, size_t len) {
    if (!edid || len < 128)
        return false;
    const unsigned n_ext = edid[0x7e];
    const size_t n_blocks = 1u + static_cast<size_t>(n_ext);
    if (len < n_blocks * 128u)
        return false;
    for (size_t bi = 1; bi < n_blocks; ++bi) {
        const uint8_t* block = edid + 128 * bi;
        if (block[0] != 0x02)
            continue;
        const uint8_t dtd_start = block[2];
        size_t pos = 4;
        while (pos < static_cast<size_t>(dtd_start) && pos + 1 < 127u) {
            const uint8_t hdr_byte = block[pos];
            const unsigned tag = (hdr_byte >> 5) & 7u;
            const unsigned dlen = hdr_byte & 0x1fu;
            if (dlen == 0 || pos + 1 + dlen > 127u)
                break;
            if (tag == 7 && dlen >= 2) {
                const uint8_t ext_tag = block[pos + 1];
                if (ext_tag == 6 && dlen >= 3) {
                    const uint8_t eotf = block[pos + 2];
                    if ((eotf & 0x04u) != 0 || (eotf & 0x08u) != 0)
                        return true;
                }
            }
            pos += 1 + dlen;
        }
    }
    return false;
}

std::optional<std::pair<std::string, std::string>> edid_vendor_model_from_blob(const uint8_t* edid,
                                                                               size_t len) {
    if (!edid || len < 128)
        return std::nullopt;
    std::string vendor;
    vendor.resize(3);
    vendor[0] = static_cast<char>(edid[0x08]);
    vendor[1] = static_cast<char>(edid[0x09]);
    vendor[2] = static_cast<char>(edid[0x0a]);

    std::string model_name;
    for (unsigned slot = 0; slot < 4; ++slot) {
        const size_t off = 54 + slot * 18;
        if (off + 18 > 128)
            break;
        if (edid[off] == 0 && edid[off + 1] == 0 && edid[off + 2] == 0 && edid[off + 3] == 0xfc) {
            for (size_t i = 5; i < 18; ++i) {
                char ch = static_cast<char>(edid[off + i]);
                if (ch == '\n' || ch == '\r')
                    break;
                if (ch >= 32 && ch != 127)
                    model_name.push_back(ch);
            }
            break;
        }
    }
    while (!model_name.empty() && model_name.back() == ' ')
        model_name.pop_back();
    return std::make_pair(std::move(vendor), std::move(model_name));
}

void sort_caps_like_hypr_merge(OutputCaps* c) {
    if (!c)
        return;
    for (auto& pr : c->resolution_refresh_hz) {
        auto& v = pr.second;
        std::sort(v.begin(), v.end(), [](double a, double b) { return a > b; });
    }
    std::sort(c->all_refresh_hz.begin(), c->all_refresh_hz.end(),
              [](double a, double b) { return a > b; });
    std::sort(c->resolutions.begin(), c->resolutions.end(), [](const std::string& a, const std::string& b) {
        auto pa = a.find('x');
        auto pb = b.find('x');
        if (pa == std::string::npos || pb == std::string::npos)
            return a < b;
        long wa = std::atol(a.c_str()) * std::atol(a.c_str() + pa + 1);
        long wb = std::atol(b.c_str()) * std::atol(b.c_str() + pb + 1);
        return wa > wb;
    });
}

void merge_connector_caps(OutputCaps& out, const drmModeConnector* conn, int drm_fd) {
    OutputCaps c;

    drmModeObjectProperties* ob_props =
        drmModeObjectGetProperties(drm_fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR);
    std::vector<drmModePropertyRes*> owned_props;
    owned_props.reserve(ob_props ? ob_props->count_props : 0);

    auto release_props = [&]() {
        for (drmModePropertyRes* p : owned_props) {
            if (p)
                drmModeFreeProperty(p);
        }
        owned_props.clear();
        if (ob_props) {
            drmModeFreeObjectProperties(ob_props);
            ob_props = nullptr;
        }
    };

    if (ob_props) {
        for (uint32_t i = 0; i < ob_props->count_props; ++i) {
            drmModePropertyRes* prop = drmModeGetProperty(drm_fd, ob_props->props[i]);
            owned_props.push_back(prop);
        }

        auto find_blob = [&](const char* n) -> std::vector<uint8_t> {
            for (uint32_t i = 0; i < ob_props->count_props; ++i) {
                drmModePropertyRes* p = owned_props[i];
                if (!p || std::strcmp(p->name, n) != 0)
                    continue;
                if ((p->flags & DRM_MODE_PROP_BLOB) == 0)
                    continue;
                uint32_t bid = static_cast<uint32_t>(ob_props->prop_values[i]);
                if (bid == 0)
                    return {};
                drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(drm_fd, bid);
                if (!blob || !blob->data || blob->length == 0) {
                    if (blob)
                        drmModeFreePropertyBlob(blob);
                    return {};
                }
                std::vector<uint8_t> copy(blob->length);
                std::memcpy(copy.data(), blob->data, blob->length);
                drmModeFreePropertyBlob(blob);
                return copy;
            }
            return {};
        };

        std::vector<uint8_t> edid = find_blob("EDID");
        if (!edid.empty()) {
            if (edid_hdr_hint_from_blob(edid.data(), edid.size()))
                c.hdr_hint = true;
            if (auto vm = edid_vendor_model_from_blob(edid.data(), edid.size())) {
                c.make = std::move(vm->first);
                c.model = std::move(vm->second);
            }
        }

        for (uint32_t i = 0; i < ob_props->count_props; ++i) {
            drmModePropertyRes* p = owned_props[i];
            if (!p || std::strcmp(p->name, "vrr_capable") != 0)
                continue;
            c.vrr_capable = (ob_props->prop_values[i] != 0);
            break;
        }

        release_props();
    }

    if (conn->count_modes > 0 && conn->modes) {
        struct ModeAgg {
            uint32_t w = 0;
            uint32_t h = 0;
            std::vector<double> hz_list;
        };
        std::vector<ModeAgg> by_wh;
        auto find_agg = [&](uint32_t w, uint32_t h) -> ModeAgg* {
            for (auto& a : by_wh) {
                if (a.w == w && a.h == h)
                    return &a;
            }
            return nullptr;
        };

        for (int mi = 0; mi < conn->count_modes; ++mi) {
            const drmModeModeInfo& m = conn->modes[mi];
            uint32_t w = static_cast<uint32_t>(m.hdisplay);
            uint32_t h = static_cast<uint32_t>(m.vdisplay);
            double hz = drm_mode_refresh_hz(m);
            if (w == 0 || h == 0 || hz <= 0.0)
                continue;
            ModeAgg* a = find_agg(w, h);
            if (!a) {
                by_wh.push_back({});
                a = &by_wh.back();
                a->w = w;
                a->h = h;
            }
            if (std::find_if(a->hz_list.begin(), a->hz_list.end(),
                             [hz](double x) { return std::fabs(x - hz) < 0.02; }) == a->hz_list.end()) {
                a->hz_list.push_back(hz);
                if (std::find_if(c.all_refresh_hz.begin(), c.all_refresh_hz.end(),
                                 [hz](double x) { return std::fabs(x - hz) < 0.02; }) ==
                    c.all_refresh_hz.end())
                    c.all_refresh_hz.push_back(hz);
            }
        }

        for (auto& a : by_wh) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%ux%u", a.w, a.h);
            std::string rk(buf);
            if (std::find(c.resolutions.begin(), c.resolutions.end(), rk) == c.resolutions.end())
                c.resolutions.push_back(rk);
            c.resolution_refresh_hz[rk] = std::move(a.hz_list);
        }
    }

    sort_caps_like_hypr_merge(&c);
    out = std::move(c);
}

}

void drm_probe_merge_caps_into(std::unordered_map<std::string, OutputCaps>* caps) {
    if (!caps)
        return;

    DIR* dir = opendir("/dev/dri");
    if (!dir)
        return;
    std::vector<std::string> cards;
    while (struct dirent* ent = readdir(dir)) {
        std::string name(ent->d_name);
        if (name.size() > 5 && name.compare(0, 5, "card") == 0)
            cards.push_back("/dev/dri/" + name);
    }
    closedir(dir);
    std::sort(cards.begin(), cards.end());

    for (const std::string& path : cards) {
        int drm_fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (drm_fd < 0)
            continue;

        drmModeRes* res = drmModeGetResources(drm_fd);
        if (!res) {
            close(drm_fd);
            continue;
        }

        for (int i = 0; i < res->count_connectors; ++i) {
            drmModeConnector* conn = drmModeGetConnector(drm_fd, res->connectors[i]);
            if (!conn)
                continue;
            if (conn->connection != DRM_MODE_CONNECTED) {
                drmModeFreeConnector(conn);
                continue;
            }

            std::string key = connector_to_name(conn->connector_type, conn->connector_type_id);
            OutputCaps merged;
            merge_connector_caps(merged, conn, drm_fd);

            auto it = caps->find(key);
            if (it == caps->end()) {
                (*caps)[key] = std::move(merged);
            } else {
                if (!merged.resolutions.empty())
                    it->second = std::move(merged);
            }

            drmModeFreeConnector(conn);
        }

        drmModeFreeResources(res);
        close(drm_fd);
    }
}

#endif

}
