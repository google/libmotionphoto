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
#include <absl/strings/numbers.h>
#include <absl/strings/string_view.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "legacy_micro_video_metadata.pb.h"  // NOLINT
#include "motion_photo/metadata_block.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr size_t kMaxXiaomiFtypScanBytes = 4096;

uint32_t ReadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int64_t ExtractInt64Attr(absl::string_view xml, absl::string_view attr_name) {
  const std::string prefixes[] = {"GCamera:", "Camera:"};
  for (const auto& prefix : prefixes) {
    std::string needle = prefix + std::string(attr_name);
    size_t pos = xml.find(needle);
    while (pos != absl::string_view::npos) {
      size_t after = pos + needle.size();
      while (after < xml.size() &&
             (xml[after] == ' ' || xml[after] == '\t' || xml[after] == '\r' ||
              xml[after] == '\n')) {
        ++after;
      }
      if (after < xml.size() && xml[after] == '=') {
        ++after;
        while (after < xml.size() &&
               (xml[after] == ' ' || xml[after] == '\t')) {
          ++after;
        }
        if (after < xml.size() && (xml[after] == '"' || xml[after] == '\'')) {
          char quote = xml[after++];
          size_t end_quote = xml.find(quote, after);
          int64_t val = -1;
          if (end_quote != absl::string_view::npos &&
              absl::SimpleAtoi(xml.substr(after, end_quote - after), &val)) {
            return val;
          }
        }
      } else if (after < xml.size() && xml[after] == '>') {
        ++after;
        while (after < xml.size() &&
               (xml[after] == ' ' || xml[after] == '\t' || xml[after] == '\r' ||
                xml[after] == '\n')) {
          ++after;
        }
        size_t close_tag = xml.find('<', after);
        if (close_tag != absl::string_view::npos) {
          absl::string_view inner = xml.substr(after, close_tag - after);
          while (!inner.empty() &&
                 (inner.back() == ' ' || inner.back() == '\t' ||
                  inner.back() == '\r' || inner.back() == '\n')) {
            inner.remove_suffix(1);
          }
          int64_t val = -1;
          if (absl::SimpleAtoi(inner, &val)) {
            return val;
          }
        }
      }
      pos = xml.find(needle, pos + 1);
    }
  }
  return -1;
}

// Finds the start offset of the embedded MP4 stream inside `container.trailer`.
// Supports standard offset 0 as well as Xiaomi MVIMG files where
// GCamera:MicroVideoOffset is inaccurate and the trailer starts with padding
// bytes before the ISO BMFF `ftyp` box.
bool FindMp4StartInTrailer(const RawMetadataBlock& block,
                           size_t* out_mp4_offset) {
  const size_t total_size = block.Size();
  if (total_size < 8) {
    return false;
  }
  const size_t scan_limit = std::min(total_size, kMaxXiaomiFtypScanBytes);
  const uint8_t* head_ptr = nullptr;
  std::vector<uint8_t> scratch;
  if (!block.ReadView(0, scan_limit, &head_ptr, &scratch)) {
    return false;
  }
  absl::string_view tag0(reinterpret_cast<const char*>(head_ptr + 4), 4);
  if (tag0 == "ftyp" || tag0 == "moov" || tag0 == "mdat" || tag0 == "free" ||
      tag0 == "skip" || tag0 == "wide") {
    if (out_mp4_offset) {
      *out_mp4_offset = 0;
    }
    return true;
  }

  // Forward scan for `[u32be box_size]["ftyp"]` (Xiaomi MVIMG offset drift).
  if (scan_limit >= 8) {
    absl::string_view window(reinterpret_cast<const char*>(head_ptr),
                             scan_limit);
    size_t pos = window.find("ftyp", 4);
    while (pos != absl::string_view::npos) {
      if (pos >= 4) {
        size_t box_start = pos - 4;
        uint32_t box_size = ReadBe32(head_ptr + box_start);
        if (box_size >= 8 && box_size <= 128) {
          if (out_mp4_offset) {
            *out_mp4_offset = box_start;
          }
          return true;
        }
      }
      pos = window.find("ftyp", pos + 1);
    }
  }
  return false;
}

}  // namespace

bool LegacyMicroVideoProvider::Identify(const RawMetadataBlock& block) {
  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    if (block.Data() == nullptr || block.BufferedSize() == 0) {
      return false;
    }
    absl::string_view xml(reinterpret_cast<const char*>(block.Data()),
                          block.BufferedSize());
    return absl::StrContains(xml, "MicroVideo");
  }
  if (block.format_identifier == "container.trailer") {
    return FindMp4StartInTrailer(block, nullptr);
  }
  return false;
}

bool LegacyMicroVideoProvider::DecodeToSemantic(
    const RawMetadataBlock& block, std::vector<uint8_t>* out_proto_bytes,
    std::string* out_type_url, std::string* error_message) {
  if (block.type != "XMP" || block.Data() == nullptr ||
      block.BufferedSize() == 0) {
    return false;
  }
  absl::string_view xml(reinterpret_cast<const char*>(block.Data()),
                        block.BufferedSize());

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

  int64_t offset = ExtractInt64Attr(xml, "MicroVideoOffset");
  if (offset > 0) {
    metadata.set_micro_video_offset(offset);
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
  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    if (block.Data() == nullptr || block.BufferedSize() == 0) {
      return false;
    }
    absl::string_view xml(reinterpret_cast<const char*>(block.Data()),
                          block.BufferedSize());
    int64_t offset = ExtractInt64Attr(xml, "MicroVideoOffset");
    if (offset > 0) {
      if (out_offset_in_block) {
        *out_offset_in_block = 0;
      }
      if (out_length) {
        *out_length = static_cast<size_t>(offset);
      }
      return true;
    }
    return false;
  }
  if (block.format_identifier == "container.trailer") {
    size_t mp4_start = 0;
    if (!FindMp4StartInTrailer(block, &mp4_start)) {
      mp4_start = 0;
    }
    size_t total_len = block.Size();
    if (mp4_start >= total_len) {
      return false;
    }
    if (out_offset_in_block) {
      *out_offset_in_block = mp4_start;
    }
    if (out_length) {
      *out_length = total_len - mp4_start;
    }
    return true;
  }
  return false;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
