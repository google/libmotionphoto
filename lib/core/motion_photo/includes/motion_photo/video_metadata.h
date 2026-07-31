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

#ifndef MOTION_PHOTO_VIDEO_METADATA_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_VIDEO_METADATA_H_  // NOLINT

#include "image_io/iso/iso_base_descriptor.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// The FrameScoreDescriptor provides information about a frame in a video track
// of a motion photo.  The descriptor has the following SDL definition:
//
// class MotionPhotoFrameScoreDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoFrameScoreDescrTag {
//   // The frame's score in the range [0, 1].
//   float(32) score;
//
//   // The frame's presentation timestamp in microseconds.
//   int(64) presentationTimestampUs;
// }
class FrameScoreDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class.
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC3;

  //   score: The score of the associated with the frame.
  //   presentation_timestamp_us: The microsecond timestamp of the frame.
  FrameScoreDescriptor(float score, int64_t presentation_timestamp_us)
      : image_io::IsoBaseDescriptor(kTag),
        score_(score),
        presentation_timestamp_us_(presentation_timestamp_us) {}

  FrameScoreDescriptor() : FrameScoreDescriptor(0., -1) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;

  // The equality operator allows for equality within a tolerance of 1.0E-6
  // for the score values.
  bool operator==(const FrameScoreDescriptor& rhs) const;
  bool operator!=(const FrameScoreDescriptor& rhs) const {
    return !(*this == rhs);
  }

  // Returns the frame's score value.
  float GetScore() const { return score_; }

  //   score: The new value of the frame's score.
  void SetScore(float score) { score_ = score; }

  // Returns the frame's presentation timestamp value, in microseconds.
  int64_t GetPresentationTimestampUs() const {
    return presentation_timestamp_us_;
  }

  //   presentation_timestamp_us: The new value of the frame's timestamp.
  void SetPresentationTimestampUs(int64_t presentation_timestamp_us) {
    presentation_timestamp_us_ = presentation_timestamp_us;
  }

 private:
  float score_;
  int64_t presentation_timestamp_us_;
};

// The TrackScoreDescriptor provides the score and timestamps for zero or more
// frames in one of the video tracks of a motion photo. The descriptor has the
// following SDL definition:
//
// class MotionPhotoTrackScoreDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoTrackScoreDescrTag {
//   // The number of scored frames in the track.
//   unsigned int(32) numScoredFrames;
//
//   // The track's frames' score data. They must be in ascending order with
//   // respect to presentation timestamp.
//   MotionPhotoFrameScoreDescriptor
//       trackFrameScoreDescr[numScoredHighResFrames];
// }
class TrackScoreDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class.
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC2;

  TrackScoreDescriptor() : image_io::IsoBaseDescriptor(kTag) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;
  bool operator==(const TrackScoreDescriptor& rhs) const;
  bool operator!=(const TrackScoreDescriptor& rhs) const {
    return !(*this == rhs);
  }

  // Adds a new frame score descriptor to this track score descriptor. This
  // function will also cause the vector of frame scores to be re-sorted by
  // ascending timestamp values.
  //   frame_score_descriptor: The frame score to add.
  void AddFrameScoreDescriptor(
      const FrameScoreDescriptor& frame_score_descriptor);

  // Removes the frame score descriptor with the given score and timestamp.
  //   frame_score_descriptor: The frame score to remove.
  // Returns whether such a frame score was found and removed.
  bool RemoveFrameScoreDescriptor(
      const FrameScoreDescriptor& frame_score_descriptor);

  // Removes all frame score descriptors from this track score.
  void RemoveAllFrameScoreDescriptors();

  // Returns the number of scored frames in the track.
  uint32_t GetNumScoredFrames() const {
    return static_cast<uint32_t>(frame_score_descriptors_.size());
  }

  // Returns the index-th frame score descriptor.
  //   index: The index of the frame score descriptor to get.
  // Returns the const reference to the frame score descriptor at the given index.
  const FrameScoreDescriptor& GetFrameScoreDescriptor(size_t index) const {
    return frame_score_descriptors_[index];
  }

  // Returns all frame score descriptors in the track.
  const std::vector<FrameScoreDescriptor>& GetFrameScoreDescriptors() const {
    return frame_score_descriptors_;
  }

 private:
  std::vector<FrameScoreDescriptor> frame_score_descriptors_;
};

