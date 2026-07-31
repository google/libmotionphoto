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

#include "motion_photo/video_metadata_reader.h"

#include <sstream>

#include "image_io/base/validated_number.h"
#include "image_io/xmp/xmp_errors.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"
#include "image_io/xmp/xmp_writer.h"
#include "motion_photo/video_metadata_writer.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataMatchResult;
using image_io::GetInvalidNumericalValueErrorResult;
using image_io::GetNamePathErrorResult;
using image_io::GetValidatedNumber;
using image_io::JoinPrefixAndName;
using image_io::kXmpRdfRdfDescription;
using image_io::kXmpRdfRdfLi;
using image_io::kXmpRdfRdfSeq;
using image_io::Message;
using image_io::XmlTokenContext;
using image_io::XmpContent;
using image_io::XmpWriter;
using std::string;
using std::stringstream;
using std::vector;
using video_metadata::kFlags;
using video_metadata::kFrame;
using video_metadata::kHighRes;
using video_metadata::kHighResTrack;
using video_metadata::kLowRes;
using video_metadata::kModelVersion;
using video_metadata::kPrefix;
using video_metadata::kPrimaryImage;
using video_metadata::kScore;
using video_metadata::kScores;
using video_metadata::kStabilized;
using video_metadata::kTime;
using video_metadata::kUri;

namespace {

string Name(const string& prefix, const string& suffix) {
  return JoinPrefixAndName(prefix, suffix);
}

}  // namespace

string VideoMetadataReader::GetSampleXmpString() {
  stringstream ss;
  MetadataDescriptor video_metadata;
  VideoMetadataWriter video_metadata_writer(video_metadata, true);
  XmpWriter xmp_writer(ss);
  xmp_writer.AddWriterSource(&video_metadata_writer);
  xmp_writer.Write();
  return ss.str();
}

VideoMetadataReader::VideoMetadataReader() { AddUri(kUri); }

void VideoMetadataReader::SetUriPrefix(const string& uri,
                                       const string& prefix) {
  const XmpContent::Type kValue = XmpContent::kValue;
  SetPrefix(kPrefix, prefix);
  AddSupportedName(Name(prefix, kFlags), kValue);
  AddSupportedName(Name(prefix, kScores), kValue);
  AddSupportedName(Name(prefix, kStabilized), kValue);
  AddSupportedName(Name(prefix, kModelVersion), kValue);
  AddSupportedName(Name(prefix, kFrame), kValue);
  AddSupportedName(Name(prefix, kScore), kValue);
  AddSupportedName(Name(prefix, kTime), kValue);
}

