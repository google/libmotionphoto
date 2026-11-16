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

#ifndef MOTION_PHOTO_MP4_MP4_METT_BOX_INJECTOR_H_  // NOLINT(build/header_guard)
#define MOTION_PHOTO_MP4_MP4_METT_BOX_INJECTOR_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "motion_photo/motion_photo_source.h"

namespace libmotionphoto {
namespace motion_photo {

// Precomputed byte layout for injecting or replacing the Motion Photo `'meta'`
// (`application/motionphoto-image-meta`) track in an MP4/QuickTime stream
// without creating temporary files or reading `mdat` payloads into memory.
struct PreparedMp4MettInjection {
  size_t moov_offset = 0;
  size_t moov_size = 0;
  bool moov_at_end = false;
  bool patch_open_tail_box = false;
  size_t open_tail_box_offset = 0;
  uint32_t open_tail_box_size = 0;
  std::vector<uint8_t> rebuilt_moov;
  std::vector<uint8_t> appended_mdat_box;
  size_t output_video_size = 0;
};

// Scans top-level box headers and the bounded `moov` box from `video_source`,
// strips any existing `"application/motionphoto-image-meta"` metadata track
// while preserving video, audio, and dynamic HDR (`it35` AGTM) tracks, shifts
// chunk offsets (`stco`/`co64`) when `moov` precedes `mdat`, and prepares a
// canonical 1000 Hz `mett` metadata track carrying `metadata` as a single sync
// sample. Populates `prepared` (including exact `output_video_size`) and
// returns true on success.
bool PrepareMp4MettInjection(const MediaSource& video_source,
                             const std::vector<uint8_t>& metadata,
                             PreparedMp4MettInjection* prepared);

// Streams `video_source` together with `prepared` into `output_sink` in a
// single forward pass without temporary files. Returns true on success.
bool WriteMp4WithMettToSink(const MediaSource& video_source,
                            const PreparedMp4MettInjection& prepared,
                            DataSink* output_sink);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MP4_MP4_METT_BOX_INJECTOR_H_  //
        // NOLINT(build/header_guard)
