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

#include "image_io/base/message_handler.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/mpvd_box.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataSource;
using image_io::Message;
using image_io::MessageHandler;

MotionPhotoHeifInfoBuilder::MotionPhotoHeifInfoBuilder(
    MessageHandler* message_handler)
    : message_handler_(message_handler) {}

bool MotionPhotoHeifInfoBuilder::Build(DataSource* data_source,
                                       size_t data_source_length) {
  if (data_source == nullptr || data_source_length == 0) {
    return false;
  }

  // Find the MpvdBox in the source first so GetMpvdBox() is populated even for
  // 3P HEIF containers that omit standard XMP.
  MpvdBoxScanReceiver receiver(mpvd_box_);
  MpvdBox::Scan(data_source, 0, receiver);

  xmp_string_range_ = FindHeifPrimaryXmpRange(data_source, data_source_length);
  if (!xmp_string_range_.IsValid()) {
    if (message_handler_ != nullptr) {
      message_handler_->ReportMessage(Message::kDecodingError,
                                      "Xmp not found in heif metadata");
    }
    return false;
  }

  if (!mpvd_box_.IsValid()) {
    if (message_handler_ != nullptr) {
      message_handler_->ReportMessage(Message::kDecodingError,
                                      "Failed to find/decode mpvd box");
    }
    return false;
  }

  return xmp_string_range_.IsValid() && mpvd_box_.IsValid();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
