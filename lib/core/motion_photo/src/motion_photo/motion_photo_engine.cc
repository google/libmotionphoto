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

#include "motion_photo/motion_photo_engine.h"

#include <utility>

namespace libmotionphoto {
namespace motion_photo {

MotionPhotoEngine::MotionPhotoEngine(image_io::MessageHandler* handler)
    : handler_(handler) {}

MotionPhotoEngine::MotionPhotoEngine(MotionPhoto photo,
                                     image_io::MessageHandler* handler)
    : photo_(std::move(photo)), handler_(handler) {}

bool MotionPhotoEngine::IngestXmp(std::string_view xmp_string,
                                  size_t* bytes_parsed) {
  MotionPhotoParser parser(handler_);
  return parser.ParseXmpString(xmp_string, &photo_, bytes_parsed);
}

DiagnosticReport MotionPhotoEngine::Validate(std::string_view file_name) {
  MotionPhotoValidator validator(photo_, handler_);
  if (!file_name.empty()) {
    validator.ValidateFileName(file_name);
  }
  validator.ValidateCameraMetadata();
  validator.ValidateContainerMetadata();
  validator.ValidateVideoMetadata();
  return validator.GetReport();
}

std::string MotionPhotoEngine::ExportXmpString() const {
  MotionPhotoSerializer serializer(photo_);
  std::string result;
  serializer.SerializeImageMetadata(&result);
  return result;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
