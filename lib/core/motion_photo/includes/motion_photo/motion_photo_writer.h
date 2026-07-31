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

#ifndef MOTION_PHOTO_MOTION_PHOTO_WRITER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MOTION_PHOTO_WRITER_H_  // NOLINT

#include <ostream>
#include <string>
#include <vector>

#include "motion_photo/motion_photo.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A class to write the image metadata of a motion photo to an XMP/XML string
// and to encode the video metadata to a byte array.
class MotionPhotoWriter {
 public:
  //   motion_photo: The motion photo used to obtain the metadata to write.
  MotionPhotoWriter(const MotionPhoto& motion_photo)
      : motion_photo_(motion_photo) {}

  //   os: The stream that is used to write the image metadata to.
  void WriteImageMetadata(std::ostream& os) const;

  //   xmp_string: A string that is used to receive the image metadata.
  void WriteImageMetadata(std::string* xmp_string) const;

  //   bytes: A byte vector to receive the encoded video metadata.
  void EncodeVideoMetadata(std::vector<uint8_t>* bytes) const;

  // Writes an xmp string containing the video metadata of the motion photo.
  // Normally the video metadata is encoded in a binary format This function
  // provides a way to write an XMP representation of the same data.
  //   xmp_string: A string that is used to receive the video metadata.
  void WriteVideoMetadata(std::string* xmp_string) const;

 private:
  const MotionPhoto& motion_photo_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MOTION_PHOTO_WRITER_H_  // NOLINT
