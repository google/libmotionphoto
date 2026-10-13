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

#ifndef LIBMOTIONPHOTO_PLUGINS_SAMSUNG_SEF_SAMSUNG_SEF_PROVIDER_H_
#define LIBMOTIONPHOTO_PLUGINS_SAMSUNG_SEF_SAMSUNG_SEF_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

// 3P plugin provider for Samsung Extended Format (SEF) Motion Photos.
// Supports both JPEG SEF trailers ("container.trailer" ending with "SEFT")
// and HEIC SEFD boxes ("heic.sefd" ending with "SEFT").
class SamsungSefProvider : public IMetadataProvider {
 public:
  SamsungSefProvider() = default;
  ~SamsungSefProvider() override = default;

  std::string GetPluginName() const override { return "samsung_sef"; }

  bool Identify(const RawMetadataBlock& block) override;

  bool DecodeToSemantic(const RawMetadataBlock& block,
                        std::vector<uint8_t>* out_proto_bytes,
                        std::string* out_type_url,
                        std::string* error_message) override;

  bool IsMotionPhoto(const std::string& type_url,
                     const std::string& payload_bytes) const override;

  bool GetVideoInfo(const RawMetadataBlock& block, size_t* out_offset_in_block,
                    size_t* out_length) const override;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // LIBMOTIONPHOTO_PLUGINS_SAMSUNG_SEF_SAMSUNG_SEF_PROVIDER_H_
