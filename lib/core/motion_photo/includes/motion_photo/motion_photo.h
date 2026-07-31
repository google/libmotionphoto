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

#ifndef MOTION_PHOTO_MOTION_PHOTO_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MOTION_PHOTO_H_  // NOLINT

#include <string>
#include <vector>

#include "motion_photo/camera_metadata.h"
#include "motion_photo/mpvd_box.h"
#include "motion_photo/video_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// Typedefs for naming consistency within the MotionPhoto class.
using VideoMetadata = MetadataDescriptor;

// Constants for Container metadata.
const char kContainerPrefix[] = "Container";
const char kContainerUri[] = "http://ns.google.com/photos/1.0/container/";
const char kContainerDirectory[] = "Directory";
const char kContainerVersionName[] = "Version";
const char kContainerItemName[] = "Item";

// Constants for ContainerItem metadata.
const char kContainerItemPrefix[] = "Item";
const char kContainerItemUri[] =
    "http://ns.google.com/photos/1.0/container/item/";
const char kContainerItemDataUri[] = "URI";
const char kContainerItemLabel[] = "Label";
const char kContainerItemLength[] = "Length";
const char kContainerItemMime[] = "Mime";
const char kContainerItemPadding[] = "Padding";
const char kContainerItemSemantic[] = "Semantic";

// Constants for common values.
const char kContainerVersionCurrent[] = "1.0";
const char kContainerItemSemanticPrimary[] = "Primary";
const char kContainerItemSemanticGainMap[] = "GainMap";
const char kContainerItemSemanticMotionPhoto[] = "MotionPhoto";
const char kContainerItemMimeImageJpeg[] = "image/jpeg";
const char kContainerItemMimeImageHeic[] = "image/heic";
const char kContainerItemMimeVideoMp4[] = "video/mp4";

// The container version number, number of items in a motion photo container
// directory and the indices for the image and video data.
const char kContainerVersion[] = "1";
const size_t kContainerItemCount = 2;
const size_t kContainerImageItemIndex = 0;
const size_t kContainerVideoItemIndex = 1;

// The default mime types used in a motion photo.
const char kContainerJpegImageItemMime[] = "image/jpeg";
const char kContainerHeicImageItemMime[] = "image/heic";
const char kContainerAvifImageItemMime[] = "image/avif";
const char kContainerVideoItemMime[] = "video/mp4";

// The semantic values for the motion photo container items.
const char kContainerImageItemSemantic[] = "Primary";
const char kContainerVideoItemSemantic[] = "MotionPhoto";

// The metadata associated with an XMP Container Item.
struct ContainerItemMetadata {
  MetadataValue<std::string> semantic;
  MetadataValue<std::string> mime;
  MetadataValue<std::string> data_uri;
  MetadataValue<std::string> label;
  MetadataValue<int64_t> length;
  MetadataValue<int64_t> padding;

  bool operator==(const ContainerItemMetadata& rhs) const {
    return semantic == rhs.semantic && mime == rhs.mime &&
           data_uri == rhs.data_uri && label == rhs.label &&
           length == rhs.length && padding == rhs.padding;
  }
  bool operator!=(const ContainerItemMetadata& rhs) const {
    return !(*this == rhs);
  }
};

// The metadata associated with an XMP Container.
struct ContainerMetadata {
  MetadataValue<std::string> version;
  std::vector<ContainerItemMetadata> items;

  bool operator==(const ContainerMetadata& rhs) const {
    return version == rhs.version && items == rhs.items;
  }
  bool operator!=(const ContainerMetadata& rhs) const {
    return !(*this == rhs);
  }
};

// The possible file types for motion photos.
enum class FileType {
  kUnsupported = 0,
  kJpeg,
  kHeic,
  kAvif,
};

inline bool IsHeif(FileType file_type) {
  return file_type == FileType::kHeic || file_type == FileType::kAvif;
}

// This class holds the metadata associated with a motion photo. The metadata
// is read and written from two sources: the camera and container metadata is
// read from and written to the XMP segement in the JPEG file that houses the
// motion photo; the video metadata is read from and written to the metadata
// track in the MP4 file that is concatenated to the end of the motion photo
// JPEG file.
//
// High level setter functions are provided in this class. These functions set
// the values in the lower-level objects in a consistent fashion. Read access
// to the metadata is provided via the const Get*Metadata() functions.
// Value consistency can be achieved by calling the EnforceConsistentValues() function.
class MotionPhoto {
 public:
  // The default constructor assumes a Jpec-based motion photo.
  MotionPhoto();

  // This constructor allows the specification of the type of motion photo.
  //   file_type: The type of motion photo file - jpeg or heic.
  explicit MotionPhoto(FileType file_type);

  // Sets whether this object represents a motion photo.
  void SetIsMotionPhoto(bool is_motion_photo);

