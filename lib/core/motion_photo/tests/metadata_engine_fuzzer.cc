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

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "motion_photo/google_motion_photo_provider.h"
#include "motion_photo/metadata_engine.h"

namespace libmotionphoto {
namespace motion_photo {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return 0;
  }

  // Create MetadataEngine with default providers.
  MetadataEngine engine(/*message_handler=*/nullptr);
  engine.RegisterProvider(std::make_unique<GoogleMotionPhotoProvider>());

  // Construct data source from fuzzer input payload.
  std::string input_buffer(reinterpret_cast<const char*>(data), size);
  // Parse for JPEG, HEIC, and AVIF format paths.
  MetadataCollection collection_jpeg =
      engine.Parse(/*data_source=*/nullptr, size, FileType::kJpeg);
  MetadataCollection collection_heic =
      engine.Parse(/*data_source=*/nullptr, size, FileType::kHeic);
  MetadataCollection collection_avif =
      engine.Parse(/*data_source=*/nullptr, size, FileType::kAvif);

  // Exercise validation and classification logic.
  HandlerOptions options;
  MetadataEngine::IsMotionPhoto(collection_jpeg, options);
  MetadataEngine::IsMotionPhoto(collection_heic, options);
  MetadataEngine::IsMotionPhoto(collection_avif, options);

  return 0;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
