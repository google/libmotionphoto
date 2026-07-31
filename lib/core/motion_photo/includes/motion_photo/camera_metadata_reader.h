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

#ifndef MOTION_PHOTO_CAMERA_METADATA_READER_H_  // NOLINT
#define MOTION_PHOTO_CAMERA_METADATA_READER_H_  // NOLINT

#include "image_io/xmp/xmp_content_handler.h"
#include "motion_photo/camera_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// An XMP content handler for the Camera metadata used in a motion photo.
// Use an instance of this class with an XmpReader.
class CameraMetadataReader : public image_io::XmpContentHandler {
 public:
  CameraMetadataReader();
  void SetUriPrefix(const std::string& uri, const std::string& prefix) override;
  image_io::DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const image_io::XmpContent& content,
      const image_io::XmlTokenContext& context) override;

  // Returns the camera metadata read from the XMP data.
  const CameraMetadata& GetCameraMetadata() const { return camera_metadata_; }

 private:
  CameraMetadata camera_metadata_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_CAMERA_METADATA_READER_H_  // NOLINT
