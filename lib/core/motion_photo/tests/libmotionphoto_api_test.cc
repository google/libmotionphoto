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

#include "image_io/base/data_segment_data_source.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace api {

namespace fs = std::filesystem;

// 1. Test ParseMotionPhotoFromFile with native MotionPhotoMetadata facade
// struct
TEST(LibMotionPhotoApiTest, ParseMotionPhotoFromFileJpeg) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
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
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  std::vector<std::string> messages;
  int status = CheckMotionPhotoFromFile(
      file_path, [&](const std::string& msg) { messages.push_back(msg); });
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(messages.empty());
}

TEST(LibMotionPhotoApiTest, CheckMotionPhotoFromFdAndMemoryValid) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  std::string input_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  EXPECT_TRUE(
      ExtractPrimaryImageFromMemory(buffer.data(), buffer.size(), out_mem));
  EXPECT_TRUE(fs::exists(out_mem));
  EXPECT_TRUE(fs::file_size(out_mem) > 0);
  fs::remove(out_mem);
}

// 5. Test ExtractVideoTrack with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractVideoTrackTriadValid) {
  std::string input_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  EXPECT_TRUE(
      ExtractVideoTrackFromMemory(buffer.data(), buffer.size(), out_mem));
  EXPECT_TRUE(fs::exists(out_mem));
  EXPECT_TRUE(fs::file_size(out_mem) > 0);
  fs::remove(out_mem);
}

// 6. Test ExtractMotionPhoto (Multi-Output) with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractMotionPhotoMultiOutputTriadValid) {
  std::string input_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
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
  EXPECT_TRUE(
      ExtractMotionPhotoFromMemory(buffer.data(), buffer.size(), params));
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);
}

// 6b. Test ExtractPrimaryImage, ExtractVideoTrack, and ExtractMotionPhoto
// (still image + video + metadata) on HEIC with File, Fd, and Memory.
// Uses TEST_TMPDIR / std::filesystem::temp_directory_path() instead of /tmp so
// the test runs cleanly on Android OS (/data/local/tmp) as well as Linux/macOS.
TEST(LibMotionPhotoApiTest, ExtractHeicTriadValid) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / "ExtractHeicTriadValid";
  fs::create_directories(test_dir);

  std::string input_path =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  size_t file_size = fs::file_size(input_path);
  std::ifstream infile(input_path, std::ios::binary);
  std::vector<uint8_t> buffer(file_size);
  ASSERT_TRUE(infile.read(reinterpret_cast<char*>(buffer.data()), file_size));

  // 1. ExtractPrimaryImage (File / Fd / Memory)
  std::string img_file =
      (test_dir / "extracted_heic_primary_file.heic").string();
  std::string img_fd = (test_dir / "extracted_heic_primary_fd.heic").string();
  std::string img_mem = (test_dir / "extracted_heic_primary_mem.heic").string();
  if (fs::exists(img_file)) fs::remove(img_file);
  if (fs::exists(img_fd)) fs::remove(img_fd);
  if (fs::exists(img_mem)) fs::remove(img_mem);

  EXPECT_TRUE(ExtractPrimaryImageFromFile(input_path, img_file));
  int fd = open(input_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractPrimaryImageFromFd(fd, 0, file_size, img_fd));
  close(fd);
  EXPECT_TRUE(
      ExtractPrimaryImageFromMemory(buffer.data(), buffer.size(), img_mem));

  ASSERT_TRUE(fs::exists(img_file));
  ASSERT_TRUE(fs::exists(img_fd));
  ASSERT_TRUE(fs::exists(img_mem));
  // All three paths must produce the exact same primary .heic byte size
  // (without leaving the 8-byte 'mpvd' box header attached).
  EXPECT_TRUE(fs::file_size(img_file) > 0u);
  EXPECT_EQ(fs::file_size(img_file), fs::file_size(img_fd));
  EXPECT_EQ(fs::file_size(img_file), fs::file_size(img_mem));
  // Extracted still image from File/Fd/Memory has MotionPhoto XMP stripped and
  // no mpvd box.
  EXPECT_FALSE(ParseMotionPhotoFromFile(img_file, nullptr));
  EXPECT_FALSE(ParseMotionPhotoFromFile(img_fd, nullptr));
  EXPECT_FALSE(ParseMotionPhotoFromFile(img_mem, nullptr));
  for (const std::string& extracted_path : {img_file, img_fd, img_mem}) {
    std::ifstream in(extracted_path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    EXPECT_EQ(bytes.find("MotionPhoto="), std::string::npos);
  }
  fs::remove(img_file);
  fs::remove(img_fd);
  fs::remove(img_mem);

  // 2. ExtractVideoTrack (File / Fd / Memory)
  std::string vid_file = (test_dir / "extracted_heic_video_file.mp4").string();
  std::string vid_fd = (test_dir / "extracted_heic_video_fd.mp4").string();
  std::string vid_mem = (test_dir / "extracted_heic_video_mem.mp4").string();
  if (fs::exists(vid_file)) fs::remove(vid_file);
  if (fs::exists(vid_fd)) fs::remove(vid_fd);
  if (fs::exists(vid_mem)) fs::remove(vid_mem);

  EXPECT_TRUE(ExtractVideoTrackFromFile(input_path, vid_file));
  fd = open(input_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractVideoTrackFromFd(fd, 0, file_size, vid_fd));
  close(fd);
  EXPECT_TRUE(
      ExtractVideoTrackFromMemory(buffer.data(), buffer.size(), vid_mem));

  ASSERT_TRUE(fs::exists(vid_file));
  ASSERT_TRUE(fs::exists(vid_fd));
  ASSERT_TRUE(fs::exists(vid_mem));
  EXPECT_TRUE(fs::file_size(vid_file) > 0u);
  EXPECT_TRUE(fs::file_size(vid_fd) > 0u);
  EXPECT_EQ(fs::file_size(vid_fd), fs::file_size(vid_mem));
  fs::remove(vid_file);
  fs::remove(vid_fd);
  fs::remove(vid_mem);

  // 3. ExtractMotionPhoto (still image + video + metadata) from File / Fd /
  // Memory
  ExtractorParams params;
  params.motion_photo_filepath = input_path;
  params.primary_image_output_path =
      (test_dir / "extracted_heic_multi_img.heic").string();
  params.video_output_path =
      (test_dir / "extracted_heic_multi_vid.mp4").string();
  params.metadata_output_path =
      (test_dir / "extracted_heic_multi_meta.xml").string();

  EXPECT_TRUE(ExtractMotionPhotoFromFile(params));
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  EXPECT_TRUE(fs::exists(params.metadata_output_path));
  EXPECT_TRUE(fs::file_size(params.metadata_output_path) > 0u);
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);
  fs::remove(params.metadata_output_path);

  fd = open(input_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  EXPECT_TRUE(ExtractMotionPhotoFromFd(fd, 0, file_size, params));
  close(fd);
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  EXPECT_TRUE(fs::exists(params.metadata_output_path));
  EXPECT_TRUE(fs::file_size(params.metadata_output_path) > 0u);
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);
  fs::remove(params.metadata_output_path);

  EXPECT_TRUE(
      ExtractMotionPhotoFromMemory(buffer.data(), buffer.size(), params));
  EXPECT_TRUE(fs::exists(params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(params.video_output_path));
  EXPECT_TRUE(fs::exists(params.metadata_output_path));
  EXPECT_TRUE(fs::file_size(params.metadata_output_path) > 0u);
  fs::remove(params.primary_image_output_path);
  fs::remove(params.video_output_path);
  fs::remove(params.metadata_output_path);

  // 4. Verify HEIC with a trailing vendor box (e.g. 'sefd') after 'mpvd' slices
  // out the exact primary .heic image and exact .mp4 video without the trailing
  // box.
  std::vector<uint8_t> heic_with_trailing = buffer;
  const uint8_t trailing_sefd_box[16] = {0x00, 0x00, 0x00, 0x10, 's',  'e',
                                         'f',  'd',  0x01, 0x02, 0x03, 0x04,
                                         0x05, 0x06, 0x07, 0x08};
  heic_with_trailing.insert(heic_with_trailing.end(),
                            std::begin(trailing_sefd_box),
                            std::end(trailing_sefd_box));
  std::string trailing_heic_path =
      (test_dir / "heic_with_trailing_box.MP.heic").string();
  {
    std::ofstream out(trailing_heic_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(heic_with_trailing.data()),
              heic_with_trailing.size());
  }
  ExtractorParams trailing_params;
  trailing_params.motion_photo_filepath = trailing_heic_path;
  trailing_params.primary_image_output_path =
      (test_dir / "extracted_trailing_heic_img.heic").string();
  trailing_params.video_output_path =
      (test_dir / "extracted_trailing_heic_vid.mp4").string();
  trailing_params.metadata_output_path =
      (test_dir / "extracted_trailing_heic_meta.xml").string();

  EXPECT_TRUE(ExtractMotionPhotoFromFile(trailing_params));
  EXPECT_TRUE(fs::exists(trailing_params.primary_image_output_path));
  EXPECT_TRUE(fs::exists(trailing_params.video_output_path));
  EXPECT_TRUE(fs::exists(trailing_params.metadata_output_path));
  // Primary image size must equal the original mpvd start offset (28727 bytes).
  EXPECT_EQ(fs::file_size(trailing_params.primary_image_output_path), 28727u);

  std::string trailing_mem_vid =
      (test_dir / "extracted_trailing_heic_mem_vid.mp4").string();
  EXPECT_TRUE(ExtractVideoTrackFromMemory(
      heic_with_trailing.data(), heic_with_trailing.size(), trailing_mem_vid));
  // Video track from memory must match the extracted video from file
  // (excluding the 16-byte trailing 'sefd' box and with metadata track
  // stripped).
  EXPECT_EQ(fs::file_size(trailing_mem_vid),
            fs::file_size(trailing_params.video_output_path));

  fs::remove_all(test_dir);
}

