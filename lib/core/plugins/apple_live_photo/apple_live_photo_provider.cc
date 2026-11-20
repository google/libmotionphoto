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

#include "plugins/apple_live_photo/apple_live_photo_provider.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "apple_live_photo_metadata.pb.h"  // NOLINT
#include "motion_photo/metadata_block.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/motion_photo_utils.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr absl::string_view kStillImageTimeKey =
    "com.apple.quicktime.still-image-time";
constexpr size_t kMaxMoovSize = 16 * 1024 * 1024;

uint32_t ReadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t ReadBe64(const uint8_t* p) {
  return (static_cast<uint64_t>(ReadBe32(p)) << 32) | ReadBe32(p + 4);
}

bool IsTopLevelIsoBmffTag(absl::string_view tag) {
  return tag == "ftyp" || tag == "wide" || tag == "mdat" || tag == "moov" ||
         tag == "free" || tag == "skip" || tag == "pnot" || tag == "uuid";
}

int64_t SafeScaleTicksToUs(uint64_t ticks, uint32_t timescale) {
  if (timescale == 0) {
    return -1;
  }
  const uint64_t whole_seconds = ticks / timescale;
  const uint64_t max_seconds =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / 1000000ULL;
  if (whole_seconds > max_seconds) {
    return -1;
  }
  const uint64_t rem_ticks = ticks % timescale;
  const uint64_t rem_us = (rem_ticks * 1000000ULL) / timescale;
  const uint64_t base_us = whole_seconds * 1000000ULL;
  if (rem_us >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - base_us) {
    return -1;
  }
  return static_cast<int64_t>(base_us + rem_us);
}

bool NextBox(const uint8_t* data, size_t end, size_t* pos,
             absl::string_view* out_type, size_t* out_payload_start,
             size_t* out_payload_end) {
  if (*pos > end || end - *pos < 8) {
    return false;
  }
  uint64_t size = ReadBe32(data + *pos);
  size_t header_size = 8;
  if (size == 1) {
    if (end - *pos < 16) {
      return false;
    }
    size = ReadBe64(data + *pos + 8);
    header_size = 16;
  } else if (size == 0) {
    size = end - *pos;
  }
  if (size < header_size || size > end - *pos) {
    return false;
  }
  *out_type =
      absl::string_view(reinterpret_cast<const char*>(data + *pos + 4), 4);
  *out_payload_start = *pos + header_size;
  *out_payload_end = *pos + static_cast<size_t>(size);
  *pos += static_cast<size_t>(size);
  return true;
}

struct TrackTiming {
  bool has_invalid_box = false;
  bool is_meta_handler = false;
  bool has_still_image_key = false;
  uint32_t track_timescale = 0;
  uint64_t tkhd_duration = 0;
  uint64_t elst_empty_duration = 0;
  uint32_t stts_entry_count = 0;
  uint64_t first_stts_ticks = 0;
};

bool ParseMvhdTimescale(const uint8_t* data, size_t len,
                        uint32_t* out_timescale) {
  if (len < 1) return false;
  const uint8_t version = data[0];
  if (version == 0) {
    if (len < 20) return false;
    *out_timescale = ReadBe32(data + 12);
    return true;
  }
  if (version == 1) {
    if (len < 24) return false;
    *out_timescale = ReadBe32(data + 20);
    return true;
  }
  return false;
}

void ParseTkhdDuration(const uint8_t* data, size_t len, TrackTiming* trk) {
  if (len < 1) return;
  const uint8_t version = data[0];
  if (version == 0) {
    if (len >= 24) {
      trk->tkhd_duration = ReadBe32(data + 20);
    }
    return;
  }
  if (version == 1) {
    if (len >= 36) {
      trk->tkhd_duration = ReadBe64(data + 28);
    }
    return;
  }
  trk->has_invalid_box = true;
}

void ParseMdhdTimescale(const uint8_t* data, size_t len, TrackTiming* trk) {
  if (len < 1) return;
  const uint8_t version = data[0];
  if (version == 0) {
    if (len >= 20) {
      trk->track_timescale = ReadBe32(data + 12);
    }
    return;
  }
  if (version == 1) {
    if (len >= 24) {
      trk->track_timescale = ReadBe32(data + 20);
    }
    return;
  }
  trk->has_invalid_box = true;
}

