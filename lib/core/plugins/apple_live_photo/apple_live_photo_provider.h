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

#ifndef LIBMOTIONPHOTO_PLUGINS_APPLE_LIVE_PHOTO_APPLE_LIVE_PHOTO_PROVIDER_H_
#define LIBMOTIONPHOTO_PLUGINS_APPLE_LIVE_PHOTO_APPLE_LIVE_PHOTO_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

// 3P plugin provider for Apple QuickTime Live Photo (.mov) containers carrying
// the "com.apple.quicktime.still-image-time" timed metadata track.
class AppleLivePhotoProvider : public IMetadataProvider {
 public:
  AppleLivePhotoProvider() = default;
  ~AppleLivePhotoProvider() override = default;

  std::string GetPluginName() const override { return "apple_live_photo"; }

  bool Identify(const RawMetadataBlock& block) override;

  bool DecodeToSemantic(const RawMetadataBlock& block,
                        std::vector<uint8_t>* out_proto_bytes,
                        std::string* out_type_url,
                        std::string* error_message) override;

  bool IsMotionPhoto(const std::string& type_url,
                     const std::string& payload_bytes) const override;

  bool GetVideoInfo(const RawMetadataBlock& block, size_t* out_offset_in_block,
                    size_t* out_length) const override;

  // Extracts the still-image presentation timestamp in microseconds from an
  // Apple Live Photo .mov block, memory buffer, or file, or returns -1 if not
  // present.
  static int64_t ExtractStillImageTimestampUs(const RawMetadataBlock& block);
  static int64_t ExtractStillImageTimestampUs(const uint8_t* data, size_t size);
  static int64_t ExtractStillImageTimestampUsFromFile(
      const std::string& file_path);
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // LIBMOTIONPHOTO_PLUGINS_APPLE_LIVE_PHOTO_APPLE_LIVE_PHOTO_PROVIDER_H_
