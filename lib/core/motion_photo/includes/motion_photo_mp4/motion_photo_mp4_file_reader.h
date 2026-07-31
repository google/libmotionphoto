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

#ifndef MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_READER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_READER_H_  // NOLINT

#include <string>
#include <vector>

#include "motion_photo/video_track_data.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A class for reading an MP4 file and the track data and video metadata.
class MotionPhotoMp4FileReader {
 public:
  //   file_name: The file name to read.
  //   offset: The offset from the start of the file where the MP4 data
  // actually begins. This allows this reader to be used with a motion photo
  // JPEG file that has an MP4 file append to it. Use the GetMp4FileOffset()
  // function in MotionPhoto to get the offset value.
  // Returns whether the file and information was read successfully.
  bool Read(const std::string& file_name, int64_t offset = 0);

  // Returns the video track data read from the MP4 file.
  const VideoTrackData& GetVideoTrackData() const { return track_data_; }

  // Returns the high res video track metadata read from the file.
  const std::vector<uint8_t>& GetHighResTrackVideoMetadata() const {
    return high_res_track_metadata_;
  }

 private:
  VideoTrackData track_data_;
  std::vector<uint8_t> high_res_track_metadata_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_READER_H_  // NOLINT
