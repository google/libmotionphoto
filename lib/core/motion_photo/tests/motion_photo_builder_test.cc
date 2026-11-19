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

#include <absl/strings/match.h>
#include <absl/strings/string_view.h>
#if defined(MOTION_PHOTO_HEIC_SUPPORT)
#include <libheif/heif.h>  // IWYU pragma: keep
#if __has_include(<libheif/heif_decoding.h>)
#include <libheif/heif_decoding.h>
#endif
#if __has_include(<libheif/heif_encoding.h>)
#include <libheif/heif_encoding.h>
#endif
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "image_io/mpf/mpf_info.h"
#include "image_io/mpf/mpf_info_decoder.h"
#include "image_io/mpf/mpf_info_encoder.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_reader.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/motion_photo_writer.h"
#include "motion_photo/tests/test_framework.h"
#include "motion_photo/video_track_data.h"
#include "motion_photo_builder/function.h"
#include "motion_photo_checker/function.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_jpeg/motion_photo_jpeg_image_builder.h"
#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"
#include "motion_photo_mp4/mp4_box_reader.h"
#include "motion_photo_mp4/mp4_mett_box_injector.h"

namespace libmotionphoto {
namespace motion_photo {

namespace fs = std::filesystem;
using std::string;

namespace {

bool HasHevcSupport() {
#if defined(MOTION_PHOTO_HEIC_SUPPORT)
  return heif_have_decoder_for_format(heif_compression_HEVC) &&
         heif_have_encoder_for_format(heif_compression_HEVC);
#else
  return false;
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)
}

string CreateTestDir(const std::string& subdir_name) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / subdir_name;
  fs::create_directories(test_dir);
  return test_dir.string();
}

std::vector<uint8_t> ReadFileBytes(const std::string& file_path) {
  std::ifstream f(file_path, std::ios::binary | std::ios::ate);
  if (!f) return {};
  size_t sz = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(sz);
  f.read(reinterpret_cast<char*>(bytes.data()), sz);
  return bytes;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. CLI Parameter Validation and Help
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, CLI_ArgumentValidationAndHelp) {
  // Case A: No arguments
  {
    bool has_usage = false;
    const char* argv[] = {"MotionPhotoBuilder"};
    int status = MotionPhotoBuilderFunction(1, argv, [&](absl::string_view str) {
      if (absl::StrContains(str, "Usage")) has_usage = true;
    });
    EXPECT_NE(status, 0);
    EXPECT_TRUE(has_usage);
  }

  // Case B: Help flag (-h)
  {
    bool has_usage = false;
    const char* argv[] = {"MotionPhotoBuilder", "-h"};
    int status = MotionPhotoBuilderFunction(2, argv, [&](absl::string_view str) {
      if (absl::StrContains(str, "Usage")) has_usage = true;
    });
    EXPECT_NE(status, 0);
    EXPECT_TRUE(has_usage);
  }

  // Case C: Invalid flag (-j)
  {
    bool has_usage = false;
    const char* argv[] = {"MotionPhotoBuilder", "-j"};
    int status = MotionPhotoBuilderFunction(2, argv, [&](absl::string_view str) {
      if (absl::StrContains(str, "Usage")) has_usage = true;
    });
    EXPECT_NE(status, 0);
    EXPECT_TRUE(has_usage);
  }
}

// -----------------------------------------------------------------------------
// 2. CLI Dry-Run and Check Modes (without -oi output arg)
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, CLI_DryRunCheckMode_JpegAndHeic) {
  const string kTestDir = CreateTestDir("CLI_DryRunCheckMode");
  const string kMomentsVideoMetadataFile = "lib/core/motion_photo/testdata/MomentsVideoMetadata.xmp";
  const string kWorkVideoFile = (fs::path(kTestDir) / "WorkVideo.mp4").string();
  const string kOptWorkVideoFile = (fs::path(kTestDir) / "OptWorkVideo.mp4").string();

  // Case A: JPEG dry-run check mode
  {
    const string kJpegImage = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
    const string kJpegVideo = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
    const char* argv[] = {"MotionPhotoBuilder",
                          "-pi", kJpegImage.c_str(),
                          "-pv", kJpegVideo.c_str(),
                          "-mv", kJpegVideo.c_str(),
                          "-mx", kMomentsVideoMetadataFile.c_str(),
                          "-wv", kWorkVideoFile.c_str(),
                          "-wo", kOptWorkVideoFile.c_str()};

    bool has_zero_errors = false;
    bool has_one_warning = false;
    int status = MotionPhotoBuilderFunction(13, argv, [&](absl::string_view str) {
      if (absl::StrContains(str, "0 errors")) has_zero_errors = true;
      if (absl::StrContains(str, "1 warning")) has_one_warning = true;
    });
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(has_zero_errors);
    EXPECT_TRUE(has_one_warning);
  }

  // Case B: HEIC dry-run check mode
  {
    const string kHeicImage = "lib/core/motion_photo/testdata/motion_photo_photo_only.heic";
    const string kHeicVideo = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
    const char* argv[] = {"MotionPhotoBuilder",
                          "-pi", kHeicImage.c_str(),
                          "-pv", kHeicVideo.c_str(),
                          "-mv", kHeicVideo.c_str(),
                          "-mx", kMomentsVideoMetadataFile.c_str(),
                          "-wv", kWorkVideoFile.c_str(),
                          "-wo", kOptWorkVideoFile.c_str()};

    bool has_zero_errors = false;
    bool has_one_warning = false;
    int status = MotionPhotoBuilderFunction(13, argv, [&](absl::string_view str) {
      if (absl::StrContains(str, "0 errors")) has_zero_errors = true;
      if (absl::StrContains(str, "1 warning")) has_one_warning = true;
    });
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(has_zero_errors);
    EXPECT_TRUE(has_one_warning);
  }

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 3. CLI Full End-to-End Workflow: Build, Check, Extract, and MP4 Demux
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, CLI_JpegFullRoundtrip_BuildCheckExtract) {
  const string kTestDir = CreateTestDir("CLI_JpegFullRoundtrip");
  const string kPrimaryImageFile = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kPrimaryVideoFile = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const string kMomentsVideoMetadataFile = "lib/core/motion_photo/testdata/MomentsVideoMetadata.xmp";
  const string kWorkVideoFile = (fs::path(kTestDir) / "WorkVideo.mp4").string();
  const string kOptWorkVideoFile = (fs::path(kTestDir) / "OptWorkVideo.mp4").string();
  const string kBuiltMotionPhoto = (fs::path(kTestDir) / "built_motion_photo.jpg").string();

  // Step 1: Build Motion Photo via CLI
  const char* build_argv[] = {"MotionPhotoBuilder",
                              "-pi", kPrimaryImageFile.c_str(),
                              "-pv", kPrimaryVideoFile.c_str(),
                              "-mv", kPrimaryVideoFile.c_str(),
                              "-mx", kMomentsVideoMetadataFile.c_str(),
                              "-wv", kWorkVideoFile.c_str(),
                              "-wo", kOptWorkVideoFile.c_str(),
                              "-oi", kBuiltMotionPhoto.c_str()};
  bool has_zero_errors = false;
  int build_status = MotionPhotoBuilderFunction(15, build_argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "0 errors")) has_zero_errors = true;
  });
  EXPECT_EQ(build_status, 0);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kBuiltMotionPhoto));
  EXPECT_TRUE(fs::file_size(kBuiltMotionPhoto) > 0);

  // Step 2: Validate built Motion Photo with Checker
  int check_status = CheckMotionPhoto(kBuiltMotionPhoto, [](absl::string_view) {});
  EXPECT_EQ(check_status, 0);

  // Step 3: Extract from the built Motion Photo
  const string kExtractedImage = (fs::path(kTestDir) / "extracted_still.jpg").string();
  const string kExtractedVideo = (fs::path(kTestDir) / "extracted_video.mp4").string();
  const string kExtractedMetadata = (fs::path(kTestDir) / "extracted_meta.xml").string();
  const char* extract_argv[] = {
      "MotionPhotoExtractor",
      "-mi", kBuiltMotionPhoto.c_str(),
      "-po", kExtractedImage.c_str(),
      "-vo", kExtractedVideo.c_str(),
      "-mo", kExtractedMetadata.c_str()};
  int extract_status = MotionPhotoExtractorFunction(9, extract_argv, [](absl::string_view) {});
  EXPECT_EQ(extract_status, 0);
  EXPECT_TRUE(fs::exists(kExtractedImage));
  EXPECT_TRUE(fs::exists(kExtractedVideo));
  EXPECT_TRUE(fs::file_size(kExtractedImage) > 0);
  EXPECT_TRUE(fs::file_size(kExtractedVideo) > 0);

  // Step 4: Verify extracted video integrity via MP4 reader
  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kExtractedVideo, 0));

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 4. CLI High-Res Multi-Track MP4 Build
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, CLI_HighResMultiTrackBuild) {
  const string kTestDir = CreateTestDir("CLI_HighResMultiTrackBuild");
  const string kPrimaryImageFile = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kPrimaryVideoFile = "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  const string kMomentsVideoMetadataFile = "lib/core/motion_photo/testdata/MomentsVideoMetadata.xmp";
  const string kWorkVideoFile = (fs::path(kTestDir) / "WorkVideo.mp4").string();
  const string kOptWorkVideoFile = (fs::path(kTestDir) / "OptWorkVideo.mp4").string();
  const string kOutputFile = (fs::path(kTestDir) / "output_high_res.jpg").string();

  const char* argv[] = {"MotionPhotoBuilder",
                        "-pi", kPrimaryImageFile.c_str(),
                        "-pv", kPrimaryVideoFile.c_str(),
                        "-mv", kPrimaryVideoFile.c_str(),
                        "-mx", kMomentsVideoMetadataFile.c_str(),
                        "-wv", kWorkVideoFile.c_str(),
                        "-wo", kOptWorkVideoFile.c_str(),
                        "-oi", kOutputFile.c_str()};

  bool has_zero_errors = false;
  bool has_zero_warnings = false;
  int status = MotionPhotoBuilderFunction(15, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "0 errors")) has_zero_errors = true;
    if (absl::StrContains(str, "0 warnings")) has_zero_warnings = true;
  });
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(has_zero_warnings);
  EXPECT_TRUE(fs::exists(kOutputFile));

  int check_status = CheckMotionPhoto(kOutputFile, [](absl::string_view) {});
  EXPECT_EQ(check_status, 0);

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 5. CLI HEIC Motion Photo Build
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, CLI_HeicBuild) {
  if (!HasHevcSupport()) {
    std::cout << "Skipping CLI_HeicBuild test because HEVC codec is not supported" << std::endl;
    return;
  }
  const string kTestDir = CreateTestDir("CLI_HeicBuild");
  const string kPrimaryImageFile = "lib/core/motion_photo/testdata/motion_photo_photo_only.heic";
  const string kPrimaryVideoFile = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const string kMomentsVideoMetadataFile = "lib/core/motion_photo/testdata/MomentsVideoMetadata.xmp";
  const string kWorkVideoFile = (fs::path(kTestDir) / "WorkVideo.mp4").string();
  const string kOptWorkVideoFile = (fs::path(kTestDir) / "OptWorkVideo.mp4").string();
  const string kOutputFile = (fs::path(kTestDir) / "output.heic").string();

  const char* argv[] = {"MotionPhotoBuilder",
                        "-pi", kPrimaryImageFile.c_str(),
                        "-pv", kPrimaryVideoFile.c_str(),
                        "-mv", kPrimaryVideoFile.c_str(),
                        "-mx", kMomentsVideoMetadataFile.c_str(),
                        "-wv", kWorkVideoFile.c_str(),
                        "-wo", kOptWorkVideoFile.c_str(),
                        "-oi", kOutputFile.c_str()};

  bool has_zero_errors = false;
  int status = MotionPhotoBuilderFunction(15, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "0 errors")) has_zero_errors = true;
  });
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kOutputFile));

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 6. Direct Streaming vs In-Memory Parity for Standard SDR JPEG
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, StreamingAndMemory_SdrJpeg_BitExactParity) {
  const string kTestDir = CreateTestDir("StreamingAndMemory_SdrJpeg");
  const string kInputImage = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kInputVideo = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const string kFileOut = (fs::path(kTestDir) / "file_built.MP.jpg").string();
  const string kLegacyFileOut = (fs::path(kTestDir) / "legacy_file.MP.jpg").string();
  const string kLegacyMemOutFile = (fs::path(kTestDir) / "legacy_mem.MP.jpg").string();

  // 1. File-based direct streaming build with timestamp
  {
    auto img_src = MediaSource::FromFile(kInputImage, nullptr);
    auto vid_src = MediaSource::FromFile(kInputVideo, nullptr);
    auto file_sink = DataSink::ToFile(kFileOut, nullptr);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, file_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(fs::exists(kFileOut));
  }

  // 2. Pure in-memory direct streaming build with timestamp
  std::vector<uint8_t> mem_bytes;
  {
    auto img_bytes = ReadFileBytes(kInputImage);
    auto vid_bytes = ReadFileBytes(kInputVideo);
    auto img_src = MediaSource::FromMemory(img_bytes.data(), img_bytes.size());
    auto vid_src = MediaSource::FromMemory(vid_bytes.data(), vid_bytes.size());
    auto mem_sink = DataSink::ToMemory(&mem_bytes);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, mem_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_FALSE(mem_bytes.empty());
  }

  // 3. Compare streaming file-based vs in-memory bit-exact parity
  auto file_bytes = ReadFileBytes(kFileOut);
  EXPECT_EQ(file_bytes.size(), mem_bytes.size());
  EXPECT_TRUE(std::memcmp(file_bytes.data(), mem_bytes.data(), file_bytes.size()) == 0);

  // 4. Validate output with MotionPhotoChecker
  int check_status = CheckMotionPhoto(kFileOut, [](absl::string_view) {});
  EXPECT_EQ(check_status, 0);

  // 5. Cross-check legacy file vs legacy memory buffer API parity
  {
    MotionPhotoBuilderParams legacy_params;
    legacy_params.primary_image_file_name = kInputImage;
    legacy_params.primary_video_file_name = kInputVideo;
    legacy_params.output_image_file_name = kLegacyFileOut;
    int status = BuildMotionPhoto(legacy_params, [](const string&) {});
    EXPECT_EQ(status, 0);

    auto img_bytes = ReadFileBytes(kInputImage);
    auto vid_bytes = ReadFileBytes(kInputVideo);
    MotionPhotoBuilderMemoryParams mem_params;
    mem_params.primary_image_bytes = img_bytes.data();
    mem_params.primary_image_size = img_bytes.size();
    mem_params.primary_video_bytes = vid_bytes.data();
    mem_params.primary_video_size = vid_bytes.size();
    mem_params.output_image_file_name = kLegacyMemOutFile;
    int mem_status = BuildMotionPhotoFromMemory(mem_params, [](const string&) {});
    EXPECT_EQ(mem_status, 0);

    auto leg_file_bytes = ReadFileBytes(kLegacyFileOut);
    auto leg_mem_bytes = ReadFileBytes(kLegacyMemOutFile);
    EXPECT_EQ(leg_file_bytes.size(), leg_mem_bytes.size());
    EXPECT_TRUE(std::memcmp(leg_file_bytes.data(), leg_mem_bytes.data(), leg_file_bytes.size()) == 0);
  }

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 7. Streaming vs Memory Parity for High-Res HDR Video
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, StreamingAndMemory_HighResHdr_BitExactParity) {
  const string kTestDir = CreateTestDir("StreamingAndMemory_HighResHdr");
  const string kInputImage = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kInputVideo = "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  const string kFileOut = (fs::path(kTestDir) / "high_res_built.MP.jpg").string();

  // 1. File-based streaming build
  {
    auto img_src = MediaSource::FromFile(kInputImage, nullptr);
    auto vid_src = MediaSource::FromFile(kInputVideo, nullptr);
    auto file_sink = DataSink::ToFile(kFileOut, nullptr);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, file_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(fs::exists(kFileOut));
  }

  // 2. Pure in-memory streaming build
  std::vector<uint8_t> mem_bytes;
  {
    auto img_bytes = ReadFileBytes(kInputImage);
    auto vid_bytes = ReadFileBytes(kInputVideo);
    auto img_src = MediaSource::FromMemory(img_bytes.data(), img_bytes.size());
    auto vid_src = MediaSource::FromMemory(vid_bytes.data(), vid_bytes.size());
    auto mem_sink = DataSink::ToMemory(&mem_bytes);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, mem_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_FALSE(mem_bytes.empty());
  }

  // 3. Bit-for-bit assertion
  auto file_bytes = ReadFileBytes(kFileOut);
  EXPECT_EQ(file_bytes.size(), mem_bytes.size());
  EXPECT_TRUE(std::memcmp(file_bytes.data(), mem_bytes.data(), file_bytes.size()) == 0);
  EXPECT_EQ(CheckMotionPhoto(kFileOut, [](absl::string_view) {}), 0);

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 8. Streaming vs Memory Parity and AGTM Preservation
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, StreamingAndMemory_AgtmPreservation_BitExactParity) {
  const string kTestDir = CreateTestDir("StreamingAndMemory_AgtmPreservation");
  const string kInputImage = "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kInputVideo = "lib/core/motion_photo/testdata/motion_photo_agtm_video.mp4";
  const string kFileOut = (fs::path(kTestDir) / "agtm_built.MP.jpg").string();

  // 1. File-based streaming build
  {
    auto img_src = MediaSource::FromFile(kInputImage, nullptr);
    auto vid_src = MediaSource::FromFile(kInputVideo, nullptr);
    auto file_sink = DataSink::ToFile(kFileOut, nullptr);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, file_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(fs::exists(kFileOut));
  }

  // 2. Pure in-memory streaming build
  std::vector<uint8_t> mem_bytes;
  {
    auto img_bytes = ReadFileBytes(kInputImage);
    auto vid_bytes = ReadFileBytes(kInputVideo);
    auto img_src = MediaSource::FromMemory(img_bytes.data(), img_bytes.size());
    auto vid_src = MediaSource::FromMemory(vid_bytes.data(), vid_bytes.size());
    auto mem_sink = DataSink::ToMemory(&mem_bytes);

    MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_src);
    stream_params.primary_video = std::move(vid_src);
    stream_params.presentation_timestamp_us = 500000;

    int status = BuildMotionPhotoDirect(stream_params, mem_sink.get(), [](absl::string_view) {});
    EXPECT_EQ(status, 0);
    EXPECT_FALSE(mem_bytes.empty());
  }

  // 3. Bit-for-bit assertion
  auto file_bytes = ReadFileBytes(kFileOut);
  EXPECT_EQ(file_bytes.size(), mem_bytes.size());
  EXPECT_TRUE(std::memcmp(file_bytes.data(), mem_bytes.data(), file_bytes.size()) == 0);
  EXPECT_EQ(CheckMotionPhoto(kFileOut, [](absl::string_view) {}), 0);

  // 4. Assert AGTM dynamic metadata is preserved alongside the Motion Photo
  // 'meta' track
  MetadataEngine engine(nullptr);
  std::string expected_agtm = engine.ExtractAgtmAtTimestamp(kInputVideo, 500000);
  EXPECT_FALSE(expected_agtm.empty());

  std::string extracted_agtm = engine.ExtractAgtmAtTimestamp(kFileOut, 500000);
  EXPECT_FALSE(extracted_agtm.empty());
  EXPECT_EQ(expected_agtm, extracted_agtm);

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 9. DataSink::TransferFrom Single-Shot and Sequential Chunk Continuity
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest, DataSink_TransferFrom_ContinuityAndIntegrity) {
  const string kTestDir = CreateTestDir("DataSink_TransferFrom");
  const string kInputVideo = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  auto in_bytes = ReadFileBytes(kInputVideo);
  ASSERT_TRUE(!in_bytes.empty());

  // Case A: Full direct single-shot transfer
  {
    const string kOutputFile = (fs::path(kTestDir) / "direct_transfer.mp4").string();
    auto src = MediaSource::FromFile(kInputVideo, nullptr);
    EXPECT_TRUE(src != nullptr);
    auto sink = DataSink::ToFile(kOutputFile, nullptr);
    EXPECT_TRUE(sink != nullptr);

    bool success = sink->TransferFrom(*src, 0, in_bytes.size());
    EXPECT_TRUE(success);
    sink->Flush();

    auto out_bytes = ReadFileBytes(kOutputFile);
    EXPECT_EQ(in_bytes.size(), out_bytes.size());
    EXPECT_TRUE(std::memcmp(in_bytes.data(), out_bytes.data(), in_bytes.size()) == 0);
  }

  // Case B: Sequential 3-segment chunked transfer (verifies offset continuity)
  {
    const string kOutputFile = (fs::path(kTestDir) / "chunked_transfer.mp4").string();
    auto src = MediaSource::FromFile(kInputVideo, nullptr);
    EXPECT_TRUE(src != nullptr);
    auto sink = DataSink::ToFile(kOutputFile, nullptr);
    EXPECT_TRUE(sink != nullptr);

    size_t chunk1 = in_bytes.size() / 3;
    size_t chunk2 = in_bytes.size() / 3;
    size_t chunk3 = in_bytes.size() - (chunk1 + chunk2);

    EXPECT_TRUE(sink->TransferFrom(*src, 0, chunk1));
    EXPECT_TRUE(sink->TransferFrom(*src, chunk1, chunk2));
    EXPECT_TRUE(sink->TransferFrom(*src, chunk1 + chunk2, chunk3));
    sink->Flush();

    auto out_bytes = ReadFileBytes(kOutputFile);
    EXPECT_EQ(in_bytes.size(), out_bytes.size());
    EXPECT_TRUE(std::memcmp(in_bytes.data(), out_bytes.data(), in_bytes.size()) == 0);
  }

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 10. Regression: JPEG + MP4 + Timestamp (Empty moments_xmp & moments_video)
//     Always Emits Canonical 45-Byte v1 'meta' Track Across File & Memory APIs
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest,
     BuildJpegMp4WithTimestampOnly_InjectsDefault45ByteMetaTrack) {
  const string kTestDir = CreateTestDir("BuildJpegMp4WithTimestampOnly");
  const string kInputImage =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kInputVideo =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const string kFileOut = (fs::path(kTestDir) / "IMG_TS_ONLY_MP.jpg").string();
  const string kMemOut =
      (fs::path(kTestDir) / "IMG_TS_ONLY_MEM_MP.jpg").string();
  constexpr int64_t kTimestampUs = 799680;

  MotionPhotoBuilderParams file_params;
  file_params.primary_image_file_name = kInputImage;
  file_params.primary_video_file_name = kInputVideo;
  file_params.output_image_file_name = kFileOut;
  file_params.presentation_timestamp_us = kTimestampUs;
  EXPECT_EQ(BuildMotionPhoto(file_params, [](const string&) {}), 0);

  auto img_bytes = ReadFileBytes(kInputImage);
  auto vid_bytes = ReadFileBytes(kInputVideo);
  MotionPhotoBuilderMemoryParams mem_params;
  mem_params.primary_image_bytes = img_bytes.data();
  mem_params.primary_image_size = img_bytes.size();
  mem_params.primary_video_bytes = vid_bytes.data();
  mem_params.primary_video_size = vid_bytes.size();
  mem_params.output_image_file_name = kMemOut;
  mem_params.presentation_timestamp_us = kTimestampUs;
  EXPECT_EQ(BuildMotionPhotoFromMemory(mem_params, [](const string&) {}), 0);

  auto built_file_bytes = ReadFileBytes(kFileOut);
  auto built_mem_bytes = ReadFileBytes(kMemOut);
  EXPECT_EQ(built_file_bytes.size(), built_mem_bytes.size());
  EXPECT_EQ(std::memcmp(built_file_bytes.data(), built_mem_bytes.data(),
                        built_file_bytes.size()),
            0);
  EXPECT_EQ(CheckMotionPhoto(kFileOut, [](absl::string_view) {}), 0);

  // Locate appended MP4 and verify the injected 'meta' track and 45-byte
  // ISO/IEC 14496-1 MetadataDescriptor payload.
  size_t mp4_offset = built_file_bytes.size();
  for (size_t i = 0; i + 8 <= built_file_bytes.size(); ++i) {
    if (std::memcmp(built_file_bytes.data() + i + 4, "ftyp", 4) == 0) {
      mp4_offset = i;
      break;
    }
  }
  ASSERT_TRUE(mp4_offset < built_file_bytes.size());

  MotionPhotoMp4FileReader mp4_reader;
  ASSERT_TRUE(mp4_reader.Read(kFileOut, static_cast<int64_t>(mp4_offset)));
  const VideoTrackData& track_data = mp4_reader.GetVideoTrackData();
  EXPECT_NE(track_data.high_res_metadata_track_id, kInvalidTrackId);
  const std::vector<uint8_t>& meta_payload =
      mp4_reader.GetHighResTrackVideoMetadata();
  EXPECT_EQ(meta_payload.size(), static_cast<size_t>(45));

  MotionPhoto decoded_mp(FileType::kJpeg);
  MotionPhotoReader mp_reader(&decoded_mp, nullptr);
  ASSERT_TRUE(mp_reader.DecodeVideoMetadata(meta_payload));
  const auto& score_desc = decoded_mp.GetVideoMetadata().GetScoreDescriptor();
  EXPECT_EQ(score_desc.GetModelVersion(), 0u);
  EXPECT_EQ(score_desc.GetPrimaryImageFrameScoreDescriptor()
                .GetPresentationTimestampUs(),
            kTimestampUs);
  EXPECT_EQ(score_desc.GetHighResTrackScoreDescriptor().GetNumScoredFrames(),
            static_cast<size_t>(0));

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 11. Regression: JPEG + MP4 + Moments XMP Metadata & Timestamp Override Sync
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest,
     BuildJpegMp4WithMomentsXmp_InjectsScoredMetaTrackAndSyncsTimestamp) {
  const string kTestDir = CreateTestDir("BuildJpegMp4WithMomentsXmp");
  const string kInputImage =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kInputVideo =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const string kMomentsXmpFile =
      "lib/core/motion_photo/testdata/MomentsVideoMetadata.xmp";
  auto xmp_file_bytes = ReadFileBytes(kMomentsXmpFile);
  const string moments_xmp_str(
      reinterpret_cast<const char*>(xmp_file_bytes.data()),
      xmp_file_bytes.size());

  // Case A: Timestamp derived from MomentsVideoMetadata.xmp (500000 us)
  {
    const string kFileOut =
        (fs::path(kTestDir) / "IMG_XMP_FILE_MP.jpg").string();
    const string kMemOut = (fs::path(kTestDir) / "IMG_XMP_MEM_MP.jpg").string();

    MotionPhotoBuilderParams file_params;
    file_params.primary_image_file_name = kInputImage;
    file_params.primary_video_file_name = kInputVideo;
    file_params.moments_xmp_file_name = kMomentsXmpFile;
    file_params.output_image_file_name = kFileOut;
    EXPECT_EQ(BuildMotionPhoto(file_params, [](const string&) {}), 0);

    auto img_bytes = ReadFileBytes(kInputImage);
    auto vid_bytes = ReadFileBytes(kInputVideo);
    MotionPhotoBuilderMemoryParams mem_params;
    mem_params.primary_image_bytes = img_bytes.data();
    mem_params.primary_image_size = img_bytes.size();
    mem_params.primary_video_bytes = vid_bytes.data();
    mem_params.primary_video_size = vid_bytes.size();
    mem_params.moments_xmp = moments_xmp_str;
    mem_params.output_image_file_name = kMemOut;
    EXPECT_EQ(BuildMotionPhotoFromMemory(mem_params, [](const string&) {}), 0);

    auto built_file = ReadFileBytes(kFileOut);
    auto built_mem = ReadFileBytes(kMemOut);
    EXPECT_EQ(built_file.size(), built_mem.size());
    EXPECT_EQ(
        std::memcmp(built_file.data(), built_mem.data(), built_file.size()), 0);
    EXPECT_EQ(CheckMotionPhoto(kFileOut, [](absl::string_view) {}), 0);
    EXPECT_EQ(CheckMotionPhoto(kMemOut, [](absl::string_view) {}), 0);

    size_t mp4_offset = built_mem.size();
    for (size_t i = 0; i + 8 <= built_mem.size(); ++i) {
      if (std::memcmp(built_mem.data() + i + 4, "ftyp", 4) == 0) {
        mp4_offset = i;
        break;
      }
    }
    MotionPhotoMp4FileReader mp4_reader;
    ASSERT_TRUE(mp4_reader.Read(kMemOut, static_cast<int64_t>(mp4_offset)));
    const auto& meta_payload = mp4_reader.GetHighResTrackVideoMetadata();
    EXPECT_EQ(meta_payload.size(), static_cast<size_t>(101));

    MotionPhoto decoded_mp(FileType::kJpeg);
    MotionPhotoReader mp_reader(&decoded_mp, nullptr);
    ASSERT_TRUE(mp_reader.DecodeVideoMetadata(meta_payload));
    EXPECT_EQ(decoded_mp.GetVideoMetadata()
                  .GetScoreDescriptor()
                  .GetPrimaryImageFrameScoreDescriptor()
                  .GetPresentationTimestampUs(),
              500000);
    EXPECT_EQ(decoded_mp.GetVideoMetadata()
                  .GetScoreDescriptor()
                  .GetHighResTrackScoreDescriptor()
                  .GetNumScoredFrames(),
              static_cast<size_t>(4));
  }

  // Case B: Explicit presentation_timestamp_us overrides Moments XMP Time
  // in both JPEG XMP and MP4 'meta' track (verifies EnforceConsistentValues
  // runs prior to EncodeVideoMetadata).
  {
    constexpr int64_t kOverrideTs = 1234567;
    const string kSingleOut =
        (fs::path(kTestDir) / "IMG_OVERRIDE_SINGLE_MP.jpg").string();
    const string kDualOut =
        (fs::path(kTestDir) / "IMG_OVERRIDE_DUAL_MP.jpg").string();
    const string kWorkVid = (fs::path(kTestDir) / "work_override.mp4").string();
    const string kOptVid = (fs::path(kTestDir) / "opt_override.mp4").string();

    MotionPhotoBuilderParams single_params;
    single_params.primary_image_file_name = kInputImage;
    single_params.primary_video_file_name = kInputVideo;
    single_params.moments_xmp_file_name = kMomentsXmpFile;
    single_params.output_image_file_name = kSingleOut;
    single_params.presentation_timestamp_us = kOverrideTs;
    EXPECT_EQ(BuildMotionPhoto(single_params, [](const string&) {}), 0);
    EXPECT_EQ(CheckMotionPhoto(kSingleOut, [](absl::string_view) {}), 0);

    MotionPhotoBuilderParams dual_params = single_params;
    dual_params.moments_video_file_name = kInputVideo;
    dual_params.working_video_file_name = kWorkVid;
    dual_params.optimized_video_file_name = kOptVid;
    dual_params.output_image_file_name = kDualOut;
    EXPECT_EQ(BuildMotionPhoto(dual_params, [](const string&) {}), 0);
    EXPECT_EQ(CheckMotionPhoto(kDualOut, [](absl::string_view) {}), 0);
  }

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 12. FastStart (moov < mdat) Chunk Offset Shifting & 'meta' Track
// Deduplication
// -----------------------------------------------------------------------------
TEST(MotionPhotoBuilderTest,
     Mp4MettBoxInjector_FastStartMoovBeforeMdat_ShiftsOffsetsAndDeduplicates) {
  // Strip existing 'meta' track from faststart fixture (or use a synthetic
  // faststart MP4 with moov before mdat and no prior 'meta' track) to verify
  // non-zero +delta_moov chunk offset shifting on video & audio samples.
  const string kTestDir = CreateTestDir("Mp4MettBoxInjector_FastStart");
  const string kStrippedFastStartMp4 =
      (fs::path(kTestDir) / "faststart_no_meta.mp4").string();
  {
    auto src_bytes = ReadFileBytes(
        "lib/core/motion_photo/testdata/"
        "motion_photo_video_only_with_high_res.mp4");
    ASSERT_FALSE(src_bytes.empty());
    std::ofstream out(kStrippedFastStartMp4, std::ios::binary);
    out.write(reinterpret_cast<const char*>(src_bytes.data()),
              src_bytes.size());
  }
  EXPECT_TRUE(MotionPhotoMp4FileBuilder::StripMetadataTrack(
      kStrippedFastStartMp4, nullptr));

  auto faststart_bytes = ReadFileBytes(kStrippedFastStartMp4);
  ASSERT_FALSE(faststart_bytes.empty());

  // Verify faststart layout (moov precedes mdat) and record original sample #1
  Mp4BoxReader orig_reader;
  ASSERT_TRUE(orig_reader.ParseFromMemory(faststart_bytes.data(),
                                          faststart_bytes.size()));
  std::vector<uint8_t> expected_video_sample;
  ASSERT_TRUE(orig_reader.ReadSample(1, 1, &expected_video_sample));
  EXPECT_FALSE(expected_video_sample.empty());
  for (const auto& t : orig_reader.GetTracks()) {
    EXPECT_FALSE(Mp4BoxReader::TrackHasMettMime(t, kMotionPhotoMetadataMime));
  }

  // Inject first 'meta' payload (+423 B moov growth -> shifts stco offsets)
  MotionPhoto mp1(FileType::kJpeg);
  mp1.SetImageTimestamp(333333);
  mp1.EnforceConsistentValues();
  MotionPhotoWriter writer1(mp1);
  std::vector<uint8_t> meta1;
  writer1.EncodeVideoMetadata(&meta1);
  ASSERT_FALSE(meta1.empty());

  auto src1 =
      MediaSource::FromMemory(faststart_bytes.data(), faststart_bytes.size());
  PreparedMp4MettInjection prep1;
  ASSERT_TRUE(PrepareMp4MettInjection(*src1, meta1, &prep1));
  EXPECT_FALSE(prep1.moov_at_end);

  std::vector<uint8_t> injected1;
  auto sink1 = DataSink::ToMemory(&injected1);
  ASSERT_TRUE(WriteMp4WithMettToSink(*src1, prep1, sink1.get()));
  EXPECT_EQ(injected1.size(), prep1.output_video_size);

  Mp4BoxReader reader1;
  ASSERT_TRUE(reader1.ParseFromMemory(injected1.data(), injected1.size()));
  std::vector<uint8_t> actual_video_sample1;
  ASSERT_TRUE(reader1.ReadSample(1, 1, &actual_video_sample1));
  EXPECT_EQ(actual_video_sample1.size(), expected_video_sample.size());
  EXPECT_EQ(
      std::memcmp(actual_video_sample1.data(), expected_video_sample.data(),
                  expected_video_sample.size()),
      0);

  int mett_count1 = 0;
  uint32_t mett_track_id1 = 0;
  for (const auto& t : reader1.GetTracks()) {
    if (Mp4BoxReader::TrackHasMettMime(t, kMotionPhotoMetadataMime)) {
      ++mett_count1;
      mett_track_id1 = t.track_id;
    }
  }
  EXPECT_EQ(mett_count1, 1);

  std::vector<uint8_t> read_meta1;
  ASSERT_TRUE(reader1.ReadSample(mett_track_id1, 1, &read_meta1));
  EXPECT_EQ(read_meta1.size(), meta1.size());
  EXPECT_EQ(std::memcmp(read_meta1.data(), meta1.data(), meta1.size()), 0);

  // Re-inject updated 'meta' payload into injected1: verifies existing 'mett'
  // track is replaced rather than duplicated.
  MotionPhoto mp2(FileType::kJpeg);
  mp2.SetImageTimestamp(888888);
  mp2.EnforceConsistentValues();
  MotionPhotoWriter writer2(mp2);
  std::vector<uint8_t> meta2;
  writer2.EncodeVideoMetadata(&meta2);
  ASSERT_FALSE(meta2.empty());

  auto src2 = MediaSource::FromMemory(injected1.data(), injected1.size());
  PreparedMp4MettInjection prep2;
  ASSERT_TRUE(PrepareMp4MettInjection(*src2, meta2, &prep2));
  std::vector<uint8_t> injected2;
  auto sink2 = DataSink::ToMemory(&injected2);
  ASSERT_TRUE(WriteMp4WithMettToSink(*src2, prep2, sink2.get()));

  Mp4BoxReader reader2;
  ASSERT_TRUE(reader2.ParseFromMemory(injected2.data(), injected2.size()));
  int mett_count2 = 0;
  uint32_t mett_track_id2 = 0;
  for (const auto& t : reader2.GetTracks()) {
    if (Mp4BoxReader::TrackHasMettMime(t, kMotionPhotoMetadataMime)) {
      ++mett_count2;
      mett_track_id2 = t.track_id;
    }
  }
  EXPECT_EQ(mett_count2, 1);

  std::vector<uint8_t> read_meta2;
  ASSERT_TRUE(reader2.ReadSample(mett_track_id2, 1, &read_meta2));
  MotionPhoto decoded2(FileType::kJpeg);
  MotionPhotoReader mp_reader2(&decoded2, nullptr);
  ASSERT_TRUE(mp_reader2.DecodeVideoMetadata(read_meta2));
  EXPECT_EQ(decoded2.GetVideoMetadata()
                .GetScoreDescriptor()
                .GetPrimaryImageFrameScoreDescriptor()
                .GetPresentationTimestampUs(),
            888888);

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// CIPA DC-007 MPF Secondary Image Offset Preservation (UltraHDR & Multi-Image)
// -----------------------------------------------------------------------------
namespace {

struct TestMpfEntry {
  uint32_t attribute = 0;
  uint32_t image_size = 0;
  uint32_t image_offset = 0;
};

enum class MpfTagLayout {
  // Standard 3-tag IFD (0xB000, 0xB001, 0xB002) -> delta_mpf == 0 on re-encode.
  kStandard3Tags,
  // 4-tag IFD (+ 0xB004 TotalNumberOfCapturedFrames) + 32-byte Attribute IFD
  // tail -> old_mpf_size > new_mpf_size (delta_mpf < 0).
  kExtraTagAndPadding,
  // Minimal 2-tag IFD (0xB001, 0xB002 without 0xB000) ->
  // old_mpf_size < new_mpf_size (delta_mpf > 0).
  kMinimal2Tags,
};

void AppendBe16(std::vector<uint8_t>* buf, uint16_t val) {
  buf->push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>(val & 0xFF));
}

void AppendBe32(std::vector<uint8_t>* buf, uint32_t val) {
  buf->push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
  buf->push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf->push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>(val & 0xFF));
}

uint16_t ReadBe16At(const std::vector<uint8_t>& buf, size_t off) {
  return static_cast<uint16_t>((static_cast<uint16_t>(buf[off]) << 8) |
                               buf[off + 1]);
}

uint32_t ReadBe32At(const std::vector<uint8_t>& buf, size_t off) {
  return (static_cast<uint32_t>(buf[off]) << 24) |
         (static_cast<uint32_t>(buf[off + 1]) << 16) |
         (static_cast<uint32_t>(buf[off + 2]) << 8) |
         static_cast<uint32_t>(buf[off + 3]);
}

std::vector<uint8_t> BuildApp2MpfSegment(
    const std::vector<TestMpfEntry>& entries, MpfTagLayout layout) {
  std::vector<uint8_t> payload;
  // MPF signature ("MPF\0") + Big-Endian TIFF header ("MM\x00\x2A") + IFD0
  // offset (8)
  payload.insert(payload.end(), {'M', 'P', 'F', '\0', 0x4D, 0x4D, 0x00, 0x2A});
  AppendBe32(&payload, 8);  // Index IFD starts right after endian + offset

  uint16_t tag_count = 3;
  if (layout == MpfTagLayout::kExtraTagAndPadding) {
    tag_count = 4;
  } else if (layout == MpfTagLayout::kMinimal2Tags) {
    tag_count = 2;
  }
  AppendBe16(&payload, tag_count);

  const uint32_t num_images = static_cast<uint32_t>(entries.size());
  const uint32_t mp_entry_offset =
      8 + 2 + static_cast<uint32_t>(tag_count) * 12 + 4;

  if (layout != MpfTagLayout::kMinimal2Tags) {
    // 0xB000: MPFVersion = "0100"
    AppendBe16(&payload, 0xB000);
    AppendBe16(&payload, 0x0007);
    AppendBe32(&payload, 4);
    payload.insert(payload.end(), {'0', '1', '0', '0'});
  }

  // 0xB001: NumberOfImages
  AppendBe16(&payload, 0xB001);
  AppendBe16(&payload, 0x0004);
  AppendBe32(&payload, 1);
  AppendBe32(&payload, num_images);

  // 0xB002: MPEntry
  AppendBe16(&payload, 0xB002);
  AppendBe16(&payload, 0x0007);
  AppendBe32(&payload, num_images * 16);
  AppendBe32(&payload, mp_entry_offset);

  if (layout == MpfTagLayout::kExtraTagAndPadding) {
    // 0xB004: TotalNumberOfCapturedFrames
    AppendBe16(&payload, 0xB004);
    AppendBe16(&payload, 0x0004);
    AppendBe32(&payload, 1);
    AppendBe32(&payload, num_images);
  }

  // Next/Attribute IFD offset (0)
  AppendBe32(&payload, 0);

  for (size_t i = 0; i < entries.size(); ++i) {
    AppendBe32(&payload, entries[i].attribute);
    AppendBe32(&payload, entries[i].image_size);
    AppendBe32(&payload, entries[i].image_offset);
    AppendBe16(&payload, 0);
    AppendBe16(&payload, 0);
  }

  if (layout == MpfTagLayout::kExtraTagAndPadding) {
    // Append 32 trailing bytes (e.g., unreferenced Attribute IFD padding)
    payload.insert(payload.end(), 32, 0x00);
  }

  std::vector<uint8_t> seg = {0xFF, 0xE2};
  AppendBe16(&seg, static_cast<uint16_t>(2 + payload.size()));
  seg.insert(seg.end(), payload.begin(), payload.end());
  return seg;
}

std::vector<uint8_t> BuildApp0JfifSegment() {
  return {0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',  0x00,
          0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
}

std::vector<uint8_t> BuildApp1ExifSegment() {
  return {0xFF, 0xE1, 0x00, 0x10, 'E',  'x',  'i',  'f',  0x00,
          0x00, 0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08};
}

std::vector<uint8_t> BuildApp1XmpSegment(const std::string& xmp_xml) {
  static constexpr char kXmpHeader[] = "http://ns.adobe.com/xap/1.0/\0";
  const size_t payload_len = (sizeof(kXmpHeader) - 1) + xmp_xml.size();
  std::vector<uint8_t> seg = {0xFF, 0xE1};
  AppendBe16(&seg, static_cast<uint16_t>(2 + payload_len));
  seg.insert(seg.end(), kXmpHeader, kXmpHeader + (sizeof(kXmpHeader) - 1));
  seg.insert(seg.end(), xmp_xml.begin(), xmp_xml.end());
  return seg;
}

std::vector<uint8_t> BuildSyntheticSecondaryJpeg(uint8_t tag_seed,
                                                 size_t body_padding) {
  std::string gm_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:hdrgm='http://ns.adobe.com/hdr-gain-map/1.0/' "
      "hdrgm:Version='1.0'/></rdf:RDF></x:xmpmeta>";
  auto xmp_seg = BuildApp1XmpSegment(gm_xmp);

  // Extract scan tail (from first DQT/SOF onward through EOI) of real test JPEG
  // so every secondary image is a valid standalone JPEG stream.
  auto base_jpg = ReadFileBytes(
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg");
  size_t scan_pos = 2;
  while (scan_pos + 4 <= base_jpg.size() && base_jpg[scan_pos] == 0xFF) {
    uint8_t marker = base_jpg[scan_pos + 1];
    if (marker >= 0xE0 && marker <= 0xEF) {
      uint16_t seg_len = ReadBe16At(base_jpg, scan_pos + 2);
      scan_pos += 2 + seg_len;
    } else {
      break;
    }
  }

  std::vector<uint8_t> com_seg = {0xFF, 0xFE};
  AppendBe16(&com_seg, static_cast<uint16_t>(2 + body_padding));
  com_seg.insert(com_seg.end(), body_padding, tag_seed);

  std::vector<uint8_t> sec = {0xFF, 0xD8};
  sec.insert(sec.end(), xmp_seg.begin(), xmp_seg.end());
  sec.insert(sec.end(), com_seg.begin(), com_seg.end());
  sec.insert(sec.end(), base_jpg.begin() + scan_pos, base_jpg.end());
  return sec;
}

enum class HeaderSegmentOrder {
  // [SOI][APP0 JFIF][APP1 XMP][APP2 MPF][DQT..EOI]
  kXmpBeforeMpf,
  // [SOI][APP0 JFIF][APP2 MPF][APP1 EXIF][APP1 XMP][DQT..EOI]
  kXmpAfterMpf,
  // [SOI][APP0 JFIF][APP2 MPF][APP1 EXIF][DQT..EOI] (no initial XMP; cut_range
  // inserted at exif_end after MPF)
  kNoXmpInsertedAfterMpf,
  // [SOI][APP0 JFIF][APP2 MPF][DQT..EOI] (no initial XMP and no EXIF; cut_range
  // inserted at jfif_end == mpf_begin)
  kNoXmpInsertedAtMpfBoundary,
};

std::vector<uint8_t> BuildMultiPictureJpeg(
    HeaderSegmentOrder order, MpfTagLayout mpf_layout,
    const std::string& initial_xmp,
    const std::vector<std::vector<uint8_t>>& secondary_jpegs) {
  auto base_jpg = ReadFileBytes(
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg");
  size_t scan_pos = 2;
  while (scan_pos + 4 <= base_jpg.size() && base_jpg[scan_pos] == 0xFF) {
    uint8_t marker = base_jpg[scan_pos + 1];
    if (marker >= 0xE0 && marker <= 0xEF) {
      uint16_t seg_len = ReadBe16At(base_jpg, scan_pos + 2);
      scan_pos += 2 + seg_len;
    } else {
      break;
    }
  }
  std::vector<uint8_t> scan_tail(base_jpg.begin() + scan_pos, base_jpg.end());

  auto jfif_seg = BuildApp0JfifSegment();
  auto exif_seg = BuildApp1ExifSegment();
  auto xmp_seg = BuildApp1XmpSegment(initial_xmp);

  // First build placeholder MPF to get exact MPF segment byte length.
  std::vector<TestMpfEntry> entries(1 + secondary_jpegs.size());
  entries[0].attribute = 0x030000;
  auto dummy_mpf = BuildApp2MpfSegment(entries, mpf_layout);
  const size_t mpf_seg_size = dummy_mpf.size();

  size_t prefix_before_mpf = 2 + jfif_seg.size();
  size_t suffix_after_mpf_in_primary = scan_tail.size();
  if (order == HeaderSegmentOrder::kXmpBeforeMpf) {
    prefix_before_mpf += xmp_seg.size();
  } else if (order == HeaderSegmentOrder::kXmpAfterMpf) {
    suffix_after_mpf_in_primary += exif_seg.size() + xmp_seg.size();
  } else if (order == HeaderSegmentOrder::kNoXmpInsertedAfterMpf) {
    suffix_after_mpf_in_primary += exif_seg.size();
  }

  const size_t primary_size =
      prefix_before_mpf + mpf_seg_size + suffix_after_mpf_in_primary;
  const size_t mpf_endian_pos = prefix_before_mpf + 8;

  entries[0].image_size = static_cast<uint32_t>(primary_size);
  entries[0].image_offset = 0;
  size_t running_sec_pos = primary_size;
  for (size_t i = 0; i < secondary_jpegs.size(); ++i) {
    entries[i + 1].attribute = 0;
    entries[i + 1].image_size =
        static_cast<uint32_t>(secondary_jpegs[i].size());
    entries[i + 1].image_offset =
        static_cast<uint32_t>(running_sec_pos - mpf_endian_pos);
    running_sec_pos += secondary_jpegs[i].size();
  }

  auto mpf_seg = BuildApp2MpfSegment(entries, mpf_layout);

  std::vector<uint8_t> out = {0xFF, 0xD8};
  out.insert(out.end(), jfif_seg.begin(), jfif_seg.end());
  if (order == HeaderSegmentOrder::kXmpBeforeMpf) {
    out.insert(out.end(), xmp_seg.begin(), xmp_seg.end());
    out.insert(out.end(), mpf_seg.begin(), mpf_seg.end());
  } else if (order == HeaderSegmentOrder::kXmpAfterMpf) {
    out.insert(out.end(), mpf_seg.begin(), mpf_seg.end());
    out.insert(out.end(), exif_seg.begin(), exif_seg.end());
    out.insert(out.end(), xmp_seg.begin(), xmp_seg.end());
  } else if (order == HeaderSegmentOrder::kNoXmpInsertedAfterMpf) {
    out.insert(out.end(), mpf_seg.begin(), mpf_seg.end());
    out.insert(out.end(), exif_seg.begin(), exif_seg.end());
  } else {
    out.insert(out.end(), mpf_seg.begin(), mpf_seg.end());
  }
  out.insert(out.end(), scan_tail.begin(), scan_tail.end());
  for (const auto& sec : secondary_jpegs) {
    out.insert(out.end(), sec.begin(), sec.end());
  }
  return out;
}

void VerifyOutputMpfAndSecondaryImages(
    const std::vector<uint8_t>& out_bytes,
    const std::vector<std::vector<uint8_t>>& expected_secondary_jpegs,
    size_t expected_trailing_bytes = 0, bool exact_trailing = true) {
  const bool has_min_jpeg_bytes = out_bytes.size() >= 4u;
  ASSERT_TRUE(has_min_jpeg_bytes);
  EXPECT_EQ(out_bytes[0], 0xFF);
  EXPECT_EQ(out_bytes[1], 0xD8);

  // Locate APP2 MPF segment inside primary image header.
  size_t pos = 2;
  size_t mpf_seg_pos = 0;
  size_t mpf_seg_len = 0;
  while (pos + 4 <= out_bytes.size() && out_bytes[pos] == 0xFF) {
    uint8_t marker = out_bytes[pos + 1];
    if (marker == 0xDA || marker == 0xD9) break;
    uint16_t seg_len = ReadBe16At(out_bytes, pos + 2);
    const bool valid_seg =
        seg_len >= 2u && (pos + 2 + seg_len <= out_bytes.size());
    ASSERT_TRUE(valid_seg);
    if (marker == 0xE2 && seg_len >= 14 &&
        std::memcmp(out_bytes.data() + pos + 4, "MPF\0", 4) == 0) {
      mpf_seg_pos = pos;
      mpf_seg_len = 2 + seg_len;
      break;
    }
    pos += 2 + seg_len;
  }
  ASSERT_NE(mpf_seg_pos, 0u);
  const size_t mpf_endian_pos = mpf_seg_pos + 8;
  const bool valid_mpf_header = mpf_endian_pos + 8 <= mpf_seg_pos + mpf_seg_len;
  ASSERT_TRUE(valid_mpf_header);
  // MpfInfoEncoder always writes Big-Endian ("MM\x00\x2A").
  EXPECT_EQ(out_bytes[mpf_endian_pos + 0], 0x4D);
  EXPECT_EQ(out_bytes[mpf_endian_pos + 1], 0x4D);
  EXPECT_EQ(out_bytes[mpf_endian_pos + 2], 0x00);
  EXPECT_EQ(out_bytes[mpf_endian_pos + 3], 0x2A);

  const uint32_t ifd_rel = ReadBe32At(out_bytes, mpf_endian_pos + 4);
  const size_t ifd_pos = mpf_endian_pos + ifd_rel;
  const uint16_t tag_count = ReadBe16At(out_bytes, ifd_pos);

  uint32_t num_images = 0;
  uint32_t mp_entry_rel = 0;
  for (uint16_t t = 0; t < tag_count; ++t) {
    size_t tpos = ifd_pos + 2 + static_cast<size_t>(t) * 12;
    uint16_t tag_id = ReadBe16At(out_bytes, tpos);
    uint32_t val = ReadBe32At(out_bytes, tpos + 8);
    if (tag_id == 0xB001) num_images = val;
    if (tag_id == 0xB002) mp_entry_rel = val;
  }
  ASSERT_EQ(num_images,
            static_cast<uint32_t>(1 + expected_secondary_jpegs.size()));
  ASSERT_NE(mp_entry_rel, 0u);

  const size_t entries_pos = mpf_endian_pos + mp_entry_rel;
  const uint32_t primary_size = ReadBe32At(out_bytes, entries_pos + 4);
  const uint32_t primary_offset = ReadBe32At(out_bytes, entries_pos + 8);
  EXPECT_EQ(primary_offset, 0u);
  const bool valid_primary_bounds =
      primary_size >= 4u && primary_size <= out_bytes.size();
  ASSERT_TRUE(valid_primary_bounds);
  // Primary JPEG must end with EOI (0xFF 0xD9).
  EXPECT_EQ(out_bytes[primary_size - 2], 0xFF);
  EXPECT_EQ(out_bytes[primary_size - 1], 0xD9);

  size_t expected_sec_pos = primary_size;
  for (size_t i = 0; i < expected_secondary_jpegs.size(); ++i) {
    const size_t epos = entries_pos + (i + 1) * 16;
    const uint32_t sec_size = ReadBe32At(out_bytes, epos + 4);
    const uint32_t sec_rel_offset = ReadBe32At(out_bytes, epos + 8);
    EXPECT_EQ(sec_size,
              static_cast<uint32_t>(expected_secondary_jpegs[i].size()));
    const size_t actual_sec_pos = mpf_endian_pos + sec_rel_offset;
    EXPECT_EQ(actual_sec_pos, expected_sec_pos);
    const bool valid_sec_bounds = actual_sec_pos + sec_size <= out_bytes.size();
    ASSERT_TRUE(valid_sec_bounds);
    EXPECT_EQ(std::memcmp(out_bytes.data() + actual_sec_pos,
                          expected_secondary_jpegs[i].data(), sec_size),
              0);
    expected_sec_pos += sec_size;
  }
  if (exact_trailing) {
    EXPECT_EQ(expected_sec_pos + expected_trailing_bytes, out_bytes.size());
  } else {
    const bool has_min_trailer =
        out_bytes.size() >= expected_sec_pos + expected_trailing_bytes;
    EXPECT_TRUE(has_min_trailer);
    const bool has_ftyp_hdr = out_bytes.size() >= expected_sec_pos + 8;
    ASSERT_TRUE(has_ftyp_hdr);
    EXPECT_EQ(std::memcmp(out_bytes.data() + expected_sec_pos + 4, "ftyp", 4),
              0);
  }
}

}  // namespace

TEST(MotionPhotoBuilderTest,
     UltraHdrMpf_XmpBeforeMpf_UpdatesSecondaryOffsetsForAllDeltaMpf) {
  const string kTestDir = CreateTestDir("UltraHdrMpf_XmpBeforeMpf");
  const string kInputVideo =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const auto gain_map_jpeg = BuildSyntheticSecondaryJpeg(0xA5, 48);
  const string initial_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:hdrgm='http://ns.adobe.com/hdr-gain-map/1.0/' "
      "hdrgm:Version='1.0'/></rdf:RDF></x:xmpmeta>";

  for (MpfTagLayout layout :
       {MpfTagLayout::kStandard3Tags, MpfTagLayout::kExtraTagAndPadding,
        MpfTagLayout::kMinimal2Tags}) {
    auto uhdr_input =
        BuildMultiPictureJpeg(HeaderSegmentOrder::kXmpBeforeMpf, layout,
                              initial_xmp, {gain_map_jpeg});
    const string in_img_path = (fs::path(kTestDir) / "in_uhdr.jpg").string();
    const string out_mp_path =
        (fs::path(kTestDir) / "out_uhdr.MP.jpg").string();
    const string ext_img_path =
        (fs::path(kTestDir) / "extracted_uhdr.jpg").string();
    const string ext_vid_path =
        (fs::path(kTestDir) / "extracted_vid.mp4").string();
    {
      std::ofstream out(in_img_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(uhdr_input.data()),
                uhdr_input.size());
    }

    // 1. Build Motion Photo from File (ReadAt path) and from Memory (GetData
    // path)
    {
      auto img_src = MediaSource::FromFile(in_img_path, nullptr);
      auto vid_src = MediaSource::FromFile(kInputVideo, nullptr);
      auto file_sink = DataSink::ToFile(out_mp_path, nullptr);
      MotionPhotoStreamParams params;
      params.primary_image = std::move(img_src);
      params.primary_video = std::move(vid_src);
      params.presentation_timestamp_us = 500000;
      EXPECT_EQ(BuildMotionPhotoDirect(params, file_sink.get(),
                                       [](absl::string_view) {}),
                0);
    }

    std::vector<uint8_t> mem_mp_bytes;
    auto vid_bytes = ReadFileBytes(kInputVideo);
    {
      auto img_src =
          MediaSource::FromMemory(uhdr_input.data(), uhdr_input.size());
      auto vid_src =
          MediaSource::FromMemory(vid_bytes.data(), vid_bytes.size());
      auto mem_sink = DataSink::ToMemory(&mem_mp_bytes);
      MotionPhotoStreamParams params;
      params.primary_image = std::move(img_src);
      params.primary_video = std::move(vid_src);
      params.presentation_timestamp_us = 500000;
      EXPECT_EQ(BuildMotionPhotoDirect(params, mem_sink.get(),
                                       [](absl::string_view) {}),
                0);
    }

    auto file_mp_bytes = ReadFileBytes(out_mp_path);
    ASSERT_EQ(file_mp_bytes.size(), mem_mp_bytes.size());
    EXPECT_TRUE(file_mp_bytes == mem_mp_bytes);

    // 2. Verify CIPA DC-007 MPF secondary Gain-Map offset and payload in built
    // MP (trailing MP4 may include an injected 'meta' track from builder)
    VerifyOutputMpfAndSecondaryImages(file_mp_bytes, {gain_map_jpeg},
                                      vid_bytes.size(),
                                      /*exact_trailing=*/false);
    EXPECT_EQ(CheckMotionPhoto(out_mp_path, [](absl::string_view) {}), 0);

    // 3. Extract still image back out (exercises delta_xmp < 0 XMP stripping)
    // and verify the Gain-Map MPF offset & payload remain 100% intact.
    const char* extract_argv[] = {
        "MotionPhotoExtractor", "-mi", out_mp_path.c_str(), "-po",
        ext_img_path.c_str(),   "-vo", ext_vid_path.c_str()};
    EXPECT_EQ(
        MotionPhotoExtractorFunction(7, extract_argv, [](absl::string_view) {}),
        0);
    auto extracted_still = ReadFileBytes(ext_img_path);
    VerifyOutputMpfAndSecondaryImages(extracted_still, {gain_map_jpeg}, 0);
  }

  fs::remove_all(kTestDir);
}

TEST(MotionPhotoBuilderTest,
     UltraHdrMpf_XmpAfterMpf_ShiftsSecondaryOffsetsByDeltaXmpPlusDeltaMpf) {
  const string kTestDir = CreateTestDir("UltraHdrMpf_XmpAfterMpf");
  const string kInputVideo =
      "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  const auto vid_bytes = ReadFileBytes(kInputVideo);
  const auto gain_map_jpeg = BuildSyntheticSecondaryJpeg(0x5A, 64);
  const string initial_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:hdrgm='http://ns.adobe.com/hdr-gain-map/1.0/' "
      "hdrgm:Version='1.0'/></rdf:RDF></x:xmpmeta>";

  // Exercise all 3 XMP-vs-MPF placement variants combined with all 3 delta_mpf
  // cases (delta_mpf == 0, delta_mpf < 0, delta_mpf > 0):
  // - kXmpAfterMpf: existing XMP follows MPF
  // - kNoXmpInsertedAfterMpf: no initial XMP, inserted at exif_end after MPF
  // - kNoXmpInsertedAtMpfBoundary: no initial XMP/EXIF, inserted at jfif_end ==
  //   mpf_begin
  for (HeaderSegmentOrder order :
       {HeaderSegmentOrder::kXmpAfterMpf,
        HeaderSegmentOrder::kNoXmpInsertedAfterMpf,
        HeaderSegmentOrder::kNoXmpInsertedAtMpfBoundary}) {
    for (MpfTagLayout layout :
         {MpfTagLayout::kStandard3Tags, MpfTagLayout::kExtraTagAndPadding,
          MpfTagLayout::kMinimal2Tags}) {
      auto uhdr_input =
          BuildMultiPictureJpeg(order, layout, initial_xmp, {gain_map_jpeg});
      const string out_mp_path =
          (fs::path(kTestDir) / "xmp_after_mpf.MP.jpg").string();
      const string ext_img_path =
          (fs::path(kTestDir) / "xmp_after_mpf_still.jpg").string();
      const string ext_vid_path =
          (fs::path(kTestDir) / "xmp_after_mpf_vid.mp4").string();

      {
        auto img_src =
            MediaSource::FromMemory(uhdr_input.data(), uhdr_input.size());
        auto vid_src =
            MediaSource::FromMemory(vid_bytes.data(), vid_bytes.size());
        auto file_sink = DataSink::ToFile(out_mp_path, nullptr);
        MotionPhotoStreamParams params;
        params.primary_image = std::move(img_src);
        params.primary_video = std::move(vid_src);
        params.presentation_timestamp_us = 333333;
        EXPECT_EQ(BuildMotionPhotoDirect(params, file_sink.get(),
                                         [](absl::string_view) {}),
                  0);
      }

      auto built_bytes = ReadFileBytes(out_mp_path);
      VerifyOutputMpfAndSecondaryImages(built_bytes, {gain_map_jpeg},
                                        vid_bytes.size(),
                                        /*exact_trailing=*/false);
      EXPECT_EQ(CheckMotionPhoto(out_mp_path, [](absl::string_view) {}), 0);

      // Also verify XMP shrinking (delta_xmp < 0) when extracting still image
      // when XMP is located after MPF!
      const char* extract_argv[] = {
          "MotionPhotoExtractor", "-mi", out_mp_path.c_str(), "-po",
          ext_img_path.c_str(),   "-vo", ext_vid_path.c_str()};
      EXPECT_EQ(MotionPhotoExtractorFunction(7, extract_argv,
                                             [](absl::string_view) {}),
                0);
      auto extracted_still = ReadFileBytes(ext_img_path);
      VerifyOutputMpfAndSecondaryImages(extracted_still, {gain_map_jpeg}, 0);
    }
  }

  fs::remove_all(kTestDir);
}

TEST(MotionPhotoBuilderTest,
     MultiImageMpf_ThreeImages_PreservesAllSecondaryOffsetsAndPayloads) {
  const string kTestDir = CreateTestDir("MultiImageMpf_ThreeImages");
  const auto gain_map_jpeg = BuildSyntheticSecondaryJpeg(0x11, 40);
  const auto depth_jpeg = BuildSyntheticSecondaryJpeg(0x22, 80);
  const string initial_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description/></rdf:RDF></x:xmpmeta>";
  const string new_motion_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:Camera='http://ns.google.com/photos/1.0/camera/' "
      "Camera:MotionPhoto='1' Camera:MotionPhotoVersion='1' "
      "Camera:MotionPhotoPresentationTimestampUs='250000'/></rdf:RDF></"
      "x:xmpmeta>";

  for (HeaderSegmentOrder order :
       {HeaderSegmentOrder::kXmpBeforeMpf, HeaderSegmentOrder::kXmpAfterMpf}) {
    auto three_img_input =
        BuildMultiPictureJpeg(order, MpfTagLayout::kExtraTagAndPadding,
                              initial_xmp, {gain_map_jpeg, depth_jpeg});
    const string in_path = (fs::path(kTestDir) / "three_img_in.jpg").string();
    {
      std::ofstream out(in_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(three_img_input.data()),
                three_img_input.size());
    }

    MotionPhotoJpegImageBuilder builder(nullptr);

    std::vector<uint8_t> out_from_mem;
    {
      auto mem_src = MediaSource::FromMemory(three_img_input.data(),
                                             three_img_input.size());
      auto mem_sink = DataSink::ToMemory(&out_from_mem);
      EXPECT_TRUE(builder.Build(*mem_src, new_motion_xmp, mem_sink.get()));
    }

    std::vector<uint8_t> out_from_file;
    {
      auto file_src = MediaSource::FromFile(in_path, nullptr);
      auto mem_sink = DataSink::ToMemory(&out_from_file);
      EXPECT_TRUE(builder.Build(*file_src, new_motion_xmp, mem_sink.get()));
    }

    ASSERT_EQ(out_from_mem.size(), out_from_file.size());
    EXPECT_TRUE(out_from_mem == out_from_file);
    VerifyOutputMpfAndSecondaryImages(out_from_mem, {gain_map_jpeg, depth_jpeg},
                                      0);
  }

  fs::remove_all(kTestDir);
}

TEST(MotionPhotoBuilderTest,
     MultiImageMpf_OffsetExceedsUint32Max_ReturnsFalseWithoutStaleOutput) {
  const auto gain_map_jpeg = BuildSyntheticSecondaryJpeg(0x11, 32);
  const string small_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description/></rdf:RDF></x:xmpmeta>";
  const string large_motion_xmp =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:Camera='http://ns.google.com/photos/1.0/camera/' "
      "Camera:MotionPhoto='1' Camera:MotionPhotoVersion='1' "
      "Camera:MotionPhotoPresentationTimestampUs='250000'/></rdf:RDF></"
      "x:xmpmeta>";

  // Put XMP after MPF so growing the XMP shifts secondary MPF offsets by
  // primary_delta > 0 and overflows 0xFFFFFFF0U past UINT32_MAX.
  std::vector<uint8_t> jpeg = BuildMultiPictureJpeg(
      HeaderSegmentOrder::kXmpAfterMpf, MpfTagLayout::kMinimal2Tags, small_xmp,
      {gain_map_jpeg});

  // Locate the MPF segment ("MPF\0") and patch entry[1].image_offset near
  // UINT32_MAX so growing the preceding XMP overflows 32-bit MPF offsets.
  const uint8_t kMpfHeader[] = {'M', 'P', 'F', 0x00};
  auto mpf_it =
      std::search(jpeg.begin(), jpeg.end(), std::begin(kMpfHeader),
                  std::end(kMpfHeader));
  ASSERT_TRUE(mpf_it != jpeg.end());
  size_t mpf_header_pos = static_cast<size_t>(mpf_it - jpeg.begin());
  size_t mpf_endian_pos = mpf_header_pos + 4;
  photos_editing_formats::image_io::MpfInfoDecoder decoder(nullptr);
  ASSERT_TRUE(decoder.Decode(jpeg.data() + mpf_endian_pos,
                             jpeg.size() - mpf_endian_pos));
  photos_editing_formats::image_io::MpfInfo mpf_info = decoder.GetInfo();
  mpf_info.entries.size();
  ASSERT_EQ(mpf_info.entries.size(), 2u);
  const uint32_t orig_offset = mpf_info.entries[1].image_offset;
  const uint8_t orig_bytes[4] = {
      static_cast<uint8_t>((orig_offset >> 24) & 0xFF),
      static_cast<uint8_t>((orig_offset >> 16) & 0xFF),
      static_cast<uint8_t>((orig_offset >> 8) & 0xFF),
      static_cast<uint8_t>(orig_offset & 0xFF),
  };
  auto search_end =
      jpeg.begin() + static_cast<std::ptrdiff_t>(
                         std::min(jpeg.size(), mpf_endian_pos + 128));
  auto off_it = std::search(
      jpeg.begin() + static_cast<std::ptrdiff_t>(mpf_endian_pos), search_end,
      std::begin(orig_bytes), std::end(orig_bytes));
  ASSERT_TRUE(off_it != search_end);
  off_it[0] = 0xFF;
  off_it[1] = 0xFF;
  off_it[2] = 0xFF;
  off_it[3] = 0xF0;

  MotionPhotoJpegImageBuilder builder(nullptr);
  std::vector<uint8_t> out;
  auto mem_src = MediaSource::FromMemory(jpeg.data(), jpeg.size());
  auto mem_sink = DataSink::ToMemory(&out);
  EXPECT_FALSE(builder.Build(*mem_src, large_motion_xmp, mem_sink.get()));
}

}  // namespace motion_photo
}  // namespace libmotionphoto
