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

#include "motion_photo/motion_photo_source.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/sendfile.h>
#include <sys/syscall.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "image_io/base/data_segment.h"
#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"

namespace libmotionphoto {
namespace motion_photo {

namespace image_io = ::photos_editing_formats::image_io;

namespace {

constexpr size_t kChunkBufferSize = 64 * 1024;

bool WriteAll(int fd, const uint8_t* data, size_t size) {
  size_t total_written = 0;
  while (total_written < size) {
    ssize_t written = write(fd, data + total_written, size - total_written);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    total_written += written;
  }
  return true;
}

// -----------------------------------------------------------------------------
// FileSource implementation
// -----------------------------------------------------------------------------
class FileSource : public MediaSource {
 public:
  FileSource(int fd, size_t offset, size_t length, bool owns_fd)
      : fd_(fd), offset_(offset), length_(length), owns_fd_(owns_fd) {}

  ~FileSource() override {
    if (owns_fd_ && fd_ >= 0) {
      close(fd_);
    }
  }

  size_t GetSize() const override { return length_; }

  bool ReadAt(size_t offset, size_t length, uint8_t* dest) const override {
    if (offset > length_ || length > length_ - offset || fd_ < 0) return false;
    if (offset_ > static_cast<size_t>(INT64_MAX) ||
        offset > static_cast<size_t>(INT64_MAX) - offset_) {
      return false;
    }
    off_t target_offset = static_cast<off_t>(offset_ + offset);
    size_t total_read = 0;
    while (total_read < length) {
      ssize_t bytes_read =
          pread(fd_, dest + total_read, length - total_read, target_offset + total_read);
      if (bytes_read <= 0) {
        if (bytes_read < 0 && errno == EINTR) continue;
        return false;
      }
      total_read += bytes_read;
    }
    return true;
  }

  int GetFd() const override { return fd_; }

 private:
  int fd_;
  size_t offset_;
  size_t length_;
  bool owns_fd_;
};

// -----------------------------------------------------------------------------
// MemorySource implementation
// -----------------------------------------------------------------------------
class MemoryMediaSource : public MediaSource {
 public:
  MemoryMediaSource(const uint8_t* data, size_t size)
      : data_(data), size_(size) {}

  size_t GetSize() const override { return size_; }

  bool ReadAt(size_t offset, size_t length, uint8_t* dest) const override {
    if (data_ == nullptr || length > size_ || offset > size_ - length) {
      return false;
    }
    std::memcpy(dest, data_ + offset, length);
    return true;
  }

  const uint8_t* GetData() const override { return data_; }

 private:
  const uint8_t* data_;
  size_t size_;
};

// -----------------------------------------------------------------------------
// DataSegmentSource implementation
// -----------------------------------------------------------------------------
class DataSegmentSource : public MediaSource {
 public:
  explicit DataSegmentSource(std::shared_ptr<image_io::DataSegment> segment)
      : segment_(std::move(segment)) {}

  size_t GetSize() const override {
    return segment_ ? segment_->GetDataRange().GetLength() : 0;
  }

  bool ReadAt(size_t offset, size_t length, uint8_t* dest) const override {
    if (!segment_ || length > GetSize() || offset > GetSize() - length) {
      return false;
    }
    const uint8_t* buf = segment_->GetBuffer(offset);
    if (!buf) return false;
    std::memcpy(dest, buf, length);
    return true;
  }

  const uint8_t* GetData() const override {
    return segment_ ? segment_->GetBuffer(0) : nullptr;
  }

 private:
  std::shared_ptr<image_io::DataSegment> segment_;
};

// -----------------------------------------------------------------------------
// FileSink implementation
// -----------------------------------------------------------------------------
class FileSink : public DataSink {
 public:
  FileSink(int fd, bool owns_fd) : fd_(fd), owns_fd_(owns_fd) {}

  ~FileSink() override {
    if (owns_fd_ && fd_ >= 0) {
      close(fd_);
    }
  }

  bool Write(const uint8_t* data, size_t size) override {
    if (fd_ < 0 || (data == nullptr && size > 0)) return false;
    return WriteAll(fd_, data, size);
  }

