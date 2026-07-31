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

#ifndef MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_IMAGE_BUILDER_H_
#define MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_IMAGE_BUILDER_H_

#include <string>
#include <string_view>

#include "motion_photo/motion_photo_source.h"
#include "motion_photo/mpvd_box.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

/// Builds the primary image of a motion photo from a HEIF (HEIC/AVIF) and metadata.
class MotionPhotoHeifImageBuilder {
 public:
  /// @param output_file_name The name of the output file built (optional for direct build).
  /// @param message_handler A message handler for writing messages.
  MotionPhotoHeifImageBuilder(const std::string& output_file_name,
                              image_io::MessageHandler* message_handler);

  explicit MotionPhotoHeifImageBuilder(image_io::MessageHandler* message_handler);

  /// Direct streaming build: reads from @p image_source, adds metadata/padding, and writes to @p output_sink.
  bool Build(const MediaSource& image_source, std::string_view xmp_metadata,
             const MpvdBox* mpvd_box, DataSink* output_sink);

  /// Legacy file-based wrapper.
  bool AddImageFileAndMetadata(const std::string& image_file_name,
                               std::string_view xmp_metadata);

  /// @param mpvd_box The mpvd_box used to generate the padding.
  bool AddPaddingFromMpvdBox(const MpvdBox& mpvd_box);

 private:
  image_io::MessageHandler* message_handler_;
  std::string output_file_name_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_IMAGE_BUILDER_H_
