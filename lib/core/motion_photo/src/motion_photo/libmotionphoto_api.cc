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

#include "libmotionphoto_api.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/base/types.h"
#include "image_io/utils/string_outputter.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "motion_photo/camera_metadata.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_parser.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_builder/function.h"
#include "motion_photo_checker/function.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_heif/motion_photo_heif_info_builder.h"
#include "motion_photo_metadata_engine/function.h"
#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"

namespace libmotionphoto {
namespace api {

namespace {

// Helper to write raw byte buffers to a disk file.
bool WriteBufferToFile(const std::string& path, const uint8_t* data, size_t size) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data), size);
  return out.good();
}

// Internal helper copying nested image_io fields into clean native structs.
void PopulateNativeMetadata(
    const ::libmotionphoto::motion_photo::MotionPhoto& photo,
    MotionPhotoMetadata* out) {
  if (out == nullptr) return;
  const auto& camera = photo.GetCameraMetadata();
  if (camera.motion_photo_presentation_timestamp_us.WasAssigned() &&
      camera.motion_photo_presentation_timestamp_us.IsValid()) {
    out->presentation_timestamp_us = camera.motion_photo_presentation_timestamp_us.GetValue();
  }
  const auto& container = photo.GetContainerMetadata();
  if (!container.version.GetValue().empty()) {
    try {
      out->container.version = std::stoi(container.version.GetValue());
    } catch (...) {
      out->container.version = 1;
    }
  }
  out->container.items.clear();
  for (const auto& item : container.items) {
    ContainerItem native_item;
    native_item.mime = item.mime.GetValue();
    native_item.semantic = item.semantic.GetValue();
    native_item.length = item.length.GetValue();
    native_item.padding = item.padding.GetValue();
    out->container.items.push_back(native_item);
  }
  if (out->container.items.size() >= 2) {
    out->primary_image_mime = out->container.items[0].mime;
    out->video_mime = out->container.items[1].mime;
    out->video_length = out->container.items[1].length;
    out->image_padding = out->container.items[0].padding;
  }
}

// Lazy DataSource backed by pread() on an open file descriptor.
// Only fetches requested chunks (e.g. initial 64-128 KB headers) into RAM,
// avoiding full-file buffering in memory.
class FdDataSource : public ::photos_editing_formats::image_io::DataSource {
 public:
  FdDataSource(int fd, int64_t base_offset, int64_t total_length,
               bool owns_fd = false)
      : fd_(fd),
        base_offset_(base_offset),
        total_length_(total_length),
        owns_fd_(owns_fd) {}