// 7. Test ExtractAgtm with File, Fd, and Memory
TEST(LibMotionPhotoApiTest, ExtractAgtmFromFileAndFdValid) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  size_t file_size = fs::file_size(file_path);

  EXPECT_EQ(ExtractAgtmFromFile(file_path), 0);

  int fd = open(file_path.c_str(), O_RDONLY);
  EXPECT_TRUE(fd >= 0);
  EXPECT_EQ(ExtractAgtmFromFd(fd, 0, file_size), 0);
  close(fd);
}

TEST(LibMotionPhotoApiTest, ExtractAgtmFromMemoryValid) {
  std::string file_path =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  EXPECT_TRUE(file.is_open());
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  EXPECT_TRUE(file.read(reinterpret_cast<char*>(buffer.data()), size));

  std::string output_json;
  int status = ExtractAgtmFromMemory(
      buffer.data(), buffer.size(),
      [&](const std::string& msg) { output_json += msg; });
  EXPECT_EQ(status, 0);
  EXPECT_FALSE(output_json.empty());
  EXPECT_NE(output_json.find("hdrReferenceWhite"), std::string::npos);
}

// 6. Test BuildMotionPhotoFromFile
TEST(LibMotionPhotoApiTest, BuildMotionPhotoFromFileValid) {
  std::string image_path =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  std::string video_path =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  std::string output_path = "/tmp/built_motion_photo_api_test.jpg";
  if (fs::exists(output_path)) fs::remove(output_path);

  int status =
      BuildMotionPhotoFromFile(image_path, video_path, output_path, 500000);
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(fs::exists(output_path));
  EXPECT_TRUE(fs::file_size(output_path) > 0);

  fs::remove(output_path);
}

