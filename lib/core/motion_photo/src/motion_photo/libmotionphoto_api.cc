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
#include <cerrno>
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

#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/utils/string_outputter.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "motion_photo/camera_metadata.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/metadata_value.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_parser.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo/mpvd_box.h"
#include "motion_photo_builder/function.h"
#include "motion_photo_checker/function.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_heif/motion_photo_heif_image_builder.h"
#include "motion_photo_heif/motion_photo_heif_info_builder.h"
#include "motion_photo_metadata_engine/function.h"
#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"

namespace libmotionphoto {
namespace api {

namespace {

// Helper to write raw byte buffers to a disk file.
bool WriteBufferToFile(const std::string& path, const uint8_t* data,
                       size_t size) {
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
    out->presentation_timestamp_us =
        camera.motion_photo_presentation_timestamp_us.GetValue();
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
  if (!out->container.items.empty()) {
    out->primary_image_mime = out->container.items[0].mime;
    out->image_padding = out->container.items[0].padding;
  }
  const ContainerItem* video_item = nullptr;
  for (size_t i = 1; i < out->container.items.size(); ++i) {
    if (::libmotionphoto::motion_photo::EqualsIgnoreCase(
            std::string_view(out->container.items[i].semantic),
            ::libmotionphoto::motion_photo::kContainerVideoItemSemantic)) {
      video_item = &out->container.items[i];
      break;
    }
  }
  if (video_item == nullptr && out->container.items.size() >= 2) {
    video_item = &out->container.items[1];
  }
  if (video_item != nullptr) {
    out->video_mime = video_item->mime;
    out->video_length = video_item->length;
  }
}

void ConfigureMessageHandler(
    MessageCallback callback,
    ::photos_editing_formats::image_io::MessageHandler* handler) {
  if (!callback || handler == nullptr) return;
  handler->SetMessageWriter(
      std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
          new ::photos_editing_formats::image_io::StringOutputterMessageWriter(
              ::photos_editing_formats::image_io::StringOutputter(
                  [callback](const std::string& msg) { callback(msg); }))));
}

bool ValidateFdSlice(int fd, int64_t offset, int64_t length) {
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
  return true;
}

bool PreadExact(int fd, int64_t offset, size_t size, uint8_t* out) {
  size_t total_read = 0;
  while (total_read < size) {
    ssize_t bytes =
        pread(fd, out + total_read, size - total_read,
              offset + static_cast<int64_t>(total_read));
    if (bytes <= 0) {
      if (bytes < 0 && errno == EINTR) continue;
      return false;
    }
    total_read += static_cast<size_t>(bytes);
  }
  return true;
}

}  // namespace

// =========================================================================
// 1. Parsing & Inspection (Decoding / Metadata Extraction)
// =========================================================================

namespace {

::libmotionphoto::motion_photo::FileType SniffFileTypeFromFd(int fd,
                                                             int64_t offset) {
  uint8_t header_buf[64];
  ssize_t header_read = pread(fd, header_buf, sizeof(header_buf), offset);
  if (header_read <= 0) {
    return ::libmotionphoto::motion_photo::FileType::kJpeg;
  }
  return ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(
      header_buf, static_cast<size_t>(header_read));
}

bool ParseMotionPhotoFromSource(
    ::photos_editing_formats::image_io::DataSource* data_source, size_t size,
    ::libmotionphoto::motion_photo::FileType file_type,
    MotionPhotoMetadata* out_metadata,
    ::photos_editing_formats::image_io::MessageHandler* message_handler,
    const HandlerOptions& options,
    std::string* out_raw_xmp = nullptr,
    ::libmotionphoto::motion_photo::MpvdBox* out_mpvd_box = nullptr) {
  ::libmotionphoto::motion_photo::MetadataEngine engine(message_handler);
  ::libmotionphoto::motion_photo::HandlerOptions engine_options;
  engine_options.disable_3p_plugins = options.disable_3p_plugins;
  engine_options.enabled_3p_plugins = options.enabled_3p_plugins;
  ::libmotionphoto::motion_photo::MetadataCollection collection =
      engine.Parse(data_source, size, file_type, engine_options);
  bool is_mp = engine.IsMotionPhoto(collection, engine_options);

  if (out_raw_xmp != nullptr) {
    out_raw_xmp->clear();
    for (const auto& block : collection.blocks()) {
      if (block.type() == ::libmotionphoto::motion_photo::BLOCK_TYPE_XMP &&
          !block.raw_bytes().empty()) {
        *out_raw_xmp = block.raw_bytes();
        break;
      }
    }
  }

  MotionPhotoMetadata local_metadata;
  MotionPhotoMetadata* metadata =
      out_metadata != nullptr ? out_metadata : &local_metadata;
  if (out_metadata != nullptr) {
    out_metadata->is_motion_photo = is_mp;
  }

  ::libmotionphoto::motion_photo::MotionPhoto photo;
  ::libmotionphoto::motion_photo::MotionPhotoParser parser(message_handler);
  size_t bytes_parsed = 0;

  for (const auto& block : collection.blocks()) {
    if (is_mp && block.has_video_length() && block.video_length() > 0) {
      metadata->video_length = block.video_length();
    }
    if (block.type() == ::libmotionphoto::motion_photo::BLOCK_TYPE_XMP ||
        block.format_identifier() == "XMP") {
      std::string_view xml(block.raw_bytes().data(), block.raw_bytes().size());
      if (out_metadata != nullptr &&
          parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
        PopulateNativeMetadata(photo, out_metadata);
      }
    }
  }

  if (::libmotionphoto::motion_photo::IsHeif(file_type)) {
    ::libmotionphoto::motion_photo::MotionPhotoHeifInfoBuilder heif_builder(
        message_handler);
    if (heif_builder.Build(data_source, size)) {
      const auto& xmp_ranges = heif_builder.GetXmpStringRanges();
      if (out_metadata != nullptr && !xmp_ranges.empty() &&
          parser.ParseXmpRanges(xmp_ranges, data_source, &photo,
                                &bytes_parsed)) {
        PopulateNativeMetadata(photo, out_metadata);
      }
      if (heif_builder.GetMpvdBox().IsValid()) {
        metadata->video_length = heif_builder.GetMpvdBox().GetVideoLength();
        is_mp = true;
      }
    }
    if (out_mpvd_box != nullptr && heif_builder.GetMpvdBox().IsValid()) {
      *out_mpvd_box = heif_builder.GetMpvdBox();
    }
  }

  if (out_metadata == nullptr) {
    return is_mp;
  }
  out_metadata->is_motion_photo = is_mp;
  if (out_metadata->primary_image_mime.empty()) {
    const char* fallback_mime =
        ::libmotionphoto::motion_photo::GetPrimaryImageMimeType(file_type);
    out_metadata->primary_image_mime =
        (fallback_mime[0] != '\0') ? fallback_mime : "image/jpeg";
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

  return is_mp;
}

}  // namespace

bool ParseMotionPhotoFromFile(const std::string& filepath,
                              MotionPhotoMetadata* out_metadata,
                              MessageCallback callback,
                              const HandlerOptions& options) {
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);

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

  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, /*base_offset=*/0, file_size, /*owns_fd=*/true);
  return ParseMotionPhotoFromSource(&data_source, static_cast<size_t>(file_size),
                                    file_type, out_metadata, &message_handler,
                                    options);
}