  ~FdDataSource() override {
    if (owns_fd_ && fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  void Reset() override { current_data_segment_.reset(); }

  std::shared_ptr<::photos_editing_formats::image_io::DataSegment>
  GetDataSegment(size_t begin, size_t min_size) override {
    if (current_data_segment_ && current_data_segment_->Contains(begin)) {
      size_t remaining = current_data_segment_->GetEnd() - begin;
      if (remaining >= min_size) {
        return current_data_segment_;
      }
    }
    current_data_segment_ = Read(begin, min_size);
    return current_data_segment_;
  }

  TransferDataResult TransferData(
      const ::photos_editing_formats::image_io::DataRange& data_range,
      size_t best_size,
      ::photos_editing_formats::image_io::DataDestination* data_destination)
      override {
    bool data_transferred = false;
    ::photos_editing_formats::image_io::DataDestination::TransferStatus status =
        ::photos_editing_formats::image_io::DataDestination::kTransferDone;
    if (data_destination && data_range.IsValid() && fd_ >= 0) {
      size_t min_size = std::min(data_range.GetLength(), best_size);
      if (current_data_segment_ &&
          current_data_segment_->GetLength() >= min_size &&
          current_data_segment_->GetDataRange().Contains(data_range)) {
        status = data_destination->Transfer(data_range, *current_data_segment_);
        data_transferred = true;
      } else {
        size_t chunk_size = std::max(min_size, static_cast<size_t>(64 * 1024));
        for (size_t begin = data_range.GetBegin(); begin < data_range.GetEnd();
             begin += chunk_size) {
          size_t end = std::min(data_range.GetEnd(), begin + chunk_size);
          auto data_segment = Read(begin, end - begin);
          if (data_segment && data_segment->GetLength() > 0) {
            status = data_destination->Transfer(data_segment->GetDataRange(),
                                                *data_segment);
            data_transferred = true;
          }
          if (status != ::photos_editing_formats::image_io::DataDestination::
                            kTransferOk ||
              !data_segment || data_segment->GetLength() == 0) {
            break;
          }
        }
      }
    }
    if (data_transferred) {
      return status == ::photos_editing_formats::image_io::DataDestination::
                           kTransferError
                 ? kTransferDataError
                 : kTransferDataSuccess;
    }
    return data_destination ? kTransferDataNone : kTransferDataError;
  }

 private:
  std::shared_ptr<::photos_editing_formats::image_io::DataSegment> Read(
      size_t begin, size_t min_size) {
    if (fd_ < 0 || begin >= static_cast<size_t>(total_length_)) {
      return nullptr;
    }
    size_t chunk_size = std::max(min_size, static_cast<size_t>(64 * 1024));
    size_t to_read =
        std::min(chunk_size, static_cast<size_t>(total_length_) - begin);
    if (to_read == 0) {
      return nullptr;
    }

    auto buffer =
        std::make_shared<std::vector<::photos_editing_formats::image_io::Byte>>(
            to_read);
    size_t total_bytes_read = 0;
    while (total_bytes_read < to_read) {
      ssize_t bytes = pread(fd_, buffer->data() + total_bytes_read,
                            to_read - total_bytes_read,
                            base_offset_ + begin + total_bytes_read);
      if (bytes <= 0) break;
      total_bytes_read += bytes;
    }

    if (total_bytes_read == 0) {
      return nullptr;
    }

    auto segment = ::photos_editing_formats::image_io::DataSegment::Create(
        ::photos_editing_formats::image_io::DataRange(begin,
                                                      begin + total_bytes_read),
        buffer->data(),
        ::photos_editing_formats::image_io::DataSegment::
            BufferDispositionPolicy::kDontDelete);
    return std::shared_ptr<::photos_editing_formats::image_io::DataSegment>(
        segment.get(),
        [segment, buffer](::photos_editing_formats::image_io::DataSegment*) {});
  }

  int fd_ = -1;
  int64_t base_offset_ = 0;
  int64_t total_length_ = 0;
  bool owns_fd_ = false;
  std::shared_ptr<::photos_editing_formats::image_io::DataSegment>
      current_data_segment_;
};

}  // namespace

// =========================================================================
// 1. Parsing & Inspection (Decoding / Metadata Extraction)
// =========================================================================

bool ParseMotionPhotoFromFile(const std::string& filepath,
                              MotionPhotoMetadata* out_metadata,
                              MessageCallback callback,
                              const HandlerOptions& options) {
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  if (callback) {
    message_handler.SetMessageWriter(
        std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
            new ::photos_editing_formats::image_io::StringOutputterMessageWriter(
                ::photos_editing_formats::image_io::StringOutputter([callback](const std::string& msg) {
                  callback(msg);
                }))));
  }

