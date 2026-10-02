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

#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

#include <absl/strings/match.h>

#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "legacy_micro_video_metadata.pb.h"
#include "motion_photo/metadata_block.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

int64_t ExtractInt64Attr(std::string_view xml, const std::string& attr_name) {
  std::regex attr_regex(R"((?:GCamera|Camera):)" + attr_name +
                        R"(\s*=\s*["'](-?\d+)["'])");
  std::cmatch match;
  if (std::regex_search(xml.data(), xml.data() + xml.size(), match,
                        attr_regex)) {
    return std::stoll(match[1].str());
  }
  std::regex elem_regex(R"(<(?:GCamera|Camera):)" + attr_name +
                        R"(>[\s]*(-?\d+)[\s]*</(?:GCamera|Camera):)" +
                        attr_name + R"(>)");
  if (std::regex_search(xml.data(), xml.data() + xml.size(), match,
                        elem_regex)) {
    return std::stoll(match[1].str());
  }
  return -1;
}

}  // namespace

bool LegacyMicroVideoProvider::Identify(const RawMetadataBlock& block) {
  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    absl::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                          block.bytes.size());
    return absl::StrContains(xml, "MicroVideo");
  }
  if (block.format_identifier == "container.trailer") {
    if (block.bytes.size() >= 8) {
      std::string_view tag(
          reinterpret_cast<const char*>(block.bytes.data() + 4), 4);
      if (tag == "ftyp" || tag == "moov" || tag == "mdat" || tag == "free" ||
          tag == "skip" || tag == "wide") {
        return true;
      }
    }
  }
  return false;
}

bool LegacyMicroVideoProvider::DecodeToSemantic(
    const RawMetadataBlock& block, std::vector<uint8_t>* out_proto_bytes,
    std::string* out_type_url, std::string* error_message) {
  if (block.type != "XMP") {
    return false;
  }
  std::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                       block.bytes.size());

  LegacyMicroVideoMetadata metadata;
  int64_t micro_video = ExtractInt64Attr(xml, "MicroVideo");
  if (micro_video > 0) {
    metadata.set_legacy_micro_video(micro_video);
  }

  int64_t timestamp =
      ExtractInt64Attr(xml, "MicroVideoPresentationTimestampUs");
  if (timestamp >= 0) {
    metadata.set_primary_image_timestamp_us(timestamp);
  }

  int64_t version = ExtractInt64Attr(xml, "MicroVideoVersion");
  if (version > 0) {
    metadata.set_version(version);
  }

  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo."
        "LegacyMicroVideoMetadata";
  }

  out_proto_bytes->resize(metadata.ByteSizeLong());
  if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                 out_proto_bytes->size())) {
    if (error_message) {
      *error_message = "Failed to serialize LegacyMicroVideoMetadata proto";
    }
    return false;
  }

  return true;
}

bool LegacyMicroVideoProvider::IsMotionPhoto(
    const std::string& type_url, const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "LegacyMicroVideoMetadata")) {
    return false;
  }
  LegacyMicroVideoMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.legacy_micro_video() == 1;
}

bool LegacyMicroVideoProvider::GetVideoInfo(const RawMetadataBlock& block,
                                            size_t* out_offset_in_block,
                                            size_t* out_length) const {
  if (block.format_identifier == "container.trailer") {
    if (out_offset_in_block) {
      *out_offset_in_block = 0;
    }
    if (out_length) {
      *out_length = (block.total_payload_size > 0) ? block.total_payload_size
                                                   : block.bytes.size();
    }
    return true;
  }
  return false;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
