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

#include "motion_photo/motion_photo_parser.h"

#include <string>
#include <vector>

#include "image_io/base/message.h"
#include "image_io/xmp/xmp_container_metadata_reader.h"
#include "image_io/xmp/xmp_reader.h"
#include "image_io/xmp/xmp_reader_data_destination.h"
#include "motion_photo/camera_metadata_reader.h"
#include "motion_photo/video_metadata_reader.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataRange;
using image_io::DataSource;
using image_io::Message;
using image_io::XmpContainerMetadataReader;
using image_io::XmpReader;
using image_io::XmpReaderDataDestination;

namespace {

template <typename T>
MetadataValue<T> ToMetadataValue(const image_io::XmpValue<T>& src) {
  if (src.WasAssigned()) {
    return MetadataValue<T>(src.GetValue(), true, src.IsValid());
  }
  return MetadataValue<T>();
}

ContainerMetadata ConvertFromImageIo(const image_io::XmpContainerMetadata& src) {
  ContainerMetadata dest;
  dest.version = ToMetadataValue(src.version);
  dest.items.reserve(src.items.size());
  for (const auto& item : src.items) {
    ContainerItemMetadata item_dest;
    item_dest.semantic = ToMetadataValue(item.semantic);
    item_dest.mime = ToMetadataValue(item.mime);
    item_dest.data_uri = ToMetadataValue(item.data_uri);
    item_dest.label = ToMetadataValue(item.label);
    item_dest.length = ToMetadataValue(item.length);
    item_dest.padding = ToMetadataValue(item.padding);
    dest.items.push_back(std::move(item_dest));
  }
  return dest;
}

}  // namespace

MotionPhotoParser::MotionPhotoParser(image_io::MessageHandler* handler)
    : handler_(handler) {}

bool MotionPhotoParser::ParseXmpString(std::string_view xmp,
                                       MotionPhoto* target,
                                       size_t* bytes_parsed) {
  if (!target) return false;

  XmpReader reader(handler_);
  CameraMetadataReader camera_reader;
  XmpContainerMetadataReader container_reader;
  reader.AddElementHandler(&camera_reader);
  reader.AddElementHandler(&container_reader);

  if (!reader.StartParse() || !reader.Parse(std::string(xmp)) || !reader.FinishParse()) {
    return false;
  }

  target->SetCameraMetadata(camera_reader.GetCameraMetadata());
  target->SetContainerMetadata(ConvertFromImageIo(container_reader.GetContainerMetadata()));
  if (bytes_parsed) {
    *bytes_parsed = reader.GetBytesParsed();
  }
  target->UpdateIsMotionPhotoFromMetadata();
  return !reader.HasErrors();
}

bool MotionPhotoParser::ParseXmpRanges(const std::vector<DataRange>& ranges,
                                       DataSource* source,
                                       MotionPhoto* target,
                                       size_t* bytes_parsed) {
  if (!target || !source) return false;

  XmpReader reader(handler_);
  reader.SetIgnoreMissingFinalXpacket();
  CameraMetadataReader camera_reader;
  XmpContainerMetadataReader container_reader;
  reader.AddElementHandler(&camera_reader);
  reader.AddElementHandler(&container_reader);

  XmpReaderDataDestination destination(&reader);
  destination.StartTransfer();
  for (const auto& range : ranges) {
    source->TransferData(range, range.GetLength(), &destination);
  }
  destination.FinishTransfer();

  if (reader.IsMissingFinalXpacket() && handler_ != nullptr) {
    handler_->ReportMessage(
        Message::kWarning,
        "XMP segment formatting missing final <?xpacket...?> marker.");
  }

  if (reader.HasErrors()) {
    return false;
  }

  target->SetCameraMetadata(camera_reader.GetCameraMetadata());
  target->SetContainerMetadata(ConvertFromImageIo(container_reader.GetContainerMetadata()));
  if (bytes_parsed) {
    *bytes_parsed = reader.GetBytesParsed();
  }
  target->UpdateIsMotionPhotoFromMetadata();
  return !reader.HasErrors();
}

bool MotionPhotoParser::ScanMpvdBox(DataSource* source, MotionPhoto* target) {
  if (!target || !source) return false;

  if (!IsHeif(target->GetFileType())) {
    if (handler_ != nullptr) {
      handler_->ReportMessage(Message::kDecodingError,
                              "Non-HEIF container formats do not include an MPVD box.");
    }
    return false;
  }

  MpvdBox box;
  MpvdBoxScanReceiver receiver(box);
  MpvdBox::Scan(source, 0, receiver);
  if (!box.IsValid()) {
    if (handler_ != nullptr) {
      handler_->ReportMessage(Message::kDecodingError,
                              "Failed to decode MPVD atom header.");
    }
    return false;
  }

  target->SetMpvdBox(box);
  return true;
}

bool MotionPhotoParser::DecodeVideoTrackMetadata(const uint8_t* bytes,
                                                  size_t length,
                                                  MotionPhoto* target) {
  if (!target || !bytes) return false;

  if (!video_decoder_.DecodeMetadata(bytes, 0, length)) {
    return false;
  }

  target->SetVideoMetadata(video_decoder_.GetMetadata());
  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