  ::libmotionphoto::motion_photo::MetadataEngine engine(&message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::GetFileTypeFromFileName(filepath);

  int fd = open(filepath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return false;
  }
  int64_t file_size = st.st_size;

  FdDataSource data_source(fd, /*base_offset=*/0, file_size, /*owns_fd=*/true);
  ::libmotionphoto::motion_photo::MetadataCollection collection =
      engine.Parse(&data_source, file_size, file_type);

  ::libmotionphoto::motion_photo::HandlerOptions engine_options;
  engine_options.disable_3p_plugins = options.disable_3p_plugins;
  engine_options.enabled_3p_plugins = options.enabled_3p_plugins;
  bool is_mp = engine.IsMotionPhoto(collection, engine_options);

  if (out_metadata != nullptr) {
    out_metadata->is_motion_photo = is_mp;

    ::libmotionphoto::motion_photo::MotionPhoto photo;
    ::libmotionphoto::motion_photo::MotionPhotoParser parser(&message_handler);
    size_t bytes_parsed = 0;

    for (const auto& block : collection.blocks()) {
      if (is_mp && block.has_video_length() && block.video_length() > 0) {
        out_metadata->video_length = block.video_length();
      }
      if (block.type() == ::libmotionphoto::motion_photo::BLOCK_TYPE_XMP ||
          block.format_identifier() == "XMP") {
        std::string_view xml(block.raw_bytes().data(), block.raw_bytes().size());
        if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
          PopulateNativeMetadata(photo, out_metadata);
        }
      }
    }

    if (::libmotionphoto::motion_photo::IsHeif(file_type)) {
      ::libmotionphoto::motion_photo::MotionPhotoHeifInfoBuilder heif_builder(&message_handler);
      if (heif_builder.Build(&data_source, file_size)) {
        const auto& xmp_range = heif_builder.GetXmpStringRange();
        if (xmp_range.GetLength() > 0) {
          std::shared_ptr<::photos_editing_formats::image_io::DataSegment> xmp_segment =
              data_source.GetDataSegment(xmp_range.GetBegin(), xmp_range.GetLength());
          if (xmp_segment) {
            std::string_view xml(
                reinterpret_cast<const char*>(xmp_segment->GetBuffer(xmp_range.GetBegin())),
                xmp_range.GetLength());
            if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
              PopulateNativeMetadata(photo, out_metadata);
            }
          }
        }
        if (heif_builder.GetMpvdBox().IsValid()) {
          out_metadata->video_length = heif_builder.GetMpvdBox().GetVideoLength();
          out_metadata->is_motion_photo = true;
          is_mp = true;
        }
      }
    }

    if (out_metadata->primary_image_mime.empty()) {
      out_metadata->primary_image_mime =
          (file_type == ::libmotionphoto::motion_photo::FileType::kHeic)
              ? "image/heic"
              : "image/jpeg";
    }
    if (out_metadata->video_mime.empty()) {
      out_metadata->video_mime = "video/mp4";
    }
    if (out_metadata->container.items.empty() && is_mp) {
      out_metadata->container.items.resize(2);
      out_metadata->container.items[0].mime = out_metadata->primary_image_mime;
      out_metadata->container.items[0].padding = out_metadata->image_padding;
      out_metadata->container.items[1].mime = out_metadata->video_mime;
      out_metadata->container.items[1].length = out_metadata->video_length;
    }
  }

  return is_mp;
}

