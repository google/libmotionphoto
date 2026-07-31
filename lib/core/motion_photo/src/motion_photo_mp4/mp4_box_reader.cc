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

#include "motion_photo_mp4/mp4_box_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

namespace {

inline uint32_t ReadBigEndianUInt32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

inline uint64_t ReadBigEndianUInt64(const uint8_t* p) {
  return (static_cast<uint64_t>(ReadBigEndianUInt32(p)) << 32) |
         ReadBigEndianUInt32(p + 4);
}

}  // namespace

Mp4BoxReader::Mp4BoxReader() = default;
Mp4BoxReader::~Mp4BoxReader() = default;

bool Mp4BoxReader::ParseFromMemory(const uint8_t* data, size_t size,
                                   int64_t offset) {
  if (data == nullptr || size == 0) {
    return false;
  }
  owned_buffer_.clear();
  data_ = data;
  data_size_ = size;
  mp4_offset_ = offset;
  tracks_.clear();
  return ParseInternal();
}

bool Mp4BoxReader::ParseFromFile(const std::string& file_path, int64_t offset) {
  FILE* fp = fopen(file_path.c_str(), "rb");
  if (!fp) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (file_size <= 0) {
    fclose(fp);
    return false;
  }

  owned_buffer_.resize(file_size);
  size_t bytes_read = fread(owned_buffer_.data(), 1, file_size, fp);
  fclose(fp);

  if (bytes_read != static_cast<size_t>(file_size)) {
    owned_buffer_.clear();
    return false;
  }

  return ParseFromMemory(owned_buffer_.data(), owned_buffer_.size(), offset);
}

const Mp4TrackInfo& Mp4BoxReader::GetTrack(size_t index) const {
  return tracks_[index];
}

const Mp4TrackInfo* Mp4BoxReader::FindTrackById(uint32_t track_id) const {
  for (const auto& track : tracks_) {
    if (track.track_id == track_id) {
      return &track;
    }
  }
  return nullptr;
}

uint32_t Mp4BoxReader::GetTrackNumberOfSamples(uint32_t track_id) const {
  const Mp4TrackInfo* track = FindTrackById(track_id);
  if (track == nullptr) {
    return 0;
  }
  return track->sample_count;
}

bool Mp4BoxReader::TrackHasMettMime(const Mp4TrackInfo& track,
                                   std::string_view mime) {
  for (const auto& entry : track.sample_entries) {
    if (entry.format == "mett" || entry.format == "mp4s") {
      std::string_view payload_view(
          reinterpret_cast<const char*>(entry.payload.data()),
          entry.payload.size());
      if (payload_view.find(mime) != std::string_view::npos) {
        return true;
      }
    }
  }
  return false;
}

bool Mp4BoxReader::TrackIsAgtmMetadata(const Mp4TrackInfo& track) {
  for (const auto& entry : track.sample_entries) {
    if (entry.format == "it35") {
      return true;
    }
    if (entry.format == "mett" || entry.format == "mp4s") {
      std::string_view payload_view(
          reinterpret_cast<const char*>(entry.payload.data()),
          entry.payload.size());
      if (payload_view.find("\xb5\x00\x90") != std::string_view::npos ||
          payload_view.find("it35") != std::string_view::npos ||
          payload_view.find("agtm") != std::string_view::npos ||
          payload_view.find("AGTM") != std::string_view::npos) {
        return true;
      }
    }
  }
  return false;
}

