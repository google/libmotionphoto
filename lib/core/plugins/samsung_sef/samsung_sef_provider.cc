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

#include "plugins/samsung_sef/samsung_sef_provider.h"

#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/string_view.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "samsung_sef_metadata.pb.h"  // NOLINT

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr uint16_t kSefTypeMotionPhotoData = 0x0A30;
constexpr size_t kMaxSefEntries = 256;

uint16_t ReadLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t ReadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool ParseSefBlock(const RawMetadataBlock& block, SamsungSefMetadata* out_meta,
                   std::string* error_message) {
  const auto& bytes = block.bytes;
  if (bytes.size() < 20) {
    if (error_message) {
      *error_message = "SEF block is too small";
    }
    return false;
  }

  absl::string_view tail_magic(
      reinterpret_cast<const char*>(bytes.data() + bytes.size() - 4), 4);
  if (tail_magic != "SEFT") {
    if (error_message) {
      *error_message = "Missing SEFT footer magic";
    }
    return false;
  }

  const uint32_t sefh_back_offset = ReadLe32(bytes.data() + bytes.size() - 8);
  if (sefh_back_offset < 12 || sefh_back_offset > bytes.size() - 8) {
    if (error_message) {
      *error_message = "Invalid SEFH backward offset";
    }
    return false;
  }

  const size_t sefh_pos = bytes.size() - 8 - sefh_back_offset;
  absl::string_view sefh_magic(
      reinterpret_cast<const char*>(bytes.data() + sefh_pos), 4);
  if (sefh_magic != "SEFH") {
    if (error_message) {
      *error_message = "Missing SEFH header magic";
    }
    return false;
  }

  const uint32_t sef_version = ReadLe32(bytes.data() + sefh_pos + 4);
  const uint32_t entry_count = ReadLe32(bytes.data() + sefh_pos + 8);
  if (entry_count > kMaxSefEntries ||
      sefh_pos + 12 + static_cast<size_t>(entry_count) * 12 >
          bytes.size() - 8) {
    if (error_message) {
      *error_message = "SEFH entry count exceeds block bounds";
    }
    return false;
  }

  out_meta->set_sef_version(sef_version);
  out_meta->set_sef_entry_count(entry_count);
  out_meta->set_primary_image_timestamp_us(-1);

  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* entry_ptr = bytes.data() + sefh_pos + 12 + i * 12;
    const uint16_t entry_type = ReadLe16(entry_ptr + 2);
    const uint32_t neg_offset = ReadLe32(entry_ptr + 4);
    const uint32_t rec_length = ReadLe32(entry_ptr + 8);

    if (neg_offset == 0 || rec_length < 8) {
      continue;
    }
    if (neg_offset > sefh_pos) {
      // In dual-tagged Samsung Motion Photos (e.g. Galaxy S25/S26 Ultra),
      // XMP Container:Directory Item:Length starts at the MP4 ftyp box inside
      // the 0x0A30 record, so the 24-byte 0x0A30 SEF record header (8B +
      // "MotionPhoto_Data") sits immediately before block.bytes[0].
      const size_t prefix_before_block = neg_offset - sefh_pos;
      if (entry_type == kSefTypeMotionPhotoData && prefix_before_block <= 64 &&
          rec_length > prefix_before_block &&
          rec_length - prefix_before_block <= sefh_pos) {
        out_meta->add_sef_keys("MotionPhoto_Data");
        out_meta->set_has_motion_photo_data(true);
        const size_t rem_len = rec_length - prefix_before_block;
        if (rem_len > 12) {
          absl::string_view magic(reinterpret_cast<const char*>(bytes.data()),
                                  4);
          if (magic == "mpv2" || magic == "mpv3") {
            out_meta->set_motion_photo_data_magic(std::string(magic));
            out_meta->set_video_offset(12);
            out_meta->set_video_length(rem_len - 12);
            continue;
          }
        }
        out_meta->set_video_offset(0);
        out_meta->set_video_length(rem_len);
      }
      continue;
    }
    const size_t rec_pos = sefh_pos - neg_offset;
    if (rec_pos + rec_length > bytes.size()) {
      continue;
    }

    const uint32_t key_len = ReadLe32(bytes.data() + rec_pos + 4);
    if (8 + static_cast<size_t>(key_len) > rec_length) {
      continue;
    }

    std::string key(reinterpret_cast<const char*>(bytes.data() + rec_pos + 8),
                    key_len);
    out_meta->add_sef_keys(key);

    if (entry_type == kSefTypeMotionPhotoData || key == "MotionPhoto_Data") {
      out_meta->set_has_motion_photo_data(true);
      const size_t payload_pos = rec_pos + 8 + key_len;
      const size_t payload_len = rec_length - 8 - key_len;

      if (payload_len >= 12) {
        absl::string_view magic(
            reinterpret_cast<const char*>(bytes.data() + payload_pos), 4);
        if (magic == "mpv2" || magic == "mpv3") {
          out_meta->set_motion_photo_data_magic(std::string(magic));
          const uint32_t hdr_mp4_offset =
              ReadBe32(bytes.data() + payload_pos + 4);
          const uint32_t hdr_mp4_size =
              ReadBe32(bytes.data() + payload_pos + 8);

          if (payload_len > 12) {
            out_meta->set_video_offset(payload_pos + 12);
            out_meta->set_video_length(payload_len - 12);
          } else if (block.format_identifier == "heic.sefd") {
            if (hdr_mp4_size > 0) {
              if (block.offset > 0 && hdr_mp4_offset >= block.offset) {
                out_meta->set_video_offset(hdr_mp4_offset - block.offset);
              } else {
                out_meta->set_video_offset(0);
              }
              out_meta->set_video_length(hdr_mp4_size);
            } else if (block.total_payload_size > 0) {
              out_meta->set_video_offset(0);
              out_meta->set_video_length(block.total_payload_size);
            }
          }
          continue;
        }
      }

      if (payload_len >= 8) {
        out_meta->set_video_offset(payload_pos);
        out_meta->set_video_length(payload_len);
      } else if (payload_len == 0 && block.format_identifier == "heic.sefd" &&
                 block.total_payload_size > 0) {
        out_meta->set_video_offset(0);
        out_meta->set_video_length(block.total_payload_size);
      }
    }
  }

  return true;
}

}  // namespace

