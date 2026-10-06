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

#ifndef MOTION_PHOTO_CONTAINER_SPLITTER_H_
#define MOTION_PHOTO_CONTAINER_SPLITTER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "motion_photo/motion_photo.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

class ContainerSplitter {
 public:
  virtual ~ContainerSplitter() = default;

  // Splits the image from data_source into raw metadata blocks.
  // file_size is the total size of the image data.
  virtual std::vector<RawMetadataBlock> Split(
      image_io::DataSource* data_source, size_t file_size) = 0;
};

// Returns a splitter for JPEG images.
std::unique_ptr<ContainerSplitter> CreateJpegContainerSplitter(
    image_io::MessageHandler* message_handler);

// Returns a splitter for HEIF-based images (HEIC and AVIF) that does not need
// libheif. It emits the primary image's XMP item as a "standard.xmp" block, and
// only when the file also has a top-level 'mpvd' (motion photo video) box.
std::unique_ptr<ContainerSplitter> CreateHeifContainerSplitter();

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_CONTAINER_SPLITTER_H_
