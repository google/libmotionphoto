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

#include "motion_photo/motion_photo.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "image_io/base/types.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

template <typename Container>
auto FindItemBySemantic(Container& items, std::string_view target_semantic)
    -> decltype(&items[0]) {
  auto it = std::find_if(
      items.begin(), items.end(), [target_semantic](const auto& item) {
        return item.semantic.WasAssigned() &&
               item.semantic.GetValue() == target_semantic;
      });
  return (it != items.end()) ? &(*it) : nullptr;
}

}  // namespace

MotionPhoto::MotionPhoto() : MotionPhoto(FileType::kJpeg) {}

MotionPhoto::MotionPhoto(FileType file_type) : file_type_(file_type) {
  EnforceCameraMetadataValues();
  EnforceContainerMetadataValues();
}

bool MotionPhoto::operator==(const MotionPhoto& rhs) const {
  return camera_metadata_ == rhs.camera_metadata_ &&
         container_metadata_ == rhs.container_metadata_ &&
         video_metadata_ == rhs.video_metadata_ &&
         file_type_ == rhs.file_type_ && mpvd_box_ == rhs.mpvd_box_;
}

bool MotionPhoto::operator!=(const MotionPhoto& rhs) const {
  return !(*this == rhs);
}

void MotionPhoto::SetImageTimestamp(int64_t timestamp) {
  camera_metadata_.motion_photo_presentation_timestamp_us = timestamp;
  auto score_desc = video_metadata_.GetScoreDescriptor();
  auto primary_frame = score_desc.GetPrimaryImageFrameScoreDescriptor();
  primary_frame.SetPresentationTimestampUs(timestamp);
  score_desc.SetPrimaryImageFrameScoreDescriptor(std::move(primary_frame));
  video_metadata_.SetScoreDescriptor(std::move(score_desc));
}

void MotionPhoto::SetImageScore(float score) {
  auto score_desc = video_metadata_.GetScoreDescriptor();
  auto primary_frame = score_desc.GetPrimaryImageFrameScoreDescriptor();
  primary_frame.SetScore(score);
  score_desc.SetPrimaryImageFrameScoreDescriptor(std::move(primary_frame));
  video_metadata_.SetScoreDescriptor(std::move(score_desc));
}

void MotionPhoto::SetImageMime(const std::string& image_mime) {
  if (auto* item = FindItemBySemantic(container_metadata_.items,
                                      kContainerImageItemSemantic)) {
    item->mime = image_mime;
  }
}

void MotionPhoto::SetVideoMime(const std::string& video_mime) {
  if (auto* item = FindItemBySemantic(container_metadata_.items,
                                      kContainerVideoItemSemantic)) {
    item->mime = video_mime;
  }
}

void MotionPhoto::SetImagePadding(int64_t image_padding) {
  if (auto* item = FindItemBySemantic(container_metadata_.items,
                                      kContainerImageItemSemantic)) {
    item->padding = image_padding;
  }
}

void MotionPhoto::SetVideoLength(int64_t video_length) {
  if (auto* item = FindItemBySemantic(container_metadata_.items,
                                      kContainerVideoItemSemantic)) {
    item->length = video_length;
  }
}

void MotionPhoto::SetLowResVideoTrackStabilized(bool is_stabilized) {
  auto flags_desc = video_metadata_.GetMetadataFlagsDescriptor();
  auto low_res = flags_desc.GetLowResTrackFlagsDescriptor();
  low_res.SetStabilized(is_stabilized);
  flags_desc.SetLowResTrackFlagsDescriptor(std::move(low_res));
  video_metadata_.SetMetadataFlagsDescriptor(std::move(flags_desc));
}

void MotionPhoto::SetHighResVideoTrackStabilized(bool is_stabilized) {
  auto flags_desc = video_metadata_.GetMetadataFlagsDescriptor();
  auto high_res = flags_desc.GetHighResTrackFlagsDescriptor();
  high_res.SetStabilized(is_stabilized);
  flags_desc.SetHighResTrackFlagsDescriptor(std::move(high_res));
  video_metadata_.SetMetadataFlagsDescriptor(std::move(flags_desc));
}