bool ParseMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                            MotionPhotoMetadata* out_metadata,
                            MessageCallback callback,
                            const HandlerOptions& options) {
  if (fd < 0 || offset < 0 || length <= 0) {
    return false;
  }
  if (static_cast<uint64_t>(offset) >
      std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(length)) {
    return false;
  }
  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size >= 0) {
    if (offset + length > st.st_size) {
      return false;
    }
  }

  // Read a small 12-byte header to sniff file type without buffering entire
  // length.
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::FileType::kJpeg;
  char header_buf[12];
  ssize_t header_read = pread(fd, header_buf, sizeof(header_buf), offset);
  if (header_read >= 12 && header_buf[4] == 'f' && header_buf[5] == 't' &&
      header_buf[6] == 'y' && header_buf[7] == 'p') {
    file_type = ::libmotionphoto::motion_photo::FileType::kHeic;
  }

  ::photos_editing_formats::image_io::MessageHandler message_handler;
  if (callback) {
    message_handler.SetMessageWriter(
        std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
            new ::photos_editing_formats::image_io::StringOutputterMessageWriter(
                ::photos_editing_formats::image_io::StringOutputter([callback](const std::string& msg) {
                  callback(msg);
                }))));
  }

  ::libmotionphoto::motion_photo::MetadataEngine engine(&message_handler);

  FdDataSource data_source(fd, offset, length, /*owns_fd=*/false);

  ::libmotionphoto::motion_photo::MetadataCollection collection =
      engine.Parse(&data_source, length, file_type);

  ::libmotionphoto::motion_photo::HandlerOptions engine_options;
  engine_options.disable_3p_plugins = options.disable_3p_plugins;
  engine_options.enabled_3p_plugins = options.enabled_3p_plugins;
  bool is_mp = engine.IsMotionPhoto(collection, engine_options);

  if (out_metadata != nullptr) {
    out_metadata->is_motion_photo = is_mp;

    ::libmotionphoto::motion_photo::MotionPhoto photo;
    ::libmotionphoto::motion_photo::MotionPhotoParser parser(&message_handler);
    size_t bytes_parsed = 0;

    for (const auto& block : collection.blocks()) {
      if (is_mp && block.has_video_length() && block.video_length() > 0) {
        out_metadata->video_length = block.video_length();
      }
      if (block.type() == ::libmotionphoto::motion_photo::BLOCK_TYPE_XMP ||
          block.format_identifier() == "XMP") {
        std::string_view xml(block.raw_bytes().data(), block.raw_bytes().size());
        if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
          PopulateNativeMetadata(photo, out_metadata);
        }
      }
    }

    if (::libmotionphoto::motion_photo::IsHeif(file_type)) {
      ::libmotionphoto::motion_photo::MotionPhotoHeifInfoBuilder heif_builder(&message_handler);
      if (heif_builder.Build(&data_source, length)) {
        const auto& xmp_range = heif_builder.GetXmpStringRange();
        if (xmp_range.GetLength() > 0) {
          std::shared_ptr<::photos_editing_formats::image_io::DataSegment> xmp_segment =
              data_source.GetDataSegment(xmp_range.GetBegin(), xmp_range.GetLength());
          if (xmp_segment) {
            std::string_view xml(
                reinterpret_cast<const char*>(xmp_segment->GetBuffer(xmp_range.GetBegin())),
                xmp_range.GetLength());
            if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
              PopulateNativeMetadata(photo, out_metadata);
            }
          }
        }
        if (heif_builder.GetMpvdBox().IsValid()) {
          out_metadata->video_length = heif_builder.GetMpvdBox().GetVideoLength();
          out_metadata->is_motion_photo = true;
          is_mp = true;
        }
      }
    }

    if (out_metadata->primary_image_mime.empty()) {
      out_metadata->primary_image_mime =
          (file_type == ::libmotionphoto::motion_photo::FileType::kHeic)
              ? "image/heic"
              : "image/jpeg";
    }
    if (out_metadata->video_mime.empty()) {
      out_metadata->video_mime = "video/mp4";
    }
    if (out_metadata->container.items.empty() && is_mp) {
      out_metadata->container.items.resize(2);
      out_metadata->container.items[0].mime = out_metadata->primary_image_mime;
      out_metadata->container.items[0].padding = out_metadata->image_padding;
      out_metadata->container.items[1].mime = out_metadata->video_mime;
      out_metadata->container.items[1].length = out_metadata->video_length;
    }
  }

  return is_mp;
}

