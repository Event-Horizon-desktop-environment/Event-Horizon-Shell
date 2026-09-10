#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eh::color {

// A decoded JPEG. `rgb` is width×height×3 bytes in row-major order.
struct JpegImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgb;
};

// Decodes a baseline (SOF0/SOF1) JPEG straight from an in-memory buffer into
// 24-bit RGB.
//
// Handles YCbCr, RGB and grayscale sources with 4:2:0, 4:2:2, 4:4:4 or 4:1:1
// chroma subsampling and restart markers. The fixed-point IDCT and color math
// are the same as the reference decoder, so the pixels come out byte-identical
// to its output.
//
// Returns false on anything it can't handle (progressive SOF2, lossless, or
// malformed data), leaving `out` untouched.
bool decode_jpeg_to_rgb(const uint8_t* data, size_t len, JpegImage& out);

}  // namespace eh::color
