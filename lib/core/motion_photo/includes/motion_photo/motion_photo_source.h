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

#ifndef MOTION_PHOTO_MOTION_PHOTO_SOURCE_H_
#define MOTION_PHOTO_MOTION_PHOTO_SOURCE_H_

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// Abstract source of binary media data (files, memory buffers, or descriptors).
class MediaSource {
 public:
  virtual ~MediaSource() = default;

  // Returns the total size in bytes of the media data.
  virtual size_t GetSize() const = 0;

  // Reads `length` bytes starting at `offset` into `dest`.
  // Returns true on success, or false if out of range or IO error.
  virtual bool ReadAt(size_t offset, size_t length, uint8_t* dest) const = 0;

  // Returns a pointer to contiguous in-memory data if available, or nullptr.
  virtual const uint8_t* GetData() const { return nullptr; }

  // Returns the underlying file descriptor if available, or -1.
  virtual int GetFd() const { return -1; }

  // Creates a MediaSource from a file on disk.
  static std::unique_ptr<MediaSource> FromFile(
      const std::string& file_name, image_io::MessageHandler* message_handler);

  // Creates a MediaSource from an open file descriptor.
  static std::unique_ptr<MediaSource> FromFd(int fd, size_t offset = 0,
                                             size_t length = 0);

  // Creates a MediaSource wrapping a non-owning in-memory buffer.
  static std::unique_ptr<MediaSource> FromMemory(const uint8_t* data,
                                                 size_t size);

  // Creates a MediaSource from a shared DataSegment.
  static std::unique_ptr<MediaSource> FromDataSegment(
      std::shared_ptr<image_io::DataSegment> data_segment);
};

// Abstract destination sink for binary output data.
class DataSink {
 public:
  virtual ~DataSink() = default;

  // Writes `size` bytes from `data` to the sink.
  virtual bool Write(const uint8_t* data, size_t size) = 0;

  // High-performance transfer of `length` bytes from `source` starting at
  // `offset` directly to this sink (uses kernel zero-copy when available).
  virtual bool TransferFrom(const MediaSource& source, size_t offset,
                            size_t length) = 0;

  // Returns the underlying file descriptor if available, or -1.
  virtual int GetFd() const { return -1; }

  // Flushes any pending buffered writes.
  virtual bool Flush() { return true; }

  // Creates a DataSink writing to a file on disk.
  static std::unique_ptr<DataSink> ToFile(
      const std::string& file_name, image_io::MessageHandler* message_handler,
      bool append = false);

  // Creates a DataSink writing directly to a file descriptor.
  static std::unique_ptr<DataSink> ToFd(int fd);

  // Creates a DataSink writing to a vector in memory.
  static std::unique_ptr<DataSink> ToMemory(std::vector<uint8_t>* buffer);
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MOTION_PHOTO_SOURCE_H_
