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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "motion_photo/motion_photo.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// Sniffs the container type (JPEG, HEIC, AVIF) from header bytes.
FileType SniffFileType(const uint8_t* data, size_t size);

// Same as SniffFileType, defaulting unrecognized headers to kJpeg.
inline FileType SniffFileTypeOrDefaultJpeg(const uint8_t* data, size_t size) {
  FileType sniffed = SniffFileType(data, size);
  return sniffed == FileType::kUnsupported ? FileType::kJpeg : sniffed;
}

// Returns the container primary-image MIME type for `file_type` (or "").
const char* GetPrimaryImageMimeType(FileType file_type);

// Reads and concatenates the byte ranges from `data_source` into `out`.
bool ReadDataRangesToString(image_io::DataSource* data_source,
                            const std::vector<image_io::DataRange>& ranges,
                            std::string* out);

//   file_name: The name of the file.
// Returns the type of the file based on the extension.
FileType GetFileTypeFromFileName(const std::string& file_name);

// Strips MotionPhoto-specific XMP attributes/elements while preserving
// unrelated XMP properties (and Container:Directory if a GainMap is present).
std::string StripMotionPhotoMetadata(const std::string& xmp);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_UTILS_H_ // NOLINT
