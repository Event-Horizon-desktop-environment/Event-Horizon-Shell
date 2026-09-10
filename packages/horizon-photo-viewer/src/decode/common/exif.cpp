#include "decode/common/exif.hpp"
#include <exiv2/exiv2.hpp>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace hpv {

static void parse_icc_profile(ExifInfo& info, const uint8_t* data, size_t size) {
    // Scan for ICC_PROFILE marker in JPEG APP2 segments
    for (size_t i = 0; i + 18 < size; i++) {
        if (data[i] != 0xFF || data[i + 1] != 0xE2) continue;
        uint16_t seg_len = (uint16_t)data[i + 2] << 8 | data[i + 3];
        if (seg_len < 14) continue;
        if (std::memcmp(data + i + 4, "ICC_PROFILE\x00", 13) != 0) continue;
        uint8_t seq_no = data[i + 17];
        if (seq_no != 1) continue; // parse header only from first segment
        size_t icc_off = i + 18;
        size_t icc_len = (size_t)(seg_len - 16);
        if (icc_off + icc_len > size) icc_len = size - icc_off;
        if (icc_len < 132) return; // too small for header + tag table

        const uint8_t* icc = data + icc_off;
        uint32_t tag_count = (uint32_t)icc[128] << 24 | (uint32_t)icc[129] << 16
                           | (uint32_t)icc[130] << 8 | icc[131];

        for (uint32_t t = 0; t < tag_count; t++) {
            size_t toff = 132 + (size_t)t * 12;
            if (toff + 12 > icc_len) break;
            char sig[5] = {};
            std::memcpy(sig, icc + toff, 4);
            uint32_t tag_off = (uint32_t)icc[toff + 4] << 24 | (uint32_t)icc[toff + 5] << 16
                             | (uint32_t)icc[toff + 6] << 8 | icc[toff + 7];
            uint32_t tag_sz = (uint32_t)icc[toff + 8] << 24 | (uint32_t)icc[toff + 9] << 16
                            | (uint32_t)icc[toff + 10] << 8 | icc[toff + 11];

            if (tag_off + tag_sz > icc_len) continue;

            auto read_icc_str = [&](size_t off, size_t max) -> std::string {
                if (off + 4 > icc_len) return {};
                uint32_t slen = (uint32_t)icc[off] << 24 | (uint32_t)icc[off + 1] << 16
                              | (uint32_t)icc[off + 2] << 8 | icc[off + 3];
                if (slen == 0 || slen > max - 4) return {};
                return std::string((const char*)icc + off + 4, slen);
            };

            if (std::strcmp(sig, "desc") == 0)
                info.icc_description = read_icc_str(tag_off, tag_sz);
            else if (std::strcmp(sig, "cprt") == 0)
                info.icc_copyright = read_icc_str(tag_off, tag_sz);
            else if (std::strcmp(sig, "dmnd") == 0 || std::strcmp(sig, "dmdd") == 0)
                info.icc_model = read_icc_str(tag_off, tag_sz);
        }
        break;
    }
}

