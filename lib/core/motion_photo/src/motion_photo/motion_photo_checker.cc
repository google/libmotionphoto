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

#include "motion_photo/motion_photo_checker.h"

#include <algorithm>
#include <ostream>
#include <regex>  // NOLINT
#include <sstream>
#include <string>
#include <vector>

#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_value.h"
#include "motion_photo/camera_metadata.h"
#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::kXmpContainerDirectory;
using image_io::kXmpContainerItemLength;
using image_io::kXmpContainerItemMime;
using image_io::kXmpContainerItemPadding;
using image_io::kXmpContainerItemPrefix;
using image_io::kXmpContainerItemSemantic;
using image_io::kXmpContainerPrefix;
using image_io::kXmpContainerVersion;
using image_io::Message;
using std::string;
using std::stringstream;
using std::vector;

namespace {

const char kNonNegativeValue[] = "a non-negative value";

string Name(const string& prefix, const string& name) {
  return image_io::JoinPrefixAndName(prefix, name);
}

string MissingValueText(const string& name) {
  stringstream ss;
  ss << "The metadata does not contain a value for " << name;
  return ss.str();
}

string MustNotBeAssignedText(const string& name, const string& type) {
  stringstream ss;
  ss << "Values for " << name << " must not be assigned when " << type
     << " values are given";
  return ss.str();
}

template <class T>
T QuoteValue(const T& value) {
  return value;
}

string QuoteValue(const string& value) { return "'" + value + "'"; }

string QuoteValues(const vector<string>& values) {
  stringstream ss;
  ss << "( ";
  for (const auto& value : values) {
    ss << QuoteValue(value) << " ";
  }
  ss << ")";
  return ss.str();
}

template <class T1, class T2>
string ExpectedValueText(const string& name, const T1& value,
                         const T2& expected) {
  stringstream ss;
  ss << "The expected value for " << name << " is " << QuoteValue(expected)
     << ", but the value read was " << QuoteValue(value);
  return ss.str();
}

template <class T1, class T2>
string ExpectedValueText(const string& name, const T1& value,
                         const vector<T2>& expected) {
  stringstream ss;
  ss << "The expected value for " << name << " is one of "
     << QuoteValues(expected) << ", but the value read was "
     << QuoteValue(value);
  return ss.str();
}

}  // namespace

bool MotionPhotoChecker::CheckFileName(const string& file_name) {
  const string kMotionPhotoJpegRegex =
      "IMG_[a-zA-Z0-9_]+_MP.(JPG|jpg|JPEG|jpeg)";
  const string kMotionPhotoHeifRegex =
      "IMG_[a-zA-Z0-9_]+_MP.(HEIC|heic|HEIF|heif|AVIF|avif)";
  string kRegex = motion_photo_.GetFileType() == FileType::kJpeg
                      ? kMotionPhotoJpegRegex
                      : kMotionPhotoHeifRegex;
  std::match_results<string::const_iterator> match_results;
  string base_name = file_name;
  size_t last_slash = base_name.rfind('/');
  if (last_slash != string::npos) {
    base_name = base_name.substr(last_slash + 1);
  }
  if (!std::regex_match(base_name, match_results, std::regex(kRegex))) {
    stringstream ss;
    ss << "The file name '" << base_name
       << "' does not follow the recommended pattern: '" << kRegex << "'";
    ReportWarning(ss.str());
    return false;
  }
  return true;
}

bool MotionPhotoChecker::CheckCameraMetadata() {
  return CheckMotionPhotoCameraMetadata();
}

bool MotionPhotoChecker::CheckMotionPhotoCameraMetadata() {
  const size_t kErrorCount = error_count_;
  const auto& metadata = motion_photo_.GetCameraMetadata();
  if (!metadata.motion_photo.WasAssigned()) {
    ReportError(MissingValueText(Name(kCameraPrefix, kMotionPhoto)));
  } else if (metadata.motion_photo.IsValid() &&
             metadata.motion_photo.GetValue() != kMotionPhotoValue) {
    ReportError(ExpectedValueText(Name(kCameraPrefix, kMotionPhoto),
                                  metadata.motion_photo.GetValue(),
                                  kMotionPhotoValue));
  }
  if (!metadata.motion_photo_version.WasAssigned()) {
    ReportError(MissingValueText(Name(kCameraPrefix, kMotionPhotoVersion)));
  } else if (metadata.motion_photo_version.IsValid() &&
             metadata.motion_photo_version.GetValue() !=
                 kMotionPhotoVersionValue) {
    ReportError(ExpectedValueText(Name(kCameraPrefix, kMotionPhotoVersion),
                                  metadata.motion_photo_version.GetValue(),
                                  kMotionPhotoVersionValue));
  }
  if (!metadata.motion_photo_presentation_timestamp_us.WasAssigned()) {
    ReportWarning(MissingValueText(Name(kCameraPrefix, kMotionPhotoTimestamp)));
  } else if (metadata.motion_photo_presentation_timestamp_us.GetValue() < 0) {
    ReportError(ExpectedValueText(
        Name(kCameraPrefix, kMotionPhotoTimestamp),
        metadata.motion_photo_presentation_timestamp_us.GetValue(),
        kNonNegativeValue));
  }

  return error_count_ == kErrorCount;
}

