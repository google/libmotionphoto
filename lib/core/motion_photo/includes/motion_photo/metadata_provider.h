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

#ifndef MOTION_PHOTO_METADATA_PROVIDER_H_
#define MOTION_PHOTO_METADATA_PROVIDER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"

namespace libmotionphoto {
namespace motion_photo {

class IMetadataProvider {
 public:
  virtual ~IMetadataProvider() = default;

  // Returns true if this provider recognizes the format.
  virtual bool Identify(const RawMetadataBlock& block) = 0;

  // Decodes raw bits into a serialized SemanticMetadata proto.
  // Returns true on success.
  // out_type_url receives the type URL of the decoded proto (e.g.
  // "type.googleapis.com/...").
  virtual bool DecodeToSemantic(const RawMetadataBlock& block,
                                std::vector<uint8_t>* out_proto_bytes,
                                std::string* out_type_url,
                                std::string* error_message) = 0;

  // Returns the unique name of the plugin (used in allowlist).
  virtual std::string GetPluginName() const { return ""; }

  // Returns true if the decoded payload represents a motion photo for this plugin.
  virtual bool IsMotionPhoto(const std::string& type_url,
                             const std::string& payload_bytes) const {
    return false;
  }

  // Returns video offset (relative to block start) and length if the block contains a video.
  virtual bool GetVideoInfo(const RawMetadataBlock& block,
                            size_t* out_offset_in_block,
                            size_t* out_length) const {
    return false;
  }

  // Extracts the motion-photo-related metadata bytes from the raw block.
  // Default implementation returns all block bytes (appropriate for XMP
  // blocks). Proprietary trailer plugins can override this to return only
  // feature headers.
  virtual bool ExtractRawMetadata(
      const RawMetadataBlock& block,
      std::vector<uint8_t>* out_metadata_bytes) const {
    if (out_metadata_bytes) {
      *out_metadata_bytes = block.bytes;
      return true;
    }
    return false;
  }
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_PROVIDER_H_
