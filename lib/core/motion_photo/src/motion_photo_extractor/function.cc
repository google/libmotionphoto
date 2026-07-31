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

#include "motion_photo_extractor/function.h"

#include <algorithm>
#include <cerrno>
#include <memory>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/utils/message_stats_writer.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "metadata_collection.pb.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo_checker.h"
#include "motion_photo/motion_photo_reader.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo/motion_photo_writer.h"
#include "motion_photo_checker_extractor_common/motion_photo_checker_extractor_common.h"
#include "motion_photo_heif/motion_photo_heif_image_builder.h"
#include "motion_photo_heif/motion_photo_heif_info_builder.h"
#include "motion_photo_jpeg/motion_photo_jpeg_image_builder.h"
#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataRange;
using image_io::DataSegment;
using image_io::DataSource;
using image_io::Message;
using image_io::MessageHandler;
using image_io::MessageStatsWriter;
using image_io::StringOutputter;
using image_io::StringOutputterMessageWriter;
using std::string;
using std::stringstream;
using std::tie;
using std::unique_ptr;

namespace {

const char kExtractorName[] = "Motion Photo Extractor";
const char kExtractorVersion[] = " V0.1";

// The fields in this structure are initialized by looking at the argv values
// passed to the MotionPhotoExtractor() function.
struct Params {
  bool usage_given = false;
  string motion_photo_file_name;
  string primary_image_file_name_output;
  string video_file_name_output;
  string metadata_file_name_output;
};

//   arg0: The first arg passed to the VideoBuilder() function.
// Returns the program name taken from the arg0 parameter with the leading path
// parts of the arg0 value removed.
string GetProgramName(std::string_view arg0) {
  string program_name;
  size_t last_slash = arg0.rfind('/');
  if (last_slash != string::npos) {
    program_name = arg0.substr(last_slash + 1);
  }
  if (program_name.empty()) {
    program_name = "motion_photo_checker";
  }
  return program_name;
}

// When there are no args specified, or there is an unrecognized option in the
// argv values this function is called to print the usage.
//   arg0: The argv[0] value that contains the executable path.
//   outputter: The outputter function to send the usage to.
//   extended: Whether to print extended help.
// Returns a Params instance with the usage_given field set to true.
Params PrintUsage(std::string_view arg0, const StringOutputter& outputter,
                  bool extended = false) {
  stringstream ostr;
  ostr << "Usage: " << GetProgramName(arg0) << " [options]\n";
  ostr << "where options is one or more of:\n"
          " -h  print this and the extended help message\n"
          " -mi motion_photo_file_name\n"
          "     specifies the name of the input motion photo image file.\n"
          " -po primary_image_file_name_output (OPTIONAL)\n"
          "     specifies the name of the primary jpeg or heif (heic/avif) image file\n"
          "     being decoded from the input motion photo file\n"
          " -vo video_file_name_output (OPTIONAL)\n"
          "     specifies the name of the primary video file\n"
          "     being decoded from the input motion photo file\n"
          " -mo metadata_file_name_output (OPTIONAL)\n"
          "     specifies the name of the metadata file extracted from the\n"
          "     input motion photo file\n";
  ostr << "\n\n";
  outputter(ostr.str());
  Params params;
  params.usage_given = true;
  return params;
}

//   argc: The number of strings in the argv array.
//   argv: The options and values used in the command line.
//   outputter: The outputter function to send the usage to.
// Returns a Params structure initialized with values from the argv array.
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
    } else if (arg == "-mi") {
      expecting = "motion_photo_file_name";
      arg_file_name_pointer = &params.motion_photo_file_name;
    } else if (arg == "-po") {
      expecting = "primary_image_file_name_output";
      arg_file_name_pointer = &params.primary_image_file_name_output;
    } else if (arg == "-vo") {
      expecting = "video_file_name_output";
      arg_file_name_pointer = &params.video_file_name_output;
    } else if (arg == "-mo") {
      expecting = "metadata_file_name_output";
      arg_file_name_pointer = &params.metadata_file_name_output;
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
  } else if (params.motion_photo_file_name.empty()) {
    outputter("Error:The input file name must be provided.\n\n");
    return PrintUsage(arg0, outputter);
  } else if (params.primary_image_file_name_output.empty() &&
                  params.video_file_name_output.empty() &&
                  params.metadata_file_name_output.empty()) {
    outputter("Error:At least one of the -po, -vo or -mo must be provided.\n\n");
    return PrintUsage(arg0, outputter);
  }

  return params;
}

// Simple template function to convert a value to a string.
//   value: The value to convert.
// Returns the string version of the value.
template <class T>
string String(const T& value) {
  stringstream ss;
  ss << value;
  return ss.str();
}

//   track_id: The id of the track to return a representation of.
// Returns a string representation of the track.

