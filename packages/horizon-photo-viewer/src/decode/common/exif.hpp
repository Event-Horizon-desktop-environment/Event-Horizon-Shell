#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace hpv {

struct ExifInfo {
    int orientation = 1;

    // Image
    std::string image_description;
    std::string software;
    std::string artist;
    std::string copyright;

    // Camera
    std::string make;
    std::string model;
    std::string lens;

    // Capture
    std::string date_time;
    std::string date_time_original;
    std::string focal_length;
    std::string aperture;
    std::string exposure;
    std::string exposure_bias;
    std::string iso;
    std::string flash;
    std::string metering_mode;
    std::string exposure_mode;
    std::string exposure_program;
    std::string white_balance;
    std::string color_space;
    std::string subject_distance;
    std::string digital_zoom;
    std::string scene_capture_type;
    std::string contrast;
    std::string saturation;
    std::string sharpness;

    // GPS
    std::string gps_latitude;
    std::string gps_longitude;
    std::string gps_altitude;

    // Resolution
    std::string x_resolution;
    std::string y_resolution;

    // ICC Profile
    std::string icc_description;
    std::string icc_copyright;
    std::string icc_model;

    // File info
    uint64_t file_size = 0;
    std::string file_modified;

    // Every other EXIF/IPTC/XMP key-value pair not covered above
    std::vector<std::pair<std::string, std::string>> all_metadata;
};

ExifInfo parse_exif(const uint8_t* data, size_t size);

}