bool SamsungSefProvider::Identify(const RawMetadataBlock& block) {
  if (block.format_identifier != "container.trailer" &&
      block.format_identifier != "heic.sefd") {
    return false;
  }
  if (block.bytes.size() < 20) {
    return false;
  }
  absl::string_view tail_magic(
      reinterpret_cast<const char*>(block.bytes.data() + block.bytes.size() -
                                    4),
      4);
  return tail_magic == "SEFT";
}

bool SamsungSefProvider::DecodeToSemantic(const RawMetadataBlock& block,
                                          std::vector<uint8_t>* out_proto_bytes,
                                          std::string* out_type_url,
                                          std::string* error_message) {
  SamsungSefMetadata metadata;
  if (!ParseSefBlock(block, &metadata, error_message)) {
    return false;
  }

  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo.SamsungSefMetadata";
  }
  if (out_proto_bytes) {
    out_proto_bytes->resize(metadata.ByteSizeLong());
    if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                   out_proto_bytes->size())) {
      if (error_message) {
        *error_message = "Failed to serialize SamsungSefMetadata proto";
      }
      return false;
    }
  }
  return true;
}

bool SamsungSefProvider::IsMotionPhoto(const std::string& type_url,
                                       const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "SamsungSefMetadata")) {
    return false;
  }
  SamsungSefMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.has_motion_photo_data() && metadata.video_length() > 0;
}

bool SamsungSefProvider::GetVideoInfo(const RawMetadataBlock& block,
                                      size_t* out_offset_in_block,
                                      size_t* out_length) const {
  SamsungSefMetadata metadata;
  if (!ParseSefBlock(block, &metadata, nullptr)) {
    return false;
  }
  if (!metadata.has_motion_photo_data() || metadata.video_length() == 0) {
    return false;
  }
  if (out_offset_in_block) {
    *out_offset_in_block = static_cast<size_t>(metadata.video_offset());
  }
  if (out_length) {
    *out_length = static_cast<size_t>(metadata.video_length());
  }
  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
