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

#include "plugins/honor_live_photo/honor_live_photo_provider.h"

#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/string_view.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "honor_live_photo_metadata.pb.h"  // NOLINT
#include "motion_photo/metadata_block.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr size_t kTagWidth = 20;
constexpr size_t kTlvFooterSize = 24;  // 4-byte u32LE length + 20-byte tag
constexpr size_t kMaxTlvRecords = 128;

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

absl::string_view TrimPadding(absl::string_view sv) {
  while (!sv.empty() &&
         (sv.back() == '\0' || sv.back() == ' ' || sv.back() == '\r' ||
          sv.back() == '\n' || sv.back() == '\t')) {
    sv.remove_suffix(1);
  }
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
    sv.remove_prefix(1);
  }
  return sv;
}

bool HasIsoBmffAt(const RawMetadataBlock& block, size_t offset,
                  size_t length) {
  const size_t total_size = block.Size();
  if (length < 8 || offset > total_size || length > total_size - offset) {
    return false;
  }
  const uint8_t* hdr = nullptr;
  std::vector<uint8_t> scratch;
  if (!block.ReadView(offset, 8, &hdr, &scratch)) {
    return false;
  }
  absl::string_view tag(reinterpret_cast<const char*>(hdr + 4), 4);
  return tag == "ftyp" || tag == "moov" || tag == "mdat" || tag == "free" ||
         tag == "skip" || tag == "wide";
}

bool ExtractJsonInt32(absl::string_view json, absl::string_view key,
                      int32_t* out_val) {
  std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = json.find(needle);
  if (pos == absl::string_view::npos) {
    return false;
  }
  size_t colon = json.find(':', pos + needle.size());
  if (colon == absl::string_view::npos) {
    return false;
  }
  size_t num_start = colon + 1;
  while (num_start < json.size() &&
         (json[num_start] == ' ' || json[num_start] == '\t')) {
    ++num_start;
  }
  size_t num_end = num_start;
  if (num_end < json.size() && json[num_end] == '-') {
    ++num_end;
  }
  while (num_end < json.size() && json[num_end] >= '0' &&
         json[num_end] <= '9') {
    ++num_end;
  }
  if (num_end == num_start) {
    return false;
  }
  return absl::SimpleAtoi(json.substr(num_start, num_end - num_start), out_val);
}

