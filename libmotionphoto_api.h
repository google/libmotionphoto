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

#ifndef LIBMOTIONPHOTO_LIBMOTIONPHOTO_API_H_
#define LIBMOTIONPHOTO_LIBMOTIONPHOTO_API_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace libmotionphoto {
namespace api {

// Native log/message callback interface eliminating image_io::MessageHandler leak.
using MessageCallback = std::function<void(const std::string& message)>;

// Clean, native ContainerItem struct hiding image_io::XmpContainerItem.
struct ContainerItem {
  std::string mime;
  std::string semantic;
  int64_t length = 0;
  int64_t padding = 0;
};

// Clean, native ContainerMetadata struct hiding image_io::XmpContainerMetadata.
struct ContainerMetadata {
  int32_t version = 1;
  std::vector<ContainerItem> items;
};

// Parameters for multi-output extraction from a Motion Photo.
//
// Field usage guidance:
// - motion_photo_filepath: Required when calling ExtractMotionPhotoFromFile().
//   Ignored for ExtractMotionPhotoFromFd() and ExtractMotionPhotoFromMemory().
// - primary_image_output_path: Optional (nullable/can be empty ""). When non-empty,
//   extracts the primary still image to this destination path.
// - video_output_path: Optional (nullable/can be empty ""). When non-empty,
//   extracts the MP4 video track to this destination path.
// - metadata_output_path: Optional (nullable/can be empty ""). When non-empty,
//   extracts raw XMP metadata to this destination path.
//
// Output paths left empty ("") are skipped during extraction without error.
// Multiple populated output paths will be extracted simultaneously in a single pass.
struct ExtractorParams {
  std::string motion_photo_filepath;
  std::string primary_image_output_path;
  std::string video_output_path;
  std::string metadata_output_path;
};

// Clean, native MotionPhotoMetadata facade struct.
// Fully extracts all metadata fields from nested third-party image_io structures.
struct MotionPhotoMetadata {
  bool is_motion_photo = false;
  int64_t presentation_timestamp_us = -1;
  std::string primary_image_mime;
  std::string video_mime;
  int64_t video_length = 0;
  int64_t image_padding = 0;
  ContainerMetadata container;
};

// =========================================================================
// 1. Parsing & Inspection (Decoding / Metadata Extraction)
// =========================================================================

// Parse Motion Photo metadata from a file path into native MotionPhotoMetadata facade struct.
// Returns true if the file is a valid Motion Photo.
bool ParseMotionPhotoFromFile(const std::string& filepath,
                              MotionPhotoMetadata* out_metadata,
                              MessageCallback callback = nullptr);

// Parse Motion Photo metadata from a POSIX file descriptor slice (fd, offset, length).
// Returns true if parsing succeeded and the payload is a valid Motion Photo.
bool ParseMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                            MotionPhotoMetadata* out_metadata,
                            MessageCallback callback = nullptr);

// Parse Motion Photo metadata from an in-memory byte buffer.
// Returns true if parsing succeeded and the payload is a valid Motion Photo.
bool ParseMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                MotionPhotoMetadata* out_metadata,
                                MessageCallback callback = nullptr);

// =========================================================================
// 2. Container Validation (Checking)
// =========================================================================

// Validate a Motion Photo container file using native callback.
// Returns 0 for success, non-zero for error.
int CheckMotionPhotoFromFile(const std::string& filepath,
                             MessageCallback callback = nullptr);

// Legacy alias for CheckMotionPhotoFromFile.
inline int CheckMotionPhotoFile(const std::string& filepath,
                                MessageCallback callback = nullptr) {
  return CheckMotionPhotoFromFile(filepath, callback);
}

// Validate a Motion Photo from a POSIX file descriptor slice (fd, offset, length).
// Returns 0 for success, non-zero for error.
int CheckMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                           MessageCallback callback = nullptr);

// Validate a Motion Photo from an in-memory byte buffer.
// Returns 0 for success, non-zero for error.
int CheckMotionPhotoFromMemory(const uint8_t* data, size_t size,
                               MessageCallback callback = nullptr);

// =========================================================================
// 3. Media & Metadata Extraction
// =========================================================================

// Extract the primary still image payload from a Motion Photo file.
// Returns true on success.
bool ExtractPrimaryImageFromFile(const std::string& input_filepath,
                                 const std::string& output_image_path,
                                 MessageCallback callback = nullptr);

