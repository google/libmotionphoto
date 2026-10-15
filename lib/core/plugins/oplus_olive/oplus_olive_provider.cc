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

#include "plugins/oplus_olive/oplus_olive_provider.h"

#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/string_view.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "oplus_olive_metadata.pb.h"  // NOLINT

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr size_t kJxrsFooterSize = 68;
constexpr size_t kJxrsShortFooterSize = 8;

uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool HasOplusXmpMarkers(absl::string_view xml) {
  return absl::StrContains(xml, "OpCamera:") ||
         absl::StrContains(xml, "OLivePhotoVersion") ||
         absl::StrContains(xml, "ns.oplus.com/photo/1.0/camera/") ||
         absl::StrContains(xml, "ns.oppo.com/photo/1.0/camera/");
}

bool ExtractXmpInt64(absl::string_view xml, absl::string_view prop_name,
                     int64_t* out_val) {
  const std::string prefixes[] = {"OpCamera:", "GCamera:", "Camera:"};
  for (const auto& prefix : prefixes) {
    std::string needle = prefix + std::string(prop_name);
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
          if (end_quote != absl::string_view::npos &&
              absl::SimpleAtoi(xml.substr(after, end_quote - after), out_val)) {
            return true;
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
          if (absl::SimpleAtoi(inner, out_val)) {
            return true;
          }
        }
      }
      pos = xml.find(needle, pos + 1);
    }
  }
  return false;
}

bool HasIsoBmffHeader(const RawMetadataBlock& block) {
  if (block.Size() < 8) {
    return false;
  }
  const uint8_t* head8 = nullptr;
  std::vector<uint8_t> scratch;
  if (!block.ReadView(0, 8, &head8, &scratch)) {
    return false;
  }
  absl::string_view tag(reinterpret_cast<const char*>(head8 + 4), 4);
  return tag == "ftyp" || tag == "moov" || tag == "mdat" || tag == "free" ||
         tag == "skip" || tag == "wide";
}

bool HasJxrsTail(const RawMetadataBlock& block, uint32_t* out_footer_u32 = nullptr) {
  const size_t total_size = block.Size();
  if (total_size < kJxrsShortFooterSize) {
    return false;
  }
  const uint8_t* tail8 = nullptr;
  std::vector<uint8_t> scratch;
  if (!block.ReadView(total_size - 8, 8, &tail8, &scratch)) {
    return false;
  }
  absl::string_view magic(reinterpret_cast<const char*>(tail8), 4);
  if (magic != "jxrs") {
    return false;
  }
  if (out_footer_u32) {
    *out_footer_u32 = ReadLe32(tail8 + 4);
  }
  return true;
}

bool ExtractMaxJxrsEntryOffset(absl::string_view json_array,
                               size_t* out_max_offset) {
  size_t max_offset = 0;
  bool found = false;
  size_t pos = 0;
  const std::string needle = "\"offset\"";
  while ((pos = json_array.find(needle, pos)) != absl::string_view::npos) {
    size_t colon = json_array.find(':', pos + needle.size());
    if (colon == absl::string_view::npos) {
      break;
    }
    size_t num_start = colon + 1;
    while (num_start < json_array.size() &&
           (json_array[num_start] == ' ' || json_array[num_start] == '\t')) {
      ++num_start;
    }
    size_t num_end = num_start;
    while (num_end < json_array.size() && json_array[num_end] >= '0' &&
           json_array[num_end] <= '9') {
      ++num_end;
    }
    size_t val = 0;
    if (num_end > num_start &&
        absl::SimpleAtoi(json_array.substr(num_start, num_end - num_start),
                         &val)) {
      if (!found || val > max_offset) {
        max_offset = val;
        found = true;
      }
    }
    pos = num_end;
  }
  if (found && out_max_offset) {
    *out_max_offset = max_offset;
  }
  return found;
}

