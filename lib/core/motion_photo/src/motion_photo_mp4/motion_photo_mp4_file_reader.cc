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

#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "motion_photo/video_track_data.h"
#include "motion_photo_mp4/mp4_box_reader.h"

namespace libmotionphoto {
namespace motion_photo {
using std::string;
using std::vector;

bool MotionPhotoMp4FileReader::Read(const string& file_name, int64_t offset) {
  Mp4BoxReader reader;
  if (!reader.ParseFromFile(file_name, offset)) {
    return false;
  }

  track_data_ = VideoTrackData();
  size_t track_count = reader.GetTrackCount();

  for (size_t index = 0; index < track_count; ++index) {
    const auto& track = reader.GetTrack(index);

    if (track.handler_type == "vide") {
      track_data_.video_track_count++;
      if (track_data_.video_track_count == 1) {
        track_data_.low_res_video_track_id = track.track_id;
      } else if (track_data_.video_track_count == 2) {
        track_data_.high_res_video_track_id = track.track_id;
      }
      continue;
    }

    if (track.handler_type == "soun") {
      track_data_.audio_track_count++;
      if (track_data_.audio_track_count == 1) {
        track_data_.low_res_audio_track_id = track.track_id;
      }
      continue;
    }

    if (track.handler_type == kVideoMetadataTrackType) {
      track_data_.metadata_track_count++;
      if (track_data_.high_res_metadata_track_id == kInvalidTrackId &&
          Mp4BoxReader::TrackHasMettMime(track, kMotionPhotoMetadataMime)) {
        track_data_.high_res_metadata_track_id = track.track_id;
        continue;
      }
    }
  }

  if (track_data_.high_res_metadata_track_id != kInvalidTrackId) {
    reader.ReadSample(track_data_.high_res_metadata_track_id, 1,
                      &high_res_track_metadata_);
  }

  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
