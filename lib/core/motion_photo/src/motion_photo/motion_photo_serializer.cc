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

#include "motion_photo/motion_photo_serializer.h"

#include <sstream>

#include "image_io/xmp/xmp_container_metadata_writer.h"
#include "image_io/xmp/xmp_writer.h"
#include "motion_photo/camera_metadata_writer.h"
#include "motion_photo/video_metadata_writer.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::IsoEncoder;
using image_io::XmpContainerMetadataWriter;
using image_io::XmpWriter;

namespace {

template <typename T>
image_io::XmpValue<T> ToXmpValue(const MetadataValue<T>& src) {
  if (src.WasAssigned()) {
    return image_io::XmpValue<T>(src.GetValue(), true, src.IsValid());
  }
  return image_io::XmpValue<T>();
}

image_io::XmpContainerMetadata ConvertToImageIo(const ContainerMetadata& src) {
  image_io::XmpContainerMetadata dest;
  dest.version = ToXmpValue(src.version);
  dest.items.reserve(src.items.size());
  for (const auto& item : src.items) {
    image_io::XmpContainerItemMetadata item_dest;
    item_dest.semantic = ToXmpValue(item.semantic);
    item_dest.mime = ToXmpValue(item.mime);
    item_dest.data_uri = ToXmpValue(item.data_uri);
    item_dest.label = ToXmpValue(item.label);
    item_dest.length = ToXmpValue(item.length);
    item_dest.padding = ToXmpValue(item.padding);
    dest.items.push_back(std::move(item_dest));
  }
  return dest;
}

}  // namespace

MotionPhotoSerializer::MotionPhotoSerializer(const MotionPhoto& photo)
    : photo_(photo) {}

void MotionPhotoSerializer::SerializeImageMetadata(std::ostream& os) const {
  XmpWriter writer(os);
  CameraMetadataWriter camera_writer(photo_.GetCameraMetadata());
  auto image_io_container = ConvertToImageIo(photo_.GetContainerMetadata());
  XmpContainerMetadataWriter container_writer(image_io_container);
  writer.AddWriterSource(&camera_writer);
  writer.AddWriterSource(&container_writer);
  writer.Write();
}

void MotionPhotoSerializer::SerializeImageMetadata(std::string* xmp_out) const {
  if (!xmp_out) return;
  std::stringstream ss;
  SerializeImageMetadata(ss);
  *xmp_out = ss.str();
}

void MotionPhotoSerializer::EncodeVideoMetadata(
    std::vector<uint8_t>* bytes_out) const {
  if (!bytes_out) return;
  IsoEncoder encoder;
  photo_.GetVideoMetadata().Encode(&encoder);
  *bytes_out = std::move(encoder.GetBytes());
}

void MotionPhotoSerializer::SerializeVideoMetadata(std::string* xmp_out) const {
  if (!xmp_out) return;
  std::stringstream ss;
  XmpWriter writer(ss);
  VideoMetadataWriter video_writer(photo_.GetVideoMetadata());
  writer.AddWriterSource(&video_writer);
  writer.Write();
  *xmp_out = ss.str();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