void MotionPhoto::SetVideoScoreModelVersion(uint32_t score_model_version) {
  auto score_desc = video_metadata_.GetScoreDescriptor();
  score_desc.SetModelVersion(score_model_version);
  video_metadata_.SetScoreDescriptor(std::move(score_desc));
}

void MotionPhoto::AddHighResVideoTrackFrameScoreAndTimestamp(
    float score, int64_t timestamp) {
  auto score_desc = video_metadata_.GetScoreDescriptor();
  score_desc.AddHighResTrackFrameScoreDescriptor(
      FrameScoreDescriptor(score, timestamp));
  video_metadata_.SetScoreDescriptor(std::move(score_desc));
}

bool MotionPhoto::EnforceConsistentValues() {
  bool changed1 = EnforceCameraMetadataValues();
  bool changed2 = EnforceContainerMetadataValues();
  bool changed = changed1 || changed2;
  auto& camera_time = camera_metadata_.motion_photo_presentation_timestamp_us;
  auto video_time = video_metadata_.GetScoreDescriptor()
                        .GetPrimaryImageFrameScoreDescriptor()
                        .GetPresentationTimestampUs();
  if (!camera_time.IsValid() && video_time != -1) {
    camera_time = video_time;
    changed = true;
  }
  if (camera_time.IsValid()) {
    if (camera_time.GetValue() != video_time) {
      changed = true;
      auto score_desc = video_metadata_.GetScoreDescriptor();
      auto primary_frame = score_desc.GetPrimaryImageFrameScoreDescriptor();
      primary_frame.SetPresentationTimestampUs(camera_time.GetValue());
      score_desc.SetPrimaryImageFrameScoreDescriptor(std::move(primary_frame));
      video_metadata_.SetScoreDescriptor(std::move(score_desc));
    }
  } else if (video_time != -1) {
    changed = true;
    auto score_desc = video_metadata_.GetScoreDescriptor();
    auto primary_frame = score_desc.GetPrimaryImageFrameScoreDescriptor();
    primary_frame.SetPresentationTimestampUs(-1);
    score_desc.SetPrimaryImageFrameScoreDescriptor(std::move(primary_frame));
    video_metadata_.SetScoreDescriptor(std::move(score_desc));
  }
  return changed;
}

bool MotionPhoto::EnforceCameraMetadataValues() {
  bool changed = false;
  if (is_motion_photo_) {
    if (!camera_metadata_.motion_photo.IsValid() ||
        camera_metadata_.motion_photo.GetValue() != kMotionPhotoValue) {
      changed = true;
      camera_metadata_.motion_photo = kMotionPhotoValue;
    }
    if (!camera_metadata_.motion_photo_version.IsValid() ||
        camera_metadata_.motion_photo_version.GetValue() !=
            kMotionPhotoVersionValue) {
      changed = true;
      camera_metadata_.motion_photo_version = kMotionPhotoVersionValue;
    }
  } else {
    if (camera_metadata_.motion_photo.WasAssigned()) {
      changed = true;
      camera_metadata_.motion_photo.Clear();
    }
    if (camera_metadata_.motion_photo_version.WasAssigned()) {
      changed = true;
      camera_metadata_.motion_photo_version.Clear();
    }
    if (camera_metadata_.motion_photo_presentation_timestamp_us.WasAssigned()) {
      changed = true;
      camera_metadata_.motion_photo_presentation_timestamp_us.Clear();
    }
  }
  return changed;
}

