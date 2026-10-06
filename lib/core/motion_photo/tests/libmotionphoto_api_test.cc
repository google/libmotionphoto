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

#include "libmotionphoto_api.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace api {

namespace fs = std::filesystem;

// 1. Test ParseMotionPhotoFromFile with native MotionPhotoMetadata facade struct
TEST(LibMotionPhotoApiTest, ParseMotionPhotoFromFileJpeg) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  MotionPhotoMetadata metadata;
  bool success = ParseMotionPhotoFromFile(file_path, &metadata);
  EXPECT_TRUE(success);
  EXPECT_TRUE(metadata.is_motion_photo);
  EXPECT_EQ(metadata.presentation_timestamp_us, 500000);
  EXPECT_EQ(metadata.primary_image_mime, "image/jpeg");
  EXPECT_EQ(metadata.video_mime, "video/mp4");
  EXPECT_TRUE(metadata.container.items.size() >= 2u);
  EXPECT_TRUE(metadata.video_length > 0);
}

TEST(LibMotionPhotoApiTest, ParseMotionPhotoFromFileHeic) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  MotionPhotoMetadata metadata;
  bool success = ParseMotionPhotoFromFile(file_path, &metadata);
  EXPECT_TRUE(success);
  EXPECT_TRUE(metadata.is_motion_photo);
  EXPECT_EQ(metadata.presentation_timestamp_us, 500000);
  EXPECT_EQ(metadata.primary_image_mime, "image/heic");
  EXPECT_EQ(metadata.video_mime, "video/mp4");
  EXPECT_TRUE(metadata.container.items.size() >= 2u);
  EXPECT_TRUE(metadata.video_length > 0);
}

TEST(LibMotionPhotoApiTest, ParseMotionPhotoHeicWithoutOutputMetadata) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  EXPECT_TRUE(ParseMotionPhotoFromFile(file_path, nullptr));

  std::ifstream file(file_path, std::ios::binary);
  std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
  ASSERT_FALSE(buffer.empty());
  EXPECT_TRUE(
      ParseMotionPhotoFromMemory(buffer.data(), buffer.size(), nullptr));
}

TEST(LibMotionPhotoApiTest, ParseMotionPhotoHeicWithoutVideo) {
  // Motion photo XMP, but no 'mpvd' box: the video was removed.
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.heic";
  EXPECT_FALSE(ParseMotionPhotoFromFile(file_path, nullptr));

  MotionPhotoMetadata metadata;
  EXPECT_FALSE(ParseMotionPhotoFromFile(file_path, &metadata));
  EXPECT_FALSE(metadata.is_motion_photo);
}

// 2. Test ParseMotionPhotoFromFd
TEST(LibMotionPhotoApiTest, ParseMotionPhotoFromFd) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  int fd = open(file_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);

  MotionPhotoMetadata metadata;
  size_t file_size = std::filesystem::file_size(file_path);
  EXPECT_TRUE(ParseMotionPhotoFromFd(fd, 0, file_size, &metadata));
  close(fd);

  EXPECT_TRUE(metadata.is_motion_photo);
  EXPECT_EQ(metadata.primary_image_mime, "image/jpeg");
}

// 3. Test CheckMotionPhoto with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, CheckMotionPhotoFileValid) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  std::vector<std::string> messages;
  int status = CheckMotionPhotoFromFile(file_path, [&](const std::string& msg) {
    messages.push_back(msg);
  });
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(messages.empty());
}

TEST(LibMotionPhotoApiTest, CheckMotionPhotoFromFdAndMemoryValid) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  int fd = open(file_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  size_t file_size = fs::file_size(file_path);
  EXPECT_EQ(CheckMotionPhotoFromFd(fd, 0, file_size), 0);
  close(fd);

  std::ifstream file(file_path, std::ios::binary);
  std::vector<uint8_t> buffer(file_size);
  EXPECT_TRUE(file.read(reinterpret_cast<char*>(buffer.data()), file_size));
  EXPECT_EQ(CheckMotionPhotoFromMemory(buffer.data(), buffer.size()), 0);
}

// 4. Test ExtractPrimaryImage with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractPrimaryImageTriadValid) {
  std::string input_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  size_t file_size = fs::file_size(input_path);

  // File
  std::string out_file = "/tmp/extracted_primary_triad_file.jpg";
  if (fs::exists(out_file)) fs::remove(out_file);
  EXPECT_TRUE(ExtractPrimaryImageFromFile(input_path, out_file));
  EXPECT_TRUE(fs::exists(out_file));
  EXPECT_TRUE(fs::file_size(out_file) > 0);
  fs::remove(out_file);

  // Fd
  std::string out_fd = "/tmp/extracted_primary_triad_fd.jpg";
  if (fs::exists(out_fd)) fs::remove(out_fd);
  int fd = open(input_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractPrimaryImageFromFd(fd, 0, file_size, out_fd));
  close(fd);
  EXPECT_TRUE(fs::exists(out_fd));
  EXPECT_TRUE(fs::file_size(out_fd) > 0);
  fs::remove(out_fd);

  // Memory
  std::string out_mem = "/tmp/extracted_primary_triad_mem.jpg";
  if (fs::exists(out_mem)) fs::remove(out_mem);
  std::ifstream infile(input_path, std::ios::binary);
  std::vector<uint8_t> buffer(file_size);
  EXPECT_TRUE(infile.read(reinterpret_cast<char*>(buffer.data()), file_size));
  EXPECT_TRUE(ExtractPrimaryImageFromMemory(buffer.data(), buffer.size(), out_mem));
  EXPECT_TRUE(fs::exists(out_mem));
  EXPECT_TRUE(fs::file_size(out_mem) > 0);
  fs::remove(out_mem);
}

