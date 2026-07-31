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

#ifndef MOTION_PHOTO_CAMERA_METADATA_WRITER_H_  // NOLINT
#define MOTION_PHOTO_CAMERA_METADATA_WRITER_H_  // NOLINT

#include "image_io/xmp/xmp_writer_source.h"
#include "motion_photo/camera_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// An XMP writer source for the Camera metadata used in a motion photo.
// Use an instance of this class with an XmpWriter.
class CameraMetadataWriter : public image_io::XmpWriterSource {
 public:
  //   metadata: The container metadata to write.
  explicit CameraMetadataWriter(const CameraMetadata& metadata);

  void WriteNamespaces(image_io::XmlWriter* writer) override;
  void WriteAttributeNamesAndValues(image_io::XmlWriter* writer) override;

 private:
  const CameraMetadata& metadata_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_CAMERA_METADATA_WRITER_H_  // NOLINT
