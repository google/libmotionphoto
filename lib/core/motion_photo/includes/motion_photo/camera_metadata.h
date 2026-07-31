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

#ifndef MOTION_PHOTO_CAMERA_METADATA_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_CAMERA_METADATA_H_  // NOLINT

#include "motion_photo/metadata_value.h"

namespace libmotionphoto {
namespace motion_photo {

// Constants for motion photo Camera metadata.
const char kCameraPrefix[] = "Camera";
const char kCameraUri[] = "http://ns.google.com/photos/1.0/camera/";
const char kMotionPhoto[] = "MotionPhoto";
const char kMotionPhotoVersion[] = "MotionPhotoVersion";
const char kMotionPhotoTimestamp[] = "MotionPhotoPresentationTimestampUs";

// The expected values for the metadata values.
const int32_t kMotionPhotoValue = 1;
const int32_t kMotionPhotoVersionValue = 1;

// The XMP Camera metadata used in a motion photo.
struct CameraMetadata {
  // The flag that indicates a motion photo is present in the container JPEG.
  MetadataValue<int32_t> motion_photo;

  // The version number of the motion photo metadata.
  MetadataValue<int32_t> motion_photo_version;

  // The timestamp of the primary image of the motion photo.
  MetadataValue<int64_t> motion_photo_presentation_timestamp_us;

  bool operator==(const CameraMetadata& rhs) const {
    return motion_photo == rhs.motion_photo &&
           motion_photo_version == rhs.motion_photo_version &&
           motion_photo_presentation_timestamp_us ==
               rhs.motion_photo_presentation_timestamp_us;
  }
  bool operator!=(const CameraMetadata& rhs) const { return !(*this == rhs); }
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_CAMERA_METADATA_H_  // NOLINT
