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

#ifndef MOTION_PHOTO_MP4_MP4_BOX_READER_H_
#define MOTION_PHOTO_MP4_MP4_BOX_READER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

// A sample entry from an stsd box.
struct Mp4SampleEntry {
  std::string format;  // e.g. "avc1", "hvc1", "mp4a", "mett", "mp4s"
  std::vector<uint8_t> payload;
};

// Track information extracted from an MP4 trak box.
struct Mp4TrackInfo {
  uint32_t track_id = 0;
  std::string handler_type;  // e.g. "vide", "soun", "meta", "hint"
  uint32_t timescale = 0;
  uint64_t duration = 0;
  std::vector<Mp4SampleEntry> sample_entries;
  std::vector<std::pair<uint32_t, uint32_t>> stts_entries;  // sample_count, sample_delta
  uint32_t uniform_sample_size = 0;
  uint32_t sample_count = 0;
  std::vector<uint32_t> sample_sizes;

  struct StscEntry {
    uint32_t first_chunk = 0;
    uint32_t samples_per_chunk = 0;
    uint32_t sample_description_index = 0;
  };
  std::vector<StscEntry> stsc_entries;
  std::vector<uint64_t> chunk_offsets;  // supports both stco and co64
};

// A lightweight, zero-dependency MP4/ISOBMFF demuxer for Motion Photos.
class Mp4BoxReader {
 public:
  Mp4BoxReader();
  ~Mp4BoxReader();

  // Parses an MP4 stream from a memory buffer.
  bool ParseFromMemory(const uint8_t* data, size_t size, int64_t offset = 0);

  // Parses an MP4 stream from a file on disk at the given byte offset.
  bool ParseFromFile(const std::string& file_path, int64_t offset = 0);

  // Returns the number of tracks parsed from the MP4 moov box.
  size_t GetTrackCount() const { return tracks_.size(); }

  // Returns track at index (0 <= index < GetTrackCount()).
  const Mp4TrackInfo& GetTrack(size_t index) const;

  // Returns all parsed tracks.
  const std::vector<Mp4TrackInfo>& GetTracks() const { return tracks_; }

  // Finds a track by its 1-based track ID.
  const Mp4TrackInfo* FindTrackById(uint32_t track_id) const;

  // Returns the number of samples in the given track.
  uint32_t GetTrackNumberOfSamples(uint32_t track_id) const;

  // Reads a sample payload (sample_id is 1-based: 1 <= sample_id <=
  // sample_count).
  bool ReadSample(uint32_t track_id, uint32_t sample_id,
                  std::vector<uint8_t>* output_bytes) const;

  // Converts a presentation timestamp in microseconds to a 1-based sample ID.
  uint32_t GetSampleIdFromTimeUs(uint32_t track_id, int64_t timestamp_us) const;

  // Checks if a track contains a mett atom with the given MIME substring.
  static bool TrackHasMettMime(const Mp4TrackInfo& track, std::string_view mime);

  // Checks if a track is an AGTM / SMPTE ST 2094-50 timed metadata track.
  static bool TrackIsAgtmMetadata(const Mp4TrackInfo& track);

 private:
  std::vector<uint8_t> owned_buffer_;
  const uint8_t* data_ = nullptr;
  size_t data_size_ = 0;
  int64_t mp4_offset_ = 0;
  std::vector<Mp4TrackInfo> tracks_;

  bool ParseInternal();
  bool ParseMoov(size_t start, size_t end);
  bool ParseTrak(size_t start, size_t end, Mp4TrackInfo* track);
  bool ParseMdia(size_t start, size_t end, Mp4TrackInfo* track);
  bool ParseMinf(size_t start, size_t end, Mp4TrackInfo* track);
  bool ParseStbl(size_t start, size_t end, Mp4TrackInfo* track);
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MP4_MP4_BOX_READER_H_
