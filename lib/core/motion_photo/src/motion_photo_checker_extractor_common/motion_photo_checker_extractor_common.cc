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

#include "motion_photo_checker_extractor_common/motion_photo_checker_extractor_common.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "image_io/base/data_line_map.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/utils/file_utils.h"
#include "image_io/xmp/xmp_listing_builder.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_heif/motion_photo_heif_image_builder.h"
#include "motion_photo_heif/motion_photo_heif_info_builder.h"
#include "motion_photo_jpeg/motion_photo_jpeg_info_builder.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataLineMap;
using image_io::DataRange;
using image_io::DataSegment;
using image_io::DataSegmentDataSource;
using image_io::DataSource;
using image_io::Message;
using image_io::MessageHandler;
using image_io::ReadEntireFile;
using image_io::StringOutputter;
using image_io::XmpListingBuilder;
using std::make_tuple;
using std::string;
using std::stringstream;
using std::tuple;
using std::unique_ptr;

namespace {
// Simple template function to convert a value to a string.
template <class T>
string String(const T& value) {
  stringstream ss;
  ss << value;
  return ss.str();
}

//   track_id: The id of the track to return a representation of.
string Track(TrackId track_id) {
  return track_id == kInvalidTrackId ? "None" : String(track_id);
}
}  // namespace

bool WriteToOutput(const std::string& inputFilePath,
                   const std::string& outputFilePath,
                   std::streamoff beginOffset,
                   std::streamoff endOffset) {
  if (beginOffset < 0 || endOffset <= beginOffset) {
    std::cerr << "Error: Invalid range (end must be > begin and both >= 0)." << std::endl;
    return false;
  }

  std::ifstream inputFile(inputFilePath, std::ios::in | std::ios::binary);
  if (!inputFile.is_open()) {
    std::cerr << "Error: Could not open input file: " << inputFilePath << std::endl;
    return false;
  }

  std::ofstream outputFile(outputFilePath, std::ios::out | std::ios::binary);
  if (!outputFile.is_open()) {
    std::cerr << "Error: Could not open output file: " << outputFilePath << std::endl;
    return false;
  }

  if (!inputFile.seekg(beginOffset)) {
    std::cerr << "Error: Could not seek to begin offset in input file." << std::endl;
    return false;
  }

  constexpr size_t kBufferSize = 64 * 1024;
  alignas(64) char buffer[kBufferSize];
  std::streamoff bytesRemaining = endOffset - beginOffset;

  while (bytesRemaining > 0) {
    size_t readCount = static_cast<size_t>(std::min<std::streamoff>(kBufferSize, bytesRemaining));
    inputFile.read(buffer, readCount);
    std::streamoff bytesRead = inputFile.gcount();

    if (bytesRead > 0) {
      outputFile.write(buffer, bytesRead);
    }

    if (inputFile.fail() && !inputFile.eof()) {
      std::cerr << "Error during file read operation." << std::endl;
      return false;
    }

    if (bytesRead < readCount) {
      std::cerr << "Warning: Reached end of file before reaching the specified endOffset." << std::endl;
      break;
    }

    bytesRemaining -= bytesRead;
  }

  return bytesRemaining == 0;
}

tuple<unique_ptr<DataSource>, size_t> ReadFile(const string& file_name,
                                                const StringOutputter& outputter,
                                                MessageHandler* message_handler) {
  size_t file_size = 0;
  unique_ptr<DataSource> data_source;
  outputter("\nReading " + file_name + "\n");
  std::shared_ptr<DataSegment> data_segment =
      ReadEntireFile(file_name, message_handler);
  if (data_segment) {
    file_size = data_segment->GetLength();
    if (file_size == 0) {
      if (message_handler) {
        message_handler->ReportMessage(Message::kPrematureEndOfDataError,
                                       file_name + " is an empty file");
      }
    } else {
      data_source = std::make_unique<DataSegmentDataSource>(data_segment);
    }
  }
  if (data_source) {
    outputter("...Input file size: " + String(file_size) + "\n");
  }
  return make_tuple(std::move(data_source), file_size);
}

