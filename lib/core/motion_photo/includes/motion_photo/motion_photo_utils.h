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

#ifndef MOTION_PHOTO_UTILS_H_  // NOLINT
#define MOTION_PHOTO_UTILS_H_  // NOLINT

#include <string>

#include "motion_photo/motion_photo.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

//   file_name: The name of the file.
// Returns the type of the file based on the extension.
FileType GetFileTypeFromFileName(const std::string& file_name);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_UTILS_H_ // NOLINT
