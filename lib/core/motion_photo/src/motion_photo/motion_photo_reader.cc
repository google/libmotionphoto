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

#include "motion_photo/motion_photo_reader.h"

#include "image_io/xmp/xmp_reader.h"
#include "motion_photo/motion_photo_parser.h"
#include "motion_photo/video_metadata_reader.h"

namespace libmotionphoto {
namespace motion_photo {

bool MotionPhotoReader::ReadImageMetadata(std::string_view xmp_string,
                                          size_t* bytes_parsed) {
  MotionPhotoParser parser(message_handler_);
  return parser.ParseXmpString(xmp_string, motion_photo_, bytes_parsed);
}

bool MotionPhotoReader::ReadImageMetadata(
    const std::vector<image_io::DataRange>& ranges,
    image_io::DataSource* source,
    size_t* bytes_parsed) {
  MotionPhotoParser parser(message_handler_);
  return parser.ParseXmpRanges(ranges, source, motion_photo_, bytes_parsed);
}

bool MotionPhotoReader::FindAndDecodeMpvdBox(image_io::DataSource* source) {
  MotionPhotoParser parser(message_handler_);
  return parser.ScanMpvdBox(source, motion_photo_);
}

bool MotionPhotoReader::DecodeVideoMetadata(const uint8_t* bytes,
                                            size_t begin,
                                            size_t end) {
  if (begin > end) return false;
  if (!video_metadata_decoder_.DecodeMetadata(bytes, begin, end)) {
    return false;
  }
  motion_photo_->SetVideoMetadata(video_metadata_decoder_.GetMetadata());
  return true;
}

bool MotionPhotoReader::ReadVideoMetadata(std::string_view xmp_string,
                                          size_t* bytes_parsed) {
  image_io::XmpReader reader(message_handler_);
  VideoMetadataReader video_metadata_reader;
  reader.AddElementHandler(&video_metadata_reader);
  if (!reader.StartParse() || !reader.Parse(std::string(xmp_string)) || !reader.FinishParse()) {
    return false;
  }
  motion_photo_->SetVideoMetadata(video_metadata_reader.GetVideoMetadata());
  if (bytes_parsed) {
    *bytes_parsed = reader.GetBytesParsed();
  }
  return !reader.HasErrors();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
