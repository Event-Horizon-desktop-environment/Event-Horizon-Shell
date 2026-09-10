/*
 * Copyright (c) 2023.
 *
 * This software is free software;
 *
 * You can redistribute it or modify it under terms of the MIT, Apache License or Zlib license
 */

// Bit-exact C++ port of zune-jpeg 0.5.12 (scalar path) decoding to RGB.
//
// This is a faithful translation of the Rust decoder used by the `image`
// crate (matugen's JPEG backend). The decode output is byte-identical to
// `zune_jpeg::JpegDecoder::decode()` with `DecoderOptions::default()`
// (out colorspace RGB, non-strict, max scans 100), which is what the image
// crate's `into_rgb8()` produces for YCbCr JPEGs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zune_jpeg {

inline constexpr int kMaxComponents = 4;
inline constexpr int kHuffLookahead = 9;
inline constexpr int kDctBlock = 64;

// JPEG markers (only those zune-jpeg distinguishes).
enum class Marker : uint8_t {
  kCom = 0xFE,
  kSof0 = 0xC0,
  kSof1 = 0xC1,
  kSof2 = 0xC2,
  kDht = 0xC4,
  kDac = 0xCC,
  kRst0 = 0xD0,
  kRst1 = 0xD1,
  kRst2 = 0xD2,
  kRst3 = 0xD3,
  kRst4 = 0xD4,
  kRst5 = 0xD5,
  kRst6 = 0xD6,
  kRst7 = 0xD7,
  kSoi = 0xD8,
  kEoi = 0xD9,
  kSos = 0xDA,
  kDqt = 0xDB,
  kDnl = 0xDC,
  kDri = 0xDD,
  kApp0 = 0xE0,
  kApp1 = 0xE1,
  kApp2 = 0xE2,
  kApp13 = 0xED,
  kApp14 = 0xEE,
  kUnknown = 0xFF,
};

inline Marker marker_from_u8(uint8_t n) {
  switch (n) {
    case 0xFE: return Marker::kCom;
    case 0xC0: return Marker::kSof0;
    case 0xC1: return Marker::kSof1;
    case 0xC2: return Marker::kSof2;
    case 0xC4: return Marker::kDht;
    case 0xCC: return Marker::kDac;
    case 0xD0: return Marker::kRst0;
    case 0xD1: return Marker::kRst1;
    case 0xD2: return Marker::kRst2;
    case 0xD3: return Marker::kRst3;
    case 0xD4: return Marker::kRst4;
    case 0xD5: return Marker::kRst5;
    case 0xD6: return Marker::kRst6;
    case 0xD7: return Marker::kRst7;
    case 0xD8: return Marker::kSoi;
    case 0xD9: return Marker::kEoi;
    case 0xDA: return Marker::kSos;
    case 0xDB: return Marker::kDqt;
    case 0xDC: return Marker::kDnl;
    case 0xDD: return Marker::kDri;
    case 0xE0: return Marker::kApp0;
    case 0xE1: return Marker::kApp1;
    case 0xE2: return Marker::kApp2;
    case 0xED: return Marker::kApp13;
    case 0xEE: return Marker::kApp14;
    default: return Marker::kUnknown;
  }
}

inline bool marker_is_app(Marker m) {
  return m == Marker::kApp0 || m == Marker::kApp1 || m == Marker::kApp2 ||
         m == Marker::kApp13 || m == Marker::kApp14;
}

inline bool marker_is_rst(Marker m) {
  const uint8_t v = static_cast<uint8_t>(m);
  return v >= 0xD0 && v <= 0xD7;
}

// Sub-sampling ratio assigned to a component during `set_upsampling`.
enum class SampleRatios : uint8_t { kNone, kV, kH, kHv, kGeneric };

inline int sample_ratio_count(SampleRatios sr, int generic_h, int generic_v) {
  switch (sr) {
    case SampleRatios::kHv: return 4;
    case SampleRatios::kV:
    case SampleRatios::kH: return 2;
    case SampleRatios::kGeneric: return generic_h * generic_v;
    default: return 1;
  }
}

// Input colorspaces that can survive header parsing.
enum class InputColorSpace : uint8_t {
  kYcc,
  kLuma,
  kCmyk,
  kYcck,
  kRgb,
  kMultiBand,
};

inline int input_colorspace_ncomp(InputColorSpace cs) {
  switch (cs) {
    case InputColorSpace::kLuma: return 1;
    case InputColorSpace::kCmyk:
    case InputColorSpace::kYcck: return 4;
    default: return 3;
  }
}

