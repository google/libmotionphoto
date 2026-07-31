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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "motion_photo/tests/test_framework.h"
#include "motion_photo_checker/function.h"

namespace libmotionphoto {
namespace motion_photo {

namespace fs = std::filesystem;

// Ensures the motion_photo_checker function shows help if no args are given.
TEST(MotionPhotoCheckerFunctionTest, NoArgs) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoChecker"};
  int status = MotionPhotoCheckerFunction(1, argv, [&](const std::string& str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures the motion_photo_checker function shows help if -h is given.
TEST(MotionPhotoCheckerFunctionTest, DashH) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoChecker", "-h"};
  int status = MotionPhotoCheckerFunction(2, argv, [&](const std::string& str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures the motion_photo_checker function shows help if bad args are given.
TEST(MotionPhotoCheckerFunctionTest, BadArgs) {
  bool has_usage = false;
  const char* argv[] = {"MotionPhotoChecker", "junk.jpg", "junk.jpg"};
  int status = MotionPhotoCheckerFunction(3, argv, [&](const std::string& str) {
    if (absl::StrContains(str, "Usage")) {
      has_usage = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_usage);
}

// Ensures the motion_photo_checker function operates with a non-motion photo.
TEST(MotionPhotoCheckerFunctionTest, NonMotionPhoto) {
  const std::string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  bool has_zero_errors = false;
  bool has_zero_warnings = false;
  bool has_input_file_size = false;
  bool motion_photo_checker_finished = false;
  const char* argv[] = {"MotionPhotoChecker", kInputFile.c_str()};
  int status = MotionPhotoCheckerFunction(2, argv, [&](const std::string& str) {
    std::cout << str;
    if (absl::StrContains(str, "...Input file size:")) {
      has_input_file_size = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
    if (absl::StrContains(str, "0 warnings")) {
      has_zero_warnings = true;
    }
    if (absl::StrContains(str, "Motion Photo Checker finished")) {
      motion_photo_checker_finished = true;
    }
  });
  EXPECT_NE(status, 0);
  EXPECT_TRUE(has_input_file_size);
  EXPECT_FALSE(has_zero_errors);
  EXPECT_TRUE(has_zero_warnings);
  EXPECT_TRUE(motion_photo_checker_finished);
}

// Ensures the motion_photo_checker function operates with a motion photo.
TEST(MotionPhotoCheckerFunctionTest, MotionPhoto) {
  // Use the single video track version we have.
  const std::string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  bool has_zero_errors = false;
  bool has_zero_warnings = false;
  bool has_input_file_size = false;
  bool motion_photo_checker_finished = false;
  const char* argv[] = {"MotionPhotoChecker", kInputFile.c_str()};
  int status = MotionPhotoCheckerFunction(2, argv, [&](const std::string& str) {
    std::cout << str;
    if (absl::StrContains(str, "...Input file size:")) {
      has_input_file_size = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
    if (absl::StrContains(str, "0 warnings")) {
      has_zero_warnings = true;
    }
    if (absl::StrContains(str, "Motion Photo Checker finished")) {
      motion_photo_checker_finished = true;
    }
  });
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_input_file_size);
  EXPECT_TRUE(has_zero_errors);
  // It might have warnings (e.g. filename pattern warning because it doesn't
  // start with IMG_). So we don't expect 0 warnings strictly, or we expect
  // warnings. The official test expected 0 warnings for IMG_000_MP.jpg because
  // it matched pattern. Our file is motion_photo_single_video_track.MP.jpg, so
  // it will have filename warning.
  EXPECT_FALSE(has_zero_warnings);
  EXPECT_TRUE(motion_photo_checker_finished);
}

#if 1  // Assuming HEIC is supported
// Ensures the motion_photo_checker function operates with a HEIC motion photo.
TEST(MotionPhotoCheckerFunctionTest, HeicBasedMotionPhoto) {
  const std::string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  bool has_zero_errors = false;
  bool has_zero_warnings = false;
  bool has_input_file_size = false;
  bool motion_photo_checker_finished = false;
  const char* argv[] = {"MotionPhotoChecker", kInputFile.c_str()};
  int status = MotionPhotoCheckerFunction(2, argv, [&](const std::string& str) {
    std::cout << str;
    if (absl::StrContains(str, "...Input file size:")) {
      has_input_file_size = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
    if (absl::StrContains(str, "0 warnings")) {
      has_zero_warnings = true;
    }
    if (absl::StrContains(str, "Motion Photo Checker finished")) {
      motion_photo_checker_finished = true;
    }
  });
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_input_file_size);
  EXPECT_TRUE(has_zero_errors);
  // Will have filename warning.
  EXPECT_FALSE(has_zero_warnings);
  EXPECT_TRUE(motion_photo_checker_finished);
}
#endif

// Ensures the motion_photo_checker function operates with a single-image JPEG
// motion photo.
TEST(MotionPhotoCheckerFunctionTest, SingleImageJpeg) {
  const std::string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_single_image.MP.jpg";
  bool has_zero_errors = false;
  bool has_zero_warnings = false;
  bool has_input_file_size = false;
  bool motion_photo_checker_finished = false;
  const char* argv[] = {"MotionPhotoChecker", kInputFile.c_str()};
  int status = MotionPhotoCheckerFunction(2, argv, [&](const std::string& str) {
    std::cout << str;
    if (absl::StrContains(str, "...Input file size:")) {
      has_input_file_size = true;
    }
    if (absl::StrContains(str, "0 errors")) {
      has_zero_errors = true;
    }
    if (absl::StrContains(str, "0 warnings")) {
      has_zero_warnings = true;
    }
    if (absl::StrContains(str, "Motion Photo Checker finished")) {
      motion_photo_checker_finished = true;
    }
  });
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(has_input_file_size);
  EXPECT_TRUE(has_zero_errors);
  // Will have filename warning.
  EXPECT_FALSE(has_zero_warnings);
  EXPECT_TRUE(motion_photo_checker_finished);
}

// Ensures still-only images are correctly identified as non-motion photos.
TEST(MotionPhotoCheckerFunctionTest, StillOnlyImageRejection) {
  const std::string kStillJpg =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.jpg";
  const std::string kStillHeic =
      "lib/core/motion_photo/testdata/motion_photo_photo_only.heic";

  // Checker should return non-zero for still-only JPEG.
  int check_jpg_status = CheckMotionPhoto(kStillJpg, [](absl::string_view) {});
  EXPECT_NE(check_jpg_status, 0);

  // Checker should return non-zero for still-only HEIC.
  int check_heic_status =
      CheckMotionPhoto(kStillHeic, [](absl::string_view) {});
  EXPECT_NE(check_heic_status, 0);
}

// Ensures corrupted and non-existent files are handled gracefully without
// crashing.
TEST(MotionPhotoCheckerFunctionTest, CorruptAndInvalidInputResiliency) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  fs::path base_dir = (test_tmpdir != nullptr && test_tmpdir[0] != '\0')
                          ? fs::path(test_tmpdir)
                          : fs::temp_directory_path();
  const std::string kNonExistent = (base_dir / "does_not_exist.jpg").string();
  const std::string kCorrupted = (base_dir / "corrupted_garbage.jpg").string();

  // Create a synthetic corrupted file.
  std::ofstream out(kCorrupted, std::ios::binary);
  std::vector<char> garbage(512, '\xFF');
  out.write(garbage.data(), garbage.size());
  out.close();

  int check_non_existent =
      CheckMotionPhoto(kNonExistent, [](absl::string_view) {});
  EXPECT_NE(check_non_existent, 0);

  int check_corrupted = CheckMotionPhoto(kCorrupted, [](absl::string_view) {});
  EXPECT_NE(check_corrupted, 0);

  fs::remove(kCorrupted);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
