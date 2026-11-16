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

#include "motion_photo_mp4/mp4_mett_box_injector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "motion_photo/video_track_data.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr size_t kMaxMoovSize = 64 * 1024 * 1024;
constexpr uint32_t kMetadataTimescaleHz = 1000;
constexpr uint16_t kUndeterminedLanguageCode = 0x55C4;

struct BoxSlice {
  size_t offset = 0;
  size_t size = 0;
  size_t header_size = 8;
  char type[4] = {};

  std::string_view TypeView() const { return std::string_view(type, 4); }
  size_t PayloadOffset() const { return offset + header_size; }
  size_t PayloadSize() const { return size - header_size; }
};

struct MvhdInfo {
  bool found = false;
  uint32_t creation_time = 0;
  uint32_t modification_time = 0;
  uint32_t next_track_id = 0;
  size_t next_track_id_offset_in_payload = 0;
};

struct MoovChildInfo {
  BoxSlice box;
  bool is_mvhd = false;
  bool is_trak = false;
  bool strip_trak = false;
  uint32_t track_id = 0;
};

uint32_t ReadBigEndianUInt32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t ReadBigEndianUInt64(const uint8_t* p) {
  return (static_cast<uint64_t>(ReadBigEndianUInt32(p)) << 32) |
         ReadBigEndianUInt32(p + 4);
}