// 5. Test ExtractVideoTrack with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractVideoTrackTriadValid) {
  std::string input_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  size_t file_size = fs::file_size(input_path);

  // File
  std::string out_file = "/tmp/extracted_video_triad_file.mp4";
  if (fs::exists(out_file)) fs::remove(out_file);
  EXPECT_TRUE(ExtractVideoTrackFromFile(input_path, out_file));
  EXPECT_TRUE(fs::exists(out_file));
  EXPECT_TRUE(fs::file_size(out_file) > 0);
  fs::remove(out_file);

  // Fd
  std::string out_fd = "/tmp/extracted_video_triad_fd.mp4";
  if (fs::exists(out_fd)) fs::remove(out_fd);
  int fd = open(input_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractVideoTrackFromFd(fd, 0, file_size, out_fd));
  close(fd);
  EXPECT_TRUE(fs::exists(out_fd));
  EXPECT_TRUE(fs::file_size(out_fd) > 0);
  fs::remove(out_fd);

  // Memory
  std::string out_mem = "/tmp/extracted_video_triad_mem.mp4";
  if (fs::exists(out_mem)) fs::remove(out_mem);
  std::ifstream infile(input_path, std::ios::binary);
  std::vector<uint8_t> buffer(file_size);
  EXPECT_TRUE(infile.read(reinterpret_cast<char*>(buffer.data()), file_size));
  EXPECT_TRUE(ExtractVideoTrackFromMemory(buffer.data(), buffer.size(), out_mem));
  EXPECT_TRUE(fs::exists(out_mem));
  EXPECT_TRUE(fs::file_size(out_mem) > 0);
  fs::remove(out_mem);
}

// 6. Test ExtractMotionPhoto (Multi-Output) with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractMotionPhotoMultiOutputTriadValid) {
  std::string input_path = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  size_t file_size = fs::file_size(input_path);

  ExtractorParams params;
  params.motion_photo_filepath = input_path;
  params.primary_image_output_path = "/tmp/extracted_multi_img.jpg";
  params.video_output_path = "/tmp/extracted_multi_vid.mp4";

  // File
  EXPECT_TRUE(ExtractMotionPhotoFromFile(params));
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);

  // Fd
  int fd = open(input_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractMotionPhotoFromFd(fd, 0, file_size, params));
  close(fd);
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);

  // Memory
  std::ifstream infile(input_path, std::ios::binary);
  std::vector<uint8_t> buffer(file_size);
  EXPECT_TRUE(infile.read(reinterpret_cast<char*>(buffer.data()), file_size));
  EXPECT_TRUE(ExtractMotionPhotoFromMemory(buffer.data(), buffer.size(), params));
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);
}

// 7. Test ExtractAgtm with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractAgtmFromFileAndFdValid) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  size_t file_size = fs::file_size(file_path);

  EXPECT_EQ(ExtractAgtmFromFile(file_path), 0);

  int fd = open(file_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  EXPECT_EQ(ExtractAgtmFromFd(fd, 0, file_size), 0);
  close(fd);
}

TEST(LibMotionPhotoApiTest, ExtractAgtmFromMemoryValid) {
  std::string file_path = "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  EXPECT_TRUE(file.is_open());
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  EXPECT_TRUE(file.read(reinterpret_cast<char*>(buffer.data()), size));

  std::string output_json;
  int status = ExtractAgtmFromMemory(buffer.data(), buffer.size(), [&](const std::string& msg) {
    output_json += msg;
  });
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(output_json.empty());
  EXPECT_NE(output_json.find("hdrReferenceWhite"), std::string::npos);
}

// 6. Test BuildMotionPhotoFromFile
TEST(LibMotionPhotoApiTest, BuildMotionPhotoFromFileValid) {
  std::string image_path = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  std::string video_path = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  std::string output_path = "/tmp/built_motion_photo_api_test.jpg";
  if (fs::exists(output_path)) fs::remove(output_path);

  int status = BuildMotionPhotoFromFile(image_path, video_path, output_path, 500000);
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(fs::exists(output_path));
  EXPECT_TRUE(fs::file_size(output_path) > 0);

  fs::remove(output_path);
}

// 7. Test GetLibMotionPhotoVersion
TEST(LibMotionPhotoApiTest, GetLibMotionPhotoVersion) {
  std::string version = GetLibMotionPhotoVersion();
  EXPECT_EQ(version, "1.0.0");
}

