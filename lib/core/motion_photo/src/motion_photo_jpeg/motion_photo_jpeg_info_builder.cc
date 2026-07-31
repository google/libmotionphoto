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

#include "motion_photo_jpeg/motion_photo_jpeg_info_builder.h"

#include <cstddef>

#include "image_io/jpeg/jpeg_scanner.h"
#include "image_io/jpeg/jpeg_xmp_info.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataRange;
using image_io::DataSource;
using image_io::JpegInfoBuilder;
using image_io::JpegScanner;
using image_io::JpegSegment;
using image_io::kXmpExtendedHeaderSize;
using image_io::kXmpId;
using image_io::MessageHandler;

MotionPhotoJpegInfoBuilder::MotionPhotoJpegInfoBuilder(
    MessageHandler* message_handler)
    : message_handler_(message_handler) {
  SetImageLimit(2);
}

bool MotionPhotoJpegInfoBuilder::Build(DataSource* data_source) {
  JpegScanner scanner(message_handler_);
  scanner.Run(data_source, this);
  if (scanner.HasError() && GetInfo().GetImageRanges().empty()) {
    return false;
  }
  return primary_xmp_string_range_.IsValid();
}

void MotionPhotoJpegInfoBuilder::Process(JpegScanner* scanner,
                                         const JpegSegment& segment) {
  JpegInfoBuilder::Process(scanner, segment);
  if (segment.GetMarker().GetType() == image_io::JpegMarker::kEOI) {
    if (!GetInfo().GetImageRanges().empty()) {
      bool has_second_jpeg = false;
      size_t next_loc = segment.GetDataRange().GetEnd();
      DataSource* ds = scanner->GetDataSource();
      if (ds != nullptr) {
        auto next_segment = ds->GetDataSegment(next_loc, 16);
        if (next_segment && next_segment->Contains(next_loc)) {
          while (next_loc < next_segment->GetEnd() &&
                 next_segment->GetValidatedByte(next_loc).is_valid &&
                 next_segment->GetValidatedByte(next_loc).value == 0x00) {
            next_loc++;
          }
          auto b0 = next_segment->GetValidatedByte(next_loc);
          auto b1 = next_segment->GetValidatedByte(next_loc + 1);
          if (b0.is_valid && b1.is_valid && b0.value == 0xFF &&
              b1.value == image_io::JpegMarker::kSOI) {
            has_second_jpeg = true;
          }
        }
      }
      if (!has_second_jpeg) {
        scanner->SetDone();
      }
    }
  }
  if (IsPrimaryXmpSegment(segment) && GetInfo().GetImageRanges().empty()) {
    size_t xmp_str_begin = segment.GetPayloadDataLocation() + sizeof(kXmpId);
    size_t xmp_str_end = segment.GetDataRange().GetEnd();
    DataRange range(xmp_str_begin, xmp_str_end);
    if (range.IsValid()) {
      primary_xmp_string_range_ = range;
      primary_xmp_segment_range_ = segment.GetDataRange();
    }
  } else if (IsExtendedXmpSegment(segment) &&
             GetInfo().GetImageRanges().empty()) {
    if (primary_xmp_string_range_.IsValid()) {
      size_t xmp_str_begin =
          segment.GetPayloadDataLocation() + kXmpExtendedHeaderSize;
      size_t xmp_str_end = segment.GetDataRange().GetEnd();
      DataRange range(xmp_str_begin, xmp_str_end);
      if (range.IsValid()) {
        extended_xmp_string_ranges_.push_back(range);
        extended_xmp_segment_ranges_.push_back(segment.GetDataRange());
      }
    }
  }
}

}  // namespace motion_photo
}  // namespace libmotionphoto