DataMatchResult VideoMetadataReader::ProcessElementName(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  const string& name = element_name;
  if (IsPrefixedName(name, kPrefix, kFlags) ||
      IsPrefixedName(name, kPrefix, kScores)) {
    if (!Contains(element_name_stack, kXmpRdfRdfDescription)) {
      return GetNamePathErrorResult(name, {}, kXmpRdfRdfDescription, context);
    }
  } else if (IsPrefixedName(name, kPrefix, kFrame)) {
    current_frame_score_.Clear();
    current_frame_time_.Clear();
  }
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::ProcessElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  const string& name = element_name;
  const string& value = content.value;
  if (IsPrefixedName(name, kPrefix, kStabilized)) {
    return CheckStablizedContext(element_name_stack, name, value, context);
  } else if (IsPrefixedName(name, kPrefix, kModelVersion)) {
    return CheckModelVersionContext(element_name_stack, name, value, context);
  } else if (IsPrefixedName(name, kPrefix, kScore)) {
    return CheckScoreAndTimeContext(element_name_stack, name, value, context);
  } else if (IsPrefixedName(name, kPrefix, kTime)) {
    return CheckScoreAndTimeContext(element_name_stack, name, value, context);
  } else if (IsPrefixedName(name, kPrefix, kFrame)) {
    return CheckFrameContext(element_name_stack, name, value, context);
  }
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::CheckStablizedContext(
    const StringVector& element_name_stack, const string& element_name,
    const string& content, const XmlTokenContext& context) {
  string flags = GetPrefixedName(kPrefix, kFlags);
  string lowres = GetPrefixedName(kPrefix, kLowRes);
  string highres = GetPrefixedName(kPrefix, kHighRes);
  if (!EndsWith(element_name_stack, {flags, lowres}) &&
      !EndsWith(element_name_stack, {flags, highres})) {
    return GetNamePathErrorResult(element_name, {flags},
                                  lowres + " or " + highres, context);
  }
  auto flag = GetValidatedNumber<int>(content);
  if (!flag.is_valid || (flag.value != 0 && flag.value != 1)) {
    return GetInvalidNumericalValueErrorResult(element_name, context);
  }
  if (element_name_stack.back() == lowres) {
    auto flags_desc = video_metadata_.GetMetadataFlagsDescriptor();
    auto low_res = flags_desc.GetLowResTrackFlagsDescriptor();
    low_res.SetStabilized(flag.value == 1);
    flags_desc.SetLowResTrackFlagsDescriptor(std::move(low_res));
    video_metadata_.SetMetadataFlagsDescriptor(std::move(flags_desc));
  } else if (element_name_stack.back() == highres) {
    auto flags_desc = video_metadata_.GetMetadataFlagsDescriptor();
    auto high_res = flags_desc.GetHighResTrackFlagsDescriptor();
    high_res.SetStabilized(flag.value == 1);
    flags_desc.SetHighResTrackFlagsDescriptor(std::move(high_res));
    video_metadata_.SetMetadataFlagsDescriptor(std::move(flags_desc));
  }
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::CheckModelVersionContext(
    const StringVector& element_name_stack, const string& element_name,
    const string& content, const XmlTokenContext& context) {
  string scores = GetPrefixedName(kPrefix, kScores);
  if (!EndsWith(element_name_stack, {scores})) {
    return GetNamePathErrorResult(element_name, {}, scores, context);
  }
  auto version = GetValidatedNumber<uint32_t>(content);
  if (!version.is_valid) {
    return GetInvalidNumericalValueErrorResult(element_name, context);
  }
  auto score_desc = video_metadata_.GetScoreDescriptor();
  score_desc.SetModelVersion(version.value);
  video_metadata_.SetScoreDescriptor(std::move(score_desc));
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::CheckScoreAndTimeContext(
    const StringVector& element_name_stack, const string& element_name,
    const string& content, const XmlTokenContext& context) {
  string primary_image = GetPrefixedName(kPrefix, kPrimaryImage);
  string high_res_track = GetPrefixedName(kPrefix, kHighResTrack);
  string frame = GetPrefixedName(kPrefix, kFrame);
  if (Contains(element_name_stack, primary_image)) {
    vector<string> path({primary_image, frame});
    if (!EndsWith(element_name_stack, path)) {
      path.pop_back();
      return GetNamePathErrorResult(element_name, path, frame, context);
    }
    return SetScoreOrTime(element_name, content, context);
  } else if (Contains(element_name_stack, high_res_track)) {
    vector<string> path({high_res_track, kXmpRdfRdfSeq, kXmpRdfRdfLi, frame});
    if (!EndsWith(element_name_stack, path)) {
      path.pop_back();
      return GetNamePathErrorResult(element_name, path, frame, context);
    }
    return SetScoreOrTime(element_name, content, context);
  }
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::SetScoreOrTime(
    const std::string& element_name, const std::string& content,
    const image_io::XmlTokenContext& context) {
  if (IsPrefixedName(element_name, kPrefix, kScore)) {
    return SetXmpValue(content, element_name, context, &current_frame_score_);
  } else if (IsPrefixedName(element_name, kPrefix, kTime)) {
    return SetXmpValue(content, element_name, context, &current_frame_time_);
  }
  return context.GetResult();
}

DataMatchResult VideoMetadataReader::CheckFrameContext(
    const StringVector& element_name_stack, const string& element_name,
    const string& content, const XmlTokenContext& context) {
  const string kMissing(" is missing a valid value for ");
  stringstream ss;
  if (!current_frame_score_.IsValid()) {
    ss << element_name << kMissing << Name(kPrefix, kScore);
  } else if (!current_frame_time_.IsValid()) {
    ss << element_name << kMissing << Name(kPrefix, kTime);
  }
  if (!ss.str().empty()) {
    auto result = context.GetResult();
    result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
    return result;
  }
  FrameScoreDescriptor frame_score(current_frame_score_.GetValue(),
                                   current_frame_time_.GetValue());
  const string& container_name = element_name_stack.back();
  if (IsPrefixedName(container_name, kPrefix, kPrimaryImage)) {
    auto score_desc = video_metadata_.GetScoreDescriptor();
    score_desc.SetPrimaryImageFrameScoreDescriptor(std::move(frame_score));
    video_metadata_.SetScoreDescriptor(std::move(score_desc));
  } else if (container_name == kXmpRdfRdfLi) {
    auto score_desc = video_metadata_.GetScoreDescriptor();
    score_desc.AddHighResTrackFrameScoreDescriptor(frame_score);
    video_metadata_.SetScoreDescriptor(std::move(score_desc));
  }
  current_frame_score_.Clear();
  current_frame_time_.Clear();
  return context.GetResult();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