std::string StripMotionPhotoMetadata(const std::string& xmp) {
  std::string result = xmp;

  bool has_gainmap =
      (xmp.find("Item:Semantic=\"GainMap\"") != std::string::npos ||
       xmp.find("Item:Semantic='GainMap'") != std::string::npos ||
       xmp.find("<Item:Semantic>GainMap</Item:Semantic>") != std::string::npos);

  if (has_gainmap) {
    // Keep container, only remove MotionPhoto item
    std::regex attr_regex(
        R"(\b(GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs)\s*=\s*("[^"]*"|'[^']*'))");
    result = std::regex_replace(result, attr_regex, "");

    std::regex elem_regex(
        R"(<((GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs))>[^<]*</\1>)");
    result = std::regex_replace(result, elem_regex, "");

    std::regex item_regex(
        R"raw(\s*<rdf:li\b(?:(?!</rdf:li>)[\s\S])*?Item:Semantic="MotionPhoto"(?:(?!</rdf:li>)[\s\S])*?</rdf:li>)raw");
    result = std::regex_replace(result, item_regex, "");
  } else {
    // Original aggressive stripping
    std::regex attr_regex(
        R"(\b(GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs|Container:Version)\s*=\s*("[^"]*"|'[^']*'))");
    result = std::regex_replace(result, attr_regex, "");

    std::regex elem_regex(
        R"(<((GCamera|Camera):(MotionPhoto|MotionPhotoVersion|MotionPhotoPresentationTimestampUs))>[^<]*</\1>)");
    result = std::regex_replace(result, elem_regex, "");

    std::regex directory_regex(
        R"(<Container:Directory>[\s\S]*?</Container:Directory>)");
    result = std::regex_replace(result, directory_regex, "");
  }

  return result;
}

}  // namespace

int ExtractMotionPhoto(const MotionPhotoExtractorParams& params,
                       const StringOutputter& outputter) {
  MessageHandler message_handler;
  unique_ptr<image_io::MessageWriter> message_writer(
      new StringOutputterMessageWriter(outputter));
  message_handler.SetMessageWriter(std::move(message_writer));
  auto message_stats = message_handler.GetMessageStats();
  MessageStatsWriter stats_writer(message_stats, kExtractorName, outputter);

  // Check the file name (which can produce only warnings, no errors).
  // Read the file into a data source for later.
  size_t file_size = 0;
  FileType file_type = GetFileTypeFromFileName(params.motion_photo_file_name);
  if (file_type == FileType::kUnsupported) {
    message_handler.ReportMessage(
        Message::kDecodingError,
        "File type (based on extension) not supported");
    return -1;
  }

  MotionPhoto motion_photo(file_type);
  MotionPhotoReader motion_photo_reader(&motion_photo, &message_handler);
  MotionPhotoChecker motion_photo_checker(motion_photo, &message_handler);

  unique_ptr<DataSource> data_source;
  tie(data_source, file_size) =
      ReadFile(params.motion_photo_file_name, outputter, &message_handler);
  if (data_source == nullptr || file_size == 0) {
    return 1;
  }

  // Try 3P extraction using MetadataEngine
  MetadataEngine engine(&message_handler);
  MetadataCollection collection = engine.Parse(data_source.get(), file_size, file_type);

  int64_t video_offset = -1;
  int64_t video_length = -1;
  int64_t image_size = -1;

  for (const auto& block : collection.blocks()) {
    if (block.format_identifier() == "container.trailer") {
      image_size = block.offset();
      if (block.has_video_offset_in_block() && block.has_video_length()) {
        video_offset = block.offset() + block.video_offset_in_block();
        video_length = block.video_length();
      }
    }
  }

  if (!params.metadata_file_name_output.empty()) {
    if (!engine.ExtractMetadata(params.motion_photo_file_name,
                                params.metadata_file_name_output)) {
      outputter("Warning: MetadataEngine failed to extract metadata file.\n");
    }
  }

  if (video_offset != -1 && video_length != -1) {
    if (!params.video_file_name_output.empty()) {
      WriteToOutput(params.motion_photo_file_name,
                    params.video_file_name_output, video_offset,
                    video_offset + video_length);
    }
    if (!params.primary_image_file_name_output.empty() && image_size != -1) {
      if (file_type == FileType::kJpeg) {
        WriteToOutput(params.motion_photo_file_name,
                      params.primary_image_file_name_output, 0, image_size);
      } else if (IsHeif(file_type)) {
        MotionPhotoHeifInfoBuilder info_builder(&message_handler);
        if (info_builder.Build(data_source.get(), file_size)) {
          DataRange xmp_range = info_builder.GetXmpStringRange();
          if (xmp_range.IsValid()) {
            MotionPhotoHeifImageBuilder heif_builder(
                params.primary_image_file_name_output, &message_handler);
            std::shared_ptr<DataSegment> xmp_segment =
                data_source->GetDataSegment(xmp_range.GetBegin(),
                                            xmp_range.GetLength());
            if (xmp_segment) {
              std::string xmp_metadata(
                  reinterpret_cast<const char*>(
                      xmp_segment->GetBuffer(xmp_range.GetBegin())),
                  xmp_range.GetLength());
              heif_builder.AddImageFileAndMetadata(
                  params.motion_photo_file_name, xmp_metadata);
            }
          }
        }
      }
    }
    return 0;
  }

  // Get the primary image size and the location in the data source of the
  // primary XMP segment string.
  size_t std_image_size = 0;
  DataRange xmp_range;
  MpvdBox mpvd_box;
  tie(std_image_size, xmp_range, mpvd_box) = GetImageSizeAndXmpRange(
      data_source.get(), file_size, file_type, params.motion_photo_file_name,
      "", outputter, &message_handler);
  if ((std_image_size == 0) || !xmp_range.IsValid()) {
    return 1;
  }
  motion_photo.SetMpvdBox(mpvd_box);

  // Parse the XMP metadata string from the data source and check the values.
  if (!ParseAndCheckXmpMetadata(data_source.get(), file_size, file_type,
                                params.motion_photo_file_name, xmp_range,
                                std_image_size, params.metadata_file_name_output,
                                &motion_photo, &motion_photo_reader,
                                &motion_photo_checker, outputter)) {
    return 1;
  }

  // Read and decode and check the MP4 track data and metadata.
  int64_t offset = motion_photo.GetMp4FileOffset(file_size);

  if (offset < file_size) {
    if (!ReadDecodeAndCheckVideoMetadata(params.motion_photo_file_name, offset,
                                         &motion_photo, &motion_photo_reader,
                                         &motion_photo_checker, outputter,
                                         &message_handler)) {
      return 0;
    }

    if (!params.video_file_name_output.empty()) {
      WriteToOutput(params.motion_photo_file_name,
                    params.video_file_name_output, offset, file_size);
      if (!MotionPhotoMp4FileBuilder::StripMetadataTrack(
              params.video_file_name_output, &message_handler)) {
        return 1;
      }
    }
  }

  if (!params.primary_image_file_name_output.empty()) {
    string image_metadata;
    bool read_ok = false;
    std::shared_ptr<image_io::DataSegment> xmp_segment =
        data_source->GetDataSegment(xmp_range.GetBegin(),
                                    xmp_range.GetLength());
    if (xmp_segment) {
      std::string original_xmp(
          reinterpret_cast<const char*>(
              xmp_segment->GetBuffer(xmp_range.GetBegin())),
          xmp_range.GetLength());
      image_metadata = StripMotionPhotoMetadata(original_xmp);
      read_ok = true;
    }

    if (!read_ok) {
      motion_photo.SetIsMotionPhoto(false);
      MotionPhotoWriter motion_photo_writer(motion_photo);
      motion_photo_writer.WriteImageMetadata(&image_metadata);
    }

    bool build_success = false;
    if (file_type == FileType::kJpeg) {
      MotionPhotoJpegImageBuilder image_builder(
          params.primary_image_file_name_output, &message_handler);
      build_success = image_builder.AddImageFileAndMetadata(params.motion_photo_file_name,
                                                            image_metadata);
    } else if (IsHeif(file_type)) {
      if (mpvd_box.IsValid() && mpvd_box.GetStartIndex() > 0) {
        WriteToOutput(params.motion_photo_file_name,
                      params.primary_image_file_name_output, 0,
                      mpvd_box.GetStartIndex());
        build_success = true;
      } else {
        MotionPhotoHeifImageBuilder image_builder(
            params.primary_image_file_name_output, &message_handler);
        build_success = image_builder.AddImageFileAndMetadata(params.motion_photo_file_name,
                                                              image_metadata);
      }
    }

    if (!build_success) {
      message_handler.ReportMessage(Message::kDecodingError,
                                    "Failed to write extracted still image");
      return 1;
    }
  }

  // Return with the proper code.
  return message_stats->error_count == 0 ? 0 : 1;
}

int MotionPhotoExtractorFunction(int argc, const char* argv[],
                                 const StringOutputter& outputter) {
  // Write the banner, get the message handler setup and parse the params.
  outputter(string(kExtractorName) + kExtractorVersion + "\n");
  Params params = GetParams(argc, argv, outputter);
  if (params.usage_given) {
    return 1;
  }

  MotionPhotoExtractorParams api_params;
  api_params.motion_photo_file_name = params.motion_photo_file_name;
  api_params.primary_image_file_name_output =
      params.primary_image_file_name_output;
  api_params.video_file_name_output = params.video_file_name_output;
  api_params.metadata_file_name_output = params.metadata_file_name_output;

  return ExtractMotionPhoto(api_params, outputter);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