struct HuffmanTable {
  int32_t maxcode[18] = {};
  int32_t offset[18] = {};
  int32_t lookup[512] = {};
  bool has_ac_lookup = false;
  int16_t ac_lookup[512] = {};
  uint8_t values[256] = {};
};

struct Components {
  int component_id = 0;  // 0=Y, 1=Cb, 2=Cr, 3=Q
  int vertical_sample = 1;
  int horizontal_sample = 1;
  int dc_huff_table = 0;
  int ac_huff_table = 0;
  uint8_t quantization_table_number = 0;
  int32_t quantization_table[64] = {};
  int32_t dc_pred = 0;
  int width_stride = 1;
  uint8_t id = 0;
  bool needed = true;
  std::vector<int16_t> raw_coeff;
  std::vector<int16_t> upsample_dest;
  std::vector<int16_t> row_up;
  std::vector<int16_t> row;
  std::vector<int16_t> first_row_upsample_dest;
  int idct_pos = 0;
  int x = 0;
  int w2 = 0;
  int y = 0;
  SampleRatios sample_ratio = SampleRatios::kNone;
  int generic_h = 0;
  int generic_v = 0;
  int fix_an_annoying_bug = 1;
};

struct BitStream {
  uint64_t buffer = 0;
  uint64_t aligned_buffer = 0;
  uint8_t bits_left = 0;
  int marker = -1;  // -1 = none, otherwise static_cast<int>(Marker)
  int16_t successive_low_mask = 1;
  uint8_t spec_start = 0;
  uint8_t spec_end = 0;
  int32_t eob_run = 0;
  int overread_by = 0;
  bool seen_eoi = false;

  void reset() {
    bits_left = 0;
    marker = -1;
    buffer = 0;
    aligned_buffer = 0;
    eob_run = 0;
  }
};

class JpegDecoder {
 public:
  JpegDecoder() = default;
  ~JpegDecoder() = default;

  // Decode a JPEG buffer to RGB (3 bytes per pixel), matching the image
  // crate's `into_rgb8()` for YCbCr JPEGs. Returns false on failure.
  bool decode_to_rgb(const uint8_t* data, size_t len, std::vector<uint8_t>& out_rgb,
                     int& width, int& height);

  const std::string& error() const { return error_; }

 private:
  // Byte reader (zune-core ZCursor<&[u8]> std-path semantics).
  const uint8_t* data_ = nullptr;
  size_t len_ = 0;
  size_t pos_ = 0;

  std::string error_;
  bool failed_ = false;

  int width_ = 0;
  int height_ = 0;
  int input_components_ = 0;

  bool qt_present_[4] = {};
  int32_t qt_tables_[4][64] = {};
  bool dc_present_[4] = {};
  HuffmanTable dc_huffman_tables_[4];
  bool ac_present_[4] = {};
  HuffmanTable ac_huffman_tables_[4];

  std::vector<Components> components_;
  int h_max_ = 1;
  int v_max_ = 1;
  int mcu_width_ = 0;
  int mcu_height_ = 0;
  int mcu_x_ = 0;
  int mcu_y_ = 0;
  bool is_interleaved_ = false;
  InputColorSpace input_colorspace_ = InputColorSpace::kYcc;
  bool is_progressive_ = false;
  int spec_start_ = 0;
  int spec_end_ = 0;
  int succ_high_ = 0;
  int succ_low_ = 0;
  int num_scans_ = 0;
  bool scan_subsampled_ = false;
  int z_order_[4] = {0, 0, 0, 0};
  int restart_interval_ = 0;
  uint32_t todo_ = 0x7FFFFFFF;
  bool headers_decoded_ = false;
  bool seen_sof_ = false;
  bool is_mjpeg_ = false;
  int coeff_ = 1;

  SampleRatios sample_ratio_ = SampleRatios::kNone;
  int generic_h_ = 0;
  int generic_v_ = 0;

  BitStream bs_;
  std::vector<std::vector<int16_t>> progressive_mcus_;
  Marker inter_scan_marker_ = Marker::kUnknown;

  // Reader helpers.
  bool eof() const { return pos_ >= len_; }
  uint8_t read_u8() { return pos_ < len_ ? data_[pos_++] : 0; }
  bool read_u8_err(uint8_t& out);
  bool read_exact(uint8_t* buf, size_t n);
  uint16_t get_u16_be() {
    uint8_t b[2];
    if (!read_exact(b, 2)) return 0;
    return static_cast<uint16_t>((b[0] << 8) | b[1]);
  }
  bool get_u16_be_err(uint16_t& out);
  void skip(size_t n) { pos_ += n; }
  bool peek_eq(size_t offset, const char* s, size_t n);

