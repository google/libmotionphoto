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
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_source.h"
#include "image_io/base/types.h"
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
    size_t scan_end = (box_size == 0) ? size : std::min<size_t>(size, box_size);
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

FdDataSource::FdDataSource(int fd, int64_t base_offset, int64_t total_length,
                           bool owns_fd)
    : fd_(fd),
      base_offset_(base_offset),
      total_length_(total_length),
      owns_fd_(owns_fd) {}

FdDataSource::~FdDataSource() {
  if (owns_fd_ && fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

void FdDataSource::Reset() { current_data_segment_.reset(); }

std::shared_ptr<image_io::DataSegment> FdDataSource::GetDataSegment(
    size_t begin, size_t min_size) {
  if (current_data_segment_ && current_data_segment_->Contains(begin)) {
    size_t remaining = current_data_segment_->GetEnd() - begin;
    if (remaining >= min_size) {
      return current_data_segment_;
    }
  }
  current_data_segment_ = Read(begin, min_size);
  return current_data_segment_;
}

FdDataSource::TransferDataResult FdDataSource::TransferData(
    const image_io::DataRange& data_range, size_t best_size,
    image_io::DataDestination* data_destination) {
  bool data_transferred = false;
  image_io::DataDestination::TransferStatus status =
      image_io::DataDestination::kTransferDone;
  if (data_destination && data_range.IsValid() && fd_ >= 0) {
    size_t min_size = std::min(data_range.GetLength(), best_size);
    if (current_data_segment_ &&
        current_data_segment_->GetLength() >= min_size &&
        current_data_segment_->GetDataRange().Contains(data_range)) {
      status = data_destination->Transfer(data_range, *current_data_segment_);
      data_transferred = true;
    } else {
      size_t chunk_size = std::max(min_size, static_cast<size_t>(64 * 1024));
      size_t pos = data_range.GetBegin();
      while (pos < data_range.GetEnd()) {
        size_t end = pos + std::min(data_range.GetEnd() - pos, chunk_size);
        auto data_segment = Read(pos, end - pos);
        if (!data_segment) {
          break;
        }
        auto transfer_range = data_segment->GetDataRange().GetIntersection(
            image_io::DataRange(pos, end));
        if (!transfer_range.IsValid() || transfer_range.GetEnd() <= pos) {
          break;
        }
        status = data_destination->Transfer(transfer_range, *data_segment);
        data_transferred = true;
        pos = transfer_range.GetEnd();
        if (status != image_io::DataDestination::kTransferOk) {
          break;
        }
      }
    }
  }
  if (data_transferred) {
    return status == image_io::DataDestination::kTransferError
               ? kTransferDataError
               : kTransferDataSuccess;
  }
  return data_destination ? kTransferDataNone : kTransferDataError;
}

std::shared_ptr<image_io::DataSegment> FdDataSource::Read(size_t begin,
                                                          size_t min_size) {
  if (fd_ < 0 || total_length_ <= 0 ||
      begin >= static_cast<size_t>(total_length_)) {
    return nullptr;
  }
  const size_t total = static_cast<size_t>(total_length_);
  const size_t default_chunk = (begin < 256 * 1024)
                                   ? static_cast<size_t>(64 * 1024)
                                   : static_cast<size_t>(4 * 1024);
  size_t chunk_size = std::max(min_size, default_chunk);
  size_t read_begin = begin;
  size_t to_read = std::min(chunk_size, total - begin);
  if (begin >= 256 * 1024 && total - begin < chunk_size) {
    read_begin = (total > chunk_size) ? (total - chunk_size) : 0;
    to_read = total - read_begin;
  }
  if (to_read == 0) {
    return nullptr;
  }

  auto buffer = std::make_shared<std::vector<image_io::Byte>>(to_read);
  size_t total_bytes_read = 0;
  while (total_bytes_read < to_read) {
    ssize_t bytes =
        pread(fd_, buffer->data() + total_bytes_read, to_read - total_bytes_read,
              static_cast<off_t>(base_offset_ + read_begin + total_bytes_read));
    if (bytes <= 0) {
      if (bytes < 0 && errno == EINTR) continue;
      break;
    }
    total_bytes_read += static_cast<size_t>(bytes);
  }

  if (total_bytes_read <= (begin - read_begin)) {
    return nullptr;
  }

  auto segment = image_io::DataSegment::Create(
      image_io::DataRange(read_begin, read_begin + total_bytes_read),
      buffer->data(),
      image_io::DataSegment::BufferDispositionPolicy::kDontDelete);
  return std::shared_ptr<image_io::DataSegment>(
      segment.get(), [segment, buffer](image_io::DataSegment*) {});
}

bool ReadDataRange(image_io::DataSource* data_source, size_t start,
                   size_t length, uint8_t* out) {
  if (length == 0) {
    return data_source != nullptr;
  }
  if (data_source == nullptr || out == nullptr ||
      length > std::numeric_limits<size_t>::max() - start) {
    return false;
  }
  size_t copied = 0;
  while (copied < length) {
    const size_t chunk_start = start + copied;
    std::shared_ptr<image_io::DataSegment> segment =
        data_source->GetDataSegment(chunk_start, length - copied);
    if (!segment || !segment->Contains(chunk_start)) {
      return false;
    }
    const size_t avail =
        std::min(length - copied, segment->GetEnd() - chunk_start);
    const uint8_t* buffer = segment->GetBuffer(chunk_start);
    if (avail == 0 || buffer == nullptr) {
      return false;
    }
    std::memcpy(out + copied, buffer, avail);
    copied += avail;
  }
  return true;
}

bool ReadDataRangeToBytes(image_io::DataSource* data_source, size_t begin,
                          size_t end, std::vector<uint8_t>* out) {
  if (data_source == nullptr || out == nullptr || begin >= end) {
    return false;
  }
  const size_t len = end - begin;
  out->resize(len);
  if (!ReadDataRange(data_source, begin, len, out->data())) {
    out->clear();
    return false;
  }
  return true;
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
    const size_t old_size = out->size();
    const size_t len = range.GetLength();
    if (len > std::numeric_limits<size_t>::max() - old_size) {
      out->clear();
      return false;
    }
    out->resize(old_size + len);
    if (!ReadDataRange(data_source, range.GetBegin(), len,
                       reinterpret_cast<uint8_t*>(&(*out)[old_size]))) {
      out->clear();
      return false;
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

  if (result.find_first_not_of(" \t\r\n") == std::string::npos) {
    return "";
  }
  return result;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
