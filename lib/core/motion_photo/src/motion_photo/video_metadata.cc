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

#include "motion_photo/video_metadata.h"

#include <algorithm>
#include <cmath>

namespace libmotionphoto {
namespace motion_photo {

using image_io::IsoDecoder;
using image_io::IsoEncoder;
using image_io::UIntX;

const char* FrameScoreDescriptor::kName = "FrameScoreDescriptor";

bool FrameScoreDescriptor::operator==(const FrameScoreDescriptor& rhs) const {
  float sdelta = score_ - rhs.score_;
  return std::fabs(sdelta) < 1.0e-6 &&
         presentation_timestamp_us_ == rhs.presentation_timestamp_us_;
}

void FrameScoreDescriptor::Encode(IsoEncoder* encoder) const {
  encoder->EncodeUInt8Value(GetTag());
  encoder->EncodeUIntXValue(sizeof(float) + sizeof(int64_t));
  encoder->EncodeFloatValue(score_);
  encoder->EncodeInt64Value(presentation_timestamp_us_);
}

void FrameScoreDescriptor::DecodeDetails(IsoDecoder* decoder) {
  score_ = decoder->DecodeFloatValue();
  presentation_timestamp_us_ = decoder->DecodeInt64Value();
}

const char* TrackScoreDescriptor::kName = "TrackScoreDescriptor";

bool TrackScoreDescriptor::operator==(const TrackScoreDescriptor& rhs) const {
  if (GetNumScoredFrames() != rhs.GetNumScoredFrames()) {
    return false;
  }
  bool eq = true;
  for (uint32_t index = 0; eq && index < GetNumScoredFrames(); ++index) {
    eq = frame_score_descriptors_[index] == rhs.frame_score_descriptors_[index];
  }
  return eq;
}

void TrackScoreDescriptor::Encode(IsoEncoder* encoder) const {
  const size_t kSizeCount = 4;
  encoder->EncodeUInt8Value(GetTag());
  size_t instance_size_index = encoder->Reserve(kSizeCount);
  size_t encoder_start_index = encoder->GetBytes().size();
  encoder->EncodeUInt32Value(GetNumScoredFrames());
  for (auto& frame_score_descriptor : frame_score_descriptors_) {
    frame_score_descriptor.Encode(encoder);
  }
  UIntX instance_size = encoder->GetBytes().size() - encoder_start_index;
  encoder->EncodeUIntXValue(instance_size, instance_size_index, kSizeCount);
}

void TrackScoreDescriptor::DecodeDetails(IsoDecoder* decoder) {
  const uint8_t kFrameTag = FrameScoreDescriptor::kTag;
  const char* kFrameName = FrameScoreDescriptor::kName;
  uint32_t frame_score_count = decoder->DecodeUInt32Value();
  frame_score_descriptors_.clear();
  frame_score_descriptors_.reserve(frame_score_count);
  for (uint32_t index = 0; index < frame_score_count && !decoder->HasErrors();
       ++index) {
    if (decoder->DecodeTagValue(kFrameTag, kFrameName)) {
      frame_score_descriptors_.emplace_back();
      frame_score_descriptors_.back().DecodeInstanceSizeAndDetails(decoder);
    } else {
      decoder->IncrementNext(decoder->DecodeUIntXValue());
    }
  }
}

void TrackScoreDescriptor::AddFrameScoreDescriptor(
    const FrameScoreDescriptor& frame_score_descriptor) {
  frame_score_descriptors_.emplace_back(frame_score_descriptor);
  std::stable_sort(
      frame_score_descriptors_.begin(), frame_score_descriptors_.end(),
      [](const FrameScoreDescriptor& lhs, const FrameScoreDescriptor& rhs) {
        return lhs.GetPresentationTimestampUs() <
               rhs.GetPresentationTimestampUs();
      });
}

bool TrackScoreDescriptor::RemoveFrameScoreDescriptor(
    const FrameScoreDescriptor& frame_score_descriptor) {
  auto pos = std::find(frame_score_descriptors_.begin(),
                       frame_score_descriptors_.end(), frame_score_descriptor);
  if (pos != frame_score_descriptors_.end()) {
    frame_score_descriptors_.erase(pos);
    return true;
  }
  return false;
}

void TrackScoreDescriptor::RemoveAllFrameScoreDescriptors() {
  frame_score_descriptors_.clear();
}

const char* ScoreDescriptor::kName = "ScoreDescriptor";

bool ScoreDescriptor::operator==(const ScoreDescriptor& rhs) const {
  return model_version_ == rhs.model_version_ &&
         primary_image_frame_score_descriptor_ ==
             rhs.primary_image_frame_score_descriptor_ &&
         high_res_track_score_descriptor_ ==
             rhs.high_res_track_score_descriptor_;
}

void ScoreDescriptor::Encode(IsoEncoder* encoder) const {
  const size_t kSizeCount = 4;
  encoder->EncodeUInt8Value(GetTag());
  size_t instance_size_index = encoder->Reserve(kSizeCount);
  size_t encoder_start_index = encoder->GetBytes().size();
  encoder->EncodeUInt32Value(model_version_);
  primary_image_frame_score_descriptor_.Encode(encoder);
  high_res_track_score_descriptor_.Encode(encoder);
  UIntX instance_size = encoder->GetBytes().size() - encoder_start_index;
  encoder->EncodeUIntXValue(instance_size, instance_size_index, kSizeCount);
}

void ScoreDescriptor::DecodeDetails(IsoDecoder* decoder) {
  const uint8_t kFrameTag = FrameScoreDescriptor::kTag;
  const char* kFrameName = FrameScoreDescriptor::kName;
  const uint8_t kTrackTag = TrackScoreDescriptor::kTag;
  const char* kTrackName = TrackScoreDescriptor::kName;
  model_version_ = decoder->DecodeUInt32Value();
  if (decoder->DecodeTagValue(kFrameTag, kFrameName)) {
    primary_image_frame_score_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
  if (decoder->DecodeTagValue(kTrackTag, kTrackName)) {
    high_res_track_score_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
}

const char* TrackFlagsDescriptor::kName = "TrackFlagsDescriptor";

void TrackFlagsDescriptor::Encode(image_io::IsoEncoder* encoder) const {
  encoder->EncodeUInt8Value(GetTag());
  encoder->EncodeUIntXValue(sizeof(uint8_t));
  encoder->EncodeUInt8Value(flags_);
}

void TrackFlagsDescriptor::DecodeDetails(image_io::IsoDecoder* decoder) {
  flags_ = decoder->DecodeUInt8Value();
}

const char* MetadataFlagsDescriptor::kName = "MetadataFlagsDescriptor";

void MetadataFlagsDescriptor::Encode(image_io::IsoEncoder* encoder) const {
  const size_t kSizeCount = 1;
  encoder->EncodeUInt8Value(GetTag());
  size_t instance_size_index = encoder->Reserve(kSizeCount);
  size_t encoder_start_index = encoder->GetBytes().size();
  low_res_track_flags_descriptor_.Encode(encoder);
  high_res_track_flags_descriptor_.Encode(encoder);
  UIntX instance_size = encoder->GetBytes().size() - encoder_start_index;
  encoder->EncodeUIntXValue(instance_size, instance_size_index, kSizeCount);
}

void MetadataFlagsDescriptor::DecodeDetails(image_io::IsoDecoder* decoder) {
  const uint8_t kFlagsTag = TrackFlagsDescriptor::kTag;
  const char* kFlagsName = TrackFlagsDescriptor::kName;
  if (decoder->DecodeTagValue(kFlagsTag, kFlagsName)) {
    low_res_track_flags_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
  if (decoder->DecodeTagValue(kFlagsTag, kFlagsName)) {
    high_res_track_flags_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
}

const char* MetadataDescriptor::kName = "MetadataDescriptor";

bool MetadataDescriptor::operator==(const MetadataDescriptor& rhs) const {
  return score_descriptor_ == rhs.score_descriptor_ &&
         metadata_flags_descriptor_ == rhs.metadata_flags_descriptor_;
}

void MetadataDescriptor::Encode(IsoEncoder* encoder) const {
  const size_t kSizeCount = 4;
  encoder->EncodeUInt8Value(GetTag());
  size_t instance_size_index = encoder->Reserve(kSizeCount);
  size_t encoder_start_index = encoder->GetBytes().size();
  score_descriptor_.Encode(encoder);
  metadata_flags_descriptor_.Encode(encoder);
  UIntX instance_size = encoder->GetBytes().size() - encoder_start_index;
  encoder->EncodeUIntXValue(instance_size, instance_size_index, kSizeCount);
}

void MetadataDescriptor::DecodeDetails(IsoDecoder* decoder) {
  const uint8_t kFlagsTag = MetadataFlagsDescriptor::kTag;
  const char* kFlagsName = MetadataFlagsDescriptor::kName;
  const uint8_t kScoreTag = ScoreDescriptor::kTag;
  const char* kScoreName = ScoreDescriptor::kName;
  if (decoder->DecodeTagValue(kScoreTag, kScoreName)) {
    score_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
  if (decoder->DecodeTagValue(kFlagsTag, kFlagsName)) {
    metadata_flags_descriptor_.DecodeInstanceSizeAndDetails(decoder);
  }
}

}  // namespace motion_photo
}  // namespace libmotionphoto