bool MotionPhoto::EnforceContainerMetadataValues() {
  bool changed = false;
  if (is_motion_photo_ || !container_metadata_.items.empty()) {
    if (!container_metadata_.version.IsValid() ||
        container_metadata_.version.GetValue() != kContainerVersion) {
      changed = true;
      container_metadata_.version = kContainerVersion;
    }
  }

  bool has_gainmap = false;
  for (const auto& item : container_metadata_.items) {
    if (item.semantic.WasAssigned() &&
        item.semantic.GetValue() == kContainerItemSemanticGainMap) {
      has_gainmap = true;
      break;
    }
  }

  size_t expected_item_count = 0;
  if (is_motion_photo_) {
    expected_item_count = has_gainmap ? 3 : kContainerItemCount;
  } else {
    expected_item_count = has_gainmap ? 2 : 0;
  }

  if (container_metadata_.items.size() != expected_item_count) {
    changed = true;
    container_metadata_.items.resize(expected_item_count);
  }

  if (expected_item_count == 0) {
    return changed;
  }

  ContainerItemMetadata* image_item = nullptr;
  ContainerItemMetadata* video_item = nullptr;
  ContainerItemMetadata* gainmap_item = nullptr;

  for (auto& item : container_metadata_.items) {
    if (item.semantic.WasAssigned()) {
      const std::string& semantic = item.semantic.GetValue();
      if (semantic == kContainerImageItemSemantic) {
        image_item = &item;
      } else if (semantic == kContainerVideoItemSemantic) {
        video_item = &item;
      } else if (semantic == kContainerItemSemanticGainMap) {
        gainmap_item = &item;
      }
    }
  }

  if (expected_item_count == 2) {
    if (is_motion_photo_) {
      if (!image_item) image_item = &container_metadata_.items[0];
      if (!video_item) video_item = &container_metadata_.items[1];
    } else {
      if (!image_item) image_item = &container_metadata_.items[0];
      if (!gainmap_item) gainmap_item = &container_metadata_.items[1];
    }
  } else if (expected_item_count == 3) {
    if (!image_item) image_item = &container_metadata_.items[0];
    if (!gainmap_item) gainmap_item = &container_metadata_.items[1];
    if (!video_item) video_item = &container_metadata_.items[2];
  }

  if (image_item && image_item->semantic.GetValue() != kContainerImageItemSemantic) {
    changed = true;
    image_item->semantic = kContainerImageItemSemantic;
  }
  if (video_item && video_item->semantic.GetValue() != kContainerVideoItemSemantic) {
    changed = true;
    video_item->semantic = kContainerVideoItemSemantic;
  }
  if (gainmap_item && gainmap_item->semantic.GetValue() != kContainerItemSemanticGainMap) {
    changed = true;
    gainmap_item->semantic = kContainerItemSemanticGainMap;
  }

  if (image_item) {
    if (!image_item->mime.WasAssigned() || image_item->mime.GetValue().empty()) {
      changed = true;
      image_item->mime = file_type_ == FileType::kJpeg
                            ? kContainerJpegImageItemMime
                            : kContainerHeicImageItemMime;
    }
  }
  if (video_item) {
    if (!video_item->mime.WasAssigned() || video_item->mime.GetValue().empty()) {
      changed = true;
      video_item->mime = kContainerVideoItemMime;
    }
  }
  if (gainmap_item) {
    if (!gainmap_item->mime.WasAssigned() || gainmap_item->mime.GetValue().empty()) {
      changed = true;
      gainmap_item->mime = kContainerJpegImageItemMime;
    }
  }

  return changed;
}

int64_t MotionPhoto::GetMp4FileOffset(int64_t file_size) const {
  int64_t length = 0;
  if (IsMotionPhoto()) {
    if (const auto* item = FindItemBySemantic(container_metadata_.items,
                                              kContainerVideoItemSemantic)) {
      if (item->length.WasAssigned() && item->length.IsValid()) {
        length = item->length.GetValue();
      }
    }
  }
  return file_size - length;
}

void MotionPhoto::SetIsMotionPhoto(bool is_motion_photo) {
  is_motion_photo_ = is_motion_photo;
  EnforceConsistentValues();
}

void MotionPhoto::UpdateIsMotionPhotoFromMetadata() {
  is_motion_photo_ = camera_metadata_.motion_photo.WasAssigned() &&
                     camera_metadata_.motion_photo.GetValue() == kMotionPhotoValue;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