TEST(LibMotionPhotoApiTest, BuildMotionPhotoFromMemoryTimestampPropagates) {
  std::string image_path =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  std::string video_path =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";

  std::ifstream img_in(image_path, std::ios::binary);
  std::vector<uint8_t> image_bytes((std::istreambuf_iterator<char>(img_in)),
                                   std::istreambuf_iterator<char>());
  ASSERT_FALSE(image_bytes.empty());

  std::ifstream vid_in(video_path, std::ios::binary);
  std::vector<uint8_t> video_bytes((std::istreambuf_iterator<char>(vid_in)),
                                   std::istreambuf_iterator<char>());
  ASSERT_FALSE(video_bytes.empty());

  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / "BuildMotionPhotoFromMemoryWithTimestamp";
  fs::create_directories(test_dir);

  // 1. Explicit presentation timestamp is recorded in both XMP and MP4 'meta'.
  std::string explicit_out =
      (test_dir / "built_motion_photo_mem_ts_explicit.jpg").string();
  if (fs::exists(explicit_out)) fs::remove(explicit_out);
  EXPECT_EQ(BuildMotionPhotoFromMemory(image_bytes.data(), image_bytes.size(),
                                       video_bytes.data(), video_bytes.size(),
                                       explicit_out, 456789),
            0);
  MotionPhotoMetadata explicit_meta;
  EXPECT_TRUE(ParseMotionPhotoFromFile(explicit_out, &explicit_meta));
  EXPECT_TRUE(explicit_meta.is_motion_photo);
  EXPECT_EQ(explicit_meta.presentation_timestamp_us, 456789);
  EXPECT_EQ(CheckMotionPhotoFromFile(explicit_out), 0);
  fs::remove(explicit_out);

  // 2. Default (-1) timestamp remains valid and spec-compliant.
  std::string default_out =
      (test_dir / "built_motion_photo_mem_ts_default.jpg").string();
  if (fs::exists(default_out)) fs::remove(default_out);
  EXPECT_EQ(BuildMotionPhotoFromMemory(image_bytes.data(), image_bytes.size(),
                                       video_bytes.data(), video_bytes.size(),
                                       default_out, -1),
            0);
  MotionPhotoMetadata default_meta;
  EXPECT_TRUE(ParseMotionPhotoFromFile(default_out, &default_meta));
  EXPECT_TRUE(default_meta.is_motion_photo);
  EXPECT_EQ(CheckMotionPhotoFromFile(default_out), 0);
  fs::remove_all(test_dir);
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

TEST(LibMotionPhotoApiTest, LargeTrailerZeroCopyDetectionFromFdAndMemory) {
  // Construct a synthetic Motion Photo with a 2 MB video trailer to verify
  // that zero-copy trailer inspection accurately detects and extracts video
  // length across both Memory and FD paths without copying the trailer into
  // MetadataBlock.raw_bytes.
  constexpr size_t kLargeTrailerSize = 2 * 1024 * 1024;
  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:Camera='http://ns.google.com/photos/1.0/camera/' "
      "xmlns:Container='http://ns.google.com/photos/1.0/container/' "
      "xmlns:Item='http://ns.google.com/photos/1.0/container/item/' "
      "Camera:MotionPhoto='1' Camera:MotionPhotoVersion='1' "
      "Camera:MotionPhotoPresentationTimestampUs='333333'>"
      "<Container:Directory><rdf:Seq>"
      "<rdf:li rdf:parseType='Resource'><Container:Item "
      "Item:Semantic='Primary' Item:Mime='image/jpeg'/></rdf:li>"
      "<rdf:li rdf:parseType='Resource'><Container:Item "
      "Item:Semantic='MotionPhoto' Item:Mime='video/mp4' "
      "Item:Length='2097152'/></rdf:li>"
      "</rdf:Seq></Container:Directory></rdf:Description></rdf:RDF></"
      "x:xmpmeta>";

  std::vector<uint8_t> file_bytes;
  file_bytes.push_back(0xFF);
  file_bytes.push_back(0xD8);  // SOI
  file_bytes.push_back(0xFF);
  file_bytes.push_back(0xE1);  // APP1
  uint16_t app1_len = static_cast<uint16_t>(2 + 29 + xmp_content.size());
  file_bytes.push_back(static_cast<uint8_t>(app1_len >> 8));
  file_bytes.push_back(static_cast<uint8_t>(app1_len & 0xFF));
  file_bytes.insert(file_bytes.end(), "http://ns.adobe.com/xap/1.0/\0",
                    "http://ns.adobe.com/xap/1.0/\0" + 29);
  file_bytes.insert(file_bytes.end(), xmp_content.begin(), xmp_content.end());
  file_bytes.push_back(0xFF);
  file_bytes.push_back(0xD9);  // EOI

  const size_t trailer_start = file_bytes.size();
  file_bytes.resize(trailer_start + kLargeTrailerSize, 0);
  // Write MP4 ftyp header at start of trailer.
  file_bytes[trailer_start + 0] = 0x00;
  file_bytes[trailer_start + 1] = 0x00;
  file_bytes[trailer_start + 2] = 0x00;
  file_bytes[trailer_start + 3] = 0x18;
  file_bytes[trailer_start + 4] = 'f';
  file_bytes[trailer_start + 5] = 't';
  file_bytes[trailer_start + 6] = 'y';
  file_bytes[trailer_start + 7] = 'p';

  MotionPhotoMetadata mem_meta;
  EXPECT_TRUE(ParseMotionPhotoFromMemory(file_bytes.data(), file_bytes.size(),
                                         &mem_meta));
  EXPECT_TRUE(mem_meta.is_motion_photo);
  EXPECT_EQ(mem_meta.video_length, static_cast<int64_t>(kLargeTrailerSize));

  const std::string tmp_path = "/tmp/libmotionphoto_zero_copy_2mb_test.jpg";
  {
    std::ofstream out(tmp_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file_bytes.data()),
              file_bytes.size());
  }
  int fd = open(tmp_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  MotionPhotoMetadata fd_meta;
  EXPECT_TRUE(ParseMotionPhotoFromFd(
      fd, 0, static_cast<int64_t>(file_bytes.size()), &fd_meta));
  // Passing a length larger than the actual file size causes a short pread
  // near the aligned tail window where total_bytes_read <= (begin -
  // read_begin); FdDataSource must safely return nullptr (and
  // ParseMotionPhotoFromFd returns false) without creating an out-of-range
  // DataSegment or crashing.
  MotionPhotoMetadata inflated_fd_meta;
  EXPECT_FALSE(ParseMotionPhotoFromFd(
      fd, 0, static_cast<int64_t>(file_bytes.size() + 2048),
      &inflated_fd_meta));
  close(fd);
  std::remove(tmp_path.c_str());
  EXPECT_TRUE(fd_meta.is_motion_photo);
  EXPECT_EQ(fd_meta.video_length, static_cast<int64_t>(kLargeTrailerSize));
}

TEST(LibMotionPhotoApiTest,
     ExtractPrimaryImageHeic_SingleTrackAndMpvdBeforeMeta) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / "HeicExtractFallbackTest";
  fs::create_directories(test_dir);

  // 1. Extracted still from motion_photo_single_video_track.MP.heic retains
  // zero Container:Version="1" or MotionPhoto tags.
  const std::string kHeicMp =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  const std::string out_still = (test_dir / "single_track_still.heic").string();
  ASSERT_TRUE(ExtractPrimaryImageFromFile(kHeicMp, out_still));
  MotionPhotoMetadata still_meta;
  EXPECT_FALSE(ParseMotionPhotoFromFile(out_still, &still_meta) &&
               still_meta.is_motion_photo);
  {
    std::ifstream in(out_still, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("Container:Version"), std::string::npos);
    EXPECT_EQ(content.find("MotionPhoto"), std::string::npos);
  }
  fs::remove(out_still);

  // 2. Non-trailing 'mpvd' (followed by trailing 'free' box) must rebuild
  // complete still rather than truncating at mpvd_start.
  std::ifstream src_in(kHeicMp, std::ios::binary);
  std::vector<uint8_t> heic_bytes((std::istreambuf_iterator<char>(src_in)),
                                  std::istreambuf_iterator<char>());
  ASSERT_TRUE(heic_bytes.size() > 100u);
  std::vector<uint8_t> non_trailing = heic_bytes;
  const uint8_t kFreeBox[8] = {0, 0, 0, 8, 'f', 'r', 'e', 'e'};
  non_trailing.insert(non_trailing.end(), std::begin(kFreeBox),
                      std::end(kFreeBox));

  const std::string out_mem_rebuilt =
      (test_dir / "rebuilt_non_trailing_mpvd.heic").string();
  ASSERT_TRUE(ExtractPrimaryImageFromMemory(
      non_trailing.data(), non_trailing.size(), out_mem_rebuilt));
  EXPECT_TRUE(fs::file_size(out_mem_rebuilt) > 1000u);
  MotionPhotoMetadata rebuilt_meta;
  EXPECT_FALSE(ParseMotionPhotoFromFile(out_mem_rebuilt, &rebuilt_meta) &&
               rebuilt_meta.is_motion_photo);
  fs::remove_all(test_dir);
}

