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

#include "motion_photo_metadata_engine/function.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_checker_extractor_common/motion_photo_checker_extractor_common.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataSource;
using image_io::MessageHandler;
using image_io::StringOutputter;
using image_io::StringOutputterMessageWriter;
using std::string;
using std::stringstream;
using std::unique_ptr;

namespace {

const char kToolName[] = "Motion Photo Metadata Engine CLI";
const char kToolVersion[] = " V0.1";

void PrintUsage(std::string_view arg0, const StringOutputter& outputter) {
  string program_name(arg0);
  size_t last_slash = program_name.rfind('/');
  if (last_slash != string::npos) {
    program_name = program_name.substr(last_slash + 1);
  }
  if (program_name.empty()) {
    program_name = "motion_photo_metadata_engine";
  }

  stringstream ostr;
  ostr << "Usage: " << program_name << " [options] <motion_photo_file_name>\n"
       << "where options are:\n"
       << " -h             print this help message\n"
       << " -agtm          extract AGTM metadata at timestamp (default: 0 us)\n"
       << " -ts            timestamp in microseconds for AGTM extraction\n"
       << " -json          output full metadata details JSON\n";
  outputter(ostr.str());
}

}  // namespace

int RunMetadataEngine(const MotionPhotoMetadataEngineParams& params,
                      const StringOutputter& outputter) {
  MessageHandler message_handler;
  unique_ptr<image_io::MessageWriter> message_writer(
      new StringOutputterMessageWriter(outputter));
  message_handler.SetMessageWriter(std::move(message_writer));

  MetadataEngine engine(&message_handler);

  if (params.extract_agtm) {
    string agtm_json = engine.ExtractAgtmAtTimestamp(
        params.motion_photo_file_name, params.timestamp_us);
    outputter(agtm_json + "\n");
    return 0;
  }

  FileType file_type = GetFileTypeFromFileName(params.motion_photo_file_name);
  size_t file_size = 0;
  unique_ptr<DataSource> data_source_owner;
  std::tie(data_source_owner, file_size) =
      ReadFile(params.motion_photo_file_name, outputter, &message_handler);
  DataSource* data_source = data_source_owner.get();

  if (data_source == nullptr || file_size == 0) {
    outputter("Error: Failed to read file " + params.motion_photo_file_name +
              "\n");
    return 1;
  }

  MetadataCollection collection =
      engine.Parse(data_source, file_size, file_type);

  bool is_mp = engine.IsMotionPhoto(collection);
  outputter("Is Motion Photo: " + string(is_mp ? "true" : "false") + "\n");
  outputter("Parsed Metadata Blocks: " +
            std::to_string(collection.blocks_size()) + "\n");

  return 0;
}

int MotionPhotoMetadataEngineFunction(int argc, const char* argv[],
                                      const StringOutputter& outputter) {
  outputter(string(kToolName) + kToolVersion + "\n");
  if (argc < 2) {
    PrintUsage(argv[0], outputter);
    return 1;
  }

  MotionPhotoMetadataEngineParams params;
  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0], outputter);
      return 0;
    } else if (arg == "-agtm") {
      params.extract_agtm = true;
    } else if (arg == "-json") {
      params.json_details = true;
    } else if ((arg == "-ts" || arg == "-timestamp_us") && i + 1 < argc) {
      params.timestamp_us = std::stoll(argv[++i]);
    } else if (arg[0] != '-') {
      params.motion_photo_file_name = arg;
    }
  }

  if (params.motion_photo_file_name.empty()) {
    outputter("Error: Motion photo file name is required.\n");
    PrintUsage(argv[0], outputter);
    return 1;
  }

  return RunMetadataEngine(params, outputter);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