bool ParseMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                MotionPhotoMetadata* out_metadata,
                                MessageCallback callback,
                                const HandlerOptions& options) {
  if (data == nullptr || size == 0) {
    return false;
  }

  ::photos_editing_formats::image_io::MessageHandler message_handler;
  if (callback) {
    message_handler.SetMessageWriter(
        std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
            new ::photos_editing_formats::image_io::StringOutputterMessageWriter(
                ::photos_editing_formats::image_io::StringOutputter([callback](const std::string& msg) {
                  callback(msg);
                }))));
  }

  ::libmotionphoto::motion_photo::MetadataEngine engine(&message_handler);

  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::FileType::kJpeg;
  if (size >= 12 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' &&
      data[7] == 'p') {
    file_type = ::libmotionphoto::motion_photo::FileType::kHeic;
  }

  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(data_segment);

  ::libmotionphoto::motion_photo::MetadataCollection collection =
      engine.Parse(&data_source, size, file_type);

  ::libmotionphoto::motion_photo::HandlerOptions engine_options;
  engine_options.disable_3p_plugins = options.disable_3p_plugins;
  engine_options.enabled_3p_plugins = options.enabled_3p_plugins;
  bool is_mp = engine.IsMotionPhoto(collection, engine_options);

  if (out_metadata != nullptr) {
    out_metadata->is_motion_photo = is_mp;

    ::libmotionphoto::motion_photo::MotionPhoto photo;
    ::libmotionphoto::motion_photo::MotionPhotoParser parser(&message_handler);
    size_t bytes_parsed = 0;

    for (const auto& block : collection.blocks()) {
      if (is_mp && block.has_video_length() && block.video_length() > 0) {
        out_metadata->video_length = block.video_length();
      }
      if (block.type() == ::libmotionphoto::motion_photo::BLOCK_TYPE_XMP ||
          block.format_identifier() == "XMP") {
        std::string_view xml(block.raw_bytes().data(), block.raw_bytes().size());
        if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
          PopulateNativeMetadata(photo, out_metadata);
        }
      }
    }

    if (::libmotionphoto::motion_photo::IsHeif(file_type)) {
      ::libmotionphoto::motion_photo::MotionPhotoHeifInfoBuilder heif_builder(&message_handler);
      if (heif_builder.Build(&data_source, size)) {
        const auto& xmp_range = heif_builder.GetXmpStringRange();
        if (xmp_range.GetLength() > 0) {
          std::shared_ptr<::photos_editing_formats::image_io::DataSegment> xmp_segment =
              data_source.GetDataSegment(xmp_range.GetBegin(), xmp_range.GetLength());
          if (xmp_segment) {
            std::string_view xml(
                reinterpret_cast<const char*>(xmp_segment->GetBuffer(xmp_range.GetBegin())),
                xmp_range.GetLength());
            if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
              PopulateNativeMetadata(photo, out_metadata);
            }
          }
        }
        if (heif_builder.GetMpvdBox().IsValid()) {
          out_metadata->video_length = heif_builder.GetMpvdBox().GetVideoLength();
          out_metadata->is_motion_photo = true;
          is_mp = true;
        }
      }
    }

    if (out_metadata->primary_image_mime.empty()) {
      out_metadata->primary_image_mime =
          (file_type == ::libmotionphoto::motion_photo::FileType::kHeic)
              ? "image/heic"
              : "image/jpeg";
    }
    if (out_metadata->video_mime.empty()) {
      out_metadata->video_mime = "video/mp4";
    }
    if (out_metadata->container.items.empty() && is_mp) {
      out_metadata->container.items.resize(2);
      out_metadata->container.items[0].mime = out_metadata->primary_image_mime;
      out_metadata->container.items[0].padding = out_metadata->image_padding;
      out_metadata->container.items[1].mime = out_metadata->video_mime;
      out_metadata->container.items[1].length = out_metadata->video_length;
    }
  }

  return is_mp;
}

// =========================================================================
// 2. Container Validation (Checking)
// =========================================================================

