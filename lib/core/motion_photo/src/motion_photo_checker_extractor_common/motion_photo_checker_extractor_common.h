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

#ifndef MOTION_PHOTO_COMMON_MOTION_PHOTO_CHECKER_EXTRACTOR_COMMON_H_
#define MOTION_PHOTO_COMMON_MOTION_PHOTO_CHECKER_EXTRACTOR_COMMON_H_

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/utils/string_outputter.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_checker.h"
#include "motion_photo/motion_photo_reader.h"

namespace libmotionphoto {
namespace motion_photo {

// Writes a byte range from an input file to an output file.
bool WriteToOutput(const std::string& inputFilePath,
                   const std::string& outputFilePath,
                   std::streamoff beginOffset,
                   std::streamoff endOffset);

// Reads a file into a data source.
std::tuple<std::unique_ptr<image_io::DataSource>, size_t> ReadFile(
    const std::string& file_name,
    const image_io::StringOutputter& outputter,
    image_io::MessageHandler* message_handler);

// Gets the primary image size and XMP range.
// Optionally writes the primary image to primary_image_file_name_output.
std::tuple<size_t, image_io::DataRange, MpvdBox> GetImageSizeAndXmpRange(
    image_io::DataSource* data_source, size_t data_source_length,
    FileType file_type, const std::string& motion_photo_file_name,
    const std::string& primary_image_file_name_output,
    const image_io::StringOutputter& outputter,
    image_io::MessageHandler* message_handler);

// Parses the XMP metadata without running MotionPhotoChecker validations.
// Optionally writes the metadata to metadata_file_name_output.
bool ParseXmpMetadata(
    image_io::DataSource* data_source,
    const std::string& motion_photo_file_name,
    const image_io::DataRange& xmp_range,
    const std::string& metadata_file_name_output,
    MotionPhotoReader* motion_photo_reader,
    const image_io::StringOutputter& outputter);

// Parses and checks the XMP metadata.
// Optionally writes the metadata to metadata_file_name_output.
bool ParseAndCheckXmpMetadata(
    image_io::DataSource* data_source, size_t file_size, FileType file_type,
    const std::string& motion_photo_file_name,
    const image_io::DataRange& xmp_range, size_t image_size,
    const std::string& metadata_file_name_output, MotionPhoto* motion_photo,
    MotionPhotoReader* motion_photo_reader,
    MotionPhotoChecker* motion_photo_checker,
    const image_io::StringOutputter& outputter);

// Reads, decodes, and checks the video metadata from the MP4 part (file-based).
bool ReadDecodeAndCheckVideoMetadata(
    const std::string& motion_photo_file_name, int64_t mp4_offset,
    MotionPhoto* motion_photo, MotionPhotoReader* motion_photo_reader,
    MotionPhotoChecker* motion_photo_checker,
    const image_io::StringOutputter& outputter,
    image_io::MessageHandler* message_handler);

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_COMMON_MOTION_PHOTO_CHECKER_EXTRACTOR_COMMON_H_