void WriteBigEndianUInt32(uint32_t value, uint8_t* dest) {
  dest[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  dest[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  dest[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  dest[3] = static_cast<uint8_t>(value & 0xFF);
}

void AppendBigEndianUInt16(uint16_t value, std::vector<uint8_t>* out) {
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendBigEndianUInt32(uint32_t value, std::vector<uint8_t>* out) {
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendBigEndianUInt64(uint64_t value, std::vector<uint8_t>* out) {
  AppendBigEndianUInt32(static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu),
                        out);
  AppendBigEndianUInt32(static_cast<uint32_t>(value & 0xFFFFFFFFu), out);
}

void AppendFourCC(const char fourcc[4], std::vector<uint8_t>* out) {
  out->insert(out->end(), fourcc, fourcc + 4);
}

void AppendBox(const char fourcc[4], const std::vector<uint8_t>& payload,
               std::vector<uint8_t>* out) {
  uint64_t total_size = 8 + static_cast<uint64_t>(payload.size());
  if (total_size <= UINT32_MAX) {
    AppendBigEndianUInt32(static_cast<uint32_t>(total_size), out);
    AppendFourCC(fourcc, out);
  } else {
    AppendBigEndianUInt32(1, out);
    AppendFourCC(fourcc, out);
    AppendBigEndianUInt64(16 + static_cast<uint64_t>(payload.size()), out);
  }
  out->insert(out->end(), payload.begin(), payload.end());
}

bool ReadBoxSlice(const uint8_t* data, size_t pos, size_t end, BoxSlice* box) {
  if (pos > end || end - pos < 8) {
    return false;
  }
  uint32_t size32 = ReadBigEndianUInt32(data + pos);
  uint64_t box_size = size32;
  size_t header_size = 8;
  if (size32 == 1) {
    if (end - pos < 16) {
      return false;
    }
    box_size = ReadBigEndianUInt64(data + pos + 8);
    header_size = 16;
  } else if (size32 == 0) {
    box_size = end - pos;
  }
  if (box_size < header_size || box_size > end - pos) {
    return false;
  }
  box->offset = pos;
  box->size = static_cast<size_t>(box_size);
  box->header_size = header_size;
  std::memcpy(box->type, data + pos + 4, 4);
  return true;
}

bool ParseMvhdBox(const uint8_t* moov_data, const BoxSlice& mvhd_box,
                  MvhdInfo* mvhd_info) {
  const uint8_t* payload = moov_data + mvhd_box.PayloadOffset();
  size_t payload_size = mvhd_box.PayloadSize();
  if (payload_size < 100) {
    return false;
  }
  uint8_t version = payload[0];
  if (version == 0) {
    mvhd_info->creation_time = ReadBigEndianUInt32(payload + 4);
    mvhd_info->modification_time = ReadBigEndianUInt32(payload + 8);
    mvhd_info->next_track_id_offset_in_payload = 96;
    mvhd_info->next_track_id = ReadBigEndianUInt32(payload + 96);
    mvhd_info->found = true;
    return true;
  }
  if (version == 1) {
    if (payload_size < 112) {
      return false;
    }
    uint64_t ctime64 = ReadBigEndianUInt64(payload + 4);
    uint64_t mtime64 = ReadBigEndianUInt64(payload + 12);
    mvhd_info->creation_time =
        ctime64 <= UINT32_MAX ? static_cast<uint32_t>(ctime64) : 0;
    mvhd_info->modification_time =
        mtime64 <= UINT32_MAX ? static_cast<uint32_t>(mtime64) : 0;
    mvhd_info->next_track_id_offset_in_payload = 108;
    mvhd_info->next_track_id = ReadBigEndianUInt32(payload + 108);
    mvhd_info->found = true;
    return true;
  }
  return false;
}

void InspectStsdBox(const uint8_t* moov_data, const BoxSlice& stsd_box,
                    bool* has_motion_photo_mett_mime, bool* is_agtm_track) {
  const uint8_t* payload = moov_data + stsd_box.PayloadOffset();
  size_t payload_size = stsd_box.PayloadSize();
  if (payload_size < 8) {
    return;
  }
  uint32_t entry_count = ReadBigEndianUInt32(payload + 4);
  size_t pos = stsd_box.PayloadOffset() + 8;
  size_t end = stsd_box.offset + stsd_box.size;
  for (uint32_t i = 0; i < entry_count && pos <= end && end - pos >= 8; ++i) {
    uint32_t entry_size = ReadBigEndianUInt32(moov_data + pos);
    if (entry_size < 8 || entry_size > end - pos) {
      break;
    }
    std::string_view entry_format(
        reinterpret_cast<const char*>(moov_data + pos + 4), 4);
    std::string_view entry_payload(
        reinterpret_cast<const char*>(moov_data + pos + 8), entry_size - 8);
    if (entry_format == "it35") {
      *is_agtm_track = true;
    } else if (entry_format == "mett" || entry_format == "mp4s") {
      if (entry_payload.find(std::string_view("\xb5\x00\x90", 3)) !=
              std::string_view::npos ||
          entry_payload.find("it35") != std::string_view::npos ||
          entry_payload.find("agtm") != std::string_view::npos ||
          entry_payload.find("AGTM") != std::string_view::npos) {
        *is_agtm_track = true;
      }
      if (entry_payload.find(kMotionPhotoMetadataMime) !=
          std::string_view::npos) {
        *has_motion_photo_mett_mime = true;
      }
    }
    pos += entry_size;
  }
}

bool InspectTrakBox(const uint8_t* moov_data, const BoxSlice& trak_box,
                    uint32_t* track_id, bool* should_strip) {
  *track_id = 0;
  *should_strip = false;
  std::string_view handler_type;
  bool has_motion_photo_mett_mime = false;
  bool is_agtm_track = false;

  size_t pos = trak_box.PayloadOffset();
  size_t end = trak_box.offset + trak_box.size;
  while (pos < end) {
    BoxSlice child;
    if (!ReadBoxSlice(moov_data, pos, end, &child)) {
      return false;
    }
    if (child.TypeView() == "tkhd") {
      if (child.PayloadSize() < 20) {
        return false;
      }
      const uint8_t* tkhd = moov_data + child.PayloadOffset();
      uint8_t version = tkhd[0];
      if (version == 0) {
        *track_id = ReadBigEndianUInt32(tkhd + 12);
      } else if (version == 1 && child.PayloadSize() >= 28) {
        *track_id = ReadBigEndianUInt32(tkhd + 20);
      } else {
        return false;
      }
    } else if (child.TypeView() == "mdia") {
      size_t mdia_pos = child.PayloadOffset();
      size_t mdia_end = child.offset + child.size;
      while (mdia_pos < mdia_end) {
        BoxSlice mdia_child;
        if (!ReadBoxSlice(moov_data, mdia_pos, mdia_end, &mdia_child)) {
          return false;
        }
        if (mdia_child.TypeView() == "hdlr" && mdia_child.PayloadSize() >= 12) {
          handler_type =
              std::string_view(reinterpret_cast<const char*>(
                                   moov_data + mdia_child.PayloadOffset() + 8),
                               4);
        } else if (mdia_child.TypeView() == "minf") {
          size_t minf_pos = mdia_child.PayloadOffset();
          size_t minf_end = mdia_child.offset + mdia_child.size;
          while (minf_pos < minf_end) {
            BoxSlice minf_child;
            if (!ReadBoxSlice(moov_data, minf_pos, minf_end, &minf_child)) {
              return false;
            }
            if (minf_child.TypeView() == "stbl") {
              size_t stbl_pos = minf_child.PayloadOffset();
              size_t stbl_end = minf_child.offset + minf_child.size;
              while (stbl_pos < stbl_end) {
                BoxSlice stbl_child;
                if (!ReadBoxSlice(moov_data, stbl_pos, stbl_end, &stbl_child)) {
                  return false;
                }
                if (stbl_child.TypeView() == "stsd") {
                  InspectStsdBox(moov_data, stbl_child,
                                 &has_motion_photo_mett_mime, &is_agtm_track);
                }
                stbl_pos += stbl_child.size;
              }
            }
            minf_pos += minf_child.size;
          }
        }
        mdia_pos += mdia_child.size;
      }
    }
    pos += child.size;
  }

  if (*track_id == 0 || handler_type.empty()) {
    return false;
  }
  if (handler_type == kVideoMetadataTrackType && !is_agtm_track &&
      has_motion_photo_mett_mime) {
    *should_strip = true;
  }
  return true;
}

bool ShiftChunkOffset(uint64_t orig_offset, size_t old_moov_end,
                      int64_t delta_moov, uint64_t* shifted_offset) {
  if (orig_offset < old_moov_end) {
    *shifted_offset = orig_offset;
    return true;
  }
  if (delta_moov >= 0) {
    uint64_t add = static_cast<uint64_t>(delta_moov);
    if (orig_offset > UINT64_MAX - add) {
      return false;
    }
    *shifted_offset = orig_offset + add;
    return true;
  }
  uint64_t sub = static_cast<uint64_t>(-delta_moov);
  if (orig_offset < sub) {
    return false;
  }
  *shifted_offset = orig_offset - sub;
  return true;
}

bool RebuildChunkOffsetBox(const uint8_t* moov_data, const BoxSlice& box,
                           bool is_co64, size_t old_moov_end,
                           int64_t delta_moov, std::vector<uint8_t>* out) {
  const uint8_t* payload = moov_data + box.PayloadOffset();
  size_t payload_size = box.PayloadSize();
  if (payload_size < 8) {
    return false;
  }
  uint32_t version_flags = ReadBigEndianUInt32(payload);
  uint32_t entry_count = ReadBigEndianUInt32(payload + 4);
  size_t entry_width = is_co64 ? 8 : 4;
  if (entry_count > (payload_size - 8) / entry_width) {
    return false;
  }

  std::vector<uint64_t> offsets;
  offsets.reserve(entry_count);
  bool requires_co64 = is_co64;
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* entry_ptr =
        payload + 8 + static_cast<size_t>(i) * entry_width;
    uint64_t orig_offset = is_co64 ? ReadBigEndianUInt64(entry_ptr)
                                   : ReadBigEndianUInt32(entry_ptr);
    uint64_t new_offset = 0;
    if (!ShiftChunkOffset(orig_offset, old_moov_end, delta_moov, &new_offset)) {
      return false;
    }
    if (new_offset > UINT32_MAX) {
      requires_co64 = true;
    }
    offsets.push_back(new_offset);
  }

  std::vector<uint8_t> new_payload;
  new_payload.reserve(8 + static_cast<size_t>(entry_count) *
                              (requires_co64 ? 8 : 4));
  AppendBigEndianUInt32(version_flags, &new_payload);
  AppendBigEndianUInt32(entry_count, &new_payload);
  if (requires_co64) {
    for (uint64_t off : offsets) {
      AppendBigEndianUInt64(off, &new_payload);
    }
    AppendBox("co64", new_payload, out);
  } else {
    for (uint64_t off : offsets) {
      AppendBigEndianUInt32(static_cast<uint32_t>(off), &new_payload);
    }
    AppendBox("stco", new_payload, out);
  }
  return true;
}

bool RebuildStblBox(const uint8_t* moov_data, const BoxSlice& stbl_box,
                    size_t old_moov_end, int64_t delta_moov,
                    std::vector<uint8_t>* out) {
  std::vector<uint8_t> payload;
  payload.reserve(stbl_box.PayloadSize() + 16);
  size_t pos = stbl_box.PayloadOffset();
  size_t end = stbl_box.offset + stbl_box.size;
  while (pos < end) {
    BoxSlice child;
    if (!ReadBoxSlice(moov_data, pos, end, &child)) {
      return false;
    }
    if (child.TypeView() == "stco") {
      if (!RebuildChunkOffsetBox(moov_data, child, /*is_co64=*/false,
                                 old_moov_end, delta_moov, &payload)) {
        return false;
      }
    } else if (child.TypeView() == "co64") {
      if (!RebuildChunkOffsetBox(moov_data, child, /*is_co64=*/true,
                                 old_moov_end, delta_moov, &payload)) {
        return false;
      }
    } else {
      payload.insert(payload.end(), moov_data + child.offset,
                     moov_data + child.offset + child.size);
    }
    pos += child.size;
  }
  AppendBox("stbl", payload, out);
  return true;
}

bool RebuildMinfBox(const uint8_t* moov_data, const BoxSlice& minf_box,
                    size_t old_moov_end, int64_t delta_moov,
                    std::vector<uint8_t>* out) {
  std::vector<uint8_t> payload;
  payload.reserve(minf_box.PayloadSize() + 16);
  size_t pos = minf_box.PayloadOffset();
  size_t end = minf_box.offset + minf_box.size;
  while (pos < end) {
    BoxSlice child;
    if (!ReadBoxSlice(moov_data, pos, end, &child)) {
      return false;
    }
    if (child.TypeView() == "stbl") {
      if (!RebuildStblBox(moov_data, child, old_moov_end, delta_moov,
                          &payload)) {
        return false;
      }
    } else {
      payload.insert(payload.end(), moov_data + child.offset,
                     moov_data + child.offset + child.size);
    }
    pos += child.size;
  }
  AppendBox("minf", payload, out);
  return true;
}

bool RebuildMdiaBox(const uint8_t* moov_data, const BoxSlice& mdia_box,
                    size_t old_moov_end, int64_t delta_moov,
                    std::vector<uint8_t>* out) {
  std::vector<uint8_t> payload;
  payload.reserve(mdia_box.PayloadSize() + 16);
  size_t pos = mdia_box.PayloadOffset();
  size_t end = mdia_box.offset + mdia_box.size;
  while (pos < end) {
    BoxSlice child;
    if (!ReadBoxSlice(moov_data, pos, end, &child)) {
      return false;
    }
    if (child.TypeView() == "minf") {
      if (!RebuildMinfBox(moov_data, child, old_moov_end, delta_moov,
                          &payload)) {
        return false;
      }
    } else {
      payload.insert(payload.end(), moov_data + child.offset,
                     moov_data + child.offset + child.size);
    }
    pos += child.size;
  }
  AppendBox("mdia", payload, out);
  return true;
}

bool RebuildTrakBox(const uint8_t* moov_data, const BoxSlice& trak_box,
                    size_t old_moov_end, int64_t delta_moov,
                    std::vector<uint8_t>* out) {
  std::vector<uint8_t> payload;
  payload.reserve(trak_box.PayloadSize() + 16);
  size_t pos = trak_box.PayloadOffset();
  size_t end = trak_box.offset + trak_box.size;
  while (pos < end) {
    BoxSlice child;
    if (!ReadBoxSlice(moov_data, pos, end, &child)) {
      return false;
    }
    if (child.TypeView() == "mdia") {
      if (!RebuildMdiaBox(moov_data, child, old_moov_end, delta_moov,
                          &payload)) {
        return false;
      }
    } else {
      payload.insert(payload.end(), moov_data + child.offset,
                     moov_data + child.offset + child.size);
    }
    pos += child.size;
  }
  AppendBox("trak", payload, out);
  return true;
}

// Builds the canonical Motion Photo v1 `'meta'` `trak` box matching
// `MP4MettAtom` (`"application/motionphoto-image-meta"` pre-Android-Q dual-MIME
// compatibility layout, 1000 Hz timescale, 1 sync sample).
void AppendMomentsMetadataTrakBox(uint32_t track_id, uint32_t creation_time,
                                  uint32_t modification_time,
                                  uint32_t sample_size,
                                  uint64_t sample_file_offset,
                                  std::vector<uint8_t>* out) {
  // 1. tkhd (84-byte version 0 payload -> 92-byte box)
  std::vector<uint8_t> tkhd_payload;
  tkhd_payload.reserve(84);
  AppendBigEndianUInt32(0x00000001u,
                        &tkhd_payload);  // version=0, flags=enabled
  AppendBigEndianUInt32(creation_time, &tkhd_payload);
  AppendBigEndianUInt32(modification_time, &tkhd_payload);
  AppendBigEndianUInt32(track_id, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);  // reserved
  AppendBigEndianUInt32(0, &tkhd_payload);  // duration
  AppendBigEndianUInt32(0, &tkhd_payload);  // reserved[0]
  AppendBigEndianUInt32(0, &tkhd_payload);  // reserved[1]
  AppendBigEndianUInt16(0, &tkhd_payload);  // layer
  AppendBigEndianUInt16(0, &tkhd_payload);  // alternate_group
  AppendBigEndianUInt16(0, &tkhd_payload);  // volume
  AppendBigEndianUInt16(0, &tkhd_payload);  // reserved
  // Identity unity matrix: [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0,
  // 0x40000000]
  AppendBigEndianUInt32(0x00010000u, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0x00010000u, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);
  AppendBigEndianUInt32(0x40000000u, &tkhd_payload);
  AppendBigEndianUInt32(0, &tkhd_payload);  // width
  AppendBigEndianUInt32(0, &tkhd_payload);  // height

  // 2. mdhd (24-byte version 0 payload -> 32-byte box)
  std::vector<uint8_t> mdhd_payload;
  mdhd_payload.reserve(24);
  AppendBigEndianUInt32(0, &mdhd_payload);  // version=0, flags=0
  AppendBigEndianUInt32(creation_time, &mdhd_payload);
  AppendBigEndianUInt32(modification_time, &mdhd_payload);
  AppendBigEndianUInt32(kMetadataTimescaleHz, &mdhd_payload);
  AppendBigEndianUInt32(0, &mdhd_payload);  // duration
  AppendBigEndianUInt16(kUndeterminedLanguageCode, &mdhd_payload);
  AppendBigEndianUInt16(0, &mdhd_payload);  // pre_defined

  // 3. hdlr (25-byte payload -> 33-byte box)
  std::vector<uint8_t> hdlr_payload;
  hdlr_payload.reserve(25);
  AppendBigEndianUInt32(0, &hdlr_payload);  // version=0, flags=0
  AppendBigEndianUInt32(0, &hdlr_payload);  // pre_defined
  AppendFourCC("meta", &hdlr_payload);
  AppendBigEndianUInt32(0, &hdlr_payload);  // reserved[0]
  AppendBigEndianUInt32(0, &hdlr_payload);  // reserved[1]
  AppendBigEndianUInt32(0, &hdlr_payload);  // reserved[2]
  hdlr_payload.push_back(0);                // empty null-terminated name

  // 4. nmhd (4-byte payload -> 12-byte box)
  std::vector<uint8_t> nmhd_payload = {0, 0, 0, 0};

  // 5. dinf -> dref -> url (self-contained flag = 1)
  std::vector<uint8_t> url_payload = {0, 0, 0, 1};
  std::vector<uint8_t> dref_payload;
  AppendBigEndianUInt32(0, &dref_payload);  // version=0, flags=0
  AppendBigEndianUInt32(1, &dref_payload);  // entry_count=1
  AppendBox("url ", url_payload, &dref_payload);
  std::vector<uint8_t> dinf_payload;
  AppendBox("dref", dref_payload, &dinf_payload);

  // 6. stbl -> stsd(mett), stts, stsz, stsc, stco/co64
  // Pre-Android-Q dual-MIME MP4MettAtom payload:
  // reserved1[6]="applic", dataReferenceIndex=0x6174 ("at"),
  // contentEncoding="ion/motionphoto-image-meta\0",
  // mimeFormat="application/motionphoto-image-meta\0".
  const size_t mime_with_nul_len = std::strlen(kMotionPhotoMetadataMime) + 1;
  std::vector<uint8_t> mett_payload;
  mett_payload.reserve(mime_with_nul_len * 2);
  mett_payload.insert(
      mett_payload.end(),
      reinterpret_cast<const uint8_t*>(kMotionPhotoMetadataMime),
      reinterpret_cast<const uint8_t*>(kMotionPhotoMetadataMime) +
          mime_with_nul_len);
  mett_payload.insert(
      mett_payload.end(),
      reinterpret_cast<const uint8_t*>(kMotionPhotoMetadataMime),
      reinterpret_cast<const uint8_t*>(kMotionPhotoMetadataMime) +
          mime_with_nul_len);

  std::vector<uint8_t> stsd_payload;
  AppendBigEndianUInt32(0, &stsd_payload);  // version=0, flags=0
  AppendBigEndianUInt32(1, &stsd_payload);  // entry_count=1
  AppendBox("mett", mett_payload, &stsd_payload);

  std::vector<uint8_t> stts_payload;
  AppendBigEndianUInt32(0, &stts_payload);  // version=0, flags=0
  AppendBigEndianUInt32(1, &stts_payload);  // entry_count=1
  AppendBigEndianUInt32(1, &stts_payload);  // sample_count=1
  AppendBigEndianUInt32(0, &stts_payload);  // sample_delta=0

  std::vector<uint8_t> stsz_payload;
  AppendBigEndianUInt32(0, &stsz_payload);            // version=0, flags=0
  AppendBigEndianUInt32(sample_size, &stsz_payload);  // uniform sample_size
  AppendBigEndianUInt32(1, &stsz_payload);            // sample_count=1

  std::vector<uint8_t> stsc_payload;
  AppendBigEndianUInt32(0, &stsc_payload);  // version=0, flags=0
  AppendBigEndianUInt32(1, &stsc_payload);  // entry_count=1
  AppendBigEndianUInt32(1, &stsc_payload);  // first_chunk=1
  AppendBigEndianUInt32(1, &stsc_payload);  // samples_per_chunk=1
  AppendBigEndianUInt32(1, &stsc_payload);  // sample_description_index=1

  std::vector<uint8_t> stbl_payload;
  AppendBox("stsd", stsd_payload, &stbl_payload);
  AppendBox("stts", stts_payload, &stbl_payload);
  AppendBox("stsz", stsz_payload, &stbl_payload);
  AppendBox("stsc", stsc_payload, &stbl_payload);
  if (sample_file_offset <= UINT32_MAX) {
    std::vector<uint8_t> stco_payload;
    AppendBigEndianUInt32(0, &stco_payload);
    AppendBigEndianUInt32(1, &stco_payload);
    AppendBigEndianUInt32(static_cast<uint32_t>(sample_file_offset),
                          &stco_payload);
    AppendBox("stco", stco_payload, &stbl_payload);
  } else {
    std::vector<uint8_t> co64_payload;
    AppendBigEndianUInt32(0, &co64_payload);
    AppendBigEndianUInt32(1, &co64_payload);
    AppendBigEndianUInt64(sample_file_offset, &co64_payload);
    AppendBox("co64", co64_payload, &stbl_payload);
  }

  std::vector<uint8_t> minf_payload;
  AppendBox("nmhd", nmhd_payload, &minf_payload);
  AppendBox("dinf", dinf_payload, &minf_payload);
  AppendBox("stbl", stbl_payload, &minf_payload);

  std::vector<uint8_t> mdia_payload;
  AppendBox("mdhd", mdhd_payload, &mdia_payload);
  AppendBox("hdlr", hdlr_payload, &mdia_payload);
  AppendBox("minf", minf_payload, &mdia_payload);

  std::vector<uint8_t> trak_payload;
  AppendBox("tkhd", tkhd_payload, &trak_payload);
  AppendBox("mdia", mdia_payload, &trak_payload);

  AppendBox("trak", trak_payload, out);
}

bool BuildRebuiltMoov(const std::vector<uint8_t>& raw_moov,
                      const std::vector<MoovChildInfo>& children,
                      const MvhdInfo& mvhd_info, uint32_t new_meta_track_id,
                      uint32_t metadata_sample_size,
                      uint64_t metadata_sample_offset, bool shift_offsets,
                      size_t old_moov_end, int64_t delta_moov,
                      size_t insert_after_child_index,
                      std::vector<uint8_t>* rebuilt_moov) {
  rebuilt_moov->clear();
  std::vector<uint8_t> moov_payload;
  moov_payload.reserve(raw_moov.size() + 512);

  for (size_t i = 0; i < children.size(); ++i) {
    const MoovChildInfo& child = children[i];
    if (child.is_trak && child.strip_trak) {
      continue;
    }

    if (child.is_mvhd) {
      std::vector<uint8_t> mvhd_bytes(
          raw_moov.begin() + child.box.offset,
          raw_moov.begin() + child.box.offset + child.box.size);
      uint32_t updated_next_track_id = 0xFFFFFFFFu;
      if (mvhd_info.next_track_id != 0xFFFFFFFFu &&
          new_meta_track_id < UINT32_MAX) {
        updated_next_track_id =
            std::max(mvhd_info.next_track_id, new_meta_track_id + 1);
      }
      WriteBigEndianUInt32(updated_next_track_id,
                           mvhd_bytes.data() + child.box.header_size +
                               mvhd_info.next_track_id_offset_in_payload);
      moov_payload.insert(moov_payload.end(), mvhd_bytes.begin(),
                          mvhd_bytes.end());
    } else if (child.is_trak && shift_offsets && delta_moov != 0) {
      if (!RebuildTrakBox(raw_moov.data(), child.box, old_moov_end, delta_moov,
                          &moov_payload)) {
        return false;
      }
    } else {
      moov_payload.insert(moov_payload.end(),
                          raw_moov.begin() + child.box.offset,
                          raw_moov.begin() + child.box.offset + child.box.size);
    }

    if (i == insert_after_child_index) {
      AppendMomentsMetadataTrakBox(new_meta_track_id, mvhd_info.creation_time,
                                   mvhd_info.modification_time,
                                   metadata_sample_size, metadata_sample_offset,
                                   &moov_payload);
    }
  }

  AppendBox("moov", moov_payload, rebuilt_moov);
  return true;
}

}  // namespace

bool PrepareMp4MettInjection(const MediaSource& video_source,
                             const std::vector<uint8_t>& metadata,
                             PreparedMp4MettInjection* prepared) {
  if (prepared == nullptr || metadata.empty() ||
      metadata.size() > UINT32_MAX - 8) {
    return false;
  }
  *prepared = PreparedMp4MettInjection();

  const size_t source_size = video_source.GetSize();
  if (source_size < 16) {
    return false;
  }

  bool found_moov = false;
  size_t moov_offset = 0;
  size_t moov_size = 0;
  size_t moov_header_size = 8;

  size_t pos = 0;
  while (pos < source_size) {
    if (source_size - pos < 8) {
      return false;
    }
    uint8_t hdr[16] = {};
    size_t to_read = std::min<size_t>(16, source_size - pos);
    if (!video_source.ReadAt(pos, to_read, hdr)) {
      return false;
    }
    uint32_t size32 = ReadBigEndianUInt32(hdr);
    uint64_t box_size = size32;
    size_t header_size = 8;
    if (size32 == 1) {
      if (to_read < 16) {
        return false;
      }
      box_size = ReadBigEndianUInt64(hdr + 8);
      header_size = 16;
    } else if (size32 == 0) {
      box_size = source_size - pos;
    }
    if (box_size < header_size || box_size > source_size - pos) {
      return false;
    }

    std::string_view box_type(reinterpret_cast<const char*>(hdr + 4), 4);
    if (box_type == "moov") {
      if (found_moov || box_size > kMaxMoovSize) {
        return false;
      }
      found_moov = true;
      moov_offset = pos;
      moov_size = static_cast<size_t>(box_size);
      moov_header_size = header_size;
    } else if (size32 == 0) {
      if (box_size > UINT32_MAX) {
        return false;
      }
      prepared->patch_open_tail_box = true;
      prepared->open_tail_box_offset = pos;
      prepared->open_tail_box_size = static_cast<uint32_t>(box_size);
    }
    pos += static_cast<size_t>(box_size);
  }

  if (!found_moov || pos != source_size) {
    return false;
  }

  std::vector<uint8_t> raw_moov(moov_size);
  if (!video_source.ReadAt(moov_offset, moov_size, raw_moov.data())) {
    return false;
  }

  std::vector<MoovChildInfo> children;
  MvhdInfo mvhd_info;
  uint32_t max_surviving_track_id = 0;
  size_t surviving_trak_count = 0;
  size_t last_surviving_trak_index = 0;

  size_t child_pos = moov_header_size;
  while (child_pos < moov_size) {
    MoovChildInfo child_info;
    if (!ReadBoxSlice(raw_moov.data(), child_pos, moov_size, &child_info.box)) {
      return false;
    }
    if (child_info.box.TypeView() == "mvhd") {
      if (mvhd_info.found ||
          !ParseMvhdBox(raw_moov.data(), child_info.box, &mvhd_info)) {
        return false;
      }
      child_info.is_mvhd = true;
    } else if (child_info.box.TypeView() == "trak") {
      child_info.is_trak = true;
      if (!InspectTrakBox(raw_moov.data(), child_info.box, &child_info.track_id,
                          &child_info.strip_trak)) {
        return false;
      }
      if (!child_info.strip_trak) {
        ++surviving_trak_count;
        max_surviving_track_id =
            std::max(max_surviving_track_id, child_info.track_id);
        last_surviving_trak_index = children.size();
      }
    }
    child_pos += child_info.box.size;
    children.push_back(child_info);
  }

  if (child_pos != moov_size || !mvhd_info.found || surviving_trak_count == 0 ||
      max_surviving_track_id == UINT32_MAX) {
    return false;
  }

  const uint32_t new_meta_track_id = max_surviving_track_id + 1;
  const uint32_t metadata_sample_size = static_cast<uint32_t>(metadata.size());

  prepared->moov_offset = moov_offset;
  prepared->moov_size = moov_size;
  prepared->moov_at_end = (moov_offset + moov_size == source_size);
  prepared->appended_mdat_box.reserve(8 + metadata.size());
  AppendBigEndianUInt32(8 + metadata_sample_size, &prepared->appended_mdat_box);
  AppendFourCC("mdat", &prepared->appended_mdat_box);
  prepared->appended_mdat_box.insert(prepared->appended_mdat_box.end(),
                                     metadata.begin(), metadata.end());

  if (prepared->moov_at_end) {
    uint64_t meta_sample_offset = static_cast<uint64_t>(moov_offset) + 8;
    if (!BuildRebuiltMoov(raw_moov, children, mvhd_info, new_meta_track_id,
                          metadata_sample_size, meta_sample_offset,
                          /*shift_offsets=*/false, moov_size, /*delta_moov=*/0,
                          last_surviving_trak_index, &prepared->rebuilt_moov)) {
      return false;
    }
    if (moov_offset > SIZE_MAX - prepared->appended_mdat_box.size()) {
      return false;
    }
    size_t prefix_plus_mdat = moov_offset + prepared->appended_mdat_box.size();
    if (prepared->rebuilt_moov.size() > SIZE_MAX - prefix_plus_mdat) {
      return false;
    }
    prepared->output_video_size =
        prefix_plus_mdat + prepared->rebuilt_moov.size();
    return true;
  }

  const size_t old_moov_end = moov_offset + moov_size;
  const size_t suffix_size = source_size - old_moov_end;
  int64_t delta_moov = 0;
  bool converged = false;
  for (int pass = 0; pass < 5; ++pass) {
    int64_t expected_moov_size = static_cast<int64_t>(moov_size) + delta_moov;
    if (expected_moov_size < 8) {
      return false;
    }
    uint64_t meta_mdat_offset = static_cast<uint64_t>(moov_offset) +
                                static_cast<uint64_t>(expected_moov_size) +
                                static_cast<uint64_t>(suffix_size);
    if (meta_mdat_offset > UINT64_MAX - 8) {
      return false;
    }
    uint64_t meta_sample_offset = meta_mdat_offset + 8;
    if (!BuildRebuiltMoov(raw_moov, children, mvhd_info, new_meta_track_id,
                          metadata_sample_size, meta_sample_offset,
                          /*shift_offsets=*/true, old_moov_end, delta_moov,
                          last_surviving_trak_index, &prepared->rebuilt_moov)) {
      return false;
    }
    int64_t actual_delta = static_cast<int64_t>(prepared->rebuilt_moov.size()) -
                           static_cast<int64_t>(moov_size);
    if (actual_delta == delta_moov) {
      converged = true;
      break;
    }
    delta_moov = actual_delta;
  }
  if (!converged) {
    return false;
  }

  if (moov_offset > SIZE_MAX - prepared->rebuilt_moov.size()) {
    return false;
  }
  size_t accum = moov_offset + prepared->rebuilt_moov.size();
  if (suffix_size > SIZE_MAX - accum) {
    return false;
  }
  accum += suffix_size;
  if (prepared->appended_mdat_box.size() > SIZE_MAX - accum) {
    return false;
  }
  prepared->output_video_size = accum + prepared->appended_mdat_box.size();
  return true;
}

bool WriteMp4WithMettToSink(const MediaSource& video_source,
                            const PreparedMp4MettInjection& prepared,
                            DataSink* output_sink) {
  if (output_sink == nullptr || prepared.rebuilt_moov.empty() ||
      prepared.appended_mdat_box.empty()) {
    return false;
  }
  const size_t source_size = video_source.GetSize();
  if (prepared.moov_offset > source_size ||
      prepared.moov_size > source_size - prepared.moov_offset) {
    return false;
  }

  if (prepared.moov_at_end) {
    if (!output_sink->TransferFrom(video_source, 0, prepared.moov_offset)) {
      return false;
    }
    if (!output_sink->Write(prepared.appended_mdat_box.data(),
                            prepared.appended_mdat_box.size())) {
      return false;
    }
    return output_sink->Write(prepared.rebuilt_moov.data(),
                              prepared.rebuilt_moov.size());
  }

  if (!output_sink->TransferFrom(video_source, 0, prepared.moov_offset)) {
    return false;
  }
  if (!output_sink->Write(prepared.rebuilt_moov.data(),
                          prepared.rebuilt_moov.size())) {
    return false;
  }

  const size_t old_moov_end = prepared.moov_offset + prepared.moov_size;
  if (prepared.patch_open_tail_box &&
      prepared.open_tail_box_offset >= old_moov_end &&
      prepared.open_tail_box_offset <= source_size - 4) {
    if (!output_sink->TransferFrom(
            video_source, old_moov_end,
            prepared.open_tail_box_offset - old_moov_end)) {
      return false;
    }
    uint8_t size_bytes[4];
    WriteBigEndianUInt32(prepared.open_tail_box_size, size_bytes);
    if (!output_sink->Write(size_bytes, sizeof(size_bytes))) {
      return false;
    }
    size_t after_size_offset = prepared.open_tail_box_offset + 4;
    if (!output_sink->TransferFrom(video_source, after_size_offset,
                                   source_size - after_size_offset)) {
      return false;
    }
  } else {
    if (!output_sink->TransferFrom(video_source, old_moov_end,
                                   source_size - old_moov_end)) {
      return false;
    }
  }

  return output_sink->Write(prepared.appended_mdat_box.data(),
                            prepared.appended_mdat_box.size());
}

}  // namespace motion_photo
}  // namespace libmotionphoto