bool ParseMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                            MotionPhotoMetadata* out_metadata,
                            MessageCallback callback,
                            const HandlerOptions& options) {
  if (!ValidateFdSlice(fd, offset, length)) {
    return false;
  }

  ::libmotionphoto::motion_photo::FileType file_type =
      SniffFileTypeFromFd(fd, offset);

  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);

  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, offset, length, /*owns_fd=*/false);
  return ParseMotionPhotoFromSource(&data_source, static_cast<size_t>(length),
                                    file_type, out_metadata, &message_handler,
                                    options);
}

bool ParseMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                MotionPhotoMetadata* out_metadata,
                                MessageCallback callback,
                                const HandlerOptions& options) {
  if (data == nullptr || size == 0) {
    return false;
  }

  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);

  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(data, size);

  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  return ParseMotionPhotoFromSource(&data_source, size, file_type, out_metadata,
                                    &message_handler, options);
}

// =========================================================================
// 2. Container Validation (Checking)
// =========================================================================

int CheckMotionPhotoFromFile(const std::string& filepath,
                             MessageCallback callback) {
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  return ::libmotionphoto::motion_photo::CheckMotionPhoto(filepath, outputter);
}

int CheckMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                           MessageCallback callback) {
  if (!ValidateFdSlice(fd, offset, length)) {
    return 1;
  }
  ::libmotionphoto::motion_photo::FileType file_type =
      SniffFileTypeFromFd(fd, offset);
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, offset, length, /*owns_fd=*/false);
  return ::libmotionphoto::motion_photo::CheckMotionPhotoFromSource(
      &data_source, static_cast<size_t>(length), file_type, "", outputter);
}

