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

#ifndef MOTION_PHOTO_AGTM_H_
#define MOTION_PHOTO_AGTM_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

struct AgtmControlPoint {
  float x = 0.0f;
  float y = 0.0f;
  float m = 0.0f;
};

struct AgtmComponentMix {
  std::array<float, 3> rgb = {0.0f, 0.0f, 0.0f};
  float max = 0.0f;
  float min = 0.0f;
  float component = 0.0f;
};

struct AgtmToneMappingRule {
  float alternate_hdr_headroom_log2 = 0.0f;
  std::vector<AgtmControlPoint> curve;
  bool use_pchip_slope = false;
  AgtmComponentMix mix;
};

struct AgtmDynamicMetadata {
  static constexpr float kDefaultHdrReferenceWhite = 203.0f;

  float hdr_reference_white = kDefaultHdrReferenceWhite;
  bool has_adaptive_tone_map_flag = true;
  float baseline_hdr_headroom_log2 = 0.0f;
  bool use_reference_white_tone_mapping_flag = false;
  std::array<float, 8> gain_application_space_chromaticities = {
      0.708f, 0.292f, 0.17f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.329f};
  std::vector<AgtmToneMappingRule> rules;
};

// Converts AgtmDynamicMetadata to a JSON string representation.
std::string AgtmToJson(const AgtmDynamicMetadata& meta);

// Parses AGTM dynamic metadata from a raw ST 2094-50 bitstream / T.35 payload.
bool ParseAgtmPayload(const uint8_t* bytes, size_t num_bytes, AgtmDynamicMetadata* out_metadata);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_AGTM_H_
