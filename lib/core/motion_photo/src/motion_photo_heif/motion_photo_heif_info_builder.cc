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

#include "motion_photo_heif/motion_photo_heif_info_builder.h"

#include <algorithm>
#include <vector>

#include "image_io/base/message_handler.h"
#include "motion_photo/mpvd_box.h"

#if defined(MOTION_PHOTO_HEIC_SUPPORT)
#include "motion_photo_heif/libheif_deleter.h"
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataRange;
using image_io::DataSource;
using image_io::Message;
using image_io::MessageHandler;
using std::find_if;
using std::unique_ptr;
using std::vector;

namespace {

// As per the ISO 23008-12:2017 spec, xmp is stored with the metadata type
// "mime" and content type "application/rdf+xml".
constexpr char kMetadataTypeXmp[] = "mime";
constexpr char kContentTypeXmp[] = "application/rdf+xml";

}  // namespace

MotionPhotoHeifInfoBuilder::MotionPhotoHeifInfoBuilder(
    MessageHandler* message_handler)
    : message_handler_(message_handler) {}

bool MotionPhotoHeifInfoBuilder::Build(DataSource* data_source,
                                       size_t data_source_length) {
#if defined(MOTION_PHOTO_HEIC_SUPPORT)
  auto data_segment = data_source->GetDataSegment(0, data_source_length);
  heif_error error;

  // Build a heif context from the data source.
  unique_ptr<heif_context, LibHeifDeleter> context(heif_context_alloc());
  error = heif_context_read_from_memory_without_copy(
      context.get(), data_segment->GetBuffer(0), data_segment->GetLength(),
      nullptr);
  if (error.code != heif_error_Ok) {
    std::string error_msg = "Failed to create heif context: ";
    error_msg += error.message;
    message_handler_->ReportMessage(Message::kDecodingError, error_msg);
    return false;
  }

  // Get the primary image handle.
  heif_image_handle* handle_ptr;
  error = heif_context_get_primary_image_handle(context.get(), &handle_ptr);
  unique_ptr<heif_image_handle, LibHeifDeleter> handle(handle_ptr);
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to get heif primary image handle");
    return false;
  }

  // Get the metadata id for the xmp section by finding all blocks that have the
  // xmp type and then filtering to the block id with the correct content type.
  int block_count = heif_image_handle_get_number_of_metadata_blocks(
      handle.get(), kMetadataTypeXmp);
  vector<heif_item_id> metadata_ids(block_count);

  heif_image_handle_get_list_of_metadata_block_IDs(
      handle.get(), kMetadataTypeXmp, metadata_ids.data(), block_count);

  auto xmp_metadata_block_id =
      find_if(metadata_ids.begin(), metadata_ids.end(),
              [handle_ptr](const heif_item_id& id) {
                return !strcmp(
                    heif_image_handle_get_metadata_content_type(handle_ptr, id),
                    kContentTypeXmp);
              });

  if (xmp_metadata_block_id == metadata_ids.end()) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Xmp not found in heif metadata");
    return false;
  }

  // Extract the metadata from the found id.
  size_t xmp_size =
      heif_image_handle_get_metadata_size(handle.get(), *xmp_metadata_block_id);
  std::vector<char> xmp(xmp_size);
  error = heif_image_handle_get_metadata(handle.get(), *xmp_metadata_block_id,
                                         xmp.data());
  if (error.code != heif_error_Ok) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to extract xmp metadata");
    return false;
  }
  size_t xmp_start = data_segment->Find(0, xmp.data(), xmp_size);
  xmp_string_range_ = DataRange(xmp_start, xmp_start + xmp_size);

  // Find the MpvdBox in the source.
  MpvdBoxScanReceiver receiver(mpvd_box_);
  MpvdBox::Scan(data_source, 0, receiver);
  if (!mpvd_box_.IsValid()) {
    message_handler_->ReportMessage(Message::kDecodingError,
                                    "Failed to find/decode mpvd box");
    return false;
  }

  return xmp_string_range_.IsValid() && mpvd_box_.IsValid();
#else   // defined(MOTION_PHOTO_HEIC_SUPPORT)
  return false;
#endif  // !defined(MOTION_PHOTO_HEIC_SUPPORT)
}

}  // namespace motion_photo
}  // namespace libmotionphoto
