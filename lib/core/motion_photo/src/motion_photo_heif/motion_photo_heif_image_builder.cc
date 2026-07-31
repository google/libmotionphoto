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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(MOTION_PHOTO_HEIC_SUPPORT)
#include <libheif/heif.h>
#include "motion_photo_heif/libheif_deleter.h"
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)

#include "image_io/utils/file_utils.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/mpvd_box.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

using image_io::Byte;
using libmotionphoto::image_io::Message;

#if defined(MOTION_PHOTO_HEIC_SUPPORT)
struct HeifSinkWriter {
  static struct heif_error WriteCallback(struct heif_context* ctx,
                                         const void* data, size_t size,
                                         void* userdata) {
    DataSink* sink = static_cast<DataSink*>(userdata);
    if (!sink->Write(static_cast<const uint8_t*>(data), size)) {
      struct heif_error err = {heif_error_Encoding_error,
                               heif_suberror_Cannot_write_output_data,
                               "Failed to write to data sink"};
      return err;
    }
    struct heif_error ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};
    return ok;
  }
};
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)

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
#if defined(MOTION_PHOTO_HEIC_SUPPORT)
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

  heif_error error;
  std::unique_ptr<heif_context, LibHeifDeleter> context(heif_context_alloc());
  error = heif_context_read_from_memory_without_copy(context.get(), raw_bytes,
                                                     raw_size, nullptr);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to create heif context");
    return false;
  }

  heif_image_handle* handle_ptr;
  error = heif_context_get_primary_image_handle(context.get(), &handle_ptr);
  std::unique_ptr<heif_image_handle, LibHeifDeleter> handle(handle_ptr);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to get heif primary image handle");
    return false;
  }

  heif_image* img_ptr;
  error = heif_decode_image(handle.get(), &img_ptr, heif_colorspace_RGB,
                            heif_chroma_interleaved_RGB, nullptr);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to decode the HEIF image");
    return false;
  }
  std::unique_ptr<heif_image, LibHeifDeleter> img(img_ptr);

  heif_compression_format compression_format = heif_compression_HEVC;
  const char* mime = heif_get_file_mime_type(raw_bytes, raw_size);
  if (mime != nullptr) {
    std::string_view mime_str(mime);
    if (mime_str.find("avif") != std::string_view::npos) {
      compression_format = heif_compression_AV1;
    }
  }

  heif_encoder* encoder_ptr;
  error = heif_context_get_encoder_for_format(context.get(), compression_format,
                                              &encoder_ptr);
  if (error.code != heif_error_Ok) {
    std::string error_msg = "Failed to get encoder for format: ";
    error_msg += (compression_format == heif_compression_AV1) ? "AV1" : "HEVC";
    message_handler_->ReportMessage(Message::kDecodingError, error_msg);
    return false;
  }
  std::unique_ptr<heif_encoder, LibHeifDeleter> encoder(encoder_ptr);
  heif_encoder_set_lossy_quality(encoder.get(), 100);

  std::unique_ptr<heif_context, LibHeifDeleter> new_context(heif_context_alloc());
  error = heif_context_encode_image(new_context.get(), img.get(), encoder.get(),
                                    nullptr, nullptr);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to re-encode the image");
    return false;
  }

  error =
      heif_context_add_XMP_metadata(new_context.get(), handle.get(),
                                    xmp_metadata.data(), xmp_metadata.length());
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to add XMP to HEIF image");
    return false;
  }

  struct heif_writer writer;
  writer.writer_api_version = 1;
  writer.write = HeifSinkWriter::WriteCallback;

  error = heif_context_write(new_context.get(), &writer, output_sink);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to write HEIF image to sink");
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
#else   // defined(MOTION_PHOTO_HEIC_SUPPORT)
  message_handler_->ReportMessage(Message::kDecodingError,
                                  "HEIF support is disabled in this build");
  return false;
#endif  // !defined(MOTION_PHOTO_HEIC_SUPPORT)
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
