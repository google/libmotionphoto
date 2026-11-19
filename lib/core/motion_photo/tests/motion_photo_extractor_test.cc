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

#include "libmotionphoto_api.h"
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

// Ensures full extraction (still image + video + metadata) works for HEIC
// motion photo without requiring libheif or Android media codecs.
TEST(MotionPhotoExtractorFunctionTest, HeicFullExtraction) {
  const string kTestDir = CreateTestDir("HeicFullExtraction");
  const string kInputFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  const string kOutputFileImage =
      (fs::path(kTestDir) / "extracted_still.heic").string();
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

  // Verify extracted still image size matches the exact byte offset of mpvd box
  // (input_size - video_length - 16-byte extended mpvd header) and that the
  // extracted still image has its MotionPhoto XMP tags stripped (so it is no
  // longer detected as a Motion Photo).
  api::MotionPhotoMetadata metadata;
  EXPECT_TRUE(api::ParseMotionPhotoFromFile(kInputFile, &metadata));
  EXPECT_EQ(fs::file_size(kOutputFileImage),
            fs::file_size(kInputFile) -
                static_cast<size_t>(metadata.video_length) - 16u);
  EXPECT_FALSE(api::ParseMotionPhotoFromFile(kOutputFileImage, nullptr));

  // Verify extracted video is a valid MP4 with its metadata track stripped.
  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutputFileVideo, 0));
  EXPECT_EQ(mp4_reader.GetVideoTrackData().high_res_metadata_track_id,
            kInvalidTrackId);

  // Verify extracted metadata contains the original XMP payload.
  std::ifstream meta_in(kOutputFileMetadata, std::ios::binary);
  string meta_str((std::istreambuf_iterator<char>(meta_in)),
                  std::istreambuf_iterator<char>());
  EXPECT_TRUE(absl::StrContains(meta_str, "MotionPhoto"));

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