bool ParseHonorTrailer(const RawMetadataBlock& block,
                       HonorLivePhotoMetadata* out_meta,
                       std::string* error_message) {
  const size_t total_size = block.Size();
  if (total_size < kTagWidth) {
    if (error_message) {
      *error_message = "Honor/Huawei trailer is shorter than 20 bytes";
    }
    return false;
  }

  out_meta->set_primary_image_timestamp_us(-1);

  const uint8_t* tail_ptr = nullptr;
  std::vector<uint8_t> tail_scratch;
  if (!block.ReadView(total_size - kTagWidth, kTagWidth, &tail_ptr,
                      &tail_scratch)) {
    if (error_message) {
      *error_message = "Failed to read Honor/Huawei trailer footer";
    }
    return false;
  }
  absl::string_view tail_20 = TrimPadding(
      absl::string_view(reinterpret_cast<const char*>(tail_ptr), kTagWidth));

  // Case 1: Classic Huawei/Honor "LIVE_<size>" 20-byte / 40-byte ASCII footer.
  if (absl::StartsWith(tail_20, "LIVE_")) {
    out_meta->set_format_variant("live_footer");
    absl::string_view size_str = tail_20.substr(5);
    uint64_t mp4_size = 0;
    if (!absl::SimpleAtoi(size_str, &mp4_size) || mp4_size == 0 ||
        mp4_size + kTagWidth > total_size) {
      if (error_message) {
        *error_message = "Invalid MP4 size in LIVE_ footer";
      }
      return false;
    }

    // Check if there is a 20-byte cover offset immediately before "LIVE_<size>"
    size_t candidate_offset = total_size - kTagWidth - mp4_size;
    if (total_size >= 2 * kTagWidth + mp4_size) {
      size_t alt_offset = total_size - 2 * kTagWidth - mp4_size;
      if (HasIsoBmffAt(block, alt_offset, mp4_size)) {
        candidate_offset = alt_offset;
        const uint8_t* cover_ptr = nullptr;
        std::vector<uint8_t> cover_scratch;
        if (block.ReadView(total_size - 2 * kTagWidth, kTagWidth, &cover_ptr,
                           &cover_scratch)) {
          absl::string_view cover_str = TrimPadding(absl::string_view(
              reinterpret_cast<const char*>(cover_ptr), kTagWidth));
          int64_t cover_ms = -1;
          if (absl::SimpleAtoi(cover_str, &cover_ms) && cover_ms >= 0) {
            out_meta->set_primary_image_timestamp_us(cover_ms * 1000);
          }
        }
      }
    }

    if (!HasIsoBmffAt(block, candidate_offset, mp4_size)) {
      if (error_message) {
        *error_message = "LIVE_ footer does not point to a valid MP4 stream";
      }
      return false;
    }

    out_meta->set_has_embedded_video(true);
    out_meta->set_video_offset(candidate_offset);
    out_meta->set_video_length(mp4_size);
    return true;
  }

  // Case 2: Backward-linked 20-byte TLV chain ("HiHonor_*" or "LivePhoto_*").
  if (absl::StartsWith(tail_20, "HiHonor_") ||
      absl::StartsWith(tail_20, "LivePhoto_")) {
    if (total_size < kTlvFooterSize) {
      if (error_message) {
        *error_message = "TLV trailer is shorter than 24 bytes";
      }
      return false;
    }

    out_meta->set_format_variant(absl::StartsWith(tail_20, "HiHonor_")
                                     ? "hihonor_tlv"
                                     : "livephoto_tlv");

    size_t pos = total_size;
    size_t record_count = 0;
    while (pos >= kTlvFooterSize && record_count < kMaxTlvRecords) {
      const uint8_t* ftr_ptr = nullptr;
      std::vector<uint8_t> ftr_scratch;
      if (!block.ReadView(pos - kTlvFooterSize, kTlvFooterSize, &ftr_ptr,
                          &ftr_scratch)) {
        break;
      }
      absl::string_view tag = TrimPadding(absl::string_view(
          reinterpret_cast<const char*>(ftr_ptr + 4), kTagWidth));
      if (!absl::StartsWith(tag, "HiHonor_") &&
          !absl::StartsWith(tag, "LivePhoto_")) {
        break;
      }

      const uint32_t payload_len = ReadLe32(ftr_ptr);
      if (kTlvFooterSize + static_cast<size_t>(payload_len) > pos) {
        if (record_count == 0) {
          if (error_message) {
            *error_message = "First TLV record length exceeds trailer size";
          }
          return false;
        }
        break;
      }

      const size_t payload_pos = pos - kTlvFooterSize - payload_len;
      out_meta->add_tlv_tags(std::string(tag));
      ++record_count;

      if (tag == "HiHonor_OfflineData" && payload_len > 4 &&
          payload_len <= 65536) {
        const uint8_t* json_ptr = nullptr;
        std::vector<uint8_t> json_scratch;
        if (block.ReadView(payload_pos + 4, payload_len - 4, &json_ptr,
                           &json_scratch)) {
          absl::string_view json(reinterpret_cast<const char*>(json_ptr),
                                 payload_len - 4);
          int32_t scene_mode = 0;
          if (ExtractJsonInt32(json, "sceneMode", &scene_mode)) {
            out_meta->set_scene_mode(scene_mode);
          }
        }
      }

      if (!out_meta->has_embedded_video() &&
          HasIsoBmffAt(block, payload_pos, payload_len)) {
        out_meta->set_has_embedded_video(true);
        out_meta->set_video_offset(payload_pos);
        out_meta->set_video_length(payload_len);
      }

      pos = payload_pos;
    }

    // If an MP4 precedes the TLV chain directly after EOI ([0 .. pos)), detect
    // it as well.
    if (!out_meta->has_embedded_video() && pos >= 16 &&
        HasIsoBmffAt(block, 0, pos)) {
      out_meta->set_has_embedded_video(true);
      out_meta->set_video_offset(0);
      out_meta->set_video_length(pos);
    }

    return record_count > 0;
  }

  if (error_message) {
    *error_message = "Unrecognized Honor/Huawei trailer signature";
  }
  return false;
}

}  // namespace

bool HonorLivePhotoProvider::Identify(const RawMetadataBlock& block) {
  const size_t total_size = block.Size();
  if (block.format_identifier != "container.trailer" ||
      total_size < kTagWidth) {
    return false;
  }
  const uint8_t* tail_ptr = nullptr;
  std::vector<uint8_t> scratch;
  if (!block.ReadView(total_size - kTagWidth, kTagWidth, &tail_ptr, &scratch)) {
    return false;
  }
  absl::string_view tail_20 = TrimPadding(
      absl::string_view(reinterpret_cast<const char*>(tail_ptr), kTagWidth));
  return absl::StartsWith(tail_20, "HiHonor_") ||
         absl::StartsWith(tail_20, "LivePhoto_") ||
         absl::StartsWith(tail_20, "LIVE_");
}

bool HonorLivePhotoProvider::DecodeToSemantic(
    const RawMetadataBlock& block, std::vector<uint8_t>* out_proto_bytes,
    std::string* out_type_url, std::string* error_message) {
  HonorLivePhotoMetadata metadata;
  if (!ParseHonorTrailer(block, &metadata, error_message)) {
    return false;
  }

  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo."
        "HonorLivePhotoMetadata";
  }
  if (out_proto_bytes) {
    out_proto_bytes->resize(metadata.ByteSizeLong());
    if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                   out_proto_bytes->size())) {
      if (error_message) {
        *error_message = "Failed to serialize HonorLivePhotoMetadata proto";
      }
      return false;
    }
  }
  return true;
}

bool HonorLivePhotoProvider::IsMotionPhoto(
    const std::string& type_url, const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "HonorLivePhotoMetadata")) {
    return false;
  }
  HonorLivePhotoMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.has_embedded_video() && metadata.video_length() > 0;
}

bool HonorLivePhotoProvider::GetVideoInfo(const RawMetadataBlock& block,
                                          size_t* out_offset_in_block,
                                          size_t* out_length) const {
  HonorLivePhotoMetadata metadata;
  if (!ParseHonorTrailer(block, &metadata, nullptr)) {
    return false;
  }
  if (!metadata.has_embedded_video() || metadata.video_length() == 0) {
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
