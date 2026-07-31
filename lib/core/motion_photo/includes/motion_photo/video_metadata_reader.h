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

#ifndef MOTION_PHOTO_VIDEO_METADATA_READER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_VIDEO_METADATA_READER_H_  // NOLINT

#include "image_io/xmp/xmp_content_handler.h"
#include "image_io/xmp/xmp_value.h"
#include "motion_photo/video_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// An XMP content handler for the Video metadata used in a motion photo.
// Normally the video metadata is encoded in a binary format and stored in the
// sample of a meta track in the MP4 portion of the motion photo file. This
// class provides a way to read an XMP representation of the same data.
// Use an instance of this class with an XmpReader.
class VideoMetadataReader : public image_io::XmpContentHandler {
 public:
  // Returns a sample xmp string that has placeholder values that can be
  // replaced with real data for the video metadata.
  static std::string GetSampleXmpString();

  VideoMetadataReader();
  void SetUriPrefix(const std::string& uri, const std::string& prefix) override;
  image_io::DataMatchResult ProcessElementName(
      const StringVector& element_name_stack, const std::string& element_name,
      const image_io::XmlTokenContext& context) override;
  image_io::DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const image_io::XmpContent& content,
      const image_io::XmlTokenContext& context) override;

  // Returns the camera metadata read from the XMP data.
  const MetadataDescriptor& GetVideoMetadata() const { return video_metadata_; }

 private:
  image_io::DataMatchResult CheckStablizedContext(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& content, const image_io::XmlTokenContext& context);
  image_io::DataMatchResult CheckModelVersionContext(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& content, const image_io::XmlTokenContext& context);
  image_io::DataMatchResult CheckScoreAndTimeContext(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& content, const image_io::XmlTokenContext& context);
  image_io::DataMatchResult SetScoreOrTime(
      const std::string& element_name, const std::string& content,
      const image_io::XmlTokenContext& context);
  image_io::DataMatchResult CheckFrameContext(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& content, const image_io::XmlTokenContext& context);
  MetadataDescriptor video_metadata_;
  image_io::XmpValue<float> current_frame_score_;
  image_io::XmpValue<int64_t> current_frame_time_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_VIDEO_METADATA_READER_H_  // NOLINT