void PopulateFromXmp(absl::string_view xml, OplusOliveMetadata* meta) {
  int64_t version = -1;
  if (ExtractXmpInt64(xml, "OLivePhotoVersion", &version) && version > 0) {
    meta->set_olive_photo_version(static_cast<int32_t>(version));
  }
  int64_t video_len = -1;
  if (ExtractXmpInt64(xml, "VideoLength", &video_len) && video_len > 0) {
    meta->set_video_length(static_cast<uint64_t>(video_len));
  }
  int64_t ts_us = -1;
  if (ExtractXmpInt64(xml, "MotionPhotoPrimaryPresentationTimestampUs",
                      &ts_us) ||
      ExtractXmpInt64(xml, "MotionPhotoPresentationTimestampUs", &ts_us) ||
      ExtractXmpInt64(xml, "PrimaryPresentationTimestampUs", &ts_us)) {
    meta->set_primary_image_timestamp_us(ts_us);
  }
}

bool ParseOplusBlock(const RawMetadataBlock& block,
                     OplusOliveMetadata* out_meta, std::string* error_message) {
  out_meta->set_primary_image_timestamp_us(-1);

  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    if (block.Data() == nullptr || block.BufferedSize() == 0) {
      return false;
    }
    absl::string_view xml(reinterpret_cast<const char*>(block.Data()),
                          block.BufferedSize());
    if (!HasOplusXmpMarkers(xml)) {
      if (error_message) {
        *error_message = "XMP does not contain Oplus OpCamera metadata";
      }
      return false;
    }
    PopulateFromXmp(xml, out_meta);
    if (out_meta->olive_photo_version() == 0 && out_meta->video_length() > 0) {
      out_meta->set_olive_photo_version(1);
    }
    return true;
  }

  if (block.format_identifier == "container.trailer") {
    const size_t total_size = block.Size();
    uint32_t footer_u32 = 0;
    if (HasJxrsTail(block, &footer_u32)) {
      if (!HasIsoBmffHeader(block)) {
        if (error_message) {
          *error_message = "Oplus trailer with jxrs footer lacks MP4 header";
        }
        return false;
      }
      out_meta->set_has_jxrs_footer(true);
      out_meta->set_jxrs_footer_version(footer_u32);
      if (out_meta->olive_photo_version() == 0 && footer_u32 > 0) {
        out_meta->set_olive_photo_version(
            footer_u32 <= 10 ? static_cast<int32_t>(footer_u32) : 2);
      }

      const uint64_t xmp_video_len = out_meta->video_length();

      // Case A: JSON directory preceding "jxrs" + u32LE(footer_len)
      // (OPPO Reno15 Pro Max / OnePlus 11 O Live Photo v2).
      // On real devices, footer_u32 is the length of "[...]\0jxrs<u32>"
      // (so json_pos = total_size - footer_u32); also support
      // footer_u32 = strlen("[...]\0") (json_pos = total_size - 8 - footer_u32).
      if (footer_u32 >= 2 && footer_u32 <= 65536 &&
          static_cast<size_t>(footer_u32) < total_size) {
        const size_t min_cand =
            (static_cast<size_t>(footer_u32) + kJxrsShortFooterSize < total_size)
                ? (total_size - kJxrsShortFooterSize - footer_u32)
                : (total_size - footer_u32);
        const size_t tail_len = total_size - min_cand;
        const uint8_t* tail_buf = nullptr;
        std::vector<uint8_t> tail_scratch;
        if (block.ReadView(min_cand, tail_len, &tail_buf, &tail_scratch)) {
          size_t json_pos = 0;
          bool has_json_dir = false;
          const size_t cand1 = total_size - footer_u32;
          if (footer_u32 > kJxrsShortFooterSize &&
              tail_buf[cand1 - min_cand] == '[') {
            json_pos = cand1;
            has_json_dir = true;
          } else if (static_cast<size_t>(footer_u32) + kJxrsShortFooterSize <
                         total_size &&
                     tail_buf[0] == '[') {
            json_pos = min_cand;
            has_json_dir = true;
          }
          if (has_json_dir) {
            absl::string_view json_dir(
                reinterpret_cast<const char*>(tail_buf + (json_pos - min_cand)),
                total_size - kJxrsShortFooterSize - json_pos);
            size_t max_entry_offset = 0;
            if (xmp_video_len > 0 && xmp_video_len <= json_pos) {
              out_meta->set_video_offset(0);
              out_meta->set_video_length(xmp_video_len);
              return true;
            }
            if (ExtractMaxJxrsEntryOffset(json_dir, &max_entry_offset) &&
                max_entry_offset < json_pos) {
              out_meta->set_video_offset(0);
              out_meta->set_video_length(json_pos - max_entry_offset);
              return true;
            }
          }
        }
      }

      // Case B: Explicit OpCamera:VideoLength in XMP.
      if (xmp_video_len > 0 &&
          xmp_video_len <= total_size - kJxrsShortFooterSize) {
        out_meta->set_video_offset(0);
        out_meta->set_video_length(xmp_video_len);
        return true;
      }

      // Case C: Fixed 68-byte jxrs checksum footer.
      if (total_size > kJxrsFooterSize) {
        const uint8_t* ftr_buf = nullptr;
        std::vector<uint8_t> ftr_scratch;
        if (block.ReadView(total_size - kJxrsFooterSize, kJxrsFooterSize,
                           &ftr_buf, &ftr_scratch)) {
          const char* hash_ptr = reinterpret_cast<const char*>(ftr_buf);
          size_t hash_len = 60;
          while (hash_len > 0 && (hash_ptr[hash_len - 1] == '\0' ||
                                  hash_ptr[hash_len - 1] == ' ')) {
            --hash_len;
          }
          out_meta->set_jxrs_checksum_hex(std::string(hash_ptr, hash_len));
          out_meta->set_video_offset(0);
          out_meta->set_video_length(total_size - kJxrsFooterSize);
        } else {
          return false;
        }
      } else if (total_size > kJxrsShortFooterSize) {
        out_meta->set_video_offset(0);
        out_meta->set_video_length(total_size - kJxrsShortFooterSize);
      } else {
        if (error_message) {
          *error_message = "Oplus jxrs trailer has zero video bytes";
        }
        return false;
      }
      return true;
    }
  }

  if (error_message) {
    *error_message = "Unrecognized Oplus O Live Photo block";
  }
  return false;
}

}  // namespace

