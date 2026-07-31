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

#ifndef MOTION_PHOTO_VIDEO_METADATA_WRITER_H_  // NOLINT
#define MOTION_PHOTO_VIDEO_METADATA_WRITER_H_  // NOLINT

#include "image_io/xmp/xmp_writer_source.h"
#include "motion_photo/video_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

namespace video_metadata {

// The default prefix, URI and element/attribute names.
const char kPrefix[] = "Metadata";
const char kUri[] = "http://ns.google.com/1.0/motion_photo_video_metadata/";
const char kFlags[] = "Flags";
const char kLowRes[] = "LowRes";
const char kHighRes[] = "HighRes";
const char kStabilized[] = "Stabilized";
const char kScores[] = "Scores";
const char kModelVersion[] = "ModelVersion";
const char kPrimaryImage[] = "PrimaryImage";
const char kHighResTrack[] = "HighResTrack";
const char kFrame[] = "Frame";
const char kScore[] = "Score";
const char kTime[] = "Time";

}  // namespace video_metadata

// An XMP writer source for the Video metadata used in a motion photo.
// Normally the video metadata is encoded in a binary format and stored in the
// sample of a meta track in the MP4 portion of the motion photo file. This
// class provides a way to write an XMP representation of the same data.
// Use an instance of this class with an XmpWriter.
class VideoMetadataWriter : public image_io::XmpWriterSource {
 public:
  //   metadata: The container metadata to write.
  //   is_sample: Whether the metadata to be written is to be used as a
  // sample/example. Sample metadata includes entries with special values for
  // the primary image and high res track frame scores.
  VideoMetadataWriter(const MetadataDescriptor& metadata,
                      bool is_sample = false);
  void WriteNamespaces(image_io::XmlWriter* writer) override;
  void WriteAttributeNamesAndValues(image_io::XmlWriter* writer) override;

 private:
  const MetadataDescriptor& metadata_;
  bool is_sample_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_VIDEO_METADATA_WRITER_H_  // NOLINT