int CheckMotionPhotoFromFile(const std::string& filepath,
                             MessageCallback callback) {
  ::photos_editing_formats::image_io::StringOutputter outputter([callback](const std::string& msg) {
    if (callback) callback(msg);
  });
  return ::libmotionphoto::motion_photo::CheckMotionPhoto(filepath, outputter);
}

int CheckMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                           MessageCallback callback) {
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFd(fd, offset, length, &metadata, callback)) {
    return 1;
  }
  return metadata.is_motion_photo ? 0 : 1;
}

int CheckMotionPhotoFromMemory(const uint8_t* data, size_t size,
                               MessageCallback callback) {
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromMemory(data, size, &metadata, callback)) {
    return 1;
  }
  return metadata.is_motion_photo ? 0 : 1;
}

// =========================================================================
// 3. Media & Metadata Extraction
// =========================================================================

bool ExtractPrimaryImageFromFile(const std::string& input_filepath,
                                 const std::string& output_image_path,
                                 MessageCallback callback) {
  int fd = open(input_filepath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return false;
  }
  bool res =
      ExtractPrimaryImageFromFd(fd, 0, st.st_size, output_image_path, callback);
  close(fd);
  return res;
}

bool ExtractPrimaryImageFromFd(int fd, int64_t offset, int64_t length,
                               const std::string& output_image_path,
                               MessageCallback callback) {
  if (fd < 0 || length <= 0 || output_image_path.empty()) return false;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFd(fd, offset, length, &metadata, callback) ||
      !metadata.is_motion_photo) {
    return false;
  }
  int64_t image_length = length - metadata.video_length;
  if (image_length <= 0 || image_length > length) return false;
  std::vector<uint8_t> buffer(image_length);
  if (lseek(fd, offset, SEEK_SET) < 0) return false;
  ssize_t bytes_read = read(fd, buffer.data(), image_length);
  if (bytes_read != static_cast<ssize_t>(image_length)) return false;
  return WriteBufferToFile(output_image_path, buffer.data(), image_length);
}

bool ExtractPrimaryImageFromMemory(const uint8_t* data, size_t size,
                                   const std::string& output_image_path,
                                   MessageCallback callback) {
  if (data == nullptr || size == 0 || output_image_path.empty()) return false;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromMemory(data, size, &metadata, callback) ||
      !metadata.is_motion_photo) {
    return false;
  }
  size_t image_length = size - metadata.video_length;
  if (image_length == 0 || image_length > size) return false;
  return WriteBufferToFile(output_image_path, data, image_length);
}

bool ExtractVideoTrackFromFile(const std::string& input_filepath,
                               const std::string& output_video_path,
                               MessageCallback callback) {
  int fd = open(input_filepath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return false;
  }
  bool res =
      ExtractVideoTrackFromFd(fd, 0, st.st_size, output_video_path, callback);
  close(fd);
  return res;
}

bool ExtractVideoTrackFromFd(int fd, int64_t offset, int64_t length,
                             const std::string& output_video_path,
                             MessageCallback callback) {
  if (fd < 0 || length <= 0 || output_video_path.empty()) return false;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFd(fd, offset, length, &metadata, callback) ||
      !metadata.is_motion_photo || metadata.video_length <= 0) {
    return false;
  }
  int64_t video_offset = length - metadata.video_length;
  if (video_offset < 0 || video_offset + metadata.video_length > length) return false;
  std::vector<uint8_t> buffer(metadata.video_length);
  if (lseek(fd, offset + video_offset, SEEK_SET) < 0) return false;
  ssize_t bytes_read = read(fd, buffer.data(), metadata.video_length);
  if (bytes_read != static_cast<ssize_t>(metadata.video_length)) return false;
  if (!WriteBufferToFile(output_video_path, buffer.data(), metadata.video_length)) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ::libmotionphoto::motion_photo::MotionPhotoMp4FileBuilder::StripMetadataTrack(
      output_video_path, &message_handler);
  return true;
}

