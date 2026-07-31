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

#include "motion_photo_builder/function.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "image_io/base/byte_data.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/base/types.h"
#include "image_io/utils/file_utils.h"
#include "image_io/utils/message_stats_writer.h"
#include "image_io/utils/string_outputter.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "image_io/xmp/xmp_container_metadata.h"
#include "motion_photo/camera_metadata.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_reader.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo/motion_photo_writer.h"
#include "motion_photo/video_metadata_reader.h"
#include "motion_photo_heif/motion_photo_heif_image_builder.h"
#include "motion_photo_jpeg/motion_photo_jpeg_image_builder.h"
#include "motion_photo_jpeg/motion_photo_jpeg_info_builder.h"
#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::Byte;
using image_io::ByteData;
using image_io::DataRange;
using image_io::DataSegment;
using image_io::DataSegmentDataSource;
using image_io::GetFileSize;
using image_io::Message;
using image_io::MessageHandler;
using image_io::MessageStatsWriter;
using image_io::MessageWriter;
using image_io::StringOutputter;
using image_io::StringOutputterMessageWriter;
using std::ifstream;
using std::ofstream;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::vector;

namespace {

const char kBuilderName[] = "Motion Photo Builder";
const char kBuilderVersion[] = " V1.0";

const char kWorkVideoFileName[] = "/tmp/motion_photo_working_trailer.mp4";
const char kOptVideoFileName[] = "/tmp/motion_photo_optimized_trailer.mp4";

string GetDefaultXmpData(int model_version = 1) {
  return string(
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
      "x:xmptk=\"Adobe XMP Core 5.1.0-jc003\">\n"
      "  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      "    <rdf:Description rdf:about=\"\"\n"
      "      xmlns:Metadata="
      "\"http://ns.google.com/1.0/motion_photo_video_metadata/\">\n"
      "      <Metadata:Flags>\n"
      "        <Metadata:LowRes Metadata:Stabilized=\"0\"/>\n"
      "        <Metadata:HighRes Metadata:Stabilized=\"0\" />\n"
      "      </Metadata:Flags>\n"
      "      <Metadata:Scores Metadata:ModelVersion=\"") +
      std::to_string(model_version) +
      "\">\n"
      "        <Metadata:PrimaryImage>\n"
      "          <Metadata:Frame Metadata:Score=\"1.0\" "
      "Metadata:Time=\"0\" />\n"
      "        </Metadata:PrimaryImage>\n"
      "        <Metadata:HighResTrack>\n"
      "          <rdf:Seq>\n"
      "            <rdf:li>\n"
      "              <Metadata:Frame Metadata:Score=\"1.0\" "
      "Metadata:Time=\"0\" />\n"
      "            </rdf:li>\n"
      "          </rdf:Seq>\n"
      "        </Metadata:HighResTrack>\n"
      "      </Metadata:Scores>\n"
      "    </rdf:Description>\n"
      "  </rdf:RDF>\n"
      "</x:xmpmeta>\n";
}

struct Params {
  bool usage_given = false;
  string primary_image_file_name;
  string primary_video_file_name;
  string moments_video_file_name;
  string moments_xmp_file_name;
  string output_image_file_name;
  string working_video_file_name;
  string optimized_video_file_name;
};

string GetProgramName(std::string_view arg0) {
  string program_name;
  size_t last_slash = arg0.rfind('/');
  if (last_slash != string::npos) {
    program_name = arg0.substr(last_slash + 1);
  }
  if (program_name.empty()) {
    program_name = "motion_photo_builder";
  }
  return program_name;
}

Params PrintUsage(std::string_view arg0, const StringOutputter& outputter,
                  bool extended = false) {
  stringstream ostr;
  ostr << "Usage: " << GetProgramName(arg0) << " [options]\n";
  ostr << "where options is one or more of:\n"
          " -h  print this and the extended help message\n"
          " -oi output_image_file_name\n"
          "     specifies the name of the output motion photo image file.\n"
          "     if this file name is not specified, the program just checks\n"
          "     the input files and outputs a summary of their contents.\n"
          " -pi primary_image_file_name\n"
          "     specifies the name of the primary jpeg or heif (heic/avif) image file\n"
          " -pv primary_video_file_name\n"
          "     specifies the name of the primary video file\n"
          " -mv moments_video_file_name (OPTIONAL)\n"
          "     specifies the name of the video file containing the alternate\n"
          "     moment frames\n"
          " -mx moments_xmp_file_name\n"
          "     specifies the name of the xmp file containing the moments\n"
          "     information -- use the -h option for more information\n";
  if (extended) {
    ostr << "Advanced options:\n"
            " -wv working_video_file\n"
            "     specifies the name of the working video file.\n"
            "     This file contains the video tracks that will be optimized\n"
            "     and then used in the final output file. If not specified, a\n"
            "     file in the /tmp directory is used\n"
            " -wo working_optimized_video_file\n"
            "     specifies the name of the optimized working video file.\n"
            "     This file contains the video tracks that will be used in\n"
            "     the final output file. If not specified, a file in the /tmp\n"
            "     directory is used\n"
            "\n"
            "Sample syntax for the moments xmp file:\n";
    ostr << VideoMetadataReader::GetSampleXmpString();
  }
  ostr << "\n\n";
  outputter(ostr.str());
  Params params;
  params.usage_given = true;
  return params;
}

Params GetParams(int argc, const char* argv[],
                 const StringOutputter& outputter) {
  Params params;
  string arg0 = argv[0];
  if (argc < 2) {
    return PrintUsage(arg0, outputter);
  }
  string expecting;
  string* arg_file_name_pointer = nullptr;
  for (int index = 1; index < argc; ++index) {
    const string arg = argv[index];
    if (arg_file_name_pointer != nullptr) {
      if (!arg_file_name_pointer->empty()) {
        stringstream ss;
        ss << "Error:Parameter already specified: " << expecting << "\n\n";
        outputter(ss.str());
        return PrintUsage(arg0, outputter);
      }
      *arg_file_name_pointer = arg;
      arg_file_name_pointer = nullptr;
    } else if (arg == "-h") {
      return PrintUsage(arg0, outputter, true);
    } else if (arg == "-pi") {
      expecting = "primary_image_file_name";
      arg_file_name_pointer = &params.primary_image_file_name;
    } else if (arg == "-pv") {
      expecting = "primary_video_file_name";
      arg_file_name_pointer = &params.primary_video_file_name;
    } else if (arg == "-mv") {
      expecting = "moments_video_file_name";
      arg_file_name_pointer = &params.moments_video_file_name;
    } else if (arg == "-mx") {
      expecting = "moments_xmp_file_name";
      arg_file_name_pointer = &params.moments_xmp_file_name;
    } else if (arg == "-oi") {
      expecting = "output_image_file_name";
      arg_file_name_pointer = &params.output_image_file_name;
    } else if (arg == "-wv") {
      expecting = "working_video_file_name";
      arg_file_name_pointer = &params.working_video_file_name;
    } else if (arg == "-wo") {
      expecting = "working_optimized_video_file_name";
      arg_file_name_pointer = &params.optimized_video_file_name;
    } else {
      stringstream ss;
      ss << "Error:Unrecognized option:" << arg << "\n\n";
      outputter(ss.str());
      return PrintUsage(arg0, outputter);
    }
  }
  if (arg_file_name_pointer != nullptr) {
    stringstream ss;
    ss << "Error:Missing " << expecting << "\n\n";
    outputter(ss.str());
    return PrintUsage(arg0, outputter);
  } else if (!params.output_image_file_name.empty() &&
             (params.primary_image_file_name.empty() ||
              params.primary_video_file_name.empty())) {
    outputter("Error:The -oi option requires -pi, -pv\n\n");
    return PrintUsage(arg0, outputter);
  }

  if (params.working_video_file_name.empty()) {
    params.working_video_file_name = kWorkVideoFileName;
  }
  if (params.optimized_video_file_name.empty()) {
    params.optimized_video_file_name = kOptVideoFileName;
  }
  return params;
}

template <class T>
string String(const T& value) {
  stringstream ss;
  ss << value;
  return ss.str();
}

bool ReadFile(const string& file_name, MessageHandler* message_handler,
              stringstream* ss) {
  ifstream file(file_name);
  if (!file.is_open()) {
    message_handler->ReportMessage(Message::kStdLibError, file_name);
    return false;
  }
  *ss << file.rdbuf();
  file.close();
  return true;
}

void GetSummary(const VideoMetadata& video_metadata, stringstream* ss) {
  const auto& scores = video_metadata.GetScoreDescriptor();
  const auto& frame = scores.GetPrimaryImageFrameScoreDescriptor();
  *ss << "Primary frame score: " << frame.GetScore()
      << " timestamp: " << frame.GetPresentationTimestampUs() << "\n";
  const auto& highres = scores.GetHighResTrackScoreDescriptor();
  size_t count = highres.GetNumScoredFrames();
  *ss << "High res frame scores: " << count << "\n";
  for (size_t index = 0; index < count; ++index) {
    const auto& f = highres.GetFrameScoreDescriptor(index);
    *ss << index << ": score: " << f.GetScore()
        << " timestamp: " << f.GetPresentationTimestampUs() << "\n";
  }
}

void GetHexDump(const vector<uint8_t>& bytes, stringstream* ss) {
  for (size_t index = 0; index < bytes.size(); ++index) {
    if ((index % 16) == 0) {
      if (index > 0) *ss << '\n';
      if (index < 10) *ss << " ";
      *ss << index << ": ";
    }
    *ss << ByteData::Byte2Hex(bytes[index]) << "  ";
  }
  *ss << "\n\n";
}

FileType DetectFileType(const MediaSource& source) {
  if (source.GetSize() < 4) return FileType::kUnsupported;
  uint8_t magic[12];
  size_t read_len = std::min(source.GetSize(), sizeof(magic));
  if (!source.ReadAt(0, read_len, magic)) return FileType::kUnsupported;

  if (magic[0] == 0xFF && magic[1] == 0xD8) {
    return FileType::kJpeg;
  }
  if (read_len >= 8 && magic[4] == 'f' && magic[5] == 't' &&
      magic[6] == 'y' && magic[7] == 'p') {
    if (read_len >= 12) {
      if (magic[8] == 'a' && magic[9] == 'v' &&
          magic[10] == 'i' && magic[11] == 'f') {
        return FileType::kAvif;
      }
    }
    return FileType::kHeic;
  }
  return FileType::kUnsupported;
}

string MergeXmpMetadata(const string& original_xmp,
                        const MotionPhoto& motion_photo) {
  string xmp = original_xmp;

  int64_t timestamp = 0;
  if (motion_photo.GetCameraMetadata()
          .motion_photo_presentation_timestamp_us.WasAssigned()) {
    timestamp = motion_photo.GetCameraMetadata()
                    .motion_photo_presentation_timestamp_us.GetValue();
  }

  size_t video_length = 0;
  size_t video_padding = 0;
  const auto& items = motion_photo.GetContainerMetadata().items;
  for (const auto& item : items) {
    if (item.semantic.WasAssigned() &&
        item.semantic.GetValue() == kContainerVideoItemSemantic) {
      if (item.length.WasAssigned()) {
        video_length = item.length.GetValue();
      }
      if (item.padding.WasAssigned()) {
        video_padding = item.padding.GetValue();
      }
      break;
    }
  }

  size_t desc_start = xmp.find("<rdf:Description");
  if (desc_start != string::npos) {
    size_t desc_end = xmp.find('>', desc_start);
    if (desc_end != string::npos) {
      if (desc_end > 0 && xmp[desc_end - 1] == '/') {
        xmp.replace(desc_end - 1, 2, ">\n  </rdf:Description>");
        desc_end = xmp.find('>', desc_start);
      }
      string desc_tag = xmp.substr(desc_start, desc_end - desc_start);
      desc_tag = std::regex_replace(
          desc_tag,
          std::regex(R"(\bContainer:Version\s*=\s*("[^"]*"|'[^']*'))"), "");
      desc_tag = std::regex_replace(
          desc_tag,
          std::regex(
              R"(\bCamera:(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs)\s*=\s*("[^"]*"|'[^']*'))"),
          "");

      if (!absl::StrContains(desc_tag, "xmlns:Camera=")) {
        desc_tag += " xmlns:Camera=\"http://ns.google.com/photos/1.0/camera/\"";
      }
      if (!absl::StrContains(desc_tag, "xmlns:Container=")) {
        desc_tag +=
            " xmlns:Container=\"http://ns.google.com/photos/1.0/container/\"";
      }
      if (!absl::StrContains(desc_tag, "xmlns:Item=")) {
        desc_tag +=
            " xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\"";
      }

      desc_tag += " Camera:MotionPhoto=\"1\"";
      desc_tag += " Camera:MotionPhotoVersion=\"" +
                  std::to_string(kMotionPhotoVersionValue) + "\"";
      desc_tag += " Camera:MotionPhotoPresentationTimestampUs=\"" +
                  std::to_string(timestamp) + "\"";
      desc_tag += " Container:Version=\"" + string(kContainerVersion) + "\"";

      xmp.replace(desc_start, desc_end - desc_start, desc_tag);
    }
  }

  string video_item =
      "          <rdf:li rdf:parseType=\"Resource\">\n"
      "            <Container:Item\n"
      "              Item:Semantic=\"MotionPhoto\"\n"
      "              Item:Mime=\"video/mp4\"\n"
      "              Item:Length=\"" +
      std::to_string(video_length) + "\"";
  if (video_padding > 0) {
    video_item += "\n              Item:Padding=\"" +
                  std::to_string(video_padding) + "\"";
  }
  video_item += "/>\n          </rdf:li>\n";

  size_t dir_start = xmp.find("<Container:Directory>");
  if (dir_start != string::npos) {
    size_t dir_end = xmp.find("</Container:Directory>", dir_start);
    if (dir_end != string::npos) {
      string directory_part = xmp.substr(dir_start, dir_end - dir_start);
      std::regex item_regex(R"(<Container:Item\b([^>]*)\/>)");
      std::smatch match;
      std::string new_directory_part = directory_part;
      bool found_motion_photo = false;
      std::string::const_iterator search_iter(directory_part.cbegin());
      while (std::regex_search(search_iter, directory_part.cend(), match,
                               item_regex)) {
        std::string item_tag = match.str(0);
        if (absl::StrContains(item_tag, "Item:Semantic=\"MotionPhoto\"")) {
          found_motion_photo = true;
          std::string new_item_tag = std::regex_replace(
              item_tag, std::regex(R"(Item:Length\s*=\s*("[^"]*"|'[^']*'))"),
              "Item:Length=\"" + std::to_string(video_length) + "\"");
          if (video_padding > 0) {
            if (absl::StrContains(new_item_tag, "Item:Padding=")) {
              new_item_tag = std::regex_replace(
                  new_item_tag,
                  std::regex(R"(Item:Padding\s*=\s*("[^"]*"|'[^']*'))"),
                  "Item:Padding=\"" + std::to_string(video_padding) + "\"");
            } else {
              size_t close_idx = new_item_tag.rfind("/>");
              if (close_idx != string::npos) {
                new_item_tag.insert(close_idx,
                                    "\n              Item:Padding=\"" +
                                        std::to_string(video_padding) + "\" ");
              }
            }
          } else {
            new_item_tag = std::regex_replace(
                new_item_tag,
                std::regex(R"(\bItem:Padding\s*=\s*("[^"]*"|'[^']*'))"), "");
          }
          size_t offset_in_orig =
              std::distance(directory_part.cbegin(), search_iter) +
              match.position(0);
          new_directory_part.replace(offset_in_orig, item_tag.length(),
                                     new_item_tag);
          break;
        }
        search_iter = match[0].second;
      }
      if (found_motion_photo) {
        xmp.replace(dir_start, dir_end - dir_start, new_directory_part);
      } else {
        size_t seq_end = directory_part.rfind("</rdf:Seq>");
        if (seq_end != string::npos) {
          directory_part.insert(seq_end, video_item);
          xmp.replace(dir_start, dir_end - dir_start, directory_part);
        }
      }
    }
  } else {
    string primary_mime = (motion_photo.GetFileType() == FileType::kJpeg)
                              ? "image/jpeg"
                              : "image/heic";
    string directory =
        "      <Container:Directory>\n"
        "        <rdf:Seq>\n"
        "          <rdf:li rdf:parseType=\"Resource\">\n"
        "            <Container:Item\n"
        "              Item:Semantic=\"Primary\"\n"
        "              Item:Mime=\"" +
        primary_mime +
        "\"/>\n"
        "          </rdf:li>\n" +
        video_item +
        "        </rdf:Seq>\n"
        "      </Container:Directory>\n";

    size_t desc_end = xmp.find("</rdf:Description>");
    if (desc_end != string::npos) {
      xmp.insert(desc_end, directory);
    }
  }

  return xmp;
}

}  // namespace

int BuildMotionPhotoDirect(const MotionPhotoStreamParams& params,
                           DataSink* output_sink,
                           const StringOutputter& outputter) {
  if (params.primary_image == nullptr || params.primary_video == nullptr ||
      output_sink == nullptr) {
    outputter("Error: Invalid null stream parameters\n");
    return 1;
  }

  MessageHandler message_handler;
  unique_ptr<image_io::MessageWriter> message_writer(
      new StringOutputterMessageWriter(outputter));
  message_handler.SetMessageWriter(std::move(message_writer));
  auto message_stats = message_handler.GetMessageStats();

  FileType file_type = DetectFileType(*params.primary_image);
  if (file_type == FileType::kUnsupported) {
    message_handler.ReportMessage(Message::kDecodingError,
                                  "Unsupported primary image file type");
    return 1;
  }

  MotionPhoto motion_photo(file_type);
  MotionPhotoWriter motion_photo_writer(motion_photo);
  int64_t target_timestamp = params.presentation_timestamp_us;
  if (target_timestamp >= 0) {
    motion_photo.SetImageTimestamp(target_timestamp);
  }

  if (!params.moments_xmp.empty()) {
    size_t count = 0;
    MotionPhotoReader motion_photo_reader(&motion_photo, &message_handler);
    if (motion_photo_reader.ReadVideoMetadata(params.moments_xmp, &count)) {
      if (target_timestamp < 0) {
        target_timestamp = motion_photo.GetVideoMetadata()
                               .GetScoreDescriptor()
                               .GetPrimaryImageFrameScoreDescriptor()
                               .GetPresentationTimestampUs();
        if (target_timestamp >= 0) {
          motion_photo.SetImageTimestamp(target_timestamp);
        }
      }
    }
  }

  size_t video_size = params.primary_video->GetSize();
  motion_photo.SetVideoLength(video_size);

  if (IsHeif(file_type)) {
    motion_photo.SetMpvdBox(MpvdBox(video_size));
    motion_photo.SetImagePadding(motion_photo.GetMpvdBox().GetHeaderLength());
  }

  string image_metadata;
  bool xmp_merged = false;
  std::unique_ptr<MotionPhotoJpegInfoBuilder> jpeg_info_builder;

  if (file_type == FileType::kJpeg) {
    jpeg_info_builder =
        std::make_unique<MotionPhotoJpegInfoBuilder>(&message_handler);
    size_t image_size = params.primary_image->GetSize();
    Byte* img_buf = new Byte[image_size];
    if (params.primary_image->ReadAt(0, image_size, img_buf)) {
      auto image_segment = DataSegment::Create(
          DataRange(0, image_size), img_buf, DataSegment::kDelete);
      image_io::DataSegmentDataSource image_source(image_segment);
      jpeg_info_builder->Build(&image_source);
      if (jpeg_info_builder->GetPrimaryXmpStringRange().IsValid()) {
        size_t bytes_parsed = 0;
        MotionPhotoReader motion_photo_reader(&motion_photo, &message_handler);
        motion_photo_reader.ReadImageMetadata(
            {jpeg_info_builder->GetPrimaryXmpStringRange()}, &image_source,
            &bytes_parsed);
        if (target_timestamp != -1) {
          motion_photo.SetImageTimestamp(target_timestamp);
        }
      }
      motion_photo.SetIsMotionPhoto(true);

      const auto& image_ranges = jpeg_info_builder->GetInfo().GetImageRanges();
      if (image_ranges.size() > 1) {
        ContainerMetadata container = motion_photo.GetContainerMetadata();
        bool has_gainmap = false;
        for (auto& item : container.items) {
          if (item.semantic.WasAssigned() &&
              item.semantic.GetValue() == kContainerItemSemanticGainMap) {
            item.length = image_ranges[1].GetLength();
            has_gainmap = true;
            break;
          }
        }
        if (!has_gainmap) {
          ContainerItemMetadata gainmap_item;
          gainmap_item.semantic = kContainerItemSemanticGainMap;
          gainmap_item.mime = kContainerJpegImageItemMime;
          gainmap_item.length = image_ranges[1].GetLength();
          container.items.insert(container.items.begin() + 1, gainmap_item);
        }
        motion_photo.SetContainerMetadata(std::move(container));
      }
      motion_photo.SetVideoLength(video_size);
      motion_photo.EnforceConsistentValues();

      if (jpeg_info_builder->GetPrimaryXmpStringRange().IsValid()) {
        auto xmp_range = jpeg_info_builder->GetPrimaryXmpStringRange();
        auto xmp_segment = image_source.GetDataSegment(xmp_range.GetBegin(),
                                                       xmp_range.GetLength());
        if (xmp_segment) {
          string original_xmp(
              reinterpret_cast<const char*>(
                  xmp_segment->GetBuffer(xmp_range.GetBegin())),
              xmp_range.GetLength());
          image_metadata = MergeXmpMetadata(original_xmp, motion_photo);
          xmp_merged = true;
        }
      }
    } else {
      delete[] img_buf;
    }
  }

  if (!xmp_merged) {
    motion_photo_writer.WriteImageMetadata(&image_metadata);
  }

  // 1. Direct Image Build into output sink
  if (file_type == FileType::kJpeg) {
    MotionPhotoJpegImageBuilder jpeg_builder(&message_handler);
    if (!jpeg_builder.Build(*params.primary_image, image_metadata, output_sink,
                            jpeg_info_builder.get())) {
      return 1;
    }
  } else if (IsHeif(file_type)) {
    MotionPhotoHeifImageBuilder heif_builder(&message_handler);
    if (!heif_builder.Build(*params.primary_image, image_metadata,
                            &motion_photo.GetMpvdBox(), output_sink)) {
      return 1;
    }
  }

  // 2. Direct Video Transfer into output sink
  if (!output_sink->TransferFrom(*params.primary_video, 0, video_size)) {
    message_handler.ReportMessage(Message::kInternalError,
                                  "Failed to append video payload to output sink");
    return 1;
  }

  output_sink->Flush();
  return message_stats->error_count == 0 ? 0 : 1;
}

int BuildMotionPhotoFromMemory(const MotionPhotoBuilderMemoryParams& params,
                               const StringOutputter& outputter) {
  if (params.primary_image_bytes == nullptr || params.primary_image_size == 0 ||
      params.primary_video_bytes == nullptr || params.primary_video_size == 0 ||
      params.output_image_file_name.empty()) {
    outputter(
        "Error: Invalid memory parameters for BuildMotionPhotoFromMemory\n");
    return 1;
  }

  auto img_source = MediaSource::FromMemory(params.primary_image_bytes,
                                            params.primary_image_size);
  auto vid_source = MediaSource::FromMemory(params.primary_video_bytes,
                                            params.primary_video_size);

  MessageHandler handler;
  auto out_sink = DataSink::ToFile(params.output_image_file_name, &handler);
  if (!out_sink) {
    outputter("Error: Failed to open output file for writing: " +
              params.output_image_file_name + "\n");
    return 1;
  }

  MotionPhotoStreamParams stream_params;
  stream_params.primary_image = std::move(img_source);
  stream_params.primary_video = std::move(vid_source);
  stream_params.moments_xmp = params.moments_xmp;
  stream_params.presentation_timestamp_us = params.presentation_timestamp_us;

  return BuildMotionPhotoDirect(stream_params, out_sink.get(), outputter);
}

int BuildMotionPhoto(const MotionPhotoBuilderParams& params,
                     const StringOutputter& outputter) {
  MotionPhotoBuilderParams local_params = params;
  if (local_params.working_video_file_name.empty()) {
    local_params.working_video_file_name = kWorkVideoFileName;
  }
  if (local_params.optimized_video_file_name.empty()) {
    local_params.optimized_video_file_name = kOptVideoFileName;
  }

  MessageHandler message_handler;
  int64_t target_timestamp = -1;
  unique_ptr<image_io::MessageWriter> message_writer(
      new StringOutputterMessageWriter(outputter));
  message_handler.SetMessageWriter(std::move(message_writer));
  auto message_stats = message_handler.GetMessageStats();
  MessageStatsWriter message_stats_writer(message_stats, kBuilderName,
                                          outputter);

  // No output file is ok (check mode): report warning
  if (local_params.output_image_file_name.empty()) {
    outputter("\n");
    message_handler.ReportMessage(
        Message::kWarning,
        "No output image file name specified\n"
        "- Input files will be read and checked only");
  }

  FileType file_type =
      GetFileTypeFromFileName(local_params.primary_image_file_name);
  if (file_type == FileType::kUnsupported) {
    message_handler.ReportMessage(Message::kDecodingError,
                                  "Unsupported primary image file type");
    return 1;
  }

  MotionPhoto motion_photo(file_type);
  MotionPhotoWriter motion_photo_writer(motion_photo);
  vector<uint8_t> video_metadata;
  stringstream xmpss;
  bool has_xmp = false;
  if (!local_params.moments_xmp_file_name.empty()) {
    outputter("\nReading " + local_params.moments_xmp_file_name + "\n");
    if (ReadFile(local_params.moments_xmp_file_name, &message_handler,
                 &xmpss)) {
      has_xmp = true;
    }
  } else if (!local_params.moments_video_file_name.empty()) {
    outputter("\nUsing default XMP metadata\n");
    xmpss << GetDefaultXmpData();
    has_xmp = true;
  }

  if (local_params.presentation_timestamp_us >= 0) {
    motion_photo.SetImageTimestamp(local_params.presentation_timestamp_us);
  }

  if (has_xmp) {
    size_t count = 0;
    MotionPhotoReader motion_photo_reader(&motion_photo, &message_handler);
    if (motion_photo_reader.ReadVideoMetadata(xmpss.str(), &count)) {
      motion_photo_writer.EncodeVideoMetadata(&video_metadata);
      if (local_params.presentation_timestamp_us < 0) {
        target_timestamp = motion_photo.GetVideoMetadata()
                               .GetScoreDescriptor()
                               .GetPrimaryImageFrameScoreDescriptor()
                               .GetPresentationTimestampUs();
        motion_photo.SetImageTimestamp(target_timestamp);
      }
      stringstream ss;
      GetSummary(motion_photo.GetVideoMetadata(), &ss);
      outputter(ss.str());
    }
  }

  bool disable_mp4_optimize = false;
#ifdef DISABLE_MP4_OPTIMIZE
  disable_mp4_optimize = true;
#endif

  if (!disable_mp4_optimize && !local_params.primary_video_file_name.empty()) {
    if (!MotionPhotoMp4FileBuilder::IsOptimizationSupported(
            local_params.primary_video_file_name, &message_handler)) {
      disable_mp4_optimize = true;
    }
  }

  if (local_params.moments_video_file_name.empty() && video_metadata.empty()) {
    disable_mp4_optimize = true;
  }

  bool need_working_video = !local_params.moments_video_file_name.empty() ||
                            !video_metadata.empty() || !disable_mp4_optimize;

  string track_summary;
  size_t file_size = 0;
  string video_to_append = local_params.primary_video_file_name;

  if (need_working_video) {
    video_to_append = local_params.working_video_file_name;
    MotionPhotoMp4FileBuilder mp4_builder(local_params.working_video_file_name,
                                          &message_handler);
    if (!local_params.primary_video_file_name.empty()) {
      outputter("\nReading " + local_params.primary_video_file_name + "\n");
      if (!mp4_builder.AddPrimaryMp4FileTracks(
              local_params.primary_video_file_name, &track_summary)) {
        return 1;
      } else {
        outputter(track_summary);
      }
    }
    if (!local_params.moments_video_file_name.empty()) {
      outputter("\nReading " + local_params.moments_video_file_name + "\n");
      if (!mp4_builder.AddMomentsMp4FileTrack(
              local_params.moments_video_file_name, &track_summary)) {
        return 1;
      } else {
        outputter(track_summary);
      }
    }
    if (!video_metadata.empty()) {
      outputter("\nWriting video metadata: " + String(video_metadata.size()) +
                " bytes\n");
      if (!mp4_builder.AddMomentsMetadataTrack(video_metadata)) {
        return 1;
      } else {
        stringstream ss;
        GetHexDump(video_metadata, &ss);
        outputter(ss.str());
      }
    }

    if (!local_params.optimized_video_file_name.empty() &&
        !disable_mp4_optimize) {
#ifndef DISABLE_MP4_OPTIMIZE
      outputter("\nOptimizing video trailer\n");
#else
      outputter("\nFinalizing video trailer\n");
#endif
      if (!mp4_builder.OptimizeMp4File(&track_summary, &file_size)) {
        return 1;
      }
      outputter("Video trailer size: " + String(file_size) + " bytes\n");
      outputter(track_summary);
      video_to_append = local_params.working_video_file_name;
    } else {
      if (!GetFileSize(local_params.working_video_file_name, &file_size)) {
        message_handler.ReportMessage(Message::kStdLibError,
                                      local_params.working_video_file_name);
        return 1;
      }
      track_summary = MotionPhotoMp4FileBuilder::GetFileTrackSummary(
          local_params.working_video_file_name, &message_handler);
      outputter("Video trailer size: " + String(file_size) + " bytes\n");
      outputter(track_summary);
    }
    motion_photo.SetVideoLength(file_size);
  } else {
    video_to_append = local_params.primary_video_file_name;
    if (!video_to_append.empty()) {
      outputter("\nBypassing video trailer modification\n");
      if (!GetFileSize(video_to_append, &file_size)) {
        message_handler.ReportMessage(Message::kStdLibError, video_to_append);
        return 1;
      }
      track_summary = MotionPhotoMp4FileBuilder::GetFileTrackSummary(
          video_to_append, &message_handler);
      if (track_summary.empty()) {
        return 1;
      }
      outputter("Video trailer size: " + String(file_size) + " bytes\n");
      outputter(track_summary);
      motion_photo.SetVideoLength(file_size);
    }
  }

  if (IsHeif(file_type)) {
    motion_photo.SetMpvdBox(MpvdBox(file_size));
    motion_photo.SetImagePadding(motion_photo.GetMpvdBox().GetHeaderLength());
  }

  string image_metadata;
  bool xmp_merged = false;

  if (file_type == FileType::kJpeg) {
    auto image_segment =
        ReadEntireFile(local_params.primary_image_file_name, &message_handler);
    if (image_segment) {
      image_io::DataSegmentDataSource image_source(image_segment);
      MotionPhotoJpegInfoBuilder info_builder(&message_handler);
      info_builder.Build(&image_source);
      if (info_builder.GetPrimaryXmpStringRange().IsValid()) {
        size_t bytes_parsed = 0;
        MotionPhotoReader motion_photo_reader(&motion_photo, &message_handler);
        motion_photo_reader.ReadImageMetadata(
            {info_builder.GetPrimaryXmpStringRange()}, &image_source,
            &bytes_parsed);
        if (target_timestamp != -1) {
          motion_photo.SetImageTimestamp(target_timestamp);
        }
      }
      motion_photo.SetIsMotionPhoto(true);

      const auto& image_ranges = info_builder.GetInfo().GetImageRanges();
      ContainerMetadata container = motion_photo.GetContainerMetadata();
      auto& items = container.items;
      if (image_ranges.size() > 1) {
        bool has_gainmap = false;
        for (auto& item : items) {
          if (item.semantic.WasAssigned() &&
              item.semantic.GetValue() == kContainerItemSemanticGainMap) {
            item.length = image_ranges[1].GetLength();
            has_gainmap = true;
            break;
          }
        }
        if (!has_gainmap) {
          ContainerItemMetadata gainmap_item;
          gainmap_item.semantic = kContainerItemSemanticGainMap;
          gainmap_item.mime = kContainerJpegImageItemMime;
          gainmap_item.length = image_ranges[1].GetLength();
          items.insert(items.begin() + 1, gainmap_item);
        }
      } else {
        for (auto it = items.begin(); it != items.end(); ++it) {
          if (it->semantic.WasAssigned() &&
              it->semantic.GetValue() == kContainerItemSemanticGainMap) {
            items.erase(it);
            break;
          }
        }
      }
      motion_photo.SetContainerMetadata(std::move(container));
      if (file_size > 0) {
        motion_photo.SetVideoLength(file_size);
      }
      motion_photo.EnforceConsistentValues();
      if (info_builder.GetPrimaryXmpStringRange().IsValid()) {
        auto xmp_range = info_builder.GetPrimaryXmpStringRange();
        auto xmp_segment = image_source.GetDataSegment(xmp_range.GetBegin(),
                                                       xmp_range.GetLength());
        if (xmp_segment) {
          string original_xmp(
              reinterpret_cast<const char*>(
                  xmp_segment->GetBuffer(xmp_range.GetBegin())),
              xmp_range.GetLength());
          image_metadata = MergeXmpMetadata(original_xmp, motion_photo);
          xmp_merged = true;
        }
      }
    }
  }

  if (!xmp_merged) {
    motion_photo_writer.WriteImageMetadata(&image_metadata);
  }

  outputter("\nImage metadata:\n" + image_metadata);
  outputter("\nReading " + local_params.primary_image_file_name + "\n");

  if (local_params.output_image_file_name.empty()) {
    // Check mode completed successfully
    outputter("Image read and metadata merged ok\n");
    if (need_working_video &&
        local_params.working_video_file_name == kWorkVideoFileName) {
      std::remove(local_params.working_video_file_name.c_str());
    }
    return 0;
  }

  // Direct generation into destination file
  auto out_sink =
      DataSink::ToFile(local_params.output_image_file_name, &message_handler);
  if (!out_sink) {
    return 1;
  }

  auto img_source = MediaSource::FromFile(local_params.primary_image_file_name,
                                          &message_handler);
  auto vid_source = MediaSource::FromFile(video_to_append, &message_handler);
  if (!img_source || !vid_source) {
    return 1;
  }

  if (file_type == FileType::kJpeg) {
    MotionPhotoJpegImageBuilder jpeg_builder(&message_handler);
    if (!jpeg_builder.Build(*img_source, image_metadata, out_sink.get())) {
      return 1;
    }
  } else if (IsHeif(file_type)) {
    MotionPhotoHeifImageBuilder heif_builder(&message_handler);
    if (!heif_builder.Build(*img_source, image_metadata,
                            &motion_photo.GetMpvdBox(), out_sink.get())) {
      return 1;
    }
  }

  outputter("Image read and metadata merged ok\n");
  outputter("\nConcatenating image and video files into " +
            local_params.output_image_file_name + "\n");

  if (!out_sink->TransferFrom(*vid_source, 0, vid_source->GetSize())) {
    message_handler.ReportMessage(Message::kInternalError,
                                  "Failed to append video payload");
    return 1;
  }

  out_sink->Flush();
  outputter("Motion photo written to " + local_params.output_image_file_name +
            " ok\n");

  if (need_working_video &&
      local_params.working_video_file_name == kWorkVideoFileName) {
    std::remove(local_params.working_video_file_name.c_str());
  }

  return message_stats->error_count == 0 ? 0 : 1;
}

int MotionPhotoBuilderFunction(int argc, const char* argv[],
                               const StringOutputter& outputter) {
  outputter(string(kBuilderName) + kBuilderVersion + "\n");
  Params params = GetParams(argc, argv, outputter);
  if (params.usage_given) {
    return 1;
  }

  MotionPhotoBuilderParams api_params;
  api_params.primary_image_file_name = params.primary_image_file_name;
  api_params.primary_video_file_name = params.primary_video_file_name;
  api_params.moments_video_file_name = params.moments_video_file_name;
  api_params.moments_xmp_file_name = params.moments_xmp_file_name;
  api_params.output_image_file_name = params.output_image_file_name;
  api_params.working_video_file_name = params.working_video_file_name;
  api_params.optimized_video_file_name = params.optimized_video_file_name;

  return BuildMotionPhoto(api_params, outputter);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
