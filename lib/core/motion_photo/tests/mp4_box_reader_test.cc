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

#include "motion_photo_mp4/mp4_box_reader.h"

#include <fstream>
#include <string>
#include <vector>

#include "motion_photo/agtm.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

TEST(Mp4BoxReaderTest, ParseMp4VideoOnly) {
  const std::string kFile = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  Mp4BoxReader reader;
  EXPECT_TRUE(reader.ParseFromFile(kFile, 0));
  EXPECT_EQ(reader.GetTrackCount(), 2);

  const auto& video_track = reader.GetTrack(0);
  EXPECT_EQ(video_track.track_id, 1);
  EXPECT_EQ(video_track.handler_type, "vide");
  EXPECT_EQ(video_track.sample_count, 186);

  const auto& audio_track = reader.GetTrack(1);
  EXPECT_EQ(audio_track.track_id, 2);
  EXPECT_EQ(audio_track.handler_type, "soun");
  EXPECT_EQ(audio_track.sample_count, 133);
}

TEST(Mp4BoxReaderTest, ParseMp4VideoWithHighResAndMetadata) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  Mp4BoxReader reader;
  EXPECT_TRUE(reader.ParseFromFile(kFile, 0));
  EXPECT_EQ(reader.GetTrackCount(), 4);

  const Mp4TrackInfo* meta_track = reader.FindTrackById(4);
  ASSERT_NE(meta_track, nullptr);
  EXPECT_EQ(meta_track->handler_type, "meta");
  EXPECT_EQ(meta_track->sample_count, 1);
  EXPECT_TRUE(Mp4BoxReader::TrackHasMettMime(*meta_track, kMotionPhotoMetadataMime));

  std::vector<uint8_t> sample;
  EXPECT_TRUE(reader.ReadSample(4, 1, &sample));
  EXPECT_EQ(sample.size(), 101);
}

TEST(Mp4BoxReaderTest, ParseJpegMotionPhotoAtOffset) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kFile, 30895));

  const auto& track_data = mp4_reader.GetVideoTrackData();
  EXPECT_EQ(track_data.video_track_count, 2);
  EXPECT_EQ(track_data.audio_track_count, 1);
  EXPECT_EQ(track_data.metadata_track_count, 2);
  EXPECT_EQ(track_data.low_res_video_track_id, 1);
  EXPECT_EQ(track_data.low_res_audio_track_id, 2);
  EXPECT_EQ(track_data.high_res_video_track_id, 3);
  EXPECT_EQ(track_data.high_res_metadata_track_id, 4);

  const auto& meta_bytes = mp4_reader.GetHighResTrackVideoMetadata();
  EXPECT_EQ(meta_bytes.size(), 101);
}

TEST(Mp4BoxReaderTest, ParseHeicMotionPhotoAtOffset) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  MotionPhotoMp4FileReader mp4_reader;
  // mpvd box header is 16 bytes at offset 28727, so MP4 starts at 28743
  EXPECT_TRUE(mp4_reader.Read(kFile, 28743));

  const auto& track_data = mp4_reader.GetVideoTrackData();
  EXPECT_EQ(track_data.video_track_count, 2);
  EXPECT_EQ(track_data.audio_track_count, 1);
  EXPECT_EQ(track_data.metadata_track_count, 2);
  EXPECT_EQ(track_data.high_res_metadata_track_id, 4);

  const auto& meta_bytes = mp4_reader.GetHighResTrackVideoMetadata();
  EXPECT_EQ(meta_bytes.size(), 101);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromRealAgtmMotionPhoto) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("hdrReferenceWhite"), std::string::npos);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromMemoryRealAgtmMotionPhoto) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  std::ifstream ifs(kFile, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(ifs.is_open());
  size_t size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  ifs.read(reinterpret_cast<char*>(buffer.data()), size);

  MetadataEngine engine(nullptr);
  std::string json =
      engine.ExtractAgtmAtTimestampMemory(buffer.data(), buffer.size(), 0);
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("hdrReferenceWhite"), std::string::npos);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromNonAgtmPhotoReturnsEmpty) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_no_agtm.MP.jpg";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_TRUE(json.empty());
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromNonAgtmVideoWithMetadataReturnsEmpty) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_TRUE(json.empty());
}

TEST(Mp4BoxReaderTest, ParseAgtmPayloadDirectBitstream) {
  // 1. Raw default ST 2094-50 2-byte bitstream (app_version=0, min_app_version=0, no tone map)
  std::vector<uint8_t> default_bytes = {0x00, 0x00};
  AgtmDynamicMetadata meta;
  EXPECT_TRUE(ParseAgtmPayload(default_bytes.data(), default_bytes.size(), &meta));
  EXPECT_EQ(meta.hdr_reference_white, 203.0f);
  EXPECT_FALSE(meta.has_adaptive_tone_map_flag);

  // 2. Wrapped with ITU-T T.35 header
  std::vector<uint8_t> t35_bytes = {0x05, 0xB5, 0x00, 0x90, 0x00, 0x01, 0x00, 0x00};
  EXPECT_TRUE(ParseAgtmPayload(t35_bytes.data(), t35_bytes.size(), &meta));
  EXPECT_EQ(meta.hdr_reference_white, 203.0f);

  // 3. Invalid payload (random garbage without T.35 header and invalid version)
  std::vector<uint8_t> invalid_bytes = {0xFF, 0xFF, 0x12, 0x34};
  EXPECT_FALSE(ParseAgtmPayload(invalid_bytes.data(), invalid_bytes.size(), &meta));
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
