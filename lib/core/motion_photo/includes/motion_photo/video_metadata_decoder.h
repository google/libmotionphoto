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

#ifndef MOTION_PHOTO_VIDEO_METADATA_DECODER_H_  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_VIDEO_METADATA_DECODER_H_  // NOLINT

#include <vector>

#include "motion_photo/video_metadata.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A decoder for a motion photos' metadata track bytes.
class VideoMetadataDecoder {
 public:
  // Returns the (decoded or default) video metadata.
  const MetadataDescriptor& GetMetadata() const { return metadata_; }

  //   bytes: The array of bytes containing the encoded image metadata.
  //   begin: The index in bytes where to begin decoding.
  //   end: The index in bytes where to end decoding.
  // Returns whether the bytes were decoded successfully.
  bool DecodeMetadata(const uint8_t* bytes, size_t begin, size_t end);

  //   bytes: The vector the entire contents of which are assumed to
  // contain the encoded image metadata.
  // Returns whether the bytes were decoded successfully.
  bool DecodeMetadata(const std::vector<uint8_t>& bytes) {
    return DecodeMetadata(bytes.data(), 0, bytes.size());
  }

  // Sets verbose mode on the ISO decoder.
  void SetVerbose(bool verbose) { iso_decoder_.SetVerbose(verbose); }

  // Returns the decoder that is used by the DecodeMetadata() function.
  // This can be used to access extra info about the decoding.
  const image_io::IsoDecoder& GetIsoDecoder() const { return iso_decoder_; }

 private:
  image_io::IsoDecoder iso_decoder_;
  MetadataDescriptor metadata_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_VIDEO_METADATA_DECODER_H_  // NOLINT