bool MotionPhotoChecker::CheckContainerMetadata() {
  const size_t kErrorCount = error_count_;
  const auto& metadata = motion_photo_.GetContainerMetadata();
  if (metadata.version.WasAssigned() &&
      metadata.version.GetValue() != kContainerVersion) {
    ReportError(
        ExpectedValueText(Name(kXmpContainerPrefix, kXmpContainerVersion),
                          metadata.version.GetValue(), kContainerVersion));
  }

  bool has_gainmap = false;
  for (const auto& item : metadata.items) {
    if (item.semantic.WasAssigned() &&
        item.semantic.GetValue() == kContainerItemSemanticGainMap) {
      has_gainmap = true;
      break;
    }
  }
  size_t expected_item_count = has_gainmap ? 3 : kContainerItemCount;

  if (metadata.items.size() != expected_item_count) {
    stringstream ss;
    ss << "The " << Name(kXmpContainerPrefix, kXmpContainerDirectory)
       << " element has " << metadata.items.size() << " elements: exactly "
       << expected_item_count << " must be defined";
    ReportError(ss.str());
  }
  size_t limit = std::min(metadata.items.size(), expected_item_count);
  for (size_t index = 0; index < limit; ++index) {
    CheckContainerItemMetadata(index, expected_item_count);
  }
  return error_count_ == kErrorCount;
}

void MotionPhotoChecker::CheckContainerItemMetadata(size_t index, size_t expected_item_count) {
  const vector<vector<string>> kMimes2(
      {{kContainerJpegImageItemMime, kContainerHeicImageItemMime,
        kContainerAvifImageItemMime},
       {kContainerVideoItemMime}});
  const vector<string> kSemantics2(
      {kContainerImageItemSemantic, kContainerVideoItemSemantic});

  const vector<vector<string>> kMimes3(
      {{kContainerJpegImageItemMime, kContainerHeicImageItemMime,
        kContainerAvifImageItemMime},
       {kContainerJpegImageItemMime, kContainerHeicImageItemMime,
        kContainerAvifImageItemMime},
       {kContainerVideoItemMime}});
  const vector<string> kSemantics3(
      {kContainerImageItemSemantic,
       kContainerItemSemanticGainMap,
       kContainerVideoItemSemantic});

  const auto& mimes = expected_item_count == 3 ? kMimes3 : kMimes2;
  const auto& semantics = expected_item_count == 3 ? kSemantics3 : kSemantics2;

  const ContainerItemMetadata kFakeItem;
  const auto& metadata = motion_photo_.GetContainerMetadata();
  stringstream pathss;
  pathss << Name(kXmpContainerPrefix, kXmpContainerDirectory) << "[" << index
         << "]/";
  const string kPath = pathss.str();
  const auto& item =
      index < metadata.items.size() ? metadata.items[index] : kFakeItem;
  string name = Name(kXmpContainerItemPrefix, kXmpContainerItemSemantic);
  if (!item.semantic.WasAssigned()) {
    ReportError(MissingValueText(kPath + name));
  } else if (!EqualsIgnoreCase(item.semantic, semantics[index])) {
    ReportError(
        ExpectedValueText(name, item.semantic.GetValue(), semantics[index]));
  }
  auto equals_ignore_case = [&item](const string& s) {
    return EqualsIgnoreCase(item.mime, s);
  };
  name = Name(kXmpContainerItemPrefix, kXmpContainerItemMime);
  if (!item.mime.WasAssigned()) {
    ReportError(MissingValueText(kPath + name));
  } else if (std::find_if(mimes[index].begin(), mimes[index].end(),
                          equals_ignore_case) == mimes[index].end()) {
    ReportError(ExpectedValueText(name, item.mime.GetValue(), mimes[index]));
  }
  if (index == 0) {
    if (item.length.IsValid() && item.length.GetValue() != 0) {
      name = Name(kXmpContainerItemPrefix, kXmpContainerItemLength);
      ReportError(ExpectedValueText(kPath + name, item.length.GetValue(), 0));
    }
    if (item.padding.IsValid() && item.padding.GetValue() < 0) {
      name = Name(kXmpContainerItemPrefix, kXmpContainerItemPadding);
      ReportError(ExpectedValueText(kPath + name, item.padding.GetValue(),
                                    kNonNegativeValue));
    }
  } else {
    if (item.length.IsValid() && item.length.GetValue() < 0) {
      name = Name(kXmpContainerItemPrefix, kXmpContainerItemLength);
      ReportError(ExpectedValueText(kPath + name, item.length.GetValue(),
                                    kNonNegativeValue));
    } else if (!item.length.WasAssigned()) {
      name = Name(kXmpContainerItemPrefix, kXmpContainerItemLength);
      ReportError(MissingValueText(kPath + name));
    }
    if (item.padding.IsValid() && item.padding.GetValue() < 0) {
      name = Name(kXmpContainerItemPrefix, kXmpContainerItemPadding);
      ReportError(ExpectedValueText(kPath + name, item.padding.GetValue(),
                                    kNonNegativeValue));
    }
  }
}

