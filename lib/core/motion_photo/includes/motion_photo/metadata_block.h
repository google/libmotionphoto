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

#ifndef MOTION_PHOTO_METADATA_BLOCK_H_
#define MOTION_PHOTO_METADATA_BLOCK_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

struct RawMetadataBlock {
  std::string type;  // "EXIF", "XMP", "ICC", "PROPRIETARY", etc.
  std::string format_identifier;
  std::vector<uint8_t> bytes;
  // Optional zero-copy view over a pinned DataSegment buffer (avoids copying
  // into `bytes` when `bytes` is empty).
  const uint8_t* data_view = nullptr;
  size_t data_view_size = 0;
  size_t offset = 0;
  // If non-zero, represents the full size of the underlying payload block
  // in cases where `bytes` only buffers a prefix or `data_source` provides
  // lazy random-access reads (e.g. for container trailers).
  size_t total_payload_size = 0;
  // Optional backing DataSource for zero-copy / lazy random-access reads
  // without copying the entire payload into `bytes`.
  image_io::DataSource* data_source = nullptr;
  mutable std::vector<std::shared_ptr<image_io::DataSegment>> pinned_segments;

  // Returns a pointer to the buffered bytes (from `bytes` if non-empty, or
  // `data_view` if set).
  const uint8_t* Data() const {
    return !bytes.empty() ? bytes.data() : data_view;
  }

  // Returns the number of bytes currently buffered in memory (`bytes.size()`
  // or `data_view_size`).
  size_t BufferedSize() const {
    return !bytes.empty() ? bytes.size() : data_view_size;
  }

  // Returns the logical size of the block (`total_payload_size` if larger than
  // `BufferedSize()`, otherwise `BufferedSize()`).
  size_t Size() const {
    const size_t buf_size = BufferedSize();
    return total_payload_size > buf_size ? total_payload_size : buf_size;
  }

  // Obtains a pointer to `len` contiguous bytes starting at `rel_offset`
  // relative to the start of this block.
  // - If the requested range lies within the already-buffered prefix
  //   (`Data()[0 .. BufferedSize())`), sets `*out_ptr` directly with zero
  //   copy and zero allocation.
  // - Otherwise, if `data_source` is available, fetches the DataSegment
  //   covering `[offset + rel_offset, offset + rel_offset + len)`. If a single
  //   DataSegment covers the entire range, pins the segment in
  //   `pinned_segments` and sets `*out_ptr` directly into the segment buffer
  //   (zero copy). If the range spans multiple segments and `scratch` is
  //   non-null, copies the requested slice into `*scratch`.
  bool ReadView(size_t rel_offset, size_t len, const uint8_t** out_ptr,
                std::vector<uint8_t>* scratch = nullptr) const;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_BLOCK_H_
