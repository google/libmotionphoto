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

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>

#include "motion_photo/tests/test_framework.h"
#include "motion_photo/video_track_data.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

namespace libmotionphoto {
namespace motion_photo {

namespace fs = std::filesystem;
using std::string;

namespace {
string CreateTestDir(const std::string& subdir_name) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  fs::path test_dir = base_dir / subdir_name;
  fs::create_directories(test_dir);
  return test_dir.string();
}
}  // namespace

// Ensures the extractor shows help if no args are given.
TEST(MotionPhotoExtractorFunctionTest, NoArgs) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoExtractor"};
  int status = MotionPhotoExtractorFunction(1, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures the extractor shows help if -h is given.
TEST(MotionPhotoExtractorFunctionTest, DashH) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoExtractor", "-h"};
  int status = MotionPhotoExtractorFunction(2, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures the extractor shows help if bad args are given.
TEST(MotionPhotoExtractorFunctionTest, BadArgs) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoExtractor", "-j"};
  int status = MotionPhotoExtractorFunction(2, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures extraction works for JPEG motion photo.
TEST(MotionPhotoExtractorFunctionTest, JpegExtraction) {
  const string kTestDir = CreateTestDir("JpegExtraction");
  const string kInputFile = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  const string kOutputFileImage = (fs::path(kTestDir) / "extracted_still.jpg").string();
  const string kOutputFileVideo = (fs::path(kTestDir) / "extracted_video.mp4").string();
  const string kOutputFileMetadata = (fs::path(kTestDir) / "extracted_metadata.xml").string();

  const char* argv[] = {"MotionPhotoExtractor",
                        "-mi", kInputFile.c_str(),
                        "-po", kOutputFileImage.c_str(),
                        "-vo", kOutputFileVideo.c_str(),
                        "-mo", kOutputFileMetadata.c_str()};

  bool has_usage = false;
  bool has_zero_errors = false;
  int status = MotionPhotoExtractorFunction(9, argv, [&](absl::string_view str) {
    std::cout << str;
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
  });

  EXPECT_EQ(status, 0);
  EXPECT_FALSE(has_usage);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kOutputFileImage));
  EXPECT_TRUE(fs::exists(kOutputFileVideo));
  EXPECT_TRUE(fs::exists(kOutputFileMetadata));

  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutputFileVideo, 0));
  EXPECT_NE(mp4_reader.GetVideoTrackData().high_res_metadata_track_id,
            kInvalidTrackId);

  fs::remove_all(kTestDir);
}

// Ensures video-only extraction works for HEIC motion photo (doesn't need codecs).
TEST(MotionPhotoExtractorFunctionTest, HeicVideoOnlyExtraction) {
  const string kTestDir = CreateTestDir("HeicVideoOnlyExtraction");
  const string kInputFile = "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  const string kOutputFileVideo = (fs::path(kTestDir) / "extracted_video.mp4").string();

  const char* argv[] = {"MotionPhotoExtractor",
                        "-mi", kInputFile.c_str(),
                        "-vo", kOutputFileVideo.c_str()};

  bool has_usage = false;
  bool has_zero_errors = false;
  int status = MotionPhotoExtractorFunction(5, argv, [&](absl::string_view str) {
    std::cout << str;
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
  });

  EXPECT_EQ(status, 0);
  EXPECT_FALSE(has_usage);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kOutputFileVideo));

  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutputFileVideo, 0));
  EXPECT_EQ(mp4_reader.GetVideoTrackData().high_res_metadata_track_id,
            kInvalidTrackId);

  fs::remove_all(kTestDir);
}