  // Header parsing.
  bool read_headers();
  bool parse_marker_inner(Marker m);
  bool parse_start_of_frame();
  bool parse_dqt();
  bool parse_huffman();
  bool parse_sos();
  bool parse_app14();
  static bool build_huffman_table(HuffmanTable& table, const uint8_t codes[17],
                                  const uint8_t values[256], bool is_dc);

  // Bitstream.
  bool refill();
  bool decode_huff(int32_t& symbol, HuffmanTable& table);
  int32_t peek_bits(int bits) const {
    return static_cast<int32_t>(bs_.aligned_buffer >> (64 - bits));
  }
  void drop_bits(uint8_t n);
  int32_t get_bits(uint8_t n);
  uint8_t get_bit();
  static int32_t huff_extend(int32_t x, int32_t s);
  bool decode_dc(HuffmanTable& dc_table, int32_t& dc_prediction);
  bool discard_dc(HuffmanTable& dc_table);
  bool decode_mcu_block(HuffmanTable& dc_table, HuffmanTable& ac_table,
                        const int32_t* qt_table, int32_t block[64],
                        int32_t& dc_prediction);
  bool discard_mcu_block(HuffmanTable& dc_table, HuffmanTable& ac_table);

  // MCU flow.
  enum class Flow { kOk, kAnotherSos, kInterScanMarker, kTerminate };
  Flow inner_decode_mcu_width(bool progressive, bool sampled, int mcu_width,
                              int mcu_height, int32_t tmp[64]);
  Flow decode_mcu_width(bool progressive, int mcu_width, int mcu_height,
                        int32_t tmp[64]);
  Flow check_stream_marker_after_mcu_width();
  bool advance_to_next_sos(Marker first_marker);
  bool handle_rst();
  bool handle_rst_main();
  bool get_marker_from_stream(int& out);
  void set_upsampling();
  void reset_params();
  bool setup_component_params();
  bool check_tables();
  bool decode_mcu_ycbcr_baseline(uint8_t* pixels, size_t pixels_len);
  bool finish_baseline_decoding(uint8_t* pixels, size_t pixels_len);
  bool post_process(uint8_t* pixels, size_t pixels_len, int i, int mcu_height,
                    int width, int padded_width, size_t& pixels_written,
                    std::vector<int16_t>& upsampler_scratch);
  bool upsample_component(Components& comp, int mcu_height, int i,
                          std::vector<int16_t>& scratch, bool has_vertical_sample);
  bool color_convert(const int16_t* const samples[4], uint8_t* output, int width,
                     int padded_width);

  // Kernels.
  static void idct_int(int32_t in_vector[64], int16_t* out_vector, int stride);
  static void idct_int_1x1(int32_t in_vector[64], int16_t* out_vector, int stride);
  static void idct4x4(int32_t in_vector[64], int16_t* out_vector, int stride);
  static void ycbcr_to_rgb_inner_16_scalar(const int16_t y[16], const int16_t cb[16],
                                           const int16_t cr[16], uint8_t* output);
  static void upsample_horizontal(const int16_t* input, const int16_t* in_near,
                                  const int16_t* in_far, int16_t* scratch,
                                  int16_t* output, size_t input_len);
  static void upsample_vertical(const int16_t* input, const int16_t* in_near,
                                const int16_t* in_far, int16_t* scratch,
                                int16_t* output, size_t input_len);
  static void upsample_hv(const int16_t* input, const int16_t* in_near,
                          const int16_t* in_far, int16_t* scratch, int16_t* output,
                          size_t input_len);
  static void upsample_generic(const int16_t* input, int16_t* output,
                               size_t input_len, size_t output_len);
  static void copy_removing_padding(const int16_t* const samples[4],
                                    uint8_t* output, int width, int padded_width);
  static uint8_t blinn_8x8(uint8_t in_val, uint8_t y);

  bool fail(const std::string& msg) {
    error_ = msg;
    failed_ = true;
    return false;
  }
};

// Convenience wrapper: decode to RGB. Equivalent to `JpegDecoder::decode_to_rgb`.
bool decode(const uint8_t* data, size_t len, std::vector<uint8_t>& out_rgb,
            int& width, int& height);

}  // namespace zune_jpeg