bool MotionPhotoChecker::CheckContainerMetadataAndSizes(size_t image_size,
                                                        size_t file_size) {
  const auto& metadata = motion_photo_.GetContainerMetadata();
  if (metadata.items.size() < 2) {
    return false;
  }
  size_t video_item_index = metadata.items.size() - 1;
  const auto& padding = metadata.items[0].padding;
  const auto& length = metadata.items[video_item_index].length;
  if (!length.WasAssigned() || !length.IsValid()) {
    return false;
  }
  int64_t padding_value = padding.IsValid() ? padding.GetValue() : 0;
  int64_t length_value = length.IsValid() ? length.GetValue() : 0;
  size_t padding_size = std::abs(padding_value);
  size_t length_size = std::abs(length_value);
  if (image_size + padding_size + length_size != file_size) {
    stringstream ss;
    ss << "Inconsistent values for the image and file sizes and "
       << "the container metadata padding and length values\n"
       << "  The file size must equal image size + padding + length\n"
       << "  file size = " << file_size << " image size = " << image_size
       << " padding value = " << padding_value
       << " length value = " << length_value;
    ReportError(ss.str());
    return false;
  }
  return true;
}

bool MotionPhotoChecker::CheckVideoTrackData(const VideoTrackData& track_data) {
  const size_t kErrorCount = error_count_;
  const int kExpectedVideoTrackCount = 2;
  const int kExpectedAudioTrackCount = 1;
  const int kExpectedMetadataTrackCount = 1;

  if (track_data.video_track_count > kExpectedVideoTrackCount) {
    stringstream ss;
    ss << "Unusual number of video tracks: " << track_data.video_track_count
       << "\n- Motion photos typically have only " << kExpectedVideoTrackCount
       << " video tracks";
    ReportWarning(ss.str());
  }
  if (track_data.audio_track_count > kExpectedAudioTrackCount) {
    stringstream ss;
    ss << "Unusual number of audio tracks: " << track_data.video_track_count
       << "\n- Motion photos typically have only " << kExpectedAudioTrackCount
       << " audio track";
    ReportWarning(ss.str());
  }
  if (track_data.metadata_track_count > kExpectedMetadataTrackCount) {
    stringstream ss;
    ss << "Unusual number of metadata tracks: "
       << track_data.metadata_track_count
       << "\n- Motion photos typically have only "
       << kExpectedMetadataTrackCount << " metadata track";
    ReportWarning(ss.str());
  }
  if (track_data.low_res_video_track_id == kInvalidTrackId) {
    ReportError("No low res video track found");
  }
  if (track_data.high_res_video_track_id != kInvalidTrackId &&
      track_data.high_res_metadata_track_id == kInvalidTrackId) {
    ReportError("No high res metadata track found");
  }
  return error_count_ == kErrorCount;
}

