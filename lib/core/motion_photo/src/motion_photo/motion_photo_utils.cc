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
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

namespace {

bool IsXmlSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsAttrNameChar(char c) {
  return IsIdentChar(c) || c == ':' || c == '-' || c == '.';
}

size_t SkipXmlSpaces(std::string_view s, size_t pos) {
  while (pos < s.size() && IsXmlSpace(s[pos])) {
    ++pos;
  }
  return pos;
}

bool ContainsTargetName(std::initializer_list<std::string_view> names,
                        std::string_view target) {
  for (std::string_view name : names) {
    if (name == target) {
      return true;
    }
  }
  return false;
}

bool ContainsSemanticValue(std::string_view s, std::string_view semantic) {
  constexpr std::string_view kKey = "Item:Semantic";
  constexpr std::string_view kCloseTag = "</Item:Semantic>";
  size_t pos = 0;
  while ((pos = s.find(kKey, pos)) != std::string_view::npos) {
    const size_t after = pos + kKey.size();
    if (pos > 0 && s[pos - 1] == '<' && after < s.size() && s[after] == '>') {
      size_t val_start = SkipXmlSpaces(s, after + 1);
      if (val_start + semantic.size() <= s.size() &&
          s.substr(val_start, semantic.size()) == semantic) {
        size_t val_end = SkipXmlSpaces(s, val_start + semantic.size());
        if (val_end + kCloseTag.size() <= s.size() &&
            s.substr(val_end, kCloseTag.size()) == kCloseTag) {
          return true;
        }
      }
    } else if (pos == 0 || !IsIdentChar(s[pos - 1])) {
      size_t eq = SkipXmlSpaces(s, after);
      if (eq < s.size() && s[eq] == '=') {
        size_t qpos = SkipXmlSpaces(s, eq + 1);
        if (qpos < s.size() && (s[qpos] == '"' || s[qpos] == '\'')) {
          const char quote = s[qpos];
          const size_t vstart = qpos + 1;
          if (vstart + semantic.size() < s.size() &&
              s.substr(vstart, semantic.size()) == semantic &&
              s[vstart + semantic.size()] == quote) {
            return true;
          }
        }
      }
    }
    pos = after;
  }
  return false;
}

std::string RemoveXmlAttributes(
    std::string_view s, std::initializer_list<std::string_view> attr_names,
    bool trim_leading_ws) {
  std::string out;
  out.reserve(s.size());
  size_t copied = 0;
  size_t search_pos = 0;
  while (search_pos < s.size()) {
    const size_t eq = s.find('=', search_pos);
    if (eq == std::string_view::npos) {
      break;
    }
    size_t name_end = eq;
    while (name_end > copied && IsXmlSpace(s[name_end - 1])) {
      --name_end;
    }
    size_t name_start = name_end;
    while (name_start > copied && IsAttrNameChar(s[name_start - 1])) {
      --name_start;
    }
    const bool boundary_ok =
        (name_start == 0 || !IsIdentChar(s[name_start - 1]));
    if (!boundary_ok ||
        !ContainsTargetName(attr_names,
                            s.substr(name_start, name_end - name_start))) {
      search_pos = eq + 1;
      continue;
    }
    const size_t qpos = SkipXmlSpaces(s, eq + 1);
    if (qpos >= s.size() || (s[qpos] != '"' && s[qpos] != '\'')) {
      search_pos = eq + 1;
      continue;
    }
    const size_t qend = s.find(s[qpos], qpos + 1);
    if (qend == std::string_view::npos) {
      search_pos = eq + 1;
      continue;
    }
    size_t cut_start = name_start;
    if (trim_leading_ws) {
      while (cut_start > copied && IsXmlSpace(s[cut_start - 1])) {
        --cut_start;
      }
    }
    out.append(s.data() + copied, cut_start - copied);
    copied = qend + 1;
    search_pos = copied;
  }
  out.append(s.data() + copied, s.size() - copied);
  return out;
}

std::string RemoveSimpleElements(
    std::string_view s, std::initializer_list<std::string_view> elem_names) {
  std::string out;
  out.reserve(s.size());
  size_t copied = 0;
  size_t search_pos = 0;
  while (search_pos < s.size()) {
    const size_t lt = s.find('<', search_pos);
    if (lt == std::string_view::npos) {
      break;
    }
    const size_t gt = s.find('>', lt + 1);
    if (gt == std::string_view::npos) {
      break;
    }
    const std::string_view tag = s.substr(lt + 1, gt - (lt + 1));
    if (!ContainsTargetName(elem_names, tag)) {
      search_pos = lt + 1;
      continue;
    }
    const size_t next_lt = s.find('<', gt + 1);
    if (next_lt == std::string_view::npos || next_lt + 2 + tag.size() >= s.size() ||
        s[next_lt + 1] != '/' || s.substr(next_lt + 2, tag.size()) != tag ||
        s[next_lt + 2 + tag.size()] != '>') {
      search_pos = gt + 1;
      continue;
    }
    out.append(s.data() + copied, lt - copied);
    copied = next_lt + 3 + tag.size();
    search_pos = copied;
  }
  out.append(s.data() + copied, s.size() - copied);
  return out;
}

std::string RemoveContainerDirectory(std::string_view s) {
  constexpr std::string_view kOpen = "<Container:Directory>";
  constexpr std::string_view kClose = "</Container:Directory>";
  std::string out;
  out.reserve(s.size());
  size_t copied = 0;
  size_t search_pos = 0;
  while (search_pos < s.size()) {
    const size_t open_pos = s.find(kOpen, search_pos);
    if (open_pos == std::string_view::npos) {
      break;
    }
    const size_t close_pos = s.find(kClose, open_pos + kOpen.size());
    if (close_pos == std::string_view::npos) {
      break;
    }
    out.append(s.data() + copied, open_pos - copied);
    copied = close_pos + kClose.size();
    search_pos = copied;
  }
  out.append(s.data() + copied, s.size() - copied);
  return out;
}

size_t FindTagCloseQuoteAware(std::string_view s, size_t start) {
  char quote = '\0';
  for (size_t i = start; i < s.size(); ++i) {
    const char c = s[i];
    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      }
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      return i;
    }
  }
  return std::string_view::npos;
}

