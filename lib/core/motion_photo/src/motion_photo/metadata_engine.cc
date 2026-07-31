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

#include "motion_photo/metadata_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "image_io/utils/file_utils.h"
#include "motion_photo/agtm.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/google_motion_photo_provider.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_provider.h"
#include "motion_photo/metadata_provider_registry.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_metadata.pb.h"
#include "motion_photo_mp4/mp4_box_reader.h"

namespace libmotionphoto {
namespace motion_photo {

MetadataEngine::MetadataEngine(image_io::MessageHandler* message_handler)
    : message_handler_(message_handler) {
  RegisterProvider(std::make_unique<GoogleMotionPhotoProvider>());
}

MetadataEngine::~MetadataEngine() = default;

void MetadataEngine::RegisterProvider(
    std::unique_ptr<IMetadataProvider> provider) {
  providers_.push_back(std::move(provider));
}

MetadataCollection MetadataEngine::Parse(image_io::DataSource* data_source,
                                         size_t file_size, FileType file_type) {
  MetadataCollection collection;

  auto splitter = CreateContainerSplitter(file_type, message_handler_);
  if (!splitter) {
    return collection;
  }

  std::vector<RawMetadataBlock> raw_blocks =
      splitter->Split(data_source, file_size);

  for (const auto& raw_block : raw_blocks) {
    MetadataBlock* block = collection.add_blocks();

    block->set_format_identifier(raw_block.format_identifier);
    block->set_raw_bytes(raw_block.bytes.data(), raw_block.bytes.size());
    block->set_offset(raw_block.offset);

    BlockType block_type = BLOCK_TYPE_UNKNOWN;
    bool decoded = false;
    std::vector<IMetadataProvider*> all_providers;
    auto global_3p = MetadataProviderRegistry::Get3pProviders();
    all_providers.reserve(providers_.size() + global_3p.size());
    for (const auto& p : providers_) {
      all_providers.push_back(p.get());
    }
    for (const auto& p : global_3p) {
      all_providers.push_back(p.get());
    }

    for (const auto& provider : all_providers) {
      if (provider->Identify(raw_block)) {
        if (raw_block.type == "EXIF") {
          block_type = BLOCK_TYPE_EXIF;
        } else if (raw_block.type == "XMP") {
          block_type = BLOCK_TYPE_XMP;
        } else if (raw_block.type == "ICC") {
          block_type = BLOCK_TYPE_ICC;
        } else if (raw_block.type == "JFIF") {
          block_type = BLOCK_TYPE_JFIF;
        } else {
          block_type = BLOCK_TYPE_PROPRIETARY;
        }

        std::vector<uint8_t> proto_bytes;
        std::string type_url;
        std::string error_message;
        if (provider->DecodeToSemantic(raw_block, &proto_bytes, &type_url,
                                       &error_message)) {
          block->set_payload_type_url(type_url);
          block->set_decoded_payload_bytes(proto_bytes.data(), proto_bytes.size());
          decoded = true;
        } else {
          if (message_handler_ && !error_message.empty()) {
            message_handler_->ReportMessage(
                image_io::Message::kWarning,
                "Provider failed to decode block: " + error_message);
          }
        }

        size_t video_offset_in_block = 0;
        size_t video_length = 0;
        if (provider->GetVideoInfo(raw_block, &video_offset_in_block, &video_length)) {
          block->set_video_offset_in_block(video_offset_in_block);
          block->set_video_length(video_length);
        }

        break;
      }
    }
    block->set_type(block_type);
    if (!decoded) {
      // Keep only raw bytes
    }
  }

  return collection;
}

bool MetadataEngine::IsMotionPhoto(const MetadataCollection& collection,
                                   const HandlerOptions& options) {
  for (const auto& block : collection.blocks()) {
    if (block.has_payload_type_url() && block.has_decoded_payload_bytes()) {
      if (block.payload_type_url() == "type.googleapis.com/libmotionphoto.motion_photo.MotionPhotoMetadata") {
        MotionPhotoMetadata metadata;
        if (metadata.ParseFromString(block.decoded_payload_bytes())) {
          if (metadata.type() == MotionPhotoMetadata::MOTION_PHOTO_TYPE_MOTION_PHOTO) {
            return true;
          }
        }
      } else if (!options.disable_3p_plugins) {
        for (const auto& provider : MetadataProviderRegistry::Get3pProviders()) {
          if (provider->IsMotionPhoto(block.payload_type_url(), block.decoded_payload_bytes())) {
            std::string name = provider->GetPluginName();
            if (options.enabled_3p_plugins.empty()) {
              return true;
            }
            auto it = std::find(options.enabled_3p_plugins.begin(),
                                options.enabled_3p_plugins.end(), name);
            if (it != options.enabled_3p_plugins.end()) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

std::string MetadataEngine::ExtractAgtmAtTimestamp(const std::string& file_path,
                                                   int64_t timestamp_us) {
  Mp4BoxReader reader;
  if (!reader.ParseFromFile(file_path, 0)) {
    return "";
  }

  AgtmDynamicMetadata metadata;
  size_t track_count = reader.GetTrackCount();

  // 1. Look specifically for confirmed AGTM timed metadata tracks (it35 / T.35)
  for (size_t i = 0; i < track_count; ++i) {
    const auto& track = reader.GetTrack(i);
    if (Mp4BoxReader::TrackIsAgtmMetadata(track)) {
      uint32_t sample_id =
          reader.GetSampleIdFromTimeUs(track.track_id, timestamp_us);
      if (sample_id == 0) {
        sample_id = 1;
      }
      std::vector<uint8_t> sample_bytes;
      if (reader.ReadSample(track.track_id, sample_id, &sample_bytes)) {
        if (ParseAgtmPayload(sample_bytes.data(), sample_bytes.size(),
                             &metadata)) {
          return AgtmToJson(metadata);
        }
      }
      // If sample payload was empty or raw baseline without custom tone curve,
      // return confirmed default ST 2094-50 baseline metadata.
      metadata.hdr_reference_white = 203.0f;
      metadata.has_adaptive_tone_map_flag = false;
      metadata.baseline_hdr_headroom_log2 = 0.0f;
      metadata.use_reference_white_tone_mapping_flag = false;
      metadata.rules.clear();
      return AgtmToJson(metadata);
    }
  }

  // 2. Check other non-video, non-audio tracks for explicit T.35 payloads
  for (size_t i = 0; i < track_count; ++i) {
    const auto& track = reader.GetTrack(i);
    if (track.sample_count > 0 && track.handler_type != "vide" &&
        track.handler_type != "soun") {
      uint32_t sample_id =
          reader.GetSampleIdFromTimeUs(track.track_id, timestamp_us);
      if (sample_id == 0) {
        sample_id = 1;
      }
      std::vector<uint8_t> sample_bytes;
      if (reader.ReadSample(track.track_id, sample_id, &sample_bytes)) {
        if (ParseAgtmPayload(sample_bytes.data(), sample_bytes.size(),
                             &metadata)) {
          return AgtmToJson(metadata);
        }
      }
    }
  }

  return "";
}

std::string MetadataEngine::ExtractAgtmAtTimestampMemory(
    const uint8_t* buffer, size_t size, int64_t timestamp_us) {
  if (buffer == nullptr || size == 0) {
    return "";
  }

  // 1. Try parsing full MP4 boxes from memory first
  Mp4BoxReader reader;
  if (reader.ParseFromMemory(buffer, size, 0)) {
    AgtmDynamicMetadata metadata;
    size_t track_count = reader.GetTrackCount();

    // 1a. Look specifically for confirmed AGTM timed metadata tracks (it35 / T.35)
    for (size_t i = 0; i < track_count; ++i) {
      const auto& track = reader.GetTrack(i);
      if (Mp4BoxReader::TrackIsAgtmMetadata(track)) {
        uint32_t sample_id =
            reader.GetSampleIdFromTimeUs(track.track_id, timestamp_us);
        if (sample_id == 0) {
          sample_id = 1;
        }
        std::vector<uint8_t> sample_bytes;
        if (reader.ReadSample(track.track_id, sample_id, &sample_bytes)) {
          if (ParseAgtmPayload(sample_bytes.data(), sample_bytes.size(),
                               &metadata)) {
            return AgtmToJson(metadata);
          }
        }
        metadata.hdr_reference_white = 203.0f;
        metadata.has_adaptive_tone_map_flag = false;
        metadata.baseline_hdr_headroom_log2 = 0.0f;
        metadata.use_reference_white_tone_mapping_flag = false;
        metadata.rules.clear();
        return AgtmToJson(metadata);
      }
    }

    // 1b. Check other non-video, non-audio tracks for explicit T.35 payloads
    for (size_t i = 0; i < track_count; ++i) {
      const auto& track = reader.GetTrack(i);
      if (track.sample_count > 0 && track.handler_type != "vide" &&
          track.handler_type != "soun") {
        uint32_t sample_id =
            reader.GetSampleIdFromTimeUs(track.track_id, timestamp_us);
        if (sample_id == 0) {
          sample_id = 1;
        }
        std::vector<uint8_t> sample_bytes;
        if (reader.ReadSample(track.track_id, sample_id, &sample_bytes)) {
          if (ParseAgtmPayload(sample_bytes.data(), sample_bytes.size(),
                               &metadata)) {
            return AgtmToJson(metadata);
          }
        }
      }
    }
  }

  // 2. Fallback to raw payload ITU-T T.35 header scanner (0xB5 0x00 0x90)
  AgtmDynamicMetadata metadata;
  for (size_t i = 0; i <= size && 5 <= size - i; ++i) {
    if (buffer[i] == 0xB5 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x90 &&
        buffer[i + 3] == 0x00 && (buffer[i + 4] == 0x01 || buffer[i + 4] == 0x05)) {
      const uint8_t* payload_start = buffer + i;
      size_t payload_len = size - i;
      if (ParseAgtmPayload(payload_start, payload_len, &metadata)) {
        return AgtmToJson(metadata);
      }
    }
  }

  return "";
}

bool MetadataEngine::ExtractMetadata(const std::string& input_file_path,
                                     const std::string& output_file_path,
                                     const HandlerOptions& options) {
  if (input_file_path.empty() || output_file_path.empty()) {
    return false;
  }

  FileType file_type = GetFileTypeFromFileName(input_file_path);
  std::shared_ptr<image_io::DataSegment> data_segment =
      image_io::ReadEntireFile(input_file_path, message_handler_);
  if (!data_segment || data_segment->GetLength() == 0) {
    return false;
  }

  image_io::DataSegmentDataSource data_source(data_segment);
  MetadataCollection collection =
      Parse(&data_source, data_segment->GetLength(), file_type);

  auto is_plugin_enabled = [&](const std::string& plugin_name) {
    if (options.disable_3p_plugins) {
      return false;
    }
    if (options.enabled_3p_plugins.empty()) {
      return true;
    }
    return std::find(options.enabled_3p_plugins.begin(),
                     options.enabled_3p_plugins.end(),
                     plugin_name) != options.enabled_3p_plugins.end();
  };

  for (const auto& block : collection.blocks()) {
    if (block.raw_bytes().empty() || !block.has_payload_type_url() ||
        !block.has_decoded_payload_bytes()) {
      continue;
    }

    const IMetadataProvider* matched_provider = nullptr;

    // 1. Check instance-registered providers (including default
    // GoogleMotionPhotoProvider)
    for (const auto& provider : providers_) {
      if (provider->IsMotionPhoto(block.payload_type_url(),
                                  block.decoded_payload_bytes())) {
        matched_provider = provider.get();
        break;
      }
    }

    // 2. Check 3P plugins (filtered by HandlerOptions)
    if (!matched_provider && !options.disable_3p_plugins) {
      for (const auto& provider : MetadataProviderRegistry::Get3pProviders()) {
        if (is_plugin_enabled(provider->GetPluginName()) &&
            provider->IsMotionPhoto(block.payload_type_url(),
                                    block.decoded_payload_bytes())) {
          matched_provider = provider.get();
          break;
        }
      }
    }

    if (matched_provider) {
      std::ofstream ofs(output_file_path, std::ios::out | std::ios::binary);
      if (ofs.is_open()) {
        RawMetadataBlock raw_block;
        raw_block.type = block.type();
        raw_block.format_identifier = block.format_identifier();
        raw_block.offset = block.offset();
        raw_block.bytes.assign(block.raw_bytes().begin(),
                               block.raw_bytes().end());

        std::vector<uint8_t> meta_bytes;
        if (matched_provider->ExtractRawMetadata(raw_block, &meta_bytes)) {
          ofs.write(reinterpret_cast<const char*>(meta_bytes.data()),
                    meta_bytes.size());
          ofs.close();
          return true;
        }
      }
      return false;
    }
  }

  return false;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