ExifInfo parse_exif(const uint8_t* data, size_t size) {
    ExifInfo info;

    try {
        auto image = Exiv2::ImageFactory::open(
            const_cast<Exiv2::byte*>(data), size
        );
        image->readMetadata();

        auto get_str = [&](Exiv2::ExifData& ed, const char* key) -> std::string {
            Exiv2::ExifKey k(key);
            auto it = ed.findKey(k);
            if (it != ed.end()) return it->toString();
            return {};
        };

        Exiv2::ExifData& exif = image->exifData();
        if (!exif.empty()) {
            info.orientation     = exif["Exif.Image.Orientation"].toUint32();

            info.make            = get_str(exif, "Exif.Image.Make");
            info.model           = get_str(exif, "Exif.Image.Model");
            info.lens            = get_str(exif, "Exif.Photo.LensModel");
            info.focal_length    = get_str(exif, "Exif.Photo.FocalLength");
            info.aperture        = get_str(exif, "Exif.Photo.FNumber");
            info.exposure        = get_str(exif, "Exif.Photo.ExposureTime");
            info.exposure_bias   = get_str(exif, "Exif.Photo.ExposureBiasValue");
            info.iso             = get_str(exif, "Exif.Photo.ISOSpeedRatings");
            info.date_time       = get_str(exif, "Exif.Image.DateTime");
            info.date_time_original = get_str(exif, "Exif.Photo.DateTimeOriginal");
            info.flash           = get_str(exif, "Exif.Photo.Flash");
            info.metering_mode   = get_str(exif, "Exif.Photo.MeteringMode");
            info.exposure_mode   = get_str(exif, "Exif.Photo.ExposureMode");
            info.exposure_program= get_str(exif, "Exif.Photo.ExposureProgram");
            info.white_balance   = get_str(exif, "Exif.Photo.WhiteBalance");
            info.color_space     = get_str(exif, "Exif.Photo.ColorSpace");
            info.subject_distance= get_str(exif, "Exif.Photo.SubjectDistance");
            info.digital_zoom    = get_str(exif, "Exif.Photo.DigitalZoomRatio");
            info.scene_capture_type = get_str(exif, "Exif.Photo.SceneCaptureType");
            info.contrast        = get_str(exif, "Exif.Photo.Contrast");
            info.saturation      = get_str(exif, "Exif.Photo.Saturation");
            info.sharpness       = get_str(exif, "Exif.Photo.Sharpness");
            info.image_description = get_str(exif, "Exif.Image.ImageDescription");
            info.software        = get_str(exif, "Exif.Image.Software");
            info.artist          = get_str(exif, "Exif.Image.Artist");
            info.copyright       = get_str(exif, "Exif.Image.Copyright");
            info.gps_latitude    = get_str(exif, "Exif.GPSInfo.GPSLatitude");
            info.gps_longitude   = get_str(exif, "Exif.GPSInfo.GPSLongitude");
            info.gps_altitude    = get_str(exif, "Exif.GPSInfo.GPSAltitude");
            info.x_resolution    = get_str(exif, "Exif.Image.XResolution");
            info.y_resolution    = get_str(exif, "Exif.Image.YResolution");

            // Collect every EXIF key-value pair not in named fields
            const char* skip_keys[] = {
                "Exif.Image.Orientation",
                "Exif.Image.Make",
                "Exif.Image.Model",
                "Exif.Photo.LensModel",
                "Exif.Photo.FocalLength",
                "Exif.Photo.FNumber",
                "Exif.Photo.ExposureTime",
                "Exif.Photo.ExposureBiasValue",
                "Exif.Photo.ISOSpeedRatings",
                "Exif.Image.DateTime",
                "Exif.Photo.DateTimeOriginal",
                "Exif.Photo.Flash",
                "Exif.Photo.MeteringMode",
                "Exif.Photo.ExposureMode",
                "Exif.Photo.ExposureProgram",
                "Exif.Photo.WhiteBalance",
                "Exif.Photo.ColorSpace",
                "Exif.Photo.SubjectDistance",
                "Exif.Photo.DigitalZoomRatio",
                "Exif.Photo.SceneCaptureType",
                "Exif.Photo.Contrast",
                "Exif.Photo.Saturation",
                "Exif.Photo.Sharpness",
                "Exif.Image.ImageDescription",
                "Exif.Image.Software",
                "Exif.Image.Artist",
                "Exif.Image.Copyright",
                "Exif.GPSInfo.GPSLatitude",
                "Exif.GPSInfo.GPSLongitude",
                "Exif.GPSInfo.GPSAltitude",
                "Exif.Image.XResolution",
                "Exif.Image.YResolution",
            };

            for (auto& entry : exif) {
                std::string key = entry.key();
                std::string val = entry.toString();
                if (val.empty()) continue;
                bool skip = false;
                for (auto* sk : skip_keys) {
                    if (key == sk) { skip = true; break; }
                }
                if (!skip) {
                    info.all_metadata.emplace_back(key, val);
                }
            }
        }

        // IPTC data
        Exiv2::IptcData& iptc = image->iptcData();
        if (!iptc.empty()) {
            for (auto& entry : iptc) {
                std::string key = entry.key();
                std::string val = entry.toString();
                if (!val.empty())
                    info.all_metadata.emplace_back("IPTC:" + key, val);
            }
        }

        // XMP data
        Exiv2::XmpData& xmp = image->xmpData();
        if (!xmp.empty()) {
            for (auto& entry : xmp) {
                std::string key = entry.key();
                std::string val = entry.toString();
                if (!val.empty())
                    info.all_metadata.emplace_back("XMP:" + key, val);
            }
        }

        std::sort(info.all_metadata.begin(), info.all_metadata.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

    } catch (std::exception& e) {
        std::cerr << "exif: parse failed: " << e.what() << "\n";
    }

    // Parse ICC profile from raw JPEG data
    parse_icc_profile(info, data, size);

    return info;
}

}