// ScoreDecriptor defines the score data for a motion photo.  The descriptor
// has the following SDL definition:
//
// class MotionPhotoScoreDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoScoreDescrTag {
//
//   // Machine-intelligence model version used to calculate the scores.
//   unsigned int(32) modelVersion;
//
//   // The primary image's frame score data.
//   MotionPhotoFrameScoreDescriptor primaryImageFrameScoreDescr;
//
//   // The high resolution motion photo frames' score data.
//   MotionPhotoTrackScoreDescriptor highResTrackScoreDescr;
// }
class ScoreDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class.
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC1;

  ScoreDescriptor() : image_io::IsoBaseDescriptor(kTag), model_version_(0) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;
  bool operator==(const ScoreDescriptor& rhs) const;
  bool operator!=(const ScoreDescriptor& rhs) const { return !(*this == rhs); }

  // Returns the score descriptor's model version.
  uint32_t GetModelVersion() const { return model_version_; }

  //   model_version: The score descriptor's new model version.
  void SetModelVersion(uint32_t model_version) {
    model_version_ = model_version;
  }

  // Returns the frame score descriptor for the primary image.
  const FrameScoreDescriptor& GetPrimaryImageFrameScoreDescriptor() const {
    return primary_image_frame_score_descriptor_;
  }

  // Sets the frame score descriptor for the primary image.
  void SetPrimaryImageFrameScoreDescriptor(FrameScoreDescriptor descriptor) {
    primary_image_frame_score_descriptor_ = std::move(descriptor);
  }

  // Returns the track score descriptor for the high res track.
  const TrackScoreDescriptor& GetHighResTrackScoreDescriptor() const {
    return high_res_track_score_descriptor_;
  }

  // Adds a frame score descriptor to the high res track.
  void AddHighResTrackFrameScoreDescriptor(
      const FrameScoreDescriptor& frame_score_descriptor) {
    high_res_track_score_descriptor_.AddFrameScoreDescriptor(
        frame_score_descriptor);
  }

 private:
  uint32_t model_version_;
  FrameScoreDescriptor primary_image_frame_score_descriptor_;
  TrackScoreDescriptor high_res_track_score_descriptor_;
};

// The flags associated with a track. The descriptor has the following SDL
// definition:
//
// class MotionPhotoTrackFlagsDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoTrackFlagDescrTag {
//   // Set to true to indicate the video frames have been stabilized and do
//   // not require readers of the track to apply any further stabilized.
//   bit(1) isStabilized;
// }
class TrackFlagsDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC5;

  // The stabilization bit mask.
  static constexpr uint8_t kStabilizedMask = 0x80;

  TrackFlagsDescriptor() : image_io::IsoBaseDescriptor(kTag), flags_(0) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;
  bool operator==(const TrackFlagsDescriptor& rhs) const {
    return flags_ == rhs.flags_;
  }
  bool operator!=(const TrackFlagsDescriptor& rhs) const {
    return !(*this == rhs);
  }

  // Returns whether the stabilized flag is set.
  bool IsStabilized() const { return (flags_ & kStabilizedMask) != 0; }

  //   is_stabilized: The new value of the stabilized flag.
  void SetStabilized(bool is_stablilized) {
    if (is_stablilized) {
      flags_ = flags_ | kStabilizedMask;
    } else {
      flags_ = flags_ & (~kStabilizedMask);
    }
  }

 private:
  uint8_t flags_;
};

