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
#include <memory>
#include <string>
#include <vector>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_source.h"
#include "motion_photo/common.h"
#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

class MediaSource;

// Lazy DataSource backed by pread() on an open file descriptor.
// Reads chunks on demand over [base_offset, base_offset + total_length) without
// buffering the entire file in memory.
class FdDataSource : public image_io::DataSource {
 public:
  FdDataSource(int fd, int64_t base_offset, int64_t total_length,
               bool owns_fd = false);
  ~FdDataSource() override;

  void Reset() override;

  std::shared_ptr<image_io::DataSegment> GetDataSegment(
      size_t begin, size_t min_size) override;

  TransferDataResult TransferData(
      const image_io::DataRange& data_range, size_t best_size,
      image_io::DataDestination* data_destination) override;

 private:
  std::shared_ptr<image_io::DataSegment> Read(size_t begin, size_t min_size);

  int fd_ = -1;
  int64_t base_offset_ = 0;
  int64_t total_length_ = 0;
  bool owns_fd_ = false;
  std::shared_ptr<image_io::DataSegment> current_data_segment_;
};

// Random-access DataSource view backed by a MediaSource with 64 KiB chunked
// caching for box/XMP inspection without buffering the whole payload.
class MediaSourceDataSource : public image_io::DataSource {
 public:
  explicit MediaSourceDataSource(const MediaSource& source) : source_(source) {}

  void Reset() override { cached_.reset(); }

  std::shared_ptr<image_io::DataSegment> GetDataSegment(
      size_t begin, size_t min_size) override;

  TransferDataResult TransferData(
      const image_io::DataRange& data_range, size_t best_size,
      image_io::DataDestination* data_destination) override;

 private:
  const MediaSource& source_;
  std::shared_ptr<image_io::DataSegment> cached_;
};

// Sniffs the container type (JPEG, HEIC, AVIF) from header bytes.
FileType SniffFileType(const uint8_t* data, size_t size);

// Same as SniffFileType, defaulting unrecognized headers to kJpeg.
inline FileType SniffFileTypeOrDefaultJpeg(const uint8_t* data, size_t size) {
  FileType sniffed = SniffFileType(data, size);
  return sniffed == FileType::kUnsupported ? FileType::kJpeg : sniffed;
}

// Returns the container primary-image MIME type for `file_type` (or "").
const char* GetPrimaryImageMimeType(FileType file_type);

// Copies `length` bytes at byte offset `start` from `data_source` into `out`.
bool ReadDataRange(image_io::DataSource* data_source, size_t start,
                   size_t length, uint8_t* out);

// Reads [begin, end) bytes from `data_source` into `out`.
bool ReadDataRangeToBytes(image_io::DataSource* data_source, size_t begin,
                          size_t end, std::vector<uint8_t>* out);

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
