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

#ifndef MOTION_PHOTO_ENGINE_H_
#define MOTION_PHOTO_ENGINE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_parser.h"
#include "motion_photo/motion_photo_serializer.h"
#include "motion_photo/motion_photo_validator.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// MotionPhotoEngine is the central coordinator for reading, building, checking,
// and serializing Google Motion Photo metadata documents.
class MotionPhotoEngine {
 public:
  explicit MotionPhotoEngine(image_io::MessageHandler* handler = nullptr);
  explicit MotionPhotoEngine(MotionPhoto photo, image_io::MessageHandler* handler = nullptr);
  ~MotionPhotoEngine() = default;

  // Ingests complete XMP metadata into the active document.
  bool IngestXmp(std::string_view xmp_string, size_t* bytes_parsed = nullptr);

  // Validates document against Google Motion Photo container rules and file
  // specs.
  DiagnosticReport Validate(std::string_view file_name = "");

  // Serializes active metadata model to string format.
  std::string ExportXmpString() const;

 private:
  MotionPhoto photo_;
  image_io::MessageHandler* handler_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_ENGINE_H_