TEST(LibMotionPhotoApiTest, BuildsParsesChecksAndExtractsAvifAndMultiExtent) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / "AvifAndMultiExtentApiTest";
  fs::create_directories(test_dir);

  auto put = [](std::vector<uint8_t>* out, uint64_t val, size_t n) {
    for (size_t i = n; i > 0; --i) {
      size_t shift = 8 * (i - 1);
      out->push_back(
          static_cast<uint8_t>(shift >= 64 ? 0 : ((val >> shift) & 0xFF)));
    }
  };
  auto put_str = [](std::vector<uint8_t>* out, const std::string& s) {
    out->insert(out->end(), s.begin(), s.end());
  };
  auto make_box = [&](const std::string& type, const std::vector<uint8_t>& p) {
    std::vector<uint8_t> b;
    put(&b, 8 + p.size(), 4);
    put_str(&b, type);
    b.insert(b.end(), p.begin(), p.end());
    return b;
  };
  auto make_full_box = [&](const std::string& type, uint8_t ver,
                           const std::vector<uint8_t>& p) {
    std::vector<uint8_t> fp;
    fp.push_back(ver);
    put(&fp, 0, 3);
    fp.insert(fp.end(), p.begin(), p.end());
    return make_box(type, fp);
  };

  // 1. AVIF end-to-end build -> check -> parse (file, fd, memory) -> extract.
  const std::string av1_pixels = "SYNTHETIC_AV1_PRIMARY_PIXELS";
  auto build_avif = [&](uint32_t mdat_data_off) {
    std::vector<uint8_t> ftyp_p;
    put_str(&ftyp_p, "avif");
    put(&ftyp_p, 0, 4);
    put_str(&ftyp_p, "mif1avif");
    std::vector<uint8_t> hdlr_p;
    put(&hdlr_p, 0, 4);
    put_str(&hdlr_p, "pict");
    hdlr_p.insert(hdlr_p.end(), 13, 0);
    std::vector<uint8_t> pitm_p;
    put(&pitm_p, 1, 2);
    std::vector<uint8_t> infe1_p;
    put(&infe1_p, 1, 2);
    put(&infe1_p, 0, 2);
    put_str(&infe1_p, "av01");
    infe1_p.push_back(0);
    std::vector<uint8_t> iinf_p;
    put(&iinf_p, 1, 2);
    auto infe1 = make_full_box("infe", 2, infe1_p);
    iinf_p.insert(iinf_p.end(), infe1.begin(), infe1.end());
    std::vector<uint8_t> iloc_p;
    put(&iloc_p, 0x4400, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, mdat_data_off, 4);
    put(&iloc_p, av1_pixels.size(), 4);

    std::vector<uint8_t> meta_children;
    for (const auto& c :
         {make_full_box("hdlr", 0, hdlr_p), make_full_box("pitm", 0, pitm_p),
          make_full_box("iinf", 0, iinf_p), make_full_box("iloc", 1, iloc_p)}) {
      meta_children.insert(meta_children.end(), c.begin(), c.end());
    }
    std::vector<uint8_t> out = make_box("ftyp", ftyp_p);
    auto meta = make_full_box("meta", 0, meta_children);
    auto mdat =
        make_box("mdat", std::vector<uint8_t>(av1_pixels.begin(), av1_pixels.end()));
    out.insert(out.end(), meta.begin(), meta.end());
    out.insert(out.end(), mdat.begin(), mdat.end());
    return out;
  };
  std::vector<uint8_t> probe = build_avif(0);
  std::vector<uint8_t> avif_still =
      build_avif(static_cast<uint32_t>(probe.size() - av1_pixels.size()));
  const std::string avif_still_path = (test_dir / "still.avif").string();
  const std::string avif_mp_path = (test_dir / "built.MP.avif").string();
  const std::string avif_vid_path =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  {
    std::ofstream out(avif_still_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(avif_still.data()),
              avif_still.size());
  }
  ASSERT_EQ(BuildMotionPhotoFromFile(avif_still_path, avif_vid_path,
                                     avif_mp_path, 42000),
            0);
  EXPECT_EQ(CheckMotionPhotoFromFile(avif_mp_path), 0);

  std::ifstream mpin(avif_mp_path, std::ios::binary);
  std::vector<uint8_t> mp_bytes((std::istreambuf_iterator<char>(mpin)),
                                std::istreambuf_iterator<char>());
  MotionPhotoMetadata file_meta;
  MotionPhotoMetadata mem_meta;
  MotionPhotoMetadata fd_meta;
  ASSERT_TRUE(ParseMotionPhotoFromFile(avif_mp_path, &file_meta));
  ASSERT_TRUE(
      ParseMotionPhotoFromMemory(mp_bytes.data(), mp_bytes.size(), &mem_meta));
  int fd = open(avif_mp_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  ASSERT_TRUE(ParseMotionPhotoFromFd(
      fd, 0, static_cast<int64_t>(mp_bytes.size()), &fd_meta));
  EXPECT_EQ(
      CheckMotionPhotoFromFd(fd, 0, static_cast<int64_t>(mp_bytes.size())),
      0);
  close(fd);
  EXPECT_EQ(CheckMotionPhotoFromMemory(mp_bytes.data(), mp_bytes.size()), 0);

  for (const MotionPhotoMetadata& meta : {file_meta, mem_meta, fd_meta}) {
    EXPECT_TRUE(meta.is_motion_photo);
    EXPECT_EQ(meta.presentation_timestamp_us, 42000);
    EXPECT_EQ(meta.primary_image_mime, "image/avif");
    EXPECT_EQ(meta.image_padding, 16);
    ASSERT_TRUE(meta.container.items.size() >= 2u);
    EXPECT_EQ(meta.container.items[0].mime, "image/avif");
    EXPECT_EQ(meta.container.items[0].padding, 16);
  }

  const std::string avif_extracted = (test_dir / "extracted.avif").string();
  ASSERT_TRUE(ExtractPrimaryImageFromFile(avif_mp_path, avif_extracted));
  MotionPhotoMetadata extracted_meta;
  EXPECT_FALSE(ParseMotionPhotoFromFile(avif_extracted, &extracted_meta) &&
               extracted_meta.is_motion_photo);

  // 2. Multi-extent XMP HEIC parsed via memory/fd and stripped via API.
  const std::string ext1 =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
      "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description "
      "xmlns:Camera=\"http://ns.google.com/photos/1.0/camera/\" "
      "xmlns:Container=\"http://ns.google.com/photos/1.0/container/\" "
      "xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\" "
      "Camera:MotionPhoto=\"1\" Camera:MotionPhotoVersion=\"1\" "
      "Camera:MotionPhotoPresentationTimestampUs=\"777\">"
      "<Container:Directory><rdf:Seq>";
  const std::string img_gap = "MULTI_EXTENT_HEVC_PIXELS";
  std::ifstream vid_in(avif_vid_path, std::ios::binary);
  std::vector<uint8_t> vid_bytes((std::istreambuf_iterator<char>(vid_in)),
                                 std::istreambuf_iterator<char>());
  const std::string ext2 =
      "<rdf:li><Container:Item Item:Semantic=\"Primary\" Item:Mime=\"image/heic\" "
      "Item:Padding=\"8\"/></rdf:li>"
      "<rdf:li><Container:Item Item:Semantic=\"MotionPhoto\" "
      "Item:Mime=\"video/mp4\" Item:Length=\"" +
      std::to_string(vid_bytes.size()) +
      "\"/></rdf:li></rdf:Seq></Container:Directory>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>";

  std::vector<uint8_t> mdat_p;
  put_str(&mdat_p, ext1);
  put_str(&mdat_p, img_gap);
  put_str(&mdat_p, ext2);
  auto mpvd_box = make_box("mpvd", vid_bytes);
  auto build_multi = [&](uint32_t mdat_off) {
    std::vector<uint8_t> ftyp_p;
    put_str(&ftyp_p, "heic");
    put(&ftyp_p, 0, 4);
    put_str(&ftyp_p, "mif1heic");
    std::vector<uint8_t> hdlr_p;
    put(&hdlr_p, 0, 4);
    put_str(&hdlr_p, "pict");
    hdlr_p.insert(hdlr_p.end(), 13, 0);
    std::vector<uint8_t> pitm_p;
    put(&pitm_p, 1, 2);
    std::vector<uint8_t> infe1_p;
    put(&infe1_p, 1, 2);
    put(&infe1_p, 0, 2);
    put_str(&infe1_p, "hvc1");
    infe1_p.push_back(0);
    std::vector<uint8_t> infe2_p;
    put(&infe2_p, 2, 2);
    put(&infe2_p, 0, 2);
    put_str(&infe2_p, "mime");
    infe2_p.push_back(0);
    put_str(&infe2_p, "application/rdf+xml");
    infe2_p.push_back(0);
    std::vector<uint8_t> iinf_p;
    put(&iinf_p, 2, 2);
    auto infe1 = make_full_box("infe", 2, infe1_p);
    auto infe2 = make_full_box("infe", 2, infe2_p);
    iinf_p.insert(iinf_p.end(), infe1.begin(), infe1.end());
    iinf_p.insert(iinf_p.end(), infe2.begin(), infe2.end());
    std::vector<uint8_t> cdsc_p;
    put(&cdsc_p, 2, 2);
    put(&cdsc_p, 1, 2);
    put(&cdsc_p, 1, 2);
    std::vector<uint8_t> iloc_p;
    put(&iloc_p, 0x4400, 2);
    put(&iloc_p, 2, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, mdat_off + ext1.size(), 4);
    put(&iloc_p, img_gap.size(), 4);
    put(&iloc_p, 2, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 2, 2);
    put(&iloc_p, mdat_off, 4);
    put(&iloc_p, ext1.size(), 4);
    put(&iloc_p, mdat_off + ext1.size() + img_gap.size(), 4);
    put(&iloc_p, ext2.size(), 4);
    std::vector<uint8_t> meta_children;
    for (const auto& c :
         {make_full_box("hdlr", 0, hdlr_p), make_full_box("pitm", 0, pitm_p),
          make_full_box("iinf", 0, iinf_p),
          make_full_box("iref", 0, make_box("cdsc", cdsc_p)),
          make_full_box("iloc", 1, iloc_p)}) {
      meta_children.insert(meta_children.end(), c.begin(), c.end());
    }
    std::vector<uint8_t> out = make_box("ftyp", ftyp_p);
    auto meta = make_full_box("meta", 0, meta_children);
    auto mdat = make_box("mdat", mdat_p);
    out.insert(out.end(), meta.begin(), meta.end());
    out.insert(out.end(), mdat.begin(), mdat.end());
    out.insert(out.end(), mpvd_box.begin(), mpvd_box.end());
    return out;
  };
  std::vector<uint8_t> multi_probe = build_multi(0);
  std::vector<uint8_t> multi_heic = build_multi(static_cast<uint32_t>(
      multi_probe.size() - mpvd_box.size() - mdat_p.size()));

  MotionPhotoMetadata multi_mem_meta;
  ASSERT_TRUE(ParseMotionPhotoFromMemory(multi_heic.data(), multi_heic.size(),
                                         &multi_mem_meta));
  EXPECT_TRUE(multi_mem_meta.is_motion_photo);
  EXPECT_EQ(multi_mem_meta.presentation_timestamp_us, 777);
  EXPECT_EQ(multi_mem_meta.primary_image_mime, "image/heic");
  EXPECT_EQ(multi_mem_meta.image_padding, 8);
  EXPECT_EQ(multi_mem_meta.video_length, static_cast<int64_t>(vid_bytes.size()));
  EXPECT_EQ(
      CheckMotionPhotoFromMemory(multi_heic.data(), multi_heic.size()), 0);

  const std::string multi_path = (test_dir / "multi_ext.MP.heic").string();
  const std::string multi_still = (test_dir / "multi_still.heic").string();
  const std::string multi_xmp_out = (test_dir / "multi_meta.xml").string();
  {
    std::ofstream out(multi_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(multi_heic.data()),
              multi_heic.size());
  }
  int multi_fd = open(multi_path.c_str(), O_RDONLY);
  ASSERT_TRUE(multi_fd >= 0);
  MotionPhotoMetadata multi_fd_meta;
  ASSERT_TRUE(ParseMotionPhotoFromFd(
      multi_fd, 0, static_cast<int64_t>(multi_heic.size()), &multi_fd_meta));
  EXPECT_EQ(CheckMotionPhotoFromFd(
                multi_fd, 0, static_cast<int64_t>(multi_heic.size())),
            0);
  close(multi_fd);
  EXPECT_EQ(CheckMotionPhotoFromFile(multi_path), 0);
  EXPECT_TRUE(multi_fd_meta.is_motion_photo);
  EXPECT_EQ(multi_fd_meta.primary_image_mime, "image/heic");
  EXPECT_EQ(multi_fd_meta.video_length, static_cast<int64_t>(vid_bytes.size()));

  ASSERT_TRUE(ExtractMotionPhotoFromFile(
      ExtractorParams{multi_path, "", "", multi_xmp_out}));
  {
    std::ifstream xin(multi_xmp_out, std::ios::binary);
    std::string xmp_str((std::istreambuf_iterator<char>(xin)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(xmp_str, ext1 + ext2);
  }
  ASSERT_TRUE(ExtractPrimaryImageFromMemory(multi_heic.data(),
                                            multi_heic.size(), multi_still));
  const std::string multi_still_fd =
      (test_dir / "multi_still_fd.heic").string();
  const std::string multi_still_file =
      (test_dir / "multi_still_file.heic").string();
  multi_fd = open(multi_path.c_str(), O_RDONLY);
  ASSERT_TRUE(multi_fd >= 0);
  ASSERT_TRUE(ExtractPrimaryImageFromFd(
      multi_fd, 0, static_cast<int64_t>(multi_heic.size()), multi_still_fd));
  close(multi_fd);
  ASSERT_TRUE(ExtractPrimaryImageFromFile(multi_path, multi_still_file));
  {
    auto read_all = [](const std::string& p) {
      std::ifstream in(p, std::ios::binary);
      return std::string((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    };
    const std::string mem_bytes = read_all(multi_still);
    EXPECT_EQ(mem_bytes, read_all(multi_still_fd));
    EXPECT_EQ(mem_bytes, read_all(multi_still_file));
  }
  MotionPhotoMetadata multi_still_meta;
  EXPECT_FALSE(ParseMotionPhotoFromFile(multi_still, &multi_still_meta) &&
               multi_still_meta.is_motion_photo);

  fs::remove_all(test_dir);
}

TEST(LibMotionPhotoApiTest, LargeFdOverreadXmlLangAndParityRegression) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / "LargeFdOverreadXmlLangAndParity";
  fs::create_directories(test_dir);

  auto put = [](std::vector<uint8_t>* out, uint64_t val, size_t n) {
    for (size_t i = n; i > 0; --i) {
      size_t shift = 8 * (i - 1);
      out->push_back(
          static_cast<uint8_t>(shift >= 64 ? 0 : ((val >> shift) & 0xFF)));
    }
  };
  auto put_str = [](std::vector<uint8_t>* out, const std::string& s) {
    out->insert(out->end(), s.begin(), s.end());
  };
  auto make_box = [&](const std::string& type, const std::vector<uint8_t>& p) {
    std::vector<uint8_t> b;
    put(&b, 8 + p.size(), 4);
    put_str(&b, type);
    b.insert(b.end(), p.begin(), p.end());
    return b;
  };
  auto make_full_box = [&](const std::string& type, uint8_t ver,
                           const std::vector<uint8_t>& p) {
    std::vector<uint8_t> fp;
    fp.push_back(ver);
    put(&fp, 0, 3);
    fp.insert(fp.end(), p.begin(), p.end());
    return make_box(type, fp);
  };

  std::ifstream vid_in("lib/core/motion_photo/testdata/motion_photo_video_only.mp4",
                       std::ios::binary);
  std::vector<uint8_t> vid_bytes((std::istreambuf_iterator<char>(vid_in)),
                                 std::istreambuf_iterator<char>());
  auto mpvd_box = make_box("mpvd", vid_bytes);

  auto build_large_heic = [&](const std::string& xmp) {
    const std::string big_pixels(280 * 1024, '\xAA');
    std::vector<uint8_t> mdat_p;
    put_str(&mdat_p, big_pixels);
    put_str(&mdat_p, xmp);

    auto make_container = [&](uint32_t mdat_data_off) {
      std::vector<uint8_t> ftyp_p;
      put_str(&ftyp_p, "heic");
      put(&ftyp_p, 0, 4);
      put_str(&ftyp_p, "mif1heic");
      std::vector<uint8_t> hdlr_p;
      put(&hdlr_p, 0, 4);
      put_str(&hdlr_p, "pict");
      hdlr_p.insert(hdlr_p.end(), 13, 0);
      std::vector<uint8_t> pitm_p;
      put(&pitm_p, 1, 2);
      std::vector<uint8_t> infe1_p;
      put(&infe1_p, 1, 2);
      put(&infe1_p, 0, 2);
      put_str(&infe1_p, "hvc1");
      infe1_p.push_back(0);
      std::vector<uint8_t> infe2_p;
      put(&infe2_p, 2, 2);
      put(&infe2_p, 0, 2);
      put_str(&infe2_p, "mime");
      infe2_p.push_back(0);
      put_str(&infe2_p, "application/rdf+xml");
      infe2_p.push_back(0);
      std::vector<uint8_t> iinf_p;
      put(&iinf_p, 2, 2);
      auto infe1 = make_full_box("infe", 2, infe1_p);
      auto infe2 = make_full_box("infe", 2, infe2_p);
      iinf_p.insert(iinf_p.end(), infe1.begin(), infe1.end());
      iinf_p.insert(iinf_p.end(), infe2.begin(), infe2.end());
      std::vector<uint8_t> cdsc_p;
      put(&cdsc_p, 2, 2);
      put(&cdsc_p, 1, 2);
      put(&cdsc_p, 1, 2);
      std::vector<uint8_t> iloc_p;
      put(&iloc_p, 0x4400, 2);
      put(&iloc_p, 2, 2);
      put(&iloc_p, 1, 2);
      put(&iloc_p, 0, 2);
      put(&iloc_p, 0, 2);
      put(&iloc_p, 1, 2);
      put(&iloc_p, mdat_data_off, 4);
      put(&iloc_p, big_pixels.size(), 4);
      put(&iloc_p, 2, 2);
      put(&iloc_p, 0, 2);
      put(&iloc_p, 0, 2);
      put(&iloc_p, 1, 2);
      put(&iloc_p, mdat_data_off + big_pixels.size(), 4);
      put(&iloc_p, xmp.size(), 4);
      std::vector<uint8_t> meta_children;
      for (const auto& c :
           {make_full_box("hdlr", 0, hdlr_p), make_full_box("pitm", 0, pitm_p),
            make_full_box("iinf", 0, iinf_p),
            make_full_box("iref", 0, make_box("cdsc", cdsc_p)),
            make_full_box("iloc", 1, iloc_p)}) {
        meta_children.insert(meta_children.end(), c.begin(), c.end());
      }
      std::vector<uint8_t> out = make_box("ftyp", ftyp_p);
      auto meta = make_full_box("meta", 0, meta_children);
      auto mdat = make_box("mdat", mdat_p);
      out.insert(out.end(), meta.begin(), meta.end());
      out.insert(out.end(), mdat.begin(), mdat.end());
      out.insert(out.end(), mpvd_box.begin(), mpvd_box.end());
      return out;
    };
    auto probe = make_container(0);
    return make_container(static_cast<uint32_t>(
        probe.size() - mpvd_box.size() - mdat_p.size()));
  };

  // XMP starts >280KB and immediately precedes 'mpvd', plus uses xml:lang.
  const std::string valid_xmp =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
      "xmlns:Camera=\"http://ns.google.com/photos/1.0/camera/\" "
      "xmlns:Container=\"http://ns.google.com/photos/1.0/container/\" "
      "xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\" "
      "Camera:MotionPhoto=\"1\" Camera:MotionPhotoVersion=\"1\" "
      "Camera:MotionPhotoPresentationTimestampUs=\"4242\">"
      "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">"
      "Title</rdf:li></rdf:Alt></dc:description>"
      "<Container:Directory><rdf:Seq>"
      "<rdf:li><Container:Item Item:Semantic=\"Primary\" "
      "Item:Mime=\"image/heic\" Item:Padding=\"8\"/></rdf:li>"
      "<rdf:li><Container:Item Item:Semantic=\"MotionPhoto\" "
      "Item:Mime=\"video/mp4\" Item:Length=\"" +
      std::to_string(vid_bytes.size()) +
      "\"/></rdf:li>"
      "</rdf:Seq></Container:Directory>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>";

  std::vector<uint8_t> valid_large = build_large_heic(valid_xmp);
  const std::string large_path = (test_dir / "large_xmllang.MP.heic").string();
  {
    std::ofstream out(large_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(valid_large.data()),
              valid_large.size());
  }

  int fd = open(large_path.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);
  MotionPhotoMetadata fd_meta;
  ASSERT_TRUE(ParseMotionPhotoFromFd(
      fd, 0, static_cast<int64_t>(valid_large.size()), &fd_meta));
  EXPECT_TRUE(fd_meta.is_motion_photo);
  EXPECT_EQ(fd_meta.presentation_timestamp_us, 4242);
  EXPECT_EQ(CheckMotionPhotoFromFd(
                fd, 0, static_cast<int64_t>(valid_large.size())),
            0);
  close(fd);
  EXPECT_EQ(CheckMotionPhotoFromFile(large_path), 0);
  EXPECT_EQ(
      CheckMotionPhotoFromMemory(valid_large.data(), valid_large.size()), 0);

  // Invalid padding fails CheckMotionPhoto consistently across File/Fd/Memory.
  std::string bad_padding_xmp = valid_xmp;
  size_t pad_pos = bad_padding_xmp.find("Item:Padding=\"8\"");
  ASSERT_NE(pad_pos, std::string::npos);
  bad_padding_xmp.replace(pad_pos, 16, "Item:Padding=\"9\"");
  std::vector<uint8_t> invalid_large = build_large_heic(bad_padding_xmp);
  const std::string bad_path = (test_dir / "bad_padding.MP.heic").string();
  {
    std::ofstream out(bad_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(invalid_large.data()),
              invalid_large.size());
  }
  int bad_fd = open(bad_path.c_str(), O_RDONLY);
  ASSERT_TRUE(bad_fd >= 0);
  EXPECT_NE(CheckMotionPhotoFromFile(bad_path), 0);
  EXPECT_NE(CheckMotionPhotoFromFd(
                bad_fd, 0, static_cast<int64_t>(invalid_large.size())),
            0);
  close(bad_fd);
  EXPECT_NE(
      CheckMotionPhotoFromMemory(invalid_large.data(), invalid_large.size()),
      0);

  // ExtractMotionPhoto File/Fd/Memory must produce byte-for-byte identical
  // still image and stripped MP4 video.
  const std::string ref_heic =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  const size_t ref_size = fs::file_size(ref_heic);
  std::ifstream ref_in(ref_heic, std::ios::binary);
  std::vector<uint8_t> ref_buf(ref_size);
  ASSERT_TRUE(ref_in.read(reinterpret_cast<char*>(ref_buf.data()), ref_size));

  auto read_bytes = [](const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  };
  ExtractorParams p_file{ref_heic,
                         (test_dir / "out_file.heic").string(),
                         (test_dir / "out_file.mp4").string(),
                         (test_dir / "out_file.xml").string()};
  ExtractorParams p_fd{ref_heic,
                       (test_dir / "out_fd.heic").string(),
                       (test_dir / "out_fd.mp4").string(),
                       (test_dir / "out_fd.xml").string()};
  ExtractorParams p_mem{ref_heic,
                        (test_dir / "out_mem.heic").string(),
                        (test_dir / "out_mem.mp4").string(),
                        (test_dir / "out_mem.xml").string()};
  ASSERT_TRUE(ExtractMotionPhotoFromFile(p_file));
  int ref_fd = open(ref_heic.c_str(), O_RDONLY);
  ASSERT_TRUE(ref_fd >= 0);
  ASSERT_TRUE(ExtractMotionPhotoFromFd(ref_fd, 0, ref_size, p_fd));
  close(ref_fd);
  ASSERT_TRUE(
      ExtractMotionPhotoFromMemory(ref_buf.data(), ref_buf.size(), p_mem));

  EXPECT_EQ(read_bytes(p_file.primary_image_output_path),
            read_bytes(p_fd.primary_image_output_path));
  EXPECT_EQ(read_bytes(p_fd.primary_image_output_path),
            read_bytes(p_mem.primary_image_output_path));
  EXPECT_EQ(read_bytes(p_file.video_output_path),
            read_bytes(p_fd.video_output_path));
  EXPECT_EQ(read_bytes(p_fd.video_output_path),
            read_bytes(p_mem.video_output_path));
  EXPECT_EQ(read_bytes(p_file.metadata_output_path),
            read_bytes(p_fd.metadata_output_path));
  EXPECT_EQ(read_bytes(p_fd.metadata_output_path),
            read_bytes(p_mem.metadata_output_path));

  // JPEG ExtractMotionPhoto File/Fd/Memory parity (single StripMetadataTrack).
  const std::string ref_jpg =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  const size_t jpg_size = fs::file_size(ref_jpg);
  std::ifstream jpg_in(ref_jpg, std::ios::binary);
  std::vector<uint8_t> jpg_buf(jpg_size);
  ASSERT_TRUE(jpg_in.read(reinterpret_cast<char*>(jpg_buf.data()), jpg_size));
  ExtractorParams jp_file{ref_jpg,
                          (test_dir / "jpg_file.jpg").string(),
                          (test_dir / "jpg_file.mp4").string(),
                          ""};
  ExtractorParams jp_fd{ref_jpg,
                        (test_dir / "jpg_fd.jpg").string(),
                        (test_dir / "jpg_fd.mp4").string(),
                        ""};
  ExtractorParams jp_mem{ref_jpg,
                         (test_dir / "jpg_mem.jpg").string(),
                         (test_dir / "jpg_mem.mp4").string(),
                         ""};
  ASSERT_TRUE(ExtractMotionPhotoFromFile(jp_file));
  int jpg_fd = open(ref_jpg.c_str(), O_RDONLY);
  ASSERT_TRUE(jpg_fd >= 0);
  ASSERT_TRUE(ExtractMotionPhotoFromFd(jpg_fd, 0, jpg_size, jp_fd));
  close(jpg_fd);
  ASSERT_TRUE(
      ExtractMotionPhotoFromMemory(jpg_buf.data(), jpg_buf.size(), jp_mem));
  EXPECT_EQ(read_bytes(jp_file.primary_image_output_path),
            read_bytes(jp_fd.primary_image_output_path));
  EXPECT_EQ(read_bytes(jp_fd.primary_image_output_path),
            read_bytes(jp_mem.primary_image_output_path));
  EXPECT_EQ(read_bytes(jp_file.video_output_path),
            read_bytes(jp_fd.video_output_path));
  EXPECT_EQ(read_bytes(jp_fd.video_output_path),
            read_bytes(jp_mem.video_output_path));

  // SniffFileType must not scan past a truncated non-zero 'ftyp' box_size < 16.
  const uint8_t truncated_mif1[] = {0, 0, 0, 12, 'f', 't', 'y', 'p',
                                    'm', 'i', 'f', '1', 0, 0, 0, 0,
                                    'a', 'v', 'i', 'f'};
  EXPECT_EQ(
      static_cast<int>(
          motion_photo::SniffFileType(truncated_mif1, sizeof(truncated_mif1))),
      static_cast<int>(motion_photo::FileType::kHeic));

  // PopulateNativeMetadata selects semantic MotionPhoto item even when GainMap
  // precedes it in Container:Directory.
  const std::string gainmap_first_xmp =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description rdf:about=\"\" "
      "xmlns:Camera=\"http://ns.google.com/photos/1.0/camera/\" "
      "xmlns:Container=\"http://ns.google.com/photos/1.0/container/\" "
      "xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\" "
      "Camera:MotionPhoto=\"1\" Camera:MotionPhotoVersion=\"1\" "
      "Camera:MotionPhotoPresentationTimestampUs=\"100\">"
      "<Container:Directory><rdf:Seq>"
      "<rdf:li><Container:Item Item:Semantic=\"Primary\" "
      "Item:Mime=\"image/heic\" Item:Padding=\"8\"/></rdf:li>"
      "<rdf:li><Container:Item Item:Semantic=\"GainMap\" "
      "Item:Mime=\"image/jpeg\" Item:Length=\"123\"/></rdf:li>"
      "<rdf:li><Container:Item Item:Semantic=\"MotionPhoto\" "
      "Item:Mime=\"video/mp4\" Item:Length=\"" +
      std::to_string(vid_bytes.size()) +
      "\"/></rdf:li>"
      "</rdf:Seq></Container:Directory>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>";
  std::vector<uint8_t> gainmap_first = build_large_heic(gainmap_first_xmp);
  MotionPhotoMetadata gm_meta;
  ASSERT_TRUE(ParseMotionPhotoFromMemory(gainmap_first.data(),
                                         gainmap_first.size(), &gm_meta));
  EXPECT_EQ(gm_meta.video_mime, "video/mp4");
  EXPECT_EQ(gm_meta.video_length, static_cast<int64_t>(vid_bytes.size()));

  // ReadDataRangesToString rejects ranges whose total length overflows size_t.
  {
    const std::vector<uint8_t> small_bytes(8, 'x');
    auto small_seg = image_io::DataSegment::Create(
        image_io::DataRange(0, small_bytes.size()), small_bytes.data(),
        image_io::DataSegment::BufferDispositionPolicy::kDontDelete);
    image_io::DataSegmentDataSource small_ds(small_seg);
    std::string combined = "stale";
    EXPECT_FALSE(motion_photo::ReadDataRangesToString(
        &small_ds,
        {image_io::DataRange(0, 1),
         image_io::DataRange(0, std::numeric_limits<size_t>::max())},
        &combined));
    EXPECT_TRUE(combined.empty());
  }

  fs::remove_all(test_dir);
}

}  // namespace api
}  // namespace libmotionphoto
