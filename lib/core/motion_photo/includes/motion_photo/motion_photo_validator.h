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

#ifndef MOTION_PHOTO_VALIDATOR_H_
#define MOTION_PHOTO_VALIDATOR_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "motion_photo/motion_photo.h"
#include "motion_photo/video_track_data.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// DiagnosticResult records errors, warnings, and diagnostic events
// captured during validation.
struct DiagnosticReport {
  size_t error_count = 0;
  size_t warning_count = 0;
};

// MotionPhotoValidator evaluates MotionPhoto instances against specification
// constraints, checking naming rules, camera metadata integrity, container
// schema layout, and MP4 track alignment.
class MotionPhotoValidator {
 public:
  explicit MotionPhotoValidator(const MotionPhoto& photo,
                                image_io::MessageHandler* handler = nullptr);
  ~MotionPhotoValidator() = default;

  // Validates file naming convention against recommended format regex patterns.
  bool ValidateFileName(std::string_view file_name);

  // Validates camera element metadata tags.
  bool ValidateCameraMetadata();

  // Validates container directory items and MIME types.
  bool ValidateContainerMetadata();

  // Checks container item offset alignment and size consistency.
  bool ValidateSizes(size_t image_size, size_t file_size);

  // Checks MP4 video track counts and track IDs.
  bool ValidateVideoTrackData(const VideoTrackData& track_data);

  // Validates frame score descriptor sorting and timestamp consistency.
  bool ValidateVideoMetadata();

  // Validates MPVD atom header length and embedded payload boundaries.
  bool ValidateMpvdBox(size_t file_size);

  DiagnosticReport GetReport() const { return report_; }
  size_t GetErrorCount() const { return report_.error_count; }
  size_t GetWarningCount() const { return report_.warning_count; }

 private:
  void ReportError(std::string_view message);
  void ReportWarning(std::string_view message);
  bool ValidateMotionPhotoCameraMetadata();

  const MotionPhoto& photo_;
  image_io::MessageHandler* handler_;
  DiagnosticReport report_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_VALIDATOR_H_