// Ensures extraction works for single-image JPEG motion photo.
TEST(MotionPhotoExtractorFunctionTest, SingleImageJpegExtraction) {
  const string kTestDir = CreateTestDir("SingleImageJpegExtraction");
  const string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_single_image.MP.jpg";
  const string kOutputFileImage =
      (fs::path(kTestDir) / "extracted_still.jpg").string();
  const string kOutputFileVideo =
      (fs::path(kTestDir) / "extracted_video.mp4").string();
  const string kOutputFileMetadata =
      (fs::path(kTestDir) / "extracted_metadata.xml").string();

  const char* argv[] = {
      "MotionPhotoExtractor",     "-mi", kInputFile.c_str(),       "-po",
      kOutputFileImage.c_str(),   "-vo", kOutputFileVideo.c_str(), "-mo",
      kOutputFileMetadata.c_str()};

  bool has_usage = false;
  bool has_zero_errors = false;
  int status =
      MotionPhotoExtractorFunction(9, argv, [&](absl::string_view str) {
        std::cout << str;
        if (absl::StrContains(str, "Usage")) {
          has_usage = true;
        }
        if (absl::StrContains(str, "0 errors")) {
          has_zero_errors = true;
        }
      });

  EXPECT_EQ(status, 0);
  EXPECT_FALSE(has_usage);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kOutputFileImage));
  EXPECT_TRUE(fs::exists(kOutputFileVideo));
  EXPECT_TRUE(fs::exists(kOutputFileMetadata));

  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutputFileVideo, 0));

  fs::remove_all(kTestDir);
}

// Ensures extractor rejects still-only images that contain no motion photo
// payload.
TEST(MotionPhotoExtractorFunctionTest, StillOnlyImageRejection) {
  const string kTestDir = CreateTestDir("StillOnlyImageRejection");
  const string kStillJpg =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const string kDummyVideo = (fs::path(kTestDir) / "dummy.mp4").string();

  const char* argv[] = {"MotionPhotoExtractor", "-mi", kStillJpg.c_str(), "-vo",
                        kDummyVideo.c_str()};

  int status = MotionPhotoExtractorFunction(5, argv, [](absl::string_view) {});
  EXPECT_NE(status, 0);
  EXPECT_FALSE(fs::exists(kDummyVideo));

  fs::remove_all(kTestDir);
}

// Ensures extractor succeeds directly without running strict MotionPhotoChecker
// validation rules (e.g. when GCamera:MotionPhotoVersion is "2" instead of "1",
// which MotionPhotoChecker rejects with an error).
TEST(MotionPhotoExtractorFunctionTest, ExtractionBypassesCheckerValidation) {
  const string kTestDir = CreateTestDir("ExtractionBypassesCheckerValidation");
  const string kSrcJpg =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  const string kModifiedJpg =
      (fs::path(kTestDir) / "non_conforming_version.jpg").string();
  const string kOutputFileImage =
      (fs::path(kTestDir) / "extracted_still.jpg").string();
  const string kOutputFileVideo =
      (fs::path(kTestDir) / "extracted_video.mp4").string();

  std::ifstream in(kSrcJpg, std::ios::binary);
  string bytes((std::istreambuf_iterator<char>(in)),
               std::istreambuf_iterator<char>());
  const string kTarget = "MotionPhotoVersion=\"1\"";
  const string kReplacement = "MotionPhotoVersion=\"2\"";
  size_t pos = bytes.find(kTarget);
  EXPECT_NE(pos, string::npos);
  if (pos != string::npos) {
    bytes.replace(pos, kTarget.size(), kReplacement);
  }
  {
    std::ofstream out(kModifiedJpg, std::ios::binary);
    out.write(bytes.data(), bytes.size());
  }

  const char* argv[] = {"MotionPhotoExtractor",     "-mi",
                        kModifiedJpg.c_str(),       "-po",
                        kOutputFileImage.c_str(),   "-vo",
                        kOutputFileVideo.c_str()};

  bool has_zero_errors = false;
  int status = MotionPhotoExtractorFunction(7, argv, [&](absl::string_view str) {
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
  });

  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_zero_errors);
  EXPECT_TRUE(fs::exists(kOutputFileImage));
  EXPECT_TRUE(fs::exists(kOutputFileVideo));

  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutputFileVideo, 0));

  fs::remove_all(kTestDir);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