int CheckMotionPhotoFromMemory(const uint8_t* data, size_t size,
                               MessageCallback callback) {
  if (data == nullptr || size == 0) {
    return 1;
  }
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(data, size);
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  return ::libmotionphoto::motion_photo::CheckMotionPhotoFromSource(
      &data_source, size, file_type, "", outputter);
}

// =========================================================================
// 3. Media & Metadata Extraction
// =========================================================================

namespace {

struct ExtractionLayout {
  bool is_motion_photo = false;
  bool is_heif = false;
  bool mpvd_is_trailing = false;
  size_t image_length = 0;
  size_t video_offset = 0;
  size_t video_length = 0;
  std::string metadata_xmp_bytes;
};

void ResolveExtractionLayoutFromSource(
    ::photos_editing_formats::image_io::DataSource* data_source, size_t size,
    ::libmotionphoto::motion_photo::FileType file_type, bool need_xmp_bytes,
    ::photos_editing_formats::image_io::MessageHandler* message_handler,
    ExtractionLayout* out) {
  MotionPhotoMetadata metadata;
  ::libmotionphoto::motion_photo::MpvdBox mpvd_box;
  const bool is_mp = ParseMotionPhotoFromSource(
      data_source, size, file_type, &metadata, message_handler,
      HandlerOptions(), need_xmp_bytes ? &out->metadata_xmp_bytes : nullptr,
      &mpvd_box);
  out->is_motion_photo = is_mp;
  out->is_heif = ::libmotionphoto::motion_photo::IsHeif(file_type);
  if (!is_mp) {
    return;
  }
  if (out->is_heif) {
    if (mpvd_box.IsValid() && mpvd_box.GetStartIndex() > 0) {
      out->mpvd_is_trailing =
          (mpvd_box.GetStartIndex() + mpvd_box.GetLength() == size);
      out->image_length =
          out->mpvd_is_trailing ? mpvd_box.GetStartIndex() : size;
      if (mpvd_box.GetVideoLength() > 0) {
        out->video_offset =
            mpvd_box.GetStartIndex() + mpvd_box.GetHeaderLength();
        out->video_length = mpvd_box.GetVideoLength();
      }
    }
    return;
  }
  if (metadata.video_length >= 0 &&
      static_cast<size_t>(metadata.video_length) <= size) {
    out->video_length = static_cast<size_t>(metadata.video_length);
    out->image_length = size - out->video_length;
    out->video_offset = size - out->video_length;
  }
}

bool ExtractPrimaryWithLayoutFromFd(
    int fd, int64_t offset, size_t total_size, const ExtractionLayout& layout,
    const std::string& output_image_path,
    ::photos_editing_formats::image_io::MessageHandler* message_handler) {
  if (!layout.is_motion_photo || layout.image_length == 0 ||
      layout.image_length > total_size) {
    return false;
  }
  // Reads only [0, layout.image_length) so trailing 'mpvd' video bytes are
  // never loaded into RAM when rebuilding still HEIF/AVIF metadata.
  std::vector<uint8_t> buffer(layout.image_length);
  if (!PreadExact(fd, offset, layout.image_length, buffer.data())) {
    return false;
  }
  if (layout.is_heif) {
    return ::libmotionphoto::motion_photo::MotionPhotoHeifImageBuilder::
        StripToStillImageFromMemory(
            buffer.data(), buffer.size(),
            layout.mpvd_is_trailing ? buffer.size() : 0, "", output_image_path,
            message_handler);
  }
  return WriteBufferToFile(output_image_path, buffer.data(), buffer.size());
}

bool ExtractPrimaryWithLayoutFromMemory(
    const uint8_t* data, size_t size, const ExtractionLayout& layout,
    const std::string& output_image_path,
    ::photos_editing_formats::image_io::MessageHandler* message_handler) {
  if (!layout.is_motion_photo || layout.image_length == 0 ||
      layout.image_length > size) {
    return false;
  }
  if (layout.is_heif) {
    return ::libmotionphoto::motion_photo::MotionPhotoHeifImageBuilder::
        StripToStillImageFromMemory(
            data, size, layout.mpvd_is_trailing ? layout.image_length : 0, "",
            output_image_path, message_handler);
  }
  return WriteBufferToFile(output_image_path, data, layout.image_length);
}

bool ExtractVideoWithLayoutFromFd(int fd, int64_t offset, size_t total_size,
                                  const ExtractionLayout& layout,
                                  const std::string& output_video_path) {
  if (!layout.is_motion_photo || layout.video_length == 0 ||
      layout.video_offset >= total_size ||
      layout.video_length > total_size - layout.video_offset) {
    return false;
  }
  std::vector<uint8_t> buffer(layout.video_length);
  if (!PreadExact(fd, offset + static_cast<int64_t>(layout.video_offset),
                  layout.video_length, buffer.data())) {
    return false;
  }
  if (!WriteBufferToFile(output_video_path, buffer.data(), buffer.size())) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ::libmotionphoto::motion_photo::MotionPhotoMp4FileBuilder::StripMetadataTrack(
      output_video_path, &message_handler);
  return true;
}

bool ExtractVideoWithLayoutFromMemory(const uint8_t* data, size_t size,
                                      const ExtractionLayout& layout,
                                      const std::string& output_video_path) {
  if (!layout.is_motion_photo || layout.video_length == 0 ||
      layout.video_offset >= size ||
      layout.video_length > size - layout.video_offset) {
    return false;
  }
  if (!WriteBufferToFile(output_video_path, data + layout.video_offset,
                         layout.video_length)) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ::libmotionphoto::motion_photo::MotionPhotoMp4FileBuilder::StripMetadataTrack(
      output_video_path, &message_handler);
  return true;
}

bool WriteExtractedMetadata(const ExtractionLayout& layout,
                            const std::string& output_path) {
  if (layout.metadata_xmp_bytes.empty()) {
    return false;
  }
  return WriteBufferToFile(
      output_path,
      reinterpret_cast<const uint8_t*>(layout.metadata_xmp_bytes.data()),
      layout.metadata_xmp_bytes.size());
}

}  // namespace

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
  if (!ValidateFdSlice(fd, offset, length) || output_image_path.empty()) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      SniffFileTypeFromFd(fd, offset);
  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, offset, length, /*owns_fd=*/false);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(
      &data_source, static_cast<size_t>(length), file_type,
      /*need_xmp_bytes=*/false, &message_handler, &layout);
  return ExtractPrimaryWithLayoutFromFd(fd, offset, static_cast<size_t>(length),
                                        layout, output_image_path,
                                        &message_handler);
}

