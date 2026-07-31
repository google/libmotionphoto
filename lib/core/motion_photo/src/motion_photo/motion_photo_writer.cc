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

#include "motion_photo/motion_photo_writer.h"

#include "motion_photo/motion_photo_serializer.h"

namespace libmotionphoto {
namespace motion_photo {

void MotionPhotoWriter::WriteImageMetadata(std::ostream& os) const {
  MotionPhotoSerializer serializer(motion_photo_);
  serializer.SerializeImageMetadata(os);
}

void MotionPhotoWriter::WriteImageMetadata(std::string* xmp_string) const {
  MotionPhotoSerializer serializer(motion_photo_);
  serializer.SerializeImageMetadata(xmp_string);
}

void MotionPhotoWriter::EncodeVideoMetadata(std::vector<uint8_t>* bytes) const {
  MotionPhotoSerializer serializer(motion_photo_);
  serializer.EncodeVideoMetadata(bytes);
}

void MotionPhotoWriter::WriteVideoMetadata(std::string* xmp_string) const {
  MotionPhotoSerializer serializer(motion_photo_);
  serializer.SerializeVideoMetadata(xmp_string);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