bool OplusOliveProvider::Identify(const RawMetadataBlock& block) {
  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    if (block.Data() == nullptr || block.BufferedSize() == 0) {
      return false;
    }
    absl::string_view xml(reinterpret_cast<const char*>(block.Data()),
                          block.BufferedSize());
    return HasOplusXmpMarkers(xml);
  }
  if (block.format_identifier == "container.trailer") {
    return HasJxrsTail(block);
  }
  return false;
}

bool OplusOliveProvider::DecodeToSemantic(const RawMetadataBlock& block,
                                          std::vector<uint8_t>* out_proto_bytes,
                                          std::string* out_type_url,
                                          std::string* error_message) {
  OplusOliveMetadata metadata;
  if (!ParseOplusBlock(block, &metadata, error_message)) {
    return false;
  }

  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo.OplusOliveMetadata";
  }
  if (out_proto_bytes) {
    out_proto_bytes->resize(metadata.ByteSizeLong());
    if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                   out_proto_bytes->size())) {
      if (error_message) {
        *error_message = "Failed to serialize OplusOliveMetadata proto";
      }
      return false;
    }
  }
  return true;
}

bool OplusOliveProvider::IsMotionPhoto(const std::string& type_url,
                                       const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "OplusOliveMetadata")) {
    return false;
  }
  OplusOliveMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.olive_photo_version() > 0 || metadata.has_jxrs_footer() ||
         metadata.video_length() > 0;
}

bool OplusOliveProvider::GetVideoInfo(const RawMetadataBlock& block,
                                      size_t* out_offset_in_block,
                                      size_t* out_length) const {
  OplusOliveMetadata metadata;
  if (!ParseOplusBlock(block, &metadata, nullptr)) {
    return false;
  }
  if (metadata.video_length() == 0) {
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