// MetadataFlagsDescriptor is a container for the various flags associated with
// the metadata. The descriptor has the following SDL definition:
//
// class MotionPhotoFlagsDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoFlagDescrTag {
//   // The low-resolution motion photo track’s flag data.
//   MotionPhotoTrackFlagDescriptor lowResTrackFlagsDescr;
//   // The high-resolution motion photo track’s flag data.
//   MotionPhotoTrackFlagDescriptor highResTrackFlagsDescr;
// }
class MetadataFlagsDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC4;

  MetadataFlagsDescriptor() : image_io::IsoBaseDescriptor(kTag) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;
  bool operator==(const MetadataFlagsDescriptor& rhs) const {
    return low_res_track_flags_descriptor_ ==
               rhs.low_res_track_flags_descriptor_ &&
           high_res_track_flags_descriptor_ ==
               rhs.high_res_track_flags_descriptor_;
  }
  bool operator!=(const MetadataFlagsDescriptor& rhs) const {
    return !(*this == rhs);
  }

  // Returns the low res track descriptor.
  const TrackFlagsDescriptor& GetLowResTrackFlagsDescriptor() const {
    return low_res_track_flags_descriptor_;
  }

  // Sets the low res track descriptor.
  void SetLowResTrackFlagsDescriptor(TrackFlagsDescriptor descriptor) {
    low_res_track_flags_descriptor_ = std::move(descriptor);
  }

  // Returns the high res track descriptor.
  const TrackFlagsDescriptor& GetHighResTrackFlagsDescriptor() const {
    return high_res_track_flags_descriptor_;
  }

  // Sets the high res track descriptor.
  void SetHighResTrackFlagsDescriptor(TrackFlagsDescriptor descriptor) {
    high_res_track_flags_descriptor_ = std::move(descriptor);
  }

 private:
  TrackFlagsDescriptor low_res_track_flags_descriptor_;
  TrackFlagsDescriptor high_res_track_flags_descriptor_;
};

// MetadataDescriptor represents the metadata for the video part of a motion
// photo. The descriptor has the following SDL definition:
//
// class MotionPhotoMetadataDescriptor extends BaseDescriptor
//         : bit(8) tag=MotionPhotoMetadataDescrTag {
//   // Scoring data for the still and high-res frames.
//   MotionPhotoScoreDescriptor motionPhotoScoreDescr;
// }
class MetadataDescriptor : public image_io::IsoBaseDescriptor {
 public:
  // The name of this class.
  static const char* kName;

  // The tag value of this class. This value is written when the an instance is
  // encoded, and it is expected when an instance is decoded.
  static constexpr uint8_t kTag = 0xC0;

  MetadataDescriptor() : image_io::IsoBaseDescriptor(kTag) {}
  const char* GetName() const override { return kName; }
  void Encode(image_io::IsoEncoder* encoder) const override;
  void DecodeDetails(image_io::IsoDecoder* decoder) override;
  bool operator==(const MetadataDescriptor& rhs) const;
  bool operator!=(const MetadataDescriptor& rhs) const {
    return !(*this == rhs);
  }

  // Returns the metadata'a score descriptor.
  const ScoreDescriptor& GetScoreDescriptor() const {
    return score_descriptor_;
  }

  // Sets the metadata's score descriptor.
  void SetScoreDescriptor(ScoreDescriptor score_descriptor) {
    score_descriptor_ = std::move(score_descriptor);
  }

  // Returns the metadata'a flags descriptor.
  const MetadataFlagsDescriptor& GetMetadataFlagsDescriptor() const {
    return metadata_flags_descriptor_;
  }

  // Sets the metadata's flags descriptor.
  void SetMetadataFlagsDescriptor(
      MetadataFlagsDescriptor metadata_flags_descriptor) {
    metadata_flags_descriptor_ = std::move(metadata_flags_descriptor);
  }

 private:
  ScoreDescriptor score_descriptor_;
  MetadataFlagsDescriptor metadata_flags_descriptor_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_VIDEO_METADATA_H_  // NOLINT