bool Mp4BoxReader::ReadSample(uint32_t track_id, uint32_t sample_id,
                              std::vector<uint8_t>* output_bytes) const {
  if (output_bytes == nullptr) {
    return false;
  }
  output_bytes->clear();

  const Mp4TrackInfo* track = FindTrackById(track_id);
  if (track == nullptr || sample_id < 1 || sample_id > track->sample_count) {
    return false;
  }

  uint32_t sample_size = track->uniform_sample_size > 0
                             ? track->uniform_sample_size
                             : track->sample_sizes[sample_id - 1];

  size_t total_samples = 0;
  size_t target_chunk = 1;
  size_t samples_before_in_chunk = 0;
  size_t first_sample_in_chunk = 1;

  size_t num_stsc = track->stsc_entries.size();
  size_t num_chunks = track->chunk_offsets.size();

  for (size_t j = 0; j < num_stsc; ++j) {
    uint32_t cur_first = track->stsc_entries[j].first_chunk;
    uint32_t spc = track->stsc_entries[j].samples_per_chunk;
    uint32_t next_first = (j + 1 < num_stsc)
                              ? track->stsc_entries[j + 1].first_chunk
                              : (static_cast<uint32_t>(num_chunks) + 1);
    if (next_first <= cur_first) continue;
    uint32_t chunk_count = next_first - cur_first;
    uint64_t samples_in_stsc_range = static_cast<uint64_t>(chunk_count) * spc;

    if (sample_id <= total_samples + samples_in_stsc_range) {
      if (spc > 0) {
        uint32_t chunk_offset_in_range =
            static_cast<uint32_t>((sample_id - 1 - total_samples) / spc);
        target_chunk = cur_first + chunk_offset_in_range;
        samples_before_in_chunk = (sample_id - 1 - total_samples) % spc;
        first_sample_in_chunk = sample_id - samples_before_in_chunk;
      }
      break;
    }
    total_samples += samples_in_stsc_range;
  }

  if (target_chunk < 1 || target_chunk > num_chunks) {
    return false;
  }

  uint64_t chunk_offset = track->chunk_offsets[target_chunk - 1];
  uint64_t offset_in_chunk = 0;
  if (track->uniform_sample_size > 0) {
    offset_in_chunk = samples_before_in_chunk * track->uniform_sample_size;
  } else {
    for (size_t s = first_sample_in_chunk; s < sample_id; ++s) {
      offset_in_chunk += track->sample_sizes[s - 1];
    }
  }

  uint64_t file_offset = mp4_offset_ + chunk_offset + offset_in_chunk;
  if (file_offset > data_size_ || sample_size > data_size_ - file_offset) {
    return false;
  }

  output_bytes->assign(data_ + file_offset, data_ + file_offset + sample_size);
  return true;
}

uint32_t Mp4BoxReader::GetSampleIdFromTimeUs(uint32_t track_id,
                                            int64_t timestamp_us) const {
  const Mp4TrackInfo* track = FindTrackById(track_id);
  if (track == nullptr || track->sample_count == 0) {
    return 1;
  }
  if (track->stts_entries.empty()) {
    return 1;
  }
  uint64_t track_time = track->timescale > 0
                            ? (static_cast<uint64_t>(timestamp_us) *
                               track->timescale) / 1000000
                            : 0;
  uint64_t accum_time = 0;
  uint32_t cur_sample = 1;
  for (const auto& entry : track->stts_entries) {
    uint32_t sc = entry.first;
    uint32_t sd = entry.second;
    uint64_t delta_span = static_cast<uint64_t>(sc) * sd;
    if (accum_time + delta_span > track_time) {
      uint32_t delta_samples =
          sd > 0 ? static_cast<uint32_t>((track_time - accum_time) / sd) : 0;
      return std::min(cur_sample + delta_samples, track->sample_count);
    }
    accum_time += delta_span;
    cur_sample += sc;
  }
  return track->sample_count;
}

bool Mp4BoxReader::ParseInternal() {
  size_t pos = static_cast<size_t>(mp4_offset_ >= 0 ? mp4_offset_ : 0);

  // If offset is 0 and no ftyp at 0, search for ftyp
  if (pos == 0 && !(pos <= data_size_ && 8 <= data_size_ - pos &&
                    std::memcmp(data_ + pos + 4, "ftyp", 4) == 0)) {
    for (size_t i = 0; i <= data_size_ && 8 <= data_size_ - i; ++i) {
      if (std::memcmp(data_ + i + 4, "ftyp", 4) == 0) {
        pos = i;
        mp4_offset_ = static_cast<int64_t>(pos);
        break;
      }
    }
  }

  size_t moov_start = 0;
  size_t moov_end = 0;
  bool found_moov = false;

  while (pos <= data_size_ && 8 <= data_size_ - pos) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > data_size_ || 16 > data_size_ - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = data_size_ - pos;
    }

    if (size < header_size || pos > data_size_ || size > data_size_ - pos) {
      break;
    }

    if (std::memcmp(data_ + pos + 4, "moov", 4) == 0) {
      moov_start = pos + header_size;
      moov_end = pos + size;
      found_moov = true;
      break;
    }
    pos += size;
  }

  if (!found_moov) {
    return false;
  }

  return ParseMoov(moov_start, moov_end);
}

