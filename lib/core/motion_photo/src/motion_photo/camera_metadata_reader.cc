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

#include "motion_photo/camera_metadata_reader.h"

#include "image_io/base/message.h"
#include "image_io/base/validated_number.h"
#include "image_io/xml/xml_token_context.h"
#include "image_io/xmp/xmp_errors.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_value.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataMatchResult;
using image_io::JoinPrefixAndName;
using image_io::XmlTokenContext;
using image_io::XmpContent;
using std::string;

namespace {

template <typename T>
DataMatchResult SetMetadataValue(const string& str_value,
                                const string& element_name,
                                const XmlTokenContext& context,
                                MetadataValue<T>* target) {
  image_io::XmpValue<T> temp;
  auto result = image_io::SetXmpValue(str_value, element_name, context, &temp);
  if (temp.WasAssigned() && temp.IsValid()) {
    target->SetValue(temp.GetValue());
  }
  return result;
}

}  // namespace

CameraMetadataReader::CameraMetadataReader() { AddUri(kCameraUri); }

void CameraMetadataReader::SetUriPrefix(const string& uri,
                                        const string& prefix) {
  const XmpContent::Type kValue = XmpContent::kValue;
  if (uri == kCameraUri) {
    SetPrefix(kCameraPrefix, prefix);
    AddSupportedName(JoinPrefixAndName(prefix, kMotionPhoto), kValue);
    AddSupportedName(JoinPrefixAndName(prefix, kMotionPhotoVersion), kValue);
    AddSupportedName(JoinPrefixAndName(prefix, kMotionPhotoTimestamp), kValue);
  }
}

DataMatchResult CameraMetadataReader::ProcessElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  const string& name = element_name;
  const string& value = content.value;
  if (IsPrefixedName(name, kCameraPrefix, kMotionPhoto)) {
    return SetMetadataValue(value, name, context,
                            &camera_metadata_.motion_photo);
  } else if (IsPrefixedName(name, kCameraPrefix, kMotionPhotoVersion)) {
    return SetMetadataValue(value, name, context,
                            &camera_metadata_.motion_photo_version);
  } else if (IsPrefixedName(name, kCameraPrefix, kMotionPhotoTimestamp)) {
    return SetMetadataValue(
        value, name, context,
        &camera_metadata_.motion_photo_presentation_timestamp_us);
  }
  return context.GetResult();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
