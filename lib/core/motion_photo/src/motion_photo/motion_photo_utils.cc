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
#include <cstring>
#include <fstream>
#include <memory>
#include <regex>
#include <string>

#include "image_io/base/data_segment.h"
#include "image_io/base/data_source.h"
#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {
bool MatchesFourCc(const uint8_t* bytes, const char (&tag)[5]) {
  return std::memcmp(bytes, tag, 4) == 0;
}

bool IsAvifBrand(const uint8_t* brand) {
  return MatchesFourCc(brand, "avif") || MatchesFourCc(brand, "avis");
}

bool IsHeicBrand(const uint8_t* brand) {
  return MatchesFourCc(brand, "heic") || MatchesFourCc(brand, "heix") ||
         MatchesFourCc(brand, "hevc") || MatchesFourCc(brand, "hevx") ||
         MatchesFourCc(brand, "mif1") || MatchesFourCc(brand, "msf1");
}

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

FileType SniffFileType(const uint8_t* data, size_t size) {
  if (data == nullptr || size < 2) {
    return FileType::kUnsupported;
  }
  if (data[0] == 0xFF && data[1] == 0xD8) {
    return FileType::kJpeg;
  }
  if (size < 12 || !MatchesFourCc(data + 4, "ftyp")) {
    return FileType::kUnsupported;
  }
  const uint8_t* major_brand = data + 8;
  if (IsAvifBrand(major_brand)) {
    return FileType::kAvif;
  }
  if (MatchesFourCc(major_brand, "mif1") || MatchesFourCc(major_brand, "msf1")) {
    uint32_t box_size = (static_cast<uint32_t>(data[0]) << 24) |
                        (static_cast<uint32_t>(data[1]) << 16) |
                        (static_cast<uint32_t>(data[2]) << 8) |
                        static_cast<uint32_t>(data[3]);
    size_t scan_end = box_size >= 16 ? std::min<size_t>(size, box_size) : size;
    for (size_t off = 16; off + 4 <= scan_end; off += 4) {
      if (IsAvifBrand(data + off)) {
        return FileType::kAvif;
      }
    }
    return FileType::kHeic;
  }
  if (IsHeicBrand(major_brand) || MatchesFourCc(major_brand, "heif") ||
      MatchesFourCc(major_brand, "heim") ||
      MatchesFourCc(major_brand, "heis")) {
    return FileType::kHeic;
  }
  return FileType::kUnsupported;
}

const char* GetPrimaryImageMimeType(FileType file_type) {
  switch (file_type) {
    case FileType::kJpeg:
      return kContainerJpegImageItemMime;
    case FileType::kHeic:
      return kContainerHeicImageItemMime;
    case FileType::kAvif:
      return kContainerAvifImageItemMime;
    case FileType::kUnsupported:
    default:
      return "";
  }
}

bool ReadDataRangesToString(image_io::DataSource* data_source,
                            const std::vector<image_io::DataRange>& ranges,
                            std::string* out) {
  if (data_source == nullptr || out == nullptr || ranges.empty()) {
    return false;
  }
  out->clear();
  for (const auto& range : ranges) {
    if (!range.IsValid() || range.GetLength() == 0) {
      out->clear();
      return false;
    }
    size_t pos = range.GetBegin();
    const size_t end = range.GetEnd();
    while (pos < end) {
      std::shared_ptr<image_io::DataSegment> segment =
          data_source->GetDataSegment(pos, end - pos);
      if (!segment || !segment->Contains(pos)) {
        out->clear();
        return false;
      }
      const size_t avail = std::min(end, segment->GetEnd()) - pos;
      if (avail == 0) {
        out->clear();
        return false;
      }
      const uint8_t* ptr = segment->GetBuffer(pos);
      if (ptr == nullptr) {
        out->clear();
        return false;
      }
      out->append(reinterpret_cast<const char*>(ptr), avail);
      pos += avail;
    }
  }
  return !out->empty();
}

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

  char header[64];
  file.read(header, sizeof(header));
  std::streamsize bytes_read = file.gcount();
  file.close();

  if (fd >= 0) {
    lseek(fd, 0, SEEK_SET);
  }

  if (bytes_read > 0) {
    FileType sniffed = SniffFileType(reinterpret_cast<const uint8_t*>(header),
                                     static_cast<size_t>(bytes_read));
    if (sniffed != FileType::kUnsupported) {
      return sniffed;
    }
  }

  return GetFileTypeFromFileExtension(file_name);
}

std::string StripMotionPhotoMetadata(const std::string& xmp) {
  std::string result = xmp;

  bool has_gainmap =
      (xmp.find("Item:Semantic=\"GainMap\"") != std::string::npos ||
       xmp.find("Item:Semantic='GainMap'") != std::string::npos ||
       xmp.find("<Item:Semantic>GainMap</Item:Semantic>") != std::string::npos);

  if (has_gainmap) {
    std::regex attr_regex(
        R"(\b(GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs)\s*=\s*("[^"]*"|'[^']*'))");
    result = std::regex_replace(result, attr_regex, "");

    std::regex elem_regex(
        R"(<((GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs))>[^<]*</\1>)");
    result = std::regex_replace(result, elem_regex, "");

    std::regex item_regex(
        R"raw(\s*<rdf:li\b[^>]*?Item:Semantic\s*=\s*(?:"MotionPhoto"|'MotionPhoto')[^>]*?/>|\s*<rdf:li\b(?:(?!</rdf:li>)[\s\S])*?(?:Item:Semantic\s*=\s*(?:"MotionPhoto"|'MotionPhoto')|<Item:Semantic>\s*MotionPhoto\s*</Item:Semantic>)(?:(?!</rdf:li>)[\s\S])*?</rdf:li>)raw");
    result = std::regex_replace(result, item_regex, "");
  } else {
    std::regex attr_regex(
        R"(\b(?:(?:GCamera|Camera):(?:MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs)|Container:Version)\s*=\s*("[^"]*"|'[^']*'))");
    result = std::regex_replace(result, attr_regex, "");

    std::regex elem_regex(
        R"(<((?:GCamera|Camera):(?:MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs)|Container:Version)>[^<]*</\1>)");
    result = std::regex_replace(result, elem_regex, "");

    std::regex directory_regex(
        R"(<Container:Directory>[\s\S]*?</Container:Directory>)");
    result = std::regex_replace(result, directory_regex, "");
  }

  auto has_prefix_use = [](const std::string& text, const std::string& prefix) {
    const std::string needle = prefix + ":";
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
      const bool is_xmlns = pos >= 6 && text.compare(pos - 6, 6, "xmlns:") == 0;
      if (!is_xmlns) {
        return true;
      }
      pos += needle.size();
    }
    return false;
  };
  for (const char* prefix : {"GCamera", "Camera", "Container", "Item"}) {
    if (!has_prefix_use(result, prefix)) {
      std::regex xmlns_regex(std::string(R"(\s*\bxmlns:)") + prefix +
                             R"(\s*=\s*("[^"]*"|'[^']*'))");
      result = std::regex_replace(result, xmlns_regex, "");
    }
  }

  return result;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
