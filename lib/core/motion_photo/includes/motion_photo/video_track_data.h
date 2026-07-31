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

#ifndef INCLUDES_MOTION_PHOTO_VIDEO_TRACK_DATA_H_  // NOLINT
#include <cstdint>
#define INCLUDES_MOTION_PHOTO_VIDEO_TRACK_DATA_H_  // NOLINT

namespace libmotionphoto {
namespace motion_photo {

// A track id in an MP4 file.
using TrackId = uint32_t;

// An invalid or unspecified track id value.
const TrackId kInvalidTrackId = 0;

// The MP4 track type of the track that contains motion photo metadata.
const char kVideoMetadataTrackType[] = "meta";

// The MP4 track atom name containing the mime string.
const char kVideoMetadataAtomName[] = "mdia.minf.stbl.stsd.mett";

// The mime type of the track sample that contains motion photo metadata.
const char kMotionPhotoMetadataMime[] = "application/motionphoto-image-meta";

// The track data in an MP4 file that is relevant to a motion photo.
struct VideoTrackData {
  TrackId low_res_video_track_id = kInvalidTrackId;
  TrackId low_res_audio_track_id = kInvalidTrackId;
  TrackId high_res_video_track_id = kInvalidTrackId;
  TrackId high_res_metadata_track_id = kInvalidTrackId;
  int video_track_count = 0;
  int audio_track_count = 0;
  int metadata_track_count = 0;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // INCLUDES_MOTION_PHOTO_VIDEO_TRACK_DATA_H_  // NOLINT
