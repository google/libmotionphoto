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

#ifndef MOTION_PHOTO_BUILDER_FUNCTION_H_
#define MOTION_PHOTO_BUILDER_FUNCTION_H_

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motion_photo/motion_photo_source.h"

namespace libmotionphoto {
namespace motion_photo {

using StringOutputter = std::function<void(const std::string&)>;

struct MotionPhotoBuilderParams {
  std::string primary_image_file_name;
  std::string primary_video_file_name;
  std::string moments_video_file_name;
  std::string moments_xmp_file_name;
  std::string output_image_file_name;
  std::string working_video_file_name;
  std::string optimized_video_file_name;
  int64_t presentation_timestamp_us = -1;
};

struct MotionPhotoBuilderMemoryParams {
  const uint8_t* primary_image_bytes = nullptr;
  size_t primary_image_size = 0;
  const uint8_t* primary_video_bytes = nullptr;
  size_t primary_video_size = 0;
  std::string moments_xmp;
  std::string output_image_file_name;
  int64_t presentation_timestamp_us = -1;
};

struct MotionPhotoStreamParams {
  std::shared_ptr<MediaSource> primary_image;
  std::shared_ptr<MediaSource> primary_video;
  std::string moments_xmp;
  int64_t presentation_timestamp_us = -1;
};

/// Direct stream-to-sink builder without intermediate files or temp storage.
int BuildMotionPhotoDirect(const MotionPhotoStreamParams& params,
                           DataSink* output_sink,
                           const StringOutputter& outputter);

/// In-memory builder without disk storage or memfd workarounds.
int BuildMotionPhotoFromMemory(const MotionPhotoBuilderMemoryParams& params,
                               const StringOutputter& outputter);

/// File-based builder.
int BuildMotionPhoto(const MotionPhotoBuilderParams& params,
                     const StringOutputter& outputter);

/// The motion photo Builder CLI entry point.
int MotionPhotoBuilderFunction(int argc, const char* argv[],
                               const StringOutputter& outputter);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_BUILDER_FUNCTION_H_