void ParseElstBox(const uint8_t* data, size_t len, TrackTiming* trk) {
  if (len < 8) return;
  const uint8_t version = data[0];
  if (version != 0 && version != 1) {
    trk->has_invalid_box = true;
    return;
  }
  const uint32_t count = ReadBe32(data + 4);
  const size_t entry_size = (version == 1) ? 20 : 12;
  if ((len - 8) / entry_size < count) {
    trk->has_invalid_box = true;
    return;
  }
  size_t cur = 8;
  uint64_t empty_dur = 0;
  for (uint32_t i = 0; i < count; ++i, cur += entry_size) {
    const uint64_t seg_dur =
        (version == 1) ? ReadBe64(data + cur) : ReadBe32(data + cur);
    const int64_t media_time =
        (version == 1) ? static_cast<int64_t>(ReadBe64(data + cur + 8))
                       : static_cast<int64_t>(
                             static_cast<int32_t>(ReadBe32(data + cur + 4)));
    if (media_time != -1) {
      break;
    }
    if (seg_dur > std::numeric_limits<uint64_t>::max() - empty_dur) {
      trk->has_invalid_box = true;
      return;
    }
    empty_dur += seg_dur;
  }
  trk->elst_empty_duration = empty_dur;
}

void ParseEdtsBox(const uint8_t* data, size_t start, size_t end,
                  TrackTiming* trk) {
  size_t pos = start;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  while (NextBox(data, end, &pos, &type, &p_start, &p_end)) {
    if (type == "elst") {
      ParseElstBox(data + p_start, p_end - p_start, trk);
    }
  }
}

bool ParseStsdForStillImageKey(const uint8_t* data, size_t len) {
  if (len < 8) return false;
  const uint32_t count = ReadBe32(data + 4);
  size_t pos = 8;
  for (uint32_t i = 0; i < count; ++i) {
    absl::string_view fmt;
    size_t p_start = 0;
    size_t p_end = 0;
    if (!NextBox(data, len, &pos, &fmt, &p_start, &p_end)) {
      break;
    }
    if (fmt == "mebx") {
      absl::string_view payload(reinterpret_cast<const char*>(data + p_start),
                                p_end - p_start);
      if (absl::StrContains(payload, kStillImageTimeKey)) {
        return true;
      }
    }
  }
  return false;
}

void ParseSttsFirstEntry(const uint8_t* data, size_t len, TrackTiming* trk) {
  if (len < 8) return;
  const uint32_t entry_count = ReadBe32(data + 4);
  if (entry_count == 0 || (len - 8) / 8 < entry_count) {
    return;
  }
  const uint32_t sc = ReadBe32(data + 8);
  const uint32_t sd = ReadBe32(data + 12);
  if (sc > 0 && sd > 0) {
    trk->stts_entry_count = entry_count;
    trk->first_stts_ticks = static_cast<uint64_t>(sc) * sd;
  }
}

void ParseStblBox(const uint8_t* data, size_t start, size_t end,
                  TrackTiming* trk) {
  size_t pos = start;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  while (NextBox(data, end, &pos, &type, &p_start, &p_end)) {
    if (type == "stsd" &&
        ParseStsdForStillImageKey(data + p_start, p_end - p_start)) {
      trk->has_still_image_key = true;
    } else if (type == "stts") {
      ParseSttsFirstEntry(data + p_start, p_end - p_start, trk);
    }
  }
}

void ParseMinfBox(const uint8_t* data, size_t start, size_t end,
                  TrackTiming* trk) {
  size_t pos = start;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  while (NextBox(data, end, &pos, &type, &p_start, &p_end)) {
    if (type == "stbl") {
      ParseStblBox(data, p_start, p_end, trk);
    }
  }
}

void ParseMdiaBox(const uint8_t* data, size_t start, size_t end,
                  TrackTiming* trk) {
  size_t pos = start;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  while (NextBox(data, end, &pos, &type, &p_start, &p_end)) {
    const size_t p_len = p_end - p_start;
    if (type == "mdhd") {
      ParseMdhdTimescale(data + p_start, p_len, trk);
    } else if (type == "hdlr" && p_len >= 12) {
      absl::string_view hdlr_sub(
          reinterpret_cast<const char*>(data + p_start + 8), 4);
      if (hdlr_sub == "meta") {
        trk->is_meta_handler = true;
      }
    } else if (type == "minf") {
      ParseMinfBox(data, p_start, p_end, trk);
    }
  }
}

