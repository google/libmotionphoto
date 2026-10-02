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

#include "motion_photo/google_motion_photo_provider.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/message_handler.h"
#include "motion_photo/camera_metadata.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_reader.h"
#include "motion_photo_metadata.pb.h"
#include "absl/strings/match.h"

namespace libmotionphoto {
namespace motion_photo {

bool GoogleMotionPhotoProvider::Identify(const RawMetadataBlock& block) {
  if (block.type == "EXIF" && block.format_identifier == "standard.exif") {
    if (block.bytes.size() < 4) {
      return false;
    }
    bool is_little_endian = (block.bytes[0] == 0x49 && block.bytes[1] == 0x49 &&
                             block.bytes[2] == 0x2A && block.bytes[3] == 0x00);
    bool is_big_endian = (block.bytes[0] == 0x4D && block.bytes[1] == 0x4D &&
                           block.bytes[2] == 0x00 && block.bytes[3] == 0x2A);
    return is_little_endian || is_big_endian;
  }

  if (block.type == "ICC" && block.format_identifier == "standard.icc") {
    if (block.bytes.size() < 40) {
      return false;
    }
    return (block.bytes[36] == 0x61 && block.bytes[37] == 0x63 &&
            block.bytes[38] == 0x73 && block.bytes[39] == 0x70);
  }

  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    absl::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                          block.bytes.size());
    return (absl::StrContains(xml, "http://ns.google.com/photos/1.0/camera/") ||
            absl::StrContains(xml,
                              "http://ns.google.com/photos/1.0/container/")) &&
           (absl::StrContains(xml, "GCamera:MotionPhoto") ||
            absl::StrContains(xml, "Camera:MotionPhoto"));
  }

  if (block.type == "JFIF" && block.format_identifier == "standard.jfif") {
    if (block.bytes.size() < 9) {
      return false;
    }
    return block.bytes[0] == 1;
  }

  return false;
}

bool GoogleMotionPhotoProvider::DecodeToSemantic(
    const RawMetadataBlock& block,
    std::vector<uint8_t>* out_proto_bytes, std::string* out_type_url,
    std::string* error_message) {
  if (block.type != "XMP") {
    return false;
  }
  std::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                       block.bytes.size());

  MotionPhoto motion_photo;
  image_io::MessageHandler message_handler;
  MotionPhotoReader reader(&motion_photo, &message_handler);

  size_t bytes_parsed = 0;
  if (!reader.ReadImageMetadata(xml, &bytes_parsed)) {
    if (error_message) {
      *error_message = "Failed to parse XMP metadata";
    }
    return false;
  }

  // Populate proto
  MotionPhotoMetadata metadata;
  const CameraMetadata& camera_meta = motion_photo.GetCameraMetadata();

  if (motion_photo.IsMotionPhoto()) {
    metadata.set_type(MotionPhotoMetadata::MOTION_PHOTO_TYPE_MOTION_PHOTO);
    if (camera_meta.motion_photo_presentation_timestamp_us.WasAssigned() &&
        camera_meta.motion_photo_presentation_timestamp_us.IsValid()) {
      metadata.set_primary_image_timestamp_us(
          camera_meta.motion_photo_presentation_timestamp_us.GetValue());
    }
  } else {
    metadata.set_type(MotionPhotoMetadata::MOTION_PHOTO_TYPE_REGULAR_PHOTO);
  }

  // Version
  if (motion_photo.IsMotionPhoto()) {
    if (camera_meta.motion_photo_version.WasAssigned() &&
        camera_meta.motion_photo_version.IsValid()) {
      metadata.set_version(camera_meta.motion_photo_version.GetValue());
    }
  }

  // Set type URL
  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo.MotionPhotoMetadata";
  }

  // Serialize proto to bytes
  out_proto_bytes->resize(metadata.ByteSizeLong());
  if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                 out_proto_bytes->size())) {
    if (error_message) {
      *error_message = "Failed to serialize MotionPhotoMetadata proto";
    }
    return false;
  }

  return true;
}

bool GoogleMotionPhotoProvider::IsMotionPhoto(
    const std::string& type_url, const std::string& payload_bytes) const {
  if (type_url ==
      "type.googleapis.com/libmotionphoto.motion_photo.MotionPhotoMetadata") {
    MotionPhotoMetadata metadata;
    if (metadata.ParseFromString(payload_bytes)) {
      return metadata.type() ==
             MotionPhotoMetadata::MOTION_PHOTO_TYPE_MOTION_PHOTO;
    }
  }
  return false;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