bool ExtractPrimaryImageFromMemory(const uint8_t* data, size_t size,
                                   const std::string& output_image_path,
                                   MessageCallback callback) {
  if (data == nullptr || size == 0 || output_image_path.empty()) return false;
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(data, size);
  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(&data_source, size, file_type,
                                    /*need_xmp_bytes=*/false, &message_handler,
                                    &layout);
  return ExtractPrimaryWithLayoutFromMemory(data, size, layout,
                                            output_image_path,
                                            &message_handler);
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
  if (!ValidateFdSlice(fd, offset, length) || output_video_path.empty()) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      SniffFileTypeFromFd(fd, offset);
  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, offset, length, /*owns_fd=*/false);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(
      &data_source, static_cast<size_t>(length), file_type,
      /*need_xmp_bytes=*/false, &message_handler, &layout);
  return ExtractVideoWithLayoutFromFd(fd, offset, static_cast<size_t>(length),
                                      layout, output_video_path);
}

bool ExtractVideoTrackFromMemory(const uint8_t* data, size_t size,
                                 const std::string& output_video_path,
                                 MessageCallback callback) {
  if (data == nullptr || size == 0 || output_video_path.empty()) return false;
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(data, size);
  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(&data_source, size, file_type,
                                    /*need_xmp_bytes=*/false, &message_handler,
                                    &layout);
  return ExtractVideoWithLayoutFromMemory(data, size, layout,
                                          output_video_path);
}

