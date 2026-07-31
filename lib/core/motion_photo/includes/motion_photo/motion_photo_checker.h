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

#ifndef MOTION_PHOTO_MOTION_PHOTO_CHECKER_H_  // NOLINT
#define MOTION_PHOTO_MOTION_PHOTO_CHECKER_H_  // NOLINT

#include "motion_photo/motion_photo.h"
#include "motion_photo/video_track_data.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A checker for the image and video metadata in a motion photo.
class MotionPhotoChecker {
 public:
  //   motion_photo: The motion photo that is to be checked.
  //   message_handler: The handler to which messages are reported.
  MotionPhotoChecker(const MotionPhoto& motion_photo,
                     image_io::MessageHandler* message_handler)
      : motion_photo_(motion_photo),
        message_handler_(message_handler),
        error_count_(0),
        warning_count_(0) {}

  struct CheckResult {
    size_t error_count = 0;
    size_t warning_count = 0;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
  };

  // Returns the number of errors that were found by the check functions.
  size_t GetErrorCount() const { return error_count_; }

  // Returns the number of warnings that were reported by the check functions.
  size_t GetWarningCount() const { return warning_count_; }

  // Resets the accumulated error and warning counts for idempotent checking.
  void Reset() {
    error_count_ = 0;
    warning_count_ = 0;
  }

  // Checks the given file name against the pattern recommended by the motion
  // photo specification. If the file name does not follow the pattern, a
  // warning message is issued.
  //   file_name: The file name to check.
  // Returns whether the file name adhears to the recommended name pattern.
  bool CheckFileName(const std::string& file_name);

  // Checks the image metadata associated with the Camera elements and reports
  // one or more error message if the data is not correct.
  // Returns whether there were errors in the camera metadata.
  bool CheckCameraMetadata();

  // Checks the image metadata associated with the Container elements and
  // reports one or more error messages if the data is not correct.
  // Returns whether there were errors in the container metadata.
  bool CheckContainerMetadata();

  // Checks the container metadata for the primary item padding value, and
  // the video item length value with the given image size and file size.
  // The file size must equal the image size + padding + length; if not then
  // an error message is reported.
  //   image_size: The size of the motion photo's primary image.
  //   file_size: The size of the motion photo's file.
  // Returns whether required metadata is present and the sizes are consistent.
  // Note that if the video item's length value was not assigned or is not
  // valid, this function returns false, but no error message is issued.
  bool CheckContainerMetadataAndSizes(size_t image_size, size_t file_size);

  // Checks the video track data and reports error and/or warnings.
  //   track_data: The track data to use for the checks.
  // Returns whether there were errors.
  bool CheckVideoTrackData(const VideoTrackData& track_data);

  // Checks the video metadata for errors and for consistency with the image
  // metadata.
  // Returns whether there were errors in the video metadata.
  bool CheckVideoMetadata();

  // Checks if the mpvd box header definitions matches what is reported in the
  // metadata and whether the box's video contents length is consistent with
  // the file size. The checks on the box are only performed for kHeic type
  // motion photos; it will always return true for kJpeg type motion photos.
  //   file_size: The size of the motion photo's file.
  // Returns whether there were errors in the mpvd box.
  bool CheckMpvdBox(size_t file_size);

 private:
  // Called from the CheckContainerMetadata() function to check the container
  // item at the given index.
  //   index: The index of the container item to check.
  void CheckContainerItemMetadata(size_t index, size_t expected_item_count);

  // Checks the image metadata associated with the Camera elements when the
  // camera metadata indicates it is truly a motion photo, and reports
  // one or more error message if the data is not correct.
  // Returns whether there were errors in the camera metadata.
  bool CheckMotionPhotoCameraMetadata();

  // Reports an error with the given text.
  //   text: The text of the message to report.
  void ReportError(const std::string& text);

  // Reports a warning with the given text.
  //   text: The text of the message to report.
  void ReportWarning(const std::string& text);

  // The motion photo being checked.
  const MotionPhoto& motion_photo_;

  // The message hander to which messages are reported.
  image_io::MessageHandler* message_handler_;

  // The number of errors that have been reported.
  size_t error_count_;

  // The number of warnings that have been reported.
  size_t warning_count_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MOTION_PHOTO_CHECKER_H_  // NOLINT
