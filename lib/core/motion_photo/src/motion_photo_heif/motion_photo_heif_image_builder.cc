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

#include "motion_photo_heif/motion_photo_heif_image_builder.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/utils/file_utils.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo/mpvd_box.h"
#include "motion_photo_heif/heif_iso_meta_editor.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

using image_io::Byte;
using libmotionphoto::image_io::Message;

}  // namespace

MotionPhotoHeifImageBuilder::MotionPhotoHeifImageBuilder(
    const std::string& output_file_name,
    image_io::MessageHandler* message_handler)
    : message_handler_(message_handler), output_file_name_(output_file_name) {}

MotionPhotoHeifImageBuilder::MotionPhotoHeifImageBuilder(
    image_io::MessageHandler* message_handler)
    : message_handler_(message_handler) {}

bool MotionPhotoHeifImageBuilder::Build(const MediaSource& image_source,
                                        std::string_view xmp_metadata,
                                        const MpvdBox* mpvd_box,
                                        DataSink* output_sink) {
  if (output_sink == nullptr || image_source.GetSize() == 0) {
    return false;
  }

  std::vector<uint8_t> local_buffer;
  const uint8_t* raw_bytes = image_source.GetData();
  size_t raw_size = image_source.GetSize();
  if (raw_bytes == nullptr) {
    local_buffer.resize(raw_size);
    if (!image_source.ReadAt(0, raw_size, local_buffer.data())) {
      return false;
    }
    raw_bytes = local_buffer.data();
  }

  // Losslessly rewrite the HEIF 'meta' box: the coded image items are copied
  // bit-exactly and only the primary image XMP item is replaced, so no HEVC/AV1
  // decoder or encoder is needed on any platform.
  std::vector<uint8_t> edited;
  std::string error;
  if (!HeifIsoMetaEditor::SetPrimaryXmp(raw_bytes, raw_size, xmp_metadata,
                                        &edited, &error)) {
    if (message_handler_ != nullptr) {
      message_handler_->ReportMessage(
          Message::kDecodingError, "Failed to set HEIF primary XMP: " + error);
    }
    return false;
  }
  if (!output_sink->Write(edited.data(), edited.size())) {
    if (message_handler_ != nullptr) {
      message_handler_->ReportMessage(Message::kDecodingError,
                                      "Failed to write HEIF image to sink");
    }
    return false;
  }

  if (mpvd_box != nullptr) {
    auto padding = mpvd_box->Encode();
    if (!output_sink->Write(reinterpret_cast<const uint8_t*>(padding.data()),
                            padding.size() * sizeof(Byte))) {
      return false;
    }
  }

  return true;
}

bool MotionPhotoHeifImageBuilder::AddImageFileAndMetadata(
    const std::string& image_file_name, std::string_view xmp_metadata) {
  auto source = MediaSource::FromFile(image_file_name, message_handler_);
  if (!source) return false;
  auto sink = DataSink::ToFile(output_file_name_, message_handler_);
  if (!sink) return false;
  return Build(*source, xmp_metadata, nullptr, sink.get());
}

bool MotionPhotoHeifImageBuilder::AddPaddingFromMpvdBox(
    const MpvdBox& mpvd_box) {
  auto output_file = OpenOutputFile(output_file_name_, message_handler_, true);
  if (!output_file) {
    return false;
  }
  auto padding = mpvd_box.Encode();
  output_file->write(reinterpret_cast<const char*>(padding.data()),
                     padding.size() * sizeof(Byte));
  return true;
}

bool MotionPhotoHeifImageBuilder::StripToStillImageFromMemory(
    const uint8_t* data, size_t size, size_t trailing_image_size,
    std::string_view fallback_xmp, const std::string& output_path,
    image_io::MessageHandler* message_handler) {
  if (data == nullptr || size == 0 || output_path.empty()) {
    return false;
  }
  const bool has_trailing_boundary =
      trailing_image_size > 0 && trailing_image_size <= size;
  const size_t still_size = has_trailing_boundary ? trailing_image_size : size;

  auto data_segment = ::photos_editing_formats::image_io::DataSegment::Create(
      ::photos_editing_formats::image_io::DataRange(0, still_size),
      const_cast<uint8_t*>(data),
      ::photos_editing_formats::image_io::DataSegment::BufferDispositionPolicy::
          kDontDelete);
  ::photos_editing_formats::image_io::DataSegmentDataSource data_source(
      data_segment);
  std::vector<::photos_editing_formats::image_io::DataRange> xmp_ranges =
      FindHeifPrimaryXmpRanges(&data_source, still_size);

  std::string stripped_xmp;
  std::string original_xmp;
  if (!xmp_ranges.empty() &&
      ReadDataRangesToString(&data_source, xmp_ranges, &original_xmp)) {
    stripped_xmp = StripMotionPhotoMetadata(original_xmp);
  } else {
    stripped_xmp.assign(fallback_xmp);
  }
  if (stripped_xmp.find_first_not_of(" \t\r\n") == std::string::npos) {
    stripped_xmp = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>";
  }

  // Fast path: overwrite single-extent XMP in place when no bytes follow
  // `still_size` and the stripped packet does not grow.
  if (has_trailing_boundary && xmp_ranges.size() == 1 &&
      xmp_ranges.front().GetEnd() <= still_size &&
      stripped_xmp.size() <= xmp_ranges.front().GetLength()) {
    const size_t xmp_begin = xmp_ranges.front().GetBegin();
    const size_t xmp_len = xmp_ranges.front().GetLength();
    const size_t xmp_end = xmp_ranges.front().GetEnd();
    std::string padded = stripped_xmp;
    padded.append(xmp_len - padded.size(), ' ');
    std::ofstream out(output_path, std::ios::binary);
    if (out.is_open()) {
      out.write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(xmp_begin));
      out.write(padded.data(), static_cast<std::streamsize>(padded.size()));
      out.write(reinterpret_cast<const char*>(data + xmp_end),
                static_cast<std::streamsize>(still_size - xmp_end));
      if (out.good()) {
        return true;
      }
    }
  }

  std::vector<uint8_t> rebuilt;
  std::string error;
  if (!HeifIsoMetaEditor::SetPrimaryXmp(data, still_size, stripped_xmp,
                                        &rebuilt, &error)) {
    if (message_handler != nullptr) {
      message_handler->ReportMessage(
          Message::kDecodingError, "Failed to set HEIF primary XMP: " + error);
    }
    return false;
  }
  std::ofstream out(output_path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(rebuilt.data()),
            static_cast<std::streamsize>(rebuilt.size()));
  return out.good();
}

bool MotionPhotoHeifImageBuilder::StripToStillImageFromDataSource(
    image_io::DataSource* data_source, size_t source_size,
    bool mpvd_is_trailing, size_t trailing_image_size,
    std::string_view fallback_xmp, const std::string& output_path,
    image_io::MessageHandler* message_handler) {
  if (data_source == nullptr || source_size == 0 || output_path.empty()) {
    return false;
  }
  const size_t read_size = (mpvd_is_trailing && trailing_image_size > 0 &&
                            trailing_image_size <= source_size)
                               ? trailing_image_size
                               : source_size;
  std::vector<uint8_t> buffer;
  if (!ReadDataRangeToBytes(data_source, 0, read_size, &buffer)) {
    return false;
  }
  return StripToStillImageFromMemory(
      buffer.data(), buffer.size(), mpvd_is_trailing ? buffer.size() : 0,
      fallback_xmp, output_path, message_handler);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
