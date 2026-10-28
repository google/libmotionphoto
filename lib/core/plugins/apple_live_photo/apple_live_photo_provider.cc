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

#include <absl/strings/match.h>
#include <absl/strings/string_view.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple_live_photo_metadata.pb.h"  // NOLINT
#include "motion_photo/metadata_block.h"

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
         tag == "free" || tag == "skip";
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

  struct TrackTiming {
    bool is_meta_handler = false;
    bool has_still_image_key = false;
    uint32_t track_timescale = 0;
    uint64_t tkhd_duration = 0;
    uint64_t elst_empty_duration = 0;
    uint32_t stts_entry_count = 0;
    uint64_t first_stts_ticks = 0;
  };
  std::vector<TrackTiming> tracks;

  while (NextBox(data, moov_size, &pos, &type, &p_start, &p_end)) {
    const size_t p_len = p_end - p_start;
    if (type == "mvhd" && p_len >= 20) {
      uint8_t version = data[p_start];
      if (version == 0) {
        movie_timescale = ReadBe32(data + p_start + 12);
      } else if (p_len >= 24) {
        movie_timescale = ReadBe32(data + p_start + 20);
      }
    } else if (type == "trak") {
      TrackTiming trk;
      size_t tpos = p_start;
      absl::string_view ttype;
      size_t tp_start = 0;
      size_t tp_end = 0;
      while (NextBox(data, p_end, &tpos, &ttype, &tp_start, &tp_end)) {
        const size_t tp_len = tp_end - tp_start;
        if (ttype == "tkhd" && tp_len >= 24) {
          uint8_t version = data[tp_start];
          if (version == 0) {
            trk.tkhd_duration = ReadBe32(data + tp_start + 20);
          } else if (tp_len >= 36) {
            trk.tkhd_duration = ReadBe64(data + tp_start + 28);
          }
        } else if (ttype == "edts") {
          size_t epos = tp_start;
          absl::string_view etype;
          size_t ep_start = 0;
          size_t ep_end = 0;
          while (NextBox(data, tp_end, &epos, &etype, &ep_start, &ep_end)) {
            if (etype == "elst" && ep_end - ep_start >= 8) {
              uint8_t version = data[ep_start];
              uint32_t count = ReadBe32(data + ep_start + 4);
              size_t entry_size = (version == 1) ? 20 : 12;
              size_t cur = ep_start + 8;
              for (uint32_t i = 0;
                   i < count && cur <= ep_end && entry_size <= ep_end - cur;
                   ++i, cur += entry_size) {
                uint64_t seg_dur = (version == 1) ? ReadBe64(data + cur)
                                                  : ReadBe32(data + cur);
                int64_t media_time =
                    (version == 1)
                        ? static_cast<int64_t>(ReadBe64(data + cur + 8))
                        : static_cast<int64_t>(
                              static_cast<int32_t>(ReadBe32(data + cur + 4)));
                if (media_time == -1) {
                  trk.elst_empty_duration += seg_dur;
                } else {
                  break;
                }
              }
            }
          }
        } else if (ttype == "mdia") {
          size_t mpos = tp_start;
          absl::string_view mtype;
          size_t mp_start = 0;
          size_t mp_end = 0;
          while (NextBox(data, tp_end, &mpos, &mtype, &mp_start, &mp_end)) {
            const size_t mp_len = mp_end - mp_start;
            if (mtype == "mdhd" && mp_len >= 20) {
              uint8_t version = data[mp_start];
              if (version == 0) {
                trk.track_timescale = ReadBe32(data + mp_start + 12);
              } else if (mp_len >= 24) {
                trk.track_timescale = ReadBe32(data + mp_start + 20);
              }
            } else if (mtype == "hdlr" && mp_len >= 12) {
              absl::string_view hdlr_sub(
                  reinterpret_cast<const char*>(data + mp_start + 8), 4);
              if (hdlr_sub == "meta") {
                trk.is_meta_handler = true;
              }
            } else if (mtype == "minf") {
              size_t ipos = mp_start;
              absl::string_view itype;
              size_t ip_start = 0;
              size_t ip_end = 0;
              while (NextBox(data, mp_end, &ipos, &itype, &ip_start, &ip_end)) {
                if (itype == "stbl") {
                  size_t spos = ip_start;
                  absl::string_view stype;
                  size_t sp_start = 0;
                  size_t sp_end = 0;
                  while (NextBox(data, ip_end, &spos, &stype, &sp_start,
                                 &sp_end)) {
                    if (stype == "stsd" && sp_end - sp_start >= 8) {
                      uint32_t count = ReadBe32(data + sp_start + 4);
                      size_t epos = sp_start + 8;
                      for (uint32_t i = 0; i < count; ++i) {
                        absl::string_view fmt;
                        size_t ep_start = 0;
                        size_t ep_end = 0;
                        if (!NextBox(data, sp_end, &epos, &fmt, &ep_start,
                                     &ep_end)) {
                          break;
                        }
                        if (fmt == "mebx") {
                          absl::string_view payload_view(
                              reinterpret_cast<const char*>(data + ep_start),
                              ep_end - ep_start);
                          if (absl::StrContains(payload_view,
                                                kStillImageTimeKey)) {
                            trk.has_still_image_key = true;
                          }
                        }
                      }
                    } else if (stype == "stts" && sp_end - sp_start >= 16) {
                      trk.stts_entry_count = ReadBe32(data + sp_start + 4);
                      if (trk.stts_entry_count > 0) {
                        uint32_t sc = ReadBe32(data + sp_start + 8);
                        uint32_t sd = ReadBe32(data + sp_start + 12);
                        trk.first_stts_ticks = static_cast<uint64_t>(sc) * sd;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      tracks.push_back(trk);
    }
  }

  for (const auto& trk : tracks) {
    if (!trk.is_meta_handler || !trk.has_still_image_key) {
      continue;
    }
    int64_t ts_us = -1;
    if (trk.elst_empty_duration > 0 && movie_timescale > 0) {
      ts_us = static_cast<int64_t>((trk.elst_empty_duration * 1000000ULL) /
                                   movie_timescale);
    } else if (trk.stts_entry_count > 1 && trk.track_timescale > 0) {
      ts_us = static_cast<int64_t>((trk.first_stts_ticks * 1000000ULL) /
                                   trk.track_timescale);
    } else if (trk.tkhd_duration > 1 && movie_timescale > 0) {
      ts_us = static_cast<int64_t>(((trk.tkhd_duration - 1) * 1000000ULL) /
                                   movie_timescale);
    }
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

  size_t pos = 0;
  while (pos <= total_size && total_size - pos >= 8) {
    const uint8_t* hdr = nullptr;
    std::vector<uint8_t> hdr_scratch;
    if (!block.ReadView(pos, 8, &hdr, &hdr_scratch)) {
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
      if (total_size - pos < 16 ||
          !block.ReadView(pos, 16, &hdr, &hdr_scratch)) {
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
  FILE* fp = std::fopen(file_path.c_str(), "rb");
  if (!fp) {
    return -1;
  }
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    return -1;
  }
  long signed_size = std::ftell(fp);
  if (signed_size < 16) {
    std::fclose(fp);
    return -1;
  }
  const size_t total_size = static_cast<size_t>(signed_size);
  size_t pos = 0;
  while (pos <= total_size && total_size - pos >= 8) {
    if (std::fseek(fp, static_cast<long>(pos), SEEK_SET) != 0) {
      break;
    }
    uint8_t hdr[16] = {0};
    if (std::fread(hdr, 1, 8, fp) != 8) {
      break;
    }
    absl::string_view tag(reinterpret_cast<const char*>(hdr + 4), 4);
    if (pos == 0 && !IsTopLevelIsoBmffTag(tag)) {
      std::fclose(fp);
      return -1;
    }
    uint64_t box_size = ReadBe32(hdr);
    size_t header_size = 8;
    if (box_size == 1) {
      if (total_size - pos < 16 || std::fread(hdr + 8, 1, 8, fp) != 8) {
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
    if (tag == "moov") {
      size_t moov_payload_size = static_cast<size_t>(box_size) - header_size;
      if (moov_payload_size > kMaxMoovSize) {
        std::fclose(fp);
        return -1;
      }
      std::vector<uint8_t> moov_buf(moov_payload_size);
      if (std::fread(moov_buf.data(), 1, moov_payload_size, fp) !=
          moov_payload_size) {
        std::fclose(fp);
        return -1;
      }
      std::fclose(fp);
      AppleLivePhotoMetadata metadata;
      if (!ParseMoovPayload(moov_buf.data(), moov_buf.size(), total_size,
                            &metadata, nullptr) ||
          !metadata.has_still_image_time_track()) {
        return -1;
      }
      return metadata.primary_image_timestamp_us();
    }
    pos += static_cast<size_t>(box_size);
  }
  std::fclose(fp);
  return -1;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