TrackTiming ParseTrakBox(const uint8_t* data, size_t start, size_t end) {
  TrackTiming trk;
  size_t pos = start;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  while (NextBox(data, end, &pos, &type, &p_start, &p_end)) {
    if (type == "tkhd") {
      ParseTkhdDuration(data + p_start, p_end - p_start, &trk);
    } else if (type == "edts") {
      ParseEdtsBox(data, p_start, p_end, &trk);
    } else if (type == "mdia") {
      ParseMdiaBox(data, p_start, p_end, &trk);
    }
  }
  return trk;
}

int64_t ResolveTrackTimestampUs(const TrackTiming& trk,
                                uint32_t movie_timescale) {
  if (trk.has_invalid_box || !trk.is_meta_handler || !trk.has_still_image_key) {
    return -1;
  }
  if (trk.elst_empty_duration > 0 && movie_timescale > 0) {
    return SafeScaleTicksToUs(trk.elst_empty_duration, movie_timescale);
  }
  if (trk.stts_entry_count > 1 && trk.first_stts_ticks > 0 &&
      trk.track_timescale > 0) {
    return SafeScaleTicksToUs(trk.first_stts_ticks, trk.track_timescale);
  }
  if (trk.tkhd_duration >= 1 && movie_timescale > 0) {
    return SafeScaleTicksToUs(trk.tkhd_duration - 1, movie_timescale);
  }
  return -1;
}

bool ParseMoovPayload(const uint8_t* data, size_t moov_size,
                      size_t total_stream_size,
                      AppleLivePhotoMetadata* out_meta,
                      std::string* error_message) {
  out_meta->set_primary_image_timestamp_us(-1);
  out_meta->set_has_still_image_time_track(false);

  uint32_t movie_timescale = 0;
  size_t pos = 0;
  absl::string_view type;
  size_t p_start = 0;
  size_t p_end = 0;
  std::vector<TrackTiming> tracks;

  while (NextBox(data, moov_size, &pos, &type, &p_start, &p_end)) {
    if (type == "mvhd") {
      if (!ParseMvhdTimescale(data + p_start, p_end - p_start,
                              &movie_timescale)) {
        if (error_message) {
          *error_message = "Unsupported or truncated mvhd box";
        }
        return false;
      }
    } else if (type == "trak") {
      tracks.push_back(ParseTrakBox(data, p_start, p_end));
    }
  }

  for (const auto& trk : tracks) {
    const int64_t ts_us = ResolveTrackTimestampUs(trk, movie_timescale);
    if (ts_us >= 0) {
      out_meta->set_has_still_image_time_track(true);
      out_meta->set_primary_image_timestamp_us(ts_us);
      out_meta->set_movie_timescale(movie_timescale);
      out_meta->set_video_offset(0);
      out_meta->set_video_length(total_stream_size);
      return true;
    }
  }

  if (error_message) {
    *error_message =
        "Apple QuickTime still-image-time track not found in .mov stream";
  }
  return false;
}

bool IsQuickTimeFtyp(const RawMetadataBlock& block, size_t payload_offset,
                     size_t payload_size, std::vector<uint8_t>* scratch) {
  if (payload_size < 4) {
    return false;
  }
  const size_t check_len = std::min<size_t>(payload_size, 4096);
  const uint8_t* ftyp = nullptr;
  if (!block.ReadView(payload_offset, check_len, &ftyp, scratch)) {
    return false;
  }
  if (absl::string_view(reinterpret_cast<const char*>(ftyp), 4) == "qt  ") {
    return true;
  }
  for (size_t off = 8; off + 4 <= check_len; off += 4) {
    if (absl::string_view(reinterpret_cast<const char*>(ftyp + off), 4) ==
        "qt  ") {
      return true;
    }
  }
  return false;
}

