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

#ifndef MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_INFO_BUILDER_H_  // NOLINT
#define MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_INFO_BUILDER_H_  // NOLINT

#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "motion_photo/mpvd_box.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

/// A class that collects information from the HEIF (HEIC/AVIF) file that houses a motion
/// photo.
class MotionPhotoHeifInfoBuilder {
 public:
  MotionPhotoHeifInfoBuilder(image_io::MessageHandler* message_handler);

  /// @param data_source The data source from which to build heif info.
  /// @param data_source_length The length of the data source.
  /// @return Whether the information was built successfully.
  bool Build(image_io::DataSource* data_source, size_t data_source_length);

  /// @return The data range in the data source that holds the primary XMP data.
  const image_io::DataRange& GetXmpStringRange() const {
    return xmp_string_range_;
  }

  /// @return the MpvdBox; use the @c IsValid() function to determine if it was
  /// read and is valid.
  const MpvdBox& GetMpvdBox() const { return mpvd_box_; }

 private:
  image_io::MessageHandler* message_handler_;
  image_io::DataRange xmp_string_range_;
  MpvdBox mpvd_box_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_HEIF_MOTION_PHOTO_HEIF_INFO_BUILDER_H_  // NOLINT
