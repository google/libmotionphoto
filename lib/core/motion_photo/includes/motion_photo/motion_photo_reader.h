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

#ifndef MOTION_PHOTO_MOTION_PHOTO_READER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MOTION_PHOTO_READER_H_  // NOLINT

#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/video_metadata_decoder.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A class to initialize a motion photo by reading/parsing the XMP/XML syntax
// typically found in the JPEG portion of a motion photo file, and by decoding
// the bytes of the sample data from the meta track found in MP4 potion of the
// file.
class MotionPhotoReader {
 public:
  //   motion_photo: The MotionPhoto to receive the image and/or video
  // metadata that is read/decoded using this reader.
  MotionPhotoReader(MotionPhoto* motion_photo,
                    image_io::MessageHandler* message_handler)
      : motion_photo_(motion_photo), message_handler_(message_handler) {}

  //   xmp_string: A string_view holding the XMP/XML characters from which the
  // MotionPhoto's image metadata is read.
  //   bytes_parsed: A variable to receive the number of bytes that were
  // parsed from the string.
  // Returns whether the string was parsed successfully.
  bool ReadImageMetadata(std::string_view xmp_string, size_t* bytes_parsed);

  //   ranges: One or more ranges in the data source that holds the bytes
  // that represent the XMP/XMP characters from which the MotionPhoto's image
  // metadata is read.
  //   source: The data source that holds the bytes.
  //   bytes_parsed: A variable to receive the number of bytes that were
  // parsed from the data source.
  // Returns whether the bytes in the data source were parsed successfully.
  bool ReadImageMetadata(const std::vector<image_io::DataRange>& ranges,
                         image_io::DataSource* source, size_t* bytes_parsed);

  // This function scans the motion photo's data source for the MpvdBox.
  // start and length of the MpvdBox in the data source.
  //   source_length: The length of the data source.
  // Returns whether the mpvd box was decoded successfully.
  bool FindAndDecodeMpvdBox(image_io::DataSource* source);

  //   bytes: The array of bytes containing the encoded video metadata.
  //   begin: The index in bytes where to begin decoding.
  //   end: The index in bytes where to end decoding.
  // Returns whether the bytes were decoded successfully.
  bool DecodeVideoMetadata(const uint8_t* bytes, size_t begin,
                           size_t end);

  //   bytes: The vector the entire contents of which are assumed to
  // contain the encoded video metadata.
  // Returns whether the bytes were decoded successfully.
  bool DecodeVideoMetadata(const std::vector<uint8_t>& bytes) {
    return DecodeVideoMetadata(bytes.data(), 0, bytes.size());
  }

  //   xmp_string: A string_view holding the XMP/XML characters from which the
  // MotionPhoto's builder metadata is read.
  bool ReadVideoMetadata(std::string_view xmp_string, size_t* bytes_parsed);

  // Sets verbose mode on the video decoder.
  void SetVideoDecoderVerbose(bool verbose) {
    video_metadata_decoder_.SetVerbose(verbose);
  }

  // Returns the decoder that is used by the DecodeVideoMetadata() function.
  // This can be used to access extra info about the decoding.
  const image_io::IsoDecoder& GetVideoDecoder() const {
    return video_metadata_decoder_.GetIsoDecoder();
  }

 private:
  MotionPhoto* motion_photo_;
  image_io::MessageHandler* message_handler_;
  VideoMetadataDecoder video_metadata_decoder_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MOTION_PHOTO_READER_H_  // NOLINT
