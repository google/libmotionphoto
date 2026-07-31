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

#ifndef MOTION_PHOTO_GOOGLE_MOTION_PHOTO_PROVIDER_H_
#include <cstdint>
#define MOTION_PHOTO_GOOGLE_MOTION_PHOTO_PROVIDER_H_

#include <string>
#include <vector>

#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

class GoogleMotionPhotoProvider : public IMetadataProvider {
 public:
  GoogleMotionPhotoProvider() = default;
  ~GoogleMotionPhotoProvider() override = default;

  bool Identify(const RawMetadataBlock& block) override;

  bool DecodeToSemantic(const RawMetadataBlock& block,
                        std::vector<uint8_t>* out_proto_bytes,
                        std::string* out_type_url,
                        std::string* error_message) override;

  bool IsMotionPhoto(const std::string& type_url,
                     const std::string& payload_bytes) const override;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_GOOGLE_MOTION_PHOTO_PROVIDER_H_