bool ExtractMotionPhotoFromFile(const ExtractorParams& params,
                                MessageCallback callback) {
  ::libmotionphoto::motion_photo::MotionPhotoExtractorParams internal_params;
  internal_params.motion_photo_file_name = params.motion_photo_filepath;
  internal_params.primary_image_file_name_output =
      params.primary_image_output_path;
  internal_params.video_file_name_output = params.video_output_path;
  internal_params.metadata_file_name_output = params.metadata_output_path;
  internal_params.strip_video_metadata_track_on_trailer = true;
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  int status = ::libmotionphoto::motion_photo::ExtractMotionPhoto(
      internal_params, outputter);
  return status == 0;
}

bool ExtractMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                              const ExtractorParams& params,
                              MessageCallback callback) {
  if (!ValidateFdSlice(fd, offset, length)) {
    return false;
  }
  if (params.primary_image_output_path.empty() &&
      params.video_output_path.empty() && params.metadata_output_path.empty()) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      SniffFileTypeFromFd(fd, offset);
  ::libmotionphoto::motion_photo::FdDataSource data_source(
      fd, offset, length, /*owns_fd=*/false);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(
      &data_source, static_cast<size_t>(length), file_type,
      /*need_xmp_bytes=*/!params.metadata_output_path.empty(),
      &message_handler, &layout);

  const size_t total_size = static_cast<size_t>(length);
  bool success = true;
  if (!params.primary_image_output_path.empty()) {
    success = ExtractPrimaryWithLayoutFromFd(
        fd, offset, total_size, layout, params.primary_image_output_path,
        &message_handler);
  }
  if (success && !params.video_output_path.empty()) {
    success = ExtractVideoWithLayoutFromFd(fd, offset, total_size, layout,
                                           params.video_output_path);
  }
  if (success && !params.metadata_output_path.empty()) {
    success = WriteExtractedMetadata(layout, params.metadata_output_path);
  }
  return success;
}

bool ExtractMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                  const ExtractorParams& params,
                                  MessageCallback callback) {
  if (data == nullptr || size == 0) {
    return false;
  }
  if (params.primary_image_output_path.empty() &&
      params.video_output_path.empty() && params.metadata_output_path.empty()) {
    return false;
  }
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  ConfigureMessageHandler(callback, &message_handler);
  ::libmotionphoto::motion_photo::FileType file_type =
      ::libmotionphoto::motion_photo::SniffFileTypeOrDefaultJpeg(data, size);
  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, size), data,
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  ExtractionLayout layout;
  ResolveExtractionLayoutFromSource(
      &data_source, size, file_type,
      /*need_xmp_bytes=*/!params.metadata_output_path.empty(),
      &message_handler, &layout);

  bool success = true;
  if (!params.primary_image_output_path.empty()) {
    success = ExtractPrimaryWithLayoutFromMemory(
        data, size, layout, params.primary_image_output_path, &message_handler);
  }
  if (success && !params.video_output_path.empty()) {
    success = ExtractVideoWithLayoutFromMemory(data, size, layout,
                                               params.video_output_path);
  }
  if (success && !params.metadata_output_path.empty()) {
    success = WriteExtractedMetadata(layout, params.metadata_output_path);
  }
  return success;
}