// 8. Test Integer Overflow Boundary Safety
TEST(LibMotionPhotoApiTest, ParseMotionPhotoFromFdIntegerOverflowSafe) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  int fd = open(file_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);

  MotionPhotoMetadata metadata;
  // Test with offset that would overflow when added to length:
  size_t huge_offset = std::numeric_limits<size_t>::max() - 100;
  size_t length = 200;
  EXPECT_FALSE(ParseMotionPhotoFromFd(fd, huge_offset, length, &metadata));

  // Test with length exceeding total bounds:
  EXPECT_FALSE(ParseMotionPhotoFromFd(fd, 0, std::numeric_limits<size_t>::max(),
                                      &metadata));
  close(fd);
}

TEST(LibMotionPhotoApiTest, ExtractAgtmFromMemoryBoundarySafe) {
  uint8_t dummy[4] = {0xB5, 0x00, 0x90, 0x00};
  std::string output_json;
  // Test with 0 or truncated size safely without out-of-bounds reads
  EXPECT_NE(ExtractAgtmFromMemory(
                dummy, 0, [&](const std::string& msg) { output_json += msg; }),
            0);
  EXPECT_NE(ExtractAgtmFromMemory(
                dummy, 2, [&](const std::string& msg) { output_json += msg; }),
            0);
}

TEST(LibMotionPhotoApiTest, BitReaderAndBoxReaderBoundarySafe) {
  // Test boundary resiliency on truncated buffers
  uint8_t truncated_mp4[16] = {0x00, 0x00, 0x00, 0x10, 'f', 't', 'y', 'p'};
  MotionPhotoMetadata metadata;
  EXPECT_FALSE(ParseMotionPhotoFromMemory(truncated_mp4, sizeof(truncated_mp4),
                                          &metadata));
}

TEST(LibMotionPhotoApiTest, ParseMotionPhoto3pPluginOptions) {
  // Construct synthetic Legacy MicroVideo JPEG in memory (recognized by the
  // "legacy_micro_video" 3P plugin, not by 1P GoogleMotionPhotoProvider).
  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoOffset='16' "
      "GCamera:MicroVideoPresentationTimestampUs='1500000'/></rdf:RDF></"
      "x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));  // APP1
  uint16_t app1_len = 2 + 29 + xmp_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(xmp_content);
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));  // EOI
  std::string mp4_trailer = "....ftypmp42....";
  jpeg_data.append(mp4_trailer);

  const auto* raw_ptr = reinterpret_cast<const uint8_t*>(jpeg_data.data());
  size_t raw_size = jpeg_data.size();

  // 1. Default (all 3P plugins enabled): is_motion_photo should be true.
  MotionPhotoMetadata meta_default;
  EXPECT_TRUE(ParseMotionPhotoFromMemory(raw_ptr, raw_size, &meta_default));
  EXPECT_TRUE(meta_default.is_motion_photo);
  EXPECT_EQ(meta_default.video_length,
            static_cast<int64_t>(mp4_trailer.size()));

  // 2. 3P plugins disabled: is_motion_photo should be false.
  HandlerOptions disabled_opts;
  disabled_opts.disable_3p_plugins = true;
  MotionPhotoMetadata meta_disabled;
  EXPECT_FALSE(ParseMotionPhotoFromMemory(raw_ptr, raw_size, &meta_disabled,
                                          nullptr, disabled_opts));
  EXPECT_FALSE(meta_disabled.is_motion_photo);
  EXPECT_EQ(meta_disabled.video_length, 0);

  // 3. Specific 3P plugin enabled ("legacy_micro_video") and recognizes format:
  // is_motion_photo should be true.
  HandlerOptions enabled_matching_opts;
  enabled_matching_opts.disable_3p_plugins = false;
  enabled_matching_opts.enabled_3p_plugins = {"legacy_micro_video"};
  MotionPhotoMetadata meta_matching;
  EXPECT_TRUE(ParseMotionPhotoFromMemory(raw_ptr, raw_size, &meta_matching,
                                         nullptr, enabled_matching_opts));
  EXPECT_TRUE(meta_matching.is_motion_photo);
  EXPECT_EQ(meta_matching.video_length,
            static_cast<int64_t>(mp4_trailer.size()));

  // 4. Specific 3P plugin enabled ("other_plugin") that does not recognize
  // format: is_motion_photo should be false.
  HandlerOptions enabled_unmatched_opts;
  enabled_unmatched_opts.disable_3p_plugins = false;
  enabled_unmatched_opts.enabled_3p_plugins = {"other_plugin"};
  MotionPhotoMetadata meta_unmatched;
  EXPECT_FALSE(ParseMotionPhotoFromMemory(raw_ptr, raw_size, &meta_unmatched,
                                          nullptr, enabled_unmatched_opts));
  EXPECT_FALSE(meta_unmatched.is_motion_photo);
  EXPECT_EQ(meta_unmatched.video_length, 0);
}

}  // namespace api
}  // namespace libmotionphoto