tuple<size_t, DataRange, MpvdBox> GetImageSizeAndXmpRange(
    DataSource* data_source, size_t data_source_length, FileType file_type,
    const std::string& motion_photo_file_name,
    const std::string& primary_image_file_name_output,
    const StringOutputter& outputter, MessageHandler* message_handler) {
  auto error_value = make_tuple(0, DataRange(), MpvdBox());
  outputter("\nReading XMP metadata\n");

  size_t image_size = 0;
  DataRange primary_xmp_range;
  MpvdBox mpvd_box;
  bool valid_mpvd_box = true;

  if (file_type == FileType::kJpeg) {
    MotionPhotoJpegInfoBuilder info_builder(message_handler);
    if (!info_builder.Build(data_source)) {
      if (message_handler && !message_handler->HasErrorMessages()) {
        message_handler->ReportMessage(Message::kValueError,
                                       "No XMP data found in input");
      }
      return error_value;
    }
    const auto& image_ranges = info_builder.GetInfo().GetImageRanges();
    image_size = image_ranges.empty() ? 0 : image_ranges.back().GetEnd();

    outputter("...Primary image size: " + String(image_size) + "\n");
    if (image_size == 0) {
      return error_value;
    }

    if (!primary_image_file_name_output.empty()) {
      WriteToOutput(motion_photo_file_name, primary_image_file_name_output,
                    image_ranges[0].GetBegin(), image_ranges.back().GetEnd());
    }

    primary_xmp_range = info_builder.GetPrimaryXmpStringRange();
    const auto& extended_ranges = info_builder.GetExtendedXmpStringRanges();
    if (!extended_ranges.empty()) {
      if (message_handler) {
        stringstream ss;
        ss << "Input file has " << extended_ranges.size()
           << " extended XMP segments that are being ignored";
        message_handler->ReportMessage(Message::kWarning, ss.str());
      }
    } else {
      outputter("...Metadata read ok\n");
    }
  } else if (IsHeif(file_type)) {
    MotionPhotoHeifInfoBuilder info_builder(message_handler);
    if (!info_builder.Build(data_source, data_source_length)) {
      return error_value;
    }
    MpvdBoxScanReceiver receiver(mpvd_box);
    MpvdBox::Scan(data_source, 0, receiver);
    primary_xmp_range = info_builder.GetXmpStringRange();
    image_size = mpvd_box.GetStartIndex();
    valid_mpvd_box = mpvd_box.IsValid();
    if (!valid_mpvd_box) {
      if (message_handler) {
        message_handler->ReportMessage(Message::kValueError,
                                       "No MpvdBox found in input");
      }
    } else if (!primary_image_file_name_output.empty()) {
      MotionPhotoHeifImageBuilder heif_builder(primary_image_file_name_output,
                                               message_handler);
      std::shared_ptr<DataSegment> xmp_segment = data_source->GetDataSegment(
          primary_xmp_range.GetBegin(), primary_xmp_range.GetLength());
      if (!xmp_segment) {
        message_handler->ReportMessage(Message::kValueError,
                                       "Failed to read XMP metadata segment.");
        return error_value;
      }
      std::string xmp_metadata(
          reinterpret_cast<const char*>(
              xmp_segment->GetBuffer(primary_xmp_range.GetBegin())),
          primary_xmp_range.GetLength());
      if (!heif_builder.AddImageFileAndMetadata(motion_photo_file_name,
                                                xmp_metadata)) {
        message_handler->ReportMessage(Message::kValueError,
                                       "Failure in writing HEIF image output.");
      }
    }
  }

  if (!primary_xmp_range.IsValid()) {
    if (message_handler) {
      message_handler->ReportMessage(Message::kValueError,
                                     "No XMP data found in input");
    }
  }
  if (!primary_xmp_range.IsValid() || !valid_mpvd_box) {
    return error_value;
  }
  return make_tuple(image_size, primary_xmp_range, mpvd_box);
}

bool ParseXmpMetadata(DataSource* data_source,
                      const string& motion_photo_file_name,
                      const DataRange& xmp_range,
                      const std::string& metadata_file_name_output,
                      MotionPhotoReader* motion_photo_reader,
                      const StringOutputter& outputter) {
  outputter("\nParsing XMP metadata\n");
  const size_t kMaxXmpLineLength = 100;
  DataLineMap data_line_map;
  stringstream ss;
  XmpListingBuilder listing_builder(ss, kMaxXmpLineLength);
  listing_builder.Build(data_source, {xmp_range}, &data_line_map);
  outputter(ss.str());
  size_t bytes_parsed = 0;
  if (!metadata_file_name_output.empty()) {
    WriteToOutput(motion_photo_file_name, metadata_file_name_output,
                  xmp_range.GetBegin(), xmp_range.GetEnd());
  }
  return motion_photo_reader->ReadImageMetadata({xmp_range}, data_source,
                                                &bytes_parsed);
}

