// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "motion_photo/agtm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

namespace {

// SMPTE ST 2094-50 bitstream constants and scaling factors (SMPTE ST 2094-50 §6).
constexpr float kDefaultHdrReferenceWhite = 203.0f;
constexpr float kHdrRefWhiteScale = 5.0f;
constexpr float kMaxRefWhiteScaled = 50000.0f;
constexpr float kHdrHeadroomScale = 10000.0f;
constexpr float kMaxHeadroomScaled = 60000.0f;
constexpr float kControlPointXScale = 1000.0f;
constexpr float kControlPointYScale = 10000.0f;
constexpr float kChromaticitiesScale = 50000.0f;
constexpr float kMaxChromaticitiesScaled = 50000.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kThetaScale = 36000.0f;
constexpr float kThetaOffset = kPi / 2.0f;
constexpr uint32_t kMaxAlternateImages = 4u;

class BitReader {
 public:
  BitReader(const uint8_t* data, size_t size)
      : data_(data), size_(size), bit_pos_(0) {}

  bool HasBits(size_t count) const {
    if (size_ > std::numeric_limits<size_t>::max() / 8) return false;
    size_t total_bits = size_ * 8;
    if (bit_pos_ > total_bits || count > total_bits - bit_pos_) {
      return false;
    }
    return true;
  }

  uint32_t ReadBit() {
    if (!HasBits(1)) return 0;
    size_t byte_idx = bit_pos_ / 8;
    size_t bit_idx = 7 - (bit_pos_ % 8);
    ++bit_pos_;
    return (data_[byte_idx] >> bit_idx) & 1;
  }

  uint32_t ReadBits(size_t count) {
    uint32_t val = 0;
    for (size_t i = 0; i < count; ++i) {
      val = (val << 1) | ReadBit();
    }
    return val;
  }