bool ParseAppleLivePhotoBlock(const RawMetadataBlock& block,
                              AppleLivePhotoMetadata* out_meta,
                              std::string* error_message) {
  const size_t total_size = block.Size();
  if (total_size < 16) {
    if (error_message) {
      *error_message = "Apple Live Photo stream is shorter than 16 bytes";
    }
    return false;
  }

  std::vector<uint8_t> scratch;
  size_t pos = 0;
  while (pos <= total_size && total_size - pos >= 8) {
    const size_t hdr_read_len = std::min<size_t>(16, total_size - pos);
    const uint8_t* hdr = nullptr;
    if (!block.ReadView(pos, hdr_read_len, &hdr, &scratch)) {
      break;
    }
    absl::string_view tag(reinterpret_cast<const char*>(hdr + 4), 4);
    if (pos == 0 && !IsTopLevelIsoBmffTag(tag)) {
      if (error_message) {
        *error_message = "Stream does not start with a top-level QuickTime box";
      }
      return false;
    }
    uint64_t box_size = ReadBe32(hdr);
    size_t header_size = 8;
    if (box_size == 1) {
      if (hdr_read_len < 16) {
        break;
      }
      box_size = ReadBe64(hdr + 8);
      header_size = 16;
    } else if (box_size == 0) {
      box_size = total_size - pos;
    }
    if (box_size < header_size || box_size > total_size - pos) {
      break;
    }
    if (pos == 0 && tag == "ftyp" &&
        !IsQuickTimeFtyp(block, pos + header_size,
                         static_cast<size_t>(box_size) - header_size,
                         &scratch)) {
      if (error_message) {
        *error_message = "ftyp box does not declare QuickTime qt brand";
      }
      return false;
    }
    if (tag == "moov") {
      size_t moov_payload_size = static_cast<size_t>(box_size) - header_size;
      if (moov_payload_size > kMaxMoovSize) {
        if (error_message) {
          *error_message = "moov box exceeds maximum allowed size";
        }
        return false;
      }
      const uint8_t* moov_ptr = nullptr;
      std::vector<uint8_t> moov_scratch;
      if (!block.ReadView(pos + header_size, moov_payload_size, &moov_ptr,
                          &moov_scratch)) {
        if (error_message) {
          *error_message = "Failed to read moov payload";
        }
        return false;
      }
      return ParseMoovPayload(moov_ptr, moov_payload_size, total_size, out_meta,
                              error_message);
    }
    pos += static_cast<size_t>(box_size);
  }

  if (error_message) {
    *error_message = "No moov box found in QuickTime stream";
  }
  return false;
}

}  // namespace

bool AppleLivePhotoProvider::Identify(const RawMetadataBlock& block) {
  if (block.format_identifier != "container.trailer" &&
      block.format_identifier != "video.mov") {
    return false;
  }
  AppleLivePhotoMetadata meta;
  return ParseAppleLivePhotoBlock(block, &meta, nullptr) &&
         meta.has_still_image_time_track();
}

bool AppleLivePhotoProvider::DecodeToSemantic(
    const RawMetadataBlock& block, std::vector<uint8_t>* out_proto_bytes,
    std::string* out_type_url, std::string* error_message) {
  AppleLivePhotoMetadata metadata;
  if (!ParseAppleLivePhotoBlock(block, &metadata, error_message)) {
    return false;
  }
  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo."
        "AppleLivePhotoMetadata";
  }
  if (out_proto_bytes) {
    out_proto_bytes->resize(metadata.ByteSizeLong());
    if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                   out_proto_bytes->size())) {
      if (error_message) {
        *error_message = "Failed to serialize AppleLivePhotoMetadata proto";
      }
      return false;
    }
  }
  return true;
}

bool AppleLivePhotoProvider::IsMotionPhoto(
    const std::string& type_url, const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "AppleLivePhotoMetadata")) {
    return false;
  }
  AppleLivePhotoMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.has_still_image_time_track() && metadata.video_length() > 0;
}

bool AppleLivePhotoProvider::GetVideoInfo(const RawMetadataBlock& block,
                                          size_t* out_offset_in_block,
                                          size_t* out_length) const {
  AppleLivePhotoMetadata metadata;
  if (!ParseAppleLivePhotoBlock(block, &metadata, nullptr) ||
      !metadata.has_still_image_time_track() || metadata.video_length() == 0) {
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

// static
int64_t AppleLivePhotoProvider::ExtractStillImageTimestampUs(
    const RawMetadataBlock& block) {
  AppleLivePhotoMetadata metadata;
  if (!ParseAppleLivePhotoBlock(block, &metadata, nullptr) ||
      !metadata.has_still_image_time_track()) {
    return -1;
  }
  return metadata.primary_image_timestamp_us();
}

// static
int64_t AppleLivePhotoProvider::ExtractStillImageTimestampUs(
    const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return -1;
  }
  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "video.mov";
  block.data_view = data;
  block.data_view_size = size;
  block.total_payload_size = size;
  return ExtractStillImageTimestampUs(block);
}

// static
int64_t AppleLivePhotoProvider::ExtractStillImageTimestampUsFromFile(
    const std::string& file_path) {
  auto source = MediaSource::FromFile(file_path, nullptr);
  if (!source || source->GetSize() < 16) {
    return -1;
  }
  MediaSourceDataSource data_source(*source);
  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "video.mov";
  block.total_payload_size = source->GetSize();
  block.data_source = &data_source;
  return ExtractStillImageTimestampUs(block);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
