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

#ifndef MOTION_PHOTO_SERIALIZER_H_
#include <cstdint>
#define MOTION_PHOTO_SERIALIZER_H_

#include <ostream>
#include <string>
#include <vector>

#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

// MotionPhotoSerializer is an optimized metadata generation module that
// serializes image, camera, container directory, and video track descriptor XMP
// metadata.
class MotionPhotoSerializer {
 public:
  explicit MotionPhotoSerializer(const MotionPhoto& photo);
  ~MotionPhotoSerializer() = default;

  // Writes complete image XMP metadata to an output stream.
  void SerializeImageMetadata(std::ostream& os) const;

  // Formats image XMP metadata into an output string buffer.
  void SerializeImageMetadata(std::string* xmp_out) const;

  // Encodes binary video track metadata descriptors.
  void EncodeVideoMetadata(std::vector<uint8_t>* bytes_out) const;

  // Generates human-readable or XML representation of video track descriptor
  // metadata.
  void SerializeVideoMetadata(std::string* xmp_out) const;

 private:
  const MotionPhoto& photo_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_SERIALIZER_H_
