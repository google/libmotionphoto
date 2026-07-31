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

#include "motion_photo_checker/function.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "image_io/base/message_handler.h"
#include "image_io/base/message_writer.h"
#include "image_io/utils/message_stats_writer.h"
#include "image_io/utils/string_outputter_message_writer.h"
#include "motion_photo/motion_photo_checker.h"
#include "motion_photo/motion_photo_reader.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_checker_extractor_common/motion_photo_checker_extractor_common.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataRange;
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

const char kCheckerName[] = "Motion Photo Checker";
const char kCheckerVersion[] = " V0.3";

// The help usage string.
const char kUsage[] = R"([options] motion_photo_file_name
Where options are:
  -h  Show this help message and exit.
)";

// The fields in this structure are initialized by looking at the argv values
// passed to the MotionPhotoChecker() function.
struct Params {
  bool usage_given = false;
  string motion_photo_file_name;
};

// When there are no args specified, or there is an unrecognized option in the
// argv values this function is called to print the usage.
//   arg0: The argv[0] value that contains the executable path.
//   outputter: The outputter function to send the usage to.
// Returns a Params instance with the usage_given field set to true.
Params PrintUsage(string arg0, const StringOutputter& outputter) {
  size_t last_slash = arg0.rfind('/');
  if (last_slash != string::npos) {
    arg0 = arg0.substr(last_slash + 1);
  }
  if (arg0.empty()) {
    arg0 = "motion_photo_checker";
  }
  stringstream ostr;
  ostr << "Usage: " << arg0 << " " << kUsage;
  outputter(ostr.str());
  Params params;
  params.usage_given = true;
  return params;
}

//   argc: The number of strings in the argv array.
//   argv: The options and values used in the command line.
//   outputter: The outputter function to send the usage to.
// Returns a Params structure with the field values set according to the values
//     in the argv array.
Params GetParams(int argc, const char* argv[],
                 const StringOutputter& outputter) {
  Params params;
  std::string arg0 = argv[0];
  if (argc < 2) {
    return PrintUsage(arg0, outputter);
  }
  for (int index = 1; index < argc; ++index) {
    const string arg = argv[index];
    if (arg[0] == '-') {
      if (arg == "-h") {
        return PrintUsage(arg0, outputter);
      } else {
        return PrintUsage(arg0, outputter);
      }
    } else {
      if (params.motion_photo_file_name.empty()) {
        params.motion_photo_file_name = arg;
      } else {
        return PrintUsage(arg0, outputter);
      }
    }
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

}  // namespace

int CheckMotionPhoto(const string& motion_photo_file_name,
                     const StringOutputter& outputter) {
  MessageHandler message_handler;
  unique_ptr<image_io::MessageWriter> message_writer(
      new StringOutputterMessageWriter(outputter));
  message_handler.SetMessageWriter(std::move(message_writer));
  auto message_stats = message_handler.GetMessageStats();
  MessageStatsWriter stats_writer(message_stats, kCheckerName, outputter);

  // Check the file name (which can produce only warnings, no errors).
  // Read the file into a data source for later.
  size_t file_size = 0;
  FileType file_type = GetFileTypeFromFileName(motion_photo_file_name);
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
      ReadFile(motion_photo_file_name, outputter, &message_handler);
  if (data_source == nullptr || file_size == 0) {
    return 1;
  }

  // Get the primary image size and the location in the data source of the
  // primary XMP segment string.
  size_t image_size = 0;
  DataRange xmp_range;
  MpvdBox mpvd_box;
  tie(image_size, xmp_range, mpvd_box) = GetImageSizeAndXmpRange(
      data_source.get(), file_size, file_type, motion_photo_file_name, "", outputter,
      &message_handler);
  if ((image_size == 0) || !xmp_range.IsValid()) {
    return 1;
  }
  motion_photo.SetMpvdBox(mpvd_box);

  // Parse the XMP metadata string from the data source and check the values.
  if (!ParseAndCheckXmpMetadata(data_source.get(), file_size, file_type,
                                motion_photo_file_name, xmp_range, image_size,
                                "", &motion_photo, &motion_photo_reader,
                                &motion_photo_checker, outputter)) {
    return 1;
  }

  // Read and decode and check the MP4 track data and metadata.
  int64_t offset = motion_photo.GetMp4FileOffset(file_size);

  if (offset < file_size) {
    if (!ReadDecodeAndCheckVideoMetadata(
            motion_photo_file_name, offset, &motion_photo, &motion_photo_reader,
            &motion_photo_checker, outputter, &message_handler)) {
      return 0;
    }
  }

  // Return with the proper code.
  return message_stats->error_count == 0 ? 0 : 1;
}

int MotionPhotoCheckerFunction(int argc, const char* argv[],
                               const StringOutputter& outputter) {
  // Write the banner, get the message handler setup and parse the params.
  outputter(string(kCheckerName) + kCheckerVersion + "\n");
  Params params = GetParams(argc, argv, outputter);
  if (params.usage_given) {
    return 1;
  }

  return CheckMotionPhoto(params.motion_photo_file_name, outputter);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