bool Mp4BoxReader::ParseMoov(size_t start, size_t end) {
  size_t pos = start;
  while (pos <= (end >= 8 ? end - 8 : 0)) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > end || 16 > end - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < header_size || pos > end || size > end - pos) break;

    if (std::memcmp(data_ + pos + 4, "trak", 4) == 0) {
      Mp4TrackInfo track;
      if (ParseTrak(pos + header_size, pos + size, &track)) {
        tracks_.push_back(std::move(track));
      }
    }
    pos += size;
  }
  return !tracks_.empty();
}

bool Mp4BoxReader::ParseTrak(size_t start, size_t end, Mp4TrackInfo* track) {
  size_t pos = start;
  while (pos <= (end >= 8 ? end - 8 : 0)) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > end || 16 > end - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < header_size || pos > end || size > end - pos) break;

    size_t payload_start = pos + header_size;
    size_t payload_end = pos + size;

    if (std::memcmp(data_ + pos + 4, "tkhd", 4) == 0 && payload_end >= payload_start + 24) {
      uint8_t version = data_[payload_start];
      if (version == 0) {
        track->track_id = ReadBigEndianUInt32(data_ + payload_start + 12);
      } else {
        track->track_id = ReadBigEndianUInt32(data_ + payload_start + 20);
      }
    } else if (std::memcmp(data_ + pos + 4, "mdia", 4) == 0) {
      ParseMdia(payload_start, payload_end, track);
    }
    pos += size;
  }
  return track->track_id != 0;
}

bool Mp4BoxReader::ParseMdia(size_t start, size_t end, Mp4TrackInfo* track) {
  size_t pos = start;
  while (pos <= (end >= 8 ? end - 8 : 0)) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > end || 16 > end - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < header_size || pos > end || size > end - pos) break;

    size_t payload_start = pos + header_size;
    size_t payload_end = pos + size;

    if (std::memcmp(data_ + pos + 4, "mdhd", 4) == 0 && payload_end >= payload_start + 24) {
      uint8_t version = data_[payload_start];
      if (version == 0) {
        track->timescale = ReadBigEndianUInt32(data_ + payload_start + 12);
        track->duration = ReadBigEndianUInt32(data_ + payload_start + 16);
      } else {
        track->timescale = ReadBigEndianUInt32(data_ + payload_start + 20);
        track->duration = ReadBigEndianUInt64(data_ + payload_start + 24);
      }
    } else if (std::memcmp(data_ + pos + 4, "hdlr", 4) == 0 && payload_end >= payload_start + 12) {
      track->handler_type.assign(reinterpret_cast<const char*>(data_ + payload_start + 8), 4);
    } else if (std::memcmp(data_ + pos + 4, "minf", 4) == 0) {
      ParseMinf(payload_start, payload_end, track);
    }
    pos += size;
  }
  return true;
}

bool Mp4BoxReader::ParseMinf(size_t start, size_t end, Mp4TrackInfo* track) {
  size_t pos = start;
  while (pos <= (end >= 8 ? end - 8 : 0)) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > end || 16 > end - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < header_size || pos > end || size > end - pos) break;

    if (std::memcmp(data_ + pos + 4, "stbl", 4) == 0) {
      ParseStbl(pos + header_size, pos + size, track);
    }
    pos += size;
  }
  return true;
}