  // Updates the internal is_motion_photo state from the camera metadata.
  void UpdateIsMotionPhotoFromMetadata();

  // The equality operators.
  bool operator==(const MotionPhoto& rhs) const;
  bool operator!=(const MotionPhoto& rhs) const;

  // Sets the timestamp of the primary image in the camera and video metadata.
  //   timestamp: The timestamp of the primary image in the low res
  // video in usecs.
  void SetImageTimestamp(int64_t timestamp);

  // Sets the score of the primary image in the video metadata.
  //   score: The score of the primary image in the video metadata.
  void SetImageScore(float score);

  // Sets the mime type of the image in the container metadata. The default
  // value set by the constructor is kContainerImageItemMime.
  //   image_mime: The mime type of the image.
  void SetImageMime(const std::string& image_mime);

  // Sets the mime type of the video in the container metadata. The default
  // value set by the constructor is kContainerVideoItemMime.
  //   video_mime: The mime type of the video.
  void SetVideoMime(const std::string& video_mime);

  // Sets the padding byte count after the image in the container metadata.
  //   image_padding: The number of pad bytes after the end of the image.
  void SetImagePadding(int64_t image_padding);

  // Sets the length of the video in the container metadata.
  //   video_length: The size of the video file appended to image file.
  void SetVideoLength(int64_t video_length);

  // Sets the flag indicating whether the low res video track is stablizized.
  //   is_stabilized: Whether the low res video track is stablizized.
  void SetLowResVideoTrackStabilized(bool is_stabilized);

  // Sets the flag indicating whether the high res video track is stablizized.
  //   is_stabilized: Whether the high res video track is stablizized.
  void SetHighResVideoTrackStabilized(bool is_stabilized);

  // Sets the model version number used for scoring the primary image and the
  // frames in the high res video track.
  //   score_model_version: The version number of the model used in scoring
  // video tracks.
  void SetVideoScoreModelVersion(uint32_t score_model_version);

  // Adds a score and timestamp for a frame in the high res video track.
  //   score: The score of frame in the high res video track.
  //   timestamp: The timestamp of the frame in the high res video track in
  // usecs.
  void AddHighResVideoTrackFrameScoreAndTimestamp(float score,
                                                  int64_t timestamp);

  // Returns the motion photo's image's camera metadata for read access.
  const CameraMetadata& GetCameraMetadata() const { return camera_metadata_; }

  // Returns the motion photo's image's container metadata for read access.
  const ContainerMetadata& GetContainerMetadata() const {
    return container_metadata_;
  }

  // Returns the motion photo's video metadata for read access.
  const VideoMetadata& GetVideoMetadata() const { return video_metadata_; }

  // Returns the motion photo's mpvd box for read access. This reference is
  // always valid, even if the motion photo is not a HEIF format (HEIC/AVIF).
  const MpvdBox& GetMpvdBox() const { return mpvd_box_; }

  // Returns the file type of the motion photo.
  FileType GetFileType() const { return file_type_; }

  // Sets the camera metadata for the motion photo.
  //   metadata: The camera metadata to move-assign.
  void SetCameraMetadata(CameraMetadata metadata) {
    camera_metadata_ = std::move(metadata);
  }

  // Sets the container metadata for the motion photo.
  //   metadata: The container metadata to move-assign.
  void SetContainerMetadata(ContainerMetadata metadata) {
    container_metadata_ = std::move(metadata);
  }

  // Sets the video metadata for the motion photo.
  //   metadata: The video metadata to move-assign.
  void SetVideoMetadata(VideoMetadata metadata) {
    video_metadata_ = std::move(metadata);
  }

  // Sets the mpvd box for the motion photo.
  //   mpvd_box: The mpvd box to move-assign.
  void SetMpvdBox(MpvdBox mpvd_box) { mpvd_box_ = std::move(mpvd_box); }

  // Ensures that the metadata values are consistent across the underlying
  // objects after metadata setters are used to update values.
  // Returns whether values were changed to make them consistent.
  bool EnforceConsistentValues();

  //   file_size: The size of the motion photo file.
  // Returns the offset from the start of the motion photo JPEG file of the MP4
  // file that was concatenated to the JPEG file.
  int64_t GetMp4FileOffset(int64_t file_size) const;

  // Returns whether this object represents a motion photo.
  bool IsMotionPhoto() const { return is_motion_photo_; }

 private:
  bool EnforceCameraMetadataValues();
  bool EnforceContainerMetadataValues();
  CameraMetadata camera_metadata_;
  ContainerMetadata container_metadata_;
  VideoMetadata video_metadata_;
  FileType file_type_;
  MpvdBox mpvd_box_;
  bool is_motion_photo_ = true;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MOTION_PHOTO_H_  // NOLINT