bool ExtractVideoTrackFromMemory(const uint8_t* data, size_t size,
                                 const std::string& output_video_path,
                                 MessageCallback callback) {
  if (data == nullptr || size == 0 || output_video_path.empty()) return false;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromMemory(data, size, &metadata, callback) ||
      !metadata.is_motion_photo || metadata.video_length <= 0) {
    return false;
  }
  size_t video_offset = size - metadata.video_length;
  if (video_offset >= size) return false;
  if (!WriteBufferToFile(output_video_path, data + video_offset, metadata.video_length)) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ::libmotionphoto::motion_photo::MotionPhotoMp4FileBuilder::StripMetadataTrack(
      output_video_path, &message_handler);
  return true;
}

bool ExtractMotionPhotoFromFile(const ExtractorParams& params,
                                MessageCallback callback) {
  ::libmotionphoto::motion_photo::MotionPhotoExtractorParams internal_params;
  internal_params.motion_photo_file_name = params.motion_photo_filepath;
  internal_params.primary_image_file_name_output = params.primary_image_output_path;
  internal_params.video_file_name_output = params.video_output_path;
  internal_params.metadata_file_name_output = params.metadata_output_path;
  ::photos_editing_formats::image_io::StringOutputter outputter([callback](const std::string& msg) {
    if (callback) callback(msg);
  });
  int status = ::libmotionphoto::motion_photo::ExtractMotionPhoto(internal_params, outputter);
  return status == 0;
}

bool ExtractMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                              const ExtractorParams& params,
                              MessageCallback callback) {
  bool success = true;
  if (!params.primary_image_output_path.empty()) {
    success = success && ExtractPrimaryImageFromFd(fd, offset, length, params.primary_image_output_path, callback);
  }
  if (!params.video_output_path.empty()) {
    success = success && ExtractVideoTrackFromFd(fd, offset, length, params.video_output_path, callback);
  }
  return success;
}

bool ExtractMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                  const ExtractorParams& params,
                                  MessageCallback callback) {
  bool success = true;
  if (!params.primary_image_output_path.empty()) {
    success = success && ExtractPrimaryImageFromMemory(data, size, params.primary_image_output_path, callback);
  }
  if (!params.video_output_path.empty()) {
    success = success && ExtractVideoTrackFromMemory(data, size, params.video_output_path, callback);
  }
  return success;
}

int ExtractAgtmFromFile(const std::string& filepath,
                        MessageCallback callback) {
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFile(filepath, &metadata, callback)) {
    if (callback) callback("Error: Failed to parse motion photo metadata from file: " + filepath);
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0 ? metadata.presentation_timestamp_us : 0;
  ::libmotionphoto::motion_photo::MotionPhotoMetadataEngineParams params;
  params.motion_photo_file_name = filepath;
  params.extract_agtm = true;
  params.timestamp_us = target_ts;
  ::photos_editing_formats::image_io::StringOutputter outputter([callback](const std::string& msg) {
    if (callback) callback(msg);
  });
  return ::libmotionphoto::motion_photo::RunMetadataEngine(params, outputter);
}

int ExtractAgtmFromFd(int fd, int64_t offset, int64_t length,
                      MessageCallback callback) {
  if (fd < 0 || length <= 0) return 1;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFd(fd, offset, length, &metadata, callback)) {
    if (callback) callback("Error: Failed to parse motion photo metadata from fd");
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0 ? metadata.presentation_timestamp_us : 0;
  std::vector<uint8_t> buffer(length);
  if (lseek(fd, offset, SEEK_SET) < 0) return 1;
  ssize_t bytes_read = read(fd, buffer.data(), length);
  if (bytes_read != static_cast<ssize_t>(length)) return 1;
  return ExtractAgtmFromMemory(buffer.data(), length, callback);
}