// Ensures multi-extent Primary XMP in HEIC is extracted cleanly and stripped.
TEST(MotionPhotoExtractorFunctionTest, ExtractsMultiExtentHeicMotionPhoto) {
  const string kTestDir = CreateTestDir("ExtractsMultiExtentHeicMotionPhoto");
  const string kVideoSrc = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  std::ifstream vin(kVideoSrc, std::ios::binary);
  std::vector<uint8_t> video_bytes((std::istreambuf_iterator<char>(vin)),
                                   std::istreambuf_iterator<char>());
  ASSERT_FALSE(video_bytes.empty());

  auto put = [](std::vector<uint8_t>* out, uint64_t val, size_t n) {
    for (size_t i = n; i > 0; --i) {
      out->push_back(static_cast<uint8_t>((val >> (8 * (i - 1))) & 0xFF));
    }
  };
  auto put_str = [](std::vector<uint8_t>* out, std::string_view s) {
    out->insert(out->end(), s.begin(), s.end());
  };
  auto make_box = [&](std::string_view type, const std::vector<uint8_t>& p) {
    std::vector<uint8_t> b;
    put(&b, 8 + p.size(), 4);
    put_str(&b, type);
    b.insert(b.end(), p.begin(), p.end());
    return b;
  };
  auto make_full_box = [&](std::string_view type, uint8_t ver,
                           const std::vector<uint8_t>& p) {
    std::vector<uint8_t> fp;
    fp.push_back(ver);
    put(&fp, 0, 3);
    fp.insert(fp.end(), p.begin(), p.end());
    return make_box(type, fp);
  };

  const std::string ext1 =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
      "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description "
      "xmlns:Camera=\"http://ns.google.com/photos/1.0/camera/\" "
      "xmlns:Container=\"http://ns.google.com/photos/1.0/container/\" "
      "xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\" "
      "Camera:MotionPhoto=\"1\" Camera:MotionPhotoVersion=\"1\" "
      "Camera:MotionPhotoPresentationTimestampUs=\"0\">"
      "<Container:Directory><rdf:Seq>";
  const std::string img_gap = "DUMMY_HEVC_PRIMARY_PIXELS";
  const std::string ext2 =
      "<rdf:li><Container:Item Item:Semantic=\"Primary\" Item:Mime=\"image/heic\" "
      "Item:Padding=\"8\"/></rdf:li>"
      "<rdf:li><Container:Item Item:Semantic=\"MotionPhoto\" "
      "Item:Mime=\"video/mp4\" Item:Length=\"" +
      std::to_string(video_bytes.size()) +
      "\"/></rdf:li></rdf:Seq></Container:Directory>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>";

  std::vector<uint8_t> mdat_p;
  put_str(&mdat_p, ext1);
  put_str(&mdat_p, img_gap);
  put_str(&mdat_p, ext2);
  std::vector<uint8_t> mpvd = make_box("mpvd", video_bytes);

  auto build = [&](uint32_t mdat_data_off) {
    std::vector<uint8_t> ftyp_p;
    put_str(&ftyp_p, "heic");
    put(&ftyp_p, 0, 4);
    put_str(&ftyp_p, "mif1heic");
    std::vector<uint8_t> hdlr_p;
    put(&hdlr_p, 0, 4);
    put_str(&hdlr_p, "pict");
    put(&hdlr_p, 0, 13);
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
    // Item 1 (primary image in gap)
    put(&iloc_p, 1, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 1, 2);
    put(&iloc_p, mdat_data_off + ext1.size(), 4);
    put(&iloc_p, img_gap.size(), 4);
    // Item 2 (multi-extent XMP)
    put(&iloc_p, 2, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 0, 2);
    put(&iloc_p, 2, 2);
    put(&iloc_p, mdat_data_off, 4);
    put(&iloc_p, ext1.size(), 4);
    put(&iloc_p, mdat_data_off + ext1.size() + img_gap.size(), 4);
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
    out.insert(out.end(), mpvd.begin(), mpvd.end());
    return out;
  };
  std::vector<uint8_t> probe = build(0);
  uint32_t mdat_off =
      static_cast<uint32_t>(probe.size() - mpvd.size() - mdat_p.size());
  std::vector<uint8_t> synth = build(mdat_off);

  const string kInputHeic = (fs::path(kTestDir) / "multi_ext.MP.heic").string();
  const string kOutStill = (fs::path(kTestDir) / "still.heic").string();
  const string kOutVideo = (fs::path(kTestDir) / "video.mp4").string();
  const string kOutMeta = (fs::path(kTestDir) / "meta.xml").string();
  {
    std::ofstream out(kInputHeic, std::ios::binary);
    out.write(reinterpret_cast<const char*>(synth.data()), synth.size());
  }

  const char* argv[] = {"MotionPhotoExtractor", "-mi", kInputHeic.c_str(),
                        "-po", kOutStill.c_str(), "-vo", kOutVideo.c_str(),
                        "-mo", kOutMeta.c_str()};
  int status = MotionPhotoExtractorFunction(9, argv, [](absl::string_view) {});
  EXPECT_EQ(status, 0);
  EXPECT_TRUE(fs::exists(kOutStill));
  EXPECT_TRUE(fs::exists(kOutVideo));
  EXPECT_TRUE(fs::exists(kOutMeta));

  {
    std::ifstream min(kOutMeta, std::ios::binary);
    std::string meta_str((std::istreambuf_iterator<char>(min)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(meta_str, ext1 + ext2);
  }
  {
    std::ifstream sin(kOutStill, std::ios::binary);
    std::string still_str((std::istreambuf_iterator<char>(sin)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(still_str.find("MotionPhoto"), std::string::npos);
    EXPECT_EQ(still_str.find("mpvd"), std::string::npos);
    EXPECT_NE(still_str.find(img_gap), std::string::npos);
  }
  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kOutVideo, 0));

  fs::remove_all(kTestDir);
}

}  // namespace motion_photo
}  // namespace libmotionphoto
