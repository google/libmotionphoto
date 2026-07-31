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

#ifndef MOTION_PHOTO_METADATA_ENGINE_H_
#include <cstdint>
#define MOTION_PHOTO_METADATA_ENGINE_H_

#include <memory>
#include <string>
#include <vector>

#include "metadata_collection.pb.h"
#include "motion_photo/motion_photo.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

class IMetadataProvider;

struct HandlerOptions {
  bool disable_3p_plugins = false;
  std::vector<std::string> enabled_3p_plugins;
};

class MetadataEngine {
 public:
  MetadataEngine(image_io::MessageHandler* message_handler);
  ~MetadataEngine();

  // Register a provider. Engine takes ownership.
  void RegisterProvider(std::unique_ptr<IMetadataProvider> provider);

  // Parses metadata from the data source.
  MetadataCollection Parse(image_io::DataSource* data_source, size_t file_size,
                           FileType file_type);

  // Extracts AGTM metadata from the video payload at the target presentation
  // timestamp in microseconds.
  std::string ExtractAgtmAtTimestamp(const std::string& file_path,
                                     int64_t timestamp_us);

  std::string ExtractAgtmAtTimestampMemory(const uint8_t* buffer, size_t size,
                                           int64_t timestamp_us);

  // Extracts motion photo related metadata from input_file_path and writes it
  // to output_file_path.
  bool ExtractMetadata(const std::string& input_file_path,
                       const std::string& output_file_path,
                       const HandlerOptions& options = {});

  // Helper to decide if the collection represents a motion photo.
  static bool IsMotionPhoto(const MetadataCollection& collection,
                            const HandlerOptions& options = {});

 private:
  image_io::MessageHandler* message_handler_;
  std::vector<std::unique_ptr<IMetadataProvider>> providers_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_ENGINE_H_