bool Mp4BoxReader::ParseStbl(size_t start, size_t end, Mp4TrackInfo* track) {
  size_t pos = start;
  while (pos <= (end >= 8 ? end - 8 : 0)) {
    uint64_t size = ReadBigEndianUInt32(data_ + pos);
    size_t header_size = 8;
    if (size == 1) {
      if (pos > end || 16 > end - pos) break;
      size = ReadBigEndianUInt64(data_ + pos + 8);
      header_size = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < header_size || pos > end || size > end - pos) break;

    size_t payload_start = pos + header_size;
    size_t payload_end = pos + size;

    if (std::memcmp(data_ + pos + 4, "stsd", 4) == 0 && payload_end >= payload_start + 8) {
      uint32_t count = ReadBigEndianUInt32(data_ + payload_start + 4);
      size_t spos = payload_start + 8;
      for (uint32_t i = 0;
           i < count && spos <= (payload_end >= 8 ? payload_end - 8 : 0); ++i) {
        uint32_t esz = ReadBigEndianUInt32(data_ + spos);
        if (esz < 8 || spos > payload_end || esz > payload_end - spos) break;
        Mp4SampleEntry entry;
        entry.format.assign(reinterpret_cast<const char*>(data_ + spos + 4), 4);
        entry.payload.assign(data_ + spos + 8, data_ + spos + esz);
        track->sample_entries.push_back(std::move(entry));
        spos += esz;
      }
    } else if (std::memcmp(data_ + pos + 4, "stts", 4) == 0 && payload_end >= payload_start + 8) {
      uint32_t count = ReadBigEndianUInt32(data_ + payload_start + 4);
      for (uint32_t i = 0; i < count; ++i) {
        size_t epos = payload_start + 8 + i * 8;
        if (epos > payload_end || 8 > payload_end - epos) break;
        uint32_t sc = ReadBigEndianUInt32(data_ + epos);
        uint32_t sd = ReadBigEndianUInt32(data_ + epos + 4);
        track->stts_entries.emplace_back(sc, sd);
      }
    } else if (std::memcmp(data_ + pos + 4, "stsz", 4) == 0 &&
               (payload_start <= payload_end &&
                12 <= payload_end - payload_start)) {
      track->uniform_sample_size =
          ReadBigEndianUInt32(data_ + payload_start + 4);
      track->sample_count = ReadBigEndianUInt32(data_ + payload_start + 8);
      if (track->uniform_sample_size == 0) {
        track->sample_sizes.reserve(track->sample_count);
        for (uint32_t i = 0; i < track->sample_count; ++i) {
          size_t offset_in_table = static_cast<size_t>(i) * 4;
          if (offset_in_table >
              SIZE_MAX - 12 - payload_start) {
            break;
          }
          size_t epos = payload_start + 12 + offset_in_table;
          if (epos > payload_end || 4 > payload_end - epos) break;
          track->sample_sizes.push_back(ReadBigEndianUInt32(data_ + epos));
        }
      }
    } else if (std::memcmp(data_ + pos + 4, "stsc", 4) == 0 &&
               (payload_start <= payload_end &&
                8 <= payload_end - payload_start)) {
      uint32_t count = ReadBigEndianUInt32(data_ + payload_start + 4);
      track->stsc_entries.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        size_t offset_in_table = static_cast<size_t>(i) * 12;
        if (offset_in_table >
            SIZE_MAX - 8 - payload_start) {
          break;
        }
        size_t epos = payload_start + 8 + offset_in_table;
        if (epos > payload_end || 12 > payload_end - epos) break;
        Mp4TrackInfo::StscEntry stsc;
        stsc.first_chunk = ReadBigEndianUInt32(data_ + epos);
        stsc.samples_per_chunk = ReadBigEndianUInt32(data_ + epos + 4);
        stsc.sample_description_index = ReadBigEndianUInt32(data_ + epos + 8);
        track->stsc_entries.push_back(stsc);
      }
    } else if (std::memcmp(data_ + pos + 4, "stco", 4) == 0 &&
               (payload_start <= payload_end &&
                8 <= payload_end - payload_start)) {
      uint32_t count = ReadBigEndianUInt32(data_ + payload_start + 4);
      track->chunk_offsets.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        size_t offset_in_table = static_cast<size_t>(i) * 4;
        if (offset_in_table >
            SIZE_MAX - 8 - payload_start) {
          break;
        }
        size_t epos = payload_start + 8 + offset_in_table;
        if (epos > payload_end || 4 > payload_end - epos) break;
        track->chunk_offsets.push_back(ReadBigEndianUInt32(data_ + epos));
      }
    } else if (std::memcmp(data_ + pos + 4, "co64", 4) == 0 &&
               (payload_start <= payload_end &&
                8 <= payload_end - payload_start)) {
      uint32_t count = ReadBigEndianUInt32(data_ + payload_start + 4);
      track->chunk_offsets.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        size_t offset_in_table = static_cast<size_t>(i) * 8;
        if (offset_in_table >
            SIZE_MAX - 8 - payload_start) {
          break;
        }
        size_t epos = payload_start + 8 + offset_in_table;
        if (epos > payload_end || 8 > payload_end - epos) break;
        track->chunk_offsets.push_back(ReadBigEndianUInt64(data_ + epos));
      }
    }
    pos += size;
  }
  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
