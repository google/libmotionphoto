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

#ifndef MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_BUILDER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_BUILDER_H_  // NOLINT

#include <mp4v2/mp4v2.h>

#include <string>
#include <vector>

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// Builds the trailer video of a motion photo from other videos and metadata.
class MotionPhotoMp4FileBuilder {
 public:
  ~MotionPhotoMp4FileBuilder();

  //   working_file_name: The name of a file to write temp data to.
  //   optimized_file_name: The name of a file to optimize the data in
  // the working_file_name to.
  //   message_handler: An message handler for writing messages.
  MotionPhotoMp4FileBuilder(const std::string& working_file_name,
                            image_io::MessageHandler* message_handler);

  //   primary_file_name: The name of the primary video file to add.
  //   track_summary: A string to receive a summary of the tracks in the
  // video file.
  // Returns whether the tracks were added successfully.
  bool AddPrimaryMp4FileTracks(const std::string& primary_file_name,
                               std::string* track_summary);

  //   moments_file_name: The name of the moments video file to add.
  //   track_summary: A string to receive a summary of the tracks in the
  // video file.
  // Returns whether the tracks were added successfully.
  bool AddMomentsMp4FileTrack(const std::string& moments_file_name,
                              std::string* track_summary);

  //   metadata: The moments track metadata to add to the meta track.
  // Returns whether the meta track was added successfully.
  bool AddMomentsMetadataTrack(const std::vector<uint8_t>& metadata);

  //   track_summary: A string to receive a summary of the tracks in the
  // optimized video file.
  //   file_size: A variable to receive the size of the optimized file.
  // Returns whether the working file was optimzied successfully to produce
  // the optimized file.
  bool OptimizeMp4File(std::string* track_summary, size_t* file_size);

  //   file_name: The name of the file to get the summary of.
  //   message_handler: An message handler for writing messages.
  // Returns a string containing the track summary of the file, or empty string
  // if the open failed.
  static std::string GetFileTrackSummary(
      const std::string& file_name, image_io::MessageHandler* message_handler);

  //   file_name: The name of the file to check.
  //   message_handler: An message handler for writing messages.
  // Returns whether the file can be optimized (e.g. doesn't contain HEVC).
  static bool IsOptimizationSupported(
      const std::string& file_name, image_io::MessageHandler* message_handler);

  // Strips the metadata track from an MP4 file in-place and optimizes it.
  //   file_name: The name of the file to modify.
  //   message_handler: An message handler for writing messages.
  // Returns whether the operation was successful.
  static bool StripMetadataTrack(const std::string& file_name,
                                 image_io::MessageHandler* message_handler);

 private:
  bool OpenFileIfNeeded();
  image_io::MessageHandler* message_handler_;
  std::string working_file_name_;
  MP4FileHandle file_handle_;
  bool open_file_failed_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MP4_MOTION_PHOTO_MP4_FILE_BUILDER_H_  // NOLINT
