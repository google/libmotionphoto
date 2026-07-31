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

#include "motion_photo/video_metadata_writer.h"

#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::JoinPrefixAndName;
using image_io::kXmpRdfRdfLi;
using image_io::kXmpRdfRdfSeq;
using image_io::XmlWriter;
using std::string;
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

void WriteStabilizedFlag(XmlWriter* writer, bool is_stablized) {
  writer->WriteAttributeNameAndValue(Name(kPrefix, kStabilized),
                                     is_stablized ? "1" : "0");
}

void WriteFlags(XmlWriter* writer, const MetadataFlagsDescriptor& flags) {
  auto flags_depth = writer->StartWritingElement(Name(kPrefix, kFlags));
  const auto& lowres_flags = flags.GetLowResTrackFlagsDescriptor();
  writer->StartWritingElement(Name(kPrefix, kLowRes));
  WriteStabilizedFlag(writer, lowres_flags.IsStabilized());
  writer->FinishWritingElement();
  const auto& highres_flags = flags.GetHighResTrackFlagsDescriptor();
  writer->StartWritingElement(Name(kPrefix, kHighRes));
  WriteStabilizedFlag(writer, highres_flags.IsStabilized());
  writer->FinishWritingElement();
  writer->FinishWritingElementsToDepth(flags_depth);
}

void WriteFrame(XmlWriter* writer, const FrameScoreDescriptor& frame,
                bool is_sample) {
  writer->StartWritingElement(Name(kPrefix, kFrame));
  if (is_sample) {
    writer->WriteAttributeNameAndValue(Name(kPrefix, kScore), "SCORE");
    writer->WriteAttributeNameAndValue(Name(kPrefix, kTime), "TIME");
  } else {
    writer->WriteAttributeNameAndValue(Name(kPrefix, kScore), frame.GetScore());
    writer->WriteAttributeNameAndValue(Name(kPrefix, kTime),
                                       frame.GetPresentationTimestampUs());
  }
  writer->FinishWritingElement();
}

void WriteScores(XmlWriter* writer, const ScoreDescriptor& scores,
                 bool is_sample) {
  auto scores_depth = writer->StartWritingElement(Name(kPrefix, kScores));
  writer->WriteAttributeNameAndValue(Name(kPrefix, kModelVersion),
                                     scores.GetModelVersion());
  writer->StartWritingElement(Name(kPrefix, kPrimaryImage));
  WriteFrame(writer, scores.GetPrimaryImageFrameScoreDescriptor(), is_sample);
  writer->FinishWritingElement();

  writer->StartWritingElements({Name(kPrefix, kHighResTrack), kXmpRdfRdfSeq});
  if (is_sample) {
    writer->StartWritingElement(kXmpRdfRdfLi);
    WriteFrame(writer, FrameScoreDescriptor(), is_sample);
    writer->FinishWritingElement();
    writer->WriteComment("Repeat as needed");
  } else {
    const auto highres_scores = scores.GetHighResTrackScoreDescriptor();
    size_t count = highres_scores.GetNumScoredFrames();
    for (size_t index = 0; index < count; ++index) {
      writer->StartWritingElement(kXmpRdfRdfLi);
      WriteFrame(writer, highres_scores.GetFrameScoreDescriptor(index), false);
      writer->FinishWritingElement();
    }
  }
  writer->FinishWritingElementsToDepth(scores_depth);
}

}  // namespace

VideoMetadataWriter::VideoMetadataWriter(const MetadataDescriptor& metadata,
                                         bool is_sample)
    : metadata_(metadata), is_sample_(is_sample) {}

void VideoMetadataWriter::WriteNamespaces(XmlWriter* writer) {
  writer->WriteXmlns(kPrefix, kUri);
}

void VideoMetadataWriter::WriteAttributeNamesAndValues(XmlWriter* writer) {
  WriteFlags(writer, metadata_.GetMetadataFlagsDescriptor());
  WriteScores(writer, metadata_.GetScoreDescriptor(), is_sample_);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