bool ParseAndCheckXmpMetadata(DataSource* data_source, size_t file_size,
                              FileType file_type,
                              const string& motion_photo_file_name,
                              const DataRange& xmp_range, size_t image_size,
                              const std::string& metadata_file_name_output,
                              MotionPhoto* motion_photo,
                              MotionPhotoReader* motion_photo_reader,
                              MotionPhotoChecker* motion_photo_checker,
                              const StringOutputter& outputter) {
  if (!ParseXmpMetadata(data_source, motion_photo_file_name, xmp_range,
                        metadata_file_name_output, motion_photo_reader,
                        outputter)) {
    return false;
  }

  outputter("\nChecking XMP metadata\n");
  size_t error_count = motion_photo_checker->GetErrorCount();
  size_t warning_count = motion_photo_checker->GetWarningCount();
  outputter("...XMP metadata indicates a motion photo\n");
  motion_photo_checker->CheckFileName(motion_photo_file_name);
  motion_photo_checker->CheckCameraMetadata();
  motion_photo_checker->CheckContainerMetadata();
  if (file_type == FileType::kJpeg) {
    motion_photo_checker->CheckContainerMetadataAndSizes(image_size, file_size);
  }
  if (motion_photo_checker->GetErrorCount() == error_count &&
      motion_photo_checker->GetWarningCount() == warning_count) {
    outputter("...Metadata looks great!\n");
  }

  if (IsHeif(file_type)) {
    error_count = motion_photo_checker->GetErrorCount();
    warning_count = motion_photo_checker->GetWarningCount();
    outputter("\nChecking Mpvd box\n");
    motion_photo_checker->CheckMpvdBox(file_size);
    if (motion_photo_checker->GetErrorCount() == error_count &&
        motion_photo_checker->GetWarningCount() == warning_count) {
      std::stringstream ss;
      const MpvdBox& box = motion_photo->GetMpvdBox();
      ss << "...Start index:   " << box.GetStartIndex() << std::endl
         << "...Header length: " << box.GetHeaderLength() << std::endl
         << "...Video length:  " << box.GetVideoLength() << std::endl;
      outputter(ss.str());
      outputter("...Mpvd box looks great!\n");
    }
  }

  return motion_photo_checker->GetErrorCount() == error_count;
}

bool ReadDecodeAndCheckVideoMetadata(const std::string& motion_photo_file_name,
                                     int64_t mp4_offset,
                                     MotionPhoto* motion_photo,
                                     MotionPhotoReader* motion_photo_reader,
                                     MotionPhotoChecker* motion_photo_checker,
                                     const StringOutputter& outputter,
                                     MessageHandler* message_handler) {
  outputter("\nReading MP4 data\n");
  MotionPhotoMp4FileReader mp4_reader;
  if (!mp4_reader.Read(motion_photo_file_name, mp4_offset)) {
    if (message_handler) {
      message_handler->ReportMessage(
          Message(Message::kStdLibError, EIO, motion_photo_file_name));
    }
    return false;
  }
  std::vector<uint8_t> video_metadata;
  const auto& track_data = mp4_reader.GetVideoTrackData();
  outputter("...Low res video track id: " +
            Track(track_data.low_res_video_track_id) + "\n");
  outputter("...Low res audio track id: " +
            Track(track_data.low_res_audio_track_id) + "\n");
  outputter("...High res video track id: " +
            Track(track_data.high_res_video_track_id) + "\n");
  outputter("...High res metadata track id: " +
            Track(track_data.high_res_metadata_track_id) + "\n");
  if (track_data.high_res_metadata_track_id != kInvalidTrackId) {
    video_metadata = mp4_reader.GetHighResTrackVideoMetadata();
    size_t size = video_metadata.size();
    outputter("...High res metadata size: " + String(size) + "\n");
  }
  motion_photo_checker->CheckVideoTrackData(track_data);

  if (track_data.high_res_metadata_track_id != kInvalidTrackId &&
      !video_metadata.empty()) {
    outputter("\nDecoding and checking high res metadata\n");
    motion_photo_reader->SetVideoDecoderVerbose(true);
    const auto& decoder = motion_photo_reader->GetVideoDecoder();
    if (!motion_photo_reader->DecodeVideoMetadata(video_metadata)) {
      if (message_handler) {
        stringstream ss;
        ss << "High res metadata could not be decoded"
           << "\n- Read past buffer count: " << decoder.GetReadPastEndCount()
           << "\n- Unexpected object count: " << decoder.GetUnexpectedTagCount()
           << "\n- Incomplete object decoding count: "
           << decoder.GetIncompleteDecodingCount();
        for (const auto& message : decoder.GetVerboseMessages()) {
          ss << std::endl << "- " << message;
        }
        message_handler->ReportMessage(Message::kDecodingError, ss.str());
      }
      return false;
    }
    if (motion_photo_checker->CheckVideoMetadata()) {
      outputter("...High res metadata looks great!\n");
    }
  }

  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