  void SkipBits(size_t count) {
    if (size_ > std::numeric_limits<size_t>::max() / 8) {
      bit_pos_ = std::numeric_limits<size_t>::max();
      return;
    }
    size_t total_bits = size_ * 8;
    if (bit_pos_ > total_bits || count > total_bits - bit_pos_) {
      bit_pos_ = total_bits;
    } else {
      bit_pos_ += count;
    }
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t bit_pos_;
};

}  // namespace

std::string AgtmToJson(const AgtmDynamicMetadata& meta) {
  std::string json = "{\n";
  json +=
      "  \"hdrReferenceWhite\": " + std::to_string(meta.hdr_reference_white) +
      ",\n";
  json += "  \"hasAdaptiveToneMap\": " +
          std::string(meta.has_adaptive_tone_map_flag ? "true" : "false") +
          ",\n";
  json += "  \"baselineHdrHeadroom\": " +
          std::to_string(meta.baseline_hdr_headroom_log2) + ",\n";
  json += "  \"useReferenceWhiteToneMapping\": " +
          std::string(meta.use_reference_white_tone_mapping_flag ? "true"
                                                                 : "false");
  if (!meta.rules.empty()) {
    json += ",\n  \"headroomAdaptiveToneMap\": {\n";
    json += "    \"alternateHdrHeadroom\": " +
            std::to_string(meta.rules[0].alternate_hdr_headroom_log2) + ",\n";
    json += "    \"curve\": [\n";
    for (size_t i = 0; i < meta.rules[0].curve.size(); ++i) {
      const auto& cp = meta.rules[0].curve[i];
      json += "      {\"x\": " + std::to_string(cp.x) +
              ", \"y\": " + std::to_string(cp.y) + "}";
      if (i + 1 < meta.rules[0].curve.size()) json += ",";
      json += "\n";
    }
    json += "    ]\n  }\n";
  } else {
    json += "\n";
  }
  json += "}";
  return json;
}

bool ParseAgtmPayload(const uint8_t* bytes, size_t num_bytes,
                      AgtmDynamicMetadata* out_metadata) {
  if (bytes == nullptr || num_bytes == 0 || out_metadata == nullptr) {
    return false;
  }

  // 1. Check if there is an ITU-T T.35 header (0xB5 0x00 0x90).
  const uint8_t* bitstream_data = bytes;
  size_t bitstream_size = num_bytes;
  bool found_t35_header = false;

  for (size_t i = 0; i <= (num_bytes >= 3 ? num_bytes - 3 : 0); ++i) {
    if (bytes[i] == 0xB5 && bytes[i + 1] == 0x00 && bytes[i + 2] == 0x90) {
      size_t offset = i + 3;
      // Optional provider-oriented code (e.g. 0x00 0x01 or 0x00 0x05)
      if (offset <= (num_bytes >= 2 ? num_bytes - 2 : 0) &&
          bytes[offset] == 0x00 &&
          (bytes[offset + 1] == 0x01 || bytes[offset + 1] == 0x05)) {
        offset += 2;
      }
      bitstream_data = bytes + offset;
      bitstream_size = num_bytes - offset;
      found_t35_header = true;
      break;
    }
  }

  // If no T.35 header was found, verify if it's a valid SMPTE ST 2094-50 bitstream.
  // In SMPTE ST 2094-50, the first byte contains:
  //   application_version (3 bits): must be 0
  //   minimum_application_version (3 bits): must be 0
  //   reserved (2 bits)
  if (!found_t35_header) {
    if (bitstream_size < 2) {
      return false;
    }
    // Top 6 bits must be 0 (app_version=0, min_app_version=0)
    if ((bitstream_data[0] & 0xFC) != 0) {
      return false;
    }
  }

  if (bitstream_size < 2) {
    if (found_t35_header) {
      out_metadata->hdr_reference_white = kDefaultHdrReferenceWhite;
      out_metadata->has_adaptive_tone_map_flag = false;
      out_metadata->baseline_hdr_headroom_log2 = 0.0f;
      out_metadata->use_reference_white_tone_mapping_flag = false;
      out_metadata->rules.clear();
      return true;
    }
    return false;
  }

  BitReader reader(bitstream_data, bitstream_size);
  if (!reader.HasBits(16)) {
    return false;
  }

  uint32_t app_version = reader.ReadBits(3);
  uint32_t min_app_version = reader.ReadBits(3);
  reader.SkipBits(2);  // reserved
  if (min_app_version > 0) {
    return false;
  }

  bool has_custom_hdr_ref_white = (reader.ReadBit() == 1);
  bool has_adaptive_tone_map = (reader.ReadBit() == 1);
  reader.SkipBits(6);  // reserved

  out_metadata->hdr_reference_white = kDefaultHdrReferenceWhite;
  if (has_custom_hdr_ref_white) {
    if (!reader.HasBits(16)) return false;
    uint32_t ref_white_scaled = reader.ReadBits(16);
    out_metadata->hdr_reference_white =
        std::max(1.0f, std::min(kMaxRefWhiteScaled, static_cast<float>(ref_white_scaled))) /
        kHdrRefWhiteScale;
  }

  out_metadata->has_adaptive_tone_map_flag = has_adaptive_tone_map;
  out_metadata->baseline_hdr_headroom_log2 = 0.0f;
  out_metadata->use_reference_white_tone_mapping_flag = false;
  out_metadata->rules.clear();

  if (!has_adaptive_tone_map) {
    return true;
  }

  if (!reader.HasBits(17)) {
    return true;
  }

  uint32_t baseline_headroom_scaled = reader.ReadBits(16);
  out_metadata->baseline_hdr_headroom_log2 =
      std::min(kMaxHeadroomScaled, static_cast<float>(baseline_headroom_scaled)) /
      kHdrHeadroomScale;
  bool use_rwtm = (reader.ReadBit() == 1);
  out_metadata->use_reference_white_tone_mapping_flag = use_rwtm;

  if (use_rwtm) {
    return true;
  }

  if (!reader.HasBits(7)) {
    return true;
  }

  uint32_t num_alternate_images = std::min(reader.ReadBits(3), kMaxAlternateImages);
  uint32_t mode = reader.ReadBits(2);
  bool has_common_mix = (reader.ReadBit() == 1);
  bool has_common_curve = (reader.ReadBit() == 1);

  if (mode == 3) {
    if (!reader.HasBits(8 * 16)) return false;
    for (int i = 0; i < 8; ++i) {
      out_metadata->gain_application_space_chromaticities[i] =
          std::min(kMaxChromaticitiesScaled, static_cast<float>(reader.ReadBits(16))) /
          kChromaticitiesScale;
    }
  }

  for (uint32_t a = 0; a < num_alternate_images; ++a) {
    if (!reader.HasBits(16)) break;
    AgtmToneMappingRule rule;
    uint32_t alt_headroom_scaled = reader.ReadBits(16);
    rule.alternate_hdr_headroom_log2 =
        std::min(kMaxHeadroomScaled, static_cast<float>(alt_headroom_scaled)) / kHdrHeadroomScale;

    if (a == 0 || !has_common_mix) {
      if (!reader.HasBits(2)) break;
      uint32_t comp_type = reader.ReadBits(2);
      if (comp_type != 3) {
        if (!reader.HasBits(6)) break;
        reader.SkipBits(6);
      } else {
        if (!reader.HasBits(6)) break;
        std::array<bool, 6> flags;
        for (int f = 0; f < 6; ++f) {
          flags[f] = (reader.ReadBit() == 1);
        }
        for (int f = 0; f < 6; ++f) {
          if (flags[f]) {
            if (!reader.HasBits(16)) break;
            reader.SkipBits(16);
          }
        }
      }
    }

    if (a == 0 || !has_common_curve) {
      if (!reader.HasBits(8)) break;
      uint32_t num_pts = reader.ReadBits(5) + 1;
      bool use_pchip = (reader.ReadBit() == 1);
      reader.SkipBits(2);
      rule.use_pchip_slope = use_pchip;

      for (uint32_t p = 0; p < num_pts; ++p) {
        if (!reader.HasBits(16)) break;
        AgtmControlPoint cp;
        cp.x = static_cast<float>(reader.ReadBits(16)) / kControlPointXScale;
        rule.curve.push_back(cp);
      }
    } else if (!out_metadata->rules.empty()) {
      rule.use_pchip_slope = out_metadata->rules[0].use_pchip_slope;
      for (const auto& cp : out_metadata->rules[0].curve) {
        AgtmControlPoint new_cp;
        new_cp.x = cp.x;
        rule.curve.push_back(new_cp);
      }
    }

    float expected_sign = (out_metadata->baseline_hdr_headroom_log2 <
                           rule.alternate_hdr_headroom_log2)
                              ? 1.0f
                              : -1.0f;
    for (auto& cp : rule.curve) {
      if (!reader.HasBits(16)) break;
      cp.y = (static_cast<float>(reader.ReadBits(16)) / kControlPointYScale) * expected_sign;
    }

    if (!rule.use_pchip_slope) {
      for (auto& cp : rule.curve) {
        if (!reader.HasBits(16)) break;
        float theta_scaled = static_cast<float>(reader.ReadBits(16));
        float theta = (theta_scaled * kPi / kThetaScale) - kThetaOffset;
        cp.m = std::tan(theta);
      }
    }

    out_metadata->rules.push_back(std::move(rule));
  }

  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