// Extract the primary still image payload from a POSIX file descriptor slice.
// Returns true on success.
bool ExtractPrimaryImageFromFd(int fd, int64_t offset, int64_t length,
                               const std::string& output_image_path,
                               MessageCallback callback = nullptr);

// Extract the primary still image payload from an in-memory byte buffer.
// Returns true on success.
bool ExtractPrimaryImageFromMemory(const uint8_t* data, size_t size,
                                   const std::string& output_image_path,
                                   MessageCallback callback = nullptr);

// Extract the MP4 video track payload from a Motion Photo file.
// Returns true on success.
bool ExtractVideoTrackFromFile(const std::string& input_filepath,
                               const std::string& output_video_path,
                               MessageCallback callback = nullptr);

// Extract the MP4 video track payload from a POSIX file descriptor slice.
// Returns true on success.
bool ExtractVideoTrackFromFd(int fd, int64_t offset, int64_t length,
                             const std::string& output_video_path,
                             MessageCallback callback = nullptr);

// Extract the MP4 video track payload from an in-memory byte buffer.
// Returns true on success.
bool ExtractVideoTrackFromMemory(const uint8_t* data, size_t size,
                                 const std::string& output_video_path,
                                 MessageCallback callback = nullptr);

// Extract primary image, video track, and/or metadata from a Motion Photo file
// according to the non-empty output paths specified in params.
// Returns true on success.
bool ExtractMotionPhotoFromFile(const ExtractorParams& params,
                                MessageCallback callback = nullptr);

// Extract primary image, video track, and/or metadata from a file descriptor slice.
// Returns true on success.
bool ExtractMotionPhotoFromFd(int fd, int64_t offset, int64_t length,
                              const ExtractorParams& params,
                              MessageCallback callback = nullptr);

// Extract primary image, video track, and/or metadata from an in-memory byte buffer.
// Returns true on success.
bool ExtractMotionPhotoFromMemory(const uint8_t* data, size_t size,
                                  const ExtractorParams& params,
                                  MessageCallback callback = nullptr);

// Extract AGTM metadata payload from a Motion Photo file.
// Automatically parses metadata first to extract the target presentation timestamp.
// Returns 0 on success, non-zero on error.
int ExtractAgtmFromFile(const std::string& filepath,
                        MessageCallback callback = nullptr);

// Extract AGTM metadata payload from a POSIX file descriptor slice.
// Automatically parses metadata first to extract the target presentation timestamp.
// Returns 0 on success, non-zero on error.
int ExtractAgtmFromFd(int fd, int64_t offset, int64_t length,
                      MessageCallback callback = nullptr);

// Extract AGTM metadata payload from an in-memory Motion Photo byte buffer.
// Automatically parses metadata first to extract the target presentation timestamp.
// Returns 0 on success, non-zero on error.
int ExtractAgtmFromMemory(const uint8_t* data, size_t size,
                          MessageCallback callback = nullptr);

// =========================================================================
// 4. Motion Photo Authoring & Building (Creation)
// =========================================================================

// Build a Motion Photo file by combining a still image and an MP4 video file.
// Returns 0 on success, non-zero on error.
int BuildMotionPhotoFromFile(const std::string& image_path,
                             const std::string& video_path,
                             const std::string& output_path,
                             int64_t presentation_timestamp_us = -1,
                             MessageCallback callback = nullptr);

// Build a Motion Photo file from POSIX file descriptor slices.
// Returns 0 on success, non-zero on error.
int BuildMotionPhotoFromFd(int image_fd, int64_t image_offset, int64_t image_length,
                           int video_fd, int64_t video_offset, int64_t video_length,
                           const std::string& output_path,
                           int64_t presentation_timestamp_us = -1,
                           MessageCallback callback = nullptr);

// Build a Motion Photo file from in-memory image and video byte buffers.
// Returns 0 on success, non-zero on error.
int BuildMotionPhotoFromMemory(const uint8_t* image_bytes, size_t image_size,
                               const uint8_t* video_bytes, size_t video_size,
                               const std::string& output_path,
                               int64_t presentation_timestamp_us = -1,
                               MessageCallback callback = nullptr);

// =========================================================================
// 5. Library Version
// =========================================================================

// Get library release version string.
std::string GetLibMotionPhotoVersion();

}  // namespace api
}  // namespace libmotionphoto

#endif  // LIBMOTIONPHOTO_LIBMOTIONPHOTO_API_H_
