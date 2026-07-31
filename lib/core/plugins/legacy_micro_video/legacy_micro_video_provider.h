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

#ifndef MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_
#define MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

// A 3P metadata provider plugin that recognizes and decodes legacy Micro Video
// (GCamera / Camera MicroVideo XMP metadata and concatenated MP4 trailer).
class LegacyMicroVideoProvider : public IMetadataProvider {
 public:
  LegacyMicroVideoProvider() = default;
  ~LegacyMicroVideoProvider() override = default;

  bool Identify(const RawMetadataBlock& block) override;

  bool DecodeToSemantic(const RawMetadataBlock& block,
                        std::vector<uint8_t>* out_proto_bytes,
                        std::string* out_type_url,
                        std::string* error_message) override;

  std::string GetPluginName() const override { return "legacy_micro_video"; }

  bool IsMotionPhoto(const std::string& type_url,
                     const std::string& payload_bytes) const override;

  bool GetVideoInfo(const RawMetadataBlock& block, size_t* out_offset_in_block,
                    size_t* out_length) const override;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_