bool MotionPhotoChecker::CheckVideoMetadata() {
  const size_t kErrorCount = error_count_;
  const auto& camera_metadata = motion_photo_.GetCameraMetadata();
  const auto& video_metadata = motion_photo_.GetVideoMetadata();
  if (camera_metadata.motion_photo.WasAssigned()) {
    int64_t video_timestamp = video_metadata.GetScoreDescriptor()
                                .GetPrimaryImageFrameScoreDescriptor()
                                .GetPresentationTimestampUs();
    if (!camera_metadata.motion_photo_presentation_timestamp_us.WasAssigned()) {
      if (video_timestamp != -1) {
        stringstream ss;
        ss << "Illegal primary image presentation timestamp: "
           << video_timestamp << "\n- This value must be -1 since "
           << Name(kCameraPrefix, kMotionPhotoTimestamp)
           << " was not defined in the JPEG's XMP data";
        ReportError(ss.str());
      }
    } else if (camera_metadata.motion_photo_presentation_timestamp_us
                   .IsValid()) {
      int64_t image_timestamp =
          camera_metadata.motion_photo_presentation_timestamp_us.GetValue();
      if (image_timestamp != video_timestamp) {
        stringstream ss;
        ss << "Illegal primary image presentation timestamp: "
           << video_timestamp << "\n- This value must be " << image_timestamp
           << " to match the value defined in the JPEG's XMP data";
        ReportError(ss.str());
      }
    }
  }
  const auto& highres_track_scores =
      video_metadata.GetScoreDescriptor().GetHighResTrackScoreDescriptor();
  size_t score_count = highres_track_scores.GetNumScoredFrames();
  if (score_count > 1) {
    bool unsorted = false;
    stringstream errss;
    errss
        << "High res track frame scores are not sorted by ascending timestamp "
           "values";
    int64_t prev_timestamp = highres_track_scores.GetFrameScoreDescriptor(0)
                                 .GetPresentationTimestampUs();
    for (size_t index = 0; index < score_count; ++index) {
      const auto& frame = highres_track_scores.GetFrameScoreDescriptor(index);
      float score = frame.GetScore();
      int64_t timestamp = frame.GetPresentationTimestampUs();
      errss << "\n- " << index << ": score: " << score
            << " timestamp: " << timestamp;
      unsorted = unsorted || prev_timestamp > timestamp;
      prev_timestamp = timestamp;
    }
    if (unsorted) {
      ReportError(errss.str());
    }
  }
  return error_count_ == kErrorCount;
}

bool MotionPhotoChecker::CheckMpvdBox(size_t file_size) {
  if (!IsHeif(motion_photo_.GetFileType())) {
    return true;
  }

  // If these two checks are not passed, then no sense in doing the others.
  const MpvdBox& box = motion_photo_.GetMpvdBox();
  if (!box.IsValid()) {
    ReportError("MpvdBox was not found or is invalid");
    return false;
  }

  if (box.GetStartIndex() + box.GetLength() != file_size) {
    std::stringstream ss;
    ss << "File size (" << file_size << ") != MpvdBox.start ("
       << box.GetStartIndex() << ") + MpvdBox.length (" << box.GetLength()
       << ")";
    ReportError(ss.str());
    return false;
  }

  // The Mpvd box is ok, now compare with XMP video length data.
  const size_t kErrorCount = error_count_;
  auto mpvd_video_length = box.GetVideoLength();
  int64_t xmp_video_length = 0;
  const auto& items = motion_photo_.GetContainerMetadata().items;
  if (items.size() > kContainerVideoItemIndex) {
    const auto& xmp_video_length_attribute = items[kContainerVideoItemIndex].length;
    if (xmp_video_length_attribute.WasAssigned()) {
      xmp_video_length = xmp_video_length_attribute.GetValue();
    }
  }
  if (mpvd_video_length != xmp_video_length) {
    std::stringstream ss;
    ss << "MpvdBox.length (" << mpvd_video_length << ") != XMP video length ("
       << xmp_video_length << ")";
    ReportError(ss.str());
  }

  // Check the XMP padding value
  int64_t xmp_image_padding = 0;
  if (items.size() > kContainerImageItemIndex) {
    const auto& xmp_image_padding_attribute = items[kContainerImageItemIndex].padding;
    if (xmp_image_padding_attribute.WasAssigned()) {
      xmp_image_padding = xmp_image_padding_attribute.GetValue();
    }
  }
  if (xmp_image_padding != box.GetHeaderLength()) {
    std::stringstream ss;
    ss << "MpvdBox.headerLength (" << box.GetHeaderLength()
       << ") != XMP image padding (" << xmp_image_padding << ")";
    ReportError(ss.str());
  }
  return error_count_ == kErrorCount;
}

void MotionPhotoChecker::ReportError(const string& text) {
  if (message_handler_) {
    message_handler_->ReportMessage(Message::kValueError, text);
  }
  error_count_++;
}

void MotionPhotoChecker::ReportWarning(const string& text) {
  if (message_handler_) {
    message_handler_->ReportMessage(Message::kWarning, text);
  }
  warning_count_++;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
