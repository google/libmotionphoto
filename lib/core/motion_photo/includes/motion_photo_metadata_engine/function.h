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

#ifndef MOTION_PHOTO_METADATA_ENGINE_FUNCTION_H_  // NOLINT
#define MOTION_PHOTO_METADATA_ENGINE_FUNCTION_H_  // NOLINT

#include <cstdint>
#include <functional>
#include <string>

namespace libmotionphoto {
namespace motion_photo {

using StringOutputter = std::function<void(const std::string&)>;

struct MotionPhotoMetadataEngineParams {
  std::string motion_photo_file_name;
  bool extract_agtm = false;
  int64_t timestamp_us = 0;
  bool json_details = false;
};

int RunMetadataEngine(
    const MotionPhotoMetadataEngineParams& params,
    const StringOutputter& outputter);

/// The motion photo metadata engine entry point, easily callable from a main()
/// type function.
int MotionPhotoMetadataEngineFunction(
    int argc, const char* argv[],
    const StringOutputter& outputter);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_ENGINE_FUNCTION_H_  // NOLINT
