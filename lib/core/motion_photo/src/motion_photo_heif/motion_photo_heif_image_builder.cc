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
#include <string>
#include <string_view>
#include <vector>

#include "image_io/utils/file_utils.h"
#include "motion_photo/motion_photo_source.h"
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

}  // namespace motion_photo
}  // namespace libmotionphoto
