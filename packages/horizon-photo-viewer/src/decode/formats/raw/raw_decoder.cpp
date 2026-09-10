#include "decode/formats/raw/decoder.hpp"
#include <libraw/libraw.h>
#include <jpeglib.h>
#include <cstring>
#include <iostream>

namespace hpv {

bool RawDecoder::can_decode(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    static const uint8_t magic[][6] = {
        {0x49, 0x49, 0x2A, 0x00},           // TIFF / CR2
        {0x4D, 0x4D, 0x00, 0x2A},           // TIFF (Motorola)
        {0x49, 0x49, 0x2A, 0x00, 0x10, 0x00}, // CR2
        {0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x10}, // CR2 (Motorola)
        {0x49, 0x49, 0x52, 0x08},           // DNG
        {0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08}, // ARW
        {'M', 'D', 'X', 0x00, 0x01},         // NEF
        {'R', 'E', 'C', 0x00, 0x00, 0x00},   // ORF
        {0x08, 0x00, 0x00, 0x00},           // RW2
        {'I', 'I', 'R', 'S', 0x08, 0x00, 0x00, 0x00}, // RW2 variant
        {'P', '6', ' ', ' ', 0x30, 0x31},    // PEF
    };
    for (auto& m : magic) {
        size_t len = sizeof(m);
        if (size >= len && std::memcmp(data, m, len) == 0)
            return true;
    }
    return false;
}

DecodeResult RawDecoder::decode(const uint8_t* data, size_t size,
                                 int /*target_width*/, int /*target_height*/) {
    DecodeResult result;
    result.format_name = "RAW";

    LibRaw raw;
    int rc = raw.open_buffer(const_cast<void*>(static_cast<const void*>(data)), size);
    if (rc != LIBRAW_SUCCESS) {
        std::cerr << "raw: open failed: " << libraw_strerror(rc) << "\n";
        return result;
    }

    rc = raw.unpack_thumb();
    if (rc != LIBRAW_SUCCESS) {
        std::cerr << "raw: no embedded thumbnail: " << libraw_strerror(rc) << "\n";
        raw.recycle();
        return result;
    }

    libraw_processed_image_t* thumb = raw.dcraw_make_mem_thumb(&rc);
    if (!thumb || rc != LIBRAW_SUCCESS) {
        std::cerr << "raw: thumb extraction failed\n";
        raw.recycle();
        return result;
    }

    result.width = thumb->width;
    result.height = thumb->height;

    if (thumb->type == LIBRAW_IMAGE_BITMAP) {
        size_t pixel_count = (size_t)thumb->width * thumb->height;
        result.pixels.resize(pixel_count * 4);
        uint8_t* dst = result.pixels.data();
        const uint8_t* src = thumb->data;
        if (thumb->colors == 3) {
            for (size_t i = 0; i < pixel_count; i++) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
                dst += 4;
                src += 3;
            }
        } else if (thumb->colors == 4) {
            std::memcpy(dst, src, pixel_count * 4);
        } else {
            std::memcpy(dst, src, pixel_count * thumb->colors);
            for (size_t i = pixel_count * thumb->colors; i < pixel_count * 4; i++) {
                dst[i] = 255;
            }
        }
    } else if (thumb->type == LIBRAW_IMAGE_JPEG) {
        size_t jpeg_size = thumb->data_size;
        const uint8_t* jpeg_data = thumb->data;
        jpeg_decompress_struct cinfo{};
        jpeg_error_mgr jerr{};
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
        jpeg_read_header(&cinfo, TRUE);
        result.width = cinfo.image_width;
        result.height = cinfo.image_height;
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
    }

    LibRaw::dcraw_clear_mem(thumb);
    raw.recycle();
    return result;
}

}
