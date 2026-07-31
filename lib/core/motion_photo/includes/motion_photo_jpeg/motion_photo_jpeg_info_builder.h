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

#ifndef MOTION_PHOTO_JPEG_MOTION_PHOTO_JPEG_INFO_BUILDER_H_  // NOLINT
#define MOTION_PHOTO_JPEG_MOTION_PHOTO_JPEG_INFO_BUILDER_H_  // NOLINT

#include <vector>

#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/jpeg/jpeg_info_builder.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

/// A subclass of JpegInfoBuilder that collects information from the JPEG file
/// that houses a motion photo.
class MotionPhotoJpegInfoBuilder : public image_io::JpegInfoBuilder {
 public:
  MotionPhotoJpegInfoBuilder(image_io::MessageHandler* message_handler);
  void Process(image_io::JpegScanner* scanner,
               const image_io::JpegSegment& segment) override;

  /// @param data_source The data source from which to build jpeg info.
  /// @return Whether the information was built successfully.
  bool Build(image_io::DataSource* data_source);

  /// @return The data range in the data source that holds the primary XMP data.
  const image_io::DataRange& GetPrimaryXmpStringRange() const {
    return primary_xmp_string_range_;
  }

  /// @return The data range in the data source of the JPEG segment that holds
  /// the primary XMP data.
  const image_io::DataRange& GetPrimaryXmpSegmentRange() const {
    return primary_xmp_segment_range_;
  }

  /// @return The data ranges in the data source that holds the extended XMP
  /// data. Most motion photos don't need extended XMP segments.
  const std::vector<image_io::DataRange>& GetExtendedXmpStringRanges() const {
    return extended_xmp_string_ranges_;
  }

  /// @return The data ranges in the data source of the JPEG segments that hold
  /// the extended XMP data. Most motion photos don't need these segments.
  const std::vector<image_io::DataRange>& GetExtendedXmpSegmentRanges() const {
    return extended_xmp_segment_ranges_;
  }

 private:
  image_io::MessageHandler* message_handler_;
  image_io::DataRange primary_xmp_string_range_;
  image_io::DataRange primary_xmp_segment_range_;
  std::vector<image_io::DataRange> extended_xmp_string_ranges_;
  std::vector<image_io::DataRange> extended_xmp_segment_ranges_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_JPEG_MOTION_PHOTO_JPEG_INFO_BUILDER_H_  // NOLINT
