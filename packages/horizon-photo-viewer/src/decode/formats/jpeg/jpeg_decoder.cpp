#include "decode/formats/jpeg/decoder.hpp"
#include <jpeglib.h>
#include <cstring>
#include <iostream>

namespace hpv {

bool JpegDecoder::can_decode(const uint8_t* data, size_t size) {
    if (size < 3) return false;
    return data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

static int compute_idct_denom(int img_w, int img_h, int target_w, int target_h) {
    if (target_w <= 0 || target_h <= 0) return 1;
    int max_dim = std::max(img_w, img_h);
    int target_max = std::max(target_w, target_h);
    if (max_dim > target_max * 8) return 8;
    if (max_dim > target_max * 4) return 4;
    if (max_dim > target_max * 2) return 2;
    return 1;
}

DecodeResult JpegDecoder::decode(const uint8_t* data, size_t size,
                                  int target_width, int target_height) {
    DecodeResult result;
    result.format_name = "JPEG";

    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr jerr{};
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, data, size);
    jpeg_read_header(&cinfo, TRUE);

    JOCTET* icc_raw = nullptr;
    unsigned int icc_len = 0;
    if (jpeg_read_icc_profile(&cinfo, &icc_raw, &icc_len) && icc_raw && icc_len > 0) {
        result.icc_profile.assign(icc_raw, icc_raw + icc_len);
        free(icc_raw);
    }

    // IDCT scaling: compute scale factor based on target vs source dimensions
    int denom = compute_idct_denom(cinfo.image_width, cinfo.image_height,
                                    target_width, target_height);
    if (denom > 1) {
        cinfo.scale_num = 1;
        cinfo.scale_denom = denom;
    }

    jpeg_calc_output_dimensions(&cinfo);

    result.width = cinfo.output_width;
    result.height = cinfo.output_height;

    jpeg_start_decompress(&cinfo);

    int row_stride = cinfo.output_width * cinfo.output_components;
    size_t pixel_count = (size_t)cinfo.output_width * cinfo.output_height;
    result.pixels.resize(pixel_count * 4);

    std::vector<uint8_t> row(row_stride);
    JSAMPROW row_ptr[1] = {row.data()};
    uint8_t* dst = result.pixels.data();

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, row_ptr, 1);

        if (cinfo.output_components == 3) {
            for (JDIMENSION x = 0; x < cinfo.output_width; x++) {
                dst[0] = row[x * 3 + 0];
                dst[1] = row[x * 3 + 1];
                dst[2] = row[x * 3 + 2];
                dst[3] = 255;
                dst += 4;
            }
        } else if (cinfo.output_components == 1) {
            for (JDIMENSION x = 0; x < cinfo.output_width; x++) {
                uint8_t v = row[x];
                dst[0] = v; dst[1] = v; dst[2] = v; dst[3] = 255;
                dst += 4;
            }
        } else {
            int comps = cinfo.output_components;
            for (JDIMENSION x = 0; x < cinfo.output_width; x++) {
                dst[0] = row[x * comps + 0];
                dst[1] = row[x * comps + 1];
                dst[2] = row[x * comps + 2];
                dst[3] = comps > 3 ? row[x * comps + 3] : 255;
                dst += 4;
            }
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return result;
}

}