int ExtractAgtmFromFile(const std::string& filepath, MessageCallback callback) {
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFile(filepath, &metadata, callback)) {
    if (callback)
      callback("Error: Failed to parse motion photo metadata from file: " +
               filepath);
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0
                          ? metadata.presentation_timestamp_us
                          : 0;
  ::libmotionphoto::motion_photo::MotionPhotoMetadataEngineParams params;
  params.motion_photo_file_name = filepath;
  params.extract_agtm = true;
  params.timestamp_us = target_ts;
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  return ::libmotionphoto::motion_photo::RunMetadataEngine(params, outputter);
}

int ExtractAgtmFromFd(int fd, int64_t offset, int64_t length,
                      MessageCallback callback) {
  if (fd < 0 || length <= 0) return 1;
  MotionPhotoMetadata metadata;
  if (!ParseMotionPhotoFromFd(fd, offset, length, &metadata, callback)) {
    if (callback)
      callback("Error: Failed to parse motion photo metadata from fd");
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0
                          ? metadata.presentation_timestamp_us
                          : 0;
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
    if (callback)
      callback(
          "Error: Failed to parse motion photo metadata from memory buffer");
    return 1;
  }
  int64_t target_ts = metadata.presentation_timestamp_us >= 0
                          ? metadata.presentation_timestamp_us
                          : 0;
  ::photos_editing_formats::image_io::MessageHandler message_handler;
  if (callback) {
    message_handler.SetMessageWriter(
        std::unique_ptr<::photos_editing_formats::image_io::MessageWriter>(
            new ::photos_editing_formats::image_io::
                StringOutputterMessageWriter(
                    ::photos_editing_formats::image_io::StringOutputter(
                        [callback](const std::string& msg) {
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
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  return ::libmotionphoto::motion_photo::BuildMotionPhoto(params, outputter);
}

int BuildMotionPhotoFromFd(int image_fd, int64_t image_offset,
                           int64_t image_length, int video_fd,
                           int64_t video_offset, int64_t video_length,
                           const std::string& output_path,
                           int64_t presentation_timestamp_us,
                           MessageCallback callback) {
  if (image_fd < 0 || image_length <= 0 || video_fd < 0 || video_length <= 0) {
    if (callback) callback("Error: Invalid input file descriptor or length");
    return 1;
  }
  std::vector<uint8_t> image_bytes(image_length);
  if (lseek(image_fd, image_offset, SEEK_SET) < 0) return 1;
  if (read(image_fd, image_bytes.data(), image_length) !=
      static_cast<ssize_t>(image_length))
    return 1;

  std::vector<uint8_t> video_bytes(video_length);
  if (lseek(video_fd, video_offset, SEEK_SET) < 0) return 1;
  if (read(video_fd, video_bytes.data(), video_length) !=
      static_cast<ssize_t>(video_length))
    return 1;

  return BuildMotionPhotoFromMemory(
      image_bytes.data(), image_length, video_bytes.data(), video_length,
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
  ::photos_editing_formats::image_io::StringOutputter outputter(
      [callback](const std::string& msg) {
        if (callback) callback(msg);
      });
  return ::libmotionphoto::motion_photo::BuildMotionPhotoFromMemory(params,
                                                                    outputter);
}

// =========================================================================
// 5. Library Version
// =========================================================================

std::string GetLibMotionPhotoVersion() { return "1.0.0"; }

}  // namespace api
}  // namespace libmotionphoto