int ExtractAgtmFromMemory(const uint8_t* data, size_t size,
                          MessageCallback callback) {
  if (data == nullptr || size == 0) {
    if (callback) callback("Error: Invalid memory buffer");
    return 1;
  }
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromMemory(data, size, &metadata, callback)) {
    if (callback) callback("Error: Failed to parse motion photo metadata from memory buffer");
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0 ? metadata.presentation_timestamp_us : 0;
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  if (callback) {
    message_handler.SetMessageWriter(
        std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
            new ::photos_editing_formats::image_io::StringOutputterMessageWriter(
                ::photos_editing_formats::image_io::StringOutputter([callback](const std::string& msg) {
                  callback(msg);
                }))));
  }
  ::libmotionphoto::motion_photo::MetadataEngine engine(&message_handler);
  std::string json = engine.ExtractAgtmAtTimestampMemory(data, size, target_ts);
  if (callback && !json.empty()) {
    callback(json);
  }
  return 0;
}

// =========================================================================
// 4. Motion Photo Authoring & Building (Creation)
// =========================================================================

int BuildMotionPhotoFromFile(const std::string& image_path,
                             const std::string& video_path,
                             const std::string& output_path,
                             int64_t presentation_timestamp_us,
                             MessageCallback callback) {
  ::libmotionphoto::motion_photo::MotionPhotoBuilderParams params;
  params.primary_image_file_name = image_path;
  params.primary_video_file_name = video_path;
  params.output_image_file_name = output_path;
  params.presentation_timestamp_us = presentation_timestamp_us;
  ::photos_editing_formats::image_io::StringOutputter outputter([callback](const std::string& msg) {
    if (callback) callback(msg);
  });
  return ::libmotionphoto::motion_photo::BuildMotionPhoto(params, outputter);
}

int BuildMotionPhotoFromFd(int image_fd, int64_t image_offset, int64_t image_length,
                           int video_fd, int64_t video_offset, int64_t video_length,
                           const std::string& output_path,
                           int64_t presentation_timestamp_us,
                           MessageCallback callback) {
  if (image_fd < 0 || image_length <= 0 || video_fd < 0 || video_length <= 0) {
    if (callback) callback("Error: Invalid input file descriptor or length");
    return 1;
  }
  std::vector<uint8_t> image_bytes(image_length);
  if (lseek(image_fd, image_offset, SEEK_SET) < 0) return 1;
  if (read(image_fd, image_bytes.data(), image_length) != static_cast<ssize_t>(image_length)) return 1;

  std::vector<uint8_t> video_bytes(video_length);
  if (lseek(video_fd, video_offset, SEEK_SET) < 0) return 1;
  if (read(video_fd, video_bytes.data(), video_length) != static_cast<ssize_t>(video_length)) return 1;

  return BuildMotionPhotoFromMemory(image_bytes.data(), image_length,
                                    video_bytes.data(), video_length,
                                    output_path, presentation_timestamp_us, callback);
}

int BuildMotionPhotoFromMemory(const uint8_t* image_bytes, size_t image_size,
                               const uint8_t* video_bytes, size_t video_size,
                               const std::string& output_path,
                               int64_t presentation_timestamp_us,
                               MessageCallback callback) {
  ::libmotionphoto::motion_photo::MotionPhotoBuilderMemoryParams params;
  params.primary_image_bytes = image_bytes;
  params.primary_image_size = image_size;
  params.primary_video_bytes = video_bytes;
  params.primary_video_size = video_size;
  params.output_image_file_name = output_path;
  params.presentation_timestamp_us = presentation_timestamp_us;
  ::photos_editing_formats::image_io::StringOutputter outputter([callback](const std::string& msg) {
    if (callback) callback(msg);
  });
  return ::libmotionphoto::motion_photo::BuildMotionPhotoFromMemory(params, outputter);
}

// =========================================================================
// 5. Library Version
// =========================================================================

std::string GetLibMotionPhotoVersion() {
  return "1.0.0";
}

}  // namespace api
}  // namespace libmotionphoto