std::string RemoveMotionPhotoListItems(std::string_view s) {
  constexpr std::string_view kOpenLi = "<rdf:li";
  constexpr std::string_view kCloseLi = "</rdf:li>";
  std::string out;
  out.reserve(s.size());
  size_t copied = 0;
  size_t search_pos = 0;
  while (search_pos < s.size()) {
    const size_t lt = s.find(kOpenLi, search_pos);
    if (lt == std::string_view::npos) {
      break;
    }
    const size_t after_name = lt + kOpenLi.size();
    if (after_name >= s.size() ||
        (!IsXmlSpace(s[after_name]) && s[after_name] != '>' &&
         s[after_name] != '/')) {
      search_pos = after_name;
      continue;
    }
    const size_t gt = FindTagCloseQuoteAware(s, after_name);
    if (gt == std::string_view::npos) {
      break;
    }
    const bool self_closing = (gt > after_name - 1 && s[gt - 1] == '/');
    size_t elem_end = gt + 1;
    if (!self_closing) {
      const size_t close_pos = s.find(kCloseLi, gt + 1);
      if (close_pos == std::string_view::npos) {
        search_pos = gt + 1;
        continue;
      }
      const size_t next_li = s.find(kOpenLi, gt + 1);
      if (next_li != std::string_view::npos && next_li < close_pos) {
        search_pos = next_li;
        continue;
      }
      elem_end = close_pos + kCloseLi.size();
    }
    if (!ContainsSemanticValue(s.substr(lt, elem_end - lt), "MotionPhoto")) {
      search_pos = elem_end;
      continue;
    }
    size_t cut_start = lt;
    while (cut_start > copied && IsXmlSpace(s[cut_start - 1])) {
      --cut_start;
    }
    out.append(s.data() + copied, cut_start - copied);
    copied = elem_end;
    search_pos = copied;
  }
  out.append(s.data() + copied, s.size() - copied);
  return out;
}

}  // namespace

std::string StripMotionPhotoMetadata(const std::string& xmp) {
  const bool has_gainmap = ContainsSemanticValue(xmp, "GainMap");

  std::string result;
  if (has_gainmap) {
    result = RemoveXmlAttributes(
        xmp,
        {"Camera:MotionPhoto", "Camera:MotionPhotoVersion",
         "Camera:MotionPhotoPresentationTimestampUs", "GCamera:MotionPhoto",
         "GCamera:MotionPhotoVersion",
         "GCamera:MotionPhotoPresentationTimestampUs"},
        /*trim_leading_ws=*/false);
    result = RemoveSimpleElements(
        result,
        {"Camera:MotionPhoto", "Camera:MotionPhotoVersion",
         "Camera:MotionPhotoPresentationTimestampUs", "GCamera:MotionPhoto",
         "GCamera:MotionPhotoVersion",
         "GCamera:MotionPhotoPresentationTimestampUs"});
    result = RemoveMotionPhotoListItems(result);
  } else {
    result = RemoveXmlAttributes(
        xmp,
        {"Camera:MotionPhoto", "Camera:MotionPhotoVersion",
         "Camera:MotionPhotoPresentationTimestampUs", "GCamera:MotionPhoto",
         "GCamera:MotionPhotoVersion",
         "GCamera:MotionPhotoPresentationTimestampUs", "Container:Version"},
        /*trim_leading_ws=*/false);
    result = RemoveSimpleElements(
        result,
        {"Camera:MotionPhoto", "Camera:MotionPhotoVersion",
         "Camera:MotionPhotoPresentationTimestampUs", "GCamera:MotionPhoto",
         "GCamera:MotionPhotoVersion",
         "GCamera:MotionPhotoPresentationTimestampUs", "Container:Version"});
    result = RemoveContainerDirectory(result);
  }

  auto has_prefix_use = [](std::string_view text, std::string_view prefix) {
    const std::string needle = std::string(prefix) + ":";
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
      const bool is_xmlns =
          pos >= 6 && text.compare(pos - 6, 6, "xmlns:") == 0;
      if (!is_xmlns) {
        return true;
      }
      pos += needle.size();
    }
    return false;
  };
  for (const char* prefix : {"GCamera", "Camera", "Container", "Item"}) {
    if (!has_prefix_use(result, prefix)) {
      const std::string xmlns_attr = std::string("xmlns:") + prefix;
      result = RemoveXmlAttributes(result, {xmlns_attr},
                                   /*trim_leading_ws=*/true);
    }
  }

  return result;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
