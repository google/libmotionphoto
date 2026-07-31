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

#include "motion_photo/camera_metadata_writer.h"

#include "image_io/xmp/xmp_helpers.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::JoinPrefixAndName;
using image_io::XmlWriter;
using std::string;

namespace {

string Name(const string& prefix, const string& suffix) {
  return JoinPrefixAndName(prefix, suffix);
}

template <class T>
bool MaybeWriteAttributeNameAndValue(const std::string& name,
                                     const MetadataValue<T>& value,
                                     XmlWriter* writer) {
  if (value.IsValid()) {
    writer->WriteAttributeNameAndValue(name, value.GetValue());
    return true;
  }
  return false;
}

}  // namespace

CameraMetadataWriter::CameraMetadataWriter(const CameraMetadata& metadata)
    : metadata_(metadata) {}

void CameraMetadataWriter::WriteNamespaces(XmlWriter* writer) {
  writer->WriteXmlns(kCameraPrefix, kCameraUri);
}

void CameraMetadataWriter::WriteAttributeNamesAndValues(XmlWriter* writer) {
  const string kFlag = Name(kCameraPrefix, kMotionPhoto);
  const string kVersion = Name(kCameraPrefix, kMotionPhotoVersion);
  const string kTimestamp = Name(kCameraPrefix, kMotionPhotoTimestamp);
  MaybeWriteAttributeNameAndValue(kFlag, metadata_.motion_photo, writer);
  MaybeWriteAttributeNameAndValue(kVersion, metadata_.motion_photo_version,
                                  writer);
  MaybeWriteAttributeNameAndValue(
      kTimestamp, metadata_.motion_photo_presentation_timestamp_us, writer);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
