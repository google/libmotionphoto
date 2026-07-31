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

#include "motion_photo/motion_photo_utils.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>

#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {
FileType GetFileTypeFromFileExtension(const std::string& file_name) {
  size_t start_extension_pos = file_name.find_last_of('.');

  if (start_extension_pos == std::string::npos) {
    return FileType::kUnsupported;
  }

  std::string file_extension = file_name.substr(start_extension_pos + 1);
  std::transform(file_extension.begin(), file_extension.end(),
                 file_extension.begin(), ::tolower);

  if (file_extension == "jpeg" || file_extension == "jpg") {
    return FileType::kJpeg;
  } else if (file_extension == "heic" || file_extension == "heif") {
    return FileType::kHeic;
  } else if (file_extension == "avif") {
    return FileType::kAvif;
  }

  return FileType::kUnsupported;
}
}  // namespace

//   file_name: The name of the file.
// Returns the type of the file based on the extension.
FileType GetFileTypeFromFileName(const std::string& file_name) {
  int fd = -1;
  if (file_name.rfind("/proc/self/fd/", 0) == 0) {
    try {
      fd = std::stoi(file_name.substr(14));
    } catch (...) {
      fd = -1;
    }
  }

  std::ifstream file(file_name, std::ios::binary);
  if (!file.is_open()) {
    return GetFileTypeFromFileExtension(file_name);
  }

  char header[12];
  file.read(header, 12);
  std::streamsize bytes_read = file.gcount();
  file.close();

  if (fd >= 0) {
    lseek(fd, 0, SEEK_SET);
  }

  if (bytes_read >= 2 &&
      static_cast<uint8_t>(header[0]) == 0xFF &&
      static_cast<uint8_t>(header[1]) == 0xD8) {
    return FileType::kJpeg;
  }

  if (bytes_read >= 12 &&
      header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
    std::string brand(header + 8, 4);
    if (brand == "heic" || brand == "heix" || brand == "mif1" || brand == "msf1") {
      return FileType::kHeic;
    } else if (brand == "avif" || brand == "avis") {
      return FileType::kAvif;
    }
  }

  return GetFileTypeFromFileExtension(file_name);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
