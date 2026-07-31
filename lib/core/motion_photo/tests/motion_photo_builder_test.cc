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

#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/tests/test_framework.h"
#include "motion_photo_builder/function.h"
#include "motion_photo_checker/function.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

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

  // 4. Assert AGTM dynamic metadata is preserved identically
  MetadataEngine engine(nullptr);
  std::string expected_agtm = engine.ExtractAgtmAtTimestamp(kInputVideo, 500000);
  EXPECT_FALSE(expected_agtm.empty());

  std::string extracted_agtm = engine.ExtractAgtmAtTimestamp(kFileOut, 500000);
  EXPECT_FALSE(extracted_agtm.empty());
  EXPECT_EQ(expected_agtm, extracted_agtm);

  fs::remove_all(kTestDir);
}

// -----------------------------------------------------------------------------
// 8. DataSink::TransferFrom Single-Shot and Sequential Chunk Continuity
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

}  // namespace motion_photo
}  // namespace libmotionphoto