  bool TransferFrom(const MediaSource& source, size_t offset,
                    size_t length) override {
    if (length == 0) return true;
    if (offset > source.GetSize() || length > source.GetSize() - offset ||
        fd_ < 0) {
      return false;
    }

    // Fast path 1: Source in-memory data
    const uint8_t* mem = source.GetData();
    if (mem != nullptr) {
      return WriteAll(fd_, mem + offset, length);
    }

    size_t curr_offset = offset;
    size_t remaining = length;

    // Fast path 2: Kernel-to-kernel zero-copy splice / sendfile if both are FDs
    int src_fd = source.GetFd();
    if (src_fd >= 0) {
#if defined(__linux__) || defined(__ANDROID__)
#if defined(__NR_copy_file_range)
      off_t in_off = static_cast<off_t>(curr_offset);
      while (remaining > 0) {
        ssize_t ret = syscall(__NR_copy_file_range, src_fd, &in_off, fd_,
                              nullptr, remaining, 0);
        if (ret > 0) {
          curr_offset += ret;
          remaining -= ret;
        } else if (ret == 0) {
          break;
        } else {
          if (errno == EINTR) continue;
          break;
        }
      }
      if (remaining == 0) return true;
#endif

      // Try sendfile for any remaining portion
      off_t in_off_sendfile = static_cast<off_t>(curr_offset);
      while (remaining > 0) {
        ssize_t ret = sendfile(fd_, src_fd, &in_off_sendfile, remaining);
        if (ret > 0) {
          curr_offset += ret;
          remaining -= ret;
        } else if (ret == 0) {
          break;
        } else {
          if (errno == EINTR) continue;
          break;
        }
      }
      if (remaining == 0) return true;
#endif
    }

    // Stack buffer streaming fallback for any remaining portion
    uint8_t buffer[kChunkBufferSize];
    while (remaining > 0) {
      size_t to_read = std::min(remaining, kChunkBufferSize);
      if (!source.ReadAt(curr_offset, to_read, buffer)) {
        return false;
      }
      if (!WriteAll(fd_, buffer, to_read)) {
        return false;
      }
      curr_offset += to_read;
      remaining -= to_read;
    }
    return true;
  }

  int GetFd() const override { return fd_; }

  bool Flush() override {
    if (fd_ >= 0) {
      fsync(fd_);
    }
    return true;
  }

 private:
  int fd_;
  bool owns_fd_;
};

// -----------------------------------------------------------------------------
// MemorySink implementation
// -----------------------------------------------------------------------------
class MemoryDataSink : public DataSink {
 public:
  explicit MemoryDataSink(std::vector<uint8_t>* buffer) : buffer_(buffer) {}

  bool Write(const uint8_t* data, size_t size) override {
    if (buffer_ == nullptr || (data == nullptr && size > 0)) return false;
    buffer_->insert(buffer_->end(), data, data + size);
    return true;
  }

  bool TransferFrom(const MediaSource& source, size_t offset,
                    size_t length) override {
    if (buffer_ == nullptr) return false;
    if (length == 0) return true;
    if (offset > source.GetSize() || length > source.GetSize() - offset) {
      return false;
    }

    const uint8_t* mem = source.GetData();
    if (mem != nullptr) {
      buffer_->insert(buffer_->end(), mem + offset, mem + offset + length);
      return true;
    }

    size_t old_size = buffer_->size();
    buffer_->resize(old_size + length);
    return source.ReadAt(offset, length, buffer_->data() + old_size);
  }

 private:
  std::vector<uint8_t>* buffer_;
};

}  // namespace

// -----------------------------------------------------------------------------
// MediaSource Factory Functions
// -----------------------------------------------------------------------------

std::unique_ptr<MediaSource> MediaSource::FromFile(
    const std::string& file_name, image_io::MessageHandler* message_handler) {
  int fd = open(file_name.c_str(), O_RDONLY);
  if (fd < 0) {
    if (message_handler != nullptr) {
      message_handler->ReportMessage(image_io::Message::kStdLibError, file_name);
    }
    return nullptr;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    if (message_handler != nullptr) {
      message_handler->ReportMessage(image_io::Message::kStdLibError, file_name);
    }
    close(fd);
    return nullptr;
  }
  return std::make_unique<FileSource>(fd, 0, st.st_size, /*owns_fd=*/true);
}

std::unique_ptr<MediaSource> MediaSource::FromFd(int fd, size_t offset,
                                                 size_t length) {
  if (fd < 0) return nullptr;
  if (length == 0) {
    struct stat st;
    if (fstat(fd, &st) != 0) return nullptr;
    length = (st.st_size > static_cast<off_t>(offset)) ? (st.st_size - offset) : 0;
  }
  return std::make_unique<FileSource>(fd, offset, length, /*owns_fd=*/false);
}

std::unique_ptr<MediaSource> MediaSource::FromMemory(const uint8_t* data,
                                                     size_t size) {
  return std::make_unique<MemoryMediaSource>(data, size);
}

std::unique_ptr<MediaSource> MediaSource::FromDataSegment(
    std::shared_ptr<image_io::DataSegment> data_segment) {
  return std::make_unique<DataSegmentSource>(std::move(data_segment));
}

// -----------------------------------------------------------------------------
// DataSink Factory Functions
// -----------------------------------------------------------------------------

std::unique_ptr<DataSink> DataSink::ToFile(const std::string& file_name,
                                           image_io::MessageHandler* message_handler,
                                           bool append) {
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int fd = open(file_name.c_str(), flags, mode);
  if (fd < 0) {
    if (message_handler != nullptr) {
      message_handler->ReportMessage(image_io::Message::kStdLibError, file_name);
    }
    return nullptr;
  }
  return std::make_unique<FileSink>(fd, /*owns_fd=*/true);
}

std::unique_ptr<DataSink> DataSink::ToFd(int fd) {
  if (fd < 0) return nullptr;
  return std::make_unique<FileSink>(fd, /*owns_fd=*/false);
}

std::unique_ptr<DataSink> DataSink::ToMemory(std::vector<uint8_t>* buffer) {
  if (buffer == nullptr) return nullptr;
  return std::make_unique<MemoryDataSink>(buffer);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
