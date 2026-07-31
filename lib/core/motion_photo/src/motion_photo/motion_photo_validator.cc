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

#include "motion_photo/motion_photo_validator.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_checker.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::Message;

MotionPhotoValidator::MotionPhotoValidator(const MotionPhoto& photo,
                                           image_io::MessageHandler* handler)
    : photo_(photo), handler_(handler) {}

void MotionPhotoValidator::ReportError(std::string_view message) {
  if (handler_) {
    handler_->ReportMessage(Message::kValueError, std::string(message));
  }
  report_.error_count++;
}

void MotionPhotoValidator::ReportWarning(std::string_view message) {
  if (handler_) {
    handler_->ReportMessage(Message::kWarning, std::string(message));
  }
  report_.warning_count++;
}

bool MotionPhotoValidator::ValidateFileName(std::string_view file_name) {
  static const std::regex kJpegRegex("IMG_[a-zA-Z0-9_]+_MP.(JPG|jpg|JPEG|jpeg)", std::regex::optimize);
  static const std::regex kHeifRegex("IMG_[a-zA-Z0-9_]+_MP.(HEIC|heic|HEIF|heif|AVIF|avif)", std::regex::optimize);

  std::string_view base_name = file_name;
  size_t last_slash = base_name.rfind('/');
  if (last_slash != std::string_view::npos) {
    base_name = base_name.substr(last_slash + 1);
  }

  const std::regex* target_regex =
      (photo_.GetFileType() == FileType::kJpeg) ? &kJpegRegex : &kHeifRegex;
  std::string pattern_name =
      (photo_.GetFileType() == FileType::kJpeg)
          ? "IMG_[a-zA-Z0-9_]+_MP.(JPG|jpg|JPEG|jpeg)"
          : "IMG_[a-zA-Z0-9_]+_MP.(HEIC|heic|HEIF|heif|AVIF|avif)";

  std::string base_str(base_name);
  if (!std::regex_match(base_str, *target_regex)) {
    ReportWarning("The file name '" + base_str + "' does not follow the recommended pattern: '" + pattern_name + "'");
    return false;
  }
  return true;
}

bool MotionPhotoValidator::ValidateCameraMetadata() {
  return ValidateMotionPhotoCameraMetadata();
}

bool MotionPhotoValidator::ValidateMotionPhotoCameraMetadata() {
  const size_t initial_errors = report_.error_count;
  const auto& meta = photo_.GetCameraMetadata();

  if (!meta.motion_photo.WasAssigned()) {
    ReportError("The metadata does not contain a value for Camera:MotionPhoto");
  }
  if (!meta.motion_photo_version.WasAssigned()) {
    ReportError("The metadata does not contain a value for Camera:MotionPhotoVersion");
  }
  return report_.error_count == initial_errors;
}

bool MotionPhotoValidator::ValidateContainerMetadata() {
  MotionPhotoChecker checker(photo_, handler_);
  bool ok = checker.CheckContainerMetadata();
  report_.error_count = checker.GetErrorCount();
  report_.warning_count = checker.GetWarningCount();
  return ok;
}

bool MotionPhotoValidator::ValidateSizes(size_t image_size, size_t file_size) {
  MotionPhotoChecker checker(photo_, handler_);
  return checker.CheckContainerMetadataAndSizes(image_size, file_size);
}

bool MotionPhotoValidator::ValidateVideoTrackData(const VideoTrackData& track_data) {
  MotionPhotoChecker checker(photo_, handler_);
  bool ok = checker.CheckVideoTrackData(track_data);
  report_.error_count = checker.GetErrorCount();
  report_.warning_count = checker.GetWarningCount();
  return ok;
}

bool MotionPhotoValidator::ValidateVideoMetadata() {
  MotionPhotoChecker checker(photo_, handler_);
  bool ok = checker.CheckVideoMetadata();
  report_.error_count = checker.GetErrorCount();
  report_.warning_count = checker.GetWarningCount();
  return ok;
}

bool MotionPhotoValidator::ValidateMpvdBox(size_t file_size) {
  MotionPhotoChecker checker(photo_, handler_);
  bool ok = checker.CheckMpvdBox(file_size);
  report_.error_count = checker.GetErrorCount();
  report_.warning_count = checker.GetWarningCount();
  return ok;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
